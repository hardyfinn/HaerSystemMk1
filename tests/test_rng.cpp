#include "doctest.h"
#include "common/Rng.hpp"
#include <cmath>

using namespace codex;

TEST_CASE("Rng: same seed, same sequence; different seed, different sequence") {
	Rng a(123), b(123), c(124);
	bool same = true, differs = false;
	for (int i = 0; i < 1000; i++) {
		uint32_t x = a.next();
		if (x != b.next())
			same = false;
		if (x != c.next())
			differs = true;
	}
	CHECK(same);
	CHECK(differs);
}

TEST_CASE("Rng: matches the published xoshiro128** algorithm") {
	// With state {1,2,3,4} the first outputs follow directly from the definition.
	Rng r;
	r.s[0] = 1; r.s[1] = 2; r.s[2] = 3; r.s[3] = 4;
	CHECK(r.next() == 11520u);
	CHECK(r.next() == 0u);
	CHECK(r.next() == 5927040u);
}

TEST_CASE("Rng: forked streams are independent of how much the parent was used") {
	Rng parent(7);
	Rng early = parent.fork("drift");
	for (int i = 0; i < 500; i++)
		parent.next();
	Rng late = parent.fork("drift");
	Rng other = parent.fork("drunk");
	Rng track0 = parent.fork("track", 0), track1 = parent.fork("track", 1);
	bool same = true, differs = false, tracksDiffer = false;
	for (int i = 0; i < 100; i++) {
		uint32_t x = early.next();
		if (x != late.next())
			same = false;
		if (x != other.next())
			differs = true;
		if (track0.next() != track1.next())
			tracksDiffer = true;
	}
	CHECK(same);
	CHECK(differs);
	CHECK(tracksDiffer);
}

TEST_CASE("Rng: ranges and rough evenness") {
	Rng r(99);
	double sum = 0;
	int counts[10] = {};
	for (int i = 0; i < 100000; i++) {
		float u = r.uniform();
		REQUIRE(u >= 0.f);
		REQUIRE(u < 1.f);
		sum += u;
		int k = r.below(10);
		REQUIRE(k >= 0);
		REQUIRE(k < 10);
		counts[k]++;
		int b = r.between(3, 5);
		REQUIRE(b >= 3);
		REQUIRE(b <= 5);
	}
	CHECK(sum / 100000.0 == doctest::Approx(0.5).epsilon(0.01));
	for (int k = 0; k < 10; k++)
		CHECK(std::abs(counts[k] - 10000) < 500);
}

TEST_CASE("SeedLatch: a new seed waits for the boundary") {
	SeedLatch s;
	CHECK(SeedLatch::read(41.6f, 0.f, false) == 42);
	CHECK(SeedLatch::read(0.f, 10.f, true) == 999);
	CHECK(SeedLatch::read(0.f, -2.f, true) == 0);
	s.set(42);
	CHECK(s.active == 0);
	CHECK(s.commit());
	CHECK(s.active == 42);
	CHECK_FALSE(s.commit());
}
