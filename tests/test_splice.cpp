#include "doctest.h"
#include "util.hpp"
#include "dsp/SpliceCore.hpp"
#include <vector>
#include <cmath>

using namespace codex;
using namespace testutil;

static const float SR = 48000.f;

TEST_CASE("SPLICE: before a stutter the input passes straight through") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	SpliceOut out;
	for (int i = 0; i < 1000; i++) {
		float x = std::sin(0.01f * i);
		core.process(p, x, -x, out);
		CHECK(out.l == x);
		CHECK(out.r == -x);
	}
}

TEST_CASE("SPLICE: with ACCEL 1, no pitch ramp and no reverse, a stutter repeats the slice exactly") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.1f; // 4800 samples
	SpliceOut out;
	std::vector<float> in;
	auto src = [](int i) { return 5.f * std::sin(2.f * (float)M_PI * 137.f * (float)i / SR) + (float)(i % 97) * 0.01f; };
	const int T = 20000;
	for (int i = 0; i < T; i++) {
		in.push_back(src(i));
		core.process(p, in.back(), in.back(), out);
	}
	p.stutter = true;
	const int fade = 96; // 2 ms
	int trigs = 0;
	float worst = 0.f, biggestStep = 0.f, last = out.l;
	for (int i = 0; i < 4800 * 5; i++) {
		float x = src(T + i);
		in.push_back(x);
		core.process(p, x, x, out);
		trigs += out.sliceTrig ? 1 : 0;
		int j = i % 4800;
		CHECK(out.repeat == i / 4800);
		if (j >= fade) // outside the crossfade the slice is reproduced exactly
			worst = std::fmax(worst, std::fabs(out.l - in[(size_t)(T - 4799 + j)])); // the slice ends with the sample that arrived as the gate rose
		biggestStep = std::fmax(biggestStep, std::fabs(out.l - last));
		last = out.l;
	}
	CHECK(trigs == 5);
	CHECK(worst < 1e-5f);
	// and the joins are no rougher than the material itself (a 137 Hz sine at 5 V moves 0.09 V per sample;
	// the test signal also has small steps of 0.97 V built in)
	CHECK(biggestStep < 1.3f);
	// letting go returns to the live input after the crossfade
	p.stutter = false;
	for (int i = 0; i < 200; i++) {
		float x = src(T + 24000 + i);
		core.process(p, x, x, out);
		if (i >= fade)
			CHECK(out.l == x);
	}
}

TEST_CASE("SPLICE: with ACCEL 0.5 repeat lengths halve until the 2 ms floor") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.128f; // 6144 samples
	p.accel = 0.5f;
	SpliceOut out;
	for (int i = 0; i < 10000; i++)
		core.process(p, 1.f, 1.f, out);
	p.stutter = true;
	std::vector<int> starts;
	for (int i = 0; i < 14000; i++) {
		core.process(p, 1.f, 1.f, out);
		if (out.sliceTrig)
			starts.push_back(i);
	}
	REQUIRE(starts.size() > 12);
	const int expect[8] = {6144, 3072, 1536, 768, 384, 192, 96, 96};
	for (int k = 0; k < 8; k++)
		CHECK(starts[(size_t)k + 1] - starts[(size_t)k] == expect[k]);
	// ACCEL 2: repeats get longer but never longer than the slice
	SpliceCore core2;
	core2.setSampleRate(SR);
	p.accel = 2.f;
	p.stutter = false;
	for (int i = 0; i < 10000; i++)
		core2.process(p, 1.f, 1.f, out);
	p.stutter = true;
	starts.clear();
	for (int i = 0; i < 20000; i++) {
		core2.process(p, 1.f, 1.f, out);
		if (out.sliceTrig)
			starts.push_back(i);
	}
	REQUIRE(starts.size() >= 3);
	CHECK(starts[1] - starts[0] == 6144);
	CHECK(starts[2] - starts[1] == 6144);
}

