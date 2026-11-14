// EAR: listening and groove capture (spec §13B). The analysis and the groove
// learning are in dsp/EarCore; this file is the Rack module around them.
#include "plugin.hpp"
#include "modules/EarLayout.hpp"
#include "dsp/EarCore.hpp"
#include "common/Phase.hpp"
#include "common/StateSwap.hpp"
#include "common/GrooveTarget.hpp"
#include "common/Theme.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_ear;

struct Ear : Module {
	enum ParamId { SENS_PARAM, GRID_PARAM, BARS_PARAM, AMT_PARAM, LEARN_PARAM, PARAMS_LEN };
	enum InputId { IN_A_INPUT, IN_B_INPUT, PHASE_INPUT, LEARN_INPUT, SENS_INPUT, INPUTS_LEN };
	enum OutputId {
		ONSET_A_OUTPUT, STRENGTH_A_OUTPUT, LOUD_A_OUTPUT, CENTROID_A_OUTPUT, NOISE_A_OUTPUT, PITCH_A_OUTPUT, CONF_A_OUTPUT,
		ONSET_B_OUTPUT, STRENGTH_B_OUTPUT, LOUD_B_OUTPUT, CENTROID_B_OUTPUT, NOISE_B_OUTPUT,
		GROOVE_OUTPUT, OUTPUTS_LEN
	};
	enum LightId { LEARN_LIGHT, LIGHTS_LEN };

	static constexpr int GRIDS[5] = {8, 12, 16, 24, 32};
	static constexpr int BARS[4] = {1, 2, 4, 8};

	struct Points {
		int n = 0;
		float x[EarGroove::MAX_POINTS], y[EarGroove::MAX_POINTS];
	};

	EarAnalyser a, b;
	EarGroove groove;
	PhaseReader reader;
	dsp::PulseGenerator onsetPulse[2];
	bool phaseWasConnected = false;
	int controlDivider = 0;
	float sens = 2.f, amt = 1.f;
	int grid = 16, bars = 2;

	StateSwap<Points> incoming; // a groove loaded from the patch
	Points* spare = nullptr;

	// For the screen and for saving. The audio thread only tries the lock.
	struct View {
		Points pts;
		int version = -1;
		int grid = 16;
		bool learning = false;
		int heard = 0;
		float frac = 0.f;
		float amt = 1.f;
		float pitch = 0.f, conf = 0.f;
		bool phase = false;
	};
	TryLock viewLock;
	View view;
	std::atomic<int> flash[2];

