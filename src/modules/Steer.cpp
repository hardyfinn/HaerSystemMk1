// STEER: macros, scenes and version tree (spec §13). Curves, the tree and
// gestures are in dsp/SteerCore; this file is the Rack module.
//
// Threads: macros write their mapped parameters from process() through
// ParamHandles, as VCV's MIDI-Map does. Everything that touches other modules
// by id (snapshots, morphing between versions, VAULT launches, generator
// states) happens on the UI thread, where a module cannot vanish mid-use.
#include "plugin.hpp"
#include "modules/SteerLayout.hpp"
#include "modules/Vault.hpp"
#include "dsp/SteerCore.hpp"
#include "common/KeepProtocol.hpp"
#include "common/Phase.hpp"
#include "common/Rng.hpp"
#include "common/Theme.hpp"
#include "common/Trigger.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

using namespace codex;
namespace L = layout_steer;

struct Steer : Module {
	static const int MACROS = 8;
	static const int MAPS = 16;
	static const int SCENES = 16;
	enum ParamId {
		ENUMS(MACRO_PARAM, MACROS), ENUMS(MAP_PARAM, MACROS),
		SCENE_PARAM, TIME_PARAM, CURVE_PARAM, GO_PARAM, REC_PARAM, PLAY_PARAM, LOOSE_PARAM, SEED_PARAM, LINK_PARAM,
		SNAP_PARAM, MORPH_PARAM, GOTO_PARAM, WALK_PARAM, WALK_BARS_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(MACRO_INPUT, MACROS),
		PHASE_INPUT, SCENE_INPUT, GO_INPUT, REC_INPUT, PLAY_INPUT, SEED_INPUT, SNAP_INPUT, MORPH_INPUT, GOTO_INPUT, WALK_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENUMS(MACRO_OUTPUT, MACROS), CHANGED_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(MAP_LIGHT, MACROS), REC_LIGHT, PLAY_LIGHT, LINK_LIGHT, LIGHTS_LEN };

	struct Mapping {
		float min = 0.f, max = 1.f;
		int curve = STEER_LINEAR;
		int steps = 4;
	};
	struct Scene {
		bool used = false;
		float macro[MACROS] = {};
		int launch[Vault::LANES]; // -1 = leave the lane alone, 0 = stop it, 1..64 = that clip
		Scene() {
			for (int& l : launch)
				l = -1;
		}
	};

	// ---- macros and mappings ----
	ParamHandle handles[MACROS][MAPS];
	Mapping maps[MACROS][MAPS];
	float macroNow[MACROS] = {};  // smoothed
	float macroSent[MACROS];
	std::atomic<int> learning{-1}; // macro waiting for a parameter to be clicked
	std::atomic<bool> linking{false};
	std::atomic<bool> resend{false};

	// ---- scenes ----
	Scene scenes[SCENES];
	bool sceneMorph = false;
	float sceneFrom[MACROS] = {}, sceneTo[MACROS] = {};
	double sceneProgress = 0.0;
	int sceneCv = -1;
	std::atomic<int> sceneLaunch{-1}; // scene whose VAULT launches the UI thread must send
	std::atomic<int> dispScene{0};

	// ---- gestures ----
	SteerGesture gesture[MACROS];
	bool armed[MACROS] = {};
	bool recording = false, playing = false;
	double recStart = 0.0, playStart = 0.0;

	// ---- time ----
	PhaseReader phase;
	bool phaseWas = false;
	double W = 0.0;
	int divider = 0;
	SchmittIn goIn, snapIn, gotoIn, recIn, playIn;
	dsp::BooleanTrigger goButton, snapButton, gotoButton, linkButton, mapButton[MACROS];
	TriggerOut changedOut;

	// ---- version tree (UI thread) ----
	SteerTree tree;
	bool treeDirty = false;
	int scope = 1; // 0 every module, 1 Codex modules, 2 mapped and linked modules
	int64_t vaultId = -1;
	std::vector<int64_t> keepIds;
	struct Pair {
		int64_t module;
		int param;
		float a, b;
		bool snap;
	};
	std::vector<Pair> pairs;
	int pairsFor[2] = {-2, -2};
	float applied = -1.f;
	Rng walkRng;
	int walkSeed = -1;
	uint32_t autoGenUi = 0;
	bool autoRunning = false;
	std::string message;
	double messageUntil = 0.0;

	// ---- between the threads ----
	std::atomic<bool> snapPending{false}, gotoPending{false};
	std::atomic<int> walkPending{0};
	std::atomic<uint32_t> autoGen{0}, autoGenSeen{0}, autoGenDone{0};
	std::atomic<float> autoProgress{0.f};
	std::atomic<float> dispMorph{0.f}, dispWalk{0.f};
	std::atomic<int> dispSeed{0};
	// audio thread's side of the timed morph
	uint32_t autoGenAudio = 0;
	bool autoActive = false;
	double autoPos = 0.0;
	double lastWalkBar = 0.0;

