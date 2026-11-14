#include "dsp/EarCore.hpp"
#include "common/Fft.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace codex {

namespace {
const float FLUX_FLOOR = 0.5f; // a whisper of new energy never counts as an onset
const double PI_D = 3.14159265358979323846;
} // namespace

EarAnalyser::EarAnalyser() {
	for (int i = 0; i < FFT; i++)
		window[i] = (float)(0.5 - 0.5 * std::cos(2.0 * PI_D * i / FFT));
	realFftTwiddles(twc, tws, FFT);
	reset(48000.f);
}

void EarAnalyser::reset(float sampleRate) {
	sr = sampleRate > 1000.f ? sampleRate : 48000.f;
	std::memset(ring, 0, sizeof(ring));
	std::memset(mag, 0, sizeof(mag));
	std::memset(prevMag, 0, sizeof(prevMag));
	std::memset(history, 0, sizeof(history));
	n = 0;
	sinceHop = sinceYin = 0;
	lastOnset = -1000000;
	histPos = histCount = 0;
	power = 0.0;
	powerCoef = 1.f - std::exp(-1.f / (0.05f * sr));
	loud = centroid = noise = strength = pitch = confidence = 0.f;
	onsetAge = 0;
}

bool EarAnalyser::process(float x) {
	if (!std::isfinite(x))
		x = 0.f;
	ring[n & (RING - 1)] = x;
	n++;
	// loudness: RMS over about 50 ms, in dB, -60..0 dBFS = 0..10 V
	power += ((double)x * x - power) * powerCoef;

	bool onset = false;
	if (++sinceHop >= HOP) {
		sinceHop = 0;
		onset = spectrum();
		float db = 10.f * std::log10((float)power + 1e-12f);
		loud = std::min(std::max((db + 60.f) / 6.f, 0.f), 10.f);
	}
	if (withPitch && ++sinceYin >= YIN_HOP) {
		sinceYin = 0;
		yin();
	}
	return onset;
}

// One hop: the spectrum of the last 512 samples. Returns true on an onset.
bool EarAnalyser::spectrum() {
	float frame[FFT];
	for (int i = 0; i < FFT; i++)
		frame[i] = at(FFT - 1 - i) * window[i];
	realFft(frame, re, im, work, FFT, twc, tws);
	float flux = 0.f, sum = 0.f, weighted = 0.f;
	float logSum = 0.f, powSum = 0.f;
	const int bins = FFT / 2;
	for (int k = 0; k <= bins; k++) {
		mag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
		flux += std::max(0.f, mag[k] - prevMag[k]);
		if (k >= 1 && k < bins) {
			sum += mag[k];
			weighted += mag[k] * (float)k * sr / FFT;
			float p = mag[k] * mag[k] + 1e-12f;
			logSum += std::log2(p);
			powSum += p;
		}
	}
	std::memcpy(prevMag, mag, sizeof(mag));
	// brightness and noisiness, held through silence
	if (sum > 1e-3f) {
		float f = weighted / sum;
		centroid = std::min(std::max(std::log2(std::max(f, 1.f) / 20.f), 0.f), 10.f);
		float flat = std::exp2(logSum / (float)(bins - 1)) / (powSum / (float)(bins - 1));
		noise = std::min(std::max((float)flat * 10.f, 0.f), 10.f);
	}

	// adaptive threshold: median of the last 16 flux values
	float sorted[HISTORY];
	int m = std::max(histCount, 1);
	for (int i = 0; i < m; i++)
		sorted[i] = histCount ? history[i] : 0.f;
	std::nth_element(sorted, sorted + m / 2, sorted + m);
	float threshold = sorted[m / 2] * sens + FLUX_FLOOR;
	history[histPos] = flux;
	histPos = (histPos + 1) % HISTORY;
	histCount = std::min(histCount + 1, HISTORY);
	lastFlux = flux;
	lastThreshold = threshold;

	int64_t gap = (int64_t)(0.03f * sr);
	if (n < 2 * FFT)
		return false; // the first frames compare against nothing
	if (flux <= threshold || n - lastOnset < gap)
		return false;
	lastOnset = n;
	strength = std::min(std::max(10.f * (1.f - threshold / flux), 0.f), 10.f);
	// When did it begin? The new energy arrived somewhere in the newest part of the window:
	// take the first sample there that reaches half the peak.
	const int SEARCH = 3 * HOP;
	float peak = 0.f;
	for (int b = 0; b < SEARCH; b++)
		peak = std::max(peak, std::fabs(at(b)));
	onsetAge = HOP / 2;
	for (int b = SEARCH - 1; b >= 0; b--)
		if (std::fabs(at(b)) >= 0.5f * peak) {
			onsetAge = b;
			break;
		}
	return true;
}

