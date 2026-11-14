// CONTOUR: curve-crossing rhythms with interference (spec §6). The rhythm
// engine is in dsp/ContourCore; this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/ContourLayout.hpp"
#include "dsp/ContourCore.hpp"
#include "common/Phase.hpp"
#include "common/Trigger.hpp"
#include "common/KeepHost.hpp"
#include "common/StateSwap.hpp"
#include "common/Theme.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_contour;

struct Contour : Module, Keepable {
	enum ParamId {
		MODE_PARAM, RATE_PARAM, MORPH_PARAM, THRESH_PARAM, BUNCH_PARAM, HYST_PARAM, CHANS_PARAM, DIR_PARAM,
		ENUMS(AMP_PARAM, 4), ENUMS(RATIO_PARAM, 4), ENUMS(THETA_PARAM, 4),
		SPREAD_PARAM, COINC_PARAM, WINDOW_PARAM, SEED_PARAM, KEEP_PARAM, RECALL_PARAM,
		PARAMS_LEN
	};
	enum InputId { PHASE_INPUT, EXT_INPUT, RATE_INPUT, MORPH_INPUT, DENS_INPUT, SPREAD_INPUT, SEED_INPUT, KEEP_INPUT, RECALL_INPUT, INPUTS_LEN };
	enum OutputId { TRIG_OUTPUT, VEL_OUTPUT, CURVE_OUTPUT, SLOPE_OUTPUT, COINC_OUTPUT, COUNT_OUTPUT, PHASORS_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(SLOT_LIGHT, 8), LIGHTS_LEN };

	// The knobs KEEP stores (everything musical; not KEEP and RECALL themselves).
	static const int KEPT = SEED_PARAM + 1;
	struct State {
		float param[KEPT];
		int ratioSet, velMode;
		bool snapRatios;
	};

	ContourCore core;
	ContourParams cp;
	PhaseReader reader;
	AdaptiveTrigger trig;
	dsp::PulseGenerator coincPulse;
	double phi = 0.0;       // position when running from the internal RATE
	float rateHz = 0.5f;
	bool phaseWasConnected = false;
	bool needsInit = true;
	int controlDivider = 0;

	// right-click options
	int ratioSet = ContourParams::R_HARMONIC;
	int velMode = ContourParams::VEL_SLOPE;
	bool snapRatios = true;

	KeepHost keep;
	StateSwap<State> restore;
	State* restored = nullptr;

	// For the display. The audio thread only tries the lock, so it never waits.
	TryLock dispLock;
	ContourParams dispParams;
	float dispLevel[16] = {};
	int dispLevels = 0;
	static const int BUF = 256;
	std::atomic<float> curveBuf[BUF];
	std::atomic<float> dispPhasor[8];
	std::atomic<float> dispFrac{0.f};
	std::atomic<int64_t> dispBar{0};
	static const int DOTS = 32;
	std::atomic<float> dotX[DOTS], dotY[DOTS];
	std::atomic<int> dotHead{0};

	Contour() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(MODE_PARAM, 0.f, 2.f, 2.f, "Curve source", {"EXT (the EXT input is the curve)", "MOIRE (8 phasors beating against each other)", "HARM (sum of four sine harmonics)"});
		configParam(RATE_PARAM, std::log2(0.01f), std::log2(2000.f), std::log2(0.5f), "Rate (used when nothing is patched to PHASE)", " Hz", 2.f, 1.f);
		configParam(MORPH_PARAM, 0.f, 4.f, 0.f, "Morph (slides the harmonics against each other)");
		configParam(THRESH_PARAM, 1.f, 16.f, 4.f, "Number of levels (more levels = more hits)");
		paramQuantities[THRESH_PARAM]->snapEnabled = true;
		configParam(BUNCH_PARAM, -1.f, 1.f, 0.f, "Bunch (left: levels crowd round the middle, right: toward top and bottom)");
		configParam(HYST_PARAM, 0.f, 0.1f, 0.02f, "Hysteresis (how far the curve must move away before a level can fire again)");
		configParam(CHANS_PARAM, 1.f, 16.f, 1.f, "Poly channels (levels are dealt out across this many channels)");
		paramQuantities[CHANS_PARAM]->snapEnabled = true;
		configSwitch(DIR_PARAM, 0.f, 2.f, 1.f, "Fire when the curve crosses a level going", {"down", "either way", "up"});
		static const float defAmp[4] = {1.f, 0.5f, 0.f, 0.f};
		for (int k = 0; k < 4; k++) {
			configParam(AMP_PARAM + k, 0.f, 1.f, defAmp[k], string::f("Harmonic %d amount", k + 1), " %", 0.f, 100.f);
			configParam(RATIO_PARAM + k, 0.25f, 16.f, (float)(k + 1), string::f("Harmonic %d cycles per bar", k + 1));
			configParam(THETA_PARAM + k, 0.f, 1.f, 0.f, string::f("Harmonic %d phase", k + 1), " cycle");
		}
		configParam(SPREAD_PARAM, 0.f, 0.1f, 0.f, "MOIRE spread (0 = locked groove; a little = slow drift out of phase and back)", " %", 0.f, 100.f);
		configParam(COINC_PARAM, 2.f, 8.f, 4.f, "MOIRE: phasors that must line up to fire COINC");
		paramQuantities[COINC_PARAM]->snapEnabled = true;
		configParam(WINDOW_PARAM, 0.001f, 0.05f, 0.01f, "MOIRE: how close counts as lined up", " cycle");
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (drives the MOIRE detuning; a new seed starts at the next bar)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configButton(KEEP_PARAM, "Keep the current settings in the next free slot");
		configParam(RECALL_PARAM, 0.f, 8.f, 0.f, "Slot to recall (0 = none). A trigger at the RECALL jack recalls it at the next bar");
		paramQuantities[RECALL_PARAM]->snapEnabled = true;

