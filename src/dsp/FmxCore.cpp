#include "dsp/FmxCore.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

namespace {

const int SINE_N = 2048;

struct SineTable {
	float v[SINE_N + 1];
	SineTable() {
		for (int i = 0; i <= SINE_N; i++)
			v[i] = (float)std::sin(2.0 * M_PI * (double)i / SINE_N);
	}
};

// The sine of a phase held as a 32-bit number, where the full range is one
// cycle (so wrapping round is free). Table of 2048 points, straight lines between.
inline float tableSin(uint32_t p) {
	static const SineTable t;
	uint32_t i = p >> 21;
	float frac = (float)(p & 0x1FFFFFu) * (1.f / 2097152.f);
	return t.v[i] + (t.v[i + 1] - t.v[i]) * frac;
}

// Cycles (any size, either sign) to the 32-bit phase format.
inline uint32_t toPhase(float cycles) { return (uint32_t)(int64_t)(cycles * 4294967296.f); }

struct HalfBand {
	float h[31];
	HalfBand() {
		// windowed-sinc low-pass at a quarter of the doubled rate (Blackman window)
		double sum = 0.0;
		for (int i = 0; i < 31; i++) {
			int n = i - 15;
			double s = n == 0 ? 0.5 : std::sin(M_PI * n * 0.5) / (M_PI * n);
			double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / 30.0) + 0.08 * std::cos(4.0 * M_PI * i / 30.0);
			h[i] = (float)(s * w);
			sum += s * w;
		}
		for (int i = 0; i < 31; i++)
			h[i] = (float)(h[i] / sum);
	}
};

const HalfBand& halfBand() {
	static const HalfBand hb;
	return hb;
}

