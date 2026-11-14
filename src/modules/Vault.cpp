// VAULT: rolling capture, KEEP and clip launcher (spec §12). The recorder,
// clips and players are in dsp/VaultCore; this file is the Rack module.
//
// Who owns what: the clip bank belongs to the UI thread. The audio thread only
// reads clip pointers out of it. A clip taken out of the bank is not freed
// until the audio thread has been round twice and no lane points at it.
#include "plugin.hpp"
#include "modules/VaultLayout.hpp"
#include "modules/Vault.hpp"
#include "common/Phase.hpp"
#include "common/Theme.hpp"
#include "common/Trigger.hpp"
#include <osdialog.h>
#include <ctime>
#include <fstream>
#include <sstream>

using namespace codex;
namespace L = layout_vault;

static const int KEEP_BARS[7] = {1, 2, 4, 8, 16, 32, 64};

Vault::Vault() {
	config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	configParam(TEMPO_PARAM, 20.f, 300.f, 100.f, "Tempo (used when nothing is patched to PHASE)", " BPM");
	configParam(BARS_PARAM, 0.f, 6.f, 3.f, "How many bars KEEP takes (0..6 = 1, 2, 4, 8, 16, 32, 64 bars)");
	paramQuantities[BARS_PARAM]->snapEnabled = true;
	configButton(KEEP_PARAM, "Keep: copy the last bars into the next free clip slot");
	configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (decides which notes THIN leaves out; a new seed starts when the clip next wraps)");
	paramQuantities[SEED_PARAM]->snapEnabled = true;
	configParam(LANE_PARAM, 1.f, 8.f, 1.f, "Lane that CLIP, THIN and TRANS are showing and setting");
	paramQuantities[LANE_PARAM]->snapEnabled = true;
	configParam(CLIP_PARAM, 0.f, 64.f, 0.f, "Clip this lane plays at its next launch (0 = stop)");
	paramQuantities[CLIP_PARAM]->snapEnabled = true;
	configParam(THIN_PARAM, 0.f, 1.f, 0.f, "Thin: chance that each note of this lane is left out", " %", 0.f, 100.f);
	configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose this lane's CV (only if the lane is marked as pitch in the right-click menu)", " semitones");
	paramQuantities[TRANSPOSE_PARAM]->snapEnabled = true;
	configInput(PHASE_INPUT, "Phase (0 to 10 V ramp, one bar). Recording and playback both follow it");
	configInput(KEEP_INPUT, "Keep trigger");
	configInput(CLIP_INPUT, "Clip CV for the chosen lane (10 V = 64 clips along)");
	configInput(THIN_INPUT, "Thin CV for the chosen lane (10 V = full range)");
	configInput(TRANSPOSE_INPUT, "Transpose CV for the chosen lane (1 V = 12 semitones)");
	configInput(STOP_INPUT, "Stop all lanes at once (trigger)");
	configOutput(END_OUTPUT, "End: a trigger each time a lane's clip wraps round (poly, one channel per lane)");
	for (int l = 0; l < LANES; l++) {
		configButton(LAUNCH_PARAM + l, string::f("Launch lane %d (starts its chosen clip at the next bar or beat)", l + 1));
		configInput(GATE_INPUT + l, string::f("Lane %d gates to record (poly)", l + 1));
		configInput(CV_INPUT + l, string::f("Lane %d CV to record: pitch, velocity or a lock (poly)", l + 1));
		configInput(LAUNCH_INPUT + l, string::f("Launch lane %d (trigger)", l + 1));
		configOutput(GATE_OUTPUT + l, string::f("Lane %d gates (poly)", l + 1));
		configOutput(CV_OUTPUT + l, string::f("Lane %d CV (poly)", l + 1));
		configLight(PLAY_LIGHT + l, string::f("Lane %d is playing (blinks while waiting for its launch)", l + 1));
		player[l].lane = l;
		laneSlot[l] = -1;
		laneWait[l] = -1;
		lanePos[l] = 0.f;
		resume[l] = -1;
	}
	for (int i = 0; i < SLOTS; i++)
		bank[i] = nullptr;
}

Vault::~Vault() {
	for (int i = 0; i < SLOTS; i++) {
		delete bank[i].load();
		if (linked[i])
			json_decref(linked[i]);
	}
	for (auto& g : graveyard)
		delete g.first;
}

// ---------------------------------------------------------------- clip bank (UI thread)

int Vault::firstFreeSlot() const {
	for (int i = 0; i < SLOTS; i++)
		if (!bank[i].load())
			return i;
	return -1;
}

