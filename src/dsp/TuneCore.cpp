#include "dsp/TuneCore.hpp"
#include "common/Fft.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace codex {

namespace {
float ratioCents(double r) { return (float)(1200.0 * std::log2(r)); }
const float SPAN = TuneMorph::SPAN;
} // namespace

void TuneField::add(float c, float w) {
	if (n >= MAX || !std::isfinite(c))
		return;
	c = std::fmod(c, period);
	if (c < 0.f)
		c += period;
	cents[n] = c;
	weight[n] = w;
	n++;
}

void TuneField::sort() {
	// insertion sort: at most 48 points
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && cents[j] < cents[j - 1]; j--) {
			std::swap(cents[j], cents[j - 1]);
			std::swap(weight[j], weight[j - 1]);
		}
}

bool tuneBuiltIn(int id, TuneField& f) {
	f.clear();
	auto list = [&](std::initializer_list<float> c) {
		for (float x : c)
			f.add(x);
	};
	switch (id) {
		case FIELD_MAJOR: list({0, 200, 400, 500, 700, 900, 1100}); break;
		case FIELD_MINOR: list({0, 200, 300, 500, 700, 800, 1000}); break;
		case FIELD_PENTATONIC: list({0, 200, 400, 700, 900}); break;
		case FIELD_JUST_MAJOR:
			for (double r : {1.0, 9.0 / 8, 5.0 / 4, 4.0 / 3, 3.0 / 2, 5.0 / 3, 15.0 / 8})
				f.add(ratioCents(r));
			break;
		case FIELD_BLUE:
			for (double r : {1.0, 9.0 / 8, 7.0 / 6, 5.0 / 4, 4.0 / 3, 3.0 / 2, 5.0 / 3, 7.0 / 4, 15.0 / 8})
				f.add(ratioCents(r));
			break;
		case FIELD_HARM_8_16:
			// partials 1 to 15 of the root: sparse at the bottom, partials 8 to 16 fill the top octave
			f.clear(4800.f);
			for (int h = 1; h < 16; h++)
				f.add(ratioCents(h));
			break;
		case FIELD_HARM_16_32:
			// the same idea one octave further up the series: partials 16 to 32 fill the top octave
			f.clear(4800.f);
			for (int h = 2; h < 32; h++)
				f.add(ratioCents(h / 2.0));
			break;
		case FIELD_BOHLEN_PIERCE: {
			// 13 equal steps of the 3:1 "tritave" (approximate: the menu says so)
			float tritave = ratioCents(3.0);
			f.clear(tritave);
			for (int k = 0; k < 13; k++)
				f.add(tritave * (float)k / 13.f);
			break;
		}
		case FIELD_SLENDRO: list({0, 240, 480, 720, 960}); break;
		case FIELD_PELOG: list({0, 120, 270, 540, 670, 785, 950}); break;
		case FIELD_19TET:
			for (int s : {0, 3, 6, 8, 11, 14, 17})
				f.add(1200.f * (float)s / 19.f);
			break;
		case FIELD_CLUSTER: list({0, 50, 100, 150}); break;
		case FIELD_QUARTER_MINOR: list({0, 150, 300, 500, 700, 800, 1000}); break;
		default: return false;
	}
	f.sort();
	return true;
}

const char* tuneFieldName(int id) {
	static const char* names[FIELD_COUNT + 1] = {"", "12-TET major", "12-TET minor", "Pentatonic", "Just major (5-limit)",
		"Blue (7-limit)", "Harmonics 8-16", "Harmonics 16-32", "Bohlen-Pierce (approx.)", "Slendro (approx.)", "Pelog (approx.)",
		"19-TET subset", "Cluster", "Quarter-tone minor", "User 1", "User 2", "Learned"};
	return id >= 1 && id <= FIELD_COUNT ? names[id] : "";
}

// ---- morph ----

namespace {
// The field repeated across the whole 4-octave span.
int unroll(const TuneField& f, float* c, float* w) {
	int n = 0;
	if (f.n == 0 || !(f.period > 1.f))
		return 0;
	for (int k = 0; k * f.period < SPAN && n < TuneMorph::MAX; k++)
		for (int i = 0; i < f.n && n < TuneMorph::MAX; i++) {
			float x = f.cents[i] + (float)k * f.period;
			if (x < SPAN - 1e-3f) {
				c[n] = x;
				w[n] = f.weight[i];
				n++;
			}
		}
	return n;
}
// Distance round the span, and the copy of b nearest to a.
float nearestImage(float a, float b) {
	float d = b - a;
	if (d > SPAN * 0.5f)
		return b - SPAN;
	if (d < -SPAN * 0.5f)
		return b + SPAN;
	return b;
}
} // namespace

