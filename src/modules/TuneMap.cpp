// TUNE MAP: harmonic fields that morph (spec §13C). The fields, the morph and
// the quantiser are in dsp/TuneCore; this file is the Rack module around them.
#include "plugin.hpp"
#include "modules/TuneMapLayout.hpp"
#include "dsp/TuneCore.hpp"
#include "common/StateSwap.hpp"
#include "common/Theme.hpp"
#include <atomic>

using namespace codex;
namespace L = layout_tunemap;

struct TuneMap : Module {
	enum ParamId { FIELD_A_PARAM, FIELD_B_PARAM, MORPH_PARAM, STRENGTH_PARAM, HYST_PARAM, LEARN_PARAM, PARAMS_LEN };
	enum InputId { IN_INPUT, ROOT_INPUT, MORPH_INPUT, AUDIO_INPUT, LEARN_INPUT, FIELD_A_INPUT, FIELD_B_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, CHANGED_OUTPUT, FIELD_OUTPUT, OUTPUTS_LEN };
	enum LightId { LEARN_LIGHT, LIGHTS_LEN };

	// The three fields the module owns: USER 1, USER 2 and LEARNED.
	enum { USER_1, USER_2, LEARNED, OWN };
	struct Own {
		TuneField field[OWN];
		int mask = 7; // which of them this update carries
	};

	Own own;                 // audio thread's copy
	StateSwap<Own> incoming; // edits from the screen or a loaded patch
	Own* spare = nullptr;

	TuneMorph morph;
	TuneQuantiser quant;
	TuneChannel chan[16];
	dsp::PulseGenerator changedPulse[16];
	int fieldA = -1, fieldB = -1;
	bool rebuild = true;
	float morphNow = -1.f;
	int controlDivider = 0;
	float strength = 1.f, hyst = 10.f, root = 0.f;
	float fieldVolts[16];
	float lastIn[16] = {}, lastOut[16] = {};
	int fieldCount = 0;

	TuneLearner learner;
	bool learning = false;
	dsp::SchmittTrigger learnGate;
	std::atomic<int> learnedFlash{0};

	// A copy for the screen and for saving. The audio thread only tries the lock.
	struct View {
		TuneField own[OWN];
		int fieldA = FIELD_MINOR, fieldB = FIELD_HARM_16_32;
		float morph = 0.f;
		float cents[TuneMorph::MAX], weight[TuneMorph::MAX];
		int n = 0;
		float inCents[16], outCents[16];
		int channels = 0;
		bool learning = false;
	};
	TryLock viewLock;
	View view;