void Vault::removeClip(int slot) {
	if (slot < 0 || slot >= SLOTS)
		return;
	VaultClip* old = bank[slot].exchange(nullptr);
	if (old)
		graveyard.push_back({old, audioTick.load()});
	if (linked[slot]) {
		json_decref(linked[slot]);
		linked[slot] = nullptr;
	}
	dirty = true;
}

int Vault::addClip(VaultClip* clip, int slot) {
	if (!clip)
		return -1;
	if (slot < 0 || slot >= SLOTS || bank[slot].load())
		slot = firstFreeSlot();
	if (slot < 0) {
		delete clip;
		return -1;
	}
	if (clip->name.empty())
		clip->name = string::f("clip %02d", slot + 1);
	bank[slot].store(clip);
	dirty = true;
	return slot;
}

// Frees clips that were taken out of the bank, once the audio thread can no longer be using them.
void Vault::collect() {
	uint64_t now = audioTick.load();
	for (size_t i = 0; i < graveyard.size();) {
		if (now >= graveyard[i].second + 2) {
			delete graveyard[i].first;
			graveyard.erase(graveyard.begin() + (long)i);
		}
		else
			i++;
	}
}

json_t* Vault::captureOthers() {
	json_t* arr = json_array();
	for (int64_t mid : APP->engine->getModuleIds()) {
		Module* m = APP->engine->getModule(mid);
		Keepable* k = dynamic_cast<Keepable*>(m);
		if (!k || m == this)
			continue;
		json_t* one = json_object();
		json_object_set_new(one, "module", json_integer(mid));
		json_object_set_new(one, "label", json_string(k->keepLabel().c_str()));
		json_object_set_new(one, "state", k->captureState());
		json_array_append_new(arr, one);
	}
	return arr;
}

int Vault::recallLinked(int slot) {
	if (slot < 0 || slot >= SLOTS || !linked[slot])
		return 0;
	int n = 0;
	size_t i;
	json_t* one;
	json_array_foreach(linked[slot], i, one) {
		json_t* idJ = json_object_get(one, "module");
		json_t* state = json_object_get(one, "state");
		if (!idJ || !state)
			continue;
		Keepable* k = dynamic_cast<Keepable*>(APP->engine->getModule(json_integer_value(idJ)));
		if (!k || k == this)
			continue;
		k->requestRestore(json_deep_copy(state));
		n++;
	}
	return n;
}

// UI thread, every frame.
void Vault::uiStep() {
	collect();
	bool all = keepAllPending.exchange(false);
	if (keepPending.exchange(false) || all) {
		int bars = KEEP_BARS[clamp((int)std::lround(params[BARS_PARAM].getValue()), 0, 6)];
		int slot = firstFreeSlot();
		VaultClip* clip = slot >= 0 ? recorder.keep(dispW.load(), bars) : nullptr;
		if (slot < 0)
			message = "ALL 64 SLOTS ARE FULL";
		else if (!clip)
			message = "NOTHING IN THE LAST BARS";
		else {
			clip->bpm = dispBpm.load();
			clip->beats = (float)beats;
			clip->time = (int64_t)std::time(nullptr);
			slot = addClip(clip, slot);
			if (all && slot >= 0)
				linked[slot] = captureOthers();
			selectedSlot = slot;
			message = string::f("KEPT %d BAR%s IN SLOT %d", bars, bars == 1 ? "" : "S", slot + 1);
		}
		messageUntil = system::getTime() + 3.0;
	}
}

void Vault::setLaneClip(int lane, int number) {
	lane = clamp(lane, 0, LANES - 1);
	number = clamp(number, 0, SLOTS);
	clipSel[lane] = number;
	if (lane == selectedLane.load())
		params[CLIP_PARAM].setValue((float)number);
}

void Vault::requestLaunch(int lane, int clipNumber, bool atBar) {
	if (lane < 0 || lane >= LANES)
		return;
	setLaneClip(lane, clipNumber);
	if (atBar)
		launchBarMask.fetch_or(1 << lane);
	launchMask.fetch_or(1 << lane);
}

// ---------------------------------------------------------------- audio

void Vault::onReset(const ResetEvent& e) {
	Module::onReset(e);
	for (int i = 0; i < SLOTS; i++)
		removeClip(i);
	for (int l = 0; l < LANES; l++) {
		player[l].stop();
		clipSel[l] = 0;
		thin[l] = 0.f;
		transpose[l] = 0.f;
		pitch[l] = reverse[l] = false;
		winStart[l] = 0.f;
		winLen[l] = 1.f;
		resume[l] = -1;
	}
	quantBeat = false;
	beats = 4;
	recorder.clear();
	W = 0.0;
	phase.reset();
	lastLane = -1;
}

