#include "dsp/FlowCore.hpp"
#include "common/Rng.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace codex {

namespace {
const float PI = 3.14159265f;
const float DYE_RATE = 1.2f;   // dye per second in the middle of an injector at DYE 1
const float FORCE_RATE = 3.6f; // speed gained per second at FORCE 1 (1 = across the grid in a second)
const float PUFF = 50.f / 60.f; // a puff is 50 ordinary steps' worth at once
const float MAX_SPEED = 20.f;
} // namespace

void FlowCore::reset(int seed) {
	Rng rng((uint64_t)seed);
	Rng r = rng.fork("flow");
	// a few broad waves with seeded directions and phases
	float ax[3], ay[3], ph[3], amp[3];
	for (int k = 0; k < 3; k++) {
		ax[k] = (float)(1 + (int)(r.uniform() * 2.99f)) * (r.uniform() < 0.5f ? -1.f : 1.f);
		ay[k] = (float)(1 + (int)(r.uniform() * 2.99f)) * (r.uniform() < 0.5f ? -1.f : 1.f);
		ph[k] = r.uniform() * 2.f * PI;
		amp[k] = 0.15f + 0.15f * r.uniform();
	}
	for (int j = 0; j <= N + 1; j++)
		for (int i = 0; i <= N + 1; i++) {
			float x = ((float)i - 0.5f) / (float)N, y = ((float)j - 0.5f) / (float)N;
			float uu = 0.f, vv = 0.f, dd = 0.f;
			for (int k = 0; k < 3; k++) {
				float a = PI * (ax[k] * x + ay[k] * y) + ph[k];
				// velocity along the wave fronts, so it starts nearly free of sources
				uu += amp[k] * ay[k] * std::sin(a) * 0.5f;
				vv -= amp[k] * ax[k] * std::sin(a) * 0.5f;
				dd += 0.08f * std::cos(a * 0.7f + (float)k);
			}
			u[at(i, j)] = uu;
			v[at(i, j)] = vv;
			d[at(i, j)] = std::max(0.f, 0.15f + dd);
		}
	std::memset(u0, 0, sizeof(u0));
	std::memset(v0, 0, sizeof(v0));
	std::memset(d0, 0, sizeof(d0));
	std::memset(w, 0, sizeof(w));
	std::memset(pr, 0, sizeof(pr));
	wrap = false;
	bounds(1, u);
	bounds(2, v);
	bounds(0, d);
	project();
}

// Edge cells. b = 1: the field is a left-right speed (it turns back at the side walls);
// b = 2: an up-down speed; b = 0: anything else.
void FlowCore::bounds(int b, float* x) const {
	if (wrap) {
		for (int i = 1; i <= N; i++) {
			x[at(0, i)] = x[at(N, i)];
			x[at(N + 1, i)] = x[at(1, i)];
			x[at(i, 0)] = x[at(i, N)];
			x[at(i, N + 1)] = x[at(i, 1)];
		}
		x[at(0, 0)] = x[at(N, N)];
		x[at(0, N + 1)] = x[at(N, 1)];
		x[at(N + 1, 0)] = x[at(1, N)];
		x[at(N + 1, N + 1)] = x[at(1, 1)];
		return;
	}
	for (int i = 1; i <= N; i++) {
		x[at(0, i)] = b == 1 ? -x[at(1, i)] : x[at(1, i)];
		x[at(N + 1, i)] = b == 1 ? -x[at(N, i)] : x[at(N, i)];
		x[at(i, 0)] = b == 2 ? -x[at(i, 1)] : x[at(i, 1)];
		x[at(i, N + 1)] = b == 2 ? -x[at(i, N)] : x[at(i, N)];
	}
	x[at(0, 0)] = 0.5f * (x[at(1, 0)] + x[at(0, 1)]);
	x[at(0, N + 1)] = 0.5f * (x[at(1, N + 1)] + x[at(0, N)]);
	x[at(N + 1, 0)] = 0.5f * (x[at(N, 0)] + x[at(N + 1, 1)]);
	x[at(N + 1, N + 1)] = 0.5f * (x[at(N, N + 1)] + x[at(N + 1, N)]);
}

