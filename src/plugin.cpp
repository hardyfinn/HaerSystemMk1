#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelWarp);
	p->addModel(modelContour);
	p->addModel(modelMutate);
	p->addModel(modelStrike);
	p->addModel(modelFmx);
	p->addModel(modelChain);
	p->addModel(modelSplice);
	p->addModel(modelDepth);
	p->addModel(modelVault);
	p->addModel(modelSteer);
	p->addModel(modelFlow);
	p->addModel(modelTuneMap);
	p->addModel(modelEar);
}
