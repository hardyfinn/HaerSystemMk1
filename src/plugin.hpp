#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;

// One model per module. Add each new module here and in plugin.cpp and plugin.json.
extern Model* modelWarp;
extern Model* modelContour;
extern Model* modelMutate;
extern Model* modelStrike;
extern Model* modelFmx;
extern Model* modelChain;
