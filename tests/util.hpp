// Helpers shared by the unit tests (spec §14).
#pragma once
#include <cmath>
#include <vector>
#include <functional>
#include <cstddef>

namespace testutil {

// Render n samples from a per-sample function.
inline std::vector<float> render(int n, const std::function<float(int)>& fn) {
	std::vector<float> out((size_t)n);
	for (int i = 0; i < n; i++)
		out[(size_t)i] = fn(i);
	return out;
}

// Strength of one frequency in a signal (0..1 for a full-scale sine at that frequency).
inline float goertzel(const std::vector<float>& x, float freq, float sampleRate) {
	double w = 2.0 * M_PI * freq / sampleRate;
	double c = 2.0 * std::cos(w);
	double s0 = 0, s1 = 0, s2 = 0;
	for (float v : x) {
		s0 = v + c * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	double power = s1 * s1 + s2 * s2 - c * s1 * s2;
	return (float)(2.0 * std::sqrt(power < 0 ? 0 : power) / (double)x.size());
}

// Time in seconds for the level to fall by 60 dB, estimated from the slope of
// the energy decay curve between -5 dB and -35 dB.
inline float t60(const std::vector<float>& x, float sampleRate) {
	std::vector<double> edc(x.size() + 1, 0.0);
	for (size_t i = x.size(); i-- > 0;)
		edc[i] = edc[i + 1] + (double)x[i] * x[i];
	if (edc[0] <= 0)
		return 0.f;
	int i5 = -1, i35 = -1;
	for (size_t i = 0; i < x.size(); i++) {
		double db = 10.0 * std::log10(edc[i] / edc[0] + 1e-30);
		if (i5 < 0 && db <= -5.0)
			i5 = (int)i;
		if (i35 < 0 && db <= -35.0) {
			i35 = (int)i;
			break;
		}
	}
	if (i5 < 0 || i35 <= i5)
		return 0.f;
	return (float)(i35 - i5) / sampleRate * 2.f; // 30 dB measured, 60 dB wanted
}

// Number of upward zero crossings.
inline int zeroCrossings(const std::vector<float>& x) {
	int n = 0;
	for (size_t i = 1; i < x.size(); i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f)
			n++;
	return n;
}

inline float maxAbs(const std::vector<float>& x) {
	float m = 0.f;
	for (float v : x)
		m = std::fmax(m, std::fabs(v));
	return m;
}

// True when every sample is a normal number (no NaN, no infinity).
inline bool allFinite(const std::vector<float>& x) {
	for (float v : x)
		if (!std::isfinite(v))
			return false;
	return true;
}

// A 0..10 V ramp at `hz` cycles per second, sample i.
inline float ramp(double hz, double sampleRate, int i) {
	double t = hz * (double)i / sampleRate;
	return (float)(10.0 * (t - std::floor(t)));
}

} // namespace testutil