		configInput(PHASE_INPUT, "Phase (0 to 10 V ramp, one cycle = one bar)");
		configInput(EXT_INPUT, "External curve (±5 V), used in EXT mode");
		configInput(RATE_INPUT, "Rate CV (1 V/oct)");
		configInput(MORPH_INPUT, "Morph CV");
		configInput(DENS_INPUT, "Density CV (±5 V adds or removes up to 8 levels)");
		configInput(SPREAD_INPUT, "MOIRE spread CV");
		configInput(SEED_INPUT, "Seed CV (0 to 10 V = 0 to 999)");
		configInput(KEEP_INPUT, "Keep trigger");
		configInput(RECALL_INPUT, "Recall trigger");
		configOutput(TRIG_OUTPUT, "Triggers (poly)");
		configOutput(VEL_OUTPUT, "Velocity (poly, 0 to 10 V, held from each hit)");
		configOutput(CURVE_OUTPUT, "The curve (±5 V)");
		configOutput(SLOPE_OUTPUT, "Steepness of the curve (±5 V)");
		configOutput(COINC_OUTPUT, "MOIRE coincidence trigger");
		configOutput(COUNT_OUTPUT, "MOIRE: how many phasors met (1.25 V each)");
		configOutput(PHASORS_OUTPUT, "MOIRE: the 8 phasors (poly, 0 to 10 V)");