// YIN (de Cheveigne and Kawahara) on the last 2048 samples: integration window
// 1024, lags up to 1024. The difference function is found with an FFT.
void EarAnalyser::yin() {
	const int W = YIN / 2, N2 = 2 * YIN;
	for (int i = 0; i < YIN; i++)
		ybuf[i] = at(YIN - 1 - i);
	cum[0] = 0.0;
	for (int i = 0; i < YIN; i++)
		cum[i + 1] = cum[i] + (double)ybuf[i] * ybuf[i];
	double e0 = cum[W];
	if (e0 < 1e-6) {
		confidence = 0.f;
		return;
	}
	// Both real signals in one complex FFT: the window in the real part, the whole buffer in the imaginary part.
	for (int i = 0; i < N2; i++) {
		are[i] = i < W ? ybuf[i] : 0.f;
		aim[i] = i < YIN ? ybuf[i] : 0.f;
	}
	fft(are, aim, N2);
	// Separate them (A from the real part, Y from the imaginary part) and form conj(A) * Y.
	for (int k = 0; k < N2; k++) {
		int j = (N2 - k) & (N2 - 1);
		float ar = 0.5f * (are[k] + are[j]), ai = 0.5f * (aim[k] - aim[j]);
		float yr = 0.5f * (aim[k] + aim[j]), yi = -0.5f * (are[k] - are[j]);
		yre[k] = ar * yr + ai * yi;
		yim[k] = ar * yi - ai * yr;
	}
	ifft(yre, yim, N2);
	// cumulative mean normalised difference
	int tauMax = W;
	int tauMin = std::max(2, (int)(sr / 2000.f));
	dprime[0] = 1.f;
	double running = 0.0;
	for (int tau = 1; tau <= tauMax; tau++) {
		double et = cum[tau + W] - cum[tau];
		double d = std::max(0.0, e0 + et - 2.0 * (double)yre[tau]);
		running += d;
		dprime[tau] = running > 1e-12 ? (float)(d * tau / running) : 1.f;
	}
	int best = -1;
	for (int tau = tauMin; tau < tauMax; tau++) {
		if (dprime[tau] < 0.1f) {
			while (tau + 1 < tauMax && dprime[tau + 1] < dprime[tau])
				tau++;
			best = tau;
			break;
		}
	}
	if (best < 0) {
		best = tauMin;
		for (int tau = tauMin; tau < tauMax; tau++)
			if (dprime[tau] < dprime[best])
				best = tau;
	}
	float shift = 0.f;
	if (best > 1 && best < tauMax) {
		float a = dprime[best - 1], b = dprime[best], c = dprime[best + 1];
		float den = a - 2.f * b + c;
		if (den > 1e-9f)
			shift = std::min(std::max(0.5f * (a - c) / den, -0.5f), 0.5f);
	}
	float conf = std::min(std::max(1.f - dprime[best], 0.f), 1.f);
	confidence = conf * 10.f;
	if (conf >= 0.5f) {
		float f = sr / ((float)best + shift);
		pitch = std::log2(f / 261.6256f);
	}
}

// ---- groove ----

EarGroove::EarGroove() {
	std::memset(ring, 0, sizeof(ring));
	clear();
}

void EarGroove::clear() {
	points = 0;
	curve = Spline();
	version++;
}

void EarGroove::phase(double W) {
	ring[n & (PHASE_RING - 1)] = W;
	n++;
}

