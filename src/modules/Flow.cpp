// FLOW: a small fluid as a modulation source (spec §13A). The fluid itself is
// in dsp/FlowCore; this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/FlowLayout.hpp"
#include "dsp/FlowCore.hpp"
#include "common/Rng.hpp"
#include "common/KeepHost.hpp"
#include "common/StateSwap.hpp"
#include "common/Theme.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_flow;

struct Flow : Module, Keepable {
	enum ParamId { VISC_PARAM, DIFF_PARAM, VORT_PARAM, DECAY_PARAM, SPEED_PARAM, RATE_PARAM, GAIN_PARAM, THRESH_PARAM, SEED_PARAM, KEEP_PARAM, RECALL_PARAM, PARAMS_LEN };
	enum InputId {
		ENUMS(X_INPUT, 4), ENUMS(Y_INPUT, 4), ENUMS(ANG_INPUT, 4), ENUMS(FORCE_INPUT, 4), ENUMS(DYE_INPUT, 4), ENUMS(PUFF_INPUT, 4),
		PROBE_X_INPUT, PROBE_Y_INPUT, KEEP_INPUT, RECALL_INPUT, INPUTS_LEN
	};
	enum OutputId { DENS_OUTPUT, U_OUTPUT, V_OUTPUT, CURL_OUTPUT, ENERGY_OUTPUT, PEAK_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(SLOT_LIGHT, 8), LIGHTS_LEN };

	static constexpr int N = FlowCore::N;
	static constexpr int CELLS = N * N;
	static constexpr int PROBES = 8;
	static constexpr int INJ = FlowCore::INJECTORS;
	static constexpr int KEPT = SEED_PARAM + 1; // the knobs KEEP stores

	// Everything KEEP stores, ready for the audio thread to take over at its next step.
	struct State {
		float u[CELLS], v[CELLS], d[CELLS];
		float param[KEPT];
		float probeX[PROBES], probeY[PROBES];
		float injX[INJ], injY[INJ], injAngle[INJ];
		bool wrap, noFade;
	};

	FlowCore core;
	FlowParams fp;
	FlowInjector inj[INJ];
	SeedLatch seed;
	double tickAcc = 0.0;
	bool needsInit = true;
	int controlDivider = 0;
	float rate = 60.f, speed = 1.f, gain = 1.f, thresh = 5.f;

	// Positions set on the screen (UI thread writes, audio thread reads).
	std::atomic<float> probeX[PROBES], probeY[PROBES];
	std::atomic<float> injX[INJ], injY[INJ], injAngle[INJ];
	std::atomic<int> uiPuff{0};            // bit k: the screen asked injector k to puff
	std::atomic<bool> restartRequest{false};
	// right-click options
	std::atomic<bool> wrap{false};
	std::atomic<bool> noFade{false};

	dsp::SchmittTrigger puffIn[INJ];
	bool puffNow[INJ] = {};

	// Output targets (set at each step) and their 10 ms smoothing.
	float target[4][PROBES] = {}, smooth[4][PROBES] = {};
	float energyTarget = 0.f, energySmooth = 0.f;
	bool peakHigh = false;
	dsp::PulseGenerator peakPulse;

	KeepHost keep;
	StateSwap<State> restore;
	State* restored = nullptr;

	// A copy for the screen and for KEEP. The audio thread only tries the lock.
	struct Snapshot {
		float u[CELLS], v[CELLS], d[CELLS];
		float injX[INJ], injY[INJ], injAngle[INJ], injForce[INJ];
		float probeX[PROBES], probeY[PROBES];
		float energy;
	};
	TryLock snapLock;
	Snapshot snap;

