#include "common/Curve.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

float ease(float p, float k) {
	p = std::min(std::max(p, 0.f), 1.f);
	k = std::min(std::max(k, 0.25f), 4.f);
	return std::pow(p, k);
}

float scurve(float p, float k) {
	p = std::min(std::max(p, 0.f), 1.f);
	if (std::fabs(k) < 1e-3f)
		return p;
	auto sig = [](float v) { return 1.f / (1.f + std::exp(-v)); };
	float lo = sig(-0.5f * k);
	float hi = sig(0.5f * k);
	return (sig(k * (p - 0.5f)) - lo) / (hi - lo);
}

void Spline::set(const float* xs, const float* ys, int count) {
	n = std::min(std::max(count, 2), (int)MAX);
	for (int i = 0; i < n; i++) {
		x[i] = xs[i];
		y[i] = ys[i];
	}
	// Slope of each straight segment.
	float d[MAX] = {};
	for (int i = 0; i < n - 1; i++) {
		float h = x[i + 1] - x[i];
		d[i] = h > 1e-9f ? (y[i + 1] - y[i]) / h : 0.f;
	}
	// First guess for the slope at each point: the average of its two segments.
	m[0] = d[0];
	m[n - 1] = d[n - 2];
	for (int i = 1; i < n - 1; i++)
		m[i] = (d[i - 1] * d[i] <= 0.f) ? 0.f : 0.5f * (d[i - 1] + d[i]);
	// Fritsch-Carlson: limit the slopes so the curve can't overshoot.
	for (int i = 0; i < n - 1; i++) {
		if (d[i] == 0.f) {
			m[i] = 0.f;
			m[i + 1] = 0.f;
			continue;
		}
		float a = m[i] / d[i];
		float b = m[i + 1] / d[i];
		if (a < 0.f) {
			m[i] = 0.f;
			a = 0.f;
		}
		if (b < 0.f) {
			m[i + 1] = 0.f;
			b = 0.f;
		}
		float s = a * a + b * b;
		if (s > 9.f) {
			float t = 3.f / std::sqrt(s);
			m[i] = t * a * d[i];
			m[i + 1] = t * b * d[i];
		}
	}
}

float Spline::eval(float t) const {
	if (t <= x[0])
		return y[0];
	if (t >= x[n - 1])
		return y[n - 1];
	int i = 0;
	while (i < n - 2 && t > x[i + 1])
		i++;
	float h = x[i + 1] - x[i];
	if (h <= 1e-9f)
		return y[i];
	float u = (t - x[i]) / h;
	float u2 = u * u, u3 = u2 * u;
	// Cubic Hermite blend of the two points and their slopes.
	return (2 * u3 - 3 * u2 + 1) * y[i] + (u3 - 2 * u2 + u) * h * m[i]
		+ (-2 * u3 + 3 * u2) * y[i + 1] + (u3 - u2) * h * m[i + 1];
}

void makeRising(float* xs, float* ys, int count, float minGap) {
	// Insertion sort by x: the list is tiny and nearly sorted.
	for (int i = 1; i < count; i++) {
		float kx = xs[i], ky = ys[i];
		int j = i - 1;
		while (j >= 0 && xs[j] > kx) {
			xs[j + 1] = xs[j];
			ys[j + 1] = ys[j];
			j--;
		}
		xs[j + 1] = kx;
		ys[j + 1] = ky;
	}
	for (int i = 1; i < count; i++) {
		xs[i] = std::max(xs[i], xs[i - 1] + minGap);
		ys[i] = std::max(ys[i], ys[i - 1] + minGap);
	}
}

} // namespace codex
