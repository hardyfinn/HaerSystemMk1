#include "doctest.h"
#include "dsp/ChainCore.hpp"
#include <vector>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

using namespace codex;

static const bool ALL[4] = {true, true, true, true};
static const bool ONE[4] = {true, false, false, false};
static const bool TWO[4] = {true, true, false, false};

static int mod12(int x) { return ((x % 12) + 12) % 12; }

TEST_CASE("CHAIN: with SCALE LOCK at 1 every note is in the scale (100,000 steps)") {
	for (int sc : {1, 7, 9}) {
		for (int root : {0, 5, 11}) {
			ChainCore core;
			ChainParams p;
			p.scale = ChainCore::SCALES[sc];
			p.root = root;
			p.character[0] = 0.f; p.character[1] = 0.4f; p.character[2] = 0.7f; p.character[3] = 1.f;
			p.strength[ChainParams::NO_PARALLELS] = 0.5f;
			p.strength[ChainParams::KEEP_INTERVAL] = 0.5f;
			p.strength[ChainParams::CONTRARY] = 0.5f;
			p.strength[ChainParams::MAX_LEAP] = 0.5f;
			p.strength[ChainParams::HOME_PULL] = 0.5f;
			core.setSeed(sc * 13 + root);
			core.reset(p);
			ChainOut out;
			int outside = 0, outOfRange = 0;
			for (int i = 0; i < 100000 / 9 + 1; i++) {
				core.step(p, ALL, -1, -1, out);
				for (int v = 0; v < 4; v++) {
					if (!ChainCore::inScale(p.scale, p.root, core.voice[v].note))
						outside++;
					if (std::abs(core.voice[v].note) > 12)
						outOfRange++;
				}
			}
			CHECK(outside == 0);
			CHECK(outOfRange == 0); // RANGE 2 octaves round C4
		}
	}
}

TEST_CASE("CHAIN: with MAX LEAP at 2 no leap is more than 2 semitones") {
	for (float character : {0.f, 0.5f, 1.f}) {
		ChainCore core;
		ChainParams p;
		p.character[0] = character;
		p.strength[ChainParams::MAX_LEAP] = 1.f;
		p.leap = 2;
		core.setSeed(3);
		core.reset(p);
		ChainOut out;
		int tooFar = 0, moved = 0;
		for (int i = 0; i < 20000; i++) {
			int was = core.voice[0].note;
			core.step(p, ONE, -1, -1, out);
			if (std::abs(core.voice[0].note - was) > 2)
				tooFar++;
			if (core.voice[0].note != was)
				moved++;
		}
		CHECK(tooFar == 0);
		CHECK(moved > 5000); // and it has not simply frozen
	}
}

TEST_CASE("CHAIN: with no rules the moves follow the table (chi-squared, p > 0.01)") {
	ChainCore core;
	ChainParams p;
	p.scale = ChainCore::SCALES[1];
	p.strength[ChainParams::SCALE_LOCK] = 0.f;
	p.character[0] = 0.3f;
	p.range[0] = 4;
	core.setSeed(11);
	core.reset(p);
	const float (*m)[12] = core.activeMatrix(0);
	std::vector<double> count(144, 0.0);
	ChainOut out;
	for (int i = 0; i < 10000; i++) {
		int from = mod12(core.voice[0].note);
		core.step(p, ONE, -1, -1, out);
		count[(size_t)(from * 12 + mod12(core.voice[0].note))] += 1.0;
	}
	double chi = 0.0;
	int dof = 0;
	for (int i = 0; i < 12; i++) {
		double rowN = 0.0, rowW = 0.0;
		for (int j = 0; j < 12; j++) {
			rowN += count[(size_t)(i * 12 + j)];
			rowW += m[i][j];
		}
		if (rowN < 50)
			continue;
		int cells = 0;
		for (int j = 0; j < 12; j++) {
			double expect = rowN * m[i][j] / rowW;
			if (expect < 5.0)
				continue;
			double d = count[(size_t)(i * 12 + j)] - expect;
			chi += d * d / expect;
			cells++;
		}
		dof += cells - 1;
	}
	REQUIRE(dof > 50);
	// the 1 % point of chi-squared with `dof` degrees of freedom (Wilson-Hilferty)
	double k = (double)dof;
	double crit = k * std::pow(1.0 - 2.0 / (9.0 * k) + 2.326 * std::sqrt(2.0 / (9.0 * k)), 3.0);
	MESSAGE("chi-squared " << chi << " with " << dof << " degrees of freedom; limit " << crit);
	CHECK(chi < crit);
}