void Vault::launchLane(int l, bool forceBar) {
	int number = clamp(clipSel[l] + (l == lastLane ? clipCvSteps : 0), 0, SLOTS);
	const VaultClip* c = number > 0 ? bank[number - 1].load() : nullptr;
	double b = (double)beats;
	double at = (quantBeat && !forceBar) ? std::ceil(W * b) / b : std::ceil(W);
	player[l].launch(c, number - 1, at);
}

void Vault::process(const ProcessArgs& args) {
	// ---- where we are, in bars ----
	bool phaseOn = inputs[PHASE_INPUT].isConnected();
	if (phaseOn != phaseWas) {
		// the bar count starts again: older recordings cannot be lined up with it
		phaseWas = phaseOn;
		phase.reset();
		W = 0.0;
		recorder.clear();
		for (int l = 0; l < LANES; l++)
			player[l].transportReset();
	}
	if (phaseOn) {
		phase.process(inputs[PHASE_INPUT].getVoltage());
		W = phase.W;
		rate += (std::fabs(phase.delta()) - rate) * 0.0005;
	}
	else {
		double step = (double)params[TEMPO_PARAM].getValue() / (60.0 * (double)beats) * (double)args.sampleTime;
		W += step;
		rate = step;
	}

	// ---- knobs, a few thousand times a second ----
	bool tick = divider == 0;
	divider = (divider + 1) & 7;
	if (tick) {
		int lane = clamp((int)std::lround(params[LANE_PARAM].getValue()) - 1, 0, LANES - 1);
		if (lane != lastLane) {
			// the three knobs now show the newly chosen lane
			lastLane = lane;
			selectedLane = lane;
			params[CLIP_PARAM].setValue((float)clipSel[lane]);
			params[THIN_PARAM].setValue(thin[lane]);
			params[TRANSPOSE_PARAM].setValue(transpose[lane]);
		}
		else {
			clipSel[lane] = clamp((int)std::lround(params[CLIP_PARAM].getValue()), 0, SLOTS);
			thin[lane] = params[THIN_PARAM].getValue();
			transpose[lane] = params[TRANSPOSE_PARAM].getValue();
		}
		clipCvSteps = (int)std::lround(inputs[CLIP_INPUT].getVoltage() * 6.4f);
		int seed = (int)std::lround(params[SEED_PARAM].getValue());
		for (int l = 0; l < LANES; l++) {
			VaultLaneSettings& s = settings[l];
			bool sel = l == lane;
			s.thin = clamp(thin[l] + (sel ? inputs[THIN_INPUT].getVoltage() * 0.1f : 0.f), 0.f, 1.f);
			s.transpose = clamp(transpose[l] + (sel ? inputs[TRANSPOSE_INPUT].getVoltage() * 12.f : 0.f), -48.f, 48.f);
			s.pitch = pitch[l];
			s.reverse = reverse[l];
			s.winStart = winStart[l];
			s.winLen = winLen[l];
			s.seed = seed;
			// a clip that was deleted stops
			const VaultClip* c = player[l].clip();
			if (c && bank[clamp(player[l].slot(), 0, SLOTS - 1)].load() != c)
				player[l].stop();
			const VaultClip* w = player[l].waitingClip();
			if (w && bank[clamp(player[l].waitingSlot(), 0, SLOTS - 1)].load() != w)
				player[l].cancelWaiting();
			// lanes that were playing when the patch was saved carry on
			int r = resume[l].exchange(-1);
			if (r >= 0 && r < SLOTS) {
				const VaultClip* rc = bank[r].load();
				if (rc)
					player[l].launch(rc, r, W);
			}
			laneSlot[l].store(player[l].slot(), std::memory_order_relaxed);
			laneWait[l].store(player[l].waiting() ? (player[l].waitingSlot() >= 0 ? player[l].waitingSlot() : SLOTS) : -1, std::memory_order_relaxed);
			lanePos[l].store(player[l].position(), std::memory_order_relaxed);
		}
		dispW.store(W, std::memory_order_relaxed);
		dispBpm.store((float)(rate * (double)args.sampleRate * 60.0 * (double)beats), std::memory_order_relaxed);
		audioTick.fetch_add(1);
		blink += 8.f * args.sampleTime;
		if (blink > 0.4f)
			blink -= 0.4f;
	}

	// ---- keep ----
	bool keepPressed = keepButton.process(params[KEEP_PARAM].getValue() > 0.5f);
	bool keepTrig = keepIn.process(0, inputs[KEEP_INPUT].getVoltage());
	if (keepPressed || keepTrig)
		keepPending = true;

	// ---- record ----
	for (int l = 0; l < LANES; l++) {
		Input& gi = inputs[GATE_INPUT + l];
		Input& ci = inputs[CV_INPUT + l];
		int gc = gi.isConnected() ? gi.getChannels() : 0;
		int cc = ci.isConnected() ? ci.getChannels() : 0;
		for (int c = 0; c < cc; c++)
			recorder.cv(W, l, c, ci.getVoltage(c));
		int top = std::max(gc, gateChannels[l]);
		for (int c = 0; c < top; c++) {
			bool was = gateIn[l].isHigh(c);
			if (c < gc)
				gateIn[l].process(c, gi.getVoltage(c));
			else
				gateIn[l].high[c] = false; // the cable lost this channel: its note ends
			bool now = gateIn[l].isHigh(c);
			if (now != was)
				recorder.gate(W, l, c, now, cc ? ci.getPolyVoltage(c) : 0.f);
		}
		gateChannels[l] = gc;
	}

	// ---- launch and stop ----
	if (stopIn.process(0, inputs[STOP_INPUT].getVoltage()))
		for (int l = 0; l < LANES; l++)
			player[l].stop();
	int asked = launchMask.exchange(0);
	int askedBar = asked ? launchBarMask.exchange(0) : 0;
	for (int l = 0; l < LANES; l++) {
		bool pressed = launchButton[l].process(params[LAUNCH_PARAM + l].getValue() > 0.5f);
		bool trig = launchIn.process(l, inputs[LAUNCH_INPUT + l].getVoltage());
		if (pressed || trig)
			launchLane(l, false);
		else if ((asked >> l) & 1)
			launchLane(l, (askedBar >> l) & 1);
	}

	// ---- play ----
	outputs[END_OUTPUT].setChannels(LANES);
	for (int l = 0; l < LANES; l++) {
		VaultPlayer& pl = player[l];
		pl.process(W, settings[l]);
		int n = pl.channels;
		outputs[GATE_OUTPUT + l].setChannels(n);
		outputs[CV_OUTPUT + l].setChannels(n);
		for (int c = 0; c < n; c++) {
			outputs[GATE_OUTPUT + l].setVoltage(pl.gateVolts(c), c);
			outputs[CV_OUTPUT + l].setVoltage(clamp(pl.cvVolts(c), -12.f, 12.f), c);
		}
		if (pl.end)
			endOut.fire(l);
		outputs[END_OUTPUT].setVoltage(endOut.process(l, args.sampleTime), l);
		if (tick)
			lights[PLAY_LIGHT + l].setBrightness(pl.clip() ? 1.f : (pl.waiting() && blink < 0.2f ? 1.f : 0.f));
	}
}