void EarGroove::process(bool gate, bool onset, int age, int bars, int grid) {
	if (gate && !lastGate) {
		learning = true;
		heard = 0;
		gridUsed = std::min(std::max(grid, 2), MAX_GRID);
		barsUsed = std::max(bars, 1);
		startW = n > 0 ? ring[(n - 1) & (PHASE_RING - 1)] : 0.0;
		std::memset(sum, 0, sizeof(sum));
		std::memset(count, 0, sizeof(count));
	}
	if (learning && n > 0) {
		double now = ring[(n - 1) & (PHASE_RING - 1)];
		if (onset && now - startW < (double)barsUsed) {
			int back = std::min(std::max(age, 0), (int)std::min<int64_t>(n - 1, PHASE_RING - 1));
			double W = ring[(n - 1 - back) & (PHASE_RING - 1)];
			double p = W - std::floor(W);
			int g = (int)std::lround(p * gridUsed);
			// downbeats are the fixed ends of the curve, so they only count as ends
			if (g > 0 && g < gridUsed) {
				sum[g] += p;
				count[g]++;
				heard++;
			}
		}
	}
	if (!gate && lastGate && learning) {
		learning = false;
		finish();
	}
	lastGate = gate;
}

void EarGroove::finish() {
	float xs[MAX_GRID + 1], ys[MAX_GRID + 1];
	int k = 0;
	for (int g = 1; g < gridUsed; g++)
		if (count[g] > 0) {
			xs[k] = (float)g / gridUsed;
			ys[k] = (float)(sum[g] / count[g]);
			k++;
		}
	if (k == 0)
		return; // nothing heard: keep the old curve
	set(xs, ys, k);
}

void EarGroove::set(const float* xs, const float* ys, int k) {
	k = std::min(std::max(k, 0), (int)MAX_POINTS);
	float fx[Spline::MAX], fy[Spline::MAX];
	fx[0] = fy[0] = 0.f;
	for (int i = 0; i < k; i++) {
		fx[i + 1] = std::min(std::max(xs[i], 0.f), 1.f);
		fy[i + 1] = std::min(std::max(ys[i], 0.f), 1.f);
	}
	// sorted, and each point at least 0.002 above the one before, so the curve always rises
	makeRising(fx + 1, fy + 1, k, 0.f);
	for (int i = 1; i <= k; i++) {
		float room = 1.f - 0.002f * (float)(k + 1 - i);
		fy[i] = std::min(std::max(fy[i], fy[i - 1] + 0.002f), room);
		fx[i] = std::min(std::max(fx[i], fx[i - 1] + 0.002f), room);
	}
	fx[k + 1] = fy[k + 1] = 1.f;
	points = k;
	for (int i = 0; i < k; i++) {
		px[i] = fx[i + 1];
		py[i] = fy[i + 1];
	}
	curve.set(fx, fy, k + 2);
	version++;
}

int EarGroove::reduced(float* xs, float* ys, int max) const {
	int k = points;
	for (int i = 0; i < k; i++) {
		xs[i] = px[i];
		ys[i] = py[i];
	}
	// Drop, one at a time, the point that a straight line between its neighbours
	// would miss by the least: it is the one the shape needs least.
	while (k > max && k > 0) {
		int least = 0;
		float best = 1e9f;
		for (int i = 0; i < k; i++) {
			float x0 = i > 0 ? xs[i - 1] : 0.f, y0 = i > 0 ? ys[i - 1] : 0.f;
			float x1 = i + 1 < k ? xs[i + 1] : 1.f, y1 = i + 1 < k ? ys[i + 1] : 1.f;
			float t = x1 - x0 > 1e-6f ? (xs[i] - x0) / (x1 - x0) : 0.5f;
			float miss = std::fabs(y0 + t * (y1 - y0) - ys[i]);
			if (miss < best) {
				best = miss;
				least = i;
			}
		}
		for (int i = least; i < k - 1; i++) {
			xs[i] = xs[i + 1];
			ys[i] = ys[i + 1];
		}
		k--;
	}
	return k;
}

float EarGroove::apply(float p, float amount) const {
	p = std::min(std::max(p, 0.f), 1.f);
	amount = std::min(std::max(amount, 0.f), 1.f);
	if (points == 0)
		return p;
	return p + amount * (curve.eval(p) - p);
}

} // namespace codex