	TuneMap() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> names;
		for (int i = 1; i <= FIELD_COUNT; i++)
			names.push_back(tuneFieldName(i));
		configSwitch(FIELD_A_PARAM, 1.f, (float)FIELD_COUNT, (float)FIELD_MINOR, "Field A", names);
		configSwitch(FIELD_B_PARAM, 1.f, (float)FIELD_COUNT, (float)FIELD_HARM_16_32, "Field B", names);
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph from field A to field B", " %", 0.f, 100.f);
		configParam(STRENGTH_PARAM, 0.f, 1.f, 1.f, "Strength (less = pitches are only pulled toward the field)", " %", 0.f, 100.f);
		configParam(HYST_PARAM, 0.f, 50.f, 10.f, "Hysteresis (stops the output flicking between two pitches)", " cents");
		configButton(LEARN_PARAM, "Learn a field from AUDIO while held");
		configInput(IN_INPUT, "Pitch in (1 V/oct, poly)");
		configInput(ROOT_INPUT, "Root (1 V/oct; transposes the field)");
		configInput(MORPH_INPUT, "Morph CV (0 to 10 V adds 0 to 100 %)");
		configInput(AUDIO_INPUT, "Audio to learn a field from");
		configInput(LEARN_INPUT, "Learn gate (learns while high)");
		configInput(FIELD_A_INPUT, "Field A CV (each 0.67 V moves one field on)");
		configInput(FIELD_B_INPUT, "Field B CV (each 0.67 V moves one field on)");
		configOutput(OUT_OUTPUT, "Pitch out (1 V/oct, poly)");
		configOutput(CHANGED_OUTPUT, "Trigger when a channel moves to a new pitch (poly)");
		configOutput(FIELD_OUTPUT, "The allowed pitches in the octave above the root (poly, up to 16, 1 V/oct)");
		configLight(LEARN_LIGHT, "Learning");
		defaultOwn(own);
		view.own[USER_1] = own.field[USER_1];
		view.own[USER_2] = own.field[USER_2];
		view.own[LEARNED] = own.field[LEARNED];
	}

	~TuneMap() { delete spare; }

	static void defaultOwn(Own& o) {
		// USER 1 starts as all twelve semitones, USER 2 as a whole-tone scale, LEARNED empty
		o.field[USER_1].clear();
		for (int i = 0; i < 12; i++)
			o.field[USER_1].add(100.f * i);
		o.field[USER_2].clear();
		for (int i = 0; i < 6; i++)
			o.field[USER_2].add(200.f * i);
		o.field[LEARNED].clear();
		o.mask = 7;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		Own* o = new Own;
		defaultOwn(*o);
		incoming.offer(o);
	}

	void getField(int id, TuneField& f) const {
		if (tuneBuiltIn(id, f))
			return;
		if (id == FIELD_USER_1)
			f = own.field[USER_1];
		else if (id == FIELD_USER_2)
			f = own.field[USER_2];
		else
			f = own.field[LEARNED];
	}

	int readField(int param, int input) {
		float v = params[param].getValue() + inputs[input].getVoltage() * 1.5f;
		return clamp((int)std::lround(v), 1, (int)FIELD_COUNT);
	}

	void control() {
		// edits from the screen or a loaded patch
		Own* next = incoming.take(spare);
		if (next != spare) {
			spare = next;
			for (int k = 0; k < OWN; k++)
				if (next->mask & (1 << k))
					own.field[k] = next->field[k];
			rebuild = true;
		}
		int a = readField(FIELD_A_PARAM, FIELD_A_INPUT), b = readField(FIELD_B_PARAM, FIELD_B_INPUT);
		if (a != fieldA || b != fieldB || rebuild) {
			fieldA = a;
			fieldB = b;
			TuneField A, B;
			getField(a, A);
			getField(b, B);
			morph.build(A, B);
			rebuild = false;
			morphNow = -1.f;
		}
		float m = clamp(params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		if (m != morphNow) {
			morphNow = m;
			quant.set(morph, m);
			float c[16];
			fieldCount = quant.firstOctave(c, 16);
			for (int i = 0; i < fieldCount; i++)
				fieldVolts[i] = c[i] / 1200.f;
		}
		strength = params[STRENGTH_PARAM].getValue();
		hyst = params[HYST_PARAM].getValue();
		root = inputs[ROOT_INPUT].getVoltage();
		lights[LEARN_LIGHT].setBrightness(learning ? 1.f : (learnedFlash.load() > 0 ? 0.4f : 0.f));
	}

	void process(const ProcessArgs& args) override {
		if (controlDivider == 0)
			control();
		controlDivider = (controlDivider + 1) & 15;

		// learning
		bool gate = params[LEARN_PARAM].getValue() > 0.5f || inputs[LEARN_INPUT].getVoltage() >= 1.f;
		if (gate && !learning) {
			learning = true;
			learner.start();
		}
		if (learning) {
			learner.feed(inputs[AUDIO_INPUT].getVoltage() * 0.2f);
			if (!gate) {
				learning = false;
				TuneField f;
				if (learner.finish(args.sampleRate, f)) {
					own.field[LEARNED] = f;
					rebuild = true;
					learnedFlash = (int)(args.sampleRate * 0.5f);
				}
			}
		}
		if (learnedFlash.load(std::memory_order_relaxed) > 0)
			learnedFlash.fetch_sub(1, std::memory_order_relaxed);

		int channels = std::max(1, inputs[IN_INPUT].getChannels());
		outputs[OUT_OUTPUT].setChannels(channels);
		outputs[CHANGED_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; c++) {
			float in = inputs[IN_INPUT].getVoltage(c);
			float out = quant.process(chan[c], in, root, strength, hyst);
			if (chan[c].changed)
				changedPulse[c].trigger(1e-3f);
			outputs[OUT_OUTPUT].setVoltage(out, c);
			outputs[CHANGED_OUTPUT].setVoltage(changedPulse[c].process(args.sampleTime) ? 10.f : 0.f, c);
			if (controlDivider == 1) {
				lastIn[c] = (in - root) * 1200.f;
				lastOut[c] = (out - root) * 1200.f;
			}
		}
		outputs[FIELD_OUTPUT].setChannels(std::max(1, fieldCount));
		for (int i = 0; i < std::max(1, fieldCount); i++)
			outputs[FIELD_OUTPUT].setVoltage(fieldCount ? root + fieldVolts[i] : root, i);

		if (controlDivider == 1 && viewLock.tryLock()) {
			for (int k = 0; k < OWN; k++)
				view.own[k] = own.field[k];
			view.fieldA = fieldA;
			view.fieldB = fieldB;
			view.morph = morphNow;
			view.n = quant.n;
			for (int k = 0; k < quant.n; k++) {
				view.cents[k] = quant.cents[k];
				view.weight[k] = quant.weight[k];
			}
			view.channels = inputs[IN_INPUT].isConnected() ? channels : 0;
			for (int c = 0; c < channels; c++) {
				view.inCents[c] = lastIn[c];
				view.outCents[c] = lastOut[c];
			}
			view.learning = learning;
			viewLock.unlock();
		}
	}

	// ---- saving (UI thread) ----
	static json_t* fieldToJson(const TuneField& f) {
		json_t* j = json_object();
		json_object_set_new(j, "period", json_real(f.period));
		json_t* c = json_array();
		json_t* w = json_array();
		for (int i = 0; i < f.n; i++) {
			json_array_append_new(c, json_real(std::round(f.cents[i] * 100.f) / 100.f));
			json_array_append_new(w, json_real(f.weight[i]));
		}
		json_object_set_new(j, "cents", c);
		json_object_set_new(j, "weights", w);
		return j;
	}
	static void fieldFromJson(json_t* j, TuneField& f) {
		json_t* p = json_object_get(j, "period");
		f.clear(p ? clamp((float)json_number_value(p), 100.f, 4800.f) : 1200.f);
		json_t* c = json_object_get(j, "cents");
		json_t* w = json_object_get(j, "weights");
		for (size_t i = 0; c && i < json_array_size(c); i++) {
			json_t* wi = w ? json_array_get(w, i) : nullptr;
			f.add((float)json_number_value(json_array_get(c, i)), wi ? clamp((float)json_number_value(wi), 0.f, 1.f) : 1.f);
		}
		f.sort();
	}

	void copyOwn(TuneField out[OWN]) {
		viewLock.lock();
		for (int k = 0; k < OWN; k++)
			out[k] = view.own[k];
		viewLock.unlock();
	}

	json_t* dataToJson() override {
		TuneField f[OWN];
		copyOwn(f);
		json_t* root = json_object();
		json_object_set_new(root, "user1", fieldToJson(f[USER_1]));
		json_object_set_new(root, "user2", fieldToJson(f[USER_2]));
		json_object_set_new(root, "learned", fieldToJson(f[LEARNED]));
		return root;
	}

	void dataFromJson(json_t* root) override {
		Own* o = new Own;
		defaultOwn(*o);
		const char* keys[OWN] = {"user1", "user2", "learned"};
		for (int k = 0; k < OWN; k++) {
			json_t* j = json_object_get(root, keys[k]);
			if (j)
				fieldFromJson(j, o->field[k]);
		}
		o->mask = 7;
		// the screen shows these straight away, before the audio thread has taken them
		viewLock.lock();
		for (int k = 0; k < OWN; k++)
			view.own[k] = o->field[k];
		viewLock.unlock();
		incoming.offer(o);
	}

	// UI thread: add or remove a point of USER 1 or USER 2.
	void toggleUserPoint(int which, float cents) {
		TuneField f[OWN];
		copyOwn(f);
		TuneField& u = f[which];
		int hit = -1;
		for (int i = 0; i < u.n; i++) {
			float d = std::fabs(u.cents[i] - cents);
			if (std::min(d, 1200.f - d) < 20.f)
				hit = i;
		}
		if (hit >= 0) {
			for (int i = hit; i < u.n - 1; i++) {
				u.cents[i] = u.cents[i + 1];
				u.weight[i] = u.weight[i + 1];
			}
			u.n--;
		}
		else
			u.add(cents);
		u.sort();
		Own* o = new Own;
		o->field[which] = u;
		o->mask = 1 << which;
		viewLock.lock();
		view.own[which] = u;
		viewLock.unlock();
		incoming.offer(o);
	}

	void clearUser(int which, bool chromatic) {
		Own* o = new Own;
		o->field[which].clear();
		if (chromatic)
			for (int i = 0; i < 12; i++)
				o->field[which].add(100.f * i);
		o->mask = 1 << which;
		viewLock.lock();
		view.own[which] = o->field[which];
		viewLock.unlock();
		incoming.offer(o);
	}

	int editing = USER_1; // which user field a click on the circle changes; -1 = none
};