// ---------------------------------------------------------------- saving

json_t* Vault::lanesToJson() {
	json_t* arr = json_array();
	for (int l = 0; l < LANES; l++) {
		json_t* j = json_object();
		json_object_set_new(j, "clip", json_integer(clipSel[l]));
		json_object_set_new(j, "thin", json_real(thin[l]));
		json_object_set_new(j, "transpose", json_real(transpose[l]));
		json_object_set_new(j, "pitch", json_boolean(pitch[l]));
		json_object_set_new(j, "reverse", json_boolean(reverse[l]));
		json_object_set_new(j, "winStart", json_real(winStart[l]));
		json_object_set_new(j, "winLen", json_real(winLen[l]));
		json_object_set_new(j, "playing", json_integer(laneSlot[l].load()));
		json_array_append_new(arr, j);
	}
	return arr;
}

void Vault::lanesFromJson(json_t* arr) {
	if (!arr || !json_is_array(arr))
		return;
	for (int l = 0; l < LANES; l++) {
		json_t* j = json_array_get(arr, (size_t)l);
		if (!j)
			continue;
		json_t* v;
		if ((v = json_object_get(j, "clip")))
			clipSel[l] = clamp((int)json_integer_value(v), 0, SLOTS);
		if ((v = json_object_get(j, "thin")))
			thin[l] = clamp((float)json_number_value(v), 0.f, 1.f);
		if ((v = json_object_get(j, "transpose")))
			transpose[l] = clamp((float)json_number_value(v), -24.f, 24.f);
		if ((v = json_object_get(j, "pitch")))
			pitch[l] = json_is_true(v);
		if ((v = json_object_get(j, "reverse")))
			reverse[l] = json_is_true(v);
		if ((v = json_object_get(j, "winStart")))
			winStart[l] = clamp((float)json_number_value(v), 0.f, 1.f);
		if ((v = json_object_get(j, "winLen")))
			winLen[l] = clamp((float)json_number_value(v), 0.f, 1.f);
		if ((v = json_object_get(j, "playing")))
			resume[l] = (int)json_integer_value(v);
	}
	lastLane = -1; // the knobs take the chosen lane's values again
}

