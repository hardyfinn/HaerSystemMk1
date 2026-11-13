// MUTATE: step drums that rewrite themselves (spec §5). The sequencer is in
// dsp/MutateCore; this file is the Rack module, its grid screen and menus.
#include "plugin.hpp"
#include "modules/MutateLayout.hpp"
#include "dsp/MutateCore.hpp"
#include "common/Phase.hpp"
#include "common/Trigger.hpp"
#include "common/KeepHost.hpp"
#include "common/StateSwap.hpp"
#include "common/Theme.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_mutate;

static const char* const STYLE_NAME[STYLE_COUNT] = {"Kick", "Snare", "Hat", "Perc", "Texture", "Free"};
static const char STYLE_LETTER[STYLE_COUNT] = {'K', 'S', 'H', 'P', 'T', 'F'};

// ---- patterns to and from JSON (UI thread) ----
static json_t* patternToJson(const MutatePattern& p) {
	json_t* tracks = json_array();
	for (int t = 0; t < 8; t++) {
		const MutateTrack& tr = p.track[t];
		json_t* jt = json_object();
		json_object_set_new(jt, "length", json_integer(tr.length));
		json_object_set_new(jt, "style", json_integer(tr.style));
		json_object_set_new(jt, "mute", json_boolean(tr.mute));
		json_t* steps = json_array();
		for (int i = 0; i < tr.length; i++) {
			const MutateStep& s = tr.step[i];
			// on, velocity, ratchet, micro, condition, chance, every-N, lock A, lock B
			json_array_append_new(steps, json_pack("[i, f, i, f, i, f, i, f, f]", s.on ? 1 : 0, (double)s.vel, (int)s.ratchet,
				(double)s.micro, (int)s.cond, (double)s.p, (int)s.n, (double)s.lockA, (double)s.lockB));
		}
		json_object_set_new(jt, "steps", steps);
		json_array_append_new(tracks, jt);
	}
	return tracks;
}

static bool patternFromJson(json_t* tracks, MutatePattern& p) {
	if (!tracks || !json_is_array(tracks))
		return false;
	for (int t = 0; t < 8; t++) {
		json_t* jt = json_array_get(tracks, t);
		if (!jt)
			continue;
		MutateTrack& tr = p.track[t];
		tr.length = clamp((int)json_integer_value(json_object_get(jt, "length")), 1, (int)MutateTrack::MAX_STEPS);
		tr.style = clamp((int)json_integer_value(json_object_get(jt, "style")), 0, STYLE_COUNT - 1);
		tr.mute = json_is_true(json_object_get(jt, "mute"));
		json_t* steps = json_object_get(jt, "steps");
		for (int i = 0; i < MutateTrack::MAX_STEPS; i++) {
			MutateStep s;
			json_t* js = steps ? json_array_get(steps, i) : nullptr;
			if (js && json_array_size(js) >= 9) {
				s.on = json_integer_value(json_array_get(js, 0)) != 0;
				s.vel = clamp((float)json_number_value(json_array_get(js, 1)), 0.f, 1.f);
				s.ratchet = (uint8_t)clamp((int)json_integer_value(json_array_get(js, 2)), 1, 8);
				s.micro = clamp((float)json_number_value(json_array_get(js, 3)), -0.5f, 0.5f);
				s.cond = (uint8_t)clamp((int)json_integer_value(json_array_get(js, 4)), 0, COND_COUNT - 1);
				s.p = clamp((float)json_number_value(json_array_get(js, 5)), 0.f, 1.f);
				s.n = (uint8_t)clamp((int)json_integer_value(json_array_get(js, 6)), 2, 8);
				s.lockA = clamp((float)json_number_value(json_array_get(js, 7)), 0.f, 1.f);
				s.lockB = clamp((float)json_number_value(json_array_get(js, 8)), 0.f, 1.f);
			}
			tr.step[i] = s;
		}
	}
	return true;
}