	Steer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int m = 0; m < MACROS; m++) {
			configParam(MACRO_PARAM + m, 0.f, 1.f, 0.f, string::f("Macro %d", m + 1), " %", 0.f, 100.f);
			configButton(MAP_PARAM + m, string::f("Map macro %d: press, then click any knob, switch or button in the rack", m + 1));
			configInput(MACRO_INPUT + m, string::f("Macro %d CV (0 to 10 V, added to the knob)", m + 1));
			configOutput(MACRO_OUTPUT + m, string::f("Macro %d (0 to 10 V)", m + 1));
			configLight(MAP_LIGHT + m, string::f("Macro %d: bright while waiting for a click, dim when it has mappings", m + 1));
			macroSent[m] = -1.f;
			for (int k = 0; k < MAPS; k++) {
				handles[m][k].color = nvgRGB(0x9b, 0x7b, 0xe8);
				handles[m][k].text = string::f("STEER macro %d", m + 1);
				APP->engine->addParamHandle(&handles[m][k]);
			}
		}
		configParam(SCENE_PARAM, 1.f, 16.f, 1.f, "Scene that GO recalls (and that the right-click menu stores into)");
		paramQuantities[SCENE_PARAM]->snapEnabled = true;
		configParam(TIME_PARAM, 0.f, 32.f, 2.f, "Time a scene change, GO TO or walk takes, in bars of PHASE (in seconds when PHASE is not patched)");
		configSwitch(CURVE_PARAM, 0.f, 3.f, 3.f, "Curve of a timed change", {"Linear", "Slow start", "Fast start", "S-shaped"});
		configButton(GO_PARAM, "Go: recall the scene");
		configButton(REC_PARAM, "Record the movements of armed macros while held (arm macros in the right-click menu)");
		configSwitch(PLAY_PARAM, 0.f, 1.f, 0.f, "Play recorded gestures in a loop", {"Off", "On"});
		configParam(LOOSE_PARAM, 0.f, 1.f, 0.f, "Loose: gestures wander in time, by up to 1/16 bar", " %", 0.f, 100.f);
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (drives WALK and LOOSE)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configButton(LINK_PARAM, "Link: press, then click any control on a VAULT or a generator (WARP, CONTOUR, MUTATE, CHAIN, FLOW)");
		configButton(SNAP_PARAM, "Snap: store the rack as it is now as a new version, under the current one");
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph: slide from the current version towards the target (click a version on the screen to make it the target)", " %", 0.f, 100.f);
		configButton(GOTO_PARAM, "Go to: move fully to the target over TIME; it becomes the current version");
		configParam(WALK_PARAM, 0.f, 1.f, 0.f, "Walk: chance, every BARS bars, of moving to a neighbouring version", " %", 0.f, 100.f);
		configParam(WALK_BARS_PARAM, 1.f, 32.f, 8.f, "Bars between walk decisions", " bars");
		paramQuantities[WALK_BARS_PARAM]->snapEnabled = true;
		configInput(PHASE_INPUT, "Phase (0 to 10 V ramp, one bar): times changes, gestures and the walk in bars");
		configInput(SCENE_INPUT, "Scene CV (0 to 10 V = scenes 1 to 16; a change recalls that scene)");
		configInput(GO_INPUT, "Go trigger");
		configInput(REC_INPUT, "Record gate");
		configInput(PLAY_INPUT, "Play gate");
		configInput(SEED_INPUT, "Seed CV (0 to 10 V = 0 to 999)");
		configInput(SNAP_INPUT, "Snap trigger");
		configInput(MORPH_INPUT, "Morph CV (10 V = full range)");
		configInput(GOTO_INPUT, "Go to trigger");
		configInput(WALK_INPUT, "Walk CV (10 V = full range)");
		configOutput(CHANGED_OUTPUT, "Scene changed (trigger)");
		configLight(REC_LIGHT, "Recording");
		configLight(PLAY_LIGHT, "Playing gestures");
		configLight(LINK_LIGHT, "Waiting for a click on a module to link");
	}

	~Steer() {
		for (int m = 0; m < MACROS; m++)
			for (int k = 0; k < MAPS; k++)
				APP->engine->removeParamHandle(&handles[m][k]);
	}

	void clearMaps_NoLock() {
		for (int m = 0; m < MACROS; m++)
			for (int k = 0; k < MAPS; k++) {
				APP->engine->updateParamHandle_NoLock(&handles[m][k], -1, 0, true);
				maps[m][k] = Mapping();
			}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		clearMaps_NoLock();
		for (int s = 0; s < SCENES; s++)
			scenes[s] = Scene();
		for (int m = 0; m < MACROS; m++) {
			gesture[m].clear();
			armed[m] = false;
			macroSent[m] = -1.f;
		}
		tree.clear();
		treeDirty = true;
		pairs.clear();
		pairsFor[0] = pairsFor[1] = -2;
		vaultId = -1;
		keepIds.clear();
		scope = 1;
		learning = -1;
		linking = false;
		sceneMorph = recording = playing = false;
	}

	int mapCount(int m) const {
		int n = 0;
		for (int k = 0; k < MAPS; k++)
			if (handles[m][k].moduleId >= 0)
				n++;
		return n;
	}

	// ------------------------------------------------------------ audio

	void process(const ProcessArgs& args) override {
		// buttons and triggers are watched every sample
		for (int m = 0; m < MACROS; m++)
			if (mapButton[m].process(params[MAP_PARAM + m].getValue() > 0.5f)) {
				learning = learning.load() == m ? -1 : m;
				linking = false;
			}
		if (linkButton.process(params[LINK_PARAM].getValue() > 0.5f)) {
			linking = !linking.load();
			learning = -1;
		}
		bool goP = goButton.process(params[GO_PARAM].getValue() > 0.5f);
		bool goT = goIn.process(0, inputs[GO_INPUT].getVoltage());
		bool go = goP || goT;
		bool snapP = snapButton.process(params[SNAP_PARAM].getValue() > 0.5f);
		bool snapT = snapIn.process(0, inputs[SNAP_INPUT].getVoltage());
		if (snapP || snapT)
			snapPending = true;
		bool gotoP = gotoButton.process(params[GOTO_PARAM].getValue() > 0.5f);
		bool gotoT = gotoIn.process(0, inputs[GOTO_INPUT].getVoltage());
		if (gotoP || gotoT)
			gotoPending = true;
		recIn.process(0, inputs[REC_INPUT].getVoltage());
		playIn.process(0, inputs[PLAY_INPUT].getVoltage());

		// bar position: from PHASE, or two seconds to the bar without it
		bool phaseOn = inputs[PHASE_INPUT].isConnected();
		if (phaseOn != phaseWas) {
			phaseWas = phaseOn;
			phase.reset();
			if (phaseOn)
				phase.W = phase.Wprev = std::floor(W);
		}
		double dW;
		if (phaseOn) {
			double before = phase.W;
			phase.process(inputs[PHASE_INPUT].getVoltage());
			dW = phase.W - before;
			W = phase.W;
		}
		else {
			dW = 0.5 * (double)args.sampleTime;
			W += dW;
		}
		// how far a timed change moves this sample
		float time = params[TIME_PARAM].getValue();
		double advance = time <= 0.f ? 1.0 : (phaseOn ? std::fabs(dW) : (double)args.sampleTime) / (double)time;

		// ---- scenes ----
		int sceneIndex = clamp((int)std::lround(params[SCENE_PARAM].getValue()) - 1, 0, SCENES - 1);
		if (inputs[SCENE_INPUT].isConnected()) {
			sceneIndex = clamp((int)(inputs[SCENE_INPUT].getVoltage() * 1.6f), 0, SCENES - 1);
			if (sceneIndex != sceneCv) {
				if (sceneCv >= 0)
					go = true;
				sceneCv = sceneIndex;
			}
		}
		else
			sceneCv = -1;
		if (go && scenes[sceneIndex].used) {
			for (int m = 0; m < MACROS; m++) {
				sceneFrom[m] = params[MACRO_PARAM + m].getValue();
				sceneTo[m] = scenes[sceneIndex].macro[m];
			}
			sceneMorph = true;
			sceneProgress = 0.0;
			sceneLaunch = sceneIndex;
			changedOut.fire(0);
		}
		if (sceneMorph) {
			sceneProgress += advance;
			if (sceneProgress >= 1.0) {
				sceneProgress = 1.0;
				sceneMorph = false;
			}
		}
		outputs[CHANGED_OUTPUT].setVoltage(changedOut.process(0, args.sampleTime));

		// ---- timed move through the version tree: the UI thread starts it, this counts it ----
		uint32_t gen = autoGen.load(std::memory_order_relaxed);
		if (gen != autoGenAudio) {
			autoGenAudio = gen;
			autoActive = true;
			autoPos = 0.0;
		}
		if (autoActive) {
			autoPos += advance;
			if (autoPos >= 1.0) {
				autoPos = 1.0;
				autoActive = false;
			}
		}

		// ---- walk: a decision every BARS bars ----
		double walkBars = (double)std::lround(params[WALK_BARS_PARAM].getValue());
		if (W < lastWalkBar)
			lastWalkBar = W;
		if (W - lastWalkBar >= walkBars) {
			lastWalkBar += walkBars * std::floor((W - lastWalkBar) / walkBars);
			walkPending.fetch_add(1);
		}

		// ---- gestures ----
		bool recGate = params[REC_PARAM].getValue() > 0.5f || recIn.isHigh(0);
		bool playGate = params[PLAY_PARAM].getValue() > 0.5f || playIn.isHigh(0);
		if (recGate && !recording) {
			recording = true;
			recStart = W;
			for (int m = 0; m < MACROS; m++)
				if (armed[m])
					gesture[m].clear();
		}
		else if (recording && (!recGate || W - recStart >= 16.0)) {
			recording = false;
			for (int m = 0; m < MACROS; m++)
				if (armed[m])
					gesture[m].finish(W - recStart);
			playStart = recStart; // carry straight on in step if PLAY is on
		}
		if (playGate && !playing)
			playStart = recording ? recStart : W;
		playing = playGate;

		bool tick = divider == 0;
		divider = (divider + 1) & 31;
		if (!tick)
			return;

		// ---- every 32 samples: macro values, and their mapped parameters ----
		int seed = SeedLatch::read(params[SEED_PARAM].getValue(), inputs[SEED_INPUT].getVoltage(), inputs[SEED_INPUT].isConnected());
		float loose = params[LOOSE_PARAM].getValue();
		int curve = clamp((int)std::lround(params[CURVE_PARAM].getValue()), 0, 3);
		float sceneBlend = steerCurve(curve, (float)sceneProgress);
		bool force = resend.exchange(false);
		float slew = 1.f - std::exp(-32.f * args.sampleTime / 0.005f);
		for (int m = 0; m < MACROS; m++) {
			Param& knob = params[MACRO_PARAM + m];
			if (sceneMorph || sceneProgress == 1.0)
				knob.setValue(sceneFrom[m] + (sceneTo[m] - sceneFrom[m]) * sceneBlend);
			if (recording && armed[m])
				gesture[m].add(W - recStart, knob.getValue());
			else if (playing && !gesture[m].empty())
				knob.setValue(gesture[m].valueAt(SteerGesture::loosen(W - playStart, loose, seed + m)));
			float want = clamp(knob.getValue() + inputs[MACRO_INPUT + m].getVoltage() * 0.1f, 0.f, 1.f);
			macroNow[m] += (want - macroNow[m]) * slew;
			if (std::fabs(want - macroNow[m]) < 1e-5f)
				macroNow[m] = want;
			outputs[MACRO_OUTPUT + m].setVoltage(macroNow[m] * 10.f);
			if (force || std::fabs(macroNow[m] - macroSent[m]) > 1e-6f) {
				macroSent[m] = macroNow[m];
				for (int k = 0; k < MAPS; k++) {
					Module* mod = handles[m][k].module;
					if (!mod)
						continue;
					int pid = handles[m][k].paramId;
					if (pid < 0 || pid >= (int)mod->paramQuantities.size())
						continue;
					ParamQuantity* pq = mod->paramQuantities[(size_t)pid];
					if (!pq || !pq->isBounded())
						continue;
					const Mapping& mp = maps[m][k];
					pq->setScaledValue(mp.min + (mp.max - mp.min) * steerCurve(mp.curve, macroNow[m], mp.steps));
				}
			}
			int learn = learning.load(std::memory_order_relaxed);
			lights[MAP_LIGHT + m].setBrightness(learn == m ? 1.f : (handles[m][0].moduleId >= 0 ? 0.25f : 0.f));
		}
		if (sceneProgress == 1.0 && !sceneMorph)
			sceneProgress = 0.0;
		lights[REC_LIGHT].setBrightness(recording ? 1.f : 0.f);
		lights[PLAY_LIGHT].setBrightness(playing ? 1.f : 0.f);
		lights[LINK_LIGHT].setBrightness(linking.load(std::memory_order_relaxed) ? 1.f : 0.f);

		int curveAuto = curve;
		autoProgress.store(steerCurve(curveAuto, (float)autoPos), std::memory_order_relaxed);
		autoGenSeen.store(autoGenAudio, std::memory_order_relaxed);
		if (!autoActive && autoPos >= 1.0)
			autoGenDone.store(autoGenAudio, std::memory_order_relaxed);
		dispMorph.store(clamp(params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 0.1f, 0.f, 1.f), std::memory_order_relaxed);
		dispWalk.store(clamp(params[WALK_PARAM].getValue() + inputs[WALK_INPUT].getVoltage() * 0.1f, 0.f, 1.f), std::memory_order_relaxed);
		dispSeed.store(seed, std::memory_order_relaxed);
		dispScene.store(sceneIndex, std::memory_order_relaxed);
	}

	// ------------------------------------------------------------ UI thread

	void say(const std::string& s) {
		message = s;
		messageUntil = system::getTime() + 3.0;
	}

	// A parameter was clicked while a MAP button was waiting.
	void learnParam(int macro, int64_t moduleId, int paramId) {
		for (int k = 0; k < MAPS; k++)
			if (handles[macro][k].moduleId < 0) {
				maps[macro][k] = Mapping();
				APP->engine->updateParamHandle(&handles[macro][k], moduleId, paramId, true);
				macroSent[macro] = -1.f;
				say(string::f("MACRO %d MAPPED (%d OF 16)", macro + 1, k + 1));
				return;
			}
		say(string::f("MACRO %d HAS 16 MAPPINGS", macro + 1));
	}

	bool isLinked(int64_t id) const { return std::find(keepIds.begin(), keepIds.end(), id) != keepIds.end(); }

	void toggleLink(Module* m) {
		if (!m || m == this)
			return;
		Vault* v = dynamic_cast<Vault*>(m);
		Keepable* k = dynamic_cast<Keepable*>(m);
		if (!v && !k) {
			say("THAT MODULE CANNOT BE LINKED");
			return;
		}
		if (isLinked(m->id) || (v && vaultId == m->id)) {
			keepIds.erase(std::remove(keepIds.begin(), keepIds.end(), m->id), keepIds.end());
			if (vaultId == m->id)
				vaultId = -1;
			say("LINK REMOVED");
			return;
		}
		if (v)
			vaultId = m->id;
		if (k)
			keepIds.push_back(m->id);
		say(k ? "LINKED: " + k->keepLabel() : "LINKED");
	}

	bool inScope(Module* m) {
		if (m == this)
			return false; // STEER's own macros are added separately
		if (scope == 0)
			return true;
		if (scope == 1)
			return m->model && m->model->plugin == pluginInstance;
		if (isLinked(m->id) || m->id == vaultId)
			return true;
		for (int a = 0; a < MACROS; a++)
			for (int k = 0; k < MAPS; k++)
				if (handles[a][k].moduleId == m->id)
					return true;
		return false;
	}

	// SNAP: a new version under the current one.
	void snap() {
		int id = tree.add(tree.current);
		SteerNode* n = tree.find(id);
		for (int m = 0; m < MACROS; m++)
			n->params.push_back({this->id, MACRO_PARAM + m, params[MACRO_PARAM + m].getValue(), false});
		for (int64_t mid : APP->engine->getModuleIds()) {
			Module* mod = APP->engine->getModule(mid);
			if (!mod || !inScope(mod))
				continue;
			for (size_t p = 0; p < mod->paramQuantities.size(); p++) {
				ParamQuantity* pq = mod->paramQuantities[p];
				if (!pq || !pq->isBounded())
					continue;
				bool stepped = pq->snapEnabled || dynamic_cast<SwitchQuantity*>(pq) != nullptr;
				n->params.push_back({mid, (int)p, pq->getValue(), stepped});
			}
		}
		for (int64_t mid : keepIds) {
			Keepable* k = dynamic_cast<Keepable*>(APP->engine->getModule(mid));
			if (!k)
				continue;
			json_t* st = k->captureState();
			char* text = st ? json_dumps(st, JSON_COMPACT | JSON_REAL_PRECISION(9)) : nullptr;
			if (text) {
				n->states.push_back({mid, text});
				std::free(text);
			}
			if (st)
				json_decref(st);
		}
		say(string::f("SNAP: %s, %d SETTINGS, %d STATES", n->name.c_str(), (int)n->params.size(), (int)n->states.size()));
		tree.current = id;
		tree.target = -1;
		treeDirty = true;
	}

	void restoreStates(int nodeId) {
		const SteerNode* n = tree.find(nodeId);
		if (!n)
			return;
		for (const SteerState& s : n->states) {
			Keepable* k = dynamic_cast<Keepable*>(APP->engine->getModule(s.module));
			if (!k || s.json.empty())
				continue;
			json_error_t err;
			json_t* j = json_loads(s.json.c_str(), 0, &err);
			if (j)
				k->requestRestore(j);
		}
	}

	// The settings that differ between the current version and the target.
	void buildPairs() {
		pairs.clear();
		pairsFor[0] = tree.current;
		pairsFor[1] = tree.target;
		applied = -1.f;
		const SteerNode* a = tree.find(tree.current);
		const SteerNode* b = tree.find(tree.target);
		if (!a || !b)
			return;
		std::map<std::pair<int64_t, int>, float> from;
		for (const SteerParamValue& v : a->params)
			from[{v.module, v.param}] = v.value;
		for (const SteerParamValue& v : b->params) {
			auto it = from.find({v.module, v.param});
			if (it == from.end())
				continue;
			// a parameter that a macro drives is left to the macro
			if (v.module != this->id && APP->engine->getParamHandle(v.module, v.param))
				continue;
			pairs.push_back({v.module, v.param, it->second, v.value, v.snap});
		}
	}

	void applyBlend(float m) {
		Module* mod = nullptr;
		int64_t modId = -1;
		for (const Pair& p : pairs) {
			if (p.module != modId) {
				modId = p.module;
				mod = APP->engine->getModule(modId);
			}
			if (!mod || p.param < 0 || p.param >= (int)mod->params.size())
				continue;
			float v = p.snap ? (m < 0.5f ? p.a : p.b) : p.a + (p.b - p.a) * m;
			APP->engine->setParamValue(mod, p.param, v);
		}
		// generators' states change over half way
		if (applied >= 0.f) {
			if (applied < 0.5f && m >= 0.5f)
				restoreStates(tree.target);
			else if (applied >= 0.5f && m < 0.5f)
				restoreStates(tree.current);
		}
		else if (m >= 0.5f)
			restoreStates(tree.target);
		applied = m;
	}

	void startAuto() {
		autoGenUi = autoGen.fetch_add(1) + 1;
		autoRunning = true;
	}

	void setTarget(int id) {
		if (autoRunning)
			return;
		tree.target = (id == tree.current) ? -1 : id;
		treeDirty = true;
	}

	// Jump to a version at once.
	void jumpTo(int id) {
		if (!tree.find(id) || id == tree.current)
			return;
		autoRunning = false;
		tree.target = id;
		buildPairs();
		applyBlend(1.f);
		tree.current = id;
		tree.target = -1;
		treeDirty = true;
	}

	void uiStep() {
		// ---- learning a mapping, or a link: wait for a click on some control ----
		int learn = learning.load();
		bool link = linking.load();
		static const int NONE = -1;
		if (learn != lastLearn || link != lastLink) {
			lastLearn = learn;
			lastLink = link;
			if (learn != NONE || link)
				APP->scene->rack->setTouchedParam(NULL);
		}
		if (learn != NONE || link) {
			ParamWidget* touched = APP->scene->rack->getTouchedParam();
			if (touched && touched->module && touched->module != this) {
				APP->scene->rack->setTouchedParam(NULL);
				if (link) {
					toggleLink(touched->module);
					linking = false;
				}
				else {
					learnParam(learn, touched->module->id, touched->paramId);
					learning = -1;
				}
			}
			else if (touched)
				APP->scene->rack->setTouchedParam(NULL);
		}

		// ---- scene launches for the linked VAULT ----
		int sl = sceneLaunch.exchange(-1);
		if (sl >= 0 && sl < SCENES) {
			Vault* v = dynamic_cast<Vault*>(APP->engine->getModule(vaultId));
			if (v)
				for (int l = 0; l < Vault::LANES; l++)
					if (scenes[sl].launch[l] >= 0)
						v->requestLaunch(l, scenes[sl].launch[l], true);
		}

		// ---- version tree ----
		if (snapPending.exchange(false) && !autoRunning)
			snap();
		if (gotoPending.exchange(false) && !autoRunning && tree.find(tree.target) && tree.find(tree.current))
			startAuto();
		int walks = walkPending.exchange(0);
		int seed = dispSeed.load();
		if (seed != walkSeed) {
			walkSeed = seed;
			walkRng = Rng((uint64_t)seed).fork("walk");
		}
		float walk = dispWalk.load();
		if (walks > 0 && walk > 0.f && !autoRunning && tree.find(tree.current)) {
			int next = steerWalk(tree, tree.current, walk, walkRng);
			if (next != tree.current) {
				tree.target = next;
				startAuto();
			}
		}
		if (pairsFor[0] != tree.current || pairsFor[1] != tree.target)
			buildPairs();
		if (!pairs.empty() || tree.find(tree.target)) {
			float m = dispMorph.load();
			bool done = false;
			if (autoRunning) {
				// the audio thread counts the bars; only believe its number once it has seen this move
				float prog = autoProgress.load();
				if (autoGenDone.load() == autoGenUi) {
					prog = 1.f;
					done = true;
				}
				else if (autoGenSeen.load() != autoGenUi)
					prog = 0.f;
				m = std::max(m, prog);
			}
			if (tree.find(tree.target) && tree.find(tree.current) && std::fabs(m - applied) > 1e-4f)
				applyBlend(m);
			if (done) {
				autoRunning = false;
				tree.current = tree.target;
				tree.target = -1;
				treeDirty = true;
			}
		}
		else
			autoRunning = false;
	}
	int lastLearn = -1;
	bool lastLink = false;

	// ------------------------------------------------------------ saving

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* mapsJ = json_array();
		for (int m = 0; m < MACROS; m++)
			for (int k = 0; k < MAPS; k++) {
				if (handles[m][k].moduleId < 0)
					continue;
				json_t* j = json_object();
				json_object_set_new(j, "macro", json_integer(m));
				json_object_set_new(j, "module", json_integer(handles[m][k].moduleId));
				json_object_set_new(j, "param", json_integer(handles[m][k].paramId));
				json_object_set_new(j, "min", json_real(maps[m][k].min));
				json_object_set_new(j, "max", json_real(maps[m][k].max));
				json_object_set_new(j, "curve", json_integer(maps[m][k].curve));
				json_object_set_new(j, "steps", json_integer(maps[m][k].steps));
				json_array_append_new(mapsJ, j);
			}
		json_object_set_new(root, "maps", mapsJ);
		json_t* scenesJ = json_array();
		for (int s = 0; s < SCENES; s++) {
			if (!scenes[s].used) {
				json_array_append_new(scenesJ, json_null());
				continue;
			}
			json_t* j = json_object();
			json_t* mac = json_array();
			for (int m = 0; m < MACROS; m++)
				json_array_append_new(mac, json_real(scenes[s].macro[m]));
			json_object_set_new(j, "macros", mac);
			json_t* la = json_array();
			for (int l = 0; l < Vault::LANES; l++)
				json_array_append_new(la, json_integer(scenes[s].launch[l]));
			json_object_set_new(j, "launch", la);
			json_array_append_new(scenesJ, j);
		}
		json_object_set_new(root, "scenes", scenesJ);
		json_t* gest = json_array();
		for (int m = 0; m < MACROS; m++) {
			json_t* j = json_object();
			json_object_set_new(j, "armed", json_boolean(armed[m]));
			const SteerGesture& g = gesture[m];
			if (!g.empty()) {
				json_object_set_new(j, "bars", json_real(g.bars));
				json_t* pts = json_array();
				for (int i = 0; i < g.count; i++) {
					json_array_append_new(pts, json_real(g.pos[(size_t)i]));
					json_array_append_new(pts, json_real(g.val[(size_t)i]));
				}
				json_object_set_new(j, "points", pts);
			}
			json_array_append_new(gest, j);
		}
		json_object_set_new(root, "gestures", gest);
		json_object_set_new(root, "vault", json_integer(vaultId));
		json_t* links = json_array();
		for (int64_t idv : keepIds)
			json_array_append_new(links, json_integer(idv));
		json_object_set_new(root, "links", links);
		json_object_set_new(root, "scope", json_integer(scope));
		return root;
	}

	void dataFromJson(json_t* root) override {
		clearMaps_NoLock();
		size_t i;
		json_t* j;
		int used[MACROS] = {};
		json_array_foreach(json_object_get(root, "maps"), i, j) {
			int m = (int)json_integer_value(json_object_get(j, "macro"));
			if (m < 0 || m >= MACROS || used[m] >= MAPS)
				continue;
			int k = used[m]++;
			maps[m][k].min = clamp((float)json_number_value(json_object_get(j, "min")), 0.f, 1.f);
			maps[m][k].max = clamp((float)json_number_value(json_object_get(j, "max")), 0.f, 1.f);
			maps[m][k].curve = clamp((int)json_integer_value(json_object_get(j, "curve")), 0, STEER_CURVES - 1);
			maps[m][k].steps = clamp((int)json_integer_value(json_object_get(j, "steps")), 2, 32);
			APP->engine->updateParamHandle_NoLock(&handles[m][k], json_integer_value(json_object_get(j, "module")), (int)json_integer_value(json_object_get(j, "param")), false);
		}
		json_array_foreach(json_object_get(root, "scenes"), i, j) {
			if (i >= SCENES)
				break;
			scenes[i] = Scene();
			if (!json_is_object(j))
				continue;
			scenes[i].used = true;
			for (int m = 0; m < MACROS; m++)
				scenes[i].macro[m] = clamp((float)json_number_value(json_array_get(json_object_get(j, "macros"), (size_t)m)), 0.f, 1.f);
			json_t* la = json_object_get(j, "launch");
			for (int l = 0; l < Vault::LANES; l++) {
				json_t* v = json_array_get(la, (size_t)l);
				scenes[i].launch[l] = v ? clamp((int)json_integer_value(v), -1, Vault::SLOTS) : -1;
			}
		}
		json_array_foreach(json_object_get(root, "gestures"), i, j) {
			if (i >= MACROS)
				break;
			armed[i] = json_is_true(json_object_get(j, "armed"));
			SteerGesture& g = gesture[i];
			g.clear();
			json_t* pts = json_object_get(j, "points");
			size_t n = pts ? json_array_size(pts) / 2 : 0;
			for (size_t k = 0; k < n && k < (size_t)SteerGesture::MAX; k++) {
				g.pos[k] = json_number_value(json_array_get(pts, 2 * k));
				g.val[k] = (float)json_number_value(json_array_get(pts, 2 * k + 1));
				g.count = (int)k + 1;
			}
			if (g.count)
				g.bars = clamp((float)json_number_value(json_object_get(j, "bars")), 1.f, 16.f);
		}
		json_t* v;
		if ((v = json_object_get(root, "vault")))
			vaultId = json_integer_value(v);
		keepIds.clear();
		json_array_foreach(json_object_get(root, "links"), i, j)
			keepIds.push_back(json_integer_value(j));
		if ((v = json_object_get(root, "scope")))
			scope = clamp((int)json_integer_value(v), 0, 2);
		for (int m = 0; m < MACROS; m++)
			macroSent[m] = -1.f;
	}

	void onSave(const SaveEvent& e) override {
		std::string dir = createPatchStorageDirectory();
		if (dir.empty())
			return;
		std::string path = system::join(dir, "tree.json");
		if (!treeDirty && system::exists(path))
			return;
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f)
			return;
		f << tree.toJson();
		treeDirty = !f.good();
	}

	void onAdd(const AddEvent& e) override {
		std::string dir = getPatchStorageDirectory();
		if (dir.empty())
			return;
		std::ifstream f(system::join(dir, "tree.json"), std::ios::binary);
		if (!f)
			return;
		std::stringstream ss;
		ss << f.rdbuf();
		if (tree.fromJson(ss.str())) {
			tree.target = -1;
			treeDirty = false;
			pairsFor[0] = pairsFor[1] = -2;
		}
	}
};

