#include "doctest.h"
#include "util.hpp"
#include "dsp/ContourCore.hpp"
#include "common/Trigger.hpp"
#include <vector>
#include <cmath>
#include <initializer_list>

using namespace codex;

// Run the core over `cycles` bars at `perCycle` samples per bar and count hits on channel 0.
static int countHits(ContourCore& core, ContourParams& in, int cycles, int perCycle, std::vector<float>* extCurve = nullptr) {
	ContourOut out;
	int hits = 0;
	for (int i = 0; i <= cycles * perCycle; i++) {
		double phi = (double)i / perCycle;
		if (extCurve)
			in.ext = (*extCurve)[(size_t)(i % perCycle)];
		core.process(in, phi, out);
		if (out.trig[0])
			hits++;
	}
	return hits;
}

TEST_CASE("CONTOUR: a sine with one level at 0, both directions, gives 2 hits per cycle") {
	ContourCore core;
	core.reset();
	ContourParams in;
	in.amp[0] = 1.f; in.amp[1] = 0.f;
	in.theta[0] = 0.1f; // keep the start off a crossing
	in.nThresh = 1;
	in.direction = ContourParams::BOTH;
	CHECK(countHits(core, in, 10, 4800) == 20);
	core.reset();
	in.direction = ContourParams::UP;
	CHECK(countHits(core, in, 10, 4800) == 10);
}

TEST_CASE("CONTOUR: a rising ramp with N levels gives exactly N hits per cycle") {
	const int per = 4800;
	std::vector<float> ramp((size_t)per);
	for (int i = 0; i < per; i++)
		ramp[(size_t)i] = -1.f + 2.f * (float)i / per;
	for (int n : {1, 3, 8, 16}) {
		for (float cluster : {-1.f, 0.f, 1.f}) {
			ContourCore core;
			core.reset();
			ContourParams in;
			in.mode = ContourParams::EXT;
			in.nThresh = n;
			in.spreadT = cluster;
			in.hyst = 0.f;
			in.direction = ContourParams::UP;
			CHECK(countHits(core, in, 10, per, &ramp) == 10 * n);
		}
	}
}

TEST_CASE("CONTOUR: levels follow the cluster control") {
	CHECK(ContourCore::levelAt(0, 1, 0.f) == doctest::Approx(0.f));
	CHECK(ContourCore::levelAt(0, 2, 0.f) == doctest::Approx(-0.5f));
	CHECK(ContourCore::levelAt(1, 2, 0.f) == doctest::Approx(0.5f));
	CHECK(std::fabs(ContourCore::levelAt(1, 2, -1.f)) < 0.1f); // crowded round 0
	CHECK(std::fabs(ContourCore::levelAt(1, 2, 1.f)) > 0.8f);  // pushed to the extremes
}

TEST_CASE("CONTOUR: hysteresis stops chatter on a noisy curve") {
	const int per = 48000;
	Rng rng(3);
	std::vector<float> noisy((size_t)per);
	for (int i = 0; i < per; i++)
		noisy[(size_t)i] = 0.8f * std::sin(6.2831853f * (float)i / per + 0.5f) + 0.02f * rng.bipolar();
	ContourParams in;
	in.mode = ContourParams::EXT;
	in.nThresh = 1;
	in.direction = ContourParams::BOTH;
	ContourCore a, b;
	a.reset();
	b.reset();
	in.hyst = 0.f;
	int without = countHits(a, in, 4, per, &noisy);
	in.hyst = 0.05f;
	int with = countHits(b, in, 4, per, &noisy);
	CHECK(without > 16); // the noise makes extra crossings
	CHECK(with == 8);    // 2 clean hits per cycle
}