	Flow() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(VISC_PARAM, 0.f, 0.01f, 0.0005f, "Viscosity (how quickly the motion smooths out and dies)", "", 0.f, 1000.f);
		configParam(DIFF_PARAM, 0.f, 0.01f, 0.0002f, "Diffusion (how quickly the dye spreads)", "", 0.f, 1000.f);
		configParam(VORT_PARAM, 0.f, 1.f, 0.3f, "Vorticity (keeps the swirls alive)", " %", 0.f, 100.f);
		configParam(DECAY_PARAM, std::log2(0.1f), std::log2(60.f), 3.f, "Dye half-life (how long the dye takes to fade to half)", " s", 2.f);
		configParam(SPEED_PARAM, std::log2(0.1f), 2.f, 0.f, "Speed (how fast time passes in the fluid)", "x", 2.f);
		configParam(RATE_PARAM, 5.f, 200.f, 60.f, "Steps per second (smoothness against CPU)", " Hz");
		paramQuantities[RATE_PARAM]->snapEnabled = true;
		configParam(GAIN_PARAM, 0.f, 4.f, 1.f, "Gain for U, V and CURL", "x");
		configParam(THRESH_PARAM, 0.f, 10.f, 5.f, "PEAK threshold (fires when ENERGY rises past this)", " V");
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (the starting swirl; a new seed restarts the fluid)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configButton(KEEP_PARAM, "Keep the fluid and settings in the next free slot");
		configParam(RECALL_PARAM, 0.f, 8.f, 0.f, "Slot to recall (0 = none). A trigger at the RECALL jack recalls it");
		paramQuantities[RECALL_PARAM]->snapEnabled = true;

