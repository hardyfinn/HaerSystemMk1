// Milestone M0 placeholder: a blank panel that proves the plugin loads.
// It will be removed once the real modules exist.
#include "plugin.hpp"

struct Blank : Module {
	Blank() { config(0, 0, 0, 0); }
};

struct BlankWidget : ModuleWidget {
	BlankWidget(Blank* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Blank.svg")));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
};

Model* modelBlank = createModel<Blank, BlankWidget>("Blank");