json_t* Vault::dataToJson() {
	json_t* root = json_object();
	json_object_set_new(root, "lanes", lanesToJson());
	json_object_set_new(root, "quantBeat", json_boolean(quantBeat));
	json_object_set_new(root, "beats", json_integer(beats));
	json_object_set_new(root, "selectedSlot", json_integer(selectedSlot));
	return root;
}

void Vault::dataFromJson(json_t* root) {
	lanesFromJson(json_object_get(root, "lanes"));
	json_t* v;
	if ((v = json_object_get(root, "quantBeat")))
		quantBeat = json_is_true(v);
	if ((v = json_object_get(root, "beats")))
		beats = clamp((int)json_integer_value(v), 1, 16);
	if ((v = json_object_get(root, "selectedSlot")))
		selectedSlot = clamp((int)json_integer_value(v), 0, SLOTS - 1);
}

json_t* Vault::captureState() {
	json_t* root = json_object();
	json_object_set_new(root, "lanes", lanesToJson());
	return root;
}

void Vault::requestRestore(json_t* state) {
	if (!state)
		return;
	json_t* arr = json_object_get(state, "lanes");
	lanesFromJson(arr);
	// lanes that were playing start their clip at the next bar; the others stop there
	for (int l = 0; l < LANES; l++) {
		int r = resume[l].exchange(-1);
		requestLaunch(l, r >= 0 ? r + 1 : 0, true);
	}
	json_decref(state);
}

std::string Vault::keepLabel() { return string::f("VAULT #%lld", (long long)id); }

std::string Vault::bankToText() {
	std::string out = "{\"version\":1,\"clips\":[";
	bool first = true;
	for (int i = 0; i < SLOTS; i++) {
		const VaultClip* c = bank[i].load();
		if (!c)
			continue;
		if (!first)
			out += ',';
		first = false;
		out += "{\"slot\":" + std::to_string(i) + ",\"clip\":" + vaultClipToJson(*c);
		if (linked[i]) {
			char* s = json_dumps(linked[i], JSON_COMPACT);
			if (s) {
				out += ",\"linked\":";
				out += s;
				std::free(s);
			}
		}
		out += '}';
	}
	out += "]}";
	return out;
}

void Vault::onSave(const SaveEvent& e) {
	std::string dir = createPatchStorageDirectory();
	if (dir.empty())
		return;
	std::string path = system::join(dir, "clips.json");
	// clips can be large: only write them when something changed
	if (!dirty && system::exists(path))
		return;
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f)
		return;
	f << bankToText();
	dirty = !f.good();
}

void Vault::onAdd(const AddEvent& e) {
	std::string dir = getPatchStorageDirectory();
	if (dir.empty())
		return;
	std::string path = system::join(dir, "clips.json");
	if (!system::exists(path))
		return;
	json_error_t err;
	json_t* root = json_load_file(path.c_str(), 0, &err);
	if (!root)
		return;
	size_t i;
	json_t* one;
	json_array_foreach(json_object_get(root, "clips"), i, one) {
		int slot = (int)json_integer_value(json_object_get(one, "slot"));
		json_t* cj = json_object_get(one, "clip");
		if (slot < 0 || slot >= SLOTS || !cj || bank[slot].load())
			continue;
		char* text = json_dumps(cj, JSON_COMPACT | JSON_REAL_PRECISION(17));
		if (!text)
			continue;
		VaultClip* clip = new VaultClip;
		if (vaultClipFromJson(text, *clip)) {
			bank[slot].store(clip);
			json_t* lj = json_object_get(one, "linked");
			if (lj && json_is_array(lj))
				linked[slot] = json_incref(lj);
		}
		else
			delete clip;
		std::free(text);
	}
	json_decref(root);
	dirty = false;
}

// ---------------------------------------------------------------- screen

// Left: the 64 clip slots. Right: the eight lanes and what each is playing.
struct VaultDisplay : OpaqueWidget {
	Vault* module = nullptr;

	float gridSize() const { return box.size.y; }

	int slotAt(Vec pos) const {
		float cell = gridSize() / 8.f;
		if (pos.x < 0 || pos.x >= gridSize() || pos.y < 0 || pos.y >= gridSize())
			return -1;
		return clamp((int)(pos.y / cell), 0, 7) * 8 + clamp((int)(pos.x / cell), 0, 7);
	}
	int laneAt(Vec pos) const {
		if (pos.x < gridSize() + 4.f || pos.y < 14.f)
			return -1;
		float rowH = (box.size.y - 14.f) / 8.f;
		return clamp((int)((pos.y - 14.f) / rowH), 0, 7);
	}

