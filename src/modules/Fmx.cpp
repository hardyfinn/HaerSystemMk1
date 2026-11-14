// FMX: phase-driven FM voice (spec §9). The sound engine is in dsp/FmxCore;
// this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/FmxLayout.hpp"
#include "dsp/FmxCore.hpp"
#include "common/Trigger.hpp"
#include "common/Theme.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_fmx;

struct Fmx : Module {
	enum ParamId {
		ENUMS(RATIO_PARAM, 4), ENUMS(FINE_PARAM, 4), ENUMS(LEVEL_PARAM, 4), ENUMS(ENV_PARAM, 4),
		ALGO_PARAM, INDEX_PARAM, FEEDBACK_PARAM, TIME_PARAM, AMP_SHAPE_PARAM, INDEX_SHAPE_PARAM, SPREAD_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, TRIG_INPUT, PHASE_INPUT, VEL_INPUT, INDEX_INPUT, RATIO2_INPUT, RATIO3_INPUT, RATIO4_INPUT,
		FEEDBACK_INPUT, ALGO_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, POLY_OUTPUT, ENUMS(OP_OUTPUT, 4), OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	static const int VOICES = 8;
	FmxVoice voice[VOICES];
	FmxVoiceIn vin[VOICES];
	FmxGlobal g;
	SchmittIn trigIn;
	int divider = 0;
	int lastChannels = 1;
	float knobRatio[4] = {1.f, 1.f, 2.f, 3.f}; // as set on the panel, before snapping

	// right-click options
	bool snap[4] = {true, true, true, true};
	bool resetPhases = false;

	// for the screen
	std::atomic<int> dispAlgo{1};
	std::atomic<float> dispE{1.f}, dispFeedback{0.f}, dispAmpShape{2.f}, dispIndexShape{3.f};

	Fmx() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		static const float defRatio[4] = {1.f, 1.f, 2.f, 3.f};
		static const float defLevel[4] = {1.f, 0.6f, 0.5f, 0.4f};
		static const float defEnv[4] = {0.f, 1.f, 1.f, 1.f};
		for (int i = 0; i < 4; i++) {
			configParam(RATIO_PARAM + i, 0.25f, 16.f, defRatio[i], string::f("Operator %d ratio (its pitch as a multiple of the note; right-click the panel to snap or free it)", i + 1), "x");
			configParam(FINE_PARAM + i, -50.f, 50.f, 0.f, string::f("Operator %d fine tune", i + 1), " cents");
			configParam(LEVEL_PARAM + i, 0.f, 1.f, defLevel[i], string::f("Operator %d level (loudness if it is heard, depth if it modulates another)", i + 1), " %", 0.f, 100.f);
			configParam(ENV_PARAM + i, 0.f, 1.f, defEnv[i], string::f("Operator %d envelope amount (how much its level falls away through the note)", i + 1), " %", 0.f, 100.f);
		}
		configParam(ALGO_PARAM, 1.f, 8.f, 1.f, "Algorithm: how the operators are wired (shown on the screen)");
		paramQuantities[ALGO_PARAM]->snapEnabled = true;
		configParam(INDEX_PARAM, 0.f, 10.f, 2.f, "Index: overall modulation depth (0 = pure tones, more = brighter and harsher)");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback of operator 4 into itself (towards a saw, then noise)", " %", 0.f, 100.f);
		configParam(TIME_PARAM, std::log2(0.002f), std::log2(8.f), std::log2(0.5f), "Time: length of the envelope after a trigger (not used when PHASE is patched)", " s", 2.f, 1.f);
		configParam(AMP_SHAPE_PARAM, std::log2(0.2f), std::log2(8.f), 1.f, "Loudness shape (left: stays loud then drops, right: falls away at once)", "", 2.f, 1.f);
		configParam(INDEX_SHAPE_PARAM, std::log2(0.2f), std::log2(8.f), std::log2(3.f), "Tone shape: the same for brightness (right = a short bright pluck)", "", 2.f, 1.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Stereo spread of the voices", " %", 0.f, 100.f);

		configInput(VOCT_INPUT, "Pitch (1 V/oct, 0 V = middle C; poly, up to 8 voices)");
		configInput(TRIG_INPUT, "Trigger (poly; restarts the envelope, works up to audio rate)");
		configInput(PHASE_INPUT, "Phase (0 to 10 V ramp; when patched the envelope follows it instead of TRIG and TIME)");
		configInput(VEL_INPUT, "Velocity (0 to 10 V, read at each hit; unpatched = full)");
		configInput(INDEX_INPUT, "Index CV (1 V = 1)");
		configInput(RATIO2_INPUT, "Operator 2 ratio CV (1 V = +1)");
		configInput(RATIO3_INPUT, "Operator 3 ratio CV (1 V = +1)");
		configInput(RATIO4_INPUT, "Operator 4 ratio CV (1 V = +1)");
		configInput(FEEDBACK_INPUT, "Feedback CV (10 V = full range)");
		configInput(ALGO_INPUT, "Algorithm CV (0 to 10 V steps through 1 to 8, added to the knob)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(POLY_OUTPUT, "Each voice separately (poly)");
		for (int i = 0; i < 4; i++)
			configOutput(OP_OUTPUT + i, string::f("Operator %d alone, from voice 1", i + 1));
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < 4; i++)
			snap[i] = true;
		resetPhases = false;
		for (FmxVoice& v : voice)
			v.reset();
	}

