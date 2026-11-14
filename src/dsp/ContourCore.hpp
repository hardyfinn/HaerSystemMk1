// CONTOUR (spec §6): rhythms from a continuous curve crossing levels, plus the
// MOIRE interference source. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include "common/Rng.hpp"

namespace codex {

struct ContourParams {
	enum Mode { HARM = 0, MOIRE = 1, EXT = 2 };
	enum Dir { UP = 0, DOWN = 1, BOTH = 2 };
	enum Vel { VEL_SLOPE = 0, VEL_HEIGHT = 1, VEL_INDEX = 2 };
	enum Ratios { R_HARMONIC = 0, R_GOLDEN = 1, R_PRIMES = 2, R_SEEDED = 3 };

	int mode = HARM;
	// HARM: four sine harmonics
	float amp[4] = {1.f, 0.5f, 0.f, 0.f}; // 0..1
	float ratio[4] = {1.f, 2.f, 3.f, 4.f}; // cycles per bar, 0.25..16
	float theta[4] = {0.f, 0.f, 0.f, 0.f}; // 0..1 cycle
	float morph = 0.f; // shifts harmonic k by morph * k * 0.25 cycle
	// MOIRE: eight phasors
	int ratioSet = R_HARMONIC;
	float spread = 0.f;    // detune, 0..0.1
	int coincideN = 4;     // 2..8 phasors must meet
	float window = 0.01f;  // 0.001..0.05 cycle
	// EXT
	float ext = 0.f; // -1..1
	// thresholds
	int nThresh = 4;      // 1..16
	float spreadT = 0.f;  // -1 levels crowd round the middle, +1 round the extremes
	int direction = BOTH;
	float hyst = 0.02f;   // 0..0.1
	int channels = 1;     // 1..16
	int velMode = VEL_SLOPE;
};

struct ContourOut {
	bool trig[16] = {};   // true on the sample channel c fires
	float vel[16] = {};   // 0..1, held from each channel's last hit
	float curve = 0.f;    // -1..1
	float slope = 0.f;    // -1..1 (normalised)
	bool coincide = false;
	int count = 0;        // phasors in the last coincidence, 0..8
	float phasor[8] = {}; // 0..1 (MOIRE mode)
	// for the display: levels crossed this sample
	int crossed = 0;
	float crossedLevel[16] = {};
};

struct ContourCore {
	int seed = 0;
	int pendingSeed = 0;
	float level[16] = {};
	int nLevels = 0;
	float moireRatio[8] = {1, 2, 3, 4, 5, 6, 7, 8}; // after detune

	void reset();
	void setSeed(int s) { pendingSeed = s; }
	// phi: the unwrapped position, in cycles (bars). May move backwards.
	void process(const ContourParams& in, double phi, ContourOut& out);

	// The HARM curve at position phi, -1..1. Also used to draw the display.
	static float harm(const ContourParams& in, double phi);
	// Greatest possible steepness of the current curve, used to scale SLOPE and velocity.
	float slopeNorm(const ContourParams& in) const;
	// Level i of n, -1..1, after the cluster control.
	static float levelAt(int i, int n, float spreadT);

private:
	bool first = true;
	double phiPrev = 0.0;
	float cPrev = 0.f;
	float slopeHeld = 0.f;
	bool above[16] = {};
	float levelSpread = 99.f;
	double theta[8] = {};
	float delta[8] = {};
	float seededRatio[8] = {};
	int ratioSetUsed = -1;
	float spreadUsed = -1.f;
	bool inCoincidence = false;
	int heldCount = 0;
	float velHeld[16] = {};
	void applySeed();
	void buildLevels(int n, float spreadT, float c);
	void buildRatios(const ContourParams& in);
};

} // namespace codex
