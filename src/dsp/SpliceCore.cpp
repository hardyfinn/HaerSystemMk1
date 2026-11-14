#include "dsp/SpliceCore.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

namespace {
const float HALF_PI = 1.5707963f;
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

void SpliceCore::setSampleRate(float sr) {
	sampleRate = sr > 1000.f ? sr : 48000.f;
	frames = (size_t)(SECONDS * sampleRate);
	tapeL.assign(frames, 0.f);
	tapeR.assign(frames, 0.f);
	fadeLen = std::max(2, (int)(FADE_SECONDS * sampleRate));
	clear();
}

void SpliceCore::clear() {
	std::fill(tapeL.begin(), tapeL.end(), 0.f);
	std::fill(tapeR.begin(), tapeR.end(), 0.f);
	for (float& o : overview)
		o = 0.f;
	writePos = 0;
	cur = Player();
	prev = Player();
	fadeLeft = 0;
	active = false;
	gateWas = false;
	holdLeft = 0.0;
	k = 0;
	for (Grain& g : grains)
		g.on = false;
	grainTimer = 0.f;
	scrubOffset = 0.f;
	binNow = -1;
	sliceLen = 0.25 * sampleRate;
	start = 0.0;
}

void SpliceCore::rebuildOverview() {
	for (float& o : overview)
		o = 0.f;
	if (!frames)
		return;
	for (size_t i = 0; i < frames; i++) {
		size_t b = i * BINS / frames;
		float a = std::max(std::fabs(tapeL[i]), std::fabs(tapeR[i]));
		if (a > overview[b])
			overview[b] = a;
	}
}

// The tape at a position between two frames (straight line between them).
void SpliceCore::read(double pos, float& l, float& r) const {
	double n = (double)frames;
	pos -= n * std::floor(pos / n);
	size_t i = (size_t)pos;
	if (i >= frames)
		i = frames - 1;
	size_t j = i + 1 == frames ? 0 : i + 1;
	float f = (float)(pos - (double)i);
	l = tapeL[i] + (tapeL[j] - tapeL[i]) * f;
	r = tapeR[i] + (tapeR[j] - tapeR[i]) * f;
}

void SpliceCore::play(Player& pl, float inL, float inR, float& l, float& r) {
	if (pl.live) {
		l = inL;
		r = inR;
		return;
	}
	read(pl.pos, l, r);
	l *= pl.level;
	r *= pl.level;
	pl.pos += pl.inc;
}

void SpliceCore::startRepeat(const SpliceParams& p, SpliceOut& out) {
	const double minLen = 0.002 * sampleRate;
	prev = cur;
	fadeLeft = fadeLen;
	double len = std::min(std::max(lenRaw, minLen), sliceLen);
	float semis = clampf(p.pitchRamp * (float)k, -48.f, 48.f);
	double speed = std::exp2((double)semis / 12.0);
	bool backwards = p.reverse > 0.f && rng.uniform() < p.reverse;
	cur.live = false;
	cur.level = levelNow;
	cur.inc = backwards ? -speed : speed;
	cur.pos = backwards ? start + len : start;
	// tape style: a faster repeat is also a shorter one
	cur.remaining = (int)std::max(minLen, std::round(len / speed));
	out.sliceTrig = true;
	// ready the next one
	lenRaw = std::min(std::max(lenRaw * (double)clampf(p.accel, 0.5f, 2.f), minLen * 0.5), sliceLen * 2.0);
	levelNow *= clampf(p.decay, 0.5f, 1.f);
}

void SpliceCore::process(const SpliceParams& p, float inL, float inR, SpliceOut& out) {
	out.sliceTrig = false;
	if (!frames) {
		out.l = inL;
		out.r = inR;
		out.repeat = 0;
		return;
	}
	// ---- record ----
	bool recording = p.rec == SpliceParams::REC_ALWAYS ? !p.freeze : (p.rec == SpliceParams::REC_GATED ? p.recGate : false);
	if (recording) {
		tapeL[writePos] = inL;
		tapeR[writePos] = inR;
		int b = (int)(writePos * BINS / frames);
		float a = std::max(std::fabs(inL), std::fabs(inR));
		if (b != binNow) {
			binNow = b;
			overview[b] = a;
		}
		else if (a > overview[b])
			overview[b] = a;
		if (++writePos >= frames)
			writePos = 0;
	}

	// ---- stutter ----
	// whole samples, so an untouched repeat is an exact copy
	double wantLen = std::round(std::min(std::max((double)p.sliceSeconds * sampleRate, 0.002 * sampleRate), (double)frames * 0.5));
	bool rising = p.stutter && !gateWas;
	gateWas = p.stutter;
	if (rising) {
		// take the slice that has just gone by
		if (pendingSeed != seed || !active) {
			seed = pendingSeed;
			rng = Rng((uint64_t)seed).fork("reverse");
			grainRng = Rng((uint64_t)seed).fork("grains");
		}
		sliceLen = wantLen;
		start = (double)writePos - sliceLen;
		lenRaw = sliceLen;
		levelNow = 1.f;
		k = 0;
		holdLeft = (double)std::max(p.holdSlices, 0.f) * sliceLen;
		active = true;
		startRepeat(p, out);
	}
	else if (active) {
		if (holdLeft > 0.0)
			holdLeft -= 1.0;
		if (!p.stutter && holdLeft <= 0.0) {
			// let go: back to the live input
			active = false;
			prev = cur;
			cur = Player();
			fadeLeft = fadeLen;
		}
		else if (--cur.remaining <= 0) {
			k++;
			startRepeat(p, out);
		}
	}
	if (!active)
		sliceLen = wantLen;
	out.repeat = active ? k : 0;

	float l, r;
	play(cur, inL, inR, l, r);
	if (fadeLeft > 0) {
		// 2 ms equal-power crossfade from what was playing before
		float pl, pr;
		play(prev, inL, inR, pl, pr);
		float t = 1.f - (float)fadeLeft / (float)fadeLen;
		float gin = std::sin(t * HALF_PI), gout = std::cos(t * HALF_PI);
		l = l * gin + pl * gout;
		r = r * gin + pr * gout;
		fadeLeft--;
	}

	// the slice the scrub input and the grains work in: the held one, or the newest
	double regionStart = active ? start : (double)writePos - sliceLen;
	double regionLen = sliceLen;

	// ---- scrub: the play position comes from a control voltage ----
	if (p.scrubOn) {
		float target = clampf(p.scrub, 0.f, 1.f) * (float)regionLen;
		scrubOffset += (target - scrubOffset) * (1.f - std::exp(-1.f / (0.003f * sampleRate)));
		read(regionStart + (double)scrubOffset, l, r);
	}

	// ---- smear: grains ----
	float smear = clampf(p.smear, 0.f, 1.f);
	if (smear > 0.f) {
		float density = clampf(p.density, 2.f, 100.f);
		float glenS = clampf(p.grain, 0.005f, 0.2f);
		grainTimer -= 1.f;
		if (grainTimer <= 0.f) {
			grainTimer += sampleRate / density;
			for (Grain& g : grains) {
				if (g.on)
					continue;
				g.on = true;
				g.age = 0;
				g.len = std::max(8, (int)(glenS * sampleRate));
				double base = active && !p.scrubOn ? cur.pos : regionStart + (double)scrubOffset;
				g.pos = base + (double)(clampf(p.jitter, 0.f, 1.f) * grainRng.bipolar()) * regionLen;
				// never read across the record head, where old and new tape meet
				double newest = (double)writePos - (double)g.len - 2.0;
				if (recording && g.pos > newest)
					g.pos = newest;
				g.pos -= (double)frames * std::floor(g.pos / (double)frames);
				g.wc = 1.f;
				g.ws = 0.f;
				g.dc = std::cos(6.2831853f / (float)g.len);
				g.ds = std::sin(6.2831853f / (float)g.len);
				float pan = grainRng.bipolar() * clampf(p.width, 0.f, 1.f);
				g.gl = std::cos((pan + 1.f) * 0.5f * HALF_PI) * 1.2f;
				g.gr = std::sin((pan + 1.f) * 0.5f * HALF_PI) * 1.2f;
				break;
			}
		}
		float gl = 0.f, gr = 0.f;
		for (Grain& g : grains) {
			if (!g.on)
				continue;
			// g.pos is kept inside the tape, so this is read() without the wrapping sums
			size_t i = (size_t)g.pos;
			if (i >= frames)
				i = frames - 1;
			size_t j = i + 1 == frames ? 0 : i + 1;
			float f = (float)(g.pos - (double)i);
			float sl = tapeL[i] + (tapeL[j] - tapeL[i]) * f;
			float sr = tapeR[i] + (tapeR[j] - tapeR[i]) * f;
			float w = 0.5f - 0.5f * g.wc; // Hann window
			float nc = g.wc * g.dc - g.ws * g.ds;
			g.ws = g.ws * g.dc + g.wc * g.ds;
			g.wc = nc;
			gl += sl * w * g.gl;
			gr += sr * w * g.gr;
			g.pos += 1.0;
			if (g.pos >= (double)frames)
				g.pos -= (double)frames;
			if (++g.age >= g.len)
				g.on = false;
		}
		// more overlapping grains are turned down so the cloud stays about as loud as the source
		float overlap = density * glenS;
		float norm = 1.f / std::sqrt(std::max(1.f, overlap * 0.5f));
		float gs = std::sin(smear * HALF_PI), gd = std::cos(smear * HALF_PI);
		l = l * gd + gl * norm * gs;
		r = r * gd + gr * norm * gs;
	}
	else {
		for (Grain& g : grains)
			g.on = false;
		grainTimer = 0.f;
	}
	out.l = l;
	out.r = r;
}

} // namespace codex
