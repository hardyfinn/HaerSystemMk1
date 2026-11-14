#include "dsp/DepthCore.hpp"
#include "common/Rng.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

namespace {
const float PI = 3.14159265f;
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
const int CONTROL = 16; // samples between the slower sums
// which lines each side's output listens to, and with which sign (two different Walsh patterns)
const float OUT_L[16] = {1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1};
const float OUT_R[16] = {1, 1, -1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1, 1, 1};
const float DIFF_L_MS[4] = {2.f, 3.f, 5.f, 7.f};
const float DIFF_R_MS[4] = {3.f, 5.f, 7.f, 11.f};
const float TAIL_LEVEL = 0.35f;
const float ER_LEVEL = 0.5f;
// The limit inside each line (spec: "a tanh soft clip in each line limits the energy"):
// untouched below 3 V, rounded off above, never past 5 V.
inline float lineClip(float v) {
	float a = v < 0.f ? -v : v;
	if (a <= 3.f)
		return v;
	float c = 3.f + 2.f * std::tanh((a - 3.f) * 0.5f);
	return v < 0.f ? -c : c;
}
} // namespace

// 16 primes between 29 and 157, so no two lines ever line up
const float DepthCore::LINE_MS[LINES] = {29, 37, 43, 53, 61, 71, 79, 89, 97, 103, 109, 127, 131, 139, 149, 157};
const float DepthCore::TAP_MS[TAPS] = {7, 11, 17, 23, 31, 41, 53, 67};

float DepthCore::softClip(float v) {
	float a = std::fabs(v);
	if (a <= 9.f)
		return v;
	float c = 9.f + 2.9f * std::tanh((a - 9.f) / 2.9f);
	return v < 0.f ? -c : c;
}

void DepthCore::Delay::resize(size_t minLen) {
	size_t n = 16;
	while (n < minLen)
		n <<= 1;
	buf.assign(n, 0.f);
	mask = n - 1;
	pos = 0;
}

void DepthCore::Delay::clear() {
	std::fill(buf.begin(), buf.end(), 0.f);
	pos = 0;
}

float DepthCore::Delay::linear(float d) const {
	size_t i = (size_t)d;
	float f = d - (float)i;
	float a = at(i), b = at(i + 1);
	return a + (b - a) * f;
}

// Four-point (Catmull-Rom) read between samples, for delays that move smoothly.
float DepthCore::Delay::cubic(float d) const {
	size_t i = (size_t)d;
	float f = d - (float)i;
	float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
	float c1 = 0.5f * (y2 - y0);
	float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
	float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
	return ((c3 * f + c2) * f + c1) * f + y1;
}

float DepthCore::Allpass::process(float x, float g) {
	float d = line.at(len);
	float y = d - g * x;
	line.write(x + g * y);
	return y;
}

DepthCore::DepthCore() {
	// every line sways at its own slow speed; the speeds are fixed so a patch always sounds the same
	Rng rng(0xDE97);
	for (int i = 0; i < LINES; i++) {
		modRate[i] = 0.05f * std::pow(40.f, rng.uniform()); // 0.05 .. 2 Hz
		modPhase[i] = rng.uniform();
	}
	for (int j = 0; j < LINES / 2; j++) {
		rotRate[j] = 0.03f * std::pow(6.f, rng.uniform());
		rotPhase[j] = rng.uniform();
		rotCos[j] = 1.f;
		rotSin[j] = 0.f;
	}
	setSampleRate(48000.f);
}

void DepthCore::setSampleRate(float sr) {
	sampleRate = sr > 1000.f ? sr : 48000.f;
	size_t chLen = (size_t)(0.21f * sampleRate) + 8;  // 62 ms pre-delay + 134 ms of reflections
	for (Channel& c : ch) {
		c.left.resize(chLen);
		c.right.resize(chLen);
		c.mono.resize(chLen);
	}
	for (int i = 0; i < 4; i++) {
		diffL[i].len = std::max<size_t>(1, (size_t)std::lround(DIFF_L_MS[i] * 0.001f * sampleRate));
		diffR[i].len = std::max<size_t>(1, (size_t)std::lround(DIFF_R_MS[i] * 0.001f * sampleRate));
		diffL[i].line.resize(diffL[i].len + 4);
		diffR[i].line.resize(diffR[i].len + 4);
	}
	for (int i = 0; i < LINES; i++)
		line[i].resize((size_t)(0.33f * sampleRate) + 16); // 157 ms x SIZE 2, plus movement
	clear();
}

