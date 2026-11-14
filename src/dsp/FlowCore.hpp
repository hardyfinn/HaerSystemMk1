// FLOW (spec §13A): a small 2D fluid (Jos Stam's "Stable Fluids") on a 24x24
// grid, used as a source of slow, connected modulation. Plain C++, no Rack.
#pragma once
#include <cstdint>

namespace codex {

struct FlowParams {
	float viscosity = 0.0005f; // 0..0.01: how quickly the motion smooths out and dies
	float diffusion = 0.0002f; // 0..0.01: how quickly the dye spreads
	float vort = 0.3f;         // 0..1: keeps the swirls alive
	float halfLife = 8.f;      // seconds for the dye to fade to half; 0 or less = never fades
	bool wrap = false;         // false: a closed box. true: what leaves one side comes in at the other
};

struct FlowInjector {
	float x = 0.5f, y = 0.5f; // 0..1 across the grid
	float angle = 0.f;        // radians; 0 = to the right, a quarter turn = down the screen
	float force = 0.f;        // 0..1, continuous push
	float dye = 0.f;          // 0..1, continuous dye
	bool puff = false;        // one burst of force and dye
};

struct FlowCore {
	static constexpr int N = 24;
	static constexpr int SIZE = (N + 2) * (N + 2);
	static constexpr int INJECTORS = 4;
	static int at(int i, int j) { return i + (N + 2) * j; }

	float u[SIZE], v[SIZE], d[SIZE];
	// Rounds of the pressure solve per projection. 40 is exact enough for the tests;
	// fewer is fine at high step rates, where each step changes little.
	int iterations = 40;

	FlowCore() { reset(0); }
	// Still fluid with a gentle seeded stir and a little dye, so there is something to read at once.
	void reset(int seed);
	// One step of dt seconds.
	void step(const FlowParams& p, const FlowInjector inj[INJECTORS], float dt);

	// Readings at a point (x, y in 0..1), between cells.
	float density(float x, float y) const { return sample(d, x, y); }
	float velU(float x, float y) const { return sample(u, x, y); }
	float velV(float x, float y) const { return sample(v, x, y); }
	float curl(float x, float y) const;
	// Mean kinetic energy over the grid.
	float energy() const;
	double totalDye() const;
	// Largest divergence in any cell, in Stam's units (what project() works to remove).
	float maxDivergence() const;
	// Makes the velocity field free of sources and sinks.
	void project();
	bool finite() const;
	// Replaces the whole fluid. Each array is N x N values, row by row (the inside cells only).
	void load(const float* nu, const float* nv, const float* nd);

private:
	float u0[SIZE], v0[SIZE], d0[SIZE], w[SIZE];
	float pr[(N + 4) * (N + 4)]; // pressure, kept from step to step; two extra rings of cells
	static int pat(int i, int j) { return (i + 1) + (N + 4) * (j + 1); }
	bool wrap = false;

	float sample(const float* f, float x, float y) const;
	void bounds(int b, float* x) const;
	void solve(int b, float* x, const float* x0, float a, float c, int iterations) const;
	void diffuse(int b, float* x, const float* x0, float rate, float dt) const;
	void advect(int b, float* out, const float* in, const float* uu, const float* vv, float dt) const;
	void confine(float strength, float dt);
	void inject(const FlowInjector& in, float dt);
};

} // namespace codex