// ---------------------------------------------------------------- screen

// The version tree: each dot is a snapshot, joined to the one it was made from.
struct SteerDisplay : OpaqueWidget {
	Steer* module = nullptr;

	struct Dot {
		int id;
		Vec pos;
	};
	std::vector<Dot> dots() const {
		std::vector<Dot> out;
		if (!module)
			return out;
		std::vector<SteerPlace> places = module->tree.layout();
		int maxDepth = 1;
		float maxRow = 1.f;
		for (const SteerPlace& p : places) {
			maxDepth = std::max(maxDepth, p.depth);
			maxRow = std::max(maxRow, p.row);
		}
		float colW = std::min(34.f, (box.size.x - 40.f) / (float)maxDepth);
		float rowH = std::min(16.f, (box.size.y - 34.f) / maxRow);
		for (const SteerPlace& p : places)
			out.push_back({p.id, Vec(12.f + colW * (float)p.depth, 24.f + rowH * p.row)});
		return out;
	}
	int nodeAt(Vec pos) const {
		int best = -1;
		float bestD = 10.f;
		for (const Dot& d : dots()) {
			float dist = d.pos.minus(pos).norm();
			if (dist < bestD) {
				bestD = dist;
				best = d.id;
			}
		}
		return best;
	}

	void nodeMenu(int id);

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS) {
			int id = nodeAt(e.pos);
			if (id >= 0) {
				if (e.button == GLFW_MOUSE_BUTTON_LEFT)
					module->setTarget(id);
				else if (e.button == GLFW_MOUSE_BUTTON_RIGHT)
					nodeMenu(id);
				e.consume(this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

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
			NVGcolor violet = theme::system();
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font)
				nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, 9.f);

			std::string head = "VERSIONS: PRESS SNAP TO STORE ONE";
			if (module) {
				int learn = module->learning.load();
				if (learn >= 0)
					head = string::f("CLICK A CONTROL TO MAP MACRO %d", learn + 1);
				else if (module->linking.load())
					head = "CLICK A CONTROL ON THE MODULE TO LINK";
				else if (system::getTime() < module->messageUntil)
					head = module->message;
				else if (!module->tree.nodes.empty()) {
					const SteerNode* c = module->tree.find(module->tree.current);
					const SteerNode* t = module->tree.find(module->tree.target);
					head = string::f("AT %s", c ? c->name.c_str() : "-");
					if (t)
						head += string::f("  >  %s  %.0f %%", t->name.c_str(), std::max(module->applied, 0.f) * 100.f);
					head += string::f("   SCENE %d%s", module->dispScene.load() + 1, module->scenes[module->dispScene.load()].used ? "" : " (empty)");
				}
			}
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, theme::dim());
			nvgText(vg, 4.f, 3.f, head.c_str(), NULL);

			std::vector<Dot> ds = dots();
			if (!module) {
				// in the module browser: a small made-up tree
				ds = {{1, Vec(14, 40)}, {2, Vec(48, 28)}, {3, Vec(48, 52)}, {4, Vec(82, 28)}};
			}
			auto posOf = [&](int id, Vec& out) {
				for (const Dot& d : ds)
					if (d.id == id) {
						out = d.pos;
						return true;
					}
				return false;
			};
			// edges
			nvgStrokeColor(vg, theme::dim());
			nvgStrokeWidth(vg, 1.f);
			for (const Dot& d : ds) {
				int parent = -1;
				if (module) {
					const SteerNode* n = module->tree.find(d.id);
					parent = n ? n->parent : -1;
				}
				else
					parent = d.id == 1 ? -1 : (d.id == 4 ? 2 : 1);
				Vec pp;
				if (parent >= 0 && posOf(parent, pp)) {
					nvgBeginPath(vg);
					nvgMoveTo(vg, pp.x, pp.y);
					nvgLineTo(vg, d.pos.x, d.pos.y);
					nvgStroke(vg);
				}
			}
			int current = module ? module->tree.current : 2;
			int target = module ? module->tree.target : 4;
			// the path being travelled
			Vec a, b;
			if (posOf(current, a) && posOf(target, b)) {
				float m = module ? std::max(module->applied, 0.f) : 0.4f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, a.x, a.y);
				nvgLineTo(vg, a.x + (b.x - a.x) * m, a.y + (b.y - a.y) * m);
				nvgStrokeColor(vg, violet);
				nvgStrokeWidth(vg, 2.5f);
				nvgStroke(vg);
			}
			for (const Dot& d : ds) {
				nvgBeginPath(vg);
				nvgCircle(vg, d.pos.x, d.pos.y, 4.5f);
				nvgFillColor(vg, d.id == current ? violet : theme::grid());
				nvgFill(vg);
				nvgStrokeColor(vg, d.id == target ? theme::text() : theme::dim());
				nvgStrokeWidth(vg, d.id == target ? 1.6f : 1.f);
				nvgStroke(vg);
				std::string name = string::f("v%d", d.id);
				if (module) {
					const SteerNode* n = module->tree.find(d.id);
					if (n)
						name = n->name;
				}
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, d.id == current ? theme::text() : theme::dim());
				nvgText(vg, d.pos.x + 7.f, d.pos.y, name.c_str(), NULL);
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}
};