	Ear() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(SENS_PARAM, 1.f, 4.f, 2.f, "Onset sensitivity (higher = only clearer hits count)", "x");
		configSwitch(GRID_PARAM, 0.f, 4.f, 2.f, "Groove grid (steps in a bar)", {"8", "12", "16", "24", "32"});
		configSwitch(BARS_PARAM, 0.f, 3.f, 1.f, "Bars to learn", {"1", "2", "4", "8"});
		configParam(AMT_PARAM, 0.f, 1.f, 1.f, "Groove amount (0 = straight, 100 % = the learned feel)", " %", 0.f, 100.f);
		configButton(LEARN_PARAM, "Learn the groove while held");
		configInput(IN_A_INPUT, "Audio A (onsets here are what the groove is learned from)");
		configInput(IN_B_INPUT, "Audio B");
		configInput(PHASE_INPUT, "Phase (0 to 10 V ramp, one cycle = one bar)");
		configInput(LEARN_INPUT, "Learn gate (learns while high)");
		configInput(SENS_INPUT, "Sensitivity CV (+1 V adds 0.3)");
		const char* side[2] = {"A", "B"};
		for (int s = 0; s < 2; s++) {
			int o = s == 0 ? ONSET_A_OUTPUT : ONSET_B_OUTPUT;
			configOutput(o, string::f("Onset %s (trigger on each hit)", side[s]));
			configOutput(o + 1, string::f("Strength %s (how hard the last hit was, 0 to 10 V, held)", side[s]));
			configOutput(o + 2, string::f("Loudness %s (0 to 10 V for -60 to 0 dB; 0 dB = ±5 V)", side[s]));
			configOutput(o + 3, string::f("Brightness %s (spectral centroid, 1 V per octave above 20 Hz)", side[s]));
			configOutput(o + 4, string::f("Noisiness %s (0 V = pure tone, 10 V = white noise)", side[s]));
		}
		configOutput(PITCH_A_OUTPUT, "Pitch A (1 V/oct, held while unsure)");
		configOutput(CONF_A_OUTPUT, "Pitch confidence A (0 to 10 V)");
		configOutput(GROOVE_OUTPUT, "Groove phase (PHASE bent by the learned groove, 0 to 10 V)");
		configLight(LEARN_LIGHT, "Learning");
		a.withPitch = true;
		flash[0] = flash[1] = 0;
	}

	~Ear() { delete spare; }

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		a.reset(e.sampleRate);
		b.reset(e.sampleRate);
	}

	void onAdd(const AddEvent& e) override {
		a.reset(APP->engine->getSampleRate());
		b.reset(APP->engine->getSampleRate());
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		incoming.offer(new Points); // an empty groove: straight
	}

	void control() {
		Points* next = incoming.take(spare);
		if (next != spare) {
			spare = next;
			if (next->n > 0)
				groove.set(next->x, next->y, next->n);
			else
				groove.clear();
		}
		sens = clamp(params[SENS_PARAM].getValue() + inputs[SENS_INPUT].getVoltage() * 0.3f, 1.f, 4.f);
		a.sens = b.sens = sens;
		grid = GRIDS[clamp((int)std::lround(params[GRID_PARAM].getValue()), 0, 4)];
		bars = BARS[clamp((int)std::lround(params[BARS_PARAM].getValue()), 0, 3)];
		amt = params[AMT_PARAM].getValue();
		lights[LEARN_LIGHT].setBrightness(groove.learning ? 1.f : 0.f);

		if (viewLock.tryLock()) {
			if (view.version != groove.version) {
				view.version = groove.version;
				view.pts.n = groove.points;
				for (int i = 0; i < groove.points; i++) {
					view.pts.x[i] = groove.px[i];
					view.pts.y[i] = groove.py[i];
				}
			}
			view.grid = grid;
			view.learning = groove.learning;
			view.heard = groove.heard;
			view.frac = (float)reader.frac();
			view.amt = amt;
			view.pitch = a.pitch;
			view.conf = a.confidence;
			view.phase = inputs[PHASE_INPUT].isConnected();
			viewLock.unlock();
		}
	}

	void process(const ProcessArgs& args) override {
		if (controlDivider == 0)
			control();
		controlDivider = (controlDivider + 1) & 31;

		bool connected = inputs[PHASE_INPUT].isConnected();
		if (connected != phaseWasConnected) {
			phaseWasConnected = connected;
			reader.reset();
		}
		if (connected)
			reader.process(inputs[PHASE_INPUT].getVoltage());
		groove.phase(reader.W);

		bool onA = a.process(inputs[IN_A_INPUT].getVoltage() * 0.2f);
		bool onB = b.process(inputs[IN_B_INPUT].getVoltage() * 0.2f);
		bool gate = params[LEARN_PARAM].getValue() > 0.5f || inputs[LEARN_INPUT].getVoltage() >= 1.f;
		groove.process(gate && connected, onA, a.onsetAge, bars, grid);

		EarAnalyser* an[2] = {&a, &b};
		bool on[2] = {onA, onB};
		for (int s = 0; s < 2; s++) {
			int o = s == 0 ? ONSET_A_OUTPUT : ONSET_B_OUTPUT;
			if (on[s]) {
				onsetPulse[s].trigger(1e-3f);
				flash[s].store(1, std::memory_order_relaxed);
			}
			outputs[o].setVoltage(onsetPulse[s].process(args.sampleTime) ? 10.f : 0.f);
			outputs[o + 1].setVoltage(an[s]->strength);
			outputs[o + 2].setVoltage(an[s]->loud);
			outputs[o + 3].setVoltage(an[s]->centroid);
			outputs[o + 4].setVoltage(an[s]->noise);
		}
		outputs[PITCH_A_OUTPUT].setVoltage(a.pitch);
		outputs[CONF_A_OUTPUT].setVoltage(a.confidence);
		outputs[GROOVE_OUTPUT].setVoltage(connected ? groove.apply((float)reader.frac(), amt) * 10.f : 0.f);
	}

	// ---- UI thread ----
	Points currentPoints() {
		viewLock.lock();
		Points p = view.pts;
		viewLock.unlock();
		return p;
	}

	int64_t linkedId = -1; // the WARP the groove goes to

	GrooveTarget* linkedWarp() {
		if (linkedId >= 0) {
			GrooveTarget* t = dynamic_cast<GrooveTarget*>(APP->engine->getModule(linkedId));
			if (t)
				return t;
		}
		// not linked (or deleted): with exactly one WARP in the rack, use that one
		GrooveTarget* only = nullptr;
		int count = 0;
		for (int64_t mid : APP->engine->getModuleIds()) {
			GrooveTarget* t = dynamic_cast<GrooveTarget*>(APP->engine->getModule(mid));
			if (t) {
				only = t;
				count++;
			}
		}
		return count == 1 ? only : nullptr;
	}

	bool sendToWarp() {
		GrooveTarget* t = linkedWarp();
		Points p = currentPoints();
		if (!t || p.n == 0)
			return false;
		float xs[EarGroove::MAX_POINTS], ys[EarGroove::MAX_POINTS];
		// WARP's drawn shape takes 14 points: keep the ones that matter most
		EarGroove tmp;
		tmp.set(p.x, p.y, p.n);
		int n = tmp.reduced(xs, ys, 14);
		t->receiveGroove(xs, ys, n);
		return true;
	}

	json_t* dataToJson() override {
		Points p = currentPoints();
		json_t* root = json_object();
		json_t* g = json_array();
		for (int i = 0; i < p.n; i++)
			json_array_append_new(g, json_pack("[f, f]", (double)p.x[i], (double)p.y[i]));
		json_object_set_new(root, "groove", g);
		json_object_set_new(root, "linkedWarp", json_integer(linkedId));
		return root;
	}

	void dataFromJson(json_t* root) override {
		Points* p = new Points;
		json_t* g = json_object_get(root, "groove");
		for (size_t i = 0; g && i < json_array_size(g) && p->n < EarGroove::MAX_POINTS; i++) {
			json_t* pt = json_array_get(g, i);
			p->x[p->n] = (float)json_number_value(json_array_get(pt, 0));
			p->y[p->n] = (float)json_number_value(json_array_get(pt, 1));
			p->n++;
		}
		json_t* l = json_object_get(root, "linkedWarp");
		linkedId = l ? (int64_t)json_integer_value(l) : -1;
		viewLock.lock();
		view.pts = *p;
		viewLock.unlock();
		incoming.offer(p);
	}
};