// Operators are numbered 1..4; bit 0 = operator 1.
const FmxAlgorithm ALGOS[8] = {
	// 1: 4 > 3 > 2 > 1
	{{0b0010, 0b0100, 0b1000, 0}, 0b0001},
	// 2: (4 + 3) > 2 > 1
	{{0b0010, 0b1100, 0, 0}, 0b0001},
	// 3: 4 > 3 and 2 > 1; 1 and 3 are heard
	{{0b0010, 0, 0b1000, 0}, 0b0101},
	// 4: 4 > 3, 4 > 2, 4 > 1; 1, 2 and 3 are heard
	{{0b1000, 0b1000, 0b1000, 0}, 0b0111},
	// 5: 4 > 3 and 4 > 2, then 3 + 2 > 1
	{{0b0110, 0b1000, 0b1000, 0}, 0b0001},
	// 6: 4 > 3 > 1 and 2 > 1
	{{0b0110, 0, 0b1000, 0}, 0b0001},
	// 7: 4 > 1; 1, 2 and 3 are heard
	{{0b1000, 0, 0, 0}, 0b0111},
	// 8: all four are heard, nothing modulates
	{{0, 0, 0, 0}, 0b1111},
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

const FmxAlgorithm& fmxAlgorithm(int number) { return ALGOS[std::clamp(number, 1, 8) - 1]; }

void FmxDecimator::reset() {
	for (float& b : buf)
		b = 0.f;
	pos = 0;
}

float FmxDecimator::process(float a, float b) {
	const float* h = halfBand().h;
	buf[pos] = a;
	buf[(pos + 1) & 31] = b;
	pos = (pos + 2) & 31;
	// newest sample is at pos - 1. Every second tap of a half-band filter is
	// zero, apart from the middle one.
	float acc = h[15] * buf[(pos - 16) & 31];
	for (int i = 0; i < 31; i += 2)
		acc += h[i] * buf[(pos - 1 - i) & 31];
	return acc;
}

float FmxVoice::shape(float e, float k) {
	float b = 1.f - clampf(e, 0.f, 1.f);
	if (b <= 0.f)
		return 0.f;
	return std::pow(b, k);
}

void FmxVoice::reset() {
	for (int i = 0; i < 4; i++) {
		phase[i] = 0u;
		opDec[i].reset();
	}
	fbHist[0] = fbHist[1] = 0.f;
	e = 1.f;
	lastPhase = 0.f;
	ampSmooth = 0.f;
	velHeld = 1.f;
	idle = true;
	dec.reset();
	controlCount = 0;
	ampTarget = 0.f;
	alg = &fmxAlgorithm(1);
	for (int i = 0; i < 4; i++) {
		inc[i] = 0u;
		lvl[i] = own[i] = 0.f;
		fineUsed[i] = 0.f;
		fineMul[i] = 1.f;
	}
	coefRate = 0.f;
}

float FmxVoice::process(const FmxGlobal& g, const FmxVoiceIn& in, float* ops) {
	// ---- where is the envelope? ----
	bool hit = in.trig;
	if (in.phaseDriven) {
		float p = in.phase - std::floor(in.phase);
		if (p < lastPhase - 0.5f)
			hit = true; // the ramp wrapped round: a new cycle
		lastPhase = p;
		e = p;
	}
	else if (in.drone) {
		e = 0.f;
	}
	else {
		if (in.trig)
			e = 0.f;
		else if (e < 1.f)
			e = std::min(1.f, e + 1.f / (clampf(g.time, 0.0005f, 20.f) * sampleRate));
	}
	if (hit || in.drone)
		velHeld = clampf(in.vel, 0.f, 1.f);
	if (hit && g.resetPhases) {
		for (int i = 0; i < 4; i++)
			phase[i] = 0u;
		fbHist[0] = fbHist[1] = 0.f;
	}

	// The slower sums are done every fourth sample, and at once on a hit.
	if (controlCount == 0 || hit) {
		ampTarget = shape(e, clampf(g.ampShape, 0.2f, 8.f)) * velHeld;
		float idxEnv = shape(e, clampf(g.indexShape, 0.2f, 8.f));
		alg = &fmxAlgorithm(in.algorithm);
		const float nyq = 0.45f * sampleRate; // operators above this are stopped
		const float twoPiInv = 1.f / 6.2831853f;
		int nCarriers = 0;
		for (int i = 0; i < 4; i++) {
			if (g.fine[i] != fineUsed[i]) {
				fineUsed[i] = g.fine[i];
				fineMul[i] = std::exp2(g.fine[i] / 1200.f);
			}
			float r = g.ratio[i] + (i > 0 ? in.ratioOffset[i - 1] : 0.f);
			r = clampf(r, 0.05f, 32.f);
			float f = in.freq * r * fineMul[i];
			inc[i] = f < nyq ? toPhase(f / (sampleRate * 2.f)) : 0u;
			float l = clampf(g.level[i], 0.f, 1.f);
			float en = clampf(g.env[i], 0.f, 1.f);
			bool carrier = (alg->carriers >> i) & 1;
			// a carrier's level is its loudness; a modulator's level is how hard it bends the others
			float envd = l * (1.f - en + en * idxEnv);
			lvl[i] = carrier ? envd : envd * clampf(in.index, 0.f, 10.f) * twoPiInv;
			own[i] = envd;
			if (inc[i] == 0u)
				lvl[i] = own[i] = 0.f;
			nCarriers += carrier ? 1 : 0;
		}
		outScale = 1.f / std::sqrt((float)std::max(nCarriers, 1));
		fb = clampf(in.feedback, 0.f, 1.f) * 2.f * twoPiInv; // up to 2 radians of self-modulation
		if (sampleRate != coefRate) {
			coefRate = sampleRate;
			// a fifth of a millisecond of smoothing takes the click off the instant attack
			ampCoef = 1.f - std::exp(-1.f / (0.0002f * sampleRate * 2.f));
		}
	}
	controlCount = (controlCount + 1) & 3;

	if (ampTarget <= 0.f && ampSmooth < 1e-5f) {
		// silent: keep the filters moving so the tail ends cleanly, then rest
		ampSmooth = 0.f;
		if (!idle) {
			float o = dec.process(0.f, 0.f);
			if (ops)
				for (int i = 0; i < 4; i++)
					ops[i] = opDec[i].process(0.f, 0.f) * 5.f;
			if (std::fabs(o) < 1e-6f)
				idle = true;
			return o * 5.f;
		}
		if (ops)
			for (int i = 0; i < 4; i++)
				ops[i] = 0.f;
		return 0.f;
	}
	idle = false;
	const FmxAlgorithm& algo = *alg;

	float y[2];
	float opOut[2][4];
	for (int s = 0; s < 2; s++) {
		ampSmooth += (ampTarget - ampSmooth) * ampCoef;
		float o[4];
		float mix = 0.f;
		// work down from operator 4 so each modulator is ready before the operator it feeds
		for (int i = 3; i >= 0; i--) {
			float pm = 0.f;
			uint8_t m = algo.mods[i];
			for (int j = i + 1; j < 4; j++)
				if ((m >> j) & 1)
					pm += o[j] * lvl[j];
			if (i == 3)
				pm += fb * 0.5f * (fbHist[0] + fbHist[1]);
			o[i] = tableSin(phase[i] + toPhase(pm));
			phase[i] += inc[i];
			if ((algo.carriers >> i) & 1)
				mix += o[i] * lvl[i];
			opOut[s][i] = o[i] * own[i];
		}
		fbHist[1] = fbHist[0];
		fbHist[0] = o[3];
		y[s] = mix * outScale * ampSmooth;
	}
	if (ops)
		for (int i = 0; i < 4; i++)
			ops[i] = opDec[i].process(opOut[0][i], opOut[1][i]) * 5.f;
	float out = dec.process(y[0], y[1]) * 5.f;
	if (!std::isfinite(out)) {
		reset();
		return 0.f;
	}
	return out < -10.f ? -10.f : (out > 10.f ? 10.f : out);
}

} // namespace codex