struct NodeNameField : ui::TextField {
	Steer* module = nullptr;
	int id = 0;
	NodeNameField() { box.size.x = 160.f; }
	void onChange(const ChangeEvent& e) override {
		SteerNode* n = module ? module->tree.find(id) : nullptr;
		if (n) {
			n->name = text;
			module->treeDirty = true;
		}
	}
};

void SteerDisplay::nodeMenu(int id) {
	Steer* m = module;
	const SteerNode* n = m->tree.find(id);
	if (!n)
		return;
	Menu* menu = createMenu();
	menu->addChild(createMenuLabel(string::f("Version: %d settings, %d generator states", (int)n->params.size(), (int)n->states.size())));
	NodeNameField* name = new NodeNameField;
	name->module = m;
	name->id = id;
	name->setText(n->name);
	menu->addChild(name);
	menu->addChild(createMenuItem("Make this the target", "click", [=]() { m->setTarget(id); }));
	menu->addChild(createMenuItem("Jump here now", "", [=]() { m->jumpTo(id); }));
	menu->addChild(createMenuItem("Delete (its children move up)", "", [=]() {
		if (m->autoRunning)
			return;
		m->tree.remove(id);
		m->treeDirty = true;
	}));
}

// A slider in a menu that sets a float between 0 and 1, shown in per cent.
struct PercentQuantity : Quantity {
	float* value = nullptr;
	std::string label;
	std::function<void()> changed;
	void setValue(float v) override {
		*value = clamp(v, 0.f, 1.f);
		if (changed)
			changed();
	}
	float getValue() override { return *value; }
	float getDefaultValue() override { return 0.f; }
	float getDisplayValue() override { return *value * 100.f; }
	void setDisplayValue(float v) override { setValue(v / 100.f); }
	std::string getLabel() override { return label; }
	std::string getUnit() override { return " %"; }
	int getDisplayPrecision() override { return 3; }
};

