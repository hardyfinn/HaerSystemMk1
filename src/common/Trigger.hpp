// Gate inputs and trigger outputs for up to 16 poly channels (spec §2, §3.3).
// These follow the same rules as Rack's dsp::SchmittTrigger and
// dsp::PulseGenerator but are plain C++ so the unit tests can use them.
#pragma once

namespace codex {

// Input: goes high at 1 V, goes low again at 0.1 V.
struct SchmittIn {
	static const int CH = 16;
	bool high[CH] = {};

	void reset() {
		for (int c = 0; c < CH; c++)
			high[c] = false;
	}
	// True on the sample where channel c rises.
	bool process(int c, float volts) {
		if (high[c]) {
			if (volts <= 0.1f)
				high[c] = false;
			return false;
		}
		if (volts >= 1.f) {
			high[c] = true;
			return true;
		}
		return false;
	}
	bool isHigh(int c) const { return high[c]; }
};

// Output: a 10 V pulse lasting 1 ms.
struct TriggerOut {
	static const int CH = 16;
	float remaining[CH] = {};

	void reset() {
		for (int c = 0; c < CH; c++)
			remaining[c] = 0.f;
	}
	void fire(int c, float seconds = 1e-3f) {
		if (seconds > remaining[c])
			remaining[c] = seconds;
	}
	// Call once per sample per channel. Returns 10 V while the pulse lasts.
	float process(int c, float sampleTime) {
		if (remaining[c] > 0.f) {
			remaining[c] -= sampleTime;
			return 10.f;
		}
		return 0.f;
	}
};

} // namespace codex

namespace codex {

// A trigger output that still works at audio rate. A normal hit gives the
// standard 1 ms pulse. When hits come faster than that, each pulse lasts half
// the gap to the hit before it, and a pulse that is still high drops for one
// sample before the next one starts. So every hit is a separate rising edge
// and fast hits become a pitched pulse train instead of one stuck-high gate.
struct AdaptiveTrigger {
	static const int CH = 16;
	float remaining[CH] = {};
	float sinceLast[CH];
	bool dip[CH] = {};

	AdaptiveTrigger() { reset(); }
	void reset() {
		for (int c = 0; c < CH; c++) {
			remaining[c] = 0.f;
			sinceLast[c] = 1.f;
			dip[c] = false;
		}
	}
	// maxSeconds: the length of a pulse when hits are far apart (1 ms for triggers, longer for gates)
	void fire(int c, float maxSeconds = 1e-3f) {
		dip[c] = remaining[c] > 0.f;
		float len = sinceLast[c] * 0.5f;
		remaining[c] = len < maxSeconds ? len : maxSeconds;
		sinceLast[c] = 0.f;
	}
	// Call once per sample per channel, after any fire() for this sample.
	float process(int c, float sampleTime) {
		if (sinceLast[c] < 1.f)
			sinceLast[c] += sampleTime;
		if (dip[c]) {
			dip[c] = false;
			return 0.f;
		}
		if (remaining[c] > 0.f) {
			remaining[c] -= sampleTime;
			return 10.f;
		}
		return 0.f;
	}
};

} // namespace codex