TEST_CASE("SPLICE: no clicks at the joins on a DC input") {
	for (float accel : {0.5f, 0.8f, 1.f, 1.7f}) {
		for (float reverse : {0.f, 0.5f, 1.f}) {
			SpliceCore core;
			core.setSampleRate(SR);
			SpliceParams p;
			p.sliceSeconds = 0.05f;
			p.accel = accel;
			p.reverse = reverse;
			p.pitchRamp = 1.f;
			p.decay = 0.9f;
			SpliceOut out;
			for (int i = 0; i < 6000; i++)
				core.process(p, 5.f, 5.f, out);
			float last = out.l, worst = 0.f;
			for (int i = 0; i < 48000; i++) {
				p.stutter = i < 30000;
				core.process(p, 5.f, 5.f, out);
				worst = std::fmax(worst, std::fabs(out.l - last));
				last = out.l;
			}
			// a hard cut would jump by up to 5 V; a 2 ms fade moves at most about 0.1 V per sample
			CHECK(worst < 0.15f);
		}
	}
}

TEST_CASE("SPLICE: pitch ramp raises each repeat and shortens it; DECAY fades them") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.1f;
	p.pitchRamp = 2.f;
	p.decay = 0.5f;
	SpliceOut out;
	for (int i = 0; i < 10000; i++)
		core.process(p, 5.f * std::sin(2.f * (float)M_PI * 480.f * (float)i / SR), 0.f, out);
	p.stutter = true;
	std::vector<int> starts;
	std::vector<float> rep[3];
	for (int i = 0; i < 16000; i++) {
		core.process(p, 0.f, 0.f, out);
		if (out.sliceTrig)
			starts.push_back(i);
		int k = (int)starts.size() - 1;
		if (k >= 0 && k < 3 && i - starts[(size_t)k] > 200)
			rep[k].push_back(out.l);
	}
	REQUIRE(starts.size() >= 4);
	float ratio = std::pow(2.f, 2.f / 12.f);
	CHECK(starts[1] - starts[0] == 4800);
	CHECK(starts[2] - starts[1] == doctest::Approx(4800.f / ratio).epsilon(0.01));
	rep[1].resize(3600);
	rep[2].resize(3000);
	CHECK(goertzel(rep[1], 480.f * ratio, SR) > 5.f * goertzel(rep[1], 480.f, SR));
	CHECK(maxAbs(rep[0]) == doctest::Approx(5.f).epsilon(0.02));
	CHECK(maxAbs(rep[1]) == doctest::Approx(2.5f).epsilon(0.02));
	CHECK(maxAbs(rep[2]) == doctest::Approx(1.25f).epsilon(0.02));
}

TEST_CASE("SPLICE: REVERSE plays the slice backwards, and the same seed reverses the same repeats") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.05f; // 2400
	p.reverse = 1.f;
	SpliceOut out;
	std::vector<float> in;
	for (int i = 0; i < 9600; i++) {
		in.push_back((float)i * 0.001f); // a rising ramp
		core.process(p, in.back(), 0.f, out);
	}
	p.stutter = true;
	std::vector<float> got;
	for (int i = 0; i < 2400; i++) {
		core.process(p, 0.f, 0.f, out);
		got.push_back(out.l);
	}
	// backwards: it starts at the end of the slice and falls
	CHECK(got[200] == doctest::Approx(in[9600 - 200]).epsilon(0.002));
	CHECK(got[2000] < got[200]);

	auto pattern = [](int seed) {
		SpliceCore c;
		c.setSampleRate(SR);
		c.setSeed(seed);
		SpliceParams q;
		q.sliceSeconds = 0.01f;
		q.reverse = 0.5f;
		SpliceOut o;
		for (int i = 0; i < 2000; i++)
			c.process(q, (float)i, 0.f, o);
		q.stutter = true;
		std::vector<int> dir;
		float last = 0.f;
		int sinceTrig = 1000;
		for (int i = 0; i < 480 * 40; i++) {
			c.process(q, 0.f, 0.f, o);
			sinceTrig = o.sliceTrig ? 0 : sinceTrig + 1;
			if (sinceTrig == 300)
				dir.push_back(o.l > last ? 1 : -1);
			last = o.l;
		}
		return dir;
	};
	auto a = pattern(1), b = pattern(1), c = pattern(2);
	CHECK(a == b);
	CHECK(a != c);
	int back = 0;
	for (int d : a)
		back += d < 0;
	CHECK(back > 8);
	CHECK(back < 32);
}