struct PercentSlider : ui::Slider {
	PercentSlider(float* value, const std::string& label, std::function<void()> changed) {
		PercentQuantity* q = new PercentQuantity;
		q->value = value;
		q->label = label;
		q->changed = changed;
		quantity = q;
		box.size.x = 200.f;
	}
	~PercentSlider() { delete quantity; }
};

struct SteerWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	SteerWidget(Steer* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Steer.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		SteerDisplay* display = new SteerDisplay;
		display->module = module;
		display->box.pos = mm2px(Vec(L::DISPLAY[0][0], L::DISPLAY[0][1]));
		display->box.size = mm2px(Vec(L::DISPLAY[1][0], L::DISPLAY[1][1]));
		addChild(display);

		for (int m = 0; m < Steer::MACROS; m++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(at(L::MACRO[m]), module, Steer::MACRO_PARAM + m));
			addParam(createParamCentered<VCVButton>(at(L::MAP[m]), module, Steer::MAP_PARAM + m));
			addChild(createLightCentered<SmallLight<YellowLight>>(at(L::MAP_LIGHT[m]), module, Steer::MAP_LIGHT + m));
			addInput(createInputCentered<PJ301MPort>(at(L::MACRO_IN[m]), module, Steer::MACRO_INPUT + m));
			addOutput(createOutputCentered<PJ301MPort>(at(L::MACRO_OUT[m]), module, Steer::MACRO_OUTPUT + m));
		}
		addParam(createParamCentered<RoundBlackKnob>(at(L::SCENE), module, Steer::SCENE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::TIME), module, Steer::TIME_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(at(L::CURVE), module, Steer::CURVE_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::GO), module, Steer::GO_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::REC), module, Steer::REC_PARAM));
		addParam(createParamCentered<VCVLatch>(at(L::PLAY), module, Steer::PLAY_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(at(L::REC_LIGHT), module, Steer::REC_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(at(L::PLAY_LIGHT), module, Steer::PLAY_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(at(L::LOOSE), module, Steer::LOOSE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Steer::SEED_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::LINK), module, Steer::LINK_PARAM));
		addChild(createLightCentered<SmallLight<YellowLight>>(at(L::LINK_LIGHT), module, Steer::LINK_LIGHT));
		addParam(createParamCentered<VCVButton>(at(L::SNAP), module, Steer::SNAP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::MORPH), module, Steer::MORPH_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::GOTO), module, Steer::GOTO_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::WALK), module, Steer::WALK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::WALK_BARS), module, Steer::WALK_BARS_PARAM));

		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Steer::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SCENE_IN), module, Steer::SCENE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::GO_IN), module, Steer::GO_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::REC_IN), module, Steer::REC_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PLAY_IN), module, Steer::PLAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SEED_IN), module, Steer::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SNAP_IN), module, Steer::SNAP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MORPH_IN), module, Steer::MORPH_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::GOTO_IN), module, Steer::GOTO_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::WALK_IN), module, Steer::WALK_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::CHANGED_OUT), module, Steer::CHANGED_OUTPUT));
	}

	void step() override {
		Steer* m = getModule<Steer>();
		if (m)
			m->uiStep();
		ModuleWidget::step();
	}

	static std::string paramName(const ParamHandle& h) {
		Module* mod = h.module;
		if (!mod || h.paramId < 0 || h.paramId >= (int)mod->paramQuantities.size() || !mod->paramQuantities[(size_t)h.paramId])
			return "(module removed)";
		std::string model = mod->model ? mod->model->name : "?";
		return model + ": " + mod->paramQuantities[(size_t)h.paramId]->getLabel();
	}

	void appendContextMenu(Menu* menu) override {
		Steer* m = getModule<Steer>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Macros"));
		for (int a = 0; a < Steer::MACROS; a++) {
			menu->addChild(createSubmenuItem(string::f("Macro %d", a + 1), string::f("%d mapped%s", m->mapCount(a), m->armed[a] ? ", armed" : ""), [=](Menu* sub) {
				sub->addChild(createBoolPtrMenuItem("Armed for gesture recording", "", &m->armed[a]));
				sub->addChild(createMenuItem("Clear its recorded gesture", m->gesture[a].empty() ? "none" : string::f("%.0f bars", m->gesture[a].bars), [=]() { m->gesture[a].clear(); }, m->gesture[a].empty()));
				sub->addChild(new MenuSeparator);
				if (!m->mapCount(a))
					sub->addChild(createMenuLabel("No mappings yet: press its MAP button, then click a control"));
				for (int k = 0; k < Steer::MAPS; k++) {
					if (m->handles[a][k].moduleId < 0)
						continue;
					sub->addChild(createSubmenuItem(paramName(m->handles[a][k]), "", [=](Menu* mm) {
						Steer::Mapping* mp = &m->maps[a][k];
						auto again = [=]() { m->resend = true; };
						mm->addChild(new PercentSlider(&mp->min, "At macro 0 %", again));
						mm->addChild(new PercentSlider(&mp->max, "At macro 100 %", again));
						mm->addChild(createMenuLabel("Response"));
						for (int c = 0; c < STEER_CURVES; c++)
							mm->addChild(createCheckMenuItem(steerCurveName(c), "", [=]() { return mp->curve == c; }, [=]() {
								mp->curve = c;
								m->resend = true;
							}));
						mm->addChild(createSubmenuItem("Number of steps", string::f("%d", mp->steps), [=](Menu* st) {
							for (int n : {2, 3, 4, 5, 6, 8, 12, 16, 24, 32})
								st->addChild(createCheckMenuItem(string::f("%d", n), "", [=]() { return mp->steps == n; }, [=]() {
									mp->steps = n;
									m->resend = true;
								}));
						}));
						mm->addChild(createMenuItem("Remove this mapping", "", [=]() { APP->engine->updateParamHandle(&m->handles[a][k], -1, 0, true); }));
					}));
				}
			}));
		}

		menu->addChild(new MenuSeparator);
		int s = clamp((int)std::lround(m->params[Steer::SCENE_PARAM].getValue()) - 1, 0, Steer::SCENES - 1);
		menu->addChild(createMenuLabel(string::f("Scene %d (chosen with the SCENE knob)%s", s + 1, m->scenes[s].used ? "" : ": empty")));
		menu->addChild(createMenuItem(string::f("Store the 8 macros as they are now in scene %d", s + 1), "", [=]() {
			for (int a = 0; a < Steer::MACROS; a++)
				m->scenes[s].macro[a] = m->params[Steer::MACRO_PARAM + a].getValue();
			m->scenes[s].used = true;
		}));
		menu->addChild(createSubmenuItem(string::f("Scene %d also launches VAULT clips", s + 1), "", [=](Menu* sub) {
			for (int l = 0; l < Vault::LANES; l++) {
				int now = m->scenes[s].launch[l];
				std::string what = now < 0 ? "leave alone" : (now == 0 ? "stop" : string::f("clip %d", now));
				sub->addChild(createSubmenuItem(string::f("Lane %d", l + 1), what, [=](Menu* ln) {
					ln->addChild(createCheckMenuItem("Leave alone", "", [=]() { return m->scenes[s].launch[l] < 0; }, [=]() { m->scenes[s].launch[l] = -1; }));
					ln->addChild(createCheckMenuItem("Stop", "", [=]() { return m->scenes[s].launch[l] == 0; }, [=]() { m->scenes[s].launch[l] = 0; }));
					for (int c = 1; c <= Vault::SLOTS; c++)
						ln->addChild(createCheckMenuItem(string::f("Clip %d", c), "", [=]() { return m->scenes[s].launch[l] == c; }, [=]() { m->scenes[s].launch[l] = c; }));
				}));
			}
		}));
		menu->addChild(createMenuItem(string::f("Empty scene %d", s + 1), "", [=]() { m->scenes[s] = Steer::Scene(); }, !m->scenes[s].used));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Links"));
		menu->addChild(createSubmenuItem("Linked modules", string::f("%d", (int)m->keepIds.size()), [=](Menu* sub) {
			bool any = false;
			for (int64_t mid : APP->engine->getModuleIds()) {
				Module* mod = APP->engine->getModule(mid);
				Keepable* k = dynamic_cast<Keepable*>(mod);
				if (!k || mod == m)
					continue;
				any = true;
				bool isVault = dynamic_cast<Vault*>(mod) != nullptr;
				sub->addChild(createCheckMenuItem(k->keepLabel(), isVault && m->vaultId == mid ? "scenes launch here" : "", [=]() { return m->isLinked(mid) || m->vaultId == mid; }, [=]() { m->toggleLink(APP->engine->getModule(mid)); }));
			}
			if (!any)
				sub->addChild(createMenuLabel("No VAULT or generator in the rack"));
		}));
		menu->addChild(createIndexSubmenuItem("A snapshot stores the settings of",
			{"Every module in the rack", "Codex modules only", "Mapped and linked modules only"},
			[=]() { return m->scope; }, [=](int i) { m->scope = i; }));
		menu->addChild(createMenuItem("Delete the whole version tree", string::f("%d versions", (int)m->tree.nodes.size()), [=]() {
			if (m->autoRunning)
				return;
			m->tree.clear();
			m->treeDirty = true;
		}, m->tree.nodes.empty()));
	}
};

Model* modelSteer = createModel<Steer, SteerWidget>("Steer");