TEST_CASE("CHAIN: CHARACTER 0 moves by step, CHARACTER 1 leaps") {
	double mean[2];
	int k = 0;
	for (float character : {0.f, 1.f}) {
		ChainCore core;
		ChainParams p;
		p.character[0] = character;
		p.range[0] = 3;
		core.reset(p);
		ChainOut out;
		double sum = 0.0;
		int small = 0;
		for (int i = 0; i < 5000; i++) {
			int was = core.voice[0].note;
			core.step(p, ONE, -1, -1, out);
			int d = std::abs(core.voice[0].note - was);
			sum += d;
			if (d <= 2)
				small++;
		}
		mean[k++] = sum / 5000.0;
		if (character == 0.f)
			CHECK(small > 3500); // mostly seconds
	}
	CHECK(mean[0] < 2.5);
	CHECK(mean[1] > 5.0);
}

TEST_CASE("CHAIN: learning C-E-G makes C to E, E to G and G to C dominate") {
	ChainCore core;
	ChainParams p;
	core.reset(p);
	ChainOut out;
	const int seq[3] = {0, 4, 7};
	for (int i = 0; i < 60; i++)
		core.step(p, ONE, 0, seq[i % 3], out);
	core.stopLearning(0);
	CHECK(core.voice[0].custom);
	const float (*m)[12] = core.activeMatrix(0);
	for (int i = 0; i < 12; i++)
		for (int j = 0; j < 12; j++) {
			bool wanted = (i == 0 && j == 4) || (i == 4 && j == 7) || (i == 7 && j == 0);
			if (wanted)
				CHECK(m[i][j] > 0.9f);
			else
				CHECK(m[i][j] == 0.f);
		}
	// and the voice now plays it: once on C, E or G it cycles C E G for ever
	int good = 0;
	for (int i = 0; i < 300; i++) {
		int was = mod12(core.voice[0].note);
		core.step(p, ONE, -1, -1, out);
		int now = mod12(core.voice[0].note);
		if ((was == 0 && now == 4) || (was == 4 && now == 7) || (was == 7 && now == 0))
			good++;
	}
	CHECK(good >= 295);
	// back to the CHARACTER table
	core.resetMatrix(0);
	CHECK_FALSE(core.voice[0].custom);
}

TEST_CASE("CHAIN: order 2 remembers two notes back") {
	// C D C E repeated: after C comes D or E, depending on what came before the C
	ChainCore core;
	ChainParams p;
	p.order = 2;
	p.scale = 0xFFF;
	core.reset(p);
	ChainOut out;
	const int seq[4] = {0, 2, 0, 4};
	for (int i = 0; i < 80; i++)
		core.step(p, ONE, 0, seq[i % 4], out);
	core.stopLearning(0);
	int right = 0, total = 0;
	int prev = -1;
	for (int i = 0; i < 400; i++) {
		int was = mod12(core.voice[0].note);
		core.step(p, ONE, -1, -1, out);
		int now = mod12(core.voice[0].note);
		if (was == 0 && (prev == 2 || prev == 4)) {
			total++;
			if ((prev == 2 && now == 4) || (prev == 4 && now == 2))
				right++;
		}
		prev = was;
	}
	CHECK(total > 50);
	CHECK(right == total);
	// with order 1 the same table cannot tell the two Cs apart
	p.order = 1;
	right = total = 0;
	prev = -1;
	for (int i = 0; i < 400; i++) {
		int was = mod12(core.voice[0].note);
		core.step(p, ONE, -1, -1, out);
		int now = mod12(core.voice[0].note);
		if (was == 0 && (prev == 2 || prev == 4)) {
			total++;
			if ((prev == 2 && now == 4) || (prev == 4 && now == 2))
				right++;
		}
		prev = was;
	}
	CHECK(right < total);
}

TEST_CASE("CHAIN: same seed, same melody; another seed, another melody") {
	auto run = [](int seed) {
		ChainCore core;
		ChainParams p;
		p.strength[ChainParams::KEEP_INTERVAL] = 0.6f;
		p.strength[ChainParams::HOME_PULL] = 0.3f;
		core.setSeed(seed);
		core.reset(p);
		ChainOut out;
		std::vector<int> notes;
		for (int i = 0; i < 500; i++) {
			core.step(p, ALL, -1, -1, out);
			for (int v = 0; v < 4; v++)
				notes.push_back(core.voice[v].note);
		}
		return notes;
	};
	CHECK(run(42) == run(42));
	CHECK(run(42) != run(43));
	// RESET rewinds to the same melody
	ChainCore core;
	ChainParams p;
	core.setSeed(7);
	core.reset(p);
	ChainOut out;
	std::vector<int> a, b;
	for (int i = 0; i < 100; i++) {
		core.step(p, ALL, -1, -1, out);
		a.push_back(core.voice[2].note);
	}
	core.reset(p);
	for (int i = 0; i < 100; i++) {
		core.step(p, ALL, -1, -1, out);
		b.push_back(core.voice[2].note);
	}
	CHECK(a == b);
}