struct Mutate : Module, Keepable {
	enum ParamId {
		STEPS_PARAM, DENSITY_PARAM, SEED_PARAM, EVERY_PARAM, AMOUNT_PARAM, DRIFT_PARAM,
		ENUMS(RULE_PARAM, RULE_COUNT), CURVE_PARAM, DECAY_PARAM, SWING_PARAM,
		GENERATE_PARAM, HOME_PARAM, HOME_SET_PARAM, KEEP_PARAM, RECALL_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		PHASE_INPUT, CLOCK_INPUT, RESET_INPUT, GENERATE_INPUT, MUTATE_INPUT, HOME_INPUT, DENSITY_INPUT, AMOUNT_INPUT, DRIFT_INPUT,
		MICRO_INPUT, SEED_INPUT, KEEP_INPUT, RECALL_INPUT, INPUTS_LEN
	};
	enum OutputId { ENUMS(GATE_OUTPUT, 8), GATES_OUTPUT, VEL_OUTPUT, LOCK_A_OUTPUT, LOCK_B_OUTPUT, ACCENT_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(SLOT_LIGHT, 8), LIGHTS_LEN };

	static const int KEPT = SWING_PARAM + 1; // the knobs KEEP stores

	// A whole pattern handed from the UI thread to the audio thread (patch load, or a recalled slot).
	struct Load {
		MutatePattern pat, home;
		bool hasParams = false;
		float param[KEPT];
	};
	// A hand edit made on the screen, queued for the audio thread.
	struct Edit {
		enum Type : uint8_t { STEP_ON, STEP_VEL, LENGTH, MUTE, STYLE, REGEN_TRACK };
		uint8_t type, track, step;
		float value;
	};

	MutateCore core;
	MutateParams mp;
	PhaseReader reader;
	AdaptiveTrigger trig; // channels 0..7 = tracks, 8 = accent
	dsp::SchmittTrigger clockIn, resetIn, generateIn, mutateIn, homeIn;
	dsp::BooleanTrigger generateButton, homeButton, homeSetButton;
	dsp::RingBuffer<Edit, 256> edits;
	int controlDivider = 0;
	bool phaseWasConnected = false;
	bool needsInit = true;
	// clock mode: one clock pulse = one step
	int64_t clockStep = -1;
	float clockPeriod = 6000.f, sinceClock = 0.f;

	KeepHost keep;
	StateSwap<Load> loadNow;  // applied at once
	StateSwap<Load> loadAtBar; // applied on the next bar line
	Load* loaded = nullptr;

	// A copy of the pattern for the screen and for saving. The audio thread refreshes it; it only tries the lock.
	TryLock snapLock;
	MutatePattern snapPat, snapHome;
	uint32_t snapVersion = 0;         // version of the copy (read by UI under the lock)
	uint32_t publishedVersion = 0;    // audio thread's note of what it last copied
	std::atomic<int64_t> dispStep{-1};
	std::atomic<int> dispSteps{16};

	Mutate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(STEPS_PARAM, 4.f, 32.f, 16.f, "Steps per bar");
		paramQuantities[STEPS_PARAM]->snapEnabled = true;
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density (how busy a generated pattern is; press GEN to hear a change)", " %", 0.f, 100.f);
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (same seed and density = same pattern and same mutations)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configParam(EVERY_PARAM, 1.f, 16.f, 2.f, "Mutate every", " bars");
		paramQuantities[EVERY_PARAM]->snapEnabled = true;
		configParam(AMOUNT_PARAM, 0.f, 1.f, 0.f, "Mutation amount (0 = pattern never changes, 100 % = four changes each time)", " %", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.5f, "Drift (0 = every change heads back home, 50 % = wanders round home, 100 % = wanders freely)", " %", 0.f, 100.f);
		static const char* const ruleName[RULE_COUNT] = {
			"ROTATE: shift a track one step", "NUDGE: push a hit early or late", "RATCHET: turn a hit into a roll, or back",
			"THIN/FILL: remove a hit or add one", "MIRROR: reverse a short section", "COPY: copy a section from another track",
			"LOCK: walk the sound locks of a few steps"};
		for (int k = 0; k < RULE_COUNT; k++)
			configParam(RULE_PARAM + k, 0.f, 1.f, 0.5f, string::f("How often %s", ruleName[k]), " %", 0.f, 100.f);
		configParam(CURVE_PARAM, -1.f, 1.f, 0.f, "Ratchet curve (left: rolls speed up, centre: even, right: rolls slow down)");
		configParam(DECAY_PARAM, 0.f, 1.f, 0.8f, "Ratchet decay (how much quieter each hit of a roll is; 100 % = no fade)", " %", 0.f, 100.f);
		configParam(SWING_PARAM, 0.f, 0.5f, 0.f, "Swing (delays every second step)", " % of a step", 0.f, 100.f);
		configButton(GENERATE_PARAM, "Generate a new pattern from SEED, DENS and each track's style");
		configButton(HOME_PARAM, "Go back to the home pattern at the next bar");
		configButton(HOME_SET_PARAM, "Make the pattern playing now the new home");
		configButton(KEEP_PARAM, "Keep the pattern, its home and the settings in the next free slot");
		configParam(RECALL_PARAM, 0.f, 8.f, 0.f, "Slot to recall (0 = none). A trigger at the RECALL jack recalls it at the next bar");
		paramQuantities[RECALL_PARAM]->snapEnabled = true;

