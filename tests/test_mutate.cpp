#include "doctest.h"
#include "util.hpp"
#include "dsp/MutateCore.hpp"
#include "common/Phase.hpp"
#include <vector>
#include <cstring>
#include <memory>
#include <initializer_list>

using namespace codex;

static bool samePattern(const MutatePattern& a, const MutatePattern& b) {
	for (int t = 0; t < 8; t++) {
		if (a.track[t].length != b.track[t].length)
			return false;
		for (int i = 0; i < a.track[t].length; i++)
			if (!a.track[t].step[i].same(b.track[t].step[i]))
				return false;
	}
	return true;
}

static int hits(const MutateTrack& tr) {
	int n = 0;
	for (int i = 0; i < tr.length; i++)
		n += tr.step[i].on ? 1 : 0;
	return n;
}

// A track with every step on and nothing fancy.
static void fillTrack(MutateCore& m, int t, int len) {
	m.setLength(t, len);
	for (int i = 0; i < len; i++) {
		m.pat.track[t].step[i] = MutateStep();
		m.pat.track[t].step[i].on = true;
	}
}
static void clearAll(MutateCore& m) {
	for (int t = 0; t < 8; t++)
		for (int i = 0; i < 64; i++)
			m.pat.track[t].step[i] = MutateStep();
}

// Drive the core from a 0..10 V ramp like the module does. Returns the sample index of every hit on `track`.
static std::vector<int> play(MutateCore& m, MutateParams& in, int track, int samples, double barHz, double sr = 48000.0, bool reverse = false) {
	PhaseReader reader;
	MutateOut out;
	std::vector<int> when;
	for (int i = 0; i < samples; i++) {
		int k = reverse ? samples - i : i;
		reader.process(testutil::ramp(barHz, sr, k));
		Crossings c = reader.crossed((double)in.steps);
		m.process(in, c, reader.W * in.steps, reader.delta() < 0.0, out);
		if (out.gate[track])
			when.push_back(i);
	}
	return when;
}

TEST_CASE("MUTATE: generation is repeatable and follows seed, density and style") {
	auto a = std::make_unique<MutateCore>(), b = std::make_unique<MutateCore>(), c = std::make_unique<MutateCore>();
	MutateParams in;
	a->setSeed(11); b->setSeed(11); c->setSeed(12);
	a->generate(in); b->generate(in); c->generate(in);
	CHECK(samePattern(a->pat, b->pat));
	CHECK_FALSE(samePattern(a->pat, c->pat));
	CHECK(samePattern(a->pat, a->home)); // a fresh pattern is its own home

	// full density on a kick track is four to the bar
	in.density = 1.f;
	a->generate(in);
	CHECK(hits(a->pat.track[0]) >= 4);
	// snare: backbeats on steps 4 and 12 of 16
	CHECK(a->pat.track[1].step[4].on);
	CHECK(a->pat.track[1].step[12].on);
	// more density, more hats
	in.density = 0.1f;
	b->generate(in);
	CHECK(hits(a->pat.track[2]) > hits(b->pat.track[2]));
	// no track is ever empty, and all values are in range
	for (int seed = 0; seed < 200; seed++) {
		a->setSeed(seed);
		in.density = (seed % 11) / 10.f;
		a->generate(in);
		for (int t = 0; t < 8; t++) {
			REQUIRE(hits(a->pat.track[t]) >= 1);
			for (int i = 0; i < a->pat.track[t].length; i++) {
				const MutateStep& s = a->pat.track[t].step[i];
				REQUIRE(s.vel >= 0.f); REQUIRE(s.vel <= 1.f);
				REQUIRE(s.ratchet >= 1); REQUIRE(s.ratchet <= 8);
				REQUIRE(s.micro >= -0.5f); REQUIRE(s.micro <= 0.5f);
				REQUIRE(s.lockA >= 0.f); REQUIRE(s.lockA <= 1.f);
			}
		}
	}
}

TEST_CASE("MUTATE: euclid spreads hits evenly") {
	int n = 0;
	for (int i = 0; i < 16; i++)
		n += euclid(i, 4, 16, 0) ? 1 : 0;
	CHECK(n == 4);
	CHECK(euclid(0, 4, 16, 0));
	CHECK(euclid(4, 4, 16, 0));
	CHECK_FALSE(euclid(1, 4, 16, 0));
	n = 0;
	for (int i = 0; i < 13; i++)
		n += euclid(i, 5, 13, 3) ? 1 : 0;
	CHECK(n == 5);
}

