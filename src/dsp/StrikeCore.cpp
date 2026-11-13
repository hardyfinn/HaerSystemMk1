#include "dsp/StrikeCore.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

namespace {

const int M = StrikeTables::MODES;
const float PI = 3.14159265358979f;

struct Tables {
	float logR[StrikeTables::MATERIALS][M];

	Tables() {
		float r[StrikeTables::MATERIALS][M];
		// wood and metal: a bar free at both ends. The first five are the spec's
		// figures; the rest carry the same series on, (2k+3)^2 / 9.066.
		static const float bar[5] = {1.f, 2.756f, 5.404f, 8.933f, 13.34f};
		for (int k = 0; k < M; k++) {
			float v = k < 5 ? bar[k] : (float)((2 * k + 3) * (2 * k + 3)) / 9.066f;
			r[0][k] = v;
			r[2][k] = v;
		}
		// rubber: nearly harmonic
		for (int k = 0; k < M; k++)
			r[1][k] = 1.f + 0.9f * (float)k;
		// glass: the spec's five, then the same curve carried on
		static const float glass[5] = {1.f, 2.32f, 4.25f, 6.63f, 9.38f};
		for (int k = 0; k < M; k++)
			r[3][k] = k < 5 ? glass[k] : 0.91f * std::pow((float)(k + 1), 1.45f);
		// membrane: an ideal round drum skin. Zeros of the Bessel functions
		// J0..J13 divided by the first zero of J0 (2.4048), lowest 32.
		static const float skin[M] = {
			1.000f, 1.593f, 2.136f, 2.295f, 2.653f, 2.917f, 3.155f, 3.500f, 3.598f, 3.647f, 4.059f,
			4.132f, 4.230f, 4.601f, 4.610f, 4.832f, 4.903f, 5.084f, 5.131f, 5.412f, 5.540f, 5.553f,
			5.651f, 5.977f, 6.019f, 6.153f, 6.163f, 6.209f, 6.483f, 6.529f, 6.669f, 6.746f};
		for (int k = 0; k < M; k++)
			r[4][k] = skin[k];
		for (int m = 0; m < StrikeTables::MATERIALS; m++)
			for (int k = 0; k < M; k++)
				logR[m][k] = std::log(r[m][k]);
	}
};

const Tables& tables() {
	static const Tables t;
	return t;
}

//                               wood   rubber metal glass  membrane
const float DECAY_SCALE[5] = {0.12f, 0.05f, 1.0f, 0.8f, 0.35f};
const float DECAY_ALPHA[5] = {1.4f, 1.0f, 0.25f, 0.15f, 0.6f};
const float BRIGHTNESS[5] = {-0.3f, -1.5f, 0.f, 0.4f, -0.4f};

// Formant frequencies, bandwidths and levels for a bass voice singing
// a, e, i, o, u. Source: the "Formant Values" appendix of the Csound manual
// (after the tables used at IRCAM for the CHANT synthesiser).
const float V_FREQ[5][5] = {
	{600.f, 1040.f, 2250.f, 2450.f, 2750.f},
	{400.f, 1620.f, 2400.f, 2800.f, 3100.f},
	{250.f, 1750.f, 2600.f, 3050.f, 3340.f},
	{400.f, 750.f, 2400.f, 2600.f, 2900.f},
	{350.f, 600.f, 2400.f, 2675.f, 2950.f}};
const float V_BW[5][5] = {
	{60.f, 70.f, 110.f, 120.f, 130.f},
	{40.f, 80.f, 100.f, 120.f, 120.f},
	{60.f, 90.f, 100.f, 120.f, 120.f},
	{40.f, 80.f, 100.f, 120.f, 120.f},
	{40.f, 80.f, 100.f, 120.f, 120.f}};
const float V_DB[5][5] = {
	{0.f, -7.f, -9.f, -9.f, -20.f},
	{0.f, -12.f, -9.f, -12.f, -18.f},
	{0.f, -30.f, -16.f, -22.f, -28.f},
	{0.f, -11.f, -21.f, -20.f, -40.f},
	{0.f, -20.f, -32.f, -28.f, -36.f}};

inline float lerp(float a, float b, float f) { return a + (b - a) * f; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

const float CHOKE_SECONDS = 0.005f;
const float MODAL_GAIN = 6.f;
const float FORMANT_GAIN = 6.f;
const float EXT_GAIN = 0.03f;

} // namespace

const float* StrikeTables::logRatio(int material) { return tables().logR[std::clamp(material, 0, MATERIALS - 1)]; }
float StrikeTables::decayScale(int material) { return DECAY_SCALE[std::clamp(material, 0, MATERIALS - 1)]; }
float StrikeTables::decayAlpha(int material) { return DECAY_ALPHA[std::clamp(material, 0, MATERIALS - 1)]; }
float StrikeTables::brightness(int material) { return BRIGHTNESS[std::clamp(material, 0, MATERIALS - 1)]; }
const float* StrikeTables::vowelFreq(int vowel) { return V_FREQ[std::clamp(vowel, 0, 4)]; }
const float* StrikeTables::vowelBw(int vowel) { return V_BW[std::clamp(vowel, 0, 4)]; }
const float* StrikeTables::vowelDb(int vowel) { return V_DB[std::clamp(vowel, 0, 4)]; }

float StrikeVoice::softClip(float v) {
	float a = std::fabs(v);
	if (a <= 6.f)
		return v;
	float c = 6.f + 4.f * std::tanh((a - 6.f) * 0.25f);
	return v < 0.f ? -c : c;
}

void StrikeVoice::clearBank(Bank& b) {
	for (int k = 0; k < MODES; k++) {
		b.re[k] = 0.f;
		b.im[k] = 0.f;
	}
	b.active = false;
	b.chokeLeft = 0;
}

void StrikeVoice::reset() {
	clearBank(bank[0]);
	clearBank(bank[1]);
	cur = 0;
	for (int k = 0; k < MODES; k++) {
		cr[k] = 0.f;
		ci[k] = 0.f;
		amp[k] = 0.f;
	}
	nModes = 0;
	controlCount = 0;
	kFreq = -1.f;
	exActive = false;
	exT = 0;
	burstLeft = 0;
	burstTimer = 0.f;
	velHeld = 1.f;
	noiseLp = 0.f;
	glotPhase = 0.f;
	glotPrev = 0.f;
	gateEnv = 0.f;
}

void StrikeVoice::setSampleRate(float sr) {
	if (sr != sampleRate) {
		sampleRate = sr;
		kFreq = -1.f; // work the resonators out again
	}
}

void StrikeVoice::updateCoeffs(const StrikeGlobal& g, const StrikeVoiceIn& in) {
	if (in.freq == kFreq && in.material == kMaterial && in.decay == kDecay && in.damp == kDamp && in.position == kPos
		&& in.bright == kBright && g.inharm == kInharm && g.solo == kSolo && g.formant == kFormant)
		return;
	if (g.formant != kFormant) {
		// the two modes use the resonators differently, so start clean
		clearBank(bank[0]);
		clearBank(bank[1]);
	}
	kFreq = in.freq; kMaterial = in.material; kDecay = in.decay; kDamp = in.damp; kPos = in.position;
	kBright = in.bright; kInharm = g.inharm; kSolo = g.solo; kFormant = g.formant;

	float mat = clampf(in.material, 0.f, 4.f);
	int m0 = std::min((int)mat, 3);
	float mf = mat - (float)m0;
	const float nyq = 0.45f * sampleRate;
	const float w = 2.f * PI / sampleRate;

	if (g.formant) {
		// five vowel filters; MATERIAL picks the vowel, INHARM the size of the mouth
		float size = 1.05f + 0.35f * clampf(g.inharm, -1.f, 1.f); // 0.7 .. 1.4
		const float *f0 = StrikeTables::vowelFreq(m0), *f1 = StrikeTables::vowelFreq(m0 + 1);
		const float *b0 = StrikeTables::vowelBw(m0), *b1 = StrikeTables::vowelBw(m0 + 1);
		const float *d0 = StrikeTables::vowelDb(m0), *d1 = StrikeTables::vowelDb(m0 + 1);
		for (int k = 0; k < 5; k++) {
			float f = lerp(f0[k], f1[k], mf) * size;
			float bw = lerp(b0[k], b1[k], mf);
			float r = std::exp(-PI * bw / sampleRate);
			float level = std::pow(10.f, lerp(d0[k], d1[k], mf) / 20.f);
			cr[k] = r * std::cos(w * f);
			ci[k] = r * std::sin(w * f);
			amp[k] = f < nyq ? level * (1.f - r) * FORMANT_GAIN : 0.f;
		}
		setModeCount(5);
		return;
	}

	const float *l0 = StrikeTables::logRatio(m0), *l1 = StrikeTables::logRatio(m0 + 1);
	// decay scale is blended as a ratio, so wood-to-rubber does not sag in the middle
	float scale = std::exp(lerp(std::log(DECAY_SCALE[m0]), std::log(DECAY_SCALE[m0 + 1]), mf));
	float alpha = lerp(DECAY_ALPHA[m0], DECAY_ALPHA[m0 + 1], mf) + clampf(in.damp, 0.f, 1.f) * 1.5f;
	float tilt = -0.6f + lerp(BRIGHTNESS[m0], BRIGHTNESS[m0 + 1], mf) + (clampf(in.bright, 0.f, 1.f) - 0.5f) * 3.f;
	float stretch = 1.f + clampf(g.inharm, -1.f, 1.f) * 0.1f;
	float pos = 0.03f + 0.94f * clampf(in.position, 0.f, 1.f); // never exactly on an end, where nothing sounds
	float t60 = clampf(in.decay, 0.001f, 20.f) * scale;
	float freq = clampf(in.freq, 1.f, nyq);

	int n = 0;
	float sum = 0.f, sumAbs = 0.f;
	for (int k = 0; k < MODES; k++) {
		float lr = lerp(l0[k], l1[k], mf) * stretch;
		float f = freq * std::exp(lr);
		if (f >= nyq)
			break; // ratios only rise, so every later mode is too high as well
		float t = std::max(t60 * std::exp(-alpha * lr), 0.0005f);
		float r = std::exp(-6.9078f / (t * sampleRate));
		cr[k] = r * std::cos(w * f);
		ci[k] = r * std::sin(w * f);
		float a = std::fabs(std::sin(PI * (float)(k + 1) * pos)) * std::exp(tilt * lr);
		if (g.solo >= 0)
			a = k == g.solo ? 1.f : 0.f;
		amp[k] = a;
		sum += a * a;
		sumAbs += a;
		n = k + 1;
	}
	setModeCount(n);
	// Level: half-way between "all modes arrive together" and "modes arrive at random",
	// so a dense drum skin and a sparse bar come out about equally loud.
	float norm = g.solo >= 0 ? 1.f : (sum > 1e-12f ? MODAL_GAIN / std::sqrt(std::sqrt(sum) * sumAbs) : 0.f);
	for (int k = 0; k < n; k++)
		amp[k] *= norm;
}

// Modes that drop out (too high for the sample rate) must not keep old ringing for later.
void StrikeVoice::setModeCount(int n) {
	for (Bank& b : bank)
		for (int k = n; k < nModes; k++) {
			b.re[k] = 0.f;
			b.im[k] = 0.f;
		}
	nModes = n;
}

void StrikeVoice::fire(float vel) {
	exActive = true;
	exT = 0;
	velHeld = vel;
	noiseLp = 0.f;
	noise.seed(0x5712E); // the same burst of noise every time, so a hit is repeatable
	bank[cur].active = true;
}

float StrikeVoice::process(const StrikeGlobal& g, const StrikeVoiceIn& in) {
	if (controlCount == 0 || in.trig)
		updateCoeffs(g, in);
	bool controlTick = controlCount == 0;
	controlCount = (controlCount + 1) & 15;

	const int chokeSamples = std::max(1, (int)(CHOKE_SECONDS * sampleRate));

	if (in.choke) {
		for (Bank& b : bank)
			if (b.active && b.chokeLeft == 0)
				b.chokeLeft = chokeSamples;
		exActive = false;
		burstLeft = 0;
	}

	float x = 0.f;
	if (g.formant) {
		// The trigger is a gate: a pulse train like vocal cords, with a soft start and a DECAY-long release.
		float target = in.gate ? 1.f : 0.f;
		if (in.trig)
			velHeld = in.vel;
		float rate = target > gateEnv ? 1.f - std::exp(-1.f / (0.005f * sampleRate))
		                              : 1.f - std::exp(-6.9078f / (clampf(in.decay, 0.005f, 20.f) * sampleRate));
		gateEnv += (target - gateEnv) * rate;
		if (gateEnv > 1e-4f || in.gate) {
			if (bank[cur].chokeLeft > 0 && in.trig) {
				cur ^= 1;
				clearBank(bank[cur]);
			}
			bank[cur].active = true;
			float freq = clampf(in.freq, 1.f, 0.25f * sampleRate);
			glotPhase += freq / sampleRate;
			if (glotPhase >= 1.f)
				glotPhase -= 1.f;
			// Rosenberg pulse: the cords open over two thirds of the open time and close over the rest
			float oq = 0.3f + 0.5f * clampf(g.hardness, 0.f, 1.f);
			float tOpen = oq * (2.f / 3.f), tClose = oq / 3.f;
			float p;
			if (glotPhase < tOpen)
				p = 0.5f * (1.f - std::cos(PI * glotPhase / tOpen));
			else if (glotPhase < tOpen + tClose)
				p = std::cos(PI * (glotPhase - tOpen) / (2.f * tClose));
			else
				p = 0.f;
			// the change in the pulse is what drives the filters (no steady push)
			// (measured per cycle, so every pitch is about as loud)
			x = (p - glotPrev) * (sampleRate / freq) * gateEnv * velHeld;
			glotPrev = p;
		}
		else {
			glotPrev = 0.f;
			glotPhase = 0.f;
		}
	}
	else {
		if (in.trig) {
			Bank& b = bank[cur];
			if (b.chokeLeft > 0 || (g.chokeOnRetrig && b.active)) {
				// let the old ringing fade over 5 ms in its own bank and start the new hit in the other
				if (b.chokeLeft == 0)
					b.chokeLeft = chokeSamples;
				cur ^= 1;
				clearBank(bank[cur]);
			}
			burstLeft = std::clamp(g.burst, 1, 16) - 1;
			burstTimer = sampleRate / clampf(g.burstRate, 2.f, 200.f);
			fire(clampf(in.vel, 0.f, 1.f));
		}
		else if (burstLeft > 0) {
			burstTimer -= 1.f;
			if (burstTimer <= 0.f) {
				burstTimer += sampleRate / clampf(g.burstRate, 2.f, 200.f);
				burstLeft--;
				fire(velHeld);
			}
		}

		float e = clampf(g.exciter, 0.f, 3.f);
		int e0 = std::min((int)e, 2);
		float ef = e - (float)e0;
		float wgt[4] = {0.f, 0.f, 0.f, 0.f};
		wgt[e0] = 1.f - ef;
		wgt[e0 + 1] = ef;
		float hard = clampf(g.hardness, 0.f, 1.f);

		if (exActive) {
			// mallet: 8 ms (soft) down to 0.2 ms (hard); noise: 50 ms down to 1 ms
			float malletLen = std::max(2.f, 0.008f * std::pow(0.025f, hard) * sampleRate);
			float noiseLen = std::max(2.f, 0.05f * std::pow(0.02f, hard) * sampleRate);
			float extLen = noiseLen * 4.f;
			float t = (float)exT;
			if (wgt[0] > 0.f && exT == 0)
				x += wgt[0];
			if (wgt[1] > 0.f && t < malletLen)
				x += wgt[1] * (1.f - std::cos(2.f * PI * t / malletLen)) / malletLen;
			if (wgt[2] > 0.f && t < noiseLen) {
				float cutoff = 200.f * std::pow(90.f, hard); // 200 Hz .. 18 kHz
				float a = 1.f - std::exp(-2.f * PI * std::min(cutoff, 0.45f * sampleRate) / sampleRate);
				noiseLp += (noise.bipolar() - noiseLp) * a;
				float env = 1.f - t / noiseLen;
				x += wgt[2] * noiseLp * env * env * std::sqrt(15.f / noiseLen);
			}
			if (wgt[3] > 0.f && g.extGated && t < extLen) {
				float env = 1.f - t / extLen;
				x += wgt[3] * in.ext * env * env * EXT_GAIN * 4.f;
			}
			x *= velHeld;
			exT++;
			if (t >= std::max(std::max(malletLen, noiseLen), extLen))
				exActive = false;
		}
		if (wgt[3] > 0.f && !g.extGated && in.ext != 0.f) {
			x += wgt[3] * in.ext * EXT_GAIN * clampf(in.vel, 0.f, 1.f);
			bank[cur].active = true;
		}
	}

	float out = 0.f;
	const int n = nModes;
	for (int bi = 0; bi < 2; bi++) {
		Bank& b = bank[bi];
		if (!b.active)
			continue;
		float xin = bi == cur ? x : 0.f;
		float acc = 0.f;
		for (int k = 0; k < n; k++) {
			float t = b.re[k] * cr[k] - b.im[k] * ci[k] + xin;
			b.im[k] = b.re[k] * ci[k] + b.im[k] * cr[k];
			b.re[k] = t;
			acc += amp[k] * b.im[k];
		}
		if (b.chokeLeft > 0) {
			// fade to -60 dB over the choke time
			float cg = std::exp(-6.9078f / (float)chokeSamples);
			for (int k = 0; k < n; k++) {
				b.re[k] *= cg;
				b.im[k] *= cg;
			}
			if (--b.chokeLeft == 0)
				clearBank(b);
		}
		else if (controlTick && !(bi == cur && (exActive || burstLeft > 0 || (g.formant && gateEnv > 1e-4f)))) {
			// put a bank to sleep once it has rung out
			float energy = 0.f;
			for (int k = 0; k < n; k++)
				energy += b.re[k] * b.re[k] + b.im[k] * b.im[k];
			if (energy < 1e-12f)
				clearBank(b);
		}
		out += acc;
	}
	if (!std::isfinite(out)) {
		reset();
		return 0.f;
	}
	return softClip(out);
}

} // namespace codex
