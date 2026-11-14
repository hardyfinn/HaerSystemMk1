#include "doctest.h"
#include "common/Curve.hpp"
#include "common/Rng.hpp"
#include <cmath>
#include <initializer_list>

using namespace codex;

TEST_CASE("ease and scurve hit their end points and rise all the way") {
	for (float k : {0.25f, 0.5f, 1.f, 2.2f, 4.f}) {
		CHECK(ease(0.f, k) == doctest::Approx(0.f));
		CHECK(ease(1.f, k) == doctest::Approx(1.f));
	}
	for (float k : {0.f, 2.f, 8.f, 20.f}) {
		CHECK(scurve(0.f, k) == doctest::Approx(0.f).epsilon(1e-5));
		CHECK(scurve(0.5f, k) == doctest::Approx(0.5f).epsilon(1e-5));
		CHECK(scurve(1.f, k) == doctest::Approx(1.f).epsilon(1e-5));
		float prev = -1.f;
		for (int i = 0; i <= 1000; i++) {
			float v = scurve(i / 1000.f, k);
			CHECK(v >= prev);
			prev = v;
		}
	}
	CHECK(ease(0.5f, 2.f) == doctest::Approx(0.25f));
}

TEST_CASE("Spline passes through its points") {
	float xs[5] = {0.f, 0.2f, 0.5f, 0.9f, 1.f};
	float ys[5] = {0.f, 0.6f, 0.65f, 0.7f, 1.f};
	Spline s;
	s.set(xs, ys, 5);
	for (int i = 0; i < 5; i++)
		CHECK(s.eval(xs[i]) == doctest::Approx(ys[i]).epsilon(1e-5));
	CHECK(s.eval(-1.f) == doctest::Approx(0.f));
	CHECK(s.eval(2.f) == doctest::Approx(1.f));
}

TEST_CASE("Spline through rising points never dips (2000 random point sets)") {
	Rng rng(42);
	for (int trial = 0; trial < 2000; trial++) {
		int n = rng.between(2, 16);
		float xs[16], ys[16];
		for (int i = 0; i < n; i++) {
			xs[i] = rng.uniform();
			ys[i] = rng.uniform();
		}
		xs[0] = 0.f;
		ys[0] = 0.f;
		makeRising(xs, ys, n, 0.002f);
		Spline s;
		s.set(xs, ys, n);
		float prev = s.eval(xs[0]);
		bool rising = true;
		for (int i = 1; i <= 2000; i++) {
			float t = xs[0] + (xs[n - 1] - xs[0]) * i / 2000.f;
			float v = s.eval(t);
			if (v < prev - 1e-5f)
				rising = false;
			prev = v;
		}
		CHECK(rising);
	}
}

TEST_CASE("makeRising sorts and spaces the points") {
	float xs[4] = {0.5f, 0.1f, 0.5f, 0.9f};
	float ys[4] = {0.5f, 0.2f, 0.1f, 0.9f};
	makeRising(xs, ys, 4, 0.04f);
	for (int i = 1; i < 4; i++) {
		CHECK(xs[i] >= xs[i - 1] + 0.04f - 1e-6f);
		CHECK(ys[i] >= ys[i - 1] + 0.04f - 1e-6f);
	}
}