	void slotMenu(int slot);

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS) {
			OpaqueWidget::onButton(e);
			return;
		}
		int slot = slotAt(e.pos), lane = laneAt(e.pos);
		if (slot >= 0) {
			module->selectedSlot = slot;
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT)
				slotMenu(slot);
			e.consume(this);
		}
		else if (lane >= 0 && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			module->params[Vault::LANE_PARAM].setValue((float)(lane + 1));
			e.consume(this);
		}
		else
			OpaqueWidget::onButton(e);
	}

	// double-click a full slot: the chosen lane plays it from the next bar or beat
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!module)
			return;
		int slot = module->selectedSlot;
		if (slot >= 0 && module->bank[slot].load())
			module->requestLaunch(module->selectedLane.load(), slot + 1, false);
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
			float pulse = 0.55f + 0.45f * (float)std::sin(system::getTime() * 6.0);
			const float cell = gridSize() / 8.f;

			int playing[Vault::LANES], waiting[Vault::LANES];
			for (int l = 0; l < Vault::LANES; l++) {
				playing[l] = module ? module->laneSlot[l].load() : (l == 0 ? 2 : -1);
				waiting[l] = module ? module->laneWait[l].load() : -1;
			}
			// ---- slots ----
			for (int i = 0; i < Vault::SLOTS; i++) {
				float x = (float)(i % 8) * cell, y = (float)(i / 8) * cell;
				bool full = module ? module->bank[i].load() != nullptr : i < 5;
				bool plays = false, waits = false;
				for (int l = 0; l < Vault::LANES; l++) {
					plays = plays || playing[l] == i;
					waits = waits || waiting[l] == i;
				}
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x + 1.5f, y + 1.5f, cell - 3.f, cell - 3.f, 1.5f);
				if (plays)
					nvgFillColor(vg, nvgTransRGBAf(violet, pulse));
				else if (full)
					nvgFillColor(vg, nvgTransRGBAf(violet, waits ? 0.25f + 0.3f * pulse : 0.45f));
				else
					nvgFillColor(vg, theme::grid());
				nvgFill(vg);
				if (module && module->selectedSlot == i) {
					nvgBeginPath(vg);
					nvgRoundedRect(vg, x + 0.75f, y + 0.75f, cell - 1.5f, cell - 1.5f, 2.f);
					nvgStrokeColor(vg, theme::text());
					nvgStrokeWidth(vg, 1.2f);
					nvgStroke(vg);
				}
				if (full) {
					nvgFontSize(vg, 8.f);
					nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, plays ? theme::screen() : theme::text());
					nvgText(vg, x + cell * 0.5f, y + cell * 0.5f, string::f("%d", i + 1).c_str(), NULL);
				}
			}
			// ---- lanes ----
			float lx = gridSize() + 6.f, lw = box.size.x - lx - 3.f;
			nvgFontSize(vg, 9.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, theme::dim());
			std::string head = "LANE  CLIP";
			if (module && system::getTime() < module->messageUntil)
				head = module->message;
			else if (module) {
				double w = module->dispW.load();
				head = string::f("BAR %d  %.0f BPM", (int)std::floor(w) + 1, module->dispBpm.load());
			}
			nvgText(vg, lx, 2.f, head.c_str(), NULL);
			float rowH = (box.size.y - 14.f) / 8.f;
			int sel = module ? module->selectedLane.load() : 0;
			for (int l = 0; l < Vault::LANES; l++) {
				float y = 14.f + (float)l * rowH;
				if (l == sel) {
					nvgBeginPath(vg);
					nvgRect(vg, lx - 2.f, y, lw + 4.f, rowH - 1.f);
					nvgFillColor(vg, theme::grid());
					nvgFill(vg);
				}
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, l == sel ? theme::text() : theme::dim());
				nvgText(vg, lx, y + rowH * 0.5f, string::f("%d", l + 1).c_str(), NULL);
				std::string what;
				int chosen = module ? module->clipSel[l] : (l == 0 ? 3 : 0);
				if (playing[l] >= 0)
					what = string::f("%02d", playing[l] + 1);
				else
					what = chosen ? string::f("(%02d)", chosen) : "--";
				if (waiting[l] >= 0 && waiting[l] != playing[l])
					what += waiting[l] >= Vault::SLOTS ? " >stop" : string::f(" >%02d", waiting[l] + 1);
				nvgFillColor(vg, playing[l] >= 0 ? violet : theme::dim());
				nvgText(vg, lx + 10.f, y + rowH * 0.5f, what.c_str(), NULL);
				// how far through its clip the lane is
				if (playing[l] >= 0) {
					float pos = module ? module->lanePos[l].load() : 0.4f;
					float bx = lx + 48.f, bw = std::max(lw - 48.f, 4.f);
					nvgBeginPath(vg);
					nvgRect(vg, bx, y + rowH * 0.5f - 1.5f, bw, 3.f);
					nvgFillColor(vg, theme::grid());
					nvgFill(vg);
					nvgBeginPath(vg);
					nvgRect(vg, bx, y + rowH * 0.5f - 1.5f, bw * clamp(pos, 0.f, 1.f), 3.f);
					nvgFillColor(vg, violet);
					nvgFill(vg);
				}
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}
};

