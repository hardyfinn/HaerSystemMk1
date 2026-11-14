// EAR (spec §13B): listening and groove capture. Onsets, loudness, brightness,
// noisiness and pitch from audio, and a groove curve learned from where the
// onsets fall in the bar. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include "common/Curve.hpp"

namespace codex {

// The analysis of one input. Feed it one sample at a time.
struct EarAnalyser {
	static constexpr int FFT = 512;  // spectrum for onsets, centroid and flatness
	static constexpr int HOP = 128;
	static constexpr int RING = 4096;
	static constexpr int YIN = 2048; // pitch window
	static constexpr int YIN_HOP = 2048; // a new pitch reading every 2048 samples
	static constexpr int HISTORY = 16; // flux values for the adaptive threshold

	bool withPitch = false;
	float sens = 2.f; // 1..4: the threshold is the median of recent flux times this

	// Readings, held between updates. Volts as on the panel.
	float loud = 0.f;     // 0..10 V for -60..0 dBFS (full scale = ±1 at the input)
	float centroid = 0.f; // 0..10 V, log2(f / 20 Hz)
	float noise = 0.f;    // 0..10 V, spectral flatness
	float strength = 0.f; // 0..10 V, held from the last onset
	float pitch = 0.f;    // 1 V/oct, 0 V = C4; held while confidence is under 0.5
	float confidence = 0.f; // 0..10 V

	// After process() returns true: how many samples ago the attack began.
	int onsetAge = 0;

	EarAnalyser();
	void reset(float sampleRate);
	// x is the input already scaled so that ±1 is full level. True on the sample an onset is detected.
	bool process(float x);

	// For the tests.
	float lastFlux = 0.f, lastThreshold = 0.f;

private:
	float sr = 48000.f;
	float ring[RING];
	int64_t n = 0;
	int sinceHop = 0, sinceYin = 0;
	int64_t lastOnset = -1000000;
	float window[FFT];
	float re[FFT / 2 + 1], im[FFT / 2 + 1], work[FFT];
	float twc[FFT / 2 + 1], tws[FFT / 2 + 1];
	float mag[FFT / 2 + 1], prevMag[FFT / 2 + 1];
	float history[HISTORY];
	int histPos = 0, histCount = 0;
	double power = 0.0; // smoothed x^2 for loudness
	float powerCoef = 0.f;
	// pitch
	float yre[2 * YIN], yim[2 * YIN], are[2 * YIN], aim[2 * YIN];
	float ybuf[YIN];
	double cum[YIN + 1];
	float dprime[YIN / 2 + 1];

	bool spectrum();
	void yin();
	float at(int64_t back) const { return ring[(int)((n - 1 - back) & (RING - 1))]; }
};

// Learns a groove: where the onsets fall in the bar against a grid, while LEARN is held.
struct EarGroove {
	static constexpr int MAX_GRID = 32;
	static constexpr int MAX_POINTS = Spline::MAX - 2; // moving points (the ends are fixed)
	static constexpr int PHASE_RING = 1024;

	// The learned curve. points = moving points only, between (0,0) and (1,1).
	int points = 0;
	float px[MAX_POINTS], py[MAX_POINTS];
	Spline curve;
	int version = 0; // goes up every time a new curve is learned
	bool learning = false;
	int heard = 0;   // onsets recorded in this take

	EarGroove();
	// Every sample: the unwrapped phase (in bars). Remembers the recent past.
	void phase(double W);
	// Every sample: the learn gate, and whether an onset (from input A) was detected,
	// with its attack `age` samples ago. bars = 1, 2, 4 or 8; grid = 8..32.
	void process(bool gate, bool onset, int age, int bars, int grid);
	// The straight position p (0..1) through the curve, blended by amount (0..1).
	float apply(float p, float amount) const;
	// The curve cut down to at most max moving points (for WARP's drawn shape, which takes 14).
	int reduced(float* xs, float* ys, int max) const;
	// Replace the curve with saved points.
	void set(const float* xs, const float* ys, int count);
	void clear();

private:
	double ring[PHASE_RING];
	int64_t n = 0;
	double startW = 0.0;
	int gridUsed = 16;
	int barsUsed = 2;
	double sum[MAX_GRID + 1];
	int count[MAX_GRID + 1];
	bool lastGate = false;
	void finish();
};

} // namespace codex