TEST_CASE("MUTATE: mutations move away from home; with DRIFT 0 return steps bring it all the way back") {
	auto m = std::make_unique<MutateCore>();
	MutateParams in;
	m->setSeed(5);
	m->generate(in);
	in.amount = 1.f;
	in.drift = 1.f;
	for (int i = 0; i < 40; i++)
		m->mutatePass(in);
	int away = 0;
	for (int t = 0; t < 8; t++)
		away += m->differing(t);
	CHECK(away > 10);
	CHECK_FALSE(samePattern(m->pat, m->home));

	in.drift = 0.f; // every operation is now a step back toward home
	int before = away;
	m->mutatePass(in);
	int after = 0;
	for (int t = 0; t < 8; t++)
		after += m->differing(t);
	CHECK(after == before - 4); // amount 1 = four operations per pass
	for (int i = 0; i < 200; i++)
		m->mutatePass(in);
	CHECK(samePattern(m->pat, m->home));
	CHECK_FALSE(m->returnStep()); // nothing left to return
}

TEST_CASE("MUTATE: each rule does what it says and keeps values legal") {
	auto m = std::make_unique<MutateCore>();
	MutateParams in;
	m->generate(in);
	for (int rule = 0; rule < RULE_COUNT; rule++) {
		for (int i = 0; i < 500; i++)
			m->mutate(rule);
		for (int t = 0; t < 8; t++) {
			for (int i = 0; i < m->pat.track[t].length; i++) {
				const MutateStep& s = m->pat.track[t].step[i];
				REQUIRE(s.ratchet >= 1); REQUIRE(s.ratchet <= 8);
				REQUIRE(s.micro >= -0.5f); REQUIRE(s.micro <= 0.5f);
				REQUIRE(s.lockA >= 0.f); REQUIRE(s.lockA <= 1.f);
				REQUIRE(s.lockB >= 0.f); REQUIRE(s.lockB <= 1.f);
			}
		}
	}
	// rotate only moves hits around: the number of hits per track stays the same
	m->generate(in);
	int before[8];
	for (int t = 0; t < 8; t++)
		before[t] = hits(m->pat.track[t]);
	for (int i = 0; i < 100; i++)
		m->mutate(RULE_ROTATE);
	for (int t = 0; t < 8; t++)
		CHECK(hits(m->pat.track[t]) == before[t]);
	// copy keeps the destination's sound locks
	m->generate(in);
	float lock[8][64];
	for (int t = 0; t < 8; t++)
		for (int i = 0; i < 64; i++)
			lock[t][i] = m->pat.track[t].step[i].lockA;
	for (int i = 0; i < 100; i++)
		m->mutate(RULE_COPY);
	bool kept = true;
	for (int t = 0; t < 8; t++)
		for (int i = 0; i < m->pat.track[t].length; i++)
			if (m->pat.track[t].step[i].lockA != lock[t][i])
				kept = false;
	CHECK(kept);
	// the same seed gives the same mutations
	auto a = std::make_unique<MutateCore>(), b = std::make_unique<MutateCore>();
	a->setSeed(9); b->setSeed(9);
	a->generate(in); b->generate(in);
	in.amount = 1.f; in.drift = 0.7f;
	for (int i = 0; i < 50; i++) {
		a->mutatePass(in);
		b->mutatePass(in);
	}
	CHECK(samePattern(a->pat, b->pat));
}

TEST_CASE("MUTATE: steps land on the grid and tracks of different lengths drift against each other") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	fillTrack(*m, 0, 16);
	m->setLength(1, 3);
	m->pat.track[1].step[0].on = true; // one hit every 3 steps
	MutateParams in;
	// bar = 2 s, 16 steps: one step every 6000 samples
	std::vector<int> a = play(*m, in, 0, 96000 * 2, 0.5);
	REQUIRE(a.size() == 32);
	for (size_t i = 0; i < a.size(); i++)
		CHECK(std::abs(a[i] - (int)i * 6000) <= 1);
	m->reset();
	std::vector<int> b = play(*m, in, 1, 96000 * 2, 0.5);
	REQUIRE(b.size() == 11); // steps 0, 3, 6 ... 30
	for (size_t i = 0; i < b.size(); i++)
		CHECK(std::abs(b[i] - (int)i * 18000) <= 1);
}