// Gauss-Seidel relaxation.
void FlowCore::solve(int b, float* x, const float* x0, float a, float c, int iterations) const {
	float inv = 1.f / c;
	for (int k = 0; k < iterations; k++) {
		for (int j = 1; j <= N; j++)
			for (int i = 1; i <= N; i++)
				x[at(i, j)] = (x0[at(i, j)] + a * (x[at(i - 1, j)] + x[at(i + 1, j)] + x[at(i, j - 1)] + x[at(i, j + 1)])) * inv;
		bounds(b, x);
	}
}

void FlowCore::diffuse(int b, float* x, const float* x0, float rate, float dt) const {
	float a = dt * rate * (float)(N * N);
	std::memcpy(x, x0, sizeof(float) * SIZE);
	if (a <= 0.f)
		return;
	solve(b, x, x0, a, 1.f + 4.f * a, 10);
}

void FlowCore::advect(int b, float* out, const float* in, const float* uu, const float* vv, float dt) const {
	float dt0 = dt * (float)N;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++) {
			// where did the fluid in this cell come from?
			float x = (float)i - dt0 * uu[at(i, j)];
			float y = (float)j - dt0 * vv[at(i, j)];
			if (wrap) {
				x -= (float)N * std::floor((x - 0.5f) / (float)N);
				y -= (float)N * std::floor((y - 0.5f) / (float)N);
			}
			x = std::min(std::max(x, 0.5f), (float)N + 0.5f);
			y = std::min(std::max(y, 0.5f), (float)N + 0.5f);
			int i0 = (int)x, j0 = (int)y;
			float s1 = x - (float)i0, t1 = y - (float)j0;
			out[at(i, j)] = (1.f - s1) * ((1.f - t1) * in[at(i0, j0)] + t1 * in[at(i0, j0 + 1)]) + s1 * ((1.f - t1) * in[at(i0 + 1, j0)] + t1 * in[at(i0 + 1, j0 + 1)]);
		}
	bounds(b, out);
}

// Stam's projection, with one change: the pressure is solved on the stencil that matches the
// way divergence is measured (two cells apart), so the result really is free of divergence
// instead of only nearly. The pressure array has two extra rings of cells round the edge for that.
void FlowCore::project() {
	float* div = v0;
	float h = 1.f / (float)N;
	bounds(1, u);
	bounds(2, v);
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++)
			div[at(i, j)] = -0.5f * h * (u[at(i + 1, j)] - u[at(i - 1, j)] + v[at(i, j + 1)] - v[at(i, j - 1)]);
	auto edges = [this]() {
		for (int k = 1; k <= N; k++) {
			if (wrap) {
				pr[pat(0, k)] = pr[pat(N, k)];
				pr[pat(-1, k)] = pr[pat(N - 1, k)];
				pr[pat(N + 1, k)] = pr[pat(1, k)];
				pr[pat(N + 2, k)] = pr[pat(2, k)];
				pr[pat(k, 0)] = pr[pat(k, N)];
				pr[pat(k, -1)] = pr[pat(k, N - 1)];
				pr[pat(k, N + 1)] = pr[pat(k, 1)];
				pr[pat(k, N + 2)] = pr[pat(k, 2)];
			}
			else {
				// a wall is a mirror
				pr[pat(0, k)] = pr[pat(1, k)];
				pr[pat(-1, k)] = pr[pat(2, k)];
				pr[pat(N + 1, k)] = pr[pat(N, k)];
				pr[pat(N + 2, k)] = pr[pat(N - 1, k)];
				pr[pat(k, 0)] = pr[pat(k, 1)];
				pr[pat(k, -1)] = pr[pat(k, 2)];
				pr[pat(k, N + 1)] = pr[pat(k, N)];
				pr[pat(k, N + 2)] = pr[pat(k, N - 1)];
			}
		}
	};
	edges();
	// Relax with a little over-shoot each time round, starting from the last step's answer.
	const float omega = 1.6f;
	int rounds = std::min(std::max(iterations, 4), 80);
	for (int k = 0; k < rounds; k++) {
		for (int j = 1; j <= N; j++)
			for (int i = 1; i <= N; i++) {
				float g = div[at(i, j)] + 0.25f * (pr[pat(i - 2, j)] + pr[pat(i + 2, j)] + pr[pat(i, j - 2)] + pr[pat(i, j + 2)]);
				pr[pat(i, j)] += omega * (g - pr[pat(i, j)]);
			}
		edges();
	}
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++) {
			u[at(i, j)] -= 0.5f * (pr[pat(i + 1, j)] - pr[pat(i - 1, j)]) / h;
			v[at(i, j)] -= 0.5f * (pr[pat(i, j + 1)] - pr[pat(i, j - 1)]) / h;
		}
	bounds(1, u);
	bounds(2, v);
}

