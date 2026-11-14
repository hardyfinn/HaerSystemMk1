#include "doctest.h"
#include "util.hpp"
#include "dsp/FmxCore.hpp"
#include "common/Rng.hpp"
#include <vector>
#include <cmath>
#include <chrono>
#include <initializer_list>

using namespace codex;
using namespace testutil;

static const float SR = 48000.f;

static std::vector<float> drone(const FmxGlobal& g, FmxVoiceIn in, int n, int skip = 2000) {
	FmxVoice v;
	v.setSampleRate(SR);
	in.drone = true;
	std::vector<float> out;
	for (int i = 0; i < n + skip; i++) {
		float o = v.process(g, in);
		if (i >= skip)
			out.push_back(o);
	}
	return out;
}

TEST_CASE("FMX: algorithm 8, all ratios 1, index 0 is a sine at the played pitch") {
	FmxGlobal g;
	for (int i = 0; i < 4; i++) {
		g.ratio[i] = 1.f;
		g.level[i] = 1.f;
		g.env[i] = 0.f;
	}
	FmxVoiceIn in;
	in.algorithm = 8;
	in.index = 0.f;
	for (float f : {110.f, 262.f, 1000.f}) {
		in.freq = f;
		auto x = drone(g, in, 48000);
		float fund = goertzel(x, f, SR);
		CHECK(fund == doctest::Approx(10.f).epsilon(0.02)); // four carriers in step: full level
		for (int h = 2; h <= 6; h++)
			CHECK(goertzel(x, f * h, SR) < fund * 0.001f);
		CHECK(std::abs(zeroCrossings(x) - (int)std::lround(f)) <= 1);
	}
}

TEST_CASE("FMX: algorithm 1 puts energy at carrier plus and minus n times the modulator") {
	FmxGlobal g;
	g.ratio[0] = 1.f; g.ratio[1] = 3.f; g.ratio[2] = 1.f; g.ratio[3] = 1.f;
	g.level[0] = 1.f; g.level[1] = 1.f; g.level[2] = 0.f; g.level[3] = 0.f;
	for (int i = 0; i < 4; i++)
		g.env[i] = 0.f;
	FmxVoiceIn in;
	in.algorithm = 1;
	in.freq = 500.f;
	in.index = 0.f;
	auto clean = drone(g, in, 48000);
	CHECK(goertzel(clean, 2000.f, SR) < 0.01f);
	in.index = 2.f;
	auto x = drone(g, in, 48000);
	// carrier 500 Hz, modulator 1500 Hz: sidebands at 500 ± 1500 n, i.e. 2000, 1000 (folded), 3500, 2500 ...
	for (float f : {500.f, 2000.f, 1000.f, 3500.f, 2500.f})
		CHECK(goertzel(x, f, SR) > 0.2f);
	// and nothing in between
	for (float f : {750.f, 1500.f, 3000.f, 1250.f})
		CHECK(goertzel(x, f, SR) < 0.02f);
	// a deeper index moves energy outwards
	in.index = 6.f;
	auto y = drone(g, in, 48000);
	CHECK(goertzel(y, 5000.f, SR) + goertzel(y, 6500.f, SR) > goertzel(x, 5000.f, SR) + goertzel(x, 6500.f, SR));
}

TEST_CASE("FMX: every algorithm sounds and stays inside 10 V") {
	FmxGlobal g;
	for (int i = 0; i < 4; i++)
		g.level[i] = 1.f;
	for (int a = 1; a <= 8; a++) {
		FmxVoiceIn in;
		in.algorithm = a;
		in.index = 10.f;
		in.feedback = 1.f;
		auto x = drone(g, in, 24000);
		CHECK(allFinite(x));
		CHECK(maxAbs(x) > 1.f);
		CHECK(maxAbs(x) <= 10.f);
	}
	// algorithms really differ: 5 (4 feeds both 3 and 2) is not 6 (4 feeds 3 only)
	FmxVoiceIn in;
	in.index = 3.f;
	in.algorithm = 5;
	auto a5 = drone(g, in, 4800);
	in.algorithm = 6;
	auto a6 = drone(g, in, 4800);
	float diff = 0.f;
	for (size_t i = 0; i < a5.size(); i++)
		diff = std::fmax(diff, std::fabs(a5[i] - a6[i]));
	CHECK(diff > 0.5f);
}