	// whole numbers and halves, plus a quarter at the bottom
	static float snapRatio(float r) {
		if (r < 0.375f)
			return 0.25f;
		return clamp(std::round(r * 2.f) * 0.5f, 0.5f, 32.f);
	}

	void readGlobals() {
		for (int i = 0; i < 4; i++) {
			knobRatio[i] = params[RATIO_PARAM + i].getValue();
			g.ratio[i] = snap[i] ? snapRatio(knobRatio[i]) : knobRatio[i];
			g.fine[i] = params[FINE_PARAM + i].getValue();
			g.level[i] = params[LEVEL_PARAM + i].getValue();
			g.env[i] = params[ENV_PARAM + i].getValue();
		}
		g.ampShape = std::pow(2.f, params[AMP_SHAPE_PARAM].getValue());
		g.indexShape = std::pow(2.f, params[INDEX_SHAPE_PARAM].getValue());
		g.time = std::pow(2.f, params[TIME_PARAM].getValue());
		g.resetPhases = resetPhases;
		dispAmpShape.store(g.ampShape, std::memory_order_relaxed);
		dispIndexShape.store(g.indexShape, std::memory_order_relaxed);
	}

	void readVoice(int c) {
		FmxVoiceIn& in = vin[c];
		in.freq = dsp::FREQ_C4 * std::pow(2.f, clamp(inputs[VOCT_INPUT].getPolyVoltage(c), -6.f, 6.f));
		in.vel = inputs[VEL_INPUT].isConnected() ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 1.f;
		in.index = clamp(params[INDEX_PARAM].getValue() + inputs[INDEX_INPUT].getPolyVoltage(c), 0.f, 10.f);
		in.feedback = clamp(params[FEEDBACK_PARAM].getValue() + inputs[FEEDBACK_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f);
		in.algorithm = clamp((int)std::lround(params[ALGO_PARAM].getValue() + inputs[ALGO_INPUT].getPolyVoltage(c) * 0.7f), 1, 8);
		for (int k = 0; k < 3; k++) {
			float cv = inputs[RATIO2_INPUT + k].getPolyVoltage(c);
			if (cv == 0.f) {
				in.ratioOffset[k] = 0.f;
				continue;
			}
			// with snap on, the CV steps through the same list the knob does
			float r = knobRatio[k + 1] + cv;
			r = snap[k + 1] ? snapRatio(r) : clamp(r, 0.05f, 32.f);
			in.ratioOffset[k] = r - g.ratio[k + 1];
		}
	}

	void process(const ProcessArgs& args) override {
		int channels = std::max(inputs[VOCT_INPUT].getChannels(), inputs[TRIG_INPUT].getChannels());
		channels = clamp(std::max(channels, inputs[PHASE_INPUT].getChannels()), 1, VOICES);
		if (channels < lastChannels)
			for (int c = channels; c < lastChannels; c++)
				voice[c].reset();
		lastChannels = channels;

		bool tick = divider == 0;
		divider = (divider + 1) & 7;
		if (tick) {
			readGlobals();
			for (int c = 0; c < channels; c++)
				voice[c].setSampleRate(args.sampleRate);
		}

		bool phaseDriven = inputs[PHASE_INPUT].isConnected();
		bool drone = !phaseDriven && !inputs[TRIG_INPUT].isConnected();
		bool wantOps = outputs[OP_OUTPUT + 0].isConnected() || outputs[OP_OUTPUT + 1].isConnected()
			|| outputs[OP_OUTPUT + 2].isConnected() || outputs[OP_OUTPUT + 3].isConnected();
		float spread = params[SPREAD_PARAM].getValue();
		float left = 0.f, right = 0.f;
		float ops[4] = {0.f, 0.f, 0.f, 0.f};
		outputs[POLY_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; c++) {
			FmxVoiceIn& in = vin[c];
			in.trig = trigIn.process(c, inputs[TRIG_INPUT].getPolyVoltage(c));
			in.phaseDriven = phaseDriven;
			in.drone = drone;
			in.phase = inputs[PHASE_INPUT].getPolyVoltage(c) * 0.1f;
			// the knobs and CVs are read a few thousand times a second, and afresh at every hit
			if (tick || in.trig)
				readVoice(c);
			float v = voice[c].process(g, in, c == 0 && wantOps ? ops : nullptr);
			outputs[POLY_OUTPUT].setVoltage(v, c);
			float pan = channels > 1 ? (2.f * (float)c / (float)(channels - 1) - 1.f) * spread : 0.f;
			left += v * (1.f - std::max(pan, 0.f));
			right += v * (1.f + std::min(pan, 0.f));
		}
		outputs[L_OUTPUT].setVoltage(clamp(left, -12.f, 12.f));
		outputs[R_OUTPUT].setVoltage(clamp(right, -12.f, 12.f));
		for (int i = 0; i < 4; i++)
			outputs[OP_OUTPUT + i].setVoltage(ops[i]);

		if (tick) {
			dispAlgo.store(vin[0].algorithm, std::memory_order_relaxed);
			dispFeedback.store(vin[0].feedback, std::memory_order_relaxed);
			dispE.store(voice[0].envPosition(), std::memory_order_relaxed);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* s = json_array();
		for (int i = 0; i < 4; i++)
			json_array_append_new(s, json_boolean(snap[i]));
		json_object_set_new(root, "snap", s);
		json_object_set_new(root, "resetPhases", json_boolean(resetPhases));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "snap");
		if (s && json_is_array(s))
			for (int i = 0; i < 4; i++) {
				json_t* v = json_array_get(s, i);
				if (v)
					snap[i] = json_is_true(v);
			}
		json_t* r = json_object_get(root, "resetPhases");
		if (r)
			resetPhases = json_is_true(r);
	}
};

// The screen. Top: the current algorithm as boxes and wires (filled boxes are
// the operators you hear; signal flows downwards). Bottom: the loudness curve
// (bright) and the tone curve (dim) across one envelope, with a marker
// showing where voice 1 is.
struct FmxDisplay : Widget {
	Fmx* module = nullptr;

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
			int algoNumber = module ? module->dispAlgo.load() : 1;
			float fbAmount = module ? module->dispFeedback.load() : 0.f;
			float e = module ? module->dispE.load() : 0.25f;
			float ka = module ? module->dispAmpShape.load() : 2.f;
			float ki = module ? module->dispIndexShape.load() : 3.f;
			NVGcolor teal = theme::sound();
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));