void DepthCore::clear() {
	for (Channel& c : ch) {
		c.left.clear();
		c.right.clear();
		c.mono.clear();
		c.lpL = c.lpR = 0.f;
		c.drain = 0;
		c.dist = -1.f; // take the knob's value at once
	}
	for (int i = 0; i < 4; i++) {
		diffL[i].line.clear();
		diffR[i].line.clear();
	}
	for (int i = 0; i < LINES; i++) {
		line[i].clear();
		lp[i] = 0.f;
		delayNow[i] = 0.f;
		modNow[i] = modInc[i] = 0.f;
	}
	modDepth = 0.f;
	size = -1.f;
	control = 0;
}

void DepthCore::updateControl(const DepthParams& p) {
	const float dt = (float)CONTROL / sampleRate;
	float wantSize = clampf(p.size, 0.25f, 2.f);
	size = size < 0.f ? wantSize : size + (wantSize - size) * (1.f - std::exp(-dt / 0.05f));

	for (int c = 0; c < CH; c++) {
		Channel& k = ch[c];
		float d = k.dist; // smoothed per sample in process()
		float cutoff = 1500.f + 16500.f * (1.f - d) * (1.f - d);
		k.airCoef = cutoff >= 17999.f ? 1.f : 1.f - std::exp(-2.f * PI * std::min(cutoff, 0.45f * sampleRate) / sampleRate);
		k.direct = 1.f / (1.f + 4.f * d);
		k.send = 0.2f + 0.8f * d;
		float pan = clampf(p.pan[c], -1.f, 1.f);
		k.panL = 1.f - std::max(pan, 0.f);
		k.panR = 1.f + std::min(pan, 0.f);
		// early reflections: louder with distance, alternating sides, leaning with PAN
		float er = (0.1f + 0.9f * d) * ER_LEVEL;
		float g = 1.f;
		for (int t = 0; t < TAPS; t++) {
			float side = clampf((t & 1 ? 0.7f : -0.7f) + pan * 0.5f, -1.f, 1.f);
			k.erL[t] = er * g * (1.f - std::max(side, 0.f));
			k.erR[t] = er * g * (1.f + std::min(side, 0.f));
			g *= 0.82f;
		}
	}
	for (int t = 0; t < TAPS; t++)
		tapSamples[t] = size * TAP_MS[t] * 0.001f * sampleRate;

	// ---- tail ----
	float t60 = clampf(p.decay, 0.3f, 30.f);
	float boost = 1.f + 0.08f * clampf(p.instability, 0.f, 1.f);
	for (int i = 0; i < LINES; i++) {
		delayWant[i] = std::round(wantSize * LINE_MS[i] * 0.001f * sampleRate);
		if (delayNow[i] <= 0.f)
			delayNow[i] = delayWant[i];
		// the standard rule: a line loses just enough each pass that the whole tail falls 60 dB in T60
		float g = std::pow(10.f, -3.f * delayWant[i] / (t60 * sampleRate));
		gain[i] = p.freeze ? 1.f : g * boost;
	}
	float damp = clampf(p.damp, 0.f, 1.f);
	if (damp <= 0.f || p.freeze)
		dampCoef = 1.f;
	else {
		float cutoff = 20000.f * std::pow(0.04f, damp); // 20 kHz .. 800 Hz
		dampCoef = cutoff >= 0.45f * sampleRate ? 1.f : 1.f - std::exp(-2.f * PI * cutoff / sampleRate);
	}
	// slow sway of every line; none while frozen, so a frozen tail loses nothing
	float wantDepth = p.freeze ? 0.f : clampf(p.mod, 0.f, 1.f) * 0.002f * sampleRate;
	modDepth += (wantDepth - modDepth) * (1.f - std::exp(-dt / 0.1f));
	if (modDepth < 1e-3f && wantDepth == 0.f)
		modDepth = 0.f;
	for (int i = 0; i < LINES; i++) {
		modPhase[i] += modRate[i] * dt;
		modPhase[i] -= std::floor(modPhase[i]);
		float target = modDepth * (0.5f + 0.5f * std::sin(2.f * PI * modPhase[i]));
		modInc[i] = (target - modNow[i]) / (float)CONTROL;
	}
	// INSTABILITY: pairs of lines are slowly turned into each other
	float inst = clampf(p.instability, 0.f, 1.f);
	rotating = inst > 0.f;
	if (rotating) {
		for (int j = 0; j < LINES / 2; j++) {
			rotPhase[j] += rotRate[j] * dt;
			rotPhase[j] -= std::floor(rotPhase[j]);
			float angle = inst * 0.7f * std::sin(2.f * PI * rotPhase[j]);
			rotCos[j] = std::cos(angle);
			rotSin[j] = std::sin(angle);
		}
	}
	float mix = clampf(p.mix, 0.f, 1.f);
	dryGain = std::cos(mix * 0.5f * PI);
	wetGain = std::sin(mix * 0.5f * PI);
}

