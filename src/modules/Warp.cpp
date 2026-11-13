// WARP: curved master clock (spec §4). The clock itself is in dsp/WarpCore;
// this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/WarpLayout.hpp"
#include "dsp/WarpCore.hpp"
#include "common/KeepHost.hpp"
#include "common/StateSwap.hpp"
#include "common/Theme.hpp"
#include "widgets/CurveDisplay.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_warp;

// The ratios an output snaps to (right-click a RATIO knob to turn snapping off).
static const int NUM_RATIOS = 20;
static const float RATIO_VALUE[NUM_RATIOS] = {1 / 16.f, 1 / 8.f, 1 / 6.f, 1 / 4.f, 1 / 3.f, 1 / 2.f, 2 / 3.f, 3 / 4.f, 1.f, 5 / 4.f,
	4 / 3.f, 3 / 2.f, 5 / 3.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
static const char* const RATIO_NAME[NUM_RATIOS] = {"1/16", "1/8", "1/6", "1/4", "1/3", "1/2", "2/3", "3/4", "1", "5/4",
	"4/3", "3/2", "5/3", "2", "3", "4", "5", "6", "7", "8"};

static int nearestRatio(float log2Ratio) {
	int best = 0;
	float bestDist = 1e9f;
	for (int i = 0; i < NUM_RATIOS; i++) {
		float d = std::fabs(std::log2(RATIO_VALUE[i]) - log2Ratio);
		if (d < bestDist) {
			bestDist = d;
			best = i;
		}
	}
	return best;
}

struct Warp : Module, Keepable {
	enum ParamId {
		BPM_PARAM, BEATS_PARAM, DRIFT_PARAM, PERIOD_PARAM, SHAPE_PARAM, DEPTH_PARAM,
		ENUMS(RATIO_PARAM, 4), ENUMS(OFFSET_PARAM, 4), ENUMS(FOLLOW_PARAM, 4),
		SEED_PARAM, RUN_PARAM, RESET_PARAM, KEEP_PARAM, RECALL_PARAM,
		PARAMS_LEN
	};
	enum InputId { TEMPO_INPUT, DEPTH_INPUT, SHAPE_INPUT, RESET_INPUT, RUN_INPUT, KEEP_INPUT, RECALL_INPUT, SEED_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(PHASE_OUTPUT, 4), POLY_OUTPUT, BAR_OUTPUT, BEAT_OUTPUT, TEMPO_OUTPUT, OUTPUTS_LEN };
	enum LightId { RUN_LIGHT, ENUMS(SLOT_LIGHT, 8), LIGHTS_LEN };

	static const int DRAWN = 6; // movable points of the drawn shape

	// What KEEP stores and RECALL brings back.
	struct State {
		float bpm, beats, drift, period, shape, depth, seed;
		float ratio[4], offset[4], follow[4];
		bool snap[4];
		float dx[WarpCore::DRAWN_MAX], dy[WarpCore::DRAWN_MAX];
		int dn;
	};

	WarpCore core;
	WarpParams wp;
	bool ratioSnap[4] = {true, true, true, true};

	// The drawn shape. Edited on the UI thread, read by the audio thread.
	std::atomic<float> drawnX[WarpCore::DRAWN_MAX], drawnY[WarpCore::DRAWN_MAX];
	std::atomic<int> drawnCount{DRAWN};
	std::atomic<int> drawnVersion{1};
	int drawnSeen = 0;

	KeepHost keep;
	StateSwap<State> restore;
	State* restored = nullptr;

	dsp::SchmittTrigger resetIn;
	dsp::BooleanTrigger resetButton;
	dsp::PulseGenerator barPulse, beatPulse;
	int controlDivider = 0;
	bool needsInit = true; // start the clock from the patch's own seed and settings

	// For the display (written by the audio thread, read by the UI thread).
	std::atomic<int64_t> dispBar{0};
	std::atomic<int> dispSeed{0};
	std::atomic<float> dispP{0.f}, dispShape{0.f}, dispDepth{0.f}, dispBpm{100.f};
	std::atomic<float> dispRatio[4];
	static const int TRACE = 128; // 16 bars, 8 points per bar
	std::atomic<float> trace[TRACE];
	std::atomic<int> traceHead{0};

	struct RatioQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Warp* m = static_cast<Warp*>(module);
			int i = paramId - RATIO_PARAM;
			if (m && m->ratioSnap[i])
				return RATIO_NAME[nearestRatio(getValue())];
			return string::f("%.3f", std::pow(2.f, getValue()));
		}
		void setDisplayValueString(std::string s) override {
			float num = 0.f, den = 1.f;
			if (std::sscanf(s.c_str(), "%f/%f", &num, &den) >= 1 && num > 0.f && den > 0.f)
				setValue(std::log2(num / den));
		}
	};

	Warp() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BPM_PARAM, 20.f, 300.f, 100.f, "Tempo", " BPM");
		configParam(BEATS_PARAM, 1.f, 16.f, 4.f, "Bar length", " beats");
		configParam(DRIFT_PARAM, 0.f, 0.25f, 0.f, "Tempo drift (how far the tempo wanders)", " %", 0.f, 100.f);
		configParam(PERIOD_PARAM, 1.f, 64.f, 8.f, "Drift period (bars between drift turning points)", " bars");
		paramQuantities[PERIOD_PARAM]->snapEnabled = true;
		configParam(SHAPE_PARAM, 0.f, 4.f, 0.f, "Warp shape (0 ease-in, 1 ease-out, 2 S-curve, 3 drunk, 4 drawn; in between blends)");
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.f, "Warp depth (how much the bar bends)", " %", 0.f, 100.f);
		static const float defRatio[4] = {1.f, 0.5f, 0.75f, 1.25f};
		for (int i = 0; i < 4; i++) {
			configParam<RatioQuantity>(RATIO_PARAM + i, -4.f, 3.f, std::log2(defRatio[i]), string::f("Output %d ratio (cycles per bar)", i + 1));
			configParam(OFFSET_PARAM + i, 0.f, 1.f, 0.f, string::f("Output %d phase offset", i + 1), " cycle");
			configParam(FOLLOW_PARAM + i, 0.f, 1.f, 1.f, string::f("Output %d warp follow (0 = straight, 100 = fully bent)", i + 1), " %", 0.f, 100.f);
			configOutput(PHASE_OUTPUT + i, string::f("Phase %d (0 to 10 V ramp)", i + 1));
			dispRatio[i] = defRatio[i];
		}
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (drives the drift and the drunk shape; a new seed starts at the next bar)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configButton(RESET_PARAM, "Reset to the start of bar 1");
		configButton(KEEP_PARAM, "Keep the current settings in the next free slot");
		configParam(RECALL_PARAM, 0.f, 8.f, 0.f, "Slot to recall (0 = none). A trigger at the RECALL jack recalls it at the next bar");
		paramQuantities[RECALL_PARAM]->snapEnabled = true;

		configInput(TEMPO_INPUT, "Tempo CV (+1 V doubles the tempo)");
		configInput(DEPTH_INPUT, "Warp depth CV (5 V = full)");
		configInput(SHAPE_INPUT, "Warp shape CV (±5 V sweeps all five shapes)");
		configInput(RESET_INPUT, "Reset trigger");
		configInput(RUN_INPUT, "Run gate (high = running; overrides the button)");
		configInput(KEEP_INPUT, "Keep trigger");
		configInput(RECALL_INPUT, "Recall trigger");
		configInput(SEED_INPUT, "Seed CV (0 to 10 V = 0 to 999)");
		configOutput(POLY_OUTPUT, "All four phases (4 channels)");
		configOutput(BAR_OUTPUT, "Bar trigger");
		configOutput(BEAT_OUTPUT, "Beat trigger (follows the warp)");
		configOutput(TEMPO_OUTPUT, "Current tempo as CV (0 V = 120 BPM)");

		for (int i = 0; i < WarpCore::DRAWN_MAX; i++) {
			drawnX[i] = (float)(i + 1) / (DRAWN + 1);
			drawnY[i] = (float)(i + 1) / (DRAWN + 1);
		}
		for (int i = 0; i < TRACE; i++)
			trace[i] = 0.f;
		core.reset();
	}

	~Warp() { delete restored; }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < 4; i++)
			ratioSnap[i] = true;
		for (int i = 0; i < WarpCore::DRAWN_MAX; i++) {
			drawnX[i] = (float)(i + 1) / (DRAWN + 1);
			drawnY[i] = (float)(i + 1) / (DRAWN + 1);
		}
		drawnCount = DRAWN;
		drawnVersion++;
	}

	float ratioOf(int i) {
		float v = params[RATIO_PARAM + i].getValue();
		return ratioSnap[i] ? RATIO_VALUE[nearestRatio(v)] : std::pow(2.f, v);
	}

	// Read knobs and CVs. Runs every 8 samples: none of these need to be faster.
	void readControls() {
		wp.bpm = params[BPM_PARAM].getValue();
		wp.tempoCv = inputs[TEMPO_INPUT].getVoltage();
		wp.beats = params[BEATS_PARAM].getValue();
		wp.driftDepth = params[DRIFT_PARAM].getValue();
		wp.driftRate = params[PERIOD_PARAM].getValue();
		wp.shape = clamp(params[SHAPE_PARAM].getValue() + inputs[SHAPE_INPUT].getVoltage() * 0.8f, 0.f, 4.f);
		wp.depth = clamp(params[DEPTH_PARAM].getValue() + inputs[DEPTH_INPUT].getVoltage() * 0.2f, 0.f, 1.f);
		for (int i = 0; i < 4; i++) {
			wp.ratio[i] = ratioOf(i);
			wp.offset[i] = params[OFFSET_PARAM + i].getValue();
			wp.follow[i] = params[FOLLOW_PARAM + i].getValue();
			dispRatio[i].store(wp.ratio[i], std::memory_order_relaxed);
		}
		wp.run = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() >= 1.f : params[RUN_PARAM].getValue() > 0.5f;
		core.setSeed(SeedLatch::read(params[SEED_PARAM].getValue(), inputs[SEED_INPUT].getVoltage(), inputs[SEED_INPUT].isConnected()));

		int v = drawnVersion.load(std::memory_order_relaxed);
		if (v != drawnSeen) {
			drawnSeen = v;
			float xs[WarpCore::DRAWN_MAX], ys[WarpCore::DRAWN_MAX];
			int n = clamp(drawnCount.load(), 1, (int)WarpCore::DRAWN_MAX);
			for (int i = 0; i < n; i++) {
				xs[i] = drawnX[i];
				ys[i] = drawnY[i];
			}
			core.setDrawn(xs, ys, n);
		}
		keep.process(params[KEEP_PARAM].getValue() > 0.5f, inputs[KEEP_INPUT].getVoltage(), params[RECALL_PARAM].getValue(), inputs[RECALL_INPUT].getVoltage());
		lights[RUN_LIGHT].setBrightness(wp.run ? 1.f : 0.f);
		for (int i = 0; i < 8; i++)
			lights[SLOT_LIGHT + i].setBrightness(keep.light(i));
	}

	// A recalled state is applied here, on the audio thread, at a bar line.
	void applyRestored(const State& s) {
		params[BPM_PARAM].setValue(s.bpm);
		params[BEATS_PARAM].setValue(s.beats);
		params[DRIFT_PARAM].setValue(s.drift);
		params[PERIOD_PARAM].setValue(s.period);
		params[SHAPE_PARAM].setValue(s.shape);
		params[DEPTH_PARAM].setValue(s.depth);
		params[SEED_PARAM].setValue(s.seed);
		for (int i = 0; i < 4; i++) {
			params[RATIO_PARAM + i].setValue(s.ratio[i]);
			params[OFFSET_PARAM + i].setValue(s.offset[i]);
			params[FOLLOW_PARAM + i].setValue(s.follow[i]);
			ratioSnap[i] = s.snap[i];
		}
		int n = clamp(s.dn, 1, (int)WarpCore::DRAWN_MAX);
		for (int i = 0; i < n; i++) {
			drawnX[i] = s.dx[i];
			drawnY[i] = s.dy[i];
		}
		drawnCount = n;
		drawnVersion++;
	}

	void process(const ProcessArgs& args) override {
		if (needsInit) {
			needsInit = false;
			readControls();
			core.reset();
		}
		if (controlDivider == 0)
			readControls();
		controlDivider = (controlDivider + 1) & 7;

		bool resetPressed = resetButton.process(params[RESET_PARAM].getValue() > 0.5f);
		bool resetTriggered = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		if (resetPressed || resetTriggered) {
			readControls();
			core.reset();
		}

		WarpOut out;
		core.process(wp, args.sampleTime, out);

		if (out.bar) {
			barPulse.trigger(1e-3f);
			// a recalled slot takes over exactly on the bar line
			State* next = restore.take(restored);
			if (next != restored) {
				restored = next;
				applyRestored(*restored);
				readControls();
			}
			dispBar.store((int64_t)std::floor(core.B), std::memory_order_relaxed);
			dispSeed.store(core.seed, std::memory_order_relaxed);
		}
		if (out.beat)
			beatPulse.trigger(1e-3f);

		outputs[POLY_OUTPUT].setChannels(4);
		for (int i = 0; i < 4; i++) {
			outputs[PHASE_OUTPUT + i].setVoltage(out.phase[i]);
			outputs[POLY_OUTPUT].setVoltage(out.phase[i], i);
		}
		outputs[BAR_OUTPUT].setVoltage(barPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[BEAT_OUTPUT].setVoltage(beatPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[TEMPO_OUTPUT].setVoltage(out.tempoCv);

		if (controlDivider == 1) {
			dispP.store(out.p, std::memory_order_relaxed);
			dispShape.store(wp.shape, std::memory_order_relaxed);
			dispDepth.store(wp.depth, std::memory_order_relaxed);
			dispBpm.store(out.bpm, std::memory_order_relaxed);
			int head = (int)(std::floor(core.B * 8.0)) & (TRACE - 1);
			trace[head].store(out.drift * wp.driftDepth * 4.f, std::memory_order_relaxed); // -1..1 = -25..+25 %
			traceHead.store(head, std::memory_order_relaxed);
		}
	}

	// ---- KEEP protocol (UI thread) ----
	json_t* captureState() override {
		json_t* j = json_object();
		json_object_set_new(j, "bpm", json_real(params[BPM_PARAM].getValue()));
		json_object_set_new(j, "beats", json_real(params[BEATS_PARAM].getValue()));
		json_object_set_new(j, "drift", json_real(params[DRIFT_PARAM].getValue()));
		json_object_set_new(j, "period", json_real(params[PERIOD_PARAM].getValue()));
		json_object_set_new(j, "shape", json_real(params[SHAPE_PARAM].getValue()));
		json_object_set_new(j, "depth", json_real(params[DEPTH_PARAM].getValue()));
		json_object_set_new(j, "seed", json_real(params[SEED_PARAM].getValue()));
		json_t* outs = json_array();
		for (int i = 0; i < 4; i++) {
			json_t* o = json_object();
			json_object_set_new(o, "ratio", json_real(params[RATIO_PARAM + i].getValue()));
			json_object_set_new(o, "offset", json_real(params[OFFSET_PARAM + i].getValue()));
			json_object_set_new(o, "follow", json_real(params[FOLLOW_PARAM + i].getValue()));
			json_object_set_new(o, "snap", json_boolean(ratioSnap[i]));
			json_array_append_new(outs, o);
		}
		json_object_set_new(j, "outputs", outs);
		json_object_set_new(j, "drawn", drawnToJson());
		return j;
	}

	void requestRestore(json_t* j) override {
		State* s = new State;
		auto num = [&](const char* key, int paramId) {
			json_t* v = json_object_get(j, key);
			return v ? (float)json_number_value(v) : params[paramId].getValue();
		};
		s->bpm = num("bpm", BPM_PARAM);
		s->beats = num("beats", BEATS_PARAM);
		s->drift = num("drift", DRIFT_PARAM);
		s->period = num("period", PERIOD_PARAM);
		s->shape = num("shape", SHAPE_PARAM);
		s->depth = num("depth", DEPTH_PARAM);
		s->seed = num("seed", SEED_PARAM);
		json_t* outs = json_object_get(j, "outputs");
		for (int i = 0; i < 4; i++) {
			json_t* o = outs ? json_array_get(outs, i) : nullptr;
			json_t* r = o ? json_object_get(o, "ratio") : nullptr;
			json_t* f = o ? json_object_get(o, "offset") : nullptr;
			json_t* w = o ? json_object_get(o, "follow") : nullptr;
			json_t* sn = o ? json_object_get(o, "snap") : nullptr;
			s->ratio[i] = r ? (float)json_number_value(r) : params[RATIO_PARAM + i].getValue();
			s->offset[i] = f ? (float)json_number_value(f) : params[OFFSET_PARAM + i].getValue();
			s->follow[i] = w ? (float)json_number_value(w) : params[FOLLOW_PARAM + i].getValue();
			s->snap[i] = sn ? json_is_true(sn) : ratioSnap[i];
		}
		s->dn = clamp(drawnCount.load(), 1, (int)WarpCore::DRAWN_MAX);
		for (int i = 0; i < WarpCore::DRAWN_MAX; i++) {
			s->dx[i] = drawnX[i];
			s->dy[i] = drawnY[i];
		}
		json_t* d = json_object_get(j, "drawn");
		if (d && json_is_array(d) && json_array_size(d) >= 1) {
			s->dn = std::min((int)json_array_size(d), (int)WarpCore::DRAWN_MAX);
			for (int i = 0; i < s->dn; i++) {
				json_t* pt = json_array_get(d, i);
				s->dx[i] = (float)json_number_value(json_array_get(pt, 0));
				s->dy[i] = (float)json_number_value(json_array_get(pt, 1));
			}
		}
		json_decref(j);
		restore.offer(s);
	}

	std::string keepLabel() override { return string::f("WARP #%lld", (long long)id); }

	json_t* drawnToJson() {
		json_t* d = json_array();
		int n = clamp(drawnCount.load(), 1, (int)WarpCore::DRAWN_MAX);
		for (int i = 0; i < n; i++)
			json_array_append_new(d, json_pack("[f, f]", (double)drawnX[i].load(), (double)drawnY[i].load()));
		return d;
	}

	// Replace the drawn shape (used by the display, and later by EAR's "send groove to WARP").
	void setDrawnShape(const float* xs, const float* ys, int n) {
		n = clamp(n, 1, (int)WarpCore::DRAWN_MAX);
		for (int i = 0; i < n; i++) {
			drawnX[i] = xs[i];
			drawnY[i] = ys[i];
		}
		drawnCount = n;
		drawnVersion++;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* snap = json_array();
		for (int i = 0; i < 4; i++)
			json_array_append_new(snap, json_boolean(ratioSnap[i]));
		json_object_set_new(root, "ratioSnap", snap);
		json_object_set_new(root, "drawn", drawnToJson());
		json_object_set_new(root, "keepSlots", keep.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* snap = json_object_get(root, "ratioSnap");
		for (int i = 0; i < 4; i++) {
			json_t* v = snap ? json_array_get(snap, i) : nullptr;
			if (v)
				ratioSnap[i] = json_is_true(v);
		}
		json_t* d = json_object_get(root, "drawn");
		if (d && json_is_array(d) && json_array_size(d) >= 1) {
			float xs[WarpCore::DRAWN_MAX], ys[WarpCore::DRAWN_MAX];
			int n = std::min((int)json_array_size(d), (int)WarpCore::DRAWN_MAX);
			for (int i = 0; i < n; i++) {
				json_t* pt = json_array_get(d, i);
				xs[i] = (float)json_number_value(json_array_get(pt, 0));
				ys[i] = (float)json_number_value(json_array_get(pt, 1));
			}
			setDrawnShape(xs, ys, n);
		}
		keep.fromJson(json_object_get(root, "keepSlots"));
	}
};

// The screen: warp curve with playhead on top, drift trace and read-outs below.
struct WarpDisplay : Widget {
	Warp* module = nullptr;
	CurveDisplay* curve = nullptr;
	WarpCore shadow; // a private copy of the clock maths, so drawing never touches the audio thread's state
	int64_t shadowBar = -1;
	int shadowSeed = -1, shadowDrawn = -1;

	void setup() {
		curve = new CurveDisplay;
		curve->box.pos = Vec(0, 0);
		curve->box.size = Vec(box.size.x, box.size.y * 0.68f);
		curve->accent = theme::time();
		curve->curve = [this](float p) {
			if (!module)
				return scurve(p, 6.f) * 0.5f + p * 0.5f; // something to look at in the module browser
			return shadow.warpDepth(p, module->dispShape.load(), module->dispDepth.load());
		};
		curve->playhead = [this]() { return module ? module->dispP.load() : 0.3f; };
		curve->editable = [this]() { return module && module->dispShape.load() > 3.5f; };
		curve->numPoints = [this]() { return module ? module->drawnCount.load() : 0; };
		curve->getPoint = [this](int i) { return Vec(module->drawnX[i].load(), module->drawnY[i].load()); };
		curve->setPoint = [this](int i, Vec v) {
			// a point can't be dragged past its neighbours
			int n = module->drawnCount.load();
			float lo = i > 0 ? module->drawnX[i - 1].load() + 0.02f : 0.02f;
			float hi = i < n - 1 ? module->drawnX[i + 1].load() - 0.02f : 0.98f;
			float ylo = i > 0 ? module->drawnY[i - 1].load() + 0.01f : 0.01f;
			float yhi = i < n - 1 ? module->drawnY[i + 1].load() - 0.01f : 0.99f;
			module->drawnX[i] = clamp(v.x, lo, std::max(lo, hi));
			module->drawnY[i] = clamp(v.y, ylo, std::max(ylo, yhi));
			module->drawnVersion++;
		};
		addChild(curve);
	}

	void step() override {
		if (module) {
			// rebuild the private copy when the bar, seed or drawn shape changes
			int64_t bar = module->dispBar.load();
			int seed = module->dispSeed.load();
			int dv = module->drawnVersion.load();
			if (bar != shadowBar || seed != shadowSeed) {
				shadowBar = bar;
				shadowSeed = seed;
				shadow.setSeed(seed);
				shadow.reset();
				shadow.B = (double)bar + 0.5;
				WarpParams wp;
				wp.run = false;
				WarpOut out;
				shadow.process(wp, 0.f, out); // first call
				shadow.process(wp, 0.f, out); // picks up the bar and builds its drunk shape
			}
			if (dv != shadowDrawn) {
				shadowDrawn = dv;
				float xs[WarpCore::DRAWN_MAX], ys[WarpCore::DRAWN_MAX];
				int n = clamp(module->drawnCount.load(), 1, (int)WarpCore::DRAWN_MAX);
				for (int i = 0; i < n; i++) {
					xs[i] = module->drawnX[i];
					ys[i] = module->drawnY[i];
				}
				shadow.setDrawn(xs, ys, n);
			}
		}
		Widget::step();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			float top = box.size.y * 0.68f + 2.f;
			float stripH = box.size.y * 0.16f;
			// drift trace: the tempo's wander over the last 16 bars
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, top + stripH * 0.5f);
			nvgLineTo(vg, box.size.x, top + stripH * 0.5f);
			nvgStrokeColor(vg, theme::grid());
			nvgStrokeWidth(vg, 0.6f);
			nvgStroke(vg);
			if (module) {
				int head = module->traceHead.load();
				nvgBeginPath(vg);
				for (int i = 0; i < Warp::TRACE; i++) {
					float v = module->trace[(head + 1 + i) & (Warp::TRACE - 1)].load();
					float x = box.size.x * (float)i / (Warp::TRACE - 1);
					float y = top + stripH * 0.5f - clamp(v, -1.f, 1.f) * stripH * 0.5f;
					if (i == 0)
						nvgMoveTo(vg, x, y);
					else
						nvgLineTo(vg, x, y);
				}
				nvgStrokeColor(vg, theme::time());
				nvgStrokeWidth(vg, 1.1f);
				nvgStroke(vg);
			}
			// read-outs: tempo now, and the four output ratios
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, 11.f);
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
				nvgFillColor(vg, theme::text());
				float y = box.size.y - 3.5f;
				std::string bpm = string::f("%.1f", module ? module->dispBpm.load() : 100.f);
				nvgText(vg, 4.f, y, bpm.c_str(), NULL);
				nvgFillColor(vg, theme::time());
				for (int i = 0; i < 4; i++) {
					float r = module ? module->dispRatio[i].load() : RATIO_VALUE[nearestRatio(std::log2(i == 0 ? 1.f : i == 1 ? 0.5f : i == 2 ? 0.75f : 1.25f))];
					int idx = nearestRatio(std::log2(r));
					std::string s = std::fabs(RATIO_VALUE[idx] - r) < 1e-4f ? std::string(RATIO_NAME[idx]) : string::f("%.2f", r);
					nvgText(vg, box.size.x * (0.34f + 0.165f * i), y, s.c_str(), NULL);
				}
				nvgFontSize(vg, 8.f);
				nvgFillColor(vg, theme::dim());
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
				nvgText(vg, box.size.x - 3.f, top + 7.f, "drift, 16 bars", NULL);
				if (module && module->dispShape.load() > 3.5f)
					nvgText(vg, box.size.x - 3.f, 9.f, "drag the points", NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}
};

struct WarpWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	WarpWidget(Warp* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Warp.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		WarpDisplay* display = new WarpDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		display->setup();
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(at(L::BPM), module, Warp::BPM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::BEATS), module, Warp::BEATS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DRIFT), module, Warp::DRIFT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::PERIOD), module, Warp::PERIOD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SHAPE), module, Warp::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DEPTH), module, Warp::DEPTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Warp::SEED_PARAM));
		for (int i = 0; i < 4; i++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RATIO[i]), module, Warp::RATIO_PARAM + i));
			addParam(createParamCentered<Trimpot>(at(L::OFFSET[i]), module, Warp::OFFSET_PARAM + i));
			addParam(createParamCentered<Trimpot>(at(L::FOLLOW[i]), module, Warp::FOLLOW_PARAM + i));
		}
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(at(L::RUN), module, Warp::RUN_PARAM, Warp::RUN_LIGHT));
		addParam(createParamCentered<VCVButton>(at(L::RESET), module, Warp::RESET_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::KEEP), module, Warp::KEEP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RECALL), module, Warp::RECALL_PARAM));
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<SmallLight<YellowLight>>(at(L::SLOT_LIGHTS[i]), module, Warp::SLOT_LIGHT + i));

		addInput(createInputCentered<PJ301MPort>(at(L::RUN_IN), module, Warp::RUN_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RESET_IN), module, Warp::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::KEEP_IN), module, Warp::KEEP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RECALL_IN), module, Warp::RECALL_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::TEMPO_IN), module, Warp::TEMPO_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SHAPE_IN), module, Warp::SHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DEPTH_IN), module, Warp::DEPTH_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SEED_IN), module, Warp::SEED_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::PHASE_OUT_1), module, Warp::PHASE_OUTPUT + 0));
		addOutput(createOutputCentered<PJ301MPort>(at(L::PHASE_OUT_2), module, Warp::PHASE_OUTPUT + 1));
		addOutput(createOutputCentered<PJ301MPort>(at(L::PHASE_OUT_3), module, Warp::PHASE_OUTPUT + 2));
		addOutput(createOutputCentered<PJ301MPort>(at(L::PHASE_OUT_4), module, Warp::PHASE_OUTPUT + 3));
		addOutput(createOutputCentered<PJ301MPort>(at(L::POLY_OUT), module, Warp::POLY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::BAR_OUT), module, Warp::BAR_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::BEAT_OUT), module, Warp::BEAT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::TEMPO_OUT), module, Warp::TEMPO_OUTPUT));
	}

	void step() override {
		Warp* m = getModule<Warp>();
		if (m) {
			m->keep.step(m);      // KEEP and RECALL are carried out here, on the UI thread
			m->restore.collect(); // free a state the audio thread has finished with
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Warp* m = getModule<Warp>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Snap output ratios to simple fractions"));
		for (int i = 0; i < 4; i++)
			menu->addChild(createBoolPtrMenuItem(string::f("Output %d (off = free ratio, for slow drifting against the bar)", i + 1), "", &m->ratioSnap[i]));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Straighten the drawn shape", "", [=]() {
			float xs[Warp::DRAWN], ys[Warp::DRAWN];
			for (int i = 0; i < Warp::DRAWN; i++)
				xs[i] = ys[i] = (float)(i + 1) / (Warp::DRAWN + 1);
			m->setDrawnShape(xs, ys, Warp::DRAWN);
		}));
		menu->addChild(createMenuLabel("Turn SHAPE fully up to 4, then drag the points on the screen"));
		m->keep.appendMenu(menu, m);
	}
};

Model* modelWarp = createModel<Warp, WarpWidget>("Warp");