			// ---- algorithm ----
			// where each operator's box sits, in columns and rows, for algorithms 1..8
			static const float POS[8][4][2] = {
				{{1.5f, 3}, {1.5f, 2}, {1.5f, 1}, {1.5f, 0}},
				{{1.5f, 2}, {1.5f, 1}, {2, 0}, {1, 0}},
				{{1, 1}, {1, 0}, {2, 1}, {2, 0}},
				{{0.5f, 1}, {1.5f, 1}, {2.5f, 1}, {1.5f, 0}},
				{{1.5f, 2}, {1, 1}, {2, 1}, {1.5f, 0}},
				{{1.5f, 2}, {1, 1}, {2, 1}, {2, 0}},
				{{0.5f, 1}, {1.5f, 1}, {2.5f, 1}, {0.5f, 0}},
				{{0, 0}, {1, 0}, {2, 0}, {3, 0}},
			};
			static const int ROWS[8] = {4, 3, 2, 2, 3, 3, 2, 1};
			const FmxAlgorithm& alg = fmxAlgorithm(algoNumber);
			const float (*pos)[2] = POS[clamp(algoNumber, 1, 8) - 1];
			int rows = ROWS[clamp(algoNumber, 1, 8) - 1];
			float areaH = box.size.y * 0.62f;
			float bw = 15.f, bh = 11.f;
			float colW = (box.size.x - 16.f) / 4.f;
			float rowH = std::min(18.f, (areaH - 16.f) / (float)rows);
			float top = 12.f + (areaH - 16.f - rowH * (float)rows) * 0.5f;
			auto cx = [&](int i) { return 8.f + colW * (pos[i][0] + 0.5f); };
			auto cy = [&](int i) { return top + rowH * (pos[i][1] + 0.5f); };