void TuneMorph::build(const TuneField& A, const TuneField& B) {
	float ca[MAX], wa[MAX], cb[MAX], wb[MAX];
	int na = unroll(A, ca, wa), nb = unroll(B, cb, wb);
	bool usedA[MAX] = {}, usedB[MAX] = {};
	n = 0;
	// Greedy: keep joining the closest pair that is still free. Each A remembers its
	// nearest free B, so only the A's whose partner was just taken look again.
	int near[MAX];
	float dist[MAX];
	auto look = [&](int i) {
		near[i] = -1;
		dist[i] = 1e9f;
		for (int j = 0; j < nb; j++) {
			if (usedB[j])
				continue;
			float d = std::fabs(nearestImage(ca[i], cb[j]) - ca[i]);
			if (d < dist[i]) {
				dist[i] = d;
				near[i] = j;
			}
		}
	};
	for (int i = 0; i < na; i++)
		look(i);
	while (true) {
		int bi = -1;
		for (int i = 0; i < na; i++)
			if (!usedA[i] && near[i] >= 0 && (bi < 0 || dist[i] < dist[bi]))
				bi = i;
		if (bi < 0)
			break;
		int bj = near[bi];
		usedA[bi] = usedB[bj] = true;
		pair[n++] = {ca[bi], nearestImage(ca[bi], cb[bj]), wa[bi], wb[bj]};
		for (int i = 0; i < na; i++)
			if (!usedA[i] && near[i] == bj)
				look(i);
	}
	// what is left over fades in or out where it stands
	for (int i = 0; i < na && n < MAX; i++)
		if (!usedA[i])
			pair[n++] = {ca[i], ca[i], wa[i], 0.f};
	for (int j = 0; j < nb && n < MAX; j++)
		if (!usedB[j])
			pair[n++] = {cb[j], cb[j], 0.f, wb[j]};
}

void TuneMorph::at(float m, float outCents[MAX], float outWeight[MAX], int& count) const {
	m = std::min(std::max(m, 0.f), 1.f);
	count = n;
	for (int k = 0; k < n; k++) {
		float c = pair[k].a + (pair[k].b - pair[k].a) * m;
		c = std::fmod(c, SPAN);
		if (c < 0.f)
			c += SPAN;
		outCents[k] = c;
		outWeight[k] = pair[k].wa + (pair[k].wb - pair[k].wa) * m;
	}
}

// ---- quantiser ----

void TuneQuantiser::set(const TuneMorph& morph, float m) {
	morph.at(m, cents, weight, n);
	allowed = 0;
	for (int k = 0; k < n; k++)
		if (weight[k] >= MIN_WEIGHT)
			order[allowed++] = k;
	std::sort(order, order + allowed, [this](int a, int b) { return cents[a] < cents[b]; });
	for (int i = 0; i < allowed; i++)
		sorted[i] = cents[order[i]];
}

float TuneQuantiser::process(TuneChannel& ch, float in, float root, float strength, float hystCents) const {
	ch.changed = false;
	float x = (in - root) * 1200.f;
	// nearest allowed point: bisect for where x falls in the span, then compare the two either side
	int best = -1;
	float bestPos = 0.f, bestD = 1e30f;
	if (allowed > 0) {
		float base = std::floor(x / SPAN) * SPAN;
		float local = x - base;
		int hi = (int)(std::lower_bound(sorted, sorted + allowed, local) - sorted);
		for (int c = hi - 1; c <= hi; c++) {
			// past either end, wrap round to the other end of the next or previous span
			int i = c, shift = 0;
			if (i < 0) {
				i = allowed - 1;
				shift = -1;
			}
			else if (i >= allowed) {
				i = 0;
				shift = 1;
			}
			float p = base + sorted[i] + (float)shift * SPAN;
			float d = std::fabs(p - x);
			if (d < bestD) {
				bestD = d;
				best = order[i];
				bestPos = p;
			}
		}
	}
	if (best < 0) {
		// nothing allowed: the pitch passes straight through
		if (ch.target >= 0)
			ch.changed = true;
		ch.target = -1;
		return in;
	}
	// hysteresis: stay with the current target unless the new one is clearly nearer
	if (ch.target >= 0 && ch.target < n && weight[ch.target] >= MIN_WEIGHT && ch.target != best) {
		float p = cents[ch.target] + (float)ch.octave * SPAN;
		// the point may have moved round the span in a morph; take its copy nearest x
		while (p - x > SPAN * 0.5f)
			p -= SPAN;
		while (x - p > SPAN * 0.5f)
			p += SPAN;
		if (std::fabs(p - x) <= bestD + hystCents) {
			best = ch.target;
			bestPos = p;
		}
	}
	int octave = (int)std::lround((bestPos - cents[best]) / SPAN);
	if (best != ch.target || octave != ch.octave)
		ch.changed = true;
	ch.target = best;
	ch.octave = octave;
	float snapped = root + bestPos / 1200.f;
	strength = std::min(std::max(strength, 0.f), 1.f);
	return in + (snapped - in) * strength;
}

