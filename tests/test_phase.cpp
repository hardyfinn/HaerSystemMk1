#include "doctest.h"
#include "util.hpp"
#include "common/Phase.hpp"
#include <vector>

using namespace codex;

TEST_CASE("PhaseReader: a forward ramp counts cycles and steps") {
	PhaseReader r;
	const double sr = 48000.0;
	std::vector<int64_t> steps;
	// 0.5 Hz ramp = one bar every 2 s; run 4 bars
	for (int i = 0; i < 4 * 96000; i++) {
		r.process(testutil::ramp(0.5, sr, i));
		Crossings c = r.crossed(16.0);
		for (int k = 0; k < c.count; k++)
			steps.push_back(c.index[k]);
	}
	CHECK(r.W == doctest::Approx(4.0).epsilon(1e-4));
	REQUIRE(steps.size() == 64); // 16 steps x 4 bars, starting with step 0
	for (size_t i = 0; i < steps.size(); i++)
		CHECK(steps[i] == (int64_t)i);
}

TEST_CASE("PhaseReader: a wrap moves on to the next bar") {
	PhaseReader r;
	r.process(9.9f);
	r.process(9.95f);
	CHECK(r.bar() == 0);
	r.process(0.05f);
	CHECK(r.bar() == 1);
	CHECK(r.W == doctest::Approx(1.005).epsilon(1e-5));
	CHECK(r.passed(1.0));
}

TEST_CASE("PhaseReader: a backward wrap goes back a bar") {
	PhaseReader r;
	r.process(0.2f);
	r.process(0.1f);
	r.process(9.9f);
	CHECK(r.W == doctest::Approx(-0.01).epsilon(1e-4));
	CHECK(r.bar() == -1);
	// backward motion: silent by default, reported in falling order when allowed
	CHECK(r.crossed(4.0).count == 0);
	Crossings c = r.crossed(4.0, true);
	CHECK(c.count == 1);
	CHECK(c.dir == -1);
	CHECK(c.index[0] == -1);
}

TEST_CASE("PhaseReader: a jump of more than half a cycle is read as a wrap (known limit)") {
	PhaseReader r;
	r.process(1.f);
	r.process(8.f); // +0.7 cycle is taken as -0.3
	CHECK(r.W == doctest::Approx(-0.2).epsilon(1e-5));
}

TEST_CASE("PhaseReader: a 1 kHz ramp still gives every step") {
	PhaseReader r;
	const double sr = 48000.0;
	int64_t count = 0, last = -1;
	bool inOrder = true;
	for (int i = 0; i < 48000; i++) {
		r.process(testutil::ramp(1000.0, sr, i));
		Crossings c = r.crossed(32.0);
		for (int k = 0; k < c.count; k++) {
			if (c.index[k] != last + 1)
				inOrder = false;
			last = c.index[k];
			count++;
		}
	}
	CHECK(inOrder); // no step skipped
	CHECK(count >= 32000 - 32);
	CHECK(count <= 32000);
}

TEST_CASE("PhaseReader: no more than 8 events in one sample, most recent kept") {
	PhaseReader r;
	r.process(0.f);
	r.process(4.f); // 0.4 cycle in one sample
	Crossings c = r.crossed(64.0);
	CHECK(c.count == 8);
	CHECK(c.index[7] == 25);
	CHECK(c.index[0] == 18);
}

TEST_CASE("PhaseReader: reset returns to bar 0 and reports the current step once") {
	PhaseReader r;
	for (int i = 0; i < 300000; i++)
		r.process(testutil::ramp(0.5, 48000.0, i));
	CHECK(r.bar() == 3);
	r.reset();
	r.process(2.5f);
	CHECK(r.bar() == 0);
	Crossings c = r.crossed(16.0);
	CHECK(c.count == 1);
	CHECK(c.index[0] == 4);
	r.process(2.5001f);
	CHECK(r.crossed(16.0).count == 0);
}

TEST_CASE("PhaseReader: out-of-range and NaN voltages are safe") {
	PhaseReader r;
	r.process(-3.f);
	r.process(15.f);
	r.process(std::nanf(""));
	CHECK(std::isfinite(r.W));
}