			nvgFontSize(vg, 9.f);
			if (font)
				nvgFontFaceId(vg, font->handle);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, theme::dim());
			nvgText(vg, 4.f, 3.f, string::f("ALGO %d", algoNumber).c_str(), NULL);

			// wires from each modulator to what it feeds
			nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.7f));
			nvgStrokeWidth(vg, 1.f);
			for (int i = 0; i < 4; i++)
				for (int j = 0; j < 4; j++)
					if ((alg.mods[i] >> j) & 1) {
						nvgBeginPath(vg);
						nvgMoveTo(vg, cx(j), cy(j) + bh * 0.5f);
						nvgLineTo(vg, cx(i), cy(i) - bh * 0.5f);
						nvgStroke(vg);
					}
			// the operators you hear drop down to an output line
			float outY = top + rowH * (float)rows + 2.f;
			float minX = 1e9f, maxX = -1e9f;
			for (int i = 0; i < 4; i++)
				if ((alg.carriers >> i) & 1) {
					nvgBeginPath(vg);
					nvgMoveTo(vg, cx(i), cy(i) + bh * 0.5f);
					nvgLineTo(vg, cx(i), outY);
					nvgStrokeColor(vg, teal);
					nvgStroke(vg);
					minX = std::min(minX, cx(i));
					maxX = std::max(maxX, cx(i));
				}
			nvgBeginPath(vg);
			nvgMoveTo(vg, minX - 6.f, outY);
			nvgLineTo(vg, maxX + 6.f, outY);
			nvgStrokeColor(vg, teal);
			nvgStrokeWidth(vg, 1.4f);
			nvgStroke(vg);

			for (int i = 0; i < 4; i++) {
				bool carrier = (alg.carriers >> i) & 1;
				float x = cx(i) - bw * 0.5f, y = cy(i) - bh * 0.5f;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y, bw, bh, 2.f);
				nvgFillColor(vg, carrier ? teal : theme::screen());
				nvgFill(vg);
				nvgStrokeColor(vg, carrier ? teal : theme::text());
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
				nvgFillColor(vg, carrier ? theme::screen() : theme::text());
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFontSize(vg, 10.f);
				nvgText(vg, cx(i), cy(i) + 0.5f, string::f("%d", i + 1).c_str(), NULL);
			}
			// operator 4's feedback loop, brighter the more there is
			if (fbAmount > 0.01f) {
				float x = cx(3) + bw * 0.5f, y = cy(3);
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, y + 2.5f);
				nvgLineTo(vg, x + 5.f, y + 2.5f);
				nvgLineTo(vg, x + 5.f, y - 2.5f);
				nvgLineTo(vg, x, y - 2.5f);
				nvgStrokeColor(vg, nvgTransRGBAf(theme::time(), 0.35f + 0.65f * fbAmount));
				nvgStrokeWidth(vg, 1.2f);
				nvgStroke(vg);
			}

			// ---- envelope curves ----
			float ex = 5.f, ew = box.size.x - 10.f;
			float ey = areaH + 6.f, eh = box.size.y - ey - 5.f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, ex, ey + eh);
			nvgLineTo(vg, ex + ew, ey + eh);
			nvgStrokeColor(vg, theme::grid());
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			for (int pass = 0; pass < 2; pass++) {
				float k = pass == 0 ? ki : ka;
				nvgBeginPath(vg);
				for (int i = 0; i <= 48; i++) {
					float t = (float)i / 48.f;
					float v = FmxVoice::shape(t, k);
					float px = ex + t * ew, py = ey + (1.f - v) * eh;
					if (i == 0)
						nvgMoveTo(vg, px, py);
					else
						nvgLineTo(vg, px, py);
				}
				nvgStrokeColor(vg, pass == 0 ? nvgTransRGBAf(theme::text(), 0.45f) : teal);
				nvgStrokeWidth(vg, pass == 0 ? 1.f : 1.5f);
				nvgStroke(vg);
			}
			if (module && e < 1.f) {
				float px = ex + clamp(e, 0.f, 1.f) * ew;
				nvgBeginPath(vg);
				nvgMoveTo(vg, px, ey);
				nvgLineTo(vg, px, ey + eh);
				nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.6f));
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}
};

