// WARP (spec §4): a master clock whose tempo drifts on a curve and whose bars
// bend internally. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include <climits>
#include "common/Rng.hpp"
#include "common/Curve.hpp"

namespace codex {

struct WarpParams {
	float bpm = 100.f;       // 20..300
	float tempoCv = 0.f;     // volts; +1 V doubles the tempo
	float beats = 4.f;       // bar length in beats, 1..16, may be fractional
	float driftDepth = 0.f;  // 0..0.25 (0..25 %)
	float driftRate = 8.f;   // bars per drift point, 1..64
	float shape = 0.f;       // 0 ease-in, 1 ease-out, 2 S-curve, 3 drunk, 4 drawn; in-between values blend
	float depth = 0.f;       // 0..1
	float ratio[4] = {1.f, 0.5f, 0.75f, 1.25f};
	float offset[4] = {0.f, 0.f, 0.f, 0.f}; // 0..1 cycle
	float follow[4] = {1.f, 1.f, 1.f, 1.f}; // how much each output follows the warp
	bool run = true;
};

struct WarpOut {
	float phase[4] = {}; // 0..10 V
	double unwrapped[4] = {}; // the same positions before wrapping, in cycles
	bool bar = false;    // true on the sample a bar starts
	bool beat = false;   // true on the sample a (warped) beat starts
	float tempoCv = 0.f; // effective tempo as CV (0 V = 120 BPM)
	float bpm = 0.f;     // effective tempo in BPM
	float p = 0.f;       // straight position inside the bar, 0..1
	float warped = 0.f;  // bent position inside the bar, 0..1
	float drift = 0.f;   // current drift curve value, -1..1
};

struct WarpCore {
	static const int DRAWN_MAX = 14; // movable points of the drawn shape (the two ends are fixed)

	double B = 0.0; // straight bar position, in bars
	int seed = 0;
	int pendingSeed = 0;

	void reset();
	// A new seed waits for the next bar (or a reset).
	void setSeed(int s) { pendingSeed = s; }
	// The hand-drawn warp shape: `count` points between (0,0) and (1,1), 1..14.
	// They are sorted and spaced so the shape always rises.
	void setDrawn(const float* xs, const float* ys, int count);
	void process(const WarpParams& in, float sampleTime, WarpOut& out);

	// Position inside the bar after bending. p in 0..1. Always rising, w(0)=0, w(1)=1.
	float warp(float p, float shape) const;
	float warpDepth(float p, float shape, float depth) const { return p + depth * (warp(p, shape) - p); }
	// The drift curve at bar position b, -1..1. Depends only on seed, b and rate.
	float driftAt(double b, float rate);

	// --- state the display reads ---
	Spline drunk;
	Spline drawn;

private:
	Rng base;
	bool first = true;
	int64_t barIndex = 0;
	int64_t beatIndex = 0;
	int64_t driftSeg = INT64_MIN;
	float driftRateUsed = 0.f;
	Spline driftSpline;
	void newBar(int64_t n);
	float shapeAt(int index, float p) const;
};

} // namespace codex