// The circle: one turn is one octave, the root at the top. Field A's points sit
// on the outer ring, field B's on the inner ring, and the morphed field (what
// the quantiser uses) in between, sized by weight. Thin needles show each
// channel's input and a bright tick its output. Click the circle to add or
// remove a point of the USER field being edited.
struct TuneDisplay : OpaqueWidget {
	TuneMap* module = nullptr;
	TuneMap::View* v = new TuneMap::View;

	~TuneDisplay() { delete v; }

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, theme::screen());
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}

	Vec centre() const { return Vec(box.size.x * 0.5f, box.size.y * 0.5f + 4.f); }
	float radius() const { return std::min(box.size.x, box.size.y) * 0.5f - 12.f; }
	static float fold(float c) {
		c = std::fmod(c, 1200.f);
		return c < 0.f ? c + 1200.f : c;
	}
	Vec point(float cents, float r) const {
		float a = fold(cents) / 1200.f * 2.f * (float)M_PI - (float)M_PI * 0.5f;
		Vec c = centre();
		return Vec(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT || module->editing < 0) {
			OpaqueWidget::onButton(e);
			return;
		}
		Vec d = e.pos.minus(centre());
		if (d.norm() < radius() * 0.3f || d.norm() > radius() + 10.f) {
			OpaqueWidget::onButton(e);
			return;
		}
		float a = std::atan2(d.y, d.x) + (float)M_PI * 0.5f;
		float cents = fold(a / (2.f * (float)M_PI) * 1200.f);
		// quarter-tones normally; hold Ctrl (Cmd on a Mac) for any cent
		if (!((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL))
			cents = fold(std::round(cents / 50.f) * 50.f);
		module->toggleUserPoint(module->editing, cents);
		e.consume(this);
	}

	void ring(NVGcontext* vg, float r, NVGcolor c) {
		Vec o = centre();
		nvgBeginPath(vg);
		nvgCircle(vg, o.x, o.y, r);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, 0.7f);
		nvgStroke(vg);
	}

	void dots(NVGcontext* vg, const TuneField& f, float r, float size, NVGcolor c) {
		for (int i = 0; i < f.n; i++) {
			Vec p = point(f.cents[i], r);
			nvgBeginPath(vg);
			nvgCircle(vg, p.x, p.y, size);
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			nvgScissor(vg, 0, 0, box.size.x, box.size.y);
			int editing = -1;
			if (module) {
				module->viewLock.lock();
				*v = module->view;
				module->viewLock.unlock();
				editing = module->editing;
			}
			else {
				TuneField a, b;
				tuneBuiltIn(FIELD_MINOR, a);
				tuneBuiltIn(FIELD_HARM_16_32, b);
				TuneMorph m;
				m.build(a, b);
				m.at(0.4f, v->cents, v->weight, v->n);
				v->fieldA = FIELD_MINOR;
				v->fieldB = FIELD_HARM_16_32;
				v->morph = 0.4f;
			}
			NVGcolor amber = theme::time();
			float R = radius();
			Vec o = centre();
			ring(vg, R, theme::grid());
			ring(vg, R * 0.62f, theme::grid());
			// 12 faint semitone ticks for orientation
			nvgBeginPath(vg);
			for (int s = 0; s < 12; s++) {
				Vec p0 = point(100.f * s, R + 2.f), p1 = point(100.f * s, R + (s == 0 ? 7.f : 4.f));
				nvgMoveTo(vg, p0.x, p0.y);
				nvgLineTo(vg, p1.x, p1.y);
			}
			nvgStrokeColor(vg, theme::dim());
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);

			// field A outside, field B inside
			TuneField A, B;
			auto get = [&](int id, TuneField& f) {
				if (tuneBuiltIn(id, f))
					return;
				f = v->own[id == FIELD_USER_1 ? TuneMap::USER_1 : (id == FIELD_USER_2 ? TuneMap::USER_2 : TuneMap::LEARNED)];
			};
			get(v->fieldA, A);
			get(v->fieldB, B);
			// fields longer than an octave are folded into one turn
			dots(vg, A, R, 1.8f, theme::text());
			dots(vg, B, R * 0.62f, 1.8f, theme::dim());

			// the morphed field between them
			for (int k = 0; k < v->n; k++) {
				if (v->weight[k] < 0.02f)
					continue;
				Vec p = point(v->cents[k], R * 0.81f);
				nvgBeginPath(vg);
				nvgCircle(vg, p.x, p.y, 1.f + 2.f * clamp(v->weight[k], 0.f, 1.f));
				nvgFillColor(vg, v->weight[k] >= TuneQuantiser::MIN_WEIGHT ? amber : nvgTransRGBAf(amber, 0.35f));
				nvgFill(vg);
			}

			// input needles and output ticks
			for (int c = 0; c < v->channels; c++) {
				Vec p = point(v->inCents[c], R * 0.95f);
				nvgBeginPath(vg);
				nvgMoveTo(vg, o.x, o.y);
				nvgLineTo(vg, p.x, p.y);
				nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.35f));
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
				Vec q0 = point(v->outCents[c], R * 0.7f), q1 = point(v->outCents[c], R * 0.92f);
				nvgBeginPath(vg);
				nvgMoveTo(vg, q0.x, q0.y);
				nvgLineTo(vg, q1.x, q1.y);
				nvgStrokeColor(vg, amber);
				nvgStrokeWidth(vg, 1.6f);
				nvgStroke(vg);
			}

			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font)
				nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, theme::text());
			nvgText(vg, 3.f, 2.f, string::f("A %s", tuneFieldName(v->fieldA)).c_str(), NULL);
			nvgFillColor(vg, theme::dim());
			nvgText(vg, 3.f, 10.f, string::f("B %s", tuneFieldName(v->fieldB)).c_str(), NULL);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, amber);
			nvgText(vg, o.x, o.y, v->learning ? "LEARN" : string::f("%d%%", (int)std::lround(v->morph * 100.f)).c_str(), NULL);
			if (editing >= 0 && (v->fieldA == FIELD_USER_1 + editing || v->fieldB == FIELD_USER_1 + editing)) {
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
				nvgFillColor(vg, theme::dim());
				nvgText(vg, box.size.x - 3.f, box.size.y - 2.f, string::f("click: USER %d", editing + 1).c_str(), NULL);
			}
			nvgRestore(vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

struct TuneMapWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	TuneMapWidget(TuneMap* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TuneMap.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		TuneDisplay* display = new TuneDisplay;
		display->module = module;
		display->box.pos = at(L::DISPLAY[0]);
		display->box.size = at(L::DISPLAY[1]);
		addChild(display);

		addParam(createParamCentered<RoundBlackSnapKnob>(at(L::FIELD_A), module, TuneMap::FIELD_A_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(at(L::FIELD_B), module, TuneMap::FIELD_B_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::MORPH), module, TuneMap::MORPH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::HYST), module, TuneMap::HYST_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::STRENGTH), module, TuneMap::STRENGTH_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::LEARN), module, TuneMap::LEARN_PARAM));
		addChild(createLightCentered<SmallLight<YellowLight>>(at(L::LEARN_LIGHT), module, TuneMap::LEARN_LIGHT));

		addInput(createInputCentered<PJ301MPort>(at(L::IN_IN), module, TuneMap::IN_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::ROOT_IN), module, TuneMap::ROOT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MORPH_IN), module, TuneMap::MORPH_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::FIELD_A_IN), module, TuneMap::FIELD_A_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::FIELD_B_IN), module, TuneMap::FIELD_B_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::AUDIO_IN), module, TuneMap::AUDIO_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::LEARN_IN), module, TuneMap::LEARN_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::OUT_OUT), module, TuneMap::OUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CHANGED_OUT), module, TuneMap::CHANGED_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::FIELD_OUT), module, TuneMap::FIELD_OUTPUT));
	}

	void step() override {
		TuneMap* m = getModule<TuneMap>();
		if (m)
			m->incoming.collect();
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		TuneMap* m = getModule<TuneMap>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clicking the circle edits", {"Nothing", "USER 1", "USER 2"},
			[=]() { return (size_t)(m->editing + 1); }, [=](size_t i) { m->editing = (int)i - 1; }));
		menu->addChild(createMenuItem("Empty USER 1", "", [=]() { m->clearUser(TuneMap::USER_1, false); }));
		menu->addChild(createMenuItem("Empty USER 2", "", [=]() { m->clearUser(TuneMap::USER_2, false); }));
		menu->addChild(createMenuItem("Set USER 1 to all 12 semitones", "", [=]() { m->clearUser(TuneMap::USER_1, true); }));
		menu->addChild(createMenuItem("Set USER 2 to all 12 semitones", "", [=]() { m->clearUser(TuneMap::USER_2, true); }));
		menu->addChild(createMenuLabel("Bohlen-Pierce, slendro and pelog are approximations"));
	}
};

Model* modelTuneMap = createModel<TuneMap, TuneMapWidget>("TuneMap");
