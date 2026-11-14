#include "dsp/ContourCore.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

static const double TWO_PI = 6.283185307179586;
static inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

void ContourCore::reset() {
	first = true;
	inCoincidence = false;
	heldCount = 0;
	nLevels = 0;
	for (int c = 0; c < 16; c++)
		velHeld[c] = 0.f;
	applySeed();
}

void ContourCore::applySeed() {
	seed = pendingSeed;
	Rng base((uint64_t)seed);
	Rng d = base.fork("delta");
	Rng r = base.fork("ratios");
	for (int j = 0; j < 8; j++) {
		delta[j] = d.bipolar();
		// seeded ratio set: 1..8, rounded to quarters so patterns still relate
		seededRatio[j] = std::round(r.range(1.f, 8.f) * 4.f) / 4.f;
	}
	ratioSetUsed = -1; // rebuild the ratios
}

float ContourCore::harm(const ContourParams& in, double phi) {
	double sum = 0.0, norm = 0.0;
	for (int k = 0; k < 4; k++) {
		double a = in.amp[k];
		if (a <= 0.0)
			continue;
		double th = (double)in.theta[k] + (double)in.morph * (double)(k + 1) * 0.25;
		sum += a * std::sin(TWO_PI * ((double)in.ratio[k] * phi + th));
		norm += a;
	}
	return norm > 0.0 ? (float)(sum / norm) : 0.f;
}

float ContourCore::slopeNorm(const ContourParams& in) const {
	if (in.mode == ContourParams::HARM) {
		float s = 0.f, n = 0.f;
		for (int k = 0; k < 4; k++) {
			s += in.amp[k] * in.ratio[k];
			n += in.amp[k];
		}
		return n > 0.f ? (float)TWO_PI * s / n : (float)TWO_PI;
	}
	if (in.mode == ContourParams::MOIRE) {
		float s = 0.f;
		for (int j = 0; j < 8; j++)
			s += moireRatio[j];
		return (float)TWO_PI * s / 8.f;
	}
	return (float)TWO_PI; // EXT: as steep as one sine cycle per bar
}

float ContourCore::levelAt(int i, int n, float spreadT) {
	// even spacing, centred: one level sits at 0, two at -0.5 and +0.5, ...
	float u = ((float)i + 0.5f) / (float)n * 2.f - 1.f;
	// exponent > 1 pulls levels toward 0, < 1 pushes them toward the extremes
	float e = std::pow(4.f, -clampf(spreadT, -1.f, 1.f));
	float v = std::pow(std::fabs(u), e);
	return u < 0.f ? -v : v;
}

void ContourCore::buildLevels(int n, float spreadT, float c) {
	nLevels = n;
	levelSpread = spreadT;
	for (int i = 0; i < n; i++) {
		level[i] = levelAt(i, n, spreadT);
		above[i] = c >= level[i]; // start on the right side without firing
	}
}

void ContourCore::buildRatios(const ContourParams& in) {
	ratioSetUsed = in.ratioSet;
	spreadUsed = in.spread;
	static const float primes[8] = {2, 3, 5, 7, 11, 13, 17, 19};
	for (int j = 0; j < 8; j++) {
		float r;
		switch (in.ratioSet) {
			case ContourParams::R_GOLDEN: {
				double g = std::pow(1.6180339887498949, (double)(j + 1));
				r = (float)(g - std::floor(g)) + 1.f;
				break;
			}
			case ContourParams::R_PRIMES: r = primes[j] * 0.5f; break;
			case ContourParams::R_SEEDED: r = seededRatio[j]; break;
			default: r = (float)(j + 1); break;
		}
		moireRatio[j] = r * (1.f + in.spread * delta[j]);
	}
}

