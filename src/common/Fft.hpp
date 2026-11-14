// A small in-place radix-2 FFT, shared by TUNE MAP and EAR. Plain C++, no Rack.
#pragma once
#include <algorithm>
#include <cmath>

namespace codex {

// Forward transform. n must be a power of two.
inline void fft(float* re, float* im, int n) {
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		double ang = -2.0 * M_PI / len;
		float wr = (float)std::cos(ang), wi = (float)std::sin(ang);
		for (int i = 0; i < n; i += len) {
			float cr = 1.f, ci = 0.f;
			for (int k = 0; k < len / 2; k++) {
				int a = i + k, b = i + k + len / 2;
				float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
				re[b] = re[a] - tr;
				im[b] = im[a] - ti;
				re[a] += tr;
				im[a] += ti;
				float nr = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = nr;
			}
		}
	}
}

// Inverse transform, scaled by 1/n.
inline void ifft(float* re, float* im, int n) {
	for (int i = 0; i < n; i++)
		im[i] = -im[i];
	fft(re, im, n);
	float s = 1.f / (float)n;
	for (int i = 0; i < n; i++) {
		re[i] *= s;
		im[i] = -im[i] * s;
	}
}

// Twiddle table for realFft: cos and sin of -2 pi k / n for k = 0..n/2.
inline void realFftTwiddles(float* twc, float* tws, int n) {
	for (int k = 0; k <= n / 2; k++) {
		double ang = -2.0 * 3.14159265358979323846 * k / n;
		twc[k] = (float)std::cos(ang);
		tws[k] = (float)std::sin(ang);
	}
}

// Spectrum of n real samples (n a power of two, at least 4), done as one complex
// transform of half the size. Fills re[0..n/2] and im[0..n/2]. `work` needs n floats.
inline void realFft(const float* x, float* re, float* im, float* work, int n, const float* twc, const float* tws) {
	int h = n / 2;
	float* zr = work;
	float* zi = work + h;
	for (int m = 0; m < h; m++) {
		zr[m] = x[2 * m];
		zi[m] = x[2 * m + 1];
	}
	fft(zr, zi, h);
	for (int k = 0; k <= h; k++) {
		int a = k % h, b = (h - k) % h;
		float er = 0.5f * (zr[a] + zr[b]), ei = 0.5f * (zi[a] - zi[b]);
		float or_ = 0.5f * (zi[a] + zi[b]), oi = -0.5f * (zr[a] - zr[b]);
		float c = twc[k], s = tws[k];
		re[k] = er + c * or_ - s * oi;
		im[k] = ei + c * oi + s * or_;
	}
}

} // namespace codex
