// VAULT (spec §12): records streams of gates and control voltages as events
// measured in bars, keeps the last few bars as a clip, and plays clips back
// against a bar position, so a kept clip follows whatever the clock does now.
// Plain C++, no Rack.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace codex {

struct VaultEvent {
	enum Type { OFF = 0, ON = 1, CV = 2 };
	double pos = 0.0;  // bars: from the start of the recording, or inside a clip from the clip's first bar line
	float value = 0.f; // gate events: the lane's CV on that channel at the edge. CV events: the new value.
	uint8_t lane = 0, ch = 0, type = OFF;
};

// One kept clip. Once made, only `name` and `node` ever change (from the UI thread).
struct VaultClip {
	static constexpr int LANES = 8;
	std::vector<VaultEvent> lane[LANES]; // each sorted by pos
	int channels[LANES] = {};            // poly channels each lane needs
	int bars = 1;
	float bpm = 0.f, beats = 4.f;        // at capture time, for reference only
	int64_t time = 0;                    // when it was kept (seconds since 1970)
	int64_t node = -1;                   // linked STEER version-tree node, -1 = none
	std::string name;

	size_t eventCount() const {
		size_t n = 0;
		for (int l = 0; l < LANES; l++)
			n += lane[l].size();
		return n;
	}
};

// A clip as JSON text, and back. A round trip gives back exactly the same clip.
std::string vaultClipToJson(const VaultClip& clip);
bool vaultClipFromJson(const std::string& text, VaultClip& clip);

// The rolling recording: the newest 262,144 events. The audio thread writes,
// the UI thread reads (keep), and neither ever waits for the other.
struct VaultRecorder {
	static constexpr size_t CAPACITY = 262144;
	static constexpr int LANES = VaultClip::LANES;
	static constexpr int CH = 16;

	VaultRecorder();
	// ---- audio thread ----
	// Forget everything recorded so far (the bar count started again).
	void clear();
	// A gate edge on one channel. cv is the lane's CV on that channel right now.
	void gate(double W, int lane, int ch, bool on, float cv);
	// Call every sample for each CV channel in use. Stores an event when the value has moved by more
	// than 1 mV, at most every 1/384 bar, and once at the start of every bar so each bar stands on its own.
	void cv(double W, int lane, int ch, float value);
	// ---- UI thread ----
	// The `bars` whole bars before the bar that W is in, as a new clip (the caller owns it).
	// NULL when those bars hold nothing.
	VaultClip* keep(double W, int bars) const;
	uint64_t written() const { return total.load(std::memory_order_acquire); }

private:
	std::vector<VaultEvent> ring;
	std::atomic<uint64_t> total{0}; // events written since the start
	std::atomic<uint64_t> base{0};  // events before this one were cleared
	float lastCv[LANES][CH];
	double lastCvPos[LANES][CH];
	bool cvKnown[LANES][CH];
	void push(const VaultEvent& e);
};

struct VaultLaneSettings {
	float thin = 0.f;      // 0..1: chance that a note is left out
	float transpose = 0.f; // semitones, added to the CV when `pitch` is set
	bool pitch = false;
	bool reverse = false;
	float winStart = 0.f;  // loop only part of the clip: start and length as fractions of it
	float winLen = 1.f;
	int seed = 0;          // which notes THIN leaves out
};

// Plays one lane of a clip.
struct VaultPlayer {
	static constexpr int CH = 16;
	int lane = 0;

	// Change to `clip` (NULL = stop) when the bar position reaches atBar.
	void launch(const VaultClip* clip, int slot, double atBar);
	// Silence now and forget any waiting launch.
	void stop();
	// The bar count started again from 0: keep playing, lined up with the new count.
	void transportReset();
	void process(double W, const VaultLaneSettings& s);

	// ---- results of process() ----
	bool gate[CH] = {};
	bool dip[CH] = {}; // the gate must read low for this one sample (a new note on a gate that was still high)
	float cv[CH] = {};
	int channels = 1;
	bool end = false;  // true on the sample the clip wraps round
	float gateVolts(int c) const { return gate[c] && !dip[c] ? 10.f : 0.f; }
	float cvVolts(int c) const { return cv[c] + transposeNow; }

	const VaultClip* clip() const { return cur; }
	int slot() const { return cur ? curSlot : -1; }
	bool waiting() const { return hasPending; }
	int waitingSlot() const { return hasPending && pending ? pendingSlot : -1; }
	const VaultClip* waitingClip() const { return hasPending ? pending : nullptr; }
	// Forget a waiting launch (its clip was deleted).
	void cancelWaiting() {
		hasPending = false;
		pending = nullptr;
	}
	float position() const { return posFrac; } // 0..1 through the clip

private:
	const VaultClip* cur = nullptr;
	const VaultClip* pending = nullptr;
	int curSlot = -1, pendingSlot = -1;
	bool hasPending = false;
	double pendingBar = 0.0, launchBar = 0.0;
	size_t next = 0;      // events before this index are at or before the play position
	double ePrev = 0.0;
	double ws = 0.0, we = 1.0;
	bool wasReverse = false;
	bool fresh = false;
	float transposeNow = 0.f;
	int seedNow = 0;
	float posFrac = 0.f;

	void allOff();
	void settle(const std::vector<VaultEvent>& ev);
	bool thinned(size_t index, float thin) const;
	void fire(const VaultEvent& e, size_t index, bool backwards, const VaultLaneSettings& s);
};

} // namespace codex
