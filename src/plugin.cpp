#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelWarp);
	p->addModel(modelContour);
	p->addModel(modelMutate);
	p->addModel(modelStrike);
	p->addModel(modelFmx);
}