void FlowCore::load(const float* nu, const float* nv, const float* nd) {
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++) {
			int k = (i - 1) + N * (j - 1);
			u[at(i, j)] = std::isfinite(nu[k]) ? std::min(std::max(nu[k], -MAX_SPEED), MAX_SPEED) : 0.f;
			v[at(i, j)] = std::isfinite(nv[k]) ? std::min(std::max(nv[k], -MAX_SPEED), MAX_SPEED) : 0.f;
			d[at(i, j)] = std::isfinite(nd[k]) ? std::min(std::max(nd[k], 0.f), 100.f) : 0.f;
		}
	std::memset(pr, 0, sizeof(pr));
	bounds(1, u);
	bounds(2, v);
	bounds(0, d);
}

float FlowCore::maxDivergence() const {
	float h = 1.f / (float)N, worst = 0.f;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++)
			worst = std::max(worst, std::fabs(0.5f * h * (u[at(i + 1, j)] - u[at(i - 1, j)] + v[at(i, j + 1)] - v[at(i, j - 1)])));
	return worst;
}

// Vorticity confinement: numerical smoothing eats small swirls; this pushes back
// around each one to keep it turning.
void FlowCore::confine(float strength, float dt) {
	if (strength <= 0.f)
		return;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++)
			w[at(i, j)] = 0.5f * (v[at(i + 1, j)] - v[at(i - 1, j)] - u[at(i, j + 1)] + u[at(i, j - 1)]);
	bounds(0, w);
	float eps = strength * 6.f;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++) {
			float gx = 0.5f * (std::fabs(w[at(i + 1, j)]) - std::fabs(w[at(i - 1, j)]));
			float gy = 0.5f * (std::fabs(w[at(i, j + 1)]) - std::fabs(w[at(i, j - 1)]));
			float len = std::sqrt(gx * gx + gy * gy) + 1e-6f;
			gx /= len;
			gy /= len;
			u[at(i, j)] += dt * eps * gy * w[at(i, j)];
			v[at(i, j)] -= dt * eps * gx * w[at(i, j)];
		}
}

void FlowCore::inject(const FlowInjector& in, float dt) {
	float amountF = FORCE_RATE * (std::min(std::max(in.force, 0.f), 1.f) * dt + (in.puff ? PUFF : 0.f));
	float amountD = DYE_RATE * (std::min(std::max(in.dye, 0.f), 1.f) * dt + (in.puff ? PUFF : 0.f));
	if (amountF <= 0.f && amountD <= 0.f)
		return;
	float cx = std::min(std::max(in.x, 0.f), 1.f) * (float)N + 0.5f;
	float cy = std::min(std::max(in.y, 0.f), 1.f) * (float)N + 0.5f;
	float fx = std::cos(in.angle) * amountF, fy = std::sin(in.angle) * amountF;
	int i0 = (int)cx, j0 = (int)cy;
	// a soft blob, radius 1.5 cells
	for (int j = j0 - 4; j <= j0 + 5; j++)
		for (int i = i0 - 4; i <= i0 + 5; i++) {
			int ii = i, jj = j;
			if (wrap) {
				ii = ((i - 1) % N + N) % N + 1;
				jj = ((j - 1) % N + N) % N + 1;
			}
			else if (i < 1 || i > N || j < 1 || j > N)
				continue;
			float dx = (float)i - cx, dy = (float)j - cy;
			float g = std::exp(-(dx * dx + dy * dy) / (2.f * 1.5f * 1.5f));
			u[at(ii, jj)] += fx * g;
			v[at(ii, jj)] += fy * g;
			d[at(ii, jj)] += amountD * g;
		}
}