		const char* letter = "ABCD";
		for (int k = 0; k < INJ; k++) {
			configInput(X_INPUT + k, string::f("Injector %c across (0 to 10 V, left to right)", letter[k]));
			configInput(Y_INPUT + k, string::f("Injector %c down (0 to 10 V, top to bottom)", letter[k]));
			configInput(ANG_INPUT + k, string::f("Injector %c direction (0 to 10 V = a full turn)", letter[k]));
			configInput(FORCE_INPUT + k, string::f("Injector %c steady push (0 to 10 V)", letter[k]));
			configInput(DYE_INPUT + k, string::f("Injector %c steady dye (0 to 10 V)", letter[k]));
			configInput(PUFF_INPUT + k, string::f("Injector %c puff trigger (a burst of push and dye)", letter[k]));
		}
		configInput(PROBE_X_INPUT, "Probe positions across (poly, up to 8, 0 to 10 V)");
		configInput(PROBE_Y_INPUT, "Probe positions down (poly, up to 8, 0 to 10 V)");
		configInput(KEEP_INPUT, "Keep trigger");
		configInput(RECALL_INPUT, "Recall trigger");
		configOutput(DENS_OUTPUT, "Dye at each probe (8 channels, 0 to 10 V)");
		configOutput(U_OUTPUT, "Left-right speed at each probe (8 channels, ±5 V)");
		configOutput(V_OUTPUT, "Up-down speed at each probe (8 channels, ±5 V; positive = down the screen)");
		configOutput(CURL_OUTPUT, "Swirl at each probe (8 channels, ±5 V; positive = clockwise)");
		configOutput(ENERGY_OUTPUT, "How much the whole fluid is moving (0 to 10 V)");
		configOutput(PEAK_OUTPUT, "Trigger when ENERGY rises past THRESH");
		defaultPositions();
	}

	~Flow() { delete restored; }

	void defaultPositions() {
		for (int i = 0; i < PROBES; i++) {
			// two rows of four
			probeX[i] = 0.2f + 0.2f * (float)(i % 4);
			probeY[i] = i < 4 ? 0.35f : 0.65f;
		}
		const float ix[INJ] = {0.2f, 0.8f, 0.8f, 0.2f}, iy[INJ] = {0.2f, 0.2f, 0.8f, 0.8f};
		for (int k = 0; k < INJ; k++) {
			injX[k] = ix[k];
			injY[k] = iy[k];
			// each one points round the box, so puffs start a slow circle
			injAngle[k] = (float)k * (float)M_PI * 0.5f;
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		defaultPositions();
		wrap = false;
		noFade = false;
		needsInit = true;
	}

	void readControls() {
		fp.viscosity = params[VISC_PARAM].getValue();
		fp.diffusion = params[DIFF_PARAM].getValue();
		fp.vort = params[VORT_PARAM].getValue();
		fp.halfLife = noFade ? 0.f : std::pow(2.f, params[DECAY_PARAM].getValue());
		fp.wrap = wrap;
		speed = std::pow(2.f, params[SPEED_PARAM].getValue());
		rate = clamp(params[RATE_PARAM].getValue(), 5.f, 200.f);
		gain = params[GAIN_PARAM].getValue();
		thresh = params[THRESH_PARAM].getValue();
		seed.set(SeedLatch::read(params[SEED_PARAM].getValue(), 0.f, false));
		// at high step rates each step changes little, so the pressure solve can be shorter
		core.iterations = clamp((int)(40.f * 60.f / rate), 12, 40);

		keep.process(params[KEEP_PARAM].getValue() > 0.5f, inputs[KEEP_INPUT].getVoltage(), params[RECALL_PARAM].getValue(), inputs[RECALL_INPUT].getVoltage());
		for (int i = 0; i < 8; i++)
			lights[SLOT_LIGHT + i].setBrightness(keep.light(i));
	}

	void applyRestored(const State& s) {
		core.load(s.u, s.v, s.d);
		for (int i = 0; i < KEPT; i++)
			params[i].setValue(s.param[i]);
		for (int i = 0; i < PROBES; i++) {
			probeX[i] = s.probeX[i];
			probeY[i] = s.probeY[i];
		}
		for (int k = 0; k < INJ; k++) {
			injX[k] = s.injX[k];
			injY[k] = s.injY[k];
			injAngle[k] = s.injAngle[k];
		}
		wrap = s.wrap;
		noFade = s.noFade;
		seed.set(SeedLatch::read(s.param[SEED_PARAM], 0.f, false));
		seed.commit(); // the kept seed is already in the kept fluid
	}

	// One step of the fluid, then new targets for the outputs.
	void tick() {
		State* next = restore.take(restored);
		if (next != restored) {
			restored = next;
			applyRestored(*restored);
			readControls();
		}
		if (seed.commit() || restartRequest.exchange(false))
			core.reset(seed.active);

		int puffs = uiPuff.exchange(0);
		for (int k = 0; k < INJ; k++) {
			FlowInjector& in = inj[k];
			in.x = inputs[X_INPUT + k].isConnected() ? clamp(inputs[X_INPUT + k].getVoltage() * 0.1f, 0.f, 1.f) : injX[k].load();
			in.y = inputs[Y_INPUT + k].isConnected() ? clamp(inputs[Y_INPUT + k].getVoltage() * 0.1f, 0.f, 1.f) : injY[k].load();
			in.angle = inputs[ANG_INPUT + k].isConnected() ? inputs[ANG_INPUT + k].getVoltage() * 0.1f * 2.f * (float)M_PI : injAngle[k].load();
			in.force = clamp(inputs[FORCE_INPUT + k].getVoltage() * 0.1f, 0.f, 1.f);
			in.dye = clamp(inputs[DYE_INPUT + k].getVoltage() * 0.1f, 0.f, 1.f);
			in.puff = puffNow[k] || ((puffs >> k) & 1);
			puffNow[k] = false;
		}
		core.step(fp, inj, speed / rate);

		int px = inputs[PROBE_X_INPUT].getChannels(), py = inputs[PROBE_Y_INPUT].getChannels();
		float probe[2][PROBES];
		for (int i = 0; i < PROBES; i++) {
			// a mono cable moves every probe; a poly cable moves one probe per channel
			probe[0][i] = (px > 0 && (px == 1 || i < px)) ? clamp(inputs[PROBE_X_INPUT].getVoltage(px == 1 ? 0 : i) * 0.1f, 0.f, 1.f) : probeX[i].load();
			probe[1][i] = (py > 0 && (py == 1 || i < py)) ? clamp(inputs[PROBE_Y_INPUT].getVoltage(py == 1 ? 0 : i) * 0.1f, 0.f, 1.f) : probeY[i].load();
			float x = probe[0][i], y = probe[1][i];
			target[0][i] = clamp(core.density(x, y) * 10.f, 0.f, 10.f);
			target[1][i] = clamp(core.velU(x, y) * gain * 5.f, -5.f, 5.f);
			target[2][i] = clamp(core.velV(x, y) * gain * 5.f, -5.f, 5.f);
			target[3][i] = clamp(core.curl(x, y) * gain * 0.25f, -5.f, 5.f);
		}
		float e = core.energy();
		energyTarget = 10.f * e / (e + 0.05f);

		if (snapLock.tryLock()) {
			for (int j = 0; j < N; j++)
				for (int i = 0; i < N; i++) {
					int c = FlowCore::at(i + 1, j + 1), k = i + N * j;
					snap.u[k] = core.u[c];
					snap.v[k] = core.v[c];
					snap.d[k] = core.d[c];
				}
			for (int k = 0; k < INJ; k++) {
				snap.injX[k] = inj[k].x;
				snap.injY[k] = inj[k].y;
				snap.injAngle[k] = inj[k].angle;
				snap.injForce[k] = inj[k].force;
			}
			for (int i = 0; i < PROBES; i++) {
				snap.probeX[i] = probe[0][i];
				snap.probeY[i] = probe[1][i];
			}
			snap.energy = energyTarget;
			snapLock.unlock();
		}
	}

	void process(const ProcessArgs& args) override {
		if (needsInit) {
			needsInit = false;
			readControls();
			seed.commit();
			core.reset(seed.active);
			tickAcc = 1.0;
		}
		if (controlDivider == 0)
			readControls();
		controlDivider = (controlDivider + 1) & 15;

		// puffs are caught at audio rate and used at the next step
		for (int k = 0; k < INJ; k++)
			if (puffIn[k].process(inputs[PUFF_INPUT + k].getVoltage(), 0.1f, 1.f))
				puffNow[k] = true;

		tickAcc += (double)rate * (double)args.sampleTime;
		if (tickAcc >= 1.0) {
			tickAcc -= 1.0;
			if (tickAcc > 1.0)
				tickAcc = 0.0;
			tick();
		}

		// 10 ms smoothing so the outputs never step
		float a = 1.f - std::exp(-args.sampleTime / 0.01f);
		for (int o = 0; o < 4; o++) {
			outputs[DENS_OUTPUT + o].setChannels(PROBES);
			for (int i = 0; i < PROBES; i++) {
				smooth[o][i] += (target[o][i] - smooth[o][i]) * a;
				outputs[DENS_OUTPUT + o].setVoltage(smooth[o][i], i);
			}
		}
		energySmooth += (energyTarget - energySmooth) * a;
		outputs[ENERGY_OUTPUT].setVoltage(energySmooth);
		// a little hysteresis so a wobble at the threshold fires only once
		if (!peakHigh && energySmooth > thresh) {
			peakHigh = true;
			peakPulse.trigger(1e-3f);
		}
		else if (peakHigh && energySmooth < thresh - 0.2f)
			peakHigh = false;
		outputs[PEAK_OUTPUT].setVoltage(peakPulse.process(args.sampleTime) ? 10.f : 0.f);
	}

	// ---- KEEP protocol (UI thread) ----
	static json_t* floats(const float* x, int n) {
		json_t* a = json_array();
		for (int i = 0; i < n; i++)
			json_array_append_new(a, json_real(std::round(x[i] * 1e5f) * 1e-5f));
		return a;
	}
	static void readFloats(json_t* a, float* x, int n, float fallback) {
		for (int i = 0; i < n; i++) {
			json_t* v = a ? json_array_get(a, i) : nullptr;
			x[i] = v ? (float)json_number_value(v) : fallback;
		}
	}

	json_t* captureState() override {
		Snapshot s;
		snapLock.lock();
		s = snap;
		snapLock.unlock();
		json_t* j = json_object();
		json_object_set_new(j, "u", floats(s.u, CELLS));
		json_object_set_new(j, "v", floats(s.v, CELLS));
		json_object_set_new(j, "d", floats(s.d, CELLS));
		float p[KEPT];
		for (int i = 0; i < KEPT; i++)
			p[i] = params[i].getValue();
		json_object_set_new(j, "params", floats(p, KEPT));
		json_object_set_new(j, "positions", positionsToJson());
		json_object_set_new(j, "wrap", json_boolean(wrap));
		json_object_set_new(j, "noFade", json_boolean(noFade));
		return j;
	}

	void requestRestore(json_t* j) override {
		State* s = new State;
		readFloats(json_object_get(j, "u"), s->u, CELLS, 0.f);
		readFloats(json_object_get(j, "v"), s->v, CELLS, 0.f);
		readFloats(json_object_get(j, "d"), s->d, CELLS, 0.f);
		json_t* p = json_object_get(j, "params");
		for (int i = 0; i < KEPT; i++) {
			json_t* v = p ? json_array_get(p, i) : nullptr;
			s->param[i] = v ? (float)json_number_value(v) : params[i].getValue();
		}
		json_t* pos = json_object_get(j, "positions");
		for (int i = 0; i < PROBES; i++) {
			s->probeX[i] = probeX[i];
			s->probeY[i] = probeY[i];
		}
		for (int k = 0; k < INJ; k++) {
			s->injX[k] = injX[k];
			s->injY[k] = injY[k];
			s->injAngle[k] = injAngle[k];
		}
		if (pos) {
			readFloats(json_object_get(pos, "probeX"), s->probeX, PROBES, 0.5f);
			readFloats(json_object_get(pos, "probeY"), s->probeY, PROBES, 0.5f);
			readFloats(json_object_get(pos, "injX"), s->injX, INJ, 0.5f);
			readFloats(json_object_get(pos, "injY"), s->injY, INJ, 0.5f);
			readFloats(json_object_get(pos, "injAngle"), s->injAngle, INJ, 0.f);
		}
		json_t* w = json_object_get(j, "wrap");
		json_t* nf = json_object_get(j, "noFade");
		s->wrap = w ? json_is_true(w) : wrap.load();
		s->noFade = nf ? json_is_true(nf) : noFade.load();
		json_decref(j);
		restore.offer(s);
	}

	std::string keepLabel() override { return string::f("FLOW #%lld", (long long)id); }

	json_t* positionsToJson() {
		float a[PROBES], b[PROBES], c[INJ], d[INJ], e[INJ];
		for (int i = 0; i < PROBES; i++) {
			a[i] = probeX[i];
			b[i] = probeY[i];
		}
		for (int k = 0; k < INJ; k++) {
			c[k] = injX[k];
			d[k] = injY[k];
			e[k] = injAngle[k];
		}
		json_t* j = json_object();
		json_object_set_new(j, "probeX", floats(a, PROBES));
		json_object_set_new(j, "probeY", floats(b, PROBES));
		json_object_set_new(j, "injX", floats(c, INJ));
		json_object_set_new(j, "injY", floats(d, INJ));
		json_object_set_new(j, "injAngle", floats(e, INJ));
		return j;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "positions", positionsToJson());
		json_object_set_new(root, "wrap", json_boolean(wrap));
		json_object_set_new(root, "noFade", json_boolean(noFade));
		json_object_set_new(root, "keepSlots", keep.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* pos = json_object_get(root, "positions");
		if (pos) {
			float a[PROBES], b[PROBES], c[INJ], d[INJ], e[INJ];
			readFloats(json_object_get(pos, "probeX"), a, PROBES, 0.5f);
			readFloats(json_object_get(pos, "probeY"), b, PROBES, 0.5f);
			readFloats(json_object_get(pos, "injX"), c, INJ, 0.5f);
			readFloats(json_object_get(pos, "injY"), d, INJ, 0.5f);
			readFloats(json_object_get(pos, "injAngle"), e, INJ, 0.f);
			for (int i = 0; i < PROBES; i++) {
				probeX[i] = clamp(a[i], 0.f, 1.f);
				probeY[i] = clamp(b[i], 0.f, 1.f);
			}
			for (int k = 0; k < INJ; k++) {
				injX[k] = clamp(c[k], 0.f, 1.f);
				injY[k] = clamp(d[k], 0.f, 1.f);
				injAngle[k] = e[k];
			}
		}
		json_t* w = json_object_get(root, "wrap");
		json_t* nf = json_object_get(root, "noFade");
		if (w)
			wrap = json_is_true(w);
		if (nf)
			noFade = json_is_true(nf);
		keep.fromJson(json_object_get(root, "keepSlots"));
		needsInit = true;
	}
};

