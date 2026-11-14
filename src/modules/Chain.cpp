// CHAIN: Markov melody voices with harmony rules (spec §7). The melody engine
// is in dsp/ChainCore; this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/ChainLayout.hpp"
#include "dsp/ChainCore.hpp"
#include "common/Trigger.hpp"
#include "common/KeepHost.hpp"
#include "common/StateSwap.hpp"
#include "common/Theme.hpp"
#include <atomic>
#include <cstring>

using namespace codex;
namespace L = layout_chain;

static const char* const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct Chain : Module, Keepable {
	enum ParamId {
		SCALE_PARAM, ROOT_PARAM, ORDER_PARAM, GLIDE_PARAM,
		ENUMS(CHARACTER_PARAM, 4), ENUMS(RANGE_PARAM, 4), ENUMS(CENTER_PARAM, 4),
		RULE_SCALE_PARAM, RULE_PARALLEL_PARAM, RULE_INTERVAL_PARAM, RULE_CONTRARY_PARAM, RULE_LEAP_PARAM, RULE_HOME_PARAM,
		LEAP_PARAM, HOME_STEPS_PARAM, SEED_PARAM,
		KEEP_PARAM, RECALL_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(STEP_INPUT, 4), SCALE_INPUT, ROOT_INPUT, LEARN_INPUT, LEARN_PITCH_INPUT, CHARACTER_INPUT, RESET_INPUT,
		SEED_INPUT, KEEP_INPUT, RECALL_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENUMS(PITCH_OUTPUT, 4), POLY_OUTPUT, ENUMS(GATE_OUTPUT, 4), VIOLATION_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(SLOT_LIGHT, 8), LIGHTS_LEN };

	static const int KEPT = SEED_PARAM + 1; // the knobs KEEP stores
	static const int VOICES = ChainCore::VOICES;

	// The melody state as the UI thread sees it (screen, saving, KEEP).
	struct Snap {
		ChainVoice voice[VOICES];
		float active[VOICES][12][12] = {}; // the table each voice is using
		uint16_t scale = 0xAB5;
		int root = 0;
	};
	typedef float Order2[VOICES][144][12];
	// A whole state handed from the UI thread to the audio thread (patch load, or a recalled slot).
	struct Load {
		ChainVoice voice[VOICES];
		Order2 order2;
		bool hasParams = false;
		float param[KEPT];
		bool hasOptions = false;
		int refVoice = 0, intervals = 0;
		bool gateOnChange = false;
	};
	// A hand edit made on the screen or in the menu, queued for the audio thread.
	struct Edit {
		enum Type : uint8_t { PAINT, RESET_MATRIX, RESET_ALL };
		uint8_t type, voice, from, to;
		float value;
	};

	ChainCore core;
	ChainParams cp;
	SchmittIn stepIn;
	dsp::SchmittTrigger resetIn;
	AdaptiveTrigger gate; // channels 0..3 = voices, 4 = violation
	dsp::RingBuffer<Edit, 512> edits;
	int controlDivider = 0;
	bool needsInit = true;
	bool learning = false;
	float outVolts[VOICES] = {};
	float slew[VOICES] = {};
	float glideSeconds = 0.f;

	// right-click options
	std::atomic<int> refVoice{0};
	std::atomic<int> intervals{(1 << 3) | (1 << 4) | (1 << 8) | (1 << 9)};
	std::atomic<bool> gateOnChange{false};
	std::atomic<int> selected{0}; // the voice shown on the screen, and the one LEARN teaches

	KeepHost keep;
	StateSwap<Load> loadNow;    // applied at once
	StateSwap<Load> loadAtStep; // applied at the next step
	Load* loaded = nullptr;

	// Copies for the UI thread. The audio thread refreshes them; it only ever tries the lock.
	TryLock snapLock;
	Snap snap;
	Order2 snapOrder2;
	uint32_t publishedVersion = 0xFFFFFFFFu, publishedOrder2 = 0xFFFFFFFFu;

	Chain() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> scales;
		for (int i = 0; i < 10; i++)
			scales.push_back(ChainCore::SCALE_NAMES[i]);
		configSwitch(SCALE_PARAM, 0.f, 9.f, 1.f, "Scale (ignored while the SCALE input is patched)", scales);
		std::vector<std::string> notes;
		for (int i = 0; i < 12; i++)
			notes.push_back(NOTE_NAMES[i]);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root note", notes);
		configSwitch(ORDER_PARAM, 0.f, 1.f, 0.f, "Memory", {"1 note back", "2 notes back (only for learned melodies; otherwise the same as 1)"});
		configParam(GLIDE_PARAM, 0.f, 0.5f, 0.f, "Glide between notes", " ms", 0.f, 1000.f);
		static const float defChar[4] = {0.f, 0.25f, 0.5f, 0.75f};
		for (int v = 0; v < 4; v++) {
			configParam(CHARACTER_PARAM + v, 0.f, 1.f, defChar[v], string::f("Voice %d character (0 moves by step, middle likes thirds and fifths, 1 leaps)", v + 1), " %", 0.f, 100.f);
			configParam(RANGE_PARAM + v, 1.f, 4.f, 2.f, string::f("Voice %d range", v + 1), " octaves");
			paramQuantities[RANGE_PARAM + v]->snapEnabled = true;
			configParam(CENTER_PARAM + v, -3.f, 3.f, 0.f, string::f("Voice %d centre of its range (0 = middle C)", v + 1), " octaves");
		}
		configParam(RULE_SCALE_PARAM, 0.f, 1.f, 1.f, "Rule: stay in the scale", " %", 0.f, 100.f);
		configParam(RULE_PARALLEL_PARAM, 0.f, 1.f, 0.f, "Rule: no parallel fifths or octaves between voices that move together", " %", 0.f, 100.f);
		configParam(RULE_INTERVAL_PARAM, 0.f, 1.f, 0.f, "Rule: keep a third or sixth from the reference voice (voice and intervals: right-click)", " %", 0.f, 100.f);
		configParam(RULE_CONTRARY_PARAM, 0.f, 1.f, 0.f, "Rule: move the opposite way to the reference voice's last move", " %", 0.f, 100.f);
		configParam(RULE_LEAP_PARAM, 0.f, 1.f, 0.f, "Rule: no leap bigger than SEMIS", " %", 0.f, 100.f);
		configParam(RULE_HOME_PARAM, 0.f, 1.f, 0.f, "Rule: pull back to the root, harder the longer it has been away", " %", 0.f, 100.f);
		configParam(LEAP_PARAM, 2.f, 24.f, 7.f, "Biggest leap allowed by MAX LEAP", " semitones");
		paramQuantities[LEAP_PARAM]->snapEnabled = true;
		configParam(HOME_STEPS_PARAM, 4.f, 64.f, 16.f, "Steps away from the root after which HOME is certain to pull back", " steps");
		paramQuantities[HOME_STEPS_PARAM]->snapEnabled = true;
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (same seed, same melody; a new seed starts at the next step)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configButton(KEEP_PARAM, "Keep the melody tables, positions and knobs in the next free slot");
		configParam(RECALL_PARAM, 0.f, 8.f, 0.f, "Slot to recall (0 = none). A trigger at the RECALL jack recalls it at the next step");
		paramQuantities[RECALL_PARAM]->snapEnabled = true;

		for (int v = 0; v < 4; v++) {
			configInput(STEP_INPUT + v, string::f("Step trigger for voice %d%s", v + 1, v == 0 ? " (drives all four when it is the only one patched)" : ""));
			configOutput(PITCH_OUTPUT + v, string::f("Voice %d pitch (1 V/oct)", v + 1));
			configOutput(GATE_OUTPUT + v, string::f("Voice %d gate (10 ms on each step)", v + 1));
		}
		configInput(SCALE_INPUT, "Scale (poly: every pitch on the cable is an allowed note, in any octave; replaces the SCALE knob)");
		configInput(ROOT_INPUT, "Root CV (1 V/oct, added to ROOT)");
		configInput(LEARN_INPUT, "Learn gate: while high, the voice selected on the screen learns the melody at PITCH");
		configInput(LEARN_PITCH_INPUT, "Melody to learn (1 V/oct, read at each of the voice's steps)");
		configInput(CHARACTER_INPUT, "Character CV for all voices (10 V = full range)");
		configInput(RESET_INPUT, "Reset: back to the root, and the same melody again");
		configInput(SEED_INPUT, "Seed CV (0 to 10 V = 0 to 999)");
		configInput(KEEP_INPUT, "Keep trigger");
		configInput(RECALL_INPUT, "Recall trigger");
		configOutput(POLY_OUTPUT, "All four pitches (poly)");
		configOutput(VIOLATION_OUTPUT, "Trigger when no note could satisfy the rules");

		std::memset(snapOrder2, 0, sizeof(snapOrder2));
		core.refreshDefaults(cp);
		for (int v = 0; v < VOICES; v++) {
			snap.voice[v] = core.voice[v];
			std::memcpy(snap.active[v], core.activeMatrix(v), sizeof(snap.active[v]));
		}
	}

	~Chain() { delete loaded; }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		refVoice = 0;
		intervals = (1 << 3) | (1 << 4) | (1 << 8) | (1 << 9);
		gateOnChange = false;
		selected = 0;
		pushEdit(Edit::RESET_ALL, 0, 0, 0, 0.f);
		needsInit = true;
	}

	void readControls() {
		cp.root = ((int)std::lround(params[ROOT_PARAM].getValue()) + (int)std::lround(inputs[ROOT_INPUT].getVoltage() * 12.f)) % 12;
		if (cp.root < 0)
			cp.root += 12;
		if (inputs[SCALE_INPUT].isConnected()) {
			// every pitch on the cable is an allowed note; turn them into "semitones above the root"
			uint16_t m = 0;
			for (int c = 0; c < inputs[SCALE_INPUT].getChannels(); c++) {
				int pc = (int)std::lround(inputs[SCALE_INPUT].getVoltage(c) * 12.f) - cp.root;
				pc = ((pc % 12) + 12) % 12;
				m = (uint16_t)(m | (1 << pc));
			}
			cp.scale = m;
		}
		else {
			cp.scale = ChainCore::SCALES[clamp((int)std::lround(params[SCALE_PARAM].getValue()), 0, 9)];
		}
		cp.order = params[ORDER_PARAM].getValue() > 0.5f ? 2 : 1;
		float charCv = inputs[CHARACTER_INPUT].getVoltage() * 0.1f;
		for (int v = 0; v < VOICES; v++) {
			// rounded so that CV noise does not rebuild the tables all the time
			cp.character[v] = std::round(clamp(params[CHARACTER_PARAM + v].getValue() + charCv, 0.f, 1.f) * 200.f) / 200.f;
			cp.range[v] = (int)std::lround(params[RANGE_PARAM + v].getValue());
			cp.center[v] = params[CENTER_PARAM + v].getValue();
		}
		cp.strength[ChainParams::SCALE_LOCK] = params[RULE_SCALE_PARAM].getValue();
		cp.strength[ChainParams::NO_PARALLELS] = params[RULE_PARALLEL_PARAM].getValue();
		cp.strength[ChainParams::KEEP_INTERVAL] = params[RULE_INTERVAL_PARAM].getValue();
		cp.strength[ChainParams::CONTRARY] = params[RULE_CONTRARY_PARAM].getValue();
		cp.strength[ChainParams::MAX_LEAP] = params[RULE_LEAP_PARAM].getValue();
		cp.strength[ChainParams::HOME_PULL] = params[RULE_HOME_PARAM].getValue();
		cp.leap = (int)std::lround(params[LEAP_PARAM].getValue());
		cp.homeSteps = (int)std::lround(params[HOME_STEPS_PARAM].getValue());
		cp.refVoice = refVoice.load(std::memory_order_relaxed);
		cp.intervals = (uint16_t)intervals.load(std::memory_order_relaxed);
		glideSeconds = params[GLIDE_PARAM].getValue();
		core.setSeed(SeedLatch::read(params[SEED_PARAM].getValue(), inputs[SEED_INPUT].getVoltage(), inputs[SEED_INPUT].isConnected()));

		keep.process(params[KEEP_PARAM].getValue() > 0.5f, inputs[KEEP_INPUT].getVoltage(), params[RECALL_PARAM].getValue(), inputs[RECALL_INPUT].getVoltage());
		for (int i = 0; i < 8; i++)
			lights[SLOT_LIGHT + i].setBrightness(keep.light(i));

		while (!edits.empty()) {
			Edit e = edits.shift();
			switch (e.type) {
				case Edit::PAINT: core.paint(e.voice, e.from, e.to, e.value); break;
				case Edit::RESET_MATRIX: core.resetMatrix(e.voice); break;
				case Edit::RESET_ALL:
					for (int v = 0; v < VOICES; v++)
						core.resetMatrix(v);
					break;
				default: break;
			}
		}
		core.refreshDefaults(cp);
		publish();
	}

	// Refresh the UI thread's copies when something has changed.
	void publish() {
		if (core.version == publishedVersion && core.order2Version == publishedOrder2)
			return;
		if (snapLock.tryLock()) {
			for (int v = 0; v < VOICES; v++) {
				snap.voice[v] = core.voice[v];
				std::memcpy(snap.active[v], core.activeMatrix(v), sizeof(snap.active[v]));
			}
			snap.scale = cp.scale;
			snap.root = cp.root;
			if (core.order2Version != publishedOrder2) {
				std::memcpy(snapOrder2, core.order2, sizeof(snapOrder2));
				publishedOrder2 = core.order2Version;
			}
			publishedVersion = core.version;
			snapLock.unlock();
		}
	}

	void applyLoad(const Load& l) {
		for (int v = 0; v < VOICES; v++)
			core.voice[v] = l.voice[v];
		std::memcpy(core.order2, l.order2, sizeof(core.order2));
		core.version++;
		core.order2Version++;
		if (l.hasParams)
			for (int i = 0; i < KEPT; i++)
				params[i].setValue(l.param[i]);
		if (l.hasOptions) {
			refVoice = l.refVoice;
			intervals = l.intervals;
			gateOnChange = l.gateOnChange;
		}
		for (int v = 0; v < VOICES; v++) {
			outVolts[v] = (float)core.voice[v].note / 12.f;
			slew[v] = 0.f;
		}
	}

	void process(const ProcessArgs& args) override {
		// a state from the patch file goes in straight away
		Load* next = loadNow.take(loaded);
		bool justLoaded = false;
		if (next != loaded) {
			loaded = next;
			applyLoad(*loaded);
			justLoaded = true;
			needsInit = false;
			controlDivider = 0;
		}
		if (controlDivider == 0)
			readControls();
		controlDivider = (controlDivider + 1) & 15;

		bool resetNow = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		if ((needsInit || resetNow) && !justLoaded) {
			needsInit = false;
			readControls();
			core.reset(cp);
			for (int v = 0; v < VOICES; v++) {
				outVolts[v] = (float)core.voice[v].note / 12.f;
				slew[v] = 0.f;
			}
		}

		// which voices step now? STEP 1 alone drives all four
		bool only1 = !inputs[STEP_INPUT + 1].isConnected() && !inputs[STEP_INPUT + 2].isConnected() && !inputs[STEP_INPUT + 3].isConnected();
		bool stepNow[VOICES];
		bool trig0 = stepIn.process(0, inputs[STEP_INPUT].getVoltage());
		bool any = trig0;
		stepNow[0] = trig0;
		for (int v = 1; v < VOICES; v++) {
			bool t = stepIn.process(v, inputs[STEP_INPUT + v].getVoltage());
			stepNow[v] = only1 ? trig0 : t;
			any = any || stepNow[v];
		}

		// learning
		int sel = clamp(selected.load(std::memory_order_relaxed), 0, VOICES - 1);
		bool learnHigh = inputs[LEARN_INPUT].getVoltage() >= 1.f;
		if (learning && !learnHigh)
			for (int v = 0; v < VOICES; v++)
				core.stopLearning(v);
		learning = learnHigh;

		if (any) {
			// a recalled slot takes over at a step
			Load* recalled = loadAtStep.take(loaded);
			if (recalled != loaded) {
				loaded = recalled;
				applyLoad(*loaded);
				readControls();
			}
			int learnPc = -1;
			if (learnHigh) {
				learnPc = (int)std::lround(inputs[LEARN_PITCH_INPUT].getVoltage() * 12.f) - cp.root;
				learnPc = ((learnPc % 12) + 12) % 12;
			}
			ChainOut out;
			core.step(cp, stepNow, learnHigh ? sel : -1, learnPc, out);
			bool onlyChanges = gateOnChange.load(std::memory_order_relaxed);
			for (int v = 0; v < VOICES; v++) {
				if (!out.stepped[v])
					continue;
				if (out.changed[v] || !onlyChanges)
					gate.fire(v, 0.01f);
				float target = (float)core.voice[v].note / 12.f;
				if (glideSeconds > 0.0005f)
					slew[v] = std::fabs(target - outVolts[v]) / (glideSeconds * args.sampleRate);
				else
					outVolts[v] = target;
			}
			if (out.violation)
				gate.fire(4);
		}

		outputs[POLY_OUTPUT].setChannels(VOICES);
		for (int v = 0; v < VOICES; v++) {
			float target = (float)core.voice[v].note / 12.f;
			if (outVolts[v] < target)
				outVolts[v] = std::min(target, outVolts[v] + slew[v]);
			else if (outVolts[v] > target)
				outVolts[v] = std::max(target, outVolts[v] - slew[v]);
			outputs[PITCH_OUTPUT + v].setVoltage(outVolts[v]);
			outputs[POLY_OUTPUT].setVoltage(outVolts[v], v);
			outputs[GATE_OUTPUT + v].setVoltage(gate.process(v, args.sampleTime));
		}
		outputs[VIOLATION_OUTPUT].setVoltage(gate.process(4, args.sampleTime));
	}

	void pushEdit(uint8_t type, int voice, int from, int to, float value) {
		if (!edits.full())
			edits.push(Edit{type, (uint8_t)voice, (uint8_t)from, (uint8_t)to, value});
	}

	// ---- saving and KEEP (UI thread) ----
	json_t* stateToJson() {
		Snap* s = new Snap;
		std::vector<float> o2(VOICES * 144 * 12);
		snapLock.lock();
		*s = snap;
		std::memcpy(o2.data(), snapOrder2, sizeof(Order2));
		snapLock.unlock();
		json_t* arr = json_array();
		for (int v = 0; v < VOICES; v++) {
			const ChainVoice& vc = s->voice[v];
			json_t* jv = json_object();
			json_object_set_new(jv, "note", json_integer(vc.note));
			json_object_set_new(jv, "prevPc", json_integer(vc.prevPc));
			json_object_set_new(jv, "lastMove", json_integer(vc.lastMove));
			json_object_set_new(jv, "sinceRoot", json_integer(vc.sinceRoot));
			json_t* rng = json_array();
			for (int i = 0; i < 4; i++)
				json_array_append_new(rng, json_integer(vc.rng.s[i]));
			json_object_set_new(jv, "rng", rng);
			if (vc.custom) {
				json_t* m = json_array();
				for (int i = 0; i < 12; i++)
					for (int j = 0; j < 12; j++)
						json_array_append_new(m, json_real(vc.matrix[i][j]));
				json_object_set_new(jv, "matrix", m);
				json_object_set_new(jv, "learnUnit", json_real(vc.learnUnit));
			}
			if (vc.hasOrder2) {
				// only the rows that hold something: [row, 12 counts]
				json_t* rows = json_array();
				for (int r = 0; r < 144; r++) {
					bool used = false;
					for (int j = 0; j < 12; j++)
						used = used || o2[(size_t)((v * 144 + r) * 12 + j)] > 0.f;
					if (!used)
						continue;
					json_t* row = json_array();
					json_array_append_new(row, json_integer(r));
					for (int j = 0; j < 12; j++)
						json_array_append_new(row, json_real(o2[(size_t)((v * 144 + r) * 12 + j)]));
					json_array_append_new(rows, row);
				}
				json_object_set_new(jv, "order2", rows);
			}
			json_array_append_new(arr, jv);
		}
		delete s;
		return arr;
	}

	Load* loadFromJson(json_t* arr) {
		Load* l = new Load;
		snapLock.lock();
		for (int v = 0; v < VOICES; v++)
			l->voice[v] = snap.voice[v]; // anything missing from the JSON stays as it is now
		snapLock.unlock();
		std::memset(l->order2, 0, sizeof(l->order2));
		for (int v = 0; v < VOICES; v++) {
			json_t* jv = arr ? json_array_get(arr, v) : nullptr;
			if (!jv)
				continue;
			ChainVoice& vc = l->voice[v];
			json_t* j;
			if ((j = json_object_get(jv, "note")))
				vc.note = clamp((int)json_integer_value(j), -60, 60);
			if ((j = json_object_get(jv, "prevPc")))
				vc.prevPc = clamp((int)json_integer_value(j), -1, 11);
			if ((j = json_object_get(jv, "lastMove")))
				vc.lastMove = (int)json_integer_value(j);
			if ((j = json_object_get(jv, "sinceRoot")))
				vc.sinceRoot = std::max(0, (int)json_integer_value(j));
			json_t* rng = json_object_get(jv, "rng");
			if (rng && json_array_size(rng) == 4) {
				uint32_t all = 0;
				for (int i = 0; i < 4; i++) {
					vc.rng.s[i] = (uint32_t)json_integer_value(json_array_get(rng, i));
					all |= vc.rng.s[i];
				}
				if (all == 0)
					vc.rng.s[0] = 1;
			}
			json_t* m = json_object_get(jv, "matrix");
			vc.custom = m && json_array_size(m) == 144;
			vc.learnPrev = vc.learnPrev2 = -1;
			vc.learnUnit = 1.f;
			if (vc.custom) {
				for (int i = 0; i < 144; i++)
					vc.matrix[i / 12][i % 12] = clamp((float)json_number_value(json_array_get(m, i)), 0.f, 1.f);
				if ((j = json_object_get(jv, "learnUnit")))
					vc.learnUnit = clamp((float)json_number_value(j), 1e-6f, 1.f);
			}
			vc.hasOrder2 = false;
			json_t* rows = json_object_get(jv, "order2");
			if (rows && json_is_array(rows)) {
				size_t n = json_array_size(rows);
				for (size_t k = 0; k < n; k++) {
					json_t* row = json_array_get(rows, k);
					if (!row || json_array_size(row) != 13)
						continue;
					int r = (int)json_integer_value(json_array_get(row, 0));
					if (r < 0 || r >= 144)
						continue;
					for (int jn = 0; jn < 12; jn++)
						l->order2[v][r][jn] = std::max(0.f, (float)json_number_value(json_array_get(row, jn + 1)));
					vc.hasOrder2 = true;
				}
			}
		}
		return l;
	}

	json_t* captureState() override {
		json_t* j = json_object();
		json_object_set_new(j, "voices", stateToJson());
		json_t* p = json_array();
		for (int i = 0; i < KEPT; i++)
			json_array_append_new(p, json_real(params[i].getValue()));
		json_object_set_new(j, "params", p);
		json_object_set_new(j, "refVoice", json_integer(refVoice));
		json_object_set_new(j, "intervals", json_integer(intervals));
		json_object_set_new(j, "gateOnChange", json_boolean(gateOnChange));
		return j;
	}

	void requestRestore(json_t* j) override {
		Load* l = loadFromJson(json_object_get(j, "voices"));
		json_t* p = json_object_get(j, "params");
		if (p && json_is_array(p)) {
			l->hasParams = true;
			for (int i = 0; i < KEPT; i++) {
				json_t* v = json_array_get(p, i);
				l->param[i] = v ? (float)json_number_value(v) : params[i].getValue();
			}
		}
		json_t* rv = json_object_get(j, "refVoice");
		json_t* iv = json_object_get(j, "intervals");
		json_t* gc = json_object_get(j, "gateOnChange");
		l->hasOptions = true;
		l->refVoice = rv ? clamp((int)json_integer_value(rv), 0, 3) : refVoice.load();
		l->intervals = iv ? ((int)json_integer_value(iv) & 0xFFF) : intervals.load();
		l->gateOnChange = gc ? json_is_true(gc) : gateOnChange.load();
		json_decref(j);
		loadAtStep.offer(l);
	}

	std::string keepLabel() override { return string::f("CHAIN #%lld", (long long)id); }

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "voices", stateToJson());
		json_object_set_new(root, "refVoice", json_integer(refVoice));
		json_object_set_new(root, "intervals", json_integer(intervals));
		json_object_set_new(root, "gateOnChange", json_boolean(gateOnChange));
		json_object_set_new(root, "selected", json_integer(selected));
		json_object_set_new(root, "keepSlots", keep.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* rv = json_object_get(root, "refVoice");
		json_t* iv = json_object_get(root, "intervals");
		json_t* gc = json_object_get(root, "gateOnChange");
		json_t* sl = json_object_get(root, "selected");
		if (rv)
			refVoice = clamp((int)json_integer_value(rv), 0, 3);
		if (iv)
			intervals = (int)json_integer_value(iv) & 0xFFF;
		if (gc)
			gateOnChange = json_is_true(gc);
		if (sl)
			selected = clamp((int)json_integer_value(sl), 0, 3);
		json_t* voices = json_object_get(root, "voices");
		if (voices) {
			Load* l = loadFromJson(voices);
			// keep the UI's copy in step even if the audio engine is not running yet
			snapLock.lock();
			for (int v = 0; v < VOICES; v++) {
				snap.voice[v] = l->voice[v];
				if (l->voice[v].custom)
					std::memcpy(snap.active[v], l->voice[v].matrix, sizeof(snap.active[v]));
			}
			std::memcpy(snapOrder2, l->order2, sizeof(snapOrder2));
			snapLock.unlock();
			loadNow.offer(l);
		}
		keep.fromJson(json_object_get(root, "keepSlots"));
	}
};