		configInput(PHASE_INPUT, "Phase (0 to 10 V ramp from WARP, one cycle = one bar)");
		configInput(CLOCK_INPUT, "Clock (one pulse = one step; used when PHASE is not patched)");
		configInput(RESET_INPUT, "Reset trigger");
		configInput(GENERATE_INPUT, "Generate trigger");
		configInput(MUTATE_INPUT, "Mutate trigger (one extra round of changes at the next bar)");
		configInput(HOME_INPUT, "Home trigger");
		configInput(DENSITY_INPUT, "Density CV (±5 V)");
		configInput(AMOUNT_INPUT, "Mutation amount CV (±5 V)");
		configInput(DRIFT_INPUT, "Drift CV (±5 V)");
		configInput(MICRO_INPUT, "Micro-timing curve (±5 V shifts every hit by up to half a step)");
		configInput(SEED_INPUT, "Seed CV (0 to 10 V = 0 to 999)");
		configInput(KEEP_INPUT, "Keep trigger");
		configInput(RECALL_INPUT, "Recall trigger");
		for (int t = 0; t < 8; t++)
			configOutput(GATE_OUTPUT + t, string::f("Track %d trigger", t + 1));
		configOutput(GATES_OUTPUT, "All 8 triggers (poly)");
		configOutput(VEL_OUTPUT, "Velocity per track (poly, 0 to 10 V, held from each hit)");
		configOutput(LOCK_A_OUTPUT, "Lock A per track (poly, 0 to 10 V, held from each hit)");
		configOutput(LOCK_B_OUTPUT, "Lock B per track (poly, 0 to 10 V, held from each hit)");
		configOutput(ACCENT_OUTPUT, "Accent trigger (any hit with velocity above 80 %)");