		for (int i = 0; i < BUF; i++)
			curveBuf[i] = 0.f;
		for (int i = 0; i < 8; i++)
			dispPhasor[i] = 0.f;
		for (int i = 0; i < DOTS; i++) {
			dotX[i] = -1.f;
			dotY[i] = 0.f;
		}
	}

	~Contour() { delete restored; }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		ratioSet = ContourParams::R_HARMONIC;
		velMode = ContourParams::VEL_SLOPE;
		snapRatios = true;
		needsInit = true;
	}

	static float snapRatio(float r) {
		if (r < 0.375f)
			return 0.25f;
		if (r < 0.75f)
			return 0.5f;
		return std::round(r);
	}

	void readControls() {
		// the switches read top to bottom, so their values are turned round here
		int modeSw = (int)std::lround(params[MODE_PARAM].getValue());
		cp.mode = modeSw == 2 ? ContourParams::HARM : (modeSw == 1 ? ContourParams::MOIRE : ContourParams::EXT);
		int dirSw = (int)std::lround(params[DIR_PARAM].getValue());
		cp.direction = dirSw == 2 ? ContourParams::UP : (dirSw == 1 ? ContourParams::BOTH : ContourParams::DOWN);
		for (int k = 0; k < 4; k++) {
			cp.amp[k] = params[AMP_PARAM + k].getValue();
			float r = params[RATIO_PARAM + k].getValue();
			cp.ratio[k] = snapRatios ? snapRatio(r) : r;
			cp.theta[k] = params[THETA_PARAM + k].getValue();
		}
		cp.morph = params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 0.8f;
		cp.ratioSet = ratioSet;
		cp.spread = clamp(params[SPREAD_PARAM].getValue() + inputs[SPREAD_INPUT].getVoltage() * 0.02f, 0.f, 0.1f);
		cp.coincideN = (int)std::lround(params[COINC_PARAM].getValue());
		cp.window = params[WINDOW_PARAM].getValue();
		int dens = (int)std::lround(inputs[DENS_INPUT].getVoltage() * 1.6f);
		cp.nThresh = clamp((int)std::lround(params[THRESH_PARAM].getValue()) + dens, 1, 16);
		cp.spreadT = params[BUNCH_PARAM].getValue();
		cp.hyst = params[HYST_PARAM].getValue();
		cp.channels = (int)std::lround(params[CHANS_PARAM].getValue());
		cp.velMode = velMode;
		rateHz = std::pow(2.f, clamp(params[RATE_PARAM].getValue() + inputs[RATE_INPUT].getVoltage(), -7.f, 11.5f));
		core.setSeed(SeedLatch::read(params[SEED_PARAM].getValue(), inputs[SEED_INPUT].getVoltage(), inputs[SEED_INPUT].isConnected()));

		keep.process(params[KEEP_PARAM].getValue() > 0.5f, inputs[KEEP_INPUT].getVoltage(), params[RECALL_PARAM].getValue(), inputs[RECALL_INPUT].getVoltage());
		for (int i = 0; i < 8; i++)
			lights[SLOT_LIGHT + i].setBrightness(keep.light(i));

		if (dispLock.tryLock()) {
			dispParams = cp;
			dispLevels = core.nLevels;
			for (int i = 0; i < 16; i++)
				dispLevel[i] = core.level[i];
			dispLock.unlock();
		}
		if (cp.mode == ContourParams::MOIRE)
			for (int j = 0; j < 8; j++)
				dispPhasor[j].store(lastOut.phasor[j], std::memory_order_relaxed);
	}

	ContourOut lastOut;

	void applyRestored(const State& s) {
		for (int i = 0; i < KEPT; i++)
			params[i].setValue(s.param[i]);
		ratioSet = s.ratioSet;
		velMode = s.velMode;
		snapRatios = s.snapRatios;
	}

	void process(const ProcessArgs& args) override {
		bool connected = inputs[PHASE_INPUT].isConnected();
		if (needsInit || connected != phaseWasConnected) {
			needsInit = false;
			phaseWasConnected = connected;
			readControls();
			core.reset();
			reader.reset();
			trig.reset();
			phi = 0.0;
		}
		if (controlDivider == 0)
			readControls();
		controlDivider = (controlDivider + 1) & 7;

		// where are we? PHASE if patched, otherwise the internal RATE
		double position;
		if (connected) {
			reader.process(inputs[PHASE_INPUT].getVoltage());
			position = reader.W;
		}
		else {
			phi += (double)rateHz * (double)args.sampleTime;
			position = phi;
		}
		cp.ext = inputs[EXT_INPUT].getVoltage() * 0.2f;

		int64_t bar = (int64_t)std::floor(position);
		if (bar != dispBar.load(std::memory_order_relaxed)) {
			dispBar.store(bar, std::memory_order_relaxed);
			// a recalled slot takes over on the bar line
			State* next = restore.take(restored);
			if (next != restored) {
				restored = next;
				applyRestored(*restored);
				readControls();
			}
		}

		ContourOut& out = lastOut;
		core.process(cp, position, out);

		int channels = clamp(cp.channels, 1, 16);
		outputs[TRIG_OUTPUT].setChannels(channels);
		outputs[VEL_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; c++) {
			if (out.trig[c])
				trig.fire(c);
			outputs[TRIG_OUTPUT].setVoltage(trig.process(c, args.sampleTime), c);
			outputs[VEL_OUTPUT].setVoltage(out.vel[c] * 10.f, c);
		}
		outputs[CURVE_OUTPUT].setVoltage(out.curve * 5.f);
		outputs[SLOPE_OUTPUT].setVoltage(out.slope * 5.f);
		if (out.coincide)
			coincPulse.trigger(1e-3f);
		outputs[COINC_OUTPUT].setVoltage(coincPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[COUNT_OUTPUT].setVoltage((float)out.count * 1.25f);
		if (cp.mode == ContourParams::MOIRE) {
			outputs[PHASORS_OUTPUT].setChannels(8);
			for (int j = 0; j < 8; j++)
				outputs[PHASORS_OUTPUT].setVoltage(out.phasor[j] * 10.f, j);
		}
		else {
			outputs[PHASORS_OUTPUT].setChannels(1);
			outputs[PHASORS_OUTPUT].setVoltage(0.f);
		}

		// picture for the screen
		float frac = (float)(position - (double)bar);
		curveBuf[(int)(frac * BUF) & (BUF - 1)].store(out.curve, std::memory_order_relaxed);
		dispFrac.store(frac, std::memory_order_relaxed);
		for (int i = 0; i < out.crossed; i++) {
			int h = (dotHead.load(std::memory_order_relaxed) + 1) % DOTS;
			dotX[h].store(frac, std::memory_order_relaxed);
			dotY[h].store(out.crossedLevel[i], std::memory_order_relaxed);
			dotHead.store(h, std::memory_order_relaxed);
		}
	}

	// ---- KEEP protocol (UI thread) ----
	json_t* captureState() override {
		json_t* j = json_object();
		json_t* p = json_array();
		for (int i = 0; i < KEPT; i++)
			json_array_append_new(p, json_real(params[i].getValue()));
		json_object_set_new(j, "params", p);
		json_object_set_new(j, "ratioSet", json_integer(ratioSet));
		json_object_set_new(j, "velMode", json_integer(velMode));
		json_object_set_new(j, "snapRatios", json_boolean(snapRatios));
		return j;
	}

	void requestRestore(json_t* j) override {
		State* s = new State;
		json_t* p = json_object_get(j, "params");
		for (int i = 0; i < KEPT; i++) {
			json_t* v = p ? json_array_get(p, i) : nullptr;
			s->param[i] = v ? (float)json_number_value(v) : params[i].getValue();
		}
		json_t* rs = json_object_get(j, "ratioSet");
		json_t* vm = json_object_get(j, "velMode");
		json_t* sn = json_object_get(j, "snapRatios");
		s->ratioSet = rs ? clamp((int)json_integer_value(rs), 0, 3) : ratioSet;
		s->velMode = vm ? clamp((int)json_integer_value(vm), 0, 2) : velMode;
		s->snapRatios = sn ? json_is_true(sn) : snapRatios;
		json_decref(j);
		restore.offer(s);
	}

	std::string keepLabel() override { return string::f("CONTOUR #%lld", (long long)id); }

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "ratioSet", json_integer(ratioSet));
		json_object_set_new(root, "velMode", json_integer(velMode));
		json_object_set_new(root, "snapRatios", json_boolean(snapRatios));
		json_object_set_new(root, "keepSlots", keep.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* rs = json_object_get(root, "ratioSet");
		json_t* vm = json_object_get(root, "velMode");
		json_t* sn = json_object_get(root, "snapRatios");
		if (rs)
			ratioSet = clamp((int)json_integer_value(rs), 0, 3);
		if (vm)
			velMode = clamp((int)json_integer_value(vm), 0, 2);
		if (sn)
			snapRatios = json_is_true(sn);
		keep.fromJson(json_object_get(root, "keepSlots"));
		needsInit = true;
	}
};

