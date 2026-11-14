#include "doctest.h"
#include "dsp/EarCore.hpp"
#include "common/Rng.hpp"
#include <cmath>
#include <vector>

using namespace codex;

TEST_CASE("EAR: a click train at 120 BPM fires ONSET once per click, with under 3 ms of jitter") {
	const float sr = 48000.f;
	EarAnalyser a;
	a.reset(sr);
	const int period = 24000; // 120 BPM
	const int clicks = 20;
	std::vector<int> fired;
	Rng rng(5);
	for (int i = 0; i < period * clicks + 2000; i++) {
		float x = (i % period == 5000) ? 0.9f : 0.f;
		x += 0.001f * rng.bipolar(); // a little noise floor
		if (a.process(x))
			fired.push_back(i);
	}
	REQUIRE(fired.size() == (size_t)clicks);
	float lo = 1e9f, hi = -1e9f;
	for (int k = 0; k < clicks; k++) {
		float lag = (float)(fired[k] - (k * period + 5000)) / sr;
		CHECK(lag >= 0.f);
		lo = std::min(lo, lag);
		hi = std::max(hi, lag);
	}
	CHECK(hi - lo < 0.003f);
	CHECK(hi < 0.015f); // and it is not late either
}

TEST_CASE("EAR: a 220 Hz sine reads 220 Hz within 0.5 % with confidence above 0.9") {
	for (float sr : {44100.f, 48000.f, 96000.f}) {
		EarAnalyser a;
		a.withPitch = true;
		a.reset(sr);
		for (int i = 0; i < (int)(sr * 0.5f); i++)
			a.process(0.5f * (float)std::sin(2.0 * M_PI * 220.0 * i / sr));
		float hz = 261.6256f * std::pow(2.f, a.pitch);
		CHECK(hz == doctest::Approx(220.f).epsilon(0.005));
		CHECK(a.confidence > 9.f);
	}
}

TEST_CASE("EAR: silence and noise give no NaN, and silence no onsets") {
	EarAnalyser a;
	a.withPitch = true;
	a.reset(48000.f);
	int onsets = 0;
	for (int i = 0; i < 48000; i++)
		onsets += a.process(0.f);
	CHECK(onsets == 0);
	Rng rng(9);
	for (int i = 0; i < 48000; i++)
		a.process(rng.bipolar());
	CHECK(std::isfinite(a.pitch));
	CHECK(std::isfinite(a.centroid));
	CHECK(a.noise > 5.f); // white noise is flat
	CHECK(a.loud > 7.f);
}

TEST_CASE("EAR: learning a swung click track reproduces the swing within 2 % of a step") {
	const float sr = 48000.f;
	const int bar = 96000; // 120 BPM, 4 beats
	const int step = bar / 16;
	EarAnalyser a;
	a.reset(sr);
	EarGroove g;
	// every other 16th is late by 30 % of a step
	auto clickAt = [&](int i) {
		int inBar = i % bar;
		int k = inBar / step;
		int pos = k * step + (k % 2 ? (int)(0.3f * step) : 0);
		return inBar == pos;
	};
	const int bars = 2;
	for (int i = 0; i < bar * (bars + 1) + 1000; i++) {
		double W = (double)i / bar;
		g.phase(W);
		bool onset = a.process(clickAt(i) ? 0.9f : 0.f);
		bool gate = i >= 10 && i < bar * bars + 10; // held for two bars
		g.process(gate, onset, a.onsetAge, bars, 16);
	}
	REQUIRE(g.points > 0);
	CHECK(g.heard >= 28);
	for (int k = 1; k < 16; k++) {
		float x = (float)k / 16;
		float want = x + (k % 2 ? 0.3f / 16 : 0.f);
		CHECK(std::fabs(g.apply(x, 1.f) - want) < 0.02f / 16);
	}
	// cut down to WARP's 14 points, the swung positions survive
	float xs[32], ys[32];
	int k = g.reduced(xs, ys, 14);
	CHECK(k == 14);
	int swung = 0;
	for (int i = 0; i < k; i++)
		if (ys[i] - xs[i] > 0.25f / 16)
			swung++;
	CHECK(swung >= 7);
	// GROOVE_AMT 0 is straight
	CHECK(g.apply(0.3f, 0.f) == doctest::Approx(0.3f));
	// and the curve always rises
	float last = -1.f;
	for (int i = 0; i <= 1000; i++) {
		float y = g.apply(i / 1000.f, 1.f);
		CHECK(y >= last);
		last = y;
	}
}
