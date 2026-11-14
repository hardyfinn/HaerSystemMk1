#include "doctest.h"
#include "util.hpp"
#include "dsp/DepthCore.hpp"
#include "common/Rng.hpp"
#include <vector>
#include <cmath>

using namespace codex;
using namespace testutil;

static const float SR = 48000.f;
static const float ZERO[4] = {0.f, 0.f, 0.f, 0.f};

static float rmsOf(const std::vector<float>& x) {
	double s = 0.0;
	for (float v : x)
		s += (double)v * v;
	return (float)std::sqrt(s / (double)std::max<size_t>(1, x.size()));
}

TEST_CASE("DEPTH: the tail's measured decay time matches DECAY (INSTABILITY 0, SIZE 1)") {
	for (float decay : {0.5f, 2.f, 6.f}) {
		DepthCore core;
		core.setSampleRate(SR);
		DepthParams p;
		p.decay = decay;
		p.damp = 0.f;
		p.mod = 0.f;
		p.mix = 1.f;
		std::vector<float> ir;
		DepthOut out;
		int n = (int)(SR * decay * 1.3f) + 12000;
		for (int i = 0; i < n; i++) {
			float in[4] = {i == 0 ? 5.f : 0.f, 0.f, 0.f, 0.f};
			core.process(p, in, in, out);
			ir.push_back(out.wetL);
		}
		float measured = t60(ir, SR);
		MESSAGE("DECAY " << decay << " s measured " << measured << " s");
		CHECK(measured == doctest::Approx(decay).epsilon(0.15));
	}
}

TEST_CASE("DEPTH: at full INSTABILITY, 60 s of white noise never passes 12 V") {
	DepthCore core;
	core.setSampleRate(SR);
	DepthParams p;
	p.instability = 1.f;
	p.decay = 30.f;
	p.damp = 0.f;
	p.mod = 1.f;
	p.mix = 0.5f;
	Rng rng(1);
	DepthOut out;
	float peak = 0.f;
	bool finite = true;
	double late = 0.0;
	for (int i = 0; i < 60 * 48000; i++) {
		float l[4], r[4];
		for (int c = 0; c < 4; c++) {
			l[c] = 5.f * rng.bipolar();
			r[c] = 5.f * rng.bipolar();
		}
		core.process(p, l, r, out);
		finite = finite && std::isfinite(out.outL) && std::isfinite(out.wetR);
		peak = std::fmax(peak, std::fmax(std::fmax(std::fabs(out.outL), std::fabs(out.outR)), std::fmax(std::fabs(out.wetL), std::fabs(out.wetR))));
		if (i > 59 * 48000)
			late += (double)out.wetL * out.wetL;
	}
	CHECK(finite);
	CHECK(peak < 12.f);
	CHECK(std::sqrt(late / 48000.0) > 1.f); // and it has not collapsed either
}

TEST_CASE("DEPTH: with INSTABILITY up, a tail with no input swells but stays bounded") {
	DepthCore core;
	core.setSampleRate(SR);
	DepthParams p;
	p.instability = 1.f;
	p.decay = 30.f;
	p.damp = 0.f;
	p.mix = 1.f;
	DepthOut out;
	std::vector<float> early, late;
	Rng rng(2);
	float peak = 0.f;
	for (int i = 0; i < 40 * 48000; i++) {
		float x = i < 24000 ? rng.bipolar() : 0.f;
		float in[4] = {x, x, x, x};
		core.process(p, in, in, out);
		peak = std::fmax(peak, std::fabs(out.wetL));
		if (i >= 48000 && i < 96000)
			early.push_back(out.wetL);
		if (i >= 39 * 48000)
			late.push_back(out.wetL);
	}
	CHECK(rmsOf(late) > rmsOf(early) * 1.5f);
	CHECK(peak < 12.f);
}

TEST_CASE("DEPTH: FREEZE holds the level for 10 s") {
	for (float mod : {0.f, 0.5f}) {
		DepthCore core;
		core.setSampleRate(SR);
		DepthParams p;
		p.decay = 1.5f;
		p.damp = 0.6f;
		p.mod = mod;
		p.mix = 1.f;
		Rng rng(7);
		DepthOut out;
		for (int i = 0; i < 48000; i++) {
			float x = 3.f * rng.bipolar();
			float in[4] = {x, x, 0.f, 0.f};
			core.process(p, in, in, out);
		}
		p.freeze = true;
		std::vector<float> a, b;
		for (int i = 0; i < 11 * 48000; i++) {
			float x = 3.f * rng.bipolar(); // input carries on, but must not get into the frozen tail
			float in[4] = {x, x, 0.f, 0.f};
			p.connected[0] = p.connected[1] = false; // unplug, so only the tail is heard
			core.process(p, in, in, out);
			if (i >= 24000 && i < 48000)
				a.push_back(out.wetL);
			if (i >= 10 * 48000 + 24000)
				b.push_back(out.wetL);
		}
		float db = 20.f * std::log10(rmsOf(b) / rmsOf(a));
		MESSAGE("MOD " << mod << ": level change over 10 s of freeze " << db << " dB, level " << rmsOf(a) << " V");
		CHECK(rmsOf(a) > 0.05f);
		CHECK(std::fabs(db) < 3.f);
	}
}

