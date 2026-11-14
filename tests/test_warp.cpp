#include "doctest.h"
#include "util.hpp"
#include "dsp/WarpCore.hpp"
#include "common/Phase.hpp"
#include <vector>
#include <initializer_list>
#include <algorithm>

using namespace codex;

static const float ST = 1.f / 48000.f;

TEST_CASE("WARP: at 120 BPM and 4 beats one bar lasts exactly 2 s") {
	WarpCore w;
	w.reset();
	WarpParams in;
	in.bpm = 120.f;
	WarpOut out;
	std::vector<int> bars;
	for (int i = 0; i < 3 * 96000 + 10; i++) {
		w.process(in, ST, out);
		if (out.bar)
			bars.push_back(i);
	}
	REQUIRE(bars.size() == 4);
	CHECK(bars[0] == 0);
	CHECK(std::abs(bars[1] - 96000) <= 1);
	CHECK(std::abs(bars[2] - 192000) <= 1);
}

TEST_CASE("WARP: beats are even with no warp and bunch up with warp, realigning on the downbeat") {
	for (float depth : {0.f, 1.f}) {
		WarpCore w;
		w.reset();
		WarpParams in;
		in.bpm = 120.f;
		in.shape = 2.f; // S-curve
		in.depth = depth;
		WarpOut out;
		std::vector<int> beats;
		for (int i = 0; i < 2 * 96000 + 10; i++) {
			w.process(in, ST, out);
			if (out.beat)
				beats.push_back(i);
		}
		REQUIRE(beats.size() == 9);
		CHECK(std::abs(beats[4] - 96000) <= 1); // downbeat of bar 2 never moves
		CHECK(std::abs(beats[8] - 192000) <= 1);
		int first = beats[1] - beats[0], second = beats[2] - beats[1];
		if (depth == 0.f)
			CHECK(std::abs(first - second) <= 1);
		else
			CHECK(first > second + 4000); // slow start, fast middle
	}
}

TEST_CASE("WARP: the bent position always rises (10,000 random settings)") {
	Rng rng(2026);
	WarpCore w;
	for (int trial = 0; trial < 10000; trial++) {
		w.setSeed(rng.below(1000));
		w.reset();
		// run into a random bar so the drunk shape differs
		w.B = (double)rng.below(500) + 0.999999;
		WarpParams in;
		WarpOut out;
		w.process(in, 1e-3f, out);
		float xs[14], ys[14];
		int n = rng.between(1, 14);
		for (int i = 0; i < n; i++) {
			xs[i] = rng.uniform();
			ys[i] = rng.uniform();
		}
		w.setDrawn(xs, ys, n);
		float shape = rng.range(0.f, 4.f), depth = rng.uniform();
		float prev = w.warpDepth(0.f, shape, depth);
		bool rising = std::fabs(prev) < 1e-5f;
		for (int i = 1; i <= 200; i++) {
			float v = w.warpDepth(i / 200.f, shape, depth);
			if (v < prev - 1e-5f)
				rising = false;
			prev = v;
		}
		if (std::fabs(prev - 1.f) > 1e-5f)
			rising = false;
		REQUIRE(rising);
	}
}

TEST_CASE("WARP: an output at ratio 3/4 has travelled exactly 3 cycles after 4 bars") {
	WarpCore w;
	w.reset();
	WarpParams in;
	in.bpm = 120.f;
	in.ratio[1] = 0.75f;
	WarpOut out;
	PhaseReader reader;
	// 4 bars at 2 s each = 384000 samples (the very first call only starts the clock)
	for (int i = 0; i <= 384000; i++) {
		w.process(in, ST, out);
		reader.process(out.phase[1]);
	}
	CHECK(out.unwrapped[1] == doctest::Approx(3.0).epsilon(1e-6));
	CHECK(reader.W == doctest::Approx(3.0).epsilon(2e-5)); // as another module would read it from the cable
}

TEST_CASE("WARP: the same seed gives the same drift, a different seed a different one") {
	auto run = [](int seed) {
		WarpCore w;
		w.setSeed(seed);
		w.reset();
		WarpParams in;
		in.bpm = 240.f;
		in.driftDepth = 0.25f;
		in.driftRate = 2.f;
		in.shape = 3.f;
		in.depth = 1.f;
		WarpOut out;
		std::vector<float> v;
		for (int i = 0; i < 48000 * 20; i++) {
			w.process(in, ST, out);
			if (i % 1000 == 0) {
				v.push_back(out.bpm);
				v.push_back(out.phase[0]);
			}
		}
		return v;
	};
	std::vector<float> a = run(5), b = run(5), c = run(6);
	CHECK(a == b);
	CHECK(a != c);
	// drift really moves the tempo, and stays inside +-25 %
	float lo = 1e9f, hi = 0.f;
	for (size_t i = 0; i < a.size(); i += 2) {
		lo = std::min(lo, a[i]);
		hi = std::max(hi, a[i]);
	}
	CHECK(hi - lo > 10.f);
	CHECK(lo >= 240.f * 0.75f - 0.01f);
	CHECK(hi <= 240.f * 1.25f + 0.01f);
}

TEST_CASE("WARP: stopped clock holds its phase; reset returns to the start") {
	WarpCore w;
	w.reset();
	WarpParams in;
	WarpOut out;
	for (int i = 0; i < 30000; i++)
		w.process(in, ST, out);
	float held = out.phase[0];
	CHECK(held > 0.5f);
	in.run = false;
	for (int i = 0; i < 1000; i++)
		w.process(in, ST, out);
	CHECK(out.phase[0] == held);
	w.reset();
	w.process(in, ST, out);
	CHECK(out.bar);
	CHECK(out.phase[0] == 0.f);
}

TEST_CASE("WARP: tempo CV follows the 0 V = 120 BPM rule and a new seed waits for the bar") {
	WarpCore w;
	w.reset();
	WarpParams in;
	in.bpm = 120.f;
	in.tempoCv = 1.f;
	WarpOut out;
	w.process(in, ST, out);
	CHECK(out.bpm == doctest::Approx(240.f));
	CHECK(out.tempoCv == doctest::Approx(1.f));
	w.setSeed(77);
	w.process(in, ST, out);
	CHECK(w.seed == 0);
	for (int i = 0; i < 48001; i++)
		w.process(in, ST, out);
	CHECK(w.seed == 77);
}