// The screen: the dye as dark-to-amber, velocity arrows every 3 cells, the
// injectors as lettered circles with their direction, the probes as numbered dots.
// Drag an injector to move it (the way you drag sets its direction); click it to puff.
// Drag a probe to move it.
struct FlowDisplay : OpaqueWidget {
	Flow* module = nullptr;
	Flow::Snapshot* view = new Flow::Snapshot;
	int dragging = -1; // 0..3 injector, 4..11 probe
	Vec dragPos, pressPos;
	bool moved = false;

	~FlowDisplay() { delete view; }

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, theme::screen());
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}

	Vec toScreen(float x, float y) const { return Vec(x * box.size.x, y * box.size.y); }

	int hit(Vec pos) const {
		if (!module)
			return -1;
		float best = 9.f * 9.f;
		int which = -1;
		for (int k = 0; k < Flow::INJ; k++) {
			Vec p = toScreen(view->injX[k], view->injY[k]);
			float d2 = p.minus(pos).square();
			if (d2 < best) {
				best = d2;
				which = k;
			}
		}
		for (int i = 0; i < Flow::PROBES; i++) {
			Vec p = toScreen(view->probeX[i], view->probeY[i]);
			float d2 = p.minus(pos).square();
			if (d2 < best) {
				best = d2;
				which = Flow::INJ + i;
			}
		}
		return which;
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.button != GLFW_MOUSE_BUTTON_LEFT) {
			OpaqueWidget::onButton(e);
			return;
		}
		if (e.action == GLFW_PRESS) {
			dragging = hit(e.pos);
			if (dragging >= 0) {
				dragPos = pressPos = e.pos;
				moved = false;
				e.consume(this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || dragging < 0)
			return;
		Vec delta = e.mouseDelta.div(getAbsoluteZoom());
		dragPos = dragPos.plus(delta);
		if (dragPos.minus(pressPos).norm() > 3.f)
			moved = true;
		float x = clamp(dragPos.x / box.size.x, 0.f, 1.f), y = clamp(dragPos.y / box.size.y, 0.f, 1.f);
		if (dragging < Flow::INJ) {
			int k = dragging;
			module->injX[k] = x;
			module->injY[k] = y;
			if (delta.norm() > 0.5f)
				module->injAngle[k] = std::atan2(delta.y, delta.x);
		}
		else {
			module->probeX[dragging - Flow::INJ] = x;
			module->probeY[dragging - Flow::INJ] = y;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (module && dragging >= 0 && dragging < Flow::INJ && !moved)
			module->uiPuff.fetch_or(1 << dragging);
		dragging = -1;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			nvgScissor(vg, 0, 0, box.size.x, box.size.y);
			const int N = Flow::N;
			if (module) {
				module->snapLock.lock();
				*view = module->snap;
				module->snapLock.unlock();
			}
			else {
				// the module browser: a still swirl
				for (int j = 0; j < N; j++)
					for (int i = 0; i < N; i++) {
						float x = (i + 0.5f) / N - 0.5f, y = (j + 0.5f) / N - 0.5f;
						view->d[i + N * j] = std::max(0.f, 0.6f * std::cos(8.f * std::sqrt(x * x + y * y) - 3.f * std::atan2(y, x)));
						view->u[i + N * j] = -y;
						view->v[i + N * j] = x;
					}
				for (int k = 0; k < Flow::INJ; k++) {
					view->injX[k] = k == 0 || k == 3 ? 0.2f : 0.8f;
					view->injY[k] = k < 2 ? 0.2f : 0.8f;
					view->injAngle[k] = k * (float)M_PI * 0.5f;
					view->injForce[k] = 0.f;
				}
				for (int i = 0; i < Flow::PROBES; i++) {
					view->probeX[i] = 0.2f + 0.2f * (i % 4);
					view->probeY[i] = i < 4 ? 0.35f : 0.65f;
				}
			}
			NVGcolor amber = theme::time();
			float cw = box.size.x / N, ch = box.size.y / N;

			// the dye
			for (int j = 0; j < N; j++)
				for (int i = 0; i < N; i++) {
					float d = clamp(view->d[i + N * j], 0.f, 1.f);
					if (d < 0.01f)
						continue;
					nvgBeginPath(vg);
					nvgRect(vg, i * cw, j * ch, cw + 0.3f, ch + 0.3f);
					// grey at first, turning amber as it thickens
					NVGcolor c = nvgLerpRGBA(nvgRGB(0x50, 0x4c, 0x46), amber, d);
					nvgFillColor(vg, nvgTransRGBAf(c, std::min(1.f, d * 1.6f)));
					nvgFill(vg);
				}

			// velocity arrows every 3 cells
			nvgBeginPath(vg);
			for (int j = 1; j < N; j += 3)
				for (int i = 1; i < N; i += 3) {
					float u = view->u[i + N * j], v = view->v[i + N * j];
					float x = (i + 0.5f) * cw, y = (j + 0.5f) * ch;
					float len = std::sqrt(u * u + v * v);
					if (len < 1e-3f)
						continue;
					float s = std::min(len * 40.f, 2.6f * cw) / len;
					float ex = x + u * s, ey = y + v * s;
					nvgMoveTo(vg, x, y);
					nvgLineTo(vg, ex, ey);
					float ax = (ex - x) * 0.3f, ay = (ey - y) * 0.3f;
					nvgMoveTo(vg, ex, ey);
					nvgLineTo(vg, ex - ax - ay * 0.6f, ey - ay + ax * 0.6f);
					nvgMoveTo(vg, ex, ey);
					nvgLineTo(vg, ex - ax + ay * 0.6f, ey - ay - ax * 0.6f);
				}
			nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.45f));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);

			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font)
				nvgFontFaceId(vg, font->handle);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

			// injectors
			const char* letter[4] = {"A", "B", "C", "D"};
			for (int k = 0; k < Flow::INJ; k++) {
				Vec p = toScreen(view->injX[k], view->injY[k]);
				float r = 5.5f;
				nvgBeginPath(vg);
				nvgCircle(vg, p.x, p.y, r);
				nvgFillColor(vg, nvgTransRGBAf(theme::screen(), 0.7f));
				nvgFill(vg);
				nvgStrokeColor(vg, amber);
				nvgStrokeWidth(vg, 1.2f);
				nvgStroke(vg);
				float a = view->injAngle[k];
				nvgBeginPath(vg);
				nvgMoveTo(vg, p.x + std::cos(a) * r, p.y + std::sin(a) * r);
				nvgLineTo(vg, p.x + std::cos(a) * (r + 5.f + 6.f * view->injForce[k]), p.y + std::sin(a) * (r + 5.f + 6.f * view->injForce[k]));
				nvgStroke(vg);
				nvgFontSize(vg, 8.f);
				nvgFillColor(vg, amber);
				nvgText(vg, p.x, p.y + 0.5f, letter[k], NULL);
			}
			// probes
			for (int i = 0; i < Flow::PROBES; i++) {
				Vec p = toScreen(view->probeX[i], view->probeY[i]);
				nvgBeginPath(vg);
				nvgCircle(vg, p.x, p.y, 4.f);
				nvgFillColor(vg, theme::text());
				nvgFill(vg);
				nvgFontSize(vg, 7.f);
				nvgFillColor(vg, theme::screen());
				nvgText(vg, p.x, p.y + 0.5f, string::f("%d", i + 1).c_str(), NULL);
			}
			// ENERGY as a thin bar along the bottom
			nvgBeginPath(vg);
			nvgRect(vg, 0, box.size.y - 2.f, box.size.x * clamp(view->energy * 0.1f, 0.f, 1.f), 2.f);
			nvgFillColor(vg, amber);
			nvgFill(vg);
			nvgRestore(vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

struct FlowWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	FlowWidget(Flow* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Flow.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		FlowDisplay* display = new FlowDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(at(L::VISC), module, Flow::VISC_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DIFF), module, Flow::DIFF_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::VORT), module, Flow::VORT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DECAY), module, Flow::DECAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SPEED), module, Flow::SPEED_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RATE), module, Flow::RATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::GAIN), module, Flow::GAIN_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::THRESH), module, Flow::THRESH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Flow::SEED_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::KEEP), module, Flow::KEEP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RECALL), module, Flow::RECALL_PARAM));
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<SmallLight<YellowLight>>(at(L::SLOT_LIGHTS[i]), module, Flow::SLOT_LIGHT + i));

		for (int k = 0; k < Flow::INJ; k++) {
			addInput(createInputCentered<PJ301MPort>(at(L::X_IN[k]), module, Flow::X_INPUT + k));
			addInput(createInputCentered<PJ301MPort>(at(L::Y_IN[k]), module, Flow::Y_INPUT + k));
			addInput(createInputCentered<PJ301MPort>(at(L::ANG_IN[k]), module, Flow::ANG_INPUT + k));
			addInput(createInputCentered<PJ301MPort>(at(L::FORCE_IN[k]), module, Flow::FORCE_INPUT + k));
			addInput(createInputCentered<PJ301MPort>(at(L::DYE_IN[k]), module, Flow::DYE_INPUT + k));
			addInput(createInputCentered<PJ301MPort>(at(L::PUFF_IN[k]), module, Flow::PUFF_INPUT + k));
		}
		addInput(createInputCentered<PJ301MPort>(at(L::PROBE_X_IN), module, Flow::PROBE_X_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PROBE_Y_IN), module, Flow::PROBE_Y_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::KEEP_IN), module, Flow::KEEP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RECALL_IN), module, Flow::RECALL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::DENS_OUT), module, Flow::DENS_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::U_OUT), module, Flow::U_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::V_OUT), module, Flow::V_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CURL_OUT), module, Flow::CURL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::ENERGY_OUT), module, Flow::ENERGY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::PEAK_OUT), module, Flow::PEAK_OUTPUT));
	}

	void step() override {
		Flow* m = getModule<Flow>();
		if (m) {
			m->keep.step(m);
			m->restore.collect();
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Flow* m = getModule<Flow>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Walls", {"Closed box", "Wraparound (what leaves one side comes in at the other)"},
			[=]() { return m->wrap ? 1 : 0; }, [=](size_t i) { m->wrap = i == 1; }));
		menu->addChild(createBoolMenuItem("Dye never fades (ignores DECAY)", "",
			[=]() { return m->noFade.load(); }, [=](bool b) { m->noFade = b; }));
		menu->addChild(createSubmenuItem("Puff an injector now", "", [=](Menu* sub) {
			const char* names[4] = {"A", "B", "C", "D"};
			for (int k = 0; k < Flow::INJ; k++)
				sub->addChild(createMenuItem(names[k], "", [=]() { m->uiPuff.fetch_or(1 << k); }));
		}));
		menu->addChild(createMenuItem("Restart the fluid from SEED", "", [=]() { m->restartRequest = true; }));
		menu->addChild(createMenuItem("Put probes and injectors back where they started", "", [=]() { m->defaultPositions(); }));
		m->keep.appendMenu(menu, m);
	}
};

Model* modelFlow = createModel<Flow, FlowWidget>("Flow");