		core.generate(mp); // something to play straight away
		snapPat = core.pat;
		snapHome = core.home;
	}

	~Mutate() { delete loaded; }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		Load* fresh = new Load;
		MutateCore* tmp = new MutateCore;
		tmp->generate(MutateParams());
		fresh->pat = tmp->pat;
		fresh->home = tmp->home;
		delete tmp;
		loadNow.offer(fresh);
	}

	void readControls() {
		mp.steps = (int)std::lround(params[STEPS_PARAM].getValue());
		mp.density = clamp(params[DENSITY_PARAM].getValue() + inputs[DENSITY_INPUT].getVoltage() * 0.2f, 0.f, 1.f);
		mp.every = (int)std::lround(params[EVERY_PARAM].getValue());
		mp.amount = clamp(params[AMOUNT_PARAM].getValue() + inputs[AMOUNT_INPUT].getVoltage() * 0.2f, 0.f, 1.f);
		mp.drift = clamp(params[DRIFT_PARAM].getValue() + inputs[DRIFT_INPUT].getVoltage() * 0.2f, 0.f, 1.f);
		for (int k = 0; k < RULE_COUNT; k++)
			mp.weight[k] = params[RULE_PARAM + k].getValue();
		mp.ratchetCurve = params[CURVE_PARAM].getValue();
		mp.ratchetDecay = params[DECAY_PARAM].getValue();
		mp.swing = params[SWING_PARAM].getValue();
		core.setSeed(SeedLatch::read(params[SEED_PARAM].getValue(), inputs[SEED_INPUT].getVoltage(), inputs[SEED_INPUT].isConnected()));
		dispSteps.store(mp.steps, std::memory_order_relaxed);

		keep.process(params[KEEP_PARAM].getValue() > 0.5f, inputs[KEEP_INPUT].getVoltage(), params[RECALL_PARAM].getValue(), inputs[RECALL_INPUT].getVoltage());
		for (int i = 0; i < 8; i++)
			lights[SLOT_LIGHT + i].setBrightness(keep.light(i));

		// hand edits from the screen
		while (!edits.empty()) {
			Edit e = edits.shift();
			switch (e.type) {
				case Edit::STEP_ON: core.setStepOn(e.track, e.step, e.value > 0.5f); break;
				case Edit::STEP_VEL: core.setStepVel(e.track, e.step, e.value); break;
				case Edit::LENGTH: core.setLength(e.track, (int)e.value); break;
				case Edit::MUTE: core.setMute(e.track, e.value > 0.5f); break;
				case Edit::STYLE: core.setStyle(e.track, (int)e.value); core.generateTrack(e.track, mp); break;
				case Edit::REGEN_TRACK: core.generateTrack(e.track, mp); break;
				default: break;
			}
		}
		publish();
	}

	// Refresh the screen's copy when the pattern has changed.
	void publish() {
		if (core.version == publishedVersion)
			return;
		if (snapLock.tryLock()) {
			snapPat = core.pat;
			snapHome = core.home;
			snapVersion = core.version;
			publishedVersion = core.version;
			snapLock.unlock();
		}
	}

	void applyLoad(const Load& l) {
		core.pat = l.pat;
		core.home = l.home;
		core.version++;
		if (l.hasParams)
			for (int i = 0; i < KEPT; i++)
				params[i].setValue(l.param[i]);
	}

	void process(const ProcessArgs& args) override {
		// a pattern from the patch file (or Initialize) goes in straight away
		Load* next = loadNow.take(loaded);
		if (next != loaded) {
			loaded = next;
			applyLoad(*loaded);
			needsInit = true;
		}
		bool connected = inputs[PHASE_INPUT].isConnected();
		bool resetNow = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		if (needsInit || connected != phaseWasConnected || resetNow) {
			needsInit = false;
			phaseWasConnected = connected;
			core.reset();
			reader.reset();
			trig.reset();
			clockStep = -1;
			sinceClock = 0.f;
		}
		if (controlDivider == 0)
			readControls();
		controlDivider = (controlDivider + 1) & 15;
		mp.microCv = clamp(inputs[MICRO_INPUT].getVoltage() * 0.1f, -0.5f, 0.5f);

		bool genPressed = generateButton.process(params[GENERATE_PARAM].getValue() > 0.5f);
		bool genTrig = generateIn.process(inputs[GENERATE_INPUT].getVoltage(), 0.1f, 1.f);
		if (genPressed || genTrig) {
			readControls();
			core.generate(mp);
		}
		bool homePressed = homeButton.process(params[HOME_PARAM].getValue() > 0.5f);
		bool homeTrig = homeIn.process(inputs[HOME_INPUT].getVoltage(), 0.1f, 1.f);
		if (homePressed || homeTrig)
			core.requestHome();
		if (homeSetButton.process(params[HOME_SET_PARAM].getValue() > 0.5f))
			core.setHome();
		if (mutateIn.process(inputs[MUTATE_INPUT].getVoltage(), 0.1f, 1.f))
			core.requestMutate();

		// where are we? PHASE if patched, otherwise count CLOCK pulses
		Crossings entered;
		double pos = -1.0;
		bool backward = false;
		bool active = true;
		if (connected) {
			reader.process(inputs[PHASE_INPUT].getVoltage());
			entered = reader.crossed((double)mp.steps);
			pos = reader.W * (double)mp.steps;
			backward = reader.delta() < 0.0;
		}
		else {
			sinceClock += 1.f;
			if (clockIn.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (clockStep >= 0)
					clockPeriod = std::max(sinceClock, 1.f);
				sinceClock = 0.f;
				clockStep++;
				entered.count = 1;
				entered.dir = 1;
				entered.index[0] = clockStep;
			}
			active = clockStep >= 0;
			// between pulses, glide through the step at the speed of the last one (for micro-timing and ratchets)
			pos = (double)clockStep + (double)std::min(sinceClock / clockPeriod, 0.999f);
		}

		MutateOut out;
		if (active) {
			// a recalled slot takes over on the bar line
			bool barLine = false;
			for (int e = 0; e < entered.count; e++)
				if (entered.index[e] >= 0 && entered.index[e] % mp.steps == 0)
					barLine = true;
			if (barLine) {
				Load* recalled = loadAtBar.take(loaded);
				if (recalled != loaded) {
					loaded = recalled;
					applyLoad(*loaded);
					readControls();
				}
			}
			core.process(mp, entered, pos, backward, out);
		}

		outputs[GATES_OUTPUT].setChannels(8);
		outputs[VEL_OUTPUT].setChannels(8);
		outputs[LOCK_A_OUTPUT].setChannels(8);
		outputs[LOCK_B_OUTPUT].setChannels(8);
		for (int t = 0; t < 8; t++) {
			if (out.gate[t])
				trig.fire(t);
			float g = trig.process(t, args.sampleTime);
			outputs[GATE_OUTPUT + t].setVoltage(g);
			outputs[GATES_OUTPUT].setVoltage(g, t);
			outputs[VEL_OUTPUT].setVoltage(out.vel[t] * 10.f, t);
			outputs[LOCK_A_OUTPUT].setVoltage(out.lockA[t] * 10.f, t);
			outputs[LOCK_B_OUTPUT].setVoltage(out.lockB[t] * 10.f, t);
		}
		if (out.accent)
			trig.fire(8);
		outputs[ACCENT_OUTPUT].setVoltage(trig.process(8, args.sampleTime));
		dispStep.store(active ? core.stepNow : -1, std::memory_order_relaxed);
	}

	// A consistent copy of the pattern for the UI thread.
	void snapshot(MutatePattern& pat, MutatePattern& home) {
		snapLock.lock();
		pat = snapPat;
		home = snapHome;
		snapLock.unlock();
	}

	// ---- KEEP protocol (UI thread) ----
	json_t* captureState() override {
		MutatePattern* pat = new MutatePattern;
		MutatePattern* home = new MutatePattern;
		snapshot(*pat, *home);
		json_t* j = json_object();
		json_object_set_new(j, "pattern", patternToJson(*pat));
		json_object_set_new(j, "home", patternToJson(*home));
		json_t* p = json_array();
		for (int i = 0; i < KEPT; i++)
			json_array_append_new(p, json_real(params[i].getValue()));
		json_object_set_new(j, "params", p);
		delete pat;
		delete home;
		return j;
	}

	Load* loadFromJson(json_t* j) {
		Load* l = new Load;
		snapshot(l->pat, l->home); // anything missing from the JSON stays as it is now
		bool ok = patternFromJson(json_object_get(j, "pattern"), l->pat);
		if (!patternFromJson(json_object_get(j, "home"), l->home) && ok)
			l->home = l->pat;
		json_t* p = json_object_get(j, "params");
		if (p && json_is_array(p)) {
			l->hasParams = true;
			for (int i = 0; i < KEPT; i++) {
				json_t* v = json_array_get(p, i);
				l->param[i] = v ? (float)json_number_value(v) : params[i].getValue();
			}
		}
		return l;
	}

	void requestRestore(json_t* j) override {
		loadAtBar.offer(loadFromJson(j));
		json_decref(j);
	}

	std::string keepLabel() override { return string::f("MUTATE #%lld", (long long)id); }

	json_t* dataToJson() override {
		MutatePattern* pat = new MutatePattern;
		MutatePattern* home = new MutatePattern;
		snapshot(*pat, *home);
		json_t* root = json_object();
		json_object_set_new(root, "pattern", patternToJson(*pat));
		json_object_set_new(root, "home", patternToJson(*home));
		json_object_set_new(root, "keepSlots", keep.toJson());
		delete pat;
		delete home;
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_object_get(root, "pattern")) {
			Load* l = loadFromJson(root);
			l->hasParams = false; // the knobs are restored by Rack itself
			// keep the screen's copy in step even if the audio engine is not running yet
			snapLock.lock();
			snapPat = l->pat;
			snapHome = l->home;
			snapLock.unlock();
			loadNow.offer(l);
		}
		keep.fromJson(json_object_get(root, "keepSlots"));
	}

	void pushEdit(uint8_t type, int track, int step, float value) {
		if (!edits.full())
			edits.push(Edit{type, (uint8_t)track, (uint8_t)step, value});
	}
};