TEST_CASE("FMX: full feedback stays bounded") {
	FmxGlobal g;
	FmxVoiceIn in;
	in.algorithm = 1;
	in.feedback = 1.f;
	in.index = 10.f;
	for (float f : {30.f, 440.f, 8000.f}) {
		in.freq = f;
		auto x = drone(g, in, 96000);
		CHECK(allFinite(x));
		CHECK(maxAbs(x) <= 10.f);
	}
}

TEST_CASE("FMX: a trigger starts the envelope and TIME sets how long it lasts") {
	FmxGlobal g;
	g.time = 0.1f;
	g.ampShape = 1.f;
	FmxVoice v;
	v.setSampleRate(SR);
	FmxVoiceIn in;
	std::vector<float> x;
	for (int i = 0; i < 9600; i++) {
		in.trig = i == 0;
		x.push_back(v.process(g, in));
	}
	float early = 0.f, mid = 0.f, late = 0.f;
	for (int i = 0; i < 480; i++) early = std::fmax(early, std::fabs(x[(size_t)i]));
	for (int i = 2160; i < 2640; i++) mid = std::fmax(mid, std::fabs(x[(size_t)i]));
	for (int i = 5000; i < 9600; i++) late = std::fmax(late, std::fabs(x[(size_t)i]));
	CHECK(early > 4.f);
	CHECK(mid == doctest::Approx(early * 0.5f).epsilon(0.15)); // half-way through a straight-line fade
	CHECK(late < 1e-4f);
	CHECK_FALSE(v.isSounding());
}

TEST_CASE("FMX: with PHASE patched the envelope follows the phase exactly") {
	FmxGlobal g;
	g.ampShape = 1.f;
	FmxVoice v;
	v.setSampleRate(SR);
	FmxVoiceIn in;
	in.phaseDriven = true;
	in.algorithm = 8;
	// a 2 Hz ramp: loud at the start of each cycle, silent at the end
	float startPeak = 0.f, endPeak = 0.f;
	for (int i = 0; i < 48000; i++) {
		in.phase = ramp(2.0, SR, i) / 10.f;
		float o = std::fabs(v.process(g, in));
		float p = in.phase;
		if (i > 24000) {
			if (p > 0.01f && p < 0.1f) startPeak = std::fmax(startPeak, o);
			if (p > 0.95f) endPeak = std::fmax(endPeak, o);
		}
		if (p > 0.3f && p < 0.31f)
			CHECK(v.envPosition() == doctest::Approx(p));
	}
	CHECK(startPeak > 4.f);
	CHECK(endPeak < startPeak * 0.08f);
	// a ramp running backwards plays the envelope backwards: quiet first, loud last
	FmxVoice v2;
	v2.setSampleRate(SR);
	float firstHalf = 0.f, secondHalf = 0.f;
	for (int i = 0; i < 24000; i++) {
		in.phase = 1.f - ramp(2.0, SR, i) / 10.f;
		float o = std::fabs(v2.process(g, in));
		if (in.phase > 0.6f) firstHalf = std::fmax(firstHalf, o);
		if (in.phase < 0.4f) secondHalf = std::fmax(secondHalf, o);
	}
	CHECK(secondHalf > firstHalf);
}

TEST_CASE("FMX: the index envelope makes the start of a note brighter than the end") {
	FmxGlobal g;
	g.time = 1.f;
	g.ampShape = 0.2f;
	g.indexShape = 3.f;
	g.ratio[1] = 1.f;
	g.level[1] = 1.f;
	g.env[1] = 1.f;
	g.level[2] = 0.f;
	FmxVoice v;
	v.setSampleRate(SR);
	FmxVoiceIn in;
	in.freq = 200.f;
	in.index = 5.f;
	std::vector<float> a, b;
	for (int i = 0; i < 40000; i++) {
		in.trig = i == 0;
		float o = v.process(g, in);
		if (i < 4800) a.push_back(o);
		if (i >= 30000 && i < 34800) b.push_back(o);
	}
	auto highShare = [](const std::vector<float>& x) {
		float hi = 0.f;
		for (int h = 3; h <= 8; h++)
			hi += goertzel(x, 200.f * h, SR);
		return hi / (goertzel(x, 200.f, SR) + 1e-6f);
	};
	CHECK(highShare(a) > 3.f * highShare(b));
}