// The screen.
//   left     voice numbers: click one to show its table (and to choose who LEARN teaches)
//   middle   the table: each row is "after this note", each column "comes that note";
//            brighter = more likely. Click or drag to paint cells full or empty.
//   right    the last 32 notes of all four voices, newest at the right
struct ChainDisplay : OpaqueWidget {
	Chain* module = nullptr;
	Chain::Snap* snap = nullptr;
	float paintValue = 1.f;
	bool painting = false;
	Vec dragPos;
	int lastCell = -1;

	static constexpr float SEL_W = 16.f, LABEL_W = 12.f, TOP = 9.f;

	ChainDisplay() { snap = new Chain::Snap; }
	~ChainDisplay() { delete snap; }

	float cell() const { return (box.size.y - TOP - 1.f) / 12.f; }
	float gridX() const { return SEL_W + LABEL_W; }

	void step() override {
		if (module) {
			module->snapLock.lock();
			*snap = module->snap;
			module->snapLock.unlock();
		}
		OpaqueWidget::step();
	}

	static NVGcolor voiceColor(int v) {
		switch (v) {
			case 0: return theme::time();
			case 1: return theme::text();
			case 2: return theme::sound();
			default: return theme::system();
		}
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, theme::screen());
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			nvgScissor(vg, 0, 0, box.size.x, box.size.y);
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font)
				nvgFontFaceId(vg, font->handle);
			int sel = module ? clamp(module->selected.load(), 0, 3) : 0;
			NVGcolor amber = theme::time();
			if (!module) {
				// module browser: show a typical table
				ChainCore::characterMatrix(snap->active[0], 0.f, snap->scale);
			}

			// voice numbers
			float bh = box.size.y / 4.f;
			for (int v = 0; v < 4; v++) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, 1.5f, v * bh + 1.5f, SEL_W - 3.f, bh - 3.f, 2.f);
				if (v == sel) {
					nvgFillColor(vg, voiceColor(v));
					nvgFill(vg);
				}
				else {
					nvgStrokeColor(vg, nvgTransRGBAf(voiceColor(v), 0.6f));
					nvgStrokeWidth(vg, 1.f);
					nvgStroke(vg);
				}
				nvgFontSize(vg, 11.f);
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, v == sel ? theme::screen() : voiceColor(v));
				nvgText(vg, SEL_W * 0.5f, v * bh + bh * 0.5f + 0.5f, string::f("%d", v + 1).c_str(), NULL);
				// a dot when this voice's table was learned or painted
				if (snap->voice[v].custom) {
					nvgBeginPath(vg);
					nvgCircle(vg, SEL_W - 4.f, v * bh + 5.f, 1.3f);
					nvgFillColor(vg, v == sel ? theme::screen() : theme::text());
					nvgFill(vg);
				}
			}

			// the table
			float c = cell(), gx = gridX();
			const float (*m)[12] = snap->active[sel];
			float mx = 1e-6f;
			for (int i = 0; i < 12; i++)
				for (int j = 0; j < 12; j++)
					mx = std::max(mx, m[i][j]);
			int cur = ((snap->voice[sel].note - snap->root) % 12 + 12) % 12;
			nvgFontSize(vg, 7.f);
			for (int i = 0; i < 12; i++) {
				bool in = (snap->scale >> i) & 1;
				const char* name = NOTE_NAMES[(i + snap->root) % 12];
				nvgFillColor(vg, in ? (i == cur ? amber : theme::text()) : theme::dim());
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
				nvgText(vg, gx - 1.5f, TOP + (i + 0.5f) * c, name, NULL);
				nvgFillColor(vg, in ? theme::text() : theme::dim());
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
				nvgText(vg, gx + (i + 0.5f) * c, TOP - 0.5f, name, NULL);
			}
			for (int i = 0; i < 12; i++)
				for (int j = 0; j < 12; j++) {
					float w = clamp(m[i][j] / mx, 0.f, 1.f);
					bool in = (snap->scale >> j) & 1;
					nvgBeginPath(vg);
					nvgRect(vg, gx + j * c + 0.5f, TOP + i * c + 0.5f, c - 1.f, c - 1.f);
					// notes outside the scale are shown dim: SCALE LOCK keeps the voice off them
					NVGcolor col = nvgLerpRGBA(theme::grid(), voiceColor(sel), w * (in ? 1.f : 0.3f));
					nvgFillColor(vg, col);
					nvgFill(vg);
				}
			// the row it is reading now
			nvgBeginPath(vg);
			nvgRect(vg, gx, TOP + cur * c, 12.f * c, c);
			nvgStrokeColor(vg, theme::text());
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);

			// the last 32 notes
			float rx = gx + 12.f * c + 6.f, rw = box.size.x - rx - 3.f;
			float ry = 4.f, rh = box.size.y - 8.f;
			int lo = 127, hi = -127;
			for (int v = 0; v < 4; v++) {
				const ChainVoice& vc = snap->voice[v];
				for (int k = 0; k < vc.histCount; k++) {
					int n = vc.history[(vc.histHead - 1 - k) & 31];
					lo = std::min(lo, n);
					hi = std::max(hi, n);
				}
			}
			if (lo > hi) {
				lo = -6;
				hi = 6;
			}
			if (hi - lo < 12) {
				int pad = (12 - (hi - lo) + 1) / 2;
				lo -= pad;
				hi += pad;
			}
			nvgBeginPath(vg);
			nvgRect(vg, rx - 2.f, 1.f, rw + 4.f, box.size.y - 2.f);
			nvgStrokeColor(vg, theme::grid());
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			float sw = rw / 32.f, nh = std::max(1.5f, std::min(4.f, rh / (float)(hi - lo + 1)));
			for (int v = 0; v < 4; v++) {
				const ChainVoice& vc = snap->voice[v];
				nvgBeginPath(vg);
				for (int k = 0; k < vc.histCount; k++) {
					int n = vc.history[(vc.histHead - 1 - k) & 31];
					float x = rx + rw - (float)(k + 1) * sw;
					float y = ry + rh - (float)(n - lo + 1) / (float)(hi - lo + 1) * rh;
					nvgRect(vg, x + 0.3f, y, sw - 0.6f, nh);
				}
				nvgFillColor(vg, nvgTransRGBAf(voiceColor(v), v == sel ? 1.f : 0.55f));
				nvgFill(vg);
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}

	// which table cell is at this position, as from * 12 + to, or -1
	int cellAt(Vec pos) const {
		float c = cell();
		float fx = (pos.x - gridX()) / c, fy = (pos.y - TOP) / c;
		if (fx < 0.f || fy < 0.f || fx >= 12.f || fy >= 12.f)
			return -1;
		return (int)fy * 12 + (int)fx;
	}

	void paintAt(Vec pos) {
		int k = cellAt(pos);
		if (k < 0 || k == lastCell)
			return;
		lastCell = k;
		module->pushEdit(Chain::Edit::PAINT, clamp(module->selected.load(), 0, 3), k / 12, k % 12, paintValue);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) {
			OpaqueWidget::onButton(e);
			return;
		}
		if (e.pos.x < SEL_W) {
			module->selected = clamp((int)(e.pos.y / (box.size.y / 4.f)), 0, 3);
			e.consume(this);
			return;
		}
		int k = cellAt(e.pos);
		if (k >= 0) {
			// a dim cell is painted full, a bright one is emptied; dragging carries on with the same
			int sel = clamp(module->selected.load(), 0, 3);
			const float (*m)[12] = snap->active[sel];
			float mx = 1e-6f;
			for (int i = 0; i < 12; i++)
				for (int j = 0; j < 12; j++)
					mx = std::max(mx, m[i][j]);
			paintValue = m[k / 12][k % 12] / mx > 0.5f ? 0.f : 1.f;
			painting = true;
			dragPos = e.pos;
			lastCell = -1;
			paintAt(e.pos);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || !painting)
			return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		paintAt(dragPos);
	}

	void onDragEnd(const DragEndEvent& e) override { painting = false; }
};