constexpr int Ear::GRIDS[5];
constexpr int Ear::BARS[4];

// The screen: the groove curve over one bar (straight in grey, learned in violet,
// with its points), the grid, where the bar is now, and a light for each input's onsets.
struct EarDisplay : Widget {
	Ear* module = nullptr;
	Ear::View v;
	float glow[2] = {};

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
			if (module) {
				module->viewLock.lock();
				v = module->view;
				module->viewLock.unlock();
				for (int s = 0; s < 2; s++) {
					if (module->flash[s].exchange(0))
						glow[s] = 1.f;
					glow[s] *= 0.85f;
				}
			}
			else {
				// the module browser: a swung 16th groove
				v.pts.n = 15;
				for (int i = 0; i < 15; i++) {
					v.pts.x[i] = (i + 1) / 16.f;
					v.pts.y[i] = (i + 1) / 16.f + ((i % 2) == 0 ? 0.3f / 16.f : 0.f);
				}
				v.frac = 0.4f;
				v.phase = true;
			}
			NVGcolor violet = theme::system();
			float L = 6.f, T = 6.f, W = box.size.x - 12.f, H = box.size.y - 20.f;
			auto X = [&](float x) { return L + x * W; };
			auto Y = [&](float y) { return T + (1.f - y) * H; };

			// grid
			nvgBeginPath(vg);
			for (int g = 1; g < v.grid; g++) {
				nvgMoveTo(vg, X((float)g / v.grid), T);
				nvgLineTo(vg, X((float)g / v.grid), T + H);
			}
			nvgStrokeColor(vg, theme::grid());
			nvgStrokeWidth(vg, 0.6f);
			nvgStroke(vg);
			// straight
			nvgBeginPath(vg);
			nvgMoveTo(vg, X(0), Y(0));
			nvgLineTo(vg, X(1), Y(1));
			nvgStrokeColor(vg, theme::dim());
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
			// the groove, drawn as how far each moment is pushed late (up) or early (down), magnified
			EarGroove g;
			if (v.pts.n > 0)
				g.set(v.pts.x, v.pts.y, v.pts.n);
			nvgBeginPath(vg);
			for (int i = 0; i <= 200; i++) {
				float x = i / 200.f;
				float y = g.apply(x, 1.f);
				if (i == 0)
					nvgMoveTo(vg, X(x), Y(y));
				else
					nvgLineTo(vg, X(x), Y(y));
			}
			nvgStrokeColor(vg, violet);
			nvgStrokeWidth(vg, 1.4f);
			nvgStroke(vg);
			for (int i = 0; i < v.pts.n; i++) {
				nvgBeginPath(vg);
				nvgCircle(vg, X(v.pts.x[i]), Y(v.pts.y[i]), 1.6f);
				nvgFillColor(vg, theme::text());
				nvgFill(vg);
			}
			// where the bar is now
			if (v.phase) {
				nvgBeginPath(vg);
				nvgMoveTo(vg, X(v.frac), T);
				nvgLineTo(vg, X(v.frac), T + H);
				nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.4f));
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}

			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font)
				nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, theme::text());
			std::string line;
			if (!v.phase)
				line = "patch PHASE to learn a groove";
			else if (v.learning)
				line = string::f("LEARNING  %d hits", v.heard);
			else if (v.conf >= 5.f)
				line = string::f("A %.1f Hz  %d%%", 261.6256f * std::pow(2.f, v.pitch), (int)std::lround(v.conf * 10.f));
			else
				line = v.pts.n ? string::f("groove: %d points", v.pts.n) : "no groove yet";
			nvgText(vg, 4.f, box.size.y - 7.f, line.c_str(), NULL);
			// onset lights
			const char* name[2] = {"A", "B"};
			for (int s = 0; s < 2; s++) {
				float cx = box.size.x - 22.f + s * 11.f, cy = box.size.y - 7.f;
				nvgBeginPath(vg);
				nvgCircle(vg, cx, cy, 3.5f);
				nvgFillColor(vg, nvgLerpRGBA(theme::grid(), violet, glow[s]));
				nvgFill(vg);
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, theme::dim());
				nvgText(vg, cx - 4.5f, cy, name[s], NULL);
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}
};