TEST_CASE("SPLICE: HOLD keeps a triggered stutter going; FREEZE and REC modes stop the tape") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.05f;
	p.holdSlices = 4.f;
	SpliceOut out;
	for (int i = 0; i < 5000; i++)
		core.process(p, 1.f, 1.f, out);
	int trigs = 0, activeFor = 0;
	for (int i = 0; i < 20000; i++) {
		p.stutter = i < 48; // a 1 ms trigger
		core.process(p, 1.f, 1.f, out);
		trigs += out.sliceTrig;
		activeFor += core.stuttering();
	}
	CHECK(trigs == 4);
	CHECK(activeFor == doctest::Approx(9600).epsilon(0.01));

	size_t head = core.writeHead();
	p.freeze = true;
	for (int i = 0; i < 100; i++)
		core.process(p, 1.f, 1.f, out);
	CHECK(core.writeHead() == head);
	p.freeze = false;
	p.rec = SpliceParams::REC_OFF;
	for (int i = 0; i < 100; i++)
		core.process(p, 1.f, 1.f, out);
	CHECK(core.writeHead() == head);
	p.rec = SpliceParams::REC_GATED;
	p.recGate = true;
	for (int i = 0; i < 100; i++)
		core.process(p, 1.f, 1.f, out);
	CHECK(core.writeHead() == head + 100);
}

TEST_CASE("SPLICE: SMEAR turns the output into grains, never more than 32, and stays in bounds") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.2f;
	p.smear = 1.f;
	p.density = 100.f;
	p.grain = 0.2f;
	p.jitter = 1.f;
	p.width = 1.f;
	SpliceOut out;
	Rng rng(3);
	float peak = 0.f, diffLR = 0.f;
	int most = 0;
	bool finite = true;
	for (int i = 0; i < 96000; i++) {
		float x = 5.f * rng.bipolar();
		p.stutter = i > 30000 && i < 70000;
		core.process(p, x, x, out);
		finite = finite && std::isfinite(out.l) && std::isfinite(out.r);
		if (i > 20000) {
			peak = std::fmax(peak, std::fmax(std::fabs(out.l), std::fabs(out.r)));
			diffLR = std::fmax(diffLR, std::fabs(out.l - out.r));
		}
		int n = 0;
		for (int g = 0; g < SpliceCore::GRAINS; g++)
			n += core.grainList()[g].on;
		most = std::max(most, n);
	}
	CHECK(finite);
	CHECK(most <= 32);
	CHECK(most >= 15);
	CHECK(peak > 1.f);
	CHECK(peak < 14.f); // 5 V noise in: a cloud of noise grains has peaks like noise
	CHECK(diffLR > 0.5f); // grains are scattered across the stereo field
}

TEST_CASE("SPLICE: SCRUB reads the slice at the position the control voltage gives") {
	SpliceCore core;
	core.setSampleRate(SR);
	SpliceParams p;
	p.sliceSeconds = 0.1f;
	SpliceOut out;
	for (int i = 0; i < 9600; i++)
		core.process(p, (float)i * 0.001f, 0.f, out);
	p.stutter = true;
	p.scrubOn = true;
	p.scrub = 0.5f;
	for (int i = 0; i < 2000; i++)
		core.process(p, 0.f, 0.f, out);
	// half-way through the last 4800 samples is sample 7200
	CHECK(out.l == doctest::Approx(7.2f).epsilon(0.005));
	p.scrub = 0.f;
	for (int i = 0; i < 2000; i++)
		core.process(p, 0.f, 0.f, out);
	CHECK(out.l == doctest::Approx(4.8f).epsilon(0.005));
}