void ContourCore::process(const ContourParams& in, double phi, ContourOut& out) {
	out.coincide = false;
	out.crossed = 0;
	int channels = std::min(std::max(in.channels, 1), 16);
	for (int c = 0; c < 16; c++)
		out.trig[c] = false;

	double dphi = 0.0;
	if (!first) {
		dphi = phi - phiPrev;
		// a new seed waits for the next bar
		if (pendingSeed != seed && std::floor(phi) != std::floor(phiPrev))
			applySeed();
	}
	if (in.ratioSet != ratioSetUsed || in.spread != spreadUsed)
		buildRatios(in);

	// ---- the curve ----
	float c = 0.f;
	if (first) {
		for (int j = 0; j < 8; j++) {
			double t = phi * (double)moireRatio[j];
			theta[j] = t - std::floor(t);
		}
	}
	else {
		for (int j = 0; j < 8; j++) {
			double t = theta[j] + dphi * (double)moireRatio[j];
			theta[j] = t - std::floor(t);
		}
	}
	if (in.mode == ContourParams::HARM) {
		c = harm(in, phi);
	}
	else if (in.mode == ContourParams::MOIRE) {
		double s = 0.0;
		for (int j = 0; j < 8; j++)
			s += std::sin(TWO_PI * theta[j]);
		c = (float)(s / 8.0);
	}
	else {
		c = clampf(in.ext, -1.f, 1.f);
	}

	// ---- MOIRE coincidence: enough phasors near the start of their cycle together ----
	if (in.mode == ContourParams::MOIRE) {
		float win = clampf(in.window, 0.001f, 0.05f);
		int near = 0;
		for (int j = 0; j < 8; j++) {
			out.phasor[j] = (float)theta[j];
			double dist = std::min(theta[j], 1.0 - theta[j]);
			if (dist < (double)win)
				near++;
		}
		bool meet = near >= std::min(std::max(in.coincideN, 2), 8);
		if (meet && !inCoincidence) {
			out.coincide = true;
			heldCount = near;
		}
		else if (meet) {
			heldCount = std::max(heldCount, near); // the count can still grow as more arrive
		}
		inCoincidence = meet;
	}
	else {
		for (int j = 0; j < 8; j++)
			out.phasor[j] = 0.f;
		inCoincidence = false;
	}
	out.count = heldCount;

	// ---- slope ----
	float norm = slopeNorm(in);
	if (!first && std::fabs(dphi) > 1e-12)
		slopeHeld = clampf((float)((double)(c - cPrev) / dphi) / norm, -1.f, 1.f);
	out.slope = first ? 0.f : slopeHeld;
	out.curve = c;

	// ---- thresholds ----
	int n = std::min(std::max(in.nThresh, 1), 16);
	if (first || n != nLevels || in.spreadT != levelSpread) {
		buildLevels(n, in.spreadT, c);
	}
	else {
		float h = clampf(in.hyst, 0.f, 0.1f);
		for (int i = 0; i < n; i++) {
			bool up = false, down = false;
			if (!above[i] && c >= level[i] + h) {
				above[i] = true;
				up = true;
			}
			else if (above[i] && c < level[i] - h) {
				above[i] = false;
				down = true;
			}
			bool fire = (up && in.direction != ContourParams::DOWN) || (down && in.direction != ContourParams::UP);
			if (!fire)
				continue;
			int ch = i % channels;
			out.trig[ch] = true;
			float v;
			if (in.velMode == ContourParams::VEL_HEIGHT)
				v = (level[i] + 1.f) * 0.5f;
			else if (in.velMode == ContourParams::VEL_INDEX)
				v = (float)(i + 1) / (float)n;
			else
				v = std::fabs(slopeHeld);
			velHeld[ch] = clampf(v, 0.f, 1.f);
			if (out.crossed < 16)
				out.crossedLevel[out.crossed++] = level[i];
		}
	}
	for (int ch = 0; ch < 16; ch++)
		out.vel[ch] = velHeld[ch];

	first = false;
	phiPrev = phi;
	cPrev = c;
}

} // namespace codex
