// STRIKE (spec §8): modal percussion. One StrikeVoice is one struck object:
// an exciter feeding 32 tuned resonators, or (formant mode) a voice-like
// pulse train feeding 5 vowel filters. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include "common/Rng.hpp"

namespace codex {

// Settings shared by all voices.
struct StrikeGlobal {
	float exciter = 1.f;    // 0 impulse, 1 mallet, 2 noise burst, 3 external audio; in between = a blend
	float hardness = 0.7f;  // 0 soft .. 1 hard
	float inharm = 0.f;     // -1..1, stretches the mode ratios (formant mode: size)
	int burst = 1;          // 1..16 exciter firings per trigger
	float burstRate = 40.f; // Hz
	bool formant = false;
	bool chokeOnRetrig = false;
	bool extGated = true;   // false: external audio feeds the resonators all the time
	int solo = -1;          // test mode: only this mode sounds, at full level
};

// Settings and signals for one voice, read every sample.
struct StrikeVoiceIn {
	float freq = 220.f;    // Hz
	float material = 2.f;  // 0 wood, 1 rubber, 2 metal, 3 glass, 4 membrane (formant mode: a e i o u)
	float decay = 1.f;     // seconds
	float damp = 0.25f;    // 0..1
	float position = 0.3f; // 0..1
	float bright = 0.5f;   // 0..1
	float vel = 1.f;       // 0..1
	float ext = 0.f;       // external exciter, -1..1 for ±5 V
	bool trig = false;     // true for one sample at each hit
	bool gate = false;     // formant mode: sound while true
	bool choke = false;    // true for one sample to silence the voice
};

struct StrikeTables {
	static const int MODES = 32;
	static const int MATERIALS = 5;
	// natural log of mode k's frequency ratio for each material
	static const float* logRatio(int material);
	static float decayScale(int material);  // how long the material rings compared to metal
	static float decayAlpha(int material);  // how much faster high modes die
	static float brightness(int material);  // spectral tilt
	// vowels a e i o u: 5 formant frequencies (Hz), bandwidths (Hz) and levels (dB)
	static const float* vowelFreq(int vowel);
	static const float* vowelBw(int vowel);
	static const float* vowelDb(int vowel);
};

struct StrikeVoice {
	static const int MODES = StrikeTables::MODES;

	StrikeVoice() { reset(); }
	void reset();
	void setSampleRate(float sr);
	// One sample out, in volts, always inside ±10 V.
	float process(const StrikeGlobal& g, const StrikeVoiceIn& in);
	bool isSounding() const { return bank[0].active || bank[1].active; }

	// Keeps the signal inside ±10 V: untouched below 6 V, rounded off above.
	static float softClip(float v);

private:
	struct Bank {
		float re[MODES], im[MODES];
		bool active = false;
		int chokeLeft = 0;
	};
	Bank bank[2];
	int cur = 0;
	// resonator settings, shared by both banks
	float cr[MODES], ci[MODES], amp[MODES];
	int nModes = 0;
	float sampleRate = 48000.f;
	int controlCount = 0;
	// what the settings above were worked out from
	float kFreq = -1.f, kMaterial = -1.f, kDecay = -1.f, kDamp = -1.f, kPos = -1.f, kBright = -1.f, kInharm = -9.f;
	int kSolo = -2;
	bool kFormant = false;
	// exciter
	bool exActive = false;
	int exT = 0;
	int burstLeft = 0;
	float burstTimer = 0.f;
	float velHeld = 1.f;
	float noiseLp = 0.f;
	Rng noise;
	// formant mode
	float glotPhase = 0.f, glotPrev = 0.f, gateEnv = 0.f;

	void fire(float vel);
	void updateCoeffs(const StrikeGlobal& g, const StrikeVoiceIn& in);
	void clearBank(Bank& b);
	void setModeCount(int n);
};

} // namespace codex