// The screen: the curve over one cycle, the levels, recent hits and a playhead.
// In MOIRE mode the 8 phasors are shown as small dials along the top.
struct ContourDisplay : Widget {
	Contour* module = nullptr;

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

			ContourParams p;
			float level[16];
			int levels = 4;
			float frac = 0.3f;
			int64_t bar = 0;
			if (module) {
				module->dispLock.lock();
				p = module->dispParams;
				levels = module->dispLevels;
				for (int i = 0; i < 16; i++)
					level[i] = module->dispLevel[i];
				module->dispLock.unlock();
				frac = module->dispFrac.load();
				bar = module->dispBar.load();
			}
			else {
				for (int i = 0; i < levels; i++)
					level[i] = ContourCore::levelAt(i, levels, 0.f);
			}
			bool moire = p.mode == ContourParams::MOIRE;
			float top = moire ? 16.f : 5.f;
			float h = box.size.y - top - 5.f;
			auto yOf = [&](float c) { return top + (1.f - (clamp(c, -1.f, 1.f) + 1.f) * 0.5f) * h; };

			// levels
			nvgBeginPath(vg);
			for (int i = 0; i < levels; i++) {
				float y = yOf(level[i]);
				nvgMoveTo(vg, 0, y);
				nvgLineTo(vg, box.size.x, y);
			}
			nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.22f));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);

			// the curve
			nvgBeginPath(vg);
			const int N = 200;
			for (int i = 0; i <= N; i++) {
				float x = (float)i / N;
				float c;
				if (p.mode == ContourParams::HARM || !module)
					c = ContourCore::harm(p, (double)bar + (double)x);
				else
					c = module->curveBuf[std::min((int)(x * Contour::BUF), Contour::BUF - 1)].load(std::memory_order_relaxed);
				float px = x * box.size.x, py = yOf(c);
				if (i == 0)
					nvgMoveTo(vg, px, py);
				else
					nvgLineTo(vg, px, py);
			}
			nvgStrokeColor(vg, theme::time());
			nvgStrokeWidth(vg, 1.4f);
			nvgStroke(vg);

			if (module) {
				// recent hits
				for (int i = 0; i < Contour::DOTS; i++) {
					float x = module->dotX[i].load(std::memory_order_relaxed);
					if (x < 0.f)
						continue;
					nvgBeginPath(vg);
					nvgCircle(vg, x * box.size.x, yOf(module->dotY[i].load(std::memory_order_relaxed)), 1.8f);
					nvgFillColor(vg, theme::text());
					nvgFill(vg);
				}
				// playhead
				nvgBeginPath(vg);
				nvgMoveTo(vg, frac * box.size.x, top - 2.f);
				nvgLineTo(vg, frac * box.size.x, box.size.y);
				nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.4f));
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}

			// MOIRE: 8 phase dials. A hand pointing straight up is "at the start of its cycle".
			if (moire) {
				for (int j = 0; j < 8; j++) {
					float cx = box.size.x * ((float)j + 0.5f) / 8.f, cy = 8.f, r = 5.f;
					float th = module ? module->dispPhasor[j].load(std::memory_order_relaxed) : 0.f;
					bool near = std::min(th, 1.f - th) < p.window;
					nvgBeginPath(vg);
					nvgCircle(vg, cx, cy, r);
					nvgStrokeColor(vg, near ? theme::text() : theme::grid());
					nvgStrokeWidth(vg, 1.f);
					nvgStroke(vg);
					float a = th * 2.f * M_PI - M_PI / 2.f;
					nvgBeginPath(vg);
					nvgMoveTo(vg, cx, cy);
					nvgLineTo(vg, cx + std::cos(a) * r, cy + std::sin(a) * r);
					nvgStrokeColor(vg, theme::time());
					nvgStrokeWidth(vg, 1.2f);
					nvgStroke(vg);
				}
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}
};

