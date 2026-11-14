// DEPTH: spatial reverb with distance placement (spec §11). The room is in
// dsp/DepthCore; this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/DepthLayout.hpp"
#include "dsp/DepthCore.hpp"
#include "common/Trigger.hpp"

using namespace codex;
namespace L = layout_depth;

struct Depth : Module {
	static const int CH = DepthParams::CH;
	enum ParamId {
		ENUMS(DIST_PARAM, CH), ENUMS(PAN_PARAM, CH),
		SIZE_PARAM, DECAY_PARAM, DAMP_PARAM, MOD_PARAM, INSTABILITY_PARAM, MIX_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(L_INPUT, CH), ENUMS(R_INPUT, CH), ENUMS(DIST_INPUT, CH),
		SIZE_INPUT, DECAY_INPUT, INSTABILITY_INPUT, MOD_INPUT, FREEZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, WET_L_OUTPUT, WET_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	DepthCore core;
	DepthParams p;
	SchmittIn freezeIn;
	int divider = 0;
	float rate = 0.f;

	Depth() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		static const float DIST_DEFAULT[CH] = {0.1f, 0.4f, 0.7f, 0.95f};
		for (int c = 0; c < CH; c++) {
			configParam(DIST_PARAM + c, 0.f, 1.f, DIST_DEFAULT[c], string::f("Source %d distance (close to far: later, duller, quieter, with more of the room)", c + 1), " %", 0.f, 100.f);
			configParam(PAN_PARAM + c, -1.f, 1.f, 0.f, string::f("Source %d pan (left to right)", c + 1), " %", 0.f, 100.f);
			configInput(L_INPUT + c, string::f("Source %d left (or mono)", c + 1));
			configInput(R_INPUT + c, string::f("Source %d right", c + 1));
			configInput(DIST_INPUT + c, string::f("Source %d distance CV (10 V = full range)", c + 1));
		}
		configParam(SIZE_PARAM, -2.f, 1.f, 0.f, "Room size (1x = a medium hall)", "x", 2.f, 1.f);
		configParam(DECAY_PARAM, std::log2(0.3f), std::log2(30.f), 1.f, "Decay: time for the tail to die away", " s", 2.f, 1.f);
		configParam(DAMP_PARAM, 0.f, 1.f, 0.3f, "Damp: how quickly the tail loses its top (bright to dark)", " %", 0.f, 100.f);
		configParam(MOD_PARAM, 0.f, 1.f, 0.2f, "Mod: slow movement inside the tail (none to a gentle chorus)", " %", 0.f, 100.f);
		configParam(INSTABILITY_PARAM, 0.f, 1.f, 0.f, "Instability: the tail feeds itself more than it loses, so it swells and smears (it is held by a limiter and never runs away)", " %", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix at OUT L and OUT R (direct sound only to room only)", " %", 0.f, 100.f);
		configInput(SIZE_INPUT, "Size CV (1 V/oct: 1 V doubles the room)");
		configInput(DECAY_INPUT, "Decay CV (1 V doubles the decay time)");
		configInput(INSTABILITY_INPUT, "Instability CV (10 V = full range)");
		configInput(MOD_INPUT, "Mod CV (10 V = full range)");
		configInput(FREEZE_INPUT, "Freeze gate: while high the tail is held and nothing new goes into it");
		configOutput(L_OUTPUT, "Left (direct sound and room, balanced by MIX)");
		configOutput(R_OUTPUT, "Right (direct sound and room, balanced by MIX)");
		configOutput(WET_L_OUTPUT, "Room only, left (early reflections and tail)");
		configOutput(WET_R_OUTPUT, "Room only, right (early reflections and tail)");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		core.clear();
		freezeIn.reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if (e.sampleRate != rate) {
			rate = e.sampleRate;
			core.setSampleRate(rate);
		}
	}

	void readParams() {
		for (int c = 0; c < CH; c++) {
			p.dist[c] = clamp(params[DIST_PARAM + c].getValue() + inputs[DIST_INPUT + c].getVoltage() * 0.1f, 0.f, 1.f);
			p.pan[c] = params[PAN_PARAM + c].getValue();
			p.connected[c] = inputs[L_INPUT + c].isConnected() || inputs[R_INPUT + c].isConnected();
		}
		p.size = std::pow(2.f, clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage(), -2.f, 1.f));
		p.decay = std::pow(2.f, clamp(params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getVoltage(), -1.74f, 4.91f));
		p.damp = params[DAMP_PARAM].getValue();
		p.mod = clamp(params[MOD_PARAM].getValue() + inputs[MOD_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		p.instability = clamp(params[INSTABILITY_PARAM].getValue() + inputs[INSTABILITY_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		p.mix = params[MIX_PARAM].getValue();
	}

	void process(const ProcessArgs& args) override {
		if (divider == 0)
			readParams();
		divider = (divider + 1) & 15;
		freezeIn.process(0, inputs[FREEZE_INPUT].getVoltage());
		p.freeze = freezeIn.isHigh(0);

		float inL[CH], inR[CH];
		for (int c = 0; c < CH; c++) {
			// a single cable in either jack is copied to both sides
			bool hasL = inputs[L_INPUT + c].isConnected(), hasR = inputs[R_INPUT + c].isConnected();
			float l = hasL ? inputs[L_INPUT + c].getVoltageSum() : 0.f;
			float r = hasR ? inputs[R_INPUT + c].getVoltageSum() : l;
			inL[c] = hasL ? l : r;
			inR[c] = r;
		}
		DepthOut out;
		core.process(p, inL, inR, out);
		outputs[L_OUTPUT].setVoltage(out.outL);
		outputs[R_OUTPUT].setVoltage(out.outR);
		outputs[WET_L_OUTPUT].setVoltage(out.wetL);
		outputs[WET_R_OUTPUT].setVoltage(out.wetR);
	}
};

struct DepthWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	DepthWidget(Depth* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Depth.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int c = 0; c < Depth::CH; c++) {
			addParam(createParamCentered<RoundBlackKnob>(at(L::DIST[c]), module, Depth::DIST_PARAM + c));
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::PAN[c]), module, Depth::PAN_PARAM + c));
			addInput(createInputCentered<PJ301MPort>(at(L::DIST_IN[c]), module, Depth::DIST_INPUT + c));
			addInput(createInputCentered<PJ301MPort>(at(L::L_IN[c]), module, Depth::L_INPUT + c));
			addInput(createInputCentered<PJ301MPort>(at(L::R_IN[c]), module, Depth::R_INPUT + c));
		}
		addParam(createParamCentered<RoundBlackKnob>(at(L::SIZE), module, Depth::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DECAY), module, Depth::DECAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DAMP), module, Depth::DAMP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::MOD), module, Depth::MOD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::INSTABILITY), module, Depth::INSTABILITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::MIX), module, Depth::MIX_PARAM));

		addInput(createInputCentered<PJ301MPort>(at(L::SIZE_IN), module, Depth::SIZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DECAY_IN), module, Depth::DECAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::INSTABILITY_IN), module, Depth::INSTABILITY_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MOD_IN), module, Depth::MOD_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::FREEZE_IN), module, Depth::FREEZE_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::L_OUT), module, Depth::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::R_OUT), module, Depth::R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::WET_L_OUT), module, Depth::WET_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::WET_R_OUT), module, Depth::WET_R_OUTPUT));
	}
};

Model* modelDepth = createModel<Depth, DepthWidget>("Depth");