TEST_CASE("DEPTH: DIST sets pre-delay, direct level and brightness") {
	auto direct = [](float dist, int& firstSample, float& peak, float& bright) {
		DepthCore core;
		core.setSampleRate(SR);
		DepthParams p;
		p.dist[0] = dist;
		p.mix = 0.f; // direct sound only
		DepthOut out;
		firstSample = -1;
		peak = 0.f;
		std::vector<float> x;
		for (int i = 0; i < 9600; i++) {
			// a burst of 6 kHz (8 samples a cycle, so the samples land on the peaks)
			float s = i < 960 ? 5.f * std::sin(2.f * (float)M_PI * 6000.f * (float)i / SR) : 0.f;
			float in[4] = {s, 0.f, 0.f, 0.f};
			core.process(p, in, in, out);
			if (firstSample < 0 && std::fabs(out.outL) > 0.01f)
				firstSample = i;
			peak = std::fmax(peak, std::fabs(out.outL));
			x.push_back(out.outL);
		}
		bright = goertzel(x, 6000.f, SR);
	};
	int nearAt, farAt;
	float nearPeak, farPeak, nearBright, farBright;
	direct(0.f, nearAt, nearPeak, nearBright);
	direct(1.f, farAt, farPeak, farBright);
	CHECK(nearAt == doctest::Approx(0.002f * SR).epsilon(0.05));
	CHECK(farAt == doctest::Approx(0.062f * SR).epsilon(0.02));
	CHECK(nearPeak == doctest::Approx(5.f).epsilon(0.05));
	CHECK(farPeak < 1.f * 0.5f);                 // 1/5 the level, and duller on top of that
	CHECK(farBright < nearBright * 0.2f * 0.5f);
}

TEST_CASE("DEPTH: far sources send more to the tail and get more early reflections") {
	auto wet = [](float dist) {
		DepthCore core;
		core.setSampleRate(SR);
		DepthParams p;
		p.dist[0] = dist;
		DepthOut out;
		Rng rng(3);
		std::vector<float> w;
		for (int i = 0; i < 96000; i++) {
			float s = 3.f * rng.bipolar();
			float in[4] = {s, 0.f, 0.f, 0.f};
			core.process(p, in, in, out);
			if (i > 48000)
				w.push_back(out.wetL);
		}
		return rmsOf(w);
	};
	float nearWet = wet(0.05f), farWet = wet(0.95f);
	MESSAGE("wet level for 3 V noise: near " << nearWet << " V, far " << farWet << " V");
	CHECK(farWet > nearWet * 1.5f);
}

TEST_CASE("DEPTH: PAN moves the direct sound; silence in gives silence out") {
	DepthCore core;
	core.setSampleRate(SR);
	DepthParams p;
	p.mix = 0.f;
	p.pan[0] = 1.f;
	DepthOut out;
	float l = 0.f, r = 0.f;
	for (int i = 0; i < 4800; i++) {
		float in[4] = {3.f, 0.f, 0.f, 0.f};
		core.process(p, in, in, out);
		l = std::fmax(l, std::fabs(out.outL));
		r = std::fmax(r, std::fabs(out.outR));
	}
	CHECK(l < 0.01f);
	CHECK(r > 2.f);
	DepthCore quiet;
	quiet.setSampleRate(SR);
	DepthParams q;
	for (int i = 0; i < 4800; i++) {
		quiet.process(q, ZERO, ZERO, out);
		CHECK(out.outL == 0.f);
		CHECK(out.wetR == 0.f);
	}
}

TEST_CASE("DEPTH: random settings never give NaN, infinity or more than 12 V") {
	DepthCore core;
	core.setSampleRate(SR);
	DepthParams p;
	Rng rng(9);
	DepthOut out;
	bool finite = true;
	float peak = 0.f;
	for (int i = 0; i < 480000; i++) {
		if ((i & 511) == 0) {
			for (int c = 0; c < 4; c++) {
				p.dist[c] = rng.range(-0.5f, 1.5f);
				p.pan[c] = rng.range(-2.f, 2.f);
				p.connected[c] = rng.chance(0.8f);
			}
			p.size = rng.range(0.f, 3.f);
			p.decay = rng.range(0.f, 40.f);
			p.damp = rng.range(-0.5f, 1.5f);
			p.mod = rng.range(-0.5f, 1.5f);
			p.instability = rng.range(-0.5f, 1.5f);
			p.mix = rng.range(-0.5f, 1.5f);
			p.freeze = rng.chance(0.2f);
		}
		float l[4], r[4];
		for (int c = 0; c < 4; c++) {
			l[c] = 10.f * rng.bipolar();
			r[c] = 10.f * rng.bipolar();
		}
		core.process(p, l, r, out);
		finite = finite && std::isfinite(out.outL) && std::isfinite(out.outR) && std::isfinite(out.wetL) && std::isfinite(out.wetR);
		peak = std::fmax(peak, std::fmax(std::fabs(out.outL), std::fabs(out.wetR)));
	}
	CHECK(finite);
	CHECK(peak <= 12.f);
}