void FlowCore::step(const FlowParams& p, const FlowInjector inj[INJECTORS], float dt) {
	if (!(dt > 0.f))
		return;
	wrap = p.wrap;
	// add sources
	for (int k = 0; k < INJECTORS; k++)
		inject(inj[k], dt);
	confine(std::min(std::max(p.vort, 0.f), 1.f), dt);
	for (int k = 0; k < SIZE; k++) {
		u[k] = std::min(std::max(u[k], -MAX_SPEED), MAX_SPEED);
		v[k] = std::min(std::max(v[k], -MAX_SPEED), MAX_SPEED);
	}
	bounds(1, u);
	bounds(2, v);
	// velocity: diffuse, project, advect, project
	float visc = std::min(std::max(p.viscosity, 0.f), 0.01f);
	std::memcpy(u0, u, sizeof(u));
	std::memcpy(v0, v, sizeof(v));
	diffuse(1, u, u0, visc, dt);
	diffuse(2, v, v0, visc, dt);
	project();
	std::memcpy(u0, u, sizeof(u));
	std::memcpy(v0, v, sizeof(v));
	advect(1, u, u0, u0, v0, dt);
	advect(2, v, v0, u0, v0, dt);
	project();
	// dye: diffuse, advect, fade. Moving dye about must not make or lose any, so the total is put back.
	double before = totalDye();
	std::memcpy(d0, d, sizeof(d));
	diffuse(0, d, d0, std::min(std::max(p.diffusion, 0.f), 0.01f), dt);
	std::memcpy(d0, d, sizeof(d));
	advect(0, d, d0, u, v, dt);
	double after = totalDye();
	float keep = after > 1e-9 ? (float)(before / after) : 1.f;
	float fade = p.halfLife > 0.f ? std::exp2(-dt / p.halfLife) : 1.f;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++)
			d[at(i, j)] = std::min(std::max(d[at(i, j)] * keep * fade, 0.f), 100.f);
	bounds(0, d);
	if (!finite())
		reset(0);
}

float FlowCore::sample(const float* f, float x, float y) const {
	float gx = std::min(std::max(x, 0.f), 1.f) * (float)N + 0.5f;
	float gy = std::min(std::max(y, 0.f), 1.f) * (float)N + 0.5f;
	gx = std::min(std::max(gx, 0.5f), (float)N + 0.5f);
	gy = std::min(std::max(gy, 0.5f), (float)N + 0.5f);
	int i0 = (int)gx, j0 = (int)gy;
	float s = gx - (float)i0, t = gy - (float)j0;
	return (1.f - s) * ((1.f - t) * f[at(i0, j0)] + t * f[at(i0, j0 + 1)]) + s * ((1.f - t) * f[at(i0 + 1, j0)] + t * f[at(i0 + 1, j0 + 1)]);
}

float FlowCore::curl(float x, float y) const {
	// how fast the fluid is turning at this point (turns to the right are positive on a screen)
	const float e = 1.f / (float)N;
	return (velV(x + e, y) - velV(x - e, y) - velU(x, y + e) + velU(x, y - e)) / (2.f * e);
}

float FlowCore::energy() const {
	double sum = 0.0;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++)
			sum += 0.5 * ((double)u[at(i, j)] * u[at(i, j)] + (double)v[at(i, j)] * v[at(i, j)]);
	return (float)(sum / (double)(N * N));
}

double FlowCore::totalDye() const {
	double sum = 0.0;
	for (int j = 1; j <= N; j++)
		for (int i = 1; i <= N; i++)
			sum += d[at(i, j)];
	return sum;
}

bool FlowCore::finite() const {
	for (int k = 0; k < SIZE; k++)
		if (!std::isfinite(u[k]) || !std::isfinite(v[k]) || !std::isfinite(d[k]))
			return false;
	return true;
}

} // namespace codex
