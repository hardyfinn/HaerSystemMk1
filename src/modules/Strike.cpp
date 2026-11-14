// STRIKE: modal percussion (spec §8). The sound engine is in dsp/StrikeCore;
// this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/StrikeLayout.hpp"
#include "dsp/StrikeCore.hpp"
#include "common/Trigger.hpp"

using namespace codex;
namespace L = layout_strike;

struct Strike : Module {
	enum ParamId {
		EXCITER_PARAM, HARD_PARAM, BURST_PARAM, RATE_PARAM, MODE_PARAM,
		PITCH_PARAM, MATERIAL_PARAM, DECAY_PARAM, DAMP_PARAM, BRIGHT_PARAM,
		INHARM_PARAM, POSITION_PARAM, SPREAD_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TRIG_INPUT, VEL_INPUT, VOCT_INPUT, MATERIAL_INPUT, DECAY_INPUT, POSITION_INPUT, DAMP_INPUT,
		BRIGHT_INPUT, EXCITE_INPUT, CHOKE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, POLY_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	static const int VOICES = 8;
	StrikeVoice voice[VOICES];
	StrikeVoiceIn vin[VOICES];
	StrikeGlobal g;
	SchmittIn trigIn, chokeIn;
	int divider = 0;
	int lastChannels = 1;
	bool chokeOnRetrig = false;

