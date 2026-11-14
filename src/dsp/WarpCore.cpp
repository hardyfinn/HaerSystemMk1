#include "dsp/WarpCore.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

static inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

void WarpCore::reset() {
	B = 0.0;
	seed = pendingSeed;
	base.seed((uint64_t)seed);
	first = true;
	barIndex = 0;
	beatIndex = 0;
	driftSeg = INT64_MIN;
	newBar(0);
}

void WarpCore::setDrawn(const float* xs, const float* ys, int count) {
	count = std::min(std::max(count, 1), (int)DRAWN_MAX);
	float x[Spline::MAX], y[Spline::MAX];
	x[0] = 0.f;
	y[0] = 0.f;
	for (int i = 0; i < count; i++) {
		x[i + 1] = clampf(xs[i], 0.f, 1.f);
		y[i + 1] = clampf(ys[i], 0.f, 1.f);
	}
	const float gap = 0.002f;
	makeRising(x, y, count + 1, gap);
	// keep room for the fixed end point
	for (int i = count; i >= 1; i--) {
		float top = 1.f - gap * (float)(count + 1 - i);
		x[i] = std::min(x[i], top);
		y[i] = std::min(y[i], top);
	}
	x[count + 1] = 1.f;
	y[count + 1] = 1.f;
	drawn.set(x, y, count + 2);
}

// A new "drunk" shape for bar n: 5 points between the ends, at least 0.04 apart.
void WarpCore::newBar(int64_t n) {
	Rng r = base.fork("drunk", (uint64_t)n);
	const int K = 5;
	const float gap = 0.04f;
	float ux[K], uy[K];
	for (int i = 0; i < K; i++) {
		ux[i] = r.uniform();
		uy[i] = r.uniform();
	}
	std::sort(ux, ux + K);
	std::sort(uy, uy + K);
	float x[K + 2], y[K + 2];
	x[0] = y[0] = 0.f;
	for (int i = 0; i < K; i++) {
		// spreading the sorted values over what is left after the gaps keeps every gap >= 0.04
		x[i + 1] = gap * (float)(i + 1) + ux[i] * (1.f - gap * (K + 1));
		y[i + 1] = gap * (float)(i + 1) + uy[i] * (1.f - gap * (K + 1));
	}
	x[K + 1] = y[K + 1] = 1.f;
	drunk.set(x, y, K + 2);
}

float WarpCore::shapeAt(int index, float p) const {
	switch (index) {
		case 0: return std::pow(p, 2.2f);             // ease-in: starts slow, ends fast
		case 1: return 1.f - std::pow(1.f - p, 2.2f); // ease-out: starts fast, ends slow
		case 2: return scurve(p, 8.f);                // S-curve: slow, fast, slow
		case 3: return drunk.eval(p);
		default: return drawn.eval(p);
	}
}

float WarpCore::warp(float p, float shape) const {
	p = clampf(p, 0.f, 1.f);
	shape = clampf(shape, 0.f, 4.f);
	int i = std::min((int)shape, 3);
	float f = shape - (float)i;
	float a = shapeAt(i, p);
	if (f <= 0.f)
		return a;
	// A blend of two rising curves is still rising.
	return a + f * (shapeAt(i + 1, p) - a);
}

float WarpCore::driftAt(double b, float rate) {
	rate = clampf(rate, 1.f, 64.f);
	double u = b / (double)rate;
	int64_t seg = (int64_t)std::floor(u);
	if (seg != driftSeg || rate != driftRateUsed) {
		driftSeg = seg;
		driftRateUsed = rate;
		// one random value per drift period; a smooth curve through four of them
		float x[4] = {-1.f, 0.f, 1.f, 2.f};
		float y[4];
		for (int k = 0; k < 4; k++)
			y[k] = base.fork("drift", (uint64_t)(seg + k - 1 + 0x40000000)).bipolar();
		driftSpline.set(x, y, 4);
	}
	return driftSpline.eval((float)(u - (double)seg));
}

void WarpCore::process(const WarpParams& in, float sampleTime, WarpOut& out) {
	out.bar = false;
	out.beat = false;
	float beats = clampf(in.beats, 1.f, 16.f);

	// tempo now: knob x CV x drift
	float d = in.driftDepth > 0.f ? driftAt(B, in.driftRate) : 0.f;
	float bpm = clampf(in.bpm * std::pow(2.f, in.tempoCv) * (1.f + in.driftDepth * d), 1.f, 3000.f);

	if (first) {
		first = false;
		out.bar = true;
		out.beat = true;
	}
	else if (in.run) {
		B += (double)bpm / 60.0 / (double)beats * (double)sampleTime;
	}

	int64_t n = (int64_t)std::floor(B);
	float p = (float)(B - (double)n);
	if (n != barIndex) {
		barIndex = n;
		out.bar = true;
		if (pendingSeed != seed) {
			seed = pendingSeed;
			base.seed((uint64_t)seed);
			driftSeg = INT64_MIN;
		}
		newBar(n);
	}

	float wd = warpDepth(p, in.shape, clampf(in.depth, 0.f, 1.f));
	// Beats land on the bent positions, so the beats bend too.
	int64_t beat = (int64_t)std::floor(((double)n + (double)wd) * (double)beats);
	if (beat > beatIndex)
		out.beat = true;
	beatIndex = beat;

	for (int i = 0; i < 4; i++) {
		double w = (double)in.ratio[i] * ((double)n + (double)p + (double)in.follow[i] * ((double)wd - (double)p)) + (double)in.offset[i];
		out.unwrapped[i] = w;
		out.phase[i] = (float)(10.0 * (w - std::floor(w)));
	}
	out.drift = d;
	out.bpm = bpm;
	out.tempoCv = std::log2(bpm / 120.f);
	out.p = p;
	out.warped = wd;
}

} // namespace codex