int TuneQuantiser::firstOctave(float* out, int max) const {
	int count = 0;
	for (int k = 0; k < n && count < max; k++)
		if (weight[k] >= MIN_WEIGHT && cents[k] < 1200.f - 1e-3f)
			out[count++] = cents[k];
	std::sort(out, out + count);
	return count;
}

// ---- learning ----

void tuneFft(float* re, float* im, int n) { fft(re, im, n); }

TuneLearner::TuneLearner() {
	// Blackman window: its side lobes are low enough that only real partials count as peaks
	for (int i = 0; i < FFT; i++) {
		double t = 2.0 * M_PI * i / (FFT - 1);
		window[i] = (float)(0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2.0 * t));
	}
	start();
}

void TuneLearner::start() {
	std::memset(ring, 0, sizeof(ring));
	std::memset(avg, 0, sizeof(avg));
	pos = sinceHop = filled = nFrames = 0;
}

void TuneLearner::feed(float x) {
	ring[pos] = std::isfinite(x) ? x : 0.f;
	pos = (pos + 1) & (FFT - 1);
	if (filled < FFT)
		filled++;
	if (++sinceHop >= HOP && filled >= FFT) {
		sinceHop = 0;
		analyse();
	}
}

void TuneLearner::analyse() {
	for (int i = 0; i < FFT; i++) {
		re[i] = ring[(pos + i) & (FFT - 1)] * window[i];
		im[i] = 0.f;
	}
	tuneFft(re, im, FFT);
	for (int k = 0; k <= FFT / 2; k++)
		avg[k] += std::sqrt(re[k] * re[k] + im[k] * im[k]);
	nFrames++;
}

bool TuneLearner::finish(float sampleRate, TuneField& out) const {
	out.clear();
	if (nFrames == 0)
		return false;
	float top = 0.f;
	for (int k = 2; k < FFT / 2; k++)
		top = std::max(top, avg[k]);
	if (top <= 1e-9f)
		return false;
	// local maxima at least 40 dB under the loudest, above 20 Hz
	struct Peak {
		float freq, mag;
	};
	Peak peaks[64];
	int np = 0;
	int lowest = std::max(2, (int)std::ceil(20.f * FFT / sampleRate));
	for (int k = lowest; k < FFT / 2 - 1; k++) {
		float m = avg[k];
		if (m < top * 0.01f || m <= avg[k - 1] || m < avg[k + 1])
			continue;
		// parabolic interpolation on the log magnitude
		float a = std::log(avg[k - 1] + 1e-12f), b = std::log(m + 1e-12f), c = std::log(avg[k + 1] + 1e-12f);
		float den = a - 2.f * b + c;
		float shift = den < 0.f ? 0.5f * (a - c) / den : 0.f;
		Peak p = {((float)k + shift) * sampleRate / FFT, m};
		if (np < 64)
			peaks[np++] = p;
		else {
			int weakest = 0;
			for (int i = 1; i < 64; i++)
				if (peaks[i].mag < peaks[weakest].mag)
					weakest = i;
			if (p.mag > peaks[weakest].mag)
				peaks[weakest] = p;
		}
	}
	std::sort(peaks, peaks + np, [](const Peak& x, const Peak& y) { return x.mag > y.mag; });
	// the 8 strongest, as pitch classes above C4; ones that land within 30 cents of a stronger one merge into it
	for (int i = 0; i < np && out.n < PEAKS; i++) {
		float c = std::fmod(ratioCents(peaks[i].freq / 261.6256), 1200.f);
		if (c < 0.f)
			c += 1200.f;
		bool dup = false;
		for (int j = 0; j < out.n; j++) {
			float d = std::fabs(out.cents[j] - c);
			if (std::min(d, 1200.f - d) < 30.f)
				dup = true;
		}
		if (!dup)
			out.add(c, 1.f);
	}
	out.sort();
	return out.n > 0;
}

} // namespace codex