TEST_CASE("CHAIN: KEEP INTERVAL at 1 holds thirds and sixths against voice 1") {
	ChainCore core;
	ChainParams p;
	p.strength[ChainParams::KEEP_INTERVAL] = 1.f;
	p.character[0] = 0.2f;
	p.character[1] = 0.6f;
	core.reset(p);
	ChainOut out;
	int good = 0, violations = 0, moved1 = 0, moved2 = 0, together = 0;
	for (int i = 0; i < 4000; i++) {
		int a = core.voice[0].note, b = core.voice[1].note;
		core.step(p, TWO, -1, -1, out);
		int iv = std::abs(core.voice[1].note - core.voice[0].note) % 12;
		if (iv == 3 || iv == 4 || iv == 8 || iv == 9)
			good++;
		violations += out.violation ? 1 : 0;
		int da = core.voice[0].note - a, db = core.voice[1].note - b;
		moved1 += da != 0;
		moved2 += db != 0;
		together += (da == db);
	}
	CHECK(good == 4000);
	CHECK(violations == 0);
	CHECK(moved1 > 2000);
	CHECK(moved2 > 2000);
	CHECK(together < 2000); // not simply shadowing voice 1
}

TEST_CASE("CHAIN: HOME PULL brings the melody back to the root") {
	auto rootShare = [](float pull) {
		ChainCore core;
		ChainParams p;
		p.root = 2;
		p.strength[ChainParams::HOME_PULL] = pull;
		p.homeSteps = 8;
		core.reset(p);
		ChainOut out;
		int roots = 0, longest = 0, run = 0;
		for (int i = 0; i < 5000; i++) {
			core.step(p, ONE, -1, -1, out);
			if (mod12(core.voice[0].note - 2) == 0) {
				roots++;
				run = 0;
			}
			else
				longest = std::max(longest, ++run);
		}
		return std::make_pair(roots, longest);
	};
	auto none = rootShare(0.f), some = rootShare(0.7f), full = rootShare(1.f);
	CHECK(some.first > none.first * 3 / 2);
	CHECK(full.second <= 8); // at full strength it is certain by HOME STEPS
}

TEST_CASE("CHAIN: NO PARALLELS removes parallel fifths and octaves; CONTRARY opposes voice 1") {
	auto count = [](float noPar, float contrary, int& same) {
		ChainCore core;
		ChainParams p;
		p.strength[ChainParams::NO_PARALLELS] = noPar;
		p.strength[ChainParams::CONTRARY] = contrary;
		core.setSeed(5);
		core.reset(p);
		ChainOut out;
		int parallels = 0;
		same = 0;
		for (int i = 0; i < 5000; i++) {
			int a = core.voice[0].note, b = core.voice[1].note;
			int lastMove1 = core.voice[0].lastMove;
			core.step(p, TWO, -1, -1, out);
			int da = core.voice[0].note - a, db = core.voice[1].note - b;
			int was = std::abs(b - a) % 12, now = std::abs(core.voice[1].note - core.voice[0].note) % 12;
			if (da != 0 && db != 0 && (da > 0) == (db > 0) && was == now && (now == 0 || now == 7))
				parallels++;
			(void)lastMove1;
			if (da != 0 && db != 0 && (da > 0) == (db > 0))
				same++;
		}
		return parallels;
	};
	int sameFree, sameRule, tmp;
	int free = count(0.f, 0.f, sameFree);
	CHECK(free > 20);
	CHECK(count(1.f, 0.f, tmp) == 0);
	count(0.f, 1.f, sameRule);
	CHECK(sameRule == 0);
	CHECK(sameFree > 500);
}

TEST_CASE("CHAIN: impossible rules fire VIOLATION and still give the least bad note") {
	ChainCore core;
	ChainParams p;
	p.scale = 1; // only the root is allowed
	p.strength[ChainParams::KEEP_INTERVAL] = 0.9f; // but voice 2 must sit a third or sixth from voice 1
	core.reset(p);
	ChainOut out;
	int violations = 0, offScale = 0;
	for (int i = 0; i < 500; i++) {
		core.step(p, TWO, -1, -1, out);
		violations += out.violation ? 1 : 0;
		offScale += mod12(core.voice[1].note) != 0;
	}
	CHECK(violations > 300);
	CHECK(offScale == 0); // SCALE LOCK is the stronger rule, so it wins
}

TEST_CASE("CHAIN: painting a table cell changes what the voice plays") {
	ChainCore core;
	ChainParams p;
	p.scale = 0xFFF;
	core.reset(p);
	for (int i = 0; i < 12; i++)
		for (int j = 0; j < 12; j++)
			core.paint(0, i, j, j == (i + 7) % 12 ? 1.f : 0.f); // always up a fifth
	ChainOut out;
	for (int i = 0; i < 50; i++) {
		int was = mod12(core.voice[0].note);
		core.step(p, ONE, -1, -1, out);
		CHECK(mod12(core.voice[0].note) == (was + 7) % 12);
	}
}