// The screen: 8 tracks by up to 64 steps.
//   click a step            turn it on or off
//   drag up or down on it   change its velocity (brightness)
//   click past the end      make the track longer; drag the bright end line to resize
//   click a track's label   mute it; right-click the label for its style
struct MutateDisplay : OpaqueWidget {
	Mutate* module = nullptr;
	MutatePattern* pat;
	MutatePattern* home;
	static constexpr float LABEL_W = 24.f; // px
	static constexpr float DRIFT_W = 7.f;

	enum DragMode { NONE, VELOCITY, LENGTH };
	int dragMode = NONE, dragTrack = 0, dragStep = 0;
	bool dragMoved = false, dragWasOn = false;
	float dragVel = 0.8f;
	Vec dragPos;

	MutateDisplay() {
		pat = new MutatePattern;
		home = new MutatePattern;
		// what the module browser shows
		MutateCore* demo = new MutateCore;
		demo->generate(MutateParams());
		*pat = demo->pat;
		*home = demo->home;
		delete demo;
	}
	~MutateDisplay() {
		delete pat;
		delete home;
	}

	float rowH() const { return box.size.y / 8.f; }
	float cellW() const { return (box.size.x - LABEL_W - DRIFT_W) / 64.f; }

	void step() override {
		if (module)
			module->snapshot(*pat, *home);
		OpaqueWidget::step();
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
			float rh = rowH(), cw = cellW();
			int perBar = module ? module->dispSteps.load() : 16;
			int64_t now = module ? module->dispStep.load() : 5;
			NVGcolor amber = theme::time();
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));

			for (int t = 0; t < 8; t++) {
				const MutateTrack& tr = pat->track[t];
				float y = t * rh;
				int len = clamp(tr.length, 1, 64);
				// label: track number and style letter; dim when muted
				if (font) {
					nvgFontFaceId(vg, font->handle);
					nvgFontSize(vg, 10.5f);
					nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, tr.mute ? theme::grid() : theme::text());
					char label[4] = {(char)('1' + t), STYLE_LETTER[clamp(tr.style, 0, STYLE_COUNT - 1)], 0, 0};
					nvgText(vg, 4.f, y + rh * 0.5f, label, NULL);
				}
				int playing = (now >= 0) ? (int)(now % len) : -1;
				for (int i = 0; i < 64; i++) {
					float x = LABEL_W + i * cw;
					bool inside = i < len;
					const MutateStep& s = tr.step[i];
					// cell background: steps in use are a little lighter; the first step of each beat lighter still
					nvgBeginPath(vg);
					nvgRect(vg, x + 0.5f, y + 1.f, cw - 1.f, rh - 2.f);
					float base = inside ? ((i % std::max(perBar / 4, 1)) == 0 ? 0.13f : 0.08f) : 0.025f;
					nvgFillColor(vg, nvgRGBAf(1, 1, 1, base));
					nvgFill(vg);
					if (inside && s.on) {
						// brightness = velocity
						nvgBeginPath(vg);
						nvgRect(vg, x + 0.5f, y + 1.f, cw - 1.f, rh - 2.f);
						nvgFillColor(vg, nvgTransRGBAf(amber, (tr.mute ? 0.25f : 1.f) * (0.2f + 0.8f * s.vel)));
						nvgFill(vg);
						// ratchet: one dot per hit, along the top
						if (s.ratchet > 1) {
							nvgBeginPath(vg);
							for (int k = 0; k < s.ratchet; k++)
								nvgRect(vg, x + 1.2f + k * (cw - 2.4f) / (float)s.ratchet, y + 2.f, 1.f, 1.6f);
							nvgFillColor(vg, theme::screen());
							nvgFill(vg);
						}
						// micro-timing: a tick at the bottom, left of centre = early, right = late
						if (s.micro != 0.f) {
							nvgBeginPath(vg);
							nvgRect(vg, x + cw * 0.5f + s.micro * (cw - 2.f) - 0.5f, y + rh - 4.f, 1.2f, 3.f);
							nvgFillColor(vg, theme::text());
							nvgFill(vg);
						}
						// a condition (does not always play): hollow corner
						if (s.cond != COND_ALWAYS) {
							nvgBeginPath(vg);
							nvgMoveTo(vg, x + cw - 0.5f, y + rh - 1.f);
							nvgLineTo(vg, x + cw - 0.5f, y + rh - 4.5f);
							nvgLineTo(vg, x + cw - 4.f, y + rh - 1.f);
							nvgClosePath(vg);
							nvgFillColor(vg, theme::screen());
							nvgFill(vg);
						}
					}
					if (inside && i == playing) {
						nvgBeginPath(vg);
						nvgRect(vg, x + 0.5f, y + 1.f, cw - 1.f, rh - 2.f);
						nvgStrokeColor(vg, theme::text());
						nvgStrokeWidth(vg, 1.f);
						nvgStroke(vg);
					}
				}
				// end of the track
				nvgBeginPath(vg);
				nvgRect(vg, LABEL_W + len * cw - 0.6f, y + 0.5f, 1.4f, rh - 1.f);
				nvgFillColor(vg, theme::text());
				nvgFill(vg);
				// how far this track has drifted from home
				int diff = 0;
				for (int i = 0; i < len; i++)
					if (!tr.step[i].same(home->track[t].step[i]))
						diff++;
				float d = clamp((float)diff / (float)len * 2.f, 0.f, 1.f);
				nvgBeginPath(vg);
				nvgRect(vg, box.size.x - DRIFT_W + 2.f, y + 1.f + (rh - 2.f) * (1.f - d), DRIFT_W - 4.f, (rh - 2.f) * d);
				nvgFillColor(vg, theme::system());
				nvgFill(vg);
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS) {
			OpaqueWidget::onButton(e);
			return;
		}
		int t = clamp((int)(e.pos.y / rowH()), 0, 7);
		const MutateTrack& tr = pat->track[t];
		if (e.pos.x < LABEL_W) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT)
				module->pushEdit(Mutate::Edit::MUTE, t, 0, tr.mute ? 0.f : 1.f);
			else if (e.button == GLFW_MOUSE_BUTTON_RIGHT)
				trackMenu(t);
			e.consume(this);
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			OpaqueWidget::onButton(e);
			return;
		}
		float col = (e.pos.x - LABEL_W) / cellW();
		int i = clamp((int)col, 0, 63);
		dragTrack = t;
		dragPos = e.pos;
		dragMoved = false;
		if (std::fabs(col - (float)tr.length) < 0.3f) {
			dragMode = LENGTH; // grabbed the end line
		}
		else if (i >= tr.length) {
			module->pushEdit(Mutate::Edit::LENGTH, t, 0, (float)(i + 1)); // clicked past the end: lengthen to here
			dragMode = LENGTH;
		}
		else {
			dragMode = VELOCITY;
			dragStep = i;
			dragWasOn = tr.step[i].on;
			dragVel = tr.step[i].vel;
			if (!dragWasOn)
				module->pushEdit(Mutate::Edit::STEP_ON, t, i, 1.f);
		}
		e.consume(this);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || dragMode == NONE)
			return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		if (dragMode == LENGTH) {
			int len = clamp((int)std::lround((dragPos.x - LABEL_W) / cellW()), 1, 64);
			module->pushEdit(Mutate::Edit::LENGTH, dragTrack, 0, (float)len);
		}
		else {
			float dy = -e.mouseDelta.y / getAbsoluteZoom();
			if (std::fabs(dy) > 0.f) {
				dragMoved = true;
				dragVel = clamp(dragVel + dy * 0.01f, 0.05f, 1.f);
				module->pushEdit(Mutate::Edit::STEP_VEL, dragTrack, dragStep, dragVel);
			}
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		// a plain click on a lit step turns it off; a drag only changed its velocity
		if (module && dragMode == VELOCITY && dragWasOn && !dragMoved)
			module->pushEdit(Mutate::Edit::STEP_ON, dragTrack, dragStep, 0.f);
		dragMode = NONE;
	}

	void trackMenu(int t) {
		Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Track %d", t + 1)));
		appendTrackItems(menu, t);
	}

	void appendTrackItems(Menu* menu, int t) {
		Mutate* m = module;
		int style = pat->track[t].style;
		for (int s = 0; s < STYLE_COUNT; s++) {
			menu->addChild(createCheckMenuItem(string::f("Style: %s", STYLE_NAME[s]), "",
				[=]() { return style == s; },
				[=]() { m->pushEdit(Mutate::Edit::STYLE, t, 0, (float)s); }));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Write this track afresh (same style)", "", [=]() { m->pushEdit(Mutate::Edit::REGEN_TRACK, t, 0, 0.f); }));
		menu->addChild(createMenuItem(pat->track[t].mute ? "Unmute" : "Mute", "", [=]() { m->pushEdit(Mutate::Edit::MUTE, t, 0, pat->track[t].mute ? 0.f : 1.f); }));
	}
};