TEST_CASE("MUTATE: ratchets are evenly spaced at curve 0, and speed up or slow down with the curve") {
	for (float curve : {0.f, -1.f, 1.f}) {
		auto m = std::make_unique<MutateCore>();
		clearAll(*m);
		m->setLength(0, 16);
		m->pat.track[0].step[0].on = true;
		m->pat.track[0].step[0].ratchet = 4;
		MutateParams in;
		in.ratchetCurve = curve;
		std::vector<int> w = play(*m, in, 0, 12000, 0.5); // step = 6000 samples
		REQUIRE(w.size() == 4);
		int d1 = w[1] - w[0], d2 = w[2] - w[1], d3 = w[3] - w[2];
		if (curve == 0.f) {
			CHECK(std::abs(d1 - 1500) <= 1);
			CHECK(std::abs(d2 - 1500) <= 1);
			CHECK(std::abs(d3 - 1500) <= 1);
		}
		else if (curve < 0.f) {
			CHECK(d1 > d2);
			CHECK(d2 > d3); // accelerating
		}
		else {
			CHECK(d1 < d2);
			CHECK(d2 < d3); // decelerating
		}
		CHECK(w[3] < 6000); // all inside the step
	}
}

TEST_CASE("MUTATE: ratchet hits get quieter, accents fire above 0.8, locks are held per hit") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	m->setLength(0, 16);
	MutateStep& s = m->pat.track[0].step[0];
	s.on = true; s.ratchet = 3; s.vel = 1.f; s.lockA = 0.3f; s.lockB = 0.9f;
	MutateParams in;
	in.ratchetDecay = 0.5f;
	PhaseReader reader;
	MutateOut out;
	std::vector<float> vels;
	int accents = 0;
	for (int i = 0; i < 6000; i++) {
		reader.process(testutil::ramp(0.5, 48000.0, i));
		m->process(in, reader.crossed(16.0), reader.W * 16.0, false, out);
		if (out.gate[0])
			vels.push_back(out.vel[0]);
		if (out.accent)
			accents++;
	}
	REQUIRE(vels.size() == 3);
	CHECK(vels[0] == doctest::Approx(1.f));
	CHECK(vels[1] == doctest::Approx(0.5f));
	CHECK(vels[2] == doctest::Approx(0.25f));
	CHECK(accents == 1);
	CHECK(out.lockA[0] == doctest::Approx(0.3f));
	CHECK(out.lockB[0] == doctest::Approx(0.9f));
}

TEST_CASE("MUTATE: micro timing and swing move hits early and late") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	m->setLength(0, 4);
	for (int i = 0; i < 4; i++)
		m->pat.track[0].step[i].on = true;
	m->pat.track[0].step[1].micro = 0.25f;  // late
	m->pat.track[0].step[2].micro = -0.25f; // early: must be started from the step before
	MutateParams in;
	std::vector<int> w = play(*m, in, 0, 24000, 0.5);
	REQUIRE(w.size() == 4);
	CHECK(std::abs(w[0] - 0) <= 1);
	CHECK(std::abs(w[1] - 7500) <= 1);
	CHECK(std::abs(w[2] - 10500) <= 1);
	CHECK(std::abs(w[3] - 18000) <= 1);

	auto sw = std::make_unique<MutateCore>();
	clearAll(*sw);
	fillTrack(*sw, 0, 4);
	in.swing = 0.5f;
	w = play(*sw, in, 0, 24000, 0.5);
	REQUIRE(w.size() == 4);
	CHECK(std::abs(w[1] - 9000) <= 1); // odd steps half a step late
	CHECK(std::abs(w[2] - 12000) <= 1);
	CHECK(std::abs(w[3] - 21000) <= 1);
}