// A light in EAR's violet.
template <typename TBase = GrayModuleLightWidget>
struct TVioletLight : TBase {
	TVioletLight() { this->addBaseColor(theme::system()); }
};

struct EarWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	EarWidget(Ear* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Ear.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		EarDisplay* display = new EarDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(at(L::SENS), module, Ear::SENS_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(at(L::GRID), module, Ear::GRID_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(at(L::BARS), module, Ear::BARS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::AMT), module, Ear::AMT_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::LEARN), module, Ear::LEARN_PARAM));
		addChild(createLightCentered<SmallLight<TVioletLight<>>>(at(L::LEARN_LIGHT), module, Ear::LEARN_LIGHT));

		addInput(createInputCentered<PJ301MPort>(at(L::IN_A), module, Ear::IN_A_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::IN_B), module, Ear::IN_B_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Ear::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::LEARN_IN), module, Ear::LEARN_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SENS_IN), module, Ear::SENS_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::ONSET_A), module, Ear::ONSET_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::STRENGTH_A), module, Ear::STRENGTH_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::LOUD_A), module, Ear::LOUD_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CENTROID_A), module, Ear::CENTROID_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::NOISE_A), module, Ear::NOISE_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::PITCH_A), module, Ear::PITCH_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CONF_A), module, Ear::CONF_A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::ONSET_B), module, Ear::ONSET_B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::STRENGTH_B), module, Ear::STRENGTH_B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::LOUD_B), module, Ear::LOUD_B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CENTROID_B), module, Ear::CENTROID_B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::NOISE_B), module, Ear::NOISE_B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::GROOVE_OUT), module, Ear::GROOVE_OUTPUT));
	}

	void step() override {
		Ear* m = getModule<Ear>();
		if (m)
			m->incoming.collect();
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Ear* m = getModule<Ear>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		GrooveTarget* target = m->linkedWarp();
		menu->addChild(createMenuItem(target ? "Send groove to " + target->grooveLabel() : "Send groove to WARP (no WARP linked)", "",
			[=]() { m->sendToWarp(); }, !target || m->currentPoints().n == 0));
		menu->addChild(createSubmenuItem("Link to WARP", "", [=](Menu* sub) {
			int found = 0;
			for (int64_t mid : APP->engine->getModuleIds()) {
				GrooveTarget* t = dynamic_cast<GrooveTarget*>(APP->engine->getModule(mid));
				if (!t)
					continue;
				found++;
				sub->addChild(createCheckMenuItem(t->grooveLabel(), "", [=]() { return m->linkedId == mid; },
					[=]() { m->linkedId = m->linkedId == mid ? -1 : mid; }));
			}
			if (!found)
				sub->addChild(createMenuLabel("No WARP in the rack"));
		}));
		menu->addChild(createMenuItem("Forget the groove (straight again)", "", [=]() {
			m->incoming.offer(new Ear::Points);
			m->viewLock.lock();
			m->view.pts.n = 0;
			m->viewLock.unlock();
		}));
		menu->addChild(createMenuLabel("WARP plays the groove when its SHAPE is at 4 (drawn)"));
	}
};

Model* modelEar = createModel<Ear, EarWidget>("Ear");
