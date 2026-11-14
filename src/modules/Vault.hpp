// VAULT's module class, in a header so that STEER can send it launches.
#pragma once
#include "plugin.hpp"
#include "dsp/VaultCore.hpp"
#include "common/KeepProtocol.hpp"
#include "common/Phase.hpp"
#include "common/Trigger.hpp"
#include <atomic>
#include <string>
#include <utility>
#include <vector>

struct Vault : Module, codex::Keepable {
	static const int LANES = 8;
	static const int SLOTS = 64;
	enum ParamId {
		TEMPO_PARAM, BARS_PARAM, KEEP_PARAM, SEED_PARAM, LANE_PARAM, CLIP_PARAM, THIN_PARAM, TRANSPOSE_PARAM,
		ENUMS(LAUNCH_PARAM, LANES),
		PARAMS_LEN
	};
	enum InputId {
		PHASE_INPUT, KEEP_INPUT, CLIP_INPUT, THIN_INPUT, TRANSPOSE_INPUT, STOP_INPUT,
		ENUMS(GATE_INPUT, LANES), ENUMS(CV_INPUT, LANES), ENUMS(LAUNCH_INPUT, LANES),
		INPUTS_LEN
	};
	enum OutputId { END_OUTPUT, ENUMS(GATE_OUTPUT, LANES), ENUMS(CV_OUTPUT, LANES), OUTPUTS_LEN };
	enum LightId { ENUMS(PLAY_LIGHT, LANES), LIGHTS_LEN };

	// ---- the clip bank: changed on the UI thread only, read by the audio thread ----
	std::atomic<codex::VaultClip*> bank[SLOTS];
	json_t* linked[SLOTS] = {}; // "keep all": the generators' states that go with a clip
	std::vector<std::pair<codex::VaultClip*, uint64_t>> graveyard;
	bool dirty = false;
	int selectedSlot = 0;
	std::string message;
	double messageUntil = 0.0;

	// ---- lane settings (the knobs show and set the chosen lane's) ----
	int clipSel[LANES] = {};
	float thin[LANES] = {};
	float transpose[LANES] = {};
	bool pitch[LANES] = {};
	bool reverse[LANES] = {};
	float winStart[LANES] = {};
	float winLen[LANES] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	bool quantBeat = false;
	int beats = 4;

	// ---- audio thread ----
	codex::VaultRecorder recorder;
	codex::VaultPlayer player[LANES];
	codex::VaultLaneSettings settings[LANES];
	codex::PhaseReader phase;
	codex::SchmittIn gateIn[LANES];
	codex::SchmittIn launchIn, keepIn, stopIn;
	codex::TriggerOut endOut;
	dsp::BooleanTrigger keepButton, launchButton[LANES];
	int gateChannels[LANES] = {};
	double W = 0.0, rate = 0.0;
	bool phaseWas = false;
	int divider = 0, lastLane = -1, clipCvSteps = 0;
	float blink = 0.f;

	// ---- between the threads ----
	std::atomic<uint64_t> audioTick{0};
	std::atomic<bool> keepPending{false}, keepAllPending{false};
	std::atomic<int> launchMask{0}, launchBarMask{0};
	std::atomic<int> selectedLane{0};
	std::atomic<int> laneSlot[LANES], laneWait[LANES], resume[LANES];
	std::atomic<float> lanePos[LANES];
	std::atomic<double> dispW{0.0};
	std::atomic<float> dispBpm{0.f};

	Vault();
	~Vault();
	void onReset(const ResetEvent& e) override;
	void onAdd(const AddEvent& e) override;
	void onSave(const SaveEvent& e) override;
	void process(const ProcessArgs& args) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;

	// Keepable: which clips play on which lanes, and the lane settings
	json_t* captureState() override;
	void requestRestore(json_t* state) override;
	std::string keepLabel() override;

	// ---- UI thread ----
	void uiStep();
	int firstFreeSlot() const;
	int addClip(codex::VaultClip* clip, int slot); // takes ownership; returns the slot used or -1
	void removeClip(int slot);
	int recallLinked(int slot);
	void setLaneClip(int lane, int number);
	// Lane 0..7 plays clip 1..64 (0 = stop) from the next bar (atBar) or the next bar or beat as set in the menu.
	void requestLaunch(int lane, int clipNumber, bool atBar);

private:
	void collect();
	void launchLane(int l, bool forceBar);
	json_t* captureOthers();
	json_t* lanesToJson();
	void lanesFromJson(json_t* arr);
	std::string bankToText();
};