TEST_CASE("MUTATE: conditions") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	m->setLength(0, 4);
	MutateStep* s = m->pat.track[0].step;
	s[0].on = true; s[0].cond = COND_FIRST;                 // first pass only
	s[1].on = true; s[1].cond = COND_EVERY_N; s[1].n = 2;   // every 2nd pass
	s[2].on = true; s[2].cond = COND_PREV;                  // only if step 1 played
	s[3].on = true; s[3].cond = COND_PROB; s[3].p = 0.5f;   // half the time
	MutateParams in;
	in.steps = 4;
	// 400 passes of 4 steps, 100 samples per step
	std::vector<int> w = play(*m, in, 0, 400 * 400, 48000.0 / 400.0);
	int count[4] = {};
	for (int x : w)
		count[((x + 5) / 100) % 4]++;
	CHECK(count[0] == 1);
	CHECK(count[1] == 200);
	CHECK(count[2] == 200);
	CHECK(count[3] > 150);
	CHECK(count[3] < 250);
	// muted tracks are silent
	m->reset();
	m->setMute(0, true);
	CHECK(play(*m, in, 0, 4000, 120.0).empty());
}

TEST_CASE("MUTATE: a phase running backwards makes no triggers and no trouble") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	fillTrack(*m, 0, 16);
	MutateParams in;
	std::vector<int> fwd = play(*m, in, 0, 96000, 0.5);
	CHECK(fwd.size() == 16);
	std::vector<int> back = play(*m, in, 0, 96000, 0.5, 48000.0, true);
	CHECK(back.size() <= 1); // at most the step it starts on
	// and it carries on normally afterwards
	m->reset();
	CHECK(play(*m, in, 0, 96000, 0.5).size() == 16);
}

TEST_CASE("MUTATE: at a 1 kHz phase with 32 steps no step is missed") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	fillTrack(*m, 0, 32);
	MutateParams in;
	in.steps = 32;
	std::vector<int> w = play(*m, in, 0, 48000, 1000.0);
	CHECK((int)w.size() >= 32000 - 32);
	CHECK((int)w.size() <= 32000);
}

TEST_CASE("MUTATE: mutations happen only on bar lines, every EVERY bars; HOME snaps back") {
	auto m = std::make_unique<MutateCore>();
	MutateParams in;
	m->setSeed(3);
	m->generate(in);
	in.amount = 1.f;
	in.drift = 1.f;
	in.every = 2;
	PhaseReader reader;
	MutateOut out;
	uint32_t v = m->version;
	std::vector<int> changedAtBar;
	int bars = 0;
	for (int i = 0; i < 9 * 9600; i++) { // 9 bars of 9600 samples
		reader.process(testutil::ramp(5.0, 48000.0, i));
		m->process(in, reader.crossed(16.0), reader.W * 16.0, false, out);
		if (out.bar)
			bars++;
		if (m->version != v) {
			v = m->version;
			CHECK(out.bar); // never between bar lines
			changedAtBar.push_back(bars - 1);
		}
	}
	CHECK(bars == 9);
	CHECK(changedAtBar == std::vector<int>({2, 4, 6, 8}));
	CHECK_FALSE(samePattern(m->pat, m->home));
	m->requestHome();
	in.amount = 0.f;
	for (int i = 9 * 9600; i < 10 * 9600 + 10; i++) {
		reader.process(testutil::ramp(5.0, 48000.0, i));
		m->process(in, reader.crossed(16.0), reader.W * 16.0, false, out);
	}
	CHECK(samePattern(m->pat, m->home));
}

TEST_CASE("MUTATE: turning STEPS while running never leaves the tracks silent") {
	auto m = std::make_unique<MutateCore>();
	clearAll(*m);
	fillTrack(*m, 0, 16);
	MutateParams in;
	in.steps = 32;
	PhaseReader reader;
	MutateOut out;
	int hitsAfter = 0;
	for (int i = 0; i < 4 * 96000; i++) {
		if (i == 2 * 96000)
			in.steps = 8; // far fewer steps per bar: the step count drops from 64 to 16
		reader.process(testutil::ramp(0.5, 48000.0, i));
		m->process(in, reader.crossed((double)in.steps), reader.W * in.steps, false, out);
		if (out.gate[0] && i > 2 * 96000 + 10)
			hitsAfter++;
	}
	CHECK(hitsAfter >= 15);
	CHECK(hitsAfter <= 16);
}
