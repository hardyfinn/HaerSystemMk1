// Curves on [0,1] -> [0,1] (spec §3.2): named shapes plus a monotonic spline.
#pragma once

namespace codex {

// p^k, k from 0.25 to 4. k > 1 starts slowly, k < 1 starts quickly.
float ease(float p, float k);

// A normalised logistic S-curve through (0,0), (0.5,0.5) and (1,1).
// k sets the steepness; k near 0 is a straight line.
float scurve(float p, float k);

// A monotone cubic spline (Fritsch-Carlson) through 2 to 16 points.
// Where the points rise, the curve never dips between them.
struct Spline {
	static const int MAX = 16;
	int n = 2;
	float x[MAX] = {0.f, 1.f};
	float y[MAX] = {0.f, 1.f};
	float m[MAX] = {1.f, 1.f}; // slope at each point

	// Points must be sorted by x, with every x different from the last.
	void set(const float* xs, const float* ys, int count);
	float eval(float t) const;
};

// Sort points by x and keep each x and y at least minGap above the one before,
// so a hand-drawn curve is always a valid, rising spline. Works in place.
void makeRising(float* xs, float* ys, int count, float minGap);

} // namespace codex
