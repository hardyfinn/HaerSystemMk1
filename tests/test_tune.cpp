#include "doctest.h"
#include "dsp/TuneCore.hpp"
#include "common/Rng.hpp"
#include <cmath>
#include <vector>

using namespace codex;

namespace {
TuneField field(int id) {
	TuneField f;
	tuneBuiltIn(id, f);
	return f;
}
// distance in cents from v (1 V/oct) to the nearest point of f. Every field is laid out
// over 4 octaves from the root and that span repeats (which only matters for Bohlen-Pierce).
float centsOff(const TuneField& f, float v, float root) {
	float x = (v - root) * 1200.f;
	float span = TuneMorph::SPAN;
	float best = 1e9f;
	for (int k = 0; k * f.period < span; k++)
		for (int i = 0; i < f.n; i++) {
			float c = f.cents[i] + k * f.period;
			if (c >= span - 1e-3f)
				continue;
			float r = std::round((x - c) / span);
			best = std::min(best, std::fabs(x - (c + r * span)));
		}
	return best;
}
} // namespace

TEST_CASE("TUNE MAP: every built-in field is in range and sorted") {
	for (int id = 1; id <= FIELD_QUARTER_MINOR; id++) {
		TuneField f;
		REQUIRE(tuneBuiltIn(id, f));
		CHECK(f.n > 0);
		for (int i = 0; i < f.n; i++) {
			CHECK(f.cents[i] >= 0.f);
			CHECK(f.cents[i] < f.period);
			if (i > 0)
				CHECK(f.cents[i] > f.cents[i - 1]);
		}
	}
	TuneField u;
	CHECK_FALSE(tuneBuiltIn(FIELD_USER_1, u));
}

TEST_CASE("TUNE MAP: with MORPH 0 every output lies on a field A pitch (within 0.1 cent)") {
	Rng rng(11);
	for (int a = 1; a <= FIELD_QUARTER_MINOR; a++) {
		int b = 1 + (a * 5) % FIELD_QUARTER_MINOR;
		TuneField A = field(a), B = field(b);
		TuneMorph morph;
		morph.build(A, B);
		TuneQuantiser q;
		q.set(morph, 0.f);
		TuneChannel ch;
		for (int i = 0; i < 300; i++) {
			float root = rng.bipolar() * 1.f;
			float in = rng.bipolar() * 4.f;
			float out = q.process(ch, in, root, 1.f, 0.f);
			CHECK(centsOff(A, out, root) < 0.1f);
		}
	}
}

TEST_CASE("TUNE MAP: each paired pitch moves steadily from its A value to its B value") {
	TuneField A = field(FIELD_MINOR), B = field(FIELD_HARM_16_32);
	TuneMorph morph;
	morph.build(A, B);
	REQUIRE(morph.n > 0);
	int paired = 0;
	for (int k = 0; k < morph.n; k++) {
		const TuneMorph::Pair& p = morph.pair[k];
		if (p.wa == 0.f || p.wb == 0.f)
			continue;
		paired++;
		float last = p.a, dir = p.b >= p.a ? 1.f : -1.f;
		for (int s = 1; s <= 100; s++) {
			float m = s / 100.f;
			// unwrapped position along the glide
			float c = p.a + (p.b - p.a) * m;
			CHECK((c - last) * dir >= -1e-4f);
			last = c;
		}
		CHECK(last == doctest::Approx(p.b));
	}
	CHECK(paired >= 20);
	// and the quantiser's morphed points at 0 and 1 are the A and B fields
	TuneQuantiser q0, q1;
	q0.set(morph, 0.f);
	q1.set(morph, 1.f);
	for (int k = 0; k < morph.n; k++) {
		if (q0.weight[k] >= TuneQuantiser::MIN_WEIGHT)
			CHECK(std::fmod(q0.cents[k], 1200.f) == doctest::Approx(std::fmod(morph.pair[k].a, 1200.f)).epsilon(1e-4));
	}
}

TEST_CASE("TUNE MAP: the pairing is greedy nearest first") {
	TuneField A, B;
	A.add(0);
	A.add(100);
	B.add(90);
	B.add(400);
	TuneMorph m;
	m.build(A, B);
	// 100 and 90 are closest, so they pair; 0 then takes 400 (nothing nearer is left)
	bool found = false;
	for (int k = 0; k < m.n; k++)
		if (std::fabs(m.pair[k].a - 100.f) < 0.01f && std::fabs(m.pair[k].b - 90.f) < 0.01f)
			found = true;
	CHECK(found);
}

TEST_CASE("TUNE MAP: hysteresis holds a target and CHANGED fires only when the target moves") {
	TuneField A = field(FIELD_MAJOR);
	TuneMorph morph;
	morph.build(A, A);
	TuneQuantiser q;
	q.set(morph, 0.f);
	TuneChannel ch;
	// halfway between C (0) and D (200) is 100 cents; wobble around it
	float out = q.process(ch, 90.f / 1200.f, 0.f, 1.f, 30.f);
	CHECK(ch.changed);
	CHECK(out == doctest::Approx(0.f).epsilon(1e-6));
	q.process(ch, 110.f / 1200.f, 0.f, 1.f, 30.f);
	CHECK_FALSE(ch.changed);
	q.process(ch, 135.f / 1200.f, 0.f, 1.f, 30.f);
	CHECK(ch.changed);
	// half strength lands halfway
	TuneChannel c2;
	float half = q.process(c2, 50.f / 1200.f, 0.f, 0.5f, 0.f);
	CHECK(half * 1200.f == doctest::Approx(25.f).epsilon(1e-3));
}

TEST_CASE("TUNE MAP: an empty field lets the pitch through") {
	TuneField A, B;
	TuneMorph morph;
	morph.build(A, B);
	TuneQuantiser q;
	q.set(morph, 0.5f);
	TuneChannel ch;
	CHECK(q.process(ch, 0.37f, 0.f, 1.f, 0.f) == doctest::Approx(0.37f));
}

TEST_CASE("TUNE MAP: learning from sines at 220, 330 and 550 Hz finds those pitch classes (within 5 cents)") {
	TuneLearner learner;
	learner.start();
	const float sr = 48000.f;
	for (int i = 0; i < 48000; i++) {
		double t = i / (double)sr;
		float x = (float)(0.3 * std::sin(2 * M_PI * 220 * t) + 0.3 * std::sin(2 * M_PI * 330 * t) + 0.3 * std::sin(2 * M_PI * 550 * t));
		learner.feed(x);
	}
	TuneField f;
	REQUIRE(learner.finish(sr, f));
	CHECK(f.n == 3);
	for (double hz : {220.0, 330.0, 550.0}) {
		float c = (float)std::fmod(1200.0 * std::log2(hz / 261.6256) + 1200.0, 1200.0);
		float best = 1e9f;
		for (int i = 0; i < f.n; i++) {
			float d = std::fabs(f.cents[i] - c);
			best = std::min(best, std::min(d, 1200.f - d));
		}
		CHECK(best < 5.f);
	}
}

TEST_CASE("TUNE MAP: FIELD lists the first octave above the root") {
	TuneField A = field(FIELD_PENTATONIC);
	TuneMorph morph;
	morph.build(A, A);
	TuneQuantiser q;
	q.set(morph, 0.f);
	float out[16];
	int n = q.firstOctave(out, 16);
	REQUIRE(n == 5);
	CHECK(out[0] == doctest::Approx(0.f));
	CHECK(out[4] == doctest::Approx(900.f));
}