	Strike() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(EXCITER_PARAM, 0.f, 3.f, 1.f, "Exciter: what hits it (0 click, 1 mallet, 2 noise burst, 3 the EXCITE input; in between = a blend)");
		configParam(HARD_PARAM, 0.f, 1.f, 0.7f, "Hardness (soft and dull to hard and bright; in formant mode, how long the voice stays open each cycle)", " %", 0.f, 100.f);
		configParam(BURST_PARAM, 1.f, 16.f, 1.f, "Burst: hits per trigger (more than 1 = a rattle or roll)");
		paramQuantities[BURST_PARAM]->snapEnabled = true;
		configParam(RATE_PARAM, std::log2(2.f), std::log2(200.f), std::log2(40.f), "Burst rate", " Hz", 2.f, 1.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Mode", {"Formant (a voice: TRIG is a gate, MATERIAL picks the vowel)", "Modal (a struck object)"});
		configParam(PITCH_PARAM, std::log2(20.f), std::log2(5000.f), std::log2(220.f), "Pitch of the lowest mode", " Hz", 2.f, 1.f);
		configParam(MATERIAL_PARAM, 0.f, 4.f, 2.f, "Material (0 wood, 1 rubber, 2 metal, 3 glass, 4 drum skin; formant mode: a, e, i, o, u)");
		configParam(DECAY_PARAM, std::log2(0.005f), std::log2(10.f), 0.f, "Decay: ring time in metal (other materials ring shorter)", " s", 2.f, 1.f);
		configParam(DAMP_PARAM, 0.f, 1.f, 0.25f, "Damp: how much faster the high modes die", " %", 0.f, 100.f);
		configParam(BRIGHT_PARAM, 0.f, 1.f, 0.5f, "Brightness (dark to bright)", " %", 0.f, 100.f);
		configParam(INHARM_PARAM, -1.f, 1.f, 0.f, "Inharmonicity: squeezes or stretches the modes apart (formant mode: size of the mouth, small to large)");
		configParam(POSITION_PARAM, 0.f, 1.f, 0.3f, "Strike position (end to end; the middle gives the hollowest sound)", " %", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Stereo spread of the voices", " %", 0.f, 100.f);

		configInput(TRIG_INPUT, "Trigger (poly: one voice per channel, up to 8; formant mode: gate)");
		configInput(VEL_INPUT, "Velocity (0 to 10 V, read at each hit; unpatched = full)");
		configInput(VOCT_INPUT, "Pitch (1 V/oct, added to PITCH)");
		configInput(MATERIAL_INPUT, "Material CV (10 V = two materials along)");
		configInput(DECAY_INPUT, "Decay CV (2 V doubles the ring time)");
		configInput(POSITION_INPUT, "Strike position CV (10 V = half the length)");
		configInput(DAMP_INPUT, "Damp CV (10 V = full range)");
		configInput(BRIGHT_INPUT, "Brightness CV (10 V = full range)");
		configInput(EXCITE_INPUT, "External exciter audio (heard when EXCITER is towards 3)");
		configInput(CHOKE_INPUT, "Choke trigger: silences the ringing in 5 ms");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(POLY_OUTPUT, "Each voice separately (poly)");
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		chokeOnRetrig = false;
		for (StrikeVoice& v : voice)
			v.reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		for (StrikeVoice& v : voice)
			v.setSampleRate(e.sampleRate);
	}

	void readGlobals() {
		g.exciter = params[EXCITER_PARAM].getValue();
		g.hardness = params[HARD_PARAM].getValue();
		g.inharm = params[INHARM_PARAM].getValue();
		g.burst = (int)std::lround(params[BURST_PARAM].getValue());
		g.burstRate = std::pow(2.f, params[RATE_PARAM].getValue());
		g.formant = params[MODE_PARAM].getValue() < 0.5f;
		g.chokeOnRetrig = chokeOnRetrig;
		g.extGated = inputs[TRIG_INPUT].isConnected();
	}

	void readVoice(int c) {
		StrikeVoiceIn& in = vin[c];
		float pitch = params[PITCH_PARAM].getValue() + inputs[VOCT_INPUT].getPolyVoltage(c);
		in.freq = std::pow(2.f, clamp(pitch, 3.3f, 14.f)); // 10 Hz .. 16 kHz
		in.material = clamp(params[MATERIAL_PARAM].getValue() + inputs[MATERIAL_INPUT].getPolyVoltage(c) * 0.2f, 0.f, 4.f);
		in.decay = std::pow(2.f, clamp(params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getPolyVoltage(c) * 0.5f, -7.7f, 4.f));
		in.position = clamp(params[POSITION_PARAM].getValue() + inputs[POSITION_INPUT].getPolyVoltage(c) * 0.05f, 0.f, 1.f);
		in.damp = clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f);
		in.bright = clamp(params[BRIGHT_PARAM].getValue() + inputs[BRIGHT_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f);
		in.vel = inputs[VEL_INPUT].isConnected() ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 1.f;
	}

	void process(const ProcessArgs& args) override {
		int channels = clamp(std::max(inputs[TRIG_INPUT].getChannels(), inputs[VOCT_INPUT].getChannels()), 1, VOICES);
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

		bool chokeMono = inputs[CHOKE_INPUT].getChannels() <= 1;
		bool chokeAll = false;
		if (chokeMono)
			chokeAll = chokeIn.process(0, inputs[CHOKE_INPUT].getVoltage());

		float spread = params[SPREAD_PARAM].getValue();
		float left = 0.f, right = 0.f;
		outputs[POLY_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; c++) {
			StrikeVoiceIn& in = vin[c];
			in.trig = trigIn.process(c, inputs[TRIG_INPUT].getPolyVoltage(c));
			in.gate = trigIn.isHigh(c);
			in.choke = chokeMono ? chokeAll : chokeIn.process(c, inputs[CHOKE_INPUT].getVoltage(c));
			// the knobs and CVs are read a few thousand times a second, and afresh at every hit
			if (tick || in.trig)
				readVoice(c);
			in.ext = inputs[EXCITE_INPUT].getPolyVoltage(c) * 0.2f;
			float v = voice[c].process(g, in);
			outputs[POLY_OUTPUT].setVoltage(v, c);
			// voices are laid out left to right; SPREAD sets how far apart
			float pan = channels > 1 ? (2.f * (float)c / (float)(channels - 1) - 1.f) * spread : 0.f;
			left += v * (1.f - std::max(pan, 0.f));
			right += v * (1.f + std::min(pan, 0.f));
		}
		outputs[L_OUTPUT].setVoltage(StrikeVoice::softClip(left));
		outputs[R_OUTPUT].setVoltage(StrikeVoice::softClip(right));
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "chokeOnRetrig", json_boolean(chokeOnRetrig));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* c = json_object_get(root, "chokeOnRetrig");
		if (c)
			chokeOnRetrig = json_is_true(c);
	}
};

struct StrikeWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	StrikeWidget(Strike* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Strike.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(at(L::EXCITER), module, Strike::EXCITER_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::HARD), module, Strike::HARD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::BURST), module, Strike::BURST_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::RATE), module, Strike::RATE_PARAM));
		addParam(createParamCentered<CKSS>(at(L::MODE), module, Strike::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::PITCH), module, Strike::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::MATERIAL), module, Strike::MATERIAL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DECAY), module, Strike::DECAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DAMP), module, Strike::DAMP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::BRIGHT), module, Strike::BRIGHT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::INHARM), module, Strike::INHARM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::POSITION), module, Strike::POSITION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SPREAD), module, Strike::SPREAD_PARAM));

		addInput(createInputCentered<PJ301MPort>(at(L::TRIG_IN), module, Strike::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::VEL_IN), module, Strike::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::VOCT_IN), module, Strike::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MATERIAL_IN), module, Strike::MATERIAL_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DECAY_IN), module, Strike::DECAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::POSITION_IN), module, Strike::POSITION_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DAMP_IN), module, Strike::DAMP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::BRIGHT_IN), module, Strike::BRIGHT_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::EXCITE_IN), module, Strike::EXCITE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::CHOKE_IN), module, Strike::CHOKE_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::L_OUT), module, Strike::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::R_OUT), module, Strike::R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::POLY_OUT), module, Strike::POLY_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Strike* m = getModule<Strike>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Choke on retrigger (a new hit cuts off the last one, like a hand on the bar)", "", &m->chokeOnRetrig));
	}
};

Model* modelStrike = createModel<Strike, StrikeWidget>("Strike");