struct FmxWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	FmxWidget(Fmx* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Fmx.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		FmxDisplay* display = new FmxDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		for (int i = 0; i < 4; i++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::RATIO[i]), module, Fmx::RATIO_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::FINE[i]), module, Fmx::FINE_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::LEVEL[i]), module, Fmx::LEVEL_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::ENV[i]), module, Fmx::ENV_PARAM + i));
			addOutput(createOutputCentered<PJ301MPort>(at(L::OP_OUT[i]), module, Fmx::OP_OUTPUT + i));
		}
		addParam(createParamCentered<RoundBlackKnob>(at(L::ALGO), module, Fmx::ALGO_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::INDEX), module, Fmx::INDEX_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::FEEDBACK), module, Fmx::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::TIME), module, Fmx::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::AMP_SHAPE), module, Fmx::AMP_SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::INDEX_SHAPE), module, Fmx::INDEX_SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SPREAD), module, Fmx::SPREAD_PARAM));

		addInput(createInputCentered<PJ301MPort>(at(L::VOCT_IN), module, Fmx::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::TRIG_IN), module, Fmx::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Fmx::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::VEL_IN), module, Fmx::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::INDEX_IN), module, Fmx::INDEX_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RATIO2_IN), module, Fmx::RATIO2_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RATIO3_IN), module, Fmx::RATIO3_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RATIO4_IN), module, Fmx::RATIO4_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::FEEDBACK_IN), module, Fmx::FEEDBACK_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::ALGO_IN), module, Fmx::ALGO_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::L_OUT), module, Fmx::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::R_OUT), module, Fmx::R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::POLY_OUT), module, Fmx::POLY_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Fmx* m = getModule<Fmx>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Snap ratio to whole numbers and halves (off = free, for clangs and beating)"));
		for (int i = 0; i < 4; i++)
			menu->addChild(createBoolPtrMenuItem(string::f("Operator %d", i + 1), "", &m->snap[i]));
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Reset operator phases on trigger (every hit identical; hard, buzzy rolls at audio rate)", "", &m->resetPhases));
	}
};

Model* modelFmx = createModel<Fmx, FmxWidget>("Fmx");