TEST_CASE("FMX: retriggering at 2 kHz stays bounded, with and without phase reset, and is fast enough") {
	for (int resetPhases = 0; resetPhases < 2; resetPhases++) {
		FmxGlobal g;
		g.resetPhases = resetPhases == 1;
		g.time = 0.002f;
		FmxVoice voices[8];
		for (FmxVoice& v : voices)
			v.setSampleRate(SR);
		FmxVoiceIn in;
		in.index = 8.f;
		in.feedback = 0.7f;
		bool finite = true;
		float peak = 0.f;
		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < 48000; i++) {
			in.trig = i % 24 == 0; // 2 kHz
			for (int c = 0; c < 8; c++) {
				in.freq = 100.f * (float)(c + 1);
				float o = voices[c].process(g, in);
				finite = finite && std::isfinite(o);
				peak = std::fmax(peak, std::fabs(o));
			}
		}
		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		CHECK(finite);
		CHECK(peak <= 10.f);
		CHECK(peak > 1.f);
		MESSAGE("8 voices for 1 s took " << seconds << " s in this slow, checked test build");
		CHECK(seconds < 10.0); // very loose: the test build is unoptimised and instrumented
	}
}

TEST_CASE("FMX: random settings at audio rate never give NaN or infinity") {
	Rng rng(4);
	FmxVoice v;
	v.setSampleRate(SR);
	FmxGlobal g;
	FmxVoiceIn in;
	bool finite = true;
	float peak = 0.f;
	float ops[4];
	for (int i = 0; i < 240000; i++) {
		in.freq = 10.f * std::pow(2000.f, rng.uniform());
		in.vel = rng.range(-0.5f, 1.5f);
		in.index = rng.range(-5.f, 15.f);
		in.feedback = rng.range(-1.f, 2.f);
		in.algorithm = rng.between(-2, 11);
		for (float& r : in.ratioOffset)
			r = rng.range(-20.f, 20.f);
		in.trig = rng.chance(0.02f);
		in.phaseDriven = (i / 5000) % 3 == 1;
		in.drone = (i / 5000) % 3 == 2;
		in.phase = rng.range(-2.f, 2.f);
		if ((i & 255) == 0) {
			for (int k = 0; k < 4; k++) {
				g.ratio[k] = rng.range(0.f, 20.f);
				g.fine[k] = rng.range(-100.f, 100.f);
				g.level[k] = rng.range(-0.5f, 1.5f);
				g.env[k] = rng.uniform();
			}
			g.ampShape = rng.range(0.f, 10.f);
			g.indexShape = rng.range(0.f, 10.f);
			g.time = rng.range(0.f, 1.f);
			g.resetPhases = rng.chance(0.5f);
		}
		float o = v.process(g, in, ops);
		finite = finite && std::isfinite(o) && std::isfinite(ops[0]) && std::isfinite(ops[3]);
		peak = std::fmax(peak, std::fabs(o));
	}
	CHECK(finite);
	CHECK(peak <= 10.f);
}

TEST_CASE("FMX: three cents of FINE on operator 2 gives slow beating in algorithm 8") {
	FmxGlobal g;
	for (int i = 0; i < 4; i++) {
		g.ratio[i] = 1.f;
		g.env[i] = 0.f;
		g.level[i] = i < 2 ? 1.f : 0.f;
	}
	g.fine[1] = 3.f;
	FmxVoiceIn in;
	in.algorithm = 8;
	in.freq = 440.f;
	// beat rate = 440 * (2^(3/1200) - 1) = 0.76 Hz: loudness rises and falls about every 1.3 s
	auto x = drone(g, in, 96000, 0);
	float loud = 0.f, quiet = 100.f;
	for (int w = 0; w < 96000; w += 2400) {
		float p = 0.f;
		for (int i = w; i < w + 2400; i++)
			p = std::fmax(p, std::fabs(x[(size_t)i]));
		loud = std::fmax(loud, p);
		quiet = std::fmin(quiet, p);
	}
	CHECK(loud > 4.5f);
	CHECK(quiet < loud * 0.25f);
}