// A text box in a menu that renames a clip as you type.
struct ClipNameField : ui::TextField {
	Vault* module = nullptr;
	int slot = 0;
	ClipNameField() { box.size.x = 180.f; }
	void onChange(const ChangeEvent& e) override {
		VaultClip* c = module ? module->bank[slot].load() : nullptr;
		if (c) {
			c->name = text;
			module->dirty = true;
		}
	}
	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
			if (overlay)
				overlay->requestDelete();
			e.consume(this);
		}
		if (!e.getTarget())
			ui::TextField::onSelectKey(e);
	}
};

static void appendSlotMenu(Menu* menu, Vault* m, int slot) {
	VaultClip* c = m->bank[slot].load();
	if (!c) {
		menu->addChild(createMenuLabel(string::f("Slot %d: empty", slot + 1)));
	}
	else {
		char when[32] = "";
		std::time_t t = (std::time_t)c->time;
		std::tm* tmv = std::localtime(&t);
		if (tmv)
			std::strftime(when, sizeof(when), "%d %b %H:%M", tmv);
		menu->addChild(createMenuLabel(string::f("Slot %d: %d bars, %d events, %.0f BPM, kept %s", slot + 1, c->bars, (int)c->eventCount(), c->bpm, when)));
		ClipNameField* name = new ClipNameField;
		name->module = m;
		name->slot = slot;
		name->setText(c->name);
		menu->addChild(name);
		menu->addChild(createMenuItem("Play on the chosen lane (next bar or beat)", "double-click", [=]() { m->requestLaunch(m->selectedLane.load(), slot + 1, false); }));
		menu->addChild(createMenuItem("Duplicate into the next free slot", "", [=]() {
			VaultClip* src = m->bank[slot].load();
			if (!src)
				return;
			VaultClip* copy = new VaultClip(*src);
			copy->name = src->name + " copy";
			int to = m->addClip(copy, -1);
			if (to >= 0 && m->linked[slot])
				m->linked[to] = json_deep_copy(m->linked[slot]);
		}));
		menu->addChild(createMenuItem("Export as a JSON file", "", [=]() {
			VaultClip* src = m->bank[slot].load();
			if (!src)
				return;
			char* path = osdialog_file(OSDIALOG_SAVE, NULL, (src->name + ".json").c_str(), NULL);
			if (!path)
				return;
			std::ofstream f(path, std::ios::binary | std::ios::trunc);
			f << vaultClipToJson(*src);
			std::free(path);
		}));
		int states = m->linked[slot] ? (int)json_array_size(m->linked[slot]) : 0;
		menu->addChild(createMenuItem("Recall linked states (puts the generators back as they were)", states ? string::f("%d modules", states) : "none kept",
			[=]() { m->recallLinked(slot); }, !states));
		menu->addChild(createMenuItem("Delete", "", [=]() { m->removeClip(slot); }));
	}
	menu->addChild(createMenuItem(c ? "Import a JSON file into the next free slot" : "Import a JSON file into this slot", "", [=]() {
		char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
		if (!path)
			return;
		std::ifstream f(path, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		std::free(path);
		VaultClip* clip = new VaultClip;
		if (vaultClipFromJson(ss.str(), *clip)) {
			int to = m->addClip(clip, slot);
			if (to >= 0)
				m->selectedSlot = to;
		}
		else
			delete clip;
	}));
}

void VaultDisplay::slotMenu(int slot) {
	Menu* menu = createMenu();
	appendSlotMenu(menu, module, slot);
}

struct VaultWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	VaultWidget(Vault* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Vault.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		VaultDisplay* display = new VaultDisplay;
		display->module = module;
		display->box.pos = mm2px(Vec(L::DISPLAY[0][0], L::DISPLAY[0][1]));
		display->box.size = mm2px(Vec(L::DISPLAY[1][0], L::DISPLAY[1][1]));
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(at(L::TEMPO), module, Vault::TEMPO_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::BARS), module, Vault::BARS_PARAM));
		addParam(createParamCentered<VCVButton>(at(L::KEEP), module, Vault::KEEP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Vault::SEED_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::LANE), module, Vault::LANE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::CLIP), module, Vault::CLIP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::THIN), module, Vault::THIN_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::TRANSPOSE), module, Vault::TRANSPOSE_PARAM));
		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Vault::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::KEEP_IN), module, Vault::KEEP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::CLIP_IN), module, Vault::CLIP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::THIN_IN), module, Vault::THIN_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::TRANSPOSE_IN), module, Vault::TRANSPOSE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::STOP_IN), module, Vault::STOP_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::END_OUT), module, Vault::END_OUTPUT));
		for (int l = 0; l < Vault::LANES; l++) {
			addInput(createInputCentered<PJ301MPort>(at(L::GATE_IN[l]), module, Vault::GATE_INPUT + l));
			addInput(createInputCentered<PJ301MPort>(at(L::CV_IN[l]), module, Vault::CV_INPUT + l));
			addParam(createParamCentered<VCVButton>(at(L::LAUNCH[l]), module, Vault::LAUNCH_PARAM + l));
			addInput(createInputCentered<PJ301MPort>(at(L::LAUNCH_IN[l]), module, Vault::LAUNCH_INPUT + l));
			addChild(createLightCentered<MediumLight<GreenLight>>(at(L::PLAY_LIGHT[l]), module, Vault::PLAY_LIGHT + l));
			addOutput(createOutputCentered<PJ301MPort>(at(L::GATE_OUT[l]), module, Vault::GATE_OUTPUT + l));
			addOutput(createOutputCentered<PJ301MPort>(at(L::CV_OUT[l]), module, Vault::CV_OUTPUT + l));
		}
	}

	void step() override {
		Vault* m = getModule<Vault>();
		if (m)
			m->uiStep();
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Vault* m = getModule<Vault>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Keep all: the clip, and the state of every generator in the rack", "", [=]() { m->keepAllPending = true; }));
		menu->addChild(createSubmenuItem(string::f("Selected clip (slot %d)", m->selectedSlot + 1), "", [=](Menu* sub) { appendSlotMenu(sub, m, m->selectedSlot); }));
		menu->addChild(createIndexSubmenuItem("Launches wait for", {"The next bar", "The next beat"}, [=]() { return m->quantBeat ? 1 : 0; }, [=](int i) { m->quantBeat = i == 1; }));
		menu->addChild(createSubmenuItem("Beats in a bar", string::f("%d", m->beats), [=](Menu* sub) {
			for (int b = 1; b <= 16; b++)
				sub->addChild(createCheckMenuItem(string::f("%d", b), "", [=]() { return m->beats == b; }, [=]() { m->beats = b; }));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Lanes"));
		for (int l = 0; l < Vault::LANES; l++) {
			menu->addChild(createSubmenuItem(string::f("Lane %d", l + 1), "", [=](Menu* sub) {
				sub->addChild(createBoolPtrMenuItem("This lane's CV is pitch (TRANS applies to it)", "", &m->pitch[l]));
				sub->addChild(createBoolPtrMenuItem("Reverse", "", &m->reverse[l]));
				sub->addChild(createSubmenuItem("Window start", string::f("%.0f %%", m->winStart[l] * 100.f), [=](Menu* w) {
					for (int k = 0; k < 8; k++)
						w->addChild(createCheckMenuItem(string::f("%d/8 of the clip", k), "", [=]() { return std::fabs(m->winStart[l] - (float)k / 8.f) < 1e-3f; }, [=]() { m->winStart[l] = (float)k / 8.f; }));
				}));
				sub->addChild(createSubmenuItem("Window length", string::f("%.1f %%", m->winLen[l] * 100.f), [=](Menu* w) {
					static const float LEN[] = {1.f, 0.75f, 0.5f, 0.375f, 0.25f, 0.125f, 0.0625f, 0.03125f};
					static const char* NAME[] = {"All of the clip", "3/4", "1/2", "3/8", "1/4", "1/8", "1/16", "1/32"};
					for (int k = 0; k < 8; k++)
						w->addChild(createCheckMenuItem(NAME[k], "", [=]() { return std::fabs(m->winLen[l] - LEN[k]) < 1e-4f; }, [=]() { m->winLen[l] = LEN[k]; }));
				}));
				sub->addChild(createMenuItem("Stop this lane at the next bar", "", [=]() { m->requestLaunch(l, 0, true); }));
			}));
		}
	}
};

Model* modelVault = createModel<Vault, VaultWidget>("Vault");
