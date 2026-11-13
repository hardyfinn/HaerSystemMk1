// FMX (spec §9): a four-operator FM voice whose loudness and brightness
// follow one envelope position, which can be driven by a phase signal.
// Plain C++, no Rack.
#pragma once
#include <cstdint>

namespace codex {

// Settings shared by all voices.
struct FmxGlobal {
	float ratio[4] = {1.f, 1.f, 2.f, 3.f}; // operators 1..4, 0.25..16
	float fine[4] = {0.f, 0.f, 0.f, 0.f};  // cents, ±50
	float level[4] = {1.f, 0.6f, 0.5f, 0.4f}; // 0..1
	float env[4] = {0.f, 1.f, 1.f, 1.f};   // 0..1, how much the index envelope shapes this operator
	float ampShape = 2.f;    // k in (1 - e)^k, 0.2..8
	float indexShape = 3.f;  // the same for the index envelope
	float time = 0.5f;       // seconds for e to run 0 -> 1 after a trigger
	bool resetPhases = false; // operators restart their cycles on each trigger
};

// Settings and signals for one voice, read every sample.
struct FmxVoiceIn {
	float freq = 261.63f;   // Hz
	float vel = 1.f;        // 0..1
	float index = 2.f;      // 0..10 radians
	float feedback = 0.f;   // 0..1
	int algorithm = 1;      // 1..8
	float ratioOffset[3] = {0.f, 0.f, 0.f}; // added to the ratios of operators 2, 3, 4 (already snapped by the caller)
	bool trig = false;      // true for one sample at each hit
	bool phaseDriven = false; // true: e follows `phase`
	float phase = 0.f;      // 0..1, used when phaseDriven
	bool drone = false;     // true: no trigger and no phase patched, so the voice just sounds
};

// Which operators feed which, for algorithms 1..8.
struct FmxAlgorithm {
	uint8_t mods[4];    // mods[i]: bit j set = operator j+1 modulates operator i+1
	uint8_t carriers;   // bit i set = operator i+1 is heard
};
const FmxAlgorithm& fmxAlgorithm(int number);

// Halves the sample rate: a 31-point half-band low-pass, then every second sample.
struct FmxDecimator {
	float buf[32] = {};
	int pos = 0;
	void reset();
	// a, b: two samples at the doubled rate, oldest first
	float process(float a, float b);
};

struct FmxVoice {
	FmxVoice() { reset(); }
	void reset();
	void setSampleRate(float sr) { sampleRate = sr; }
	// One sample out, in volts (±10 V at most). When ops is given it receives
	// the four operators' own outputs (±5 V at full level, following each
	// operator's LEVEL and ENV but not the loudness envelope).
	float process(const FmxGlobal& g, const FmxVoiceIn& in, float* ops = nullptr);
	float envPosition() const { return e; }
	bool isSounding() const { return !idle; }

	static float shape(float e, float k);

private:
	float sampleRate = 48000.f;
	uint32_t phase[4] = {}; // the full 32-bit range is one cycle
	float fbHist[2] = {};
	float e = 1.f;
	float lastPhase = 0.f;
	float ampSmooth = 0.f;
	float velHeld = 1.f;
	bool idle = true;
	// worked out every few samples
	int controlCount = 0;
	float ampTarget = 0.f, outScale = 1.f, fb = 0.f, ampCoef = 0.1f, coefRate = 0.f;
	uint32_t inc[4] = {};
	float lvl[4] = {}, own[4] = {}, fineUsed[4] = {}, fineMul[4] = {1.f, 1.f, 1.f, 1.f};
	const FmxAlgorithm* alg = nullptr;
	FmxDecimator dec;
	FmxDecimator opDec[4];
};

} // namespace codex