struct ChainWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	ChainWidget(Chain* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Chain.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		ChainDisplay* display = new ChainDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		for (int v = 0; v < 4; v++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::CHARACTER[v]), module, Chain::CHARACTER_PARAM + v));
			addParam(createParamCentered<Trimpot>(at(L::RANGE[v]), module, Chain::RANGE_PARAM + v));
			addParam(createParamCentered<Trimpot>(at(L::CENTER[v]), module, Chain::CENTER_PARAM + v));
			addInput(createInputCentered<PJ301MPort>(at(L::STEP_IN[v]), module, Chain::STEP_INPUT + v));
			addOutput(createOutputCentered<PJ301MPort>(at(L::PITCH_OUT[v]), module, Chain::PITCH_OUTPUT + v));
			addOutput(createOutputCentered<PJ301MPort>(at(L::GATE_OUT[v]), module, Chain::GATE_OUTPUT + v));
		}
		addParam(createParamCentered<RoundBlackKnob>(at(L::SCALE), module, Chain::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::ROOT), module, Chain::ROOT_PARAM));
		addParam(createParamCentered<CKSS>(at(L::ORDER), module, Chain::ORDER_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::GLIDE), module, Chain::GLIDE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Chain::SEED_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::KEEP), module, Chain::KEEP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RECALL), module, Chain::RECALL_PARAM));
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<SmallLight<YellowLight>>(at(L::SLOT_LIGHTS[i]), module, Chain::SLOT_LIGHT + i));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RULE_SCALE), module, Chain::RULE_SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RULE_PARALLEL), module, Chain::RULE_PARALLEL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RULE_INTERVAL), module, Chain::RULE_INTERVAL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RULE_CONTRARY), module, Chain::RULE_CONTRARY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RULE_LEAP), module, Chain::RULE_LEAP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::LEAP), module, Chain::LEAP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RULE_HOME), module, Chain::RULE_HOME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::HOME_STEPS), module, Chain::HOME_STEPS_PARAM));

		addInput(createInputCentered<PJ301MPort>(at(L::SCALE_IN), module, Chain::SCALE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::ROOT_IN), module, Chain::ROOT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::LEARN_IN), module, Chain::LEARN_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::LEARN_PITCH_IN), module, Chain::LEARN_PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::CHARACTER_IN), module, Chain::CHARACTER_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RESET_IN), module, Chain::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SEED_IN), module, Chain::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::KEEP_IN), module, Chain::KEEP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RECALL_IN), module, Chain::RECALL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::POLY_OUT), module, Chain::POLY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::VIOLATION_OUT), module, Chain::VIOLATION_OUTPUT));
	}

	void step() override {
		Chain* m = getModule<Chain>();
		if (m) {
			m->keep.step(m);
			m->loadNow.collect();
			m->loadAtStep.collect();
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Chain* m = getModule<Chain>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Reference voice for INTVL and CONTRA", string::f("voice %d", m->refVoice.load() + 1), [=](Menu* sub) {
			for (int v = 0; v < 4; v++)
				sub->addChild(createCheckMenuItem(string::f("Voice %d", v + 1), "", [=]() { return m->refVoice.load() == v; }, [=]() { m->refVoice = v; }));
		}));
		menu->addChild(createSubmenuItem("Distances INTVL allows to the reference voice", "", [=](Menu* sub) {
			static const char* const names[12] = {"Unison / octave", "Minor second", "Major second", "Minor third", "Major third",
				"Fourth", "Tritone", "Fifth", "Minor sixth", "Major sixth", "Minor seventh", "Major seventh"};
			for (int i = 0; i < 12; i++)
				sub->addChild(createCheckMenuItem(names[i], "", [=]() { return (m->intervals.load() >> i) & 1; },
					[=]() { m->intervals = m->intervals.load() ^ (1 << i); }));
		}));
		menu->addChild(createCheckMenuItem("Gates only when the note changes (repeated notes tie)", "",
			[=]() { return m->gateOnChange.load(); }, [=]() { m->gateOnChange = !m->gateOnChange.load(); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem(string::f("Reset voice %d's table to its CHARACTER (forgets learning and painting)", m->selected.load() + 1), "",
			[=]() { m->pushEdit(Chain::Edit::RESET_MATRIX, m->selected.load(), 0, 0, 0.f); }));
		menu->addChild(createMenuItem("Reset all four tables", "", [=]() { m->pushEdit(Chain::Edit::RESET_ALL, 0, 0, 0, 0.f); }));
		m->keep.appendMenu(menu, m);
	}
};

Model* modelChain = createModel<Chain, ChainWidget>("Chain");
