#include "doctest.h"
#include "dsp/FlowCore.hpp"
#include "common/Rng.hpp"
#include <chrono>
#include <cmath>
#include <cstring>

using namespace codex;

TEST_CASE("FLOW: after project() the divergence over the grid is below 1e-3") {
	FlowCore f;
	Rng rng(3);
	// a rough random velocity field, full of sources and sinks
	for (int k = 0; k < FlowCore::SIZE; k++) {
		f.u[k] = rng.bipolar();
		f.v[k] = rng.bipolar();
	}
	CHECK(f.maxDivergence() > 0.01f);
	f.project();
	CHECK(f.maxDivergence() < 1e-3f);
	// and after ordinary steps with puffs, in both kinds of box
	for (int wrap = 0; wrap < 2; wrap++) {
		FlowCore g;
		FlowParams p;
		p.wrap = wrap == 1;
		FlowInjector inj[4];
		inj[0].puff = true;
		inj[1].x = 0.9f;
		inj[1].angle = 2.f;
		inj[1].puff = true;
		g.step(p, inj, 1.f / 60.f);
		inj[0].puff = inj[1].puff = false;
		for (int i = 0; i < 30; i++)
			g.step(p, inj, 1.f / 60.f);
		CHECK(g.maxDivergence() < 1e-3f);
	}
}

TEST_CASE("FLOW: a puff with no viscosity and no decay keeps its dye within 5 % over 100 steps in a closed box") {
	FlowCore f;
	FlowParams p;
	p.viscosity = 0.f;
	p.halfLife = 0.f; // never fades
	FlowInjector inj[4];
	inj[0].x = 0.3f;
	inj[0].y = 0.6f;
	inj[0].angle = 0.4f;
	inj[0].puff = true;
	f.step(p, inj, 1.f / 60.f);
	inj[0].puff = false;
	double start = f.totalDye();
	float e0 = f.energy();
	CHECK(start > 1.0);
	CHECK(e0 > 0.f);
	float moved = 0.f;
	float d0 = f.density(0.3f, 0.6f);
	for (int i = 0; i < 100; i++)
		f.step(p, inj, 1.f / 60.f);
	moved = std::fabs(f.density(0.3f, 0.6f) - d0);
	CHECK(f.totalDye() == doctest::Approx(start).epsilon(0.05));
	CHECK(moved > 0.01f); // the dye did go somewhere
}

TEST_CASE("FLOW: DECAY halves the dye in its half-life; VISCOSITY and VORT change how long the motion lasts") {
	FlowCore f;
	FlowParams p;
	p.halfLife = 2.f;
	FlowInjector inj[4];
	double start = f.totalDye();
	for (int i = 0; i < 120; i++)
		f.step(p, inj, 1.f / 60.f);
	CHECK(f.totalDye() == doctest::Approx(start * 0.5).epsilon(0.02));

	auto energyAfter = [](float visc, float vort) {
		FlowCore g;
		FlowParams q;
		q.viscosity = visc;
		q.vort = vort;
		FlowInjector in[4];
		in[0].puff = true;
		in[0].angle = 1.f;
		g.step(q, in, 1.f / 60.f);
		in[0].puff = false;
		for (int i = 0; i < 180; i++)
			g.step(q, in, 1.f / 60.f);
		return g.energy();
	};
	float thin = energyAfter(0.f, 0.f), thick = energyAfter(0.01f, 0.f), swirly = energyAfter(0.f, 1.f);
	CHECK(thick < thin * 0.8f);
	CHECK(swirly > thin * 1.1f);
}

TEST_CASE("FLOW: ten minutes of random puffs give no NaN and stay bounded") {
	FlowCore f;
	FlowParams p;
	p.vort = 1.f;
	p.viscosity = 0.f;
	Rng rng(9);
	FlowInjector inj[4];
	bool ok = true;
	float worstSpeed = 0.f, worstDye = 0.f;
	double spent = 0.0;
	const int STEPS = 10 * 60 * 60;
	for (int i = 0; i < STEPS; i++) {
		for (int k = 0; k < 4; k++) {
			inj[k].x = rng.uniform();
			inj[k].y = rng.uniform();
			inj[k].angle = rng.uniform() * 6.283f;
			inj[k].force = rng.uniform();
			inj[k].dye = rng.uniform();
			inj[k].puff = rng.uniform() < 0.05f;
		}
		p.wrap = (i / 6000) & 1;
		auto t0 = std::chrono::steady_clock::now();
		f.step(p, inj, (1.f / 60.f) * (0.1f + 3.9f * rng.uniform()));
		spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		if (i % 97 == 0) {
			ok = ok && f.finite();
			for (int c = 0; c < FlowCore::SIZE; c++) {
				worstSpeed = std::fmax(worstSpeed, std::fmax(std::fabs(f.u[c]), std::fabs(f.v[c])));
				worstDye = std::fmax(worstDye, f.d[c]);
			}
		}
	}
	CHECK(ok);
	CHECK(worstSpeed <= 20.f);
	CHECK(worstDye <= 100.f);
	MESSAGE("one step takes " << spent / STEPS * 1e6 << " microseconds (in this slow test build)");
}

TEST_CASE("FLOW: the same seed and the same inputs give the same outputs") {
	auto run = [](int seed) {
		FlowCore f;
		f.reset(seed);
		FlowParams p;
		FlowInjector inj[4];
		for (int i = 0; i < 200; i++) {
			inj[1].puff = i % 40 == 0;
			inj[1].x = 0.2f + 0.003f * (float)i;
			f.step(p, inj, 1.f / 60.f);
		}
		return f;
	};
	FlowCore a = run(4), b = run(4), c = run(5);
	CHECK(std::memcmp(a.u, b.u, sizeof(a.u)) == 0);
	CHECK(std::memcmp(a.v, b.v, sizeof(a.v)) == 0);
	CHECK(std::memcmp(a.d, b.d, sizeof(a.d)) == 0);
	CHECK(std::memcmp(a.d, c.d, sizeof(a.d)) != 0);
	// a fresh field has something to read straight away, and readings between cells are smooth
	FlowCore f;
	f.reset(1);
	CHECK(f.energy() > 1e-4f);
	CHECK(f.totalDye() > 10.0);
	CHECK(f.maxDivergence() < 1e-3f);
	float last = f.density(0.f, 0.37f), jump = 0.f;
	for (int i = 1; i <= 1000; i++) {
		float now = f.density((float)i / 1000.f, 0.37f);
		jump = std::fmax(jump, std::fabs(now - last));
		last = now;
	}
	CHECK(jump < 0.01f);
}