void DepthCore::process(const DepthParams& p, const float inL[CH], const float inR[CH], DepthOut& out) {
	// DIST is smoothed so that a moving source glides (with a little Doppler) and does not click
	const float distCoef = 1.f - std::exp(-1.f / (0.03f * sampleRate));
	for (int c = 0; c < CH; c++) {
		float want = clampf(p.dist[c], 0.f, 1.f);
		ch[c].dist = ch[c].dist < 0.f ? want : ch[c].dist + (want - ch[c].dist) * distCoef;
	}
	if (control == 0)
		updateControl(p);
	control = (control + 1) % CONTROL;

	float dirL = 0.f, dirR = 0.f, erL = 0.f, erR = 0.f, sendL = 0.f, sendR = 0.f;
	const int drainLen = (int)(0.25f * sampleRate);
	for (int c = 0; c < CH; c++) {
		Channel& k = ch[c];
		if (p.connected[c])
			k.drain = drainLen;
		else if (k.drain > 0)
			k.drain--;
		else
			continue;
		float xl = p.connected[c] ? inL[c] : 0.f, xr = p.connected[c] ? inR[c] : 0.f;
		// air loss
		k.lpL += (xl - k.lpL) * k.airCoef;
		k.lpR += (xr - k.lpR) * k.airCoef;
		k.left.write(k.lpL);
		k.right.write(k.lpR);
		k.mono.write(0.5f * (k.lpL + k.lpR));
		// pre-delay: 2 ms close, 62 ms far
		float pd = (0.002f + 0.06f * k.dist) * sampleRate + 1.f;
		float dl = k.left.linear(pd), dr = k.right.linear(pd);
		dirL += dl * k.direct * k.panL;
		dirR += dr * k.direct * k.panR;
		sendL += dl * k.send;
		sendR += dr * k.send;
		for (int t = 0; t < TAPS; t++) {
			float e = k.mono.linear(pd + tapSamples[t]);
			erL += e * k.erL[t];
			erR += e * k.erR[t];
		}
	}

	// ---- tail ----
	float tl = p.freeze ? 0.f : sendL, tr = p.freeze ? 0.f : sendR;
	for (int i = 0; i < 4; i++) {
		tl = diffL[i].process(tl, 0.6f);
		tr = diffR[i].process(tr, 0.6f);
	}
	float v[LINES];
	float tailL = 0.f, tailR = 0.f;
	for (int i = 0; i < LINES; i++) {
		// glide to a new SIZE instead of jumping
		float diff = delayWant[i] - delayNow[i];
		if (diff != 0.f)
			delayNow[i] = std::fabs(diff) <= 0.25f ? delayWant[i] : delayNow[i] + (diff > 0.f ? 0.25f : -0.25f);
		modNow[i] += modInc[i];
		float d = delayNow[i] + (modDepth > 0.f ? modNow[i] : 0.f);
		float whole = std::floor(d);
		float x = d == whole ? line[i].at((size_t)whole) : line[i].cubic(d);
		tailL += x * OUT_L[i];
		tailR += x * OUT_R[i];
		lp[i] += (x - lp[i]) * dampCoef;
		v[i] = lp[i] * gain[i];
	}
	if (rotating) {
		for (int j = 0; j < LINES / 2; j++) {
			float a = v[2 * j], b = v[2 * j + 1];
			v[2 * j] = a * rotCos[j] - b * rotSin[j];
			v[2 * j + 1] = a * rotSin[j] + b * rotCos[j];
		}
	}
	// mix every line into every other (Hadamard), keeping the total energy the same
	for (int h = 1; h < LINES; h <<= 1)
		for (int i = 0; i < LINES; i += h << 1)
			for (int j = i; j < i + h; j++) {
				float a = v[j], b = v[j + h];
				v[j] = a + b;
				v[j + h] = a - b;
			}
	for (int i = 0; i < LINES; i++) {
		float in = (i & 1) ? tr : tl;
		if (i & 2)
			in = -in;
		line[i].write(lineClip(v[i] * 0.25f + in * 0.5f));
	}
	tailL *= TAIL_LEVEL;
	tailR *= TAIL_LEVEL;

	float wetL = erL + tailL, wetR = erR + tailR;
	if (!std::isfinite(wetL) || !std::isfinite(wetR)) {
		clear();
		out = DepthOut();
		return;
	}
	out.wetL = softClip(wetL);
	out.wetR = softClip(wetR);
	out.outL = softClip(dirL * dryGain + wetL * wetGain);
	out.outR = softClip(dirR * dryGain + wetR * wetGain);
}

} // namespace codex