struct MutateWidget : ModuleWidget {
	MutateDisplay* display = nullptr;
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	MutateWidget(Mutate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Mutate.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		display = new MutateDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(at(L::STEPS), module, Mutate::STEPS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DENSITY), module, Mutate::DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Mutate::SEED_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::GENERATE), module, Mutate::GENERATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::EVERY), module, Mutate::EVERY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::AMOUNT), module, Mutate::AMOUNT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DRIFT), module, Mutate::DRIFT_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::HOME), module, Mutate::HOME_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::HOME_SET), module, Mutate::HOME_SET_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::CURVE), module, Mutate::CURVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DECAY), module, Mutate::DECAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SWING), module, Mutate::SWING_PARAM));
		for (int k = 0; k < RULE_COUNT; k++)
			addParam(createParamCentered<Trimpot>(at(L::RULE[k]), module, Mutate::RULE_PARAM + k));
		addParam(createParamCentered<VCVButton>(at(L::KEEP), module, Mutate::KEEP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RECALL), module, Mutate::RECALL_PARAM));
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<SmallLight<YellowLight>>(at(L::SLOT_LIGHTS[i]), module, Mutate::SLOT_LIGHT + i));

		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Mutate::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::CLOCK_IN), module, Mutate::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RESET_IN), module, Mutate::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::GENERATE_IN), module, Mutate::GENERATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MUTATE_IN), module, Mutate::MUTATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::HOME_IN), module, Mutate::HOME_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DENSITY_IN), module, Mutate::DENSITY_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::AMOUNT_IN), module, Mutate::AMOUNT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DRIFT_IN), module, Mutate::DRIFT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MICRO_IN), module, Mutate::MICRO_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SEED_IN), module, Mutate::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::KEEP_IN), module, Mutate::KEEP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RECALL_IN), module, Mutate::RECALL_INPUT));

		for (int t = 0; t < 8; t++)
			addOutput(createOutputCentered<PJ301MPort>(at(L::GATE_OUT[t]), module, Mutate::GATE_OUTPUT + t));
		addOutput(createOutputCentered<PJ301MPort>(at(L::GATES_OUT), module, Mutate::GATES_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::VEL_OUT), module, Mutate::VEL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::LOCK_A_OUT), module, Mutate::LOCK_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::LOCK_B_OUT), module, Mutate::LOCK_B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::ACCENT_OUT), module, Mutate::ACCENT_OUTPUT));
	}

	void step() override {
		Mutate* m = getModule<Mutate>();
		if (m) {
			m->keep.step(m);
			m->loadNow.collect();
			m->loadAtBar.collect();
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Mutate* m = getModule<Mutate>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tracks (or right-click a track's label on the screen)"));
		for (int t = 0; t < 8; t++) {
			int style = display->pat->track[t].style;
			menu->addChild(createSubmenuItem(string::f("Track %d", t + 1), STYLE_NAME[clamp(style, 0, STYLE_COUNT - 1)],
				[=](Menu* sub) { display->appendTrackItems(sub, t); }));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Screen: click a step = on/off, drag up/down = velocity,"));
		menu->addChild(createMenuLabel("click past the end or drag the end line = track length"));
		m->keep.appendMenu(menu, m);
	}
};

Model* modelMutate = createModel<Mutate, MutateWidget>("Mutate");