struct ContourWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	ContourWidget(Contour* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Contour.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		ContourDisplay* display = new ContourDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		for (int k = 0; k < 4; k++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::AMP[k]), module, Contour::AMP_PARAM + k));
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RATIO[k]), module, Contour::RATIO_PARAM + k));
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::THETA[k]), module, Contour::THETA_PARAM + k));
		}
		addParam(createParamCentered<CKSSThree>(at(L::MODE), module, Contour::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RATE), module, Contour::RATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::MORPH), module, Contour::MORPH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::THRESH), module, Contour::THRESH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::BUNCH), module, Contour::BUNCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::HYST), module, Contour::HYST_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::CHANS), module, Contour::CHANS_PARAM));
		addParam(createParamCentered<CKSSThree>(at(L::DIR), module, Contour::DIR_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SPREAD), module, Contour::SPREAD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::COINC), module, Contour::COINC_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::WINDOW), module, Contour::WINDOW_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Contour::SEED_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::KEEP), module, Contour::KEEP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RECALL), module, Contour::RECALL_PARAM));
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<SmallLight<YellowLight>>(at(L::SLOT_LIGHTS[i]), module, Contour::SLOT_LIGHT + i));

		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Contour::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::EXT_IN), module, Contour::EXT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RATE_IN), module, Contour::RATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MORPH_IN), module, Contour::MORPH_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DENS_IN), module, Contour::DENS_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SPREAD_IN), module, Contour::SPREAD_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SEED_IN), module, Contour::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::KEEP_IN), module, Contour::KEEP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RECALL_IN), module, Contour::RECALL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::TRIG_OUT), module, Contour::TRIG_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::VEL_OUT), module, Contour::VEL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CURVE_OUT), module, Contour::CURVE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::SLOPE_OUT), module, Contour::SLOPE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::COINC_OUT), module, Contour::COINC_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::COUNT_OUT), module, Contour::COUNT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::PHASORS_OUT), module, Contour::PHASORS_OUTPUT));
	}

	void step() override {
		Contour* m = getModule<Contour>();
		if (m) {
			m->keep.step(m);
			m->restore.collect();
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Contour* m = getModule<Contour>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Velocity comes from",
			{"How steep the curve is at the hit", "How high the level is", "Which level it is (low to high)"}, &m->velMode));
		menu->addChild(createBoolPtrMenuItem("Snap harmonic ratios to whole numbers (off = free, for slow shifting)", "", &m->snapRatios));
		menu->addChild(createIndexPtrSubmenuItem("MOIRE phasor ratios",
			{"Harmonic: 1 to 8", "Golden ratio: never quite repeats", "Primes: 2, 3, 5 ... 19, halved", "Seeded: picked by SEED"}, &m->ratioSet));
		m->keep.appendMenu(menu, m);
	}
};

Model* modelContour = createModel<Contour, ContourWidget>("Contour");