TEST_CASE("CONTOUR: MOIRE with no detune and harmonic ratios repeats every cycle and meets on the bar") {
	ContourCore core;
	core.reset();
	ContourParams in;
	in.mode = ContourParams::MOIRE;
	in.spread = 0.f;
	in.ratioSet = ContourParams::R_HARMONIC;
	in.coincideN = 8;
	in.window = 0.004f;
	const int per = 4800;
	ContourOut out;
	std::vector<float> first, second;
	int meets = 0;
	for (int i = 0; i < 3 * per; i++) {
		core.process(in, (double)i / per + 0.25 / per, out);
		if (i < per)
			first.push_back(out.curve);
		else if (i < 2 * per)
			second.push_back(out.curve);
		if (out.coincide) {
			meets++;
			CHECK(out.count == 8);
		}
	}
	float worst = 0.f;
	for (size_t i = 0; i < first.size(); i++)
		worst = std::fmax(worst, std::fabs(first[i] - second[i]));
	CHECK(worst < 1e-4f);
	// once at the start, then just before each of the next three bar lines
	CHECK(meets >= 3);
	CHECK(meets <= 4);
}

TEST_CASE("CONTOUR: MOIRE detune makes the pattern drift, and the seed decides how") {
	auto run = [](int seed, float spread) {
		ContourCore core;
		core.setSeed(seed);
		core.reset();
		ContourParams in;
		in.mode = ContourParams::MOIRE;
		in.spread = spread;
		ContourOut out;
		std::vector<float> v;
		for (int i = 0; i < 20000; i++) {
			core.process(in, (double)i / 1000.0, out);
			v.push_back(out.curve);
		}
		return v;
	};
	CHECK(run(1, 0.02f) == run(1, 0.02f));
	CHECK(run(1, 0.02f) != run(2, 0.02f));
	CHECK(run(1, 0.f) == run(2, 0.f));
}

TEST_CASE("CONTOUR: at 440 Hz with 4 levels the trigger output is a pulse train with an 880 Hz fundamental") {
	const float sr = 48000.f;
	ContourCore core;
	core.reset();
	ContourParams in;
	in.amp[0] = 1.f; in.amp[1] = 0.f;
	in.nThresh = 4;
	in.hyst = 0.f;
	in.direction = ContourParams::BOTH;
	ContourOut out;
	AdaptiveTrigger trig;
	std::vector<float> sig;
	int hits = 0;
	for (int i = 0; i < 48000; i++) {
		core.process(in, 440.0 * (double)i / sr + 0.01, out);
		if (out.trig[0]) {
			hits++;
			trig.fire(0);
		}
		sig.push_back(trig.process(0, 1.f / sr) - 5.f);
	}
	// 4 levels crossed up and down = 8 hits per cycle
	CHECK(hits >= 3520 - 8);
	CHECK(hits <= 3520);
	// every hit is still a separate pulse, not one stuck-high gate
	CHECK(std::abs(testutil::zeroCrossings(sig) - hits) <= 2);
	// the hit pattern repeats twice per cycle: 440 x 4 / 2 = 880 Hz, with nothing at 440 Hz
	std::vector<float> steady(sig.begin() + 4800, sig.end());
	float f880 = testutil::goertzel(steady, 880.f, sr);
	float f440 = testutil::goertzel(steady, 440.f, sr);
	CHECK(f880 > 0.5f);
	CHECK(f440 < 0.05f * f880);
}

TEST_CASE("CONTOUR: hits spread across channels, velocity follows the chosen source, backwards is safe") {
	ContourCore core;
	core.reset();
	ContourParams in;
	in.amp[0] = 1.f; in.amp[1] = 0.f;
	in.theta[0] = 0.1f;
	in.nThresh = 4;
	in.channels = 4;
	in.velMode = ContourParams::VEL_INDEX;
	ContourOut out;
	int perChannel[4] = {};
	for (int i = 0; i <= 48000; i++) {
		core.process(in, (double)i / 4800.0, out);
		for (int c = 0; c < 4; c++)
			if (out.trig[c])
				perChannel[c]++;
	}
	for (int c = 0; c < 4; c++) {
		CHECK(perChannel[c] == 20);
		CHECK(out.vel[c] == doctest::Approx((c + 1) / 4.f));
	}
	CHECK(std::fabs(out.slope) <= 1.f);
	// run backwards: the curve still crosses its levels and nothing breaks
	int back = 0;
	for (int i = 48000; i >= 0; i--) {
		core.process(in, (double)i / 4800.0, out);
		for (int c = 0; c < 4; c++)
			if (out.trig[c])
				back++;
		REQUIRE(std::isfinite(out.curve));
		REQUIRE(std::isfinite(out.slope));
	}
	CHECK(back == 80);
}
