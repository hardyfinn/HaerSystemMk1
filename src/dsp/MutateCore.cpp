#include "dsp/MutateCore.hpp"
#include "common/Curve.hpp"
#include <cmath>
#include <algorithm>
#include <climits>

namespace codex {

static inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }
static inline int clampi(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }

bool euclid(int i, int k, int len, int rot) {
	if (k <= 0 || len <= 0)
		return false;
	if (k >= len)
		return true;
	int j = ((i + rot) % len + len) % len;
	return (j * k) % len < k;
}

MutateCore::MutateCore() {
	static const int defStyle[8] = {STYLE_KICK, STYLE_SNARE, STYLE_HAT, STYLE_PERC, STYLE_HAT, STYLE_PERC, STYLE_TEXTURE, STYLE_FREE};
	// the last three tracks have odd lengths so they drift against the bar
	static const int defLen[8] = {16, 16, 16, 16, 16, 12, 20, 14};
	for (int t = 0; t < 8; t++) {
		pat.track[t].style = defStyle[t];
		pat.track[t].length = defLen[t];
	}
	home = pat;
	mutRng = Rng(0).fork("mutate");
	reset();
}

void MutateCore::reset() {
	for (int t = 0; t < 8; t++) {
		pendingCount[t] = 0;
		resolved[t] = INT64_MIN;
		lastFired[t] = false;
	}
	stepNow = -1;
}

// ---------------------------------------------------------------- generation

void MutateCore::generateTrack(int t, const MutateParams& in) {
	MutateTrack& tr = pat.track[t];
	Rng r = Rng((uint64_t)pendingSeed).fork("gen", (uint64_t)t);
	int len = clampi(tr.length, 1, MutateTrack::MAX_STEPS);
	int perBar = clampi(in.steps, 4, 32);
	float d = clampf(in.density, 0.f, 1.f);
	for (int i = 0; i < MutateTrack::MAX_STEPS; i++)
		tr.step[i] = MutateStep();

	// The two sound locks follow smooth curves across the track, so the sound changes in shapes, not noise.
	float cx[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f}, ca[5], cb[5];
	for (int k = 0; k < 5; k++) {
		ca[k] = r.uniform();
		cb[k] = r.uniform();
	}
	Spline la, lb;
	la.set(cx, ca, 5);
	lb.set(cx, cb, 5);
	for (int i = 0; i < len; i++) {
		float x = len > 1 ? (float)i / (float)(len - 1) : 0.f;
		tr.step[i].lockA = clampf(la.eval(x), 0.f, 1.f);
		tr.step[i].lockB = clampf(lb.eval(x), 0.f, 1.f);
	}

	switch (tr.style) {
		case STYLE_KICK: {
			// evenly spread hits; full density is four to the bar
			int k = std::max(1, (int)std::lround(d * (float)len / 4.f));
			// nearly always start on the downbeat; now and then the whole pattern sits on the off-beats
			int rot = r.chance(0.15f) ? std::max(len / (2 * k), 1) : 0;
			for (int i = 0; i < len; i++) {
				if (euclid(i, k, len, rot)) {
					tr.step[i].on = true;
					tr.step[i].vel = r.range(0.85f, 1.f);
				}
			}
			// now and then an extra, quieter hit on an off-beat step
			for (int q = 0; q + 3 < len; q += 4) {
				if (r.chance(d * 0.3f)) {
					int i = q + 1 + 2 * r.below(2); // the 2nd or 4th step of the group
					if (!tr.step[i].on) {
						tr.step[i].on = true;
						tr.step[i].vel = r.range(0.55f, 0.8f);
					}
				}
			}
			break;
		}
		case STYLE_SNARE: {
			// backbeats: beats 2 and 4 of each bar, wherever they fall in the track
			float beat = (float)perBar / 4.f;
			for (int bar = 0; bar * perBar < len; bar++) {
				for (int b = 1; b < 4; b += 2) {
					int i = bar * perBar + (int)std::lround(beat * (float)b);
					if (i < len) {
						tr.step[i].on = true;
						tr.step[i].vel = r.range(0.85f, 1.f);
					}
				}
			}
			// ghost notes
			for (int i = 0; i < len; i++) {
				if (!tr.step[i].on && r.chance(d * 0.18f)) {
					tr.step[i].on = true;
					tr.step[i].vel = r.range(0.2f, 0.4f);
					if (r.chance(0.4f)) {
						tr.step[i].cond = COND_PROB;
						tr.step[i].p = r.range(0.5f, 0.9f);
					}
				}
			}
			break;
		}
		case STYLE_HAT: {
			int k = clampi((int)std::lround((float)len * (0.3f + 0.6f * d)), 1, len);
			int rot = r.below(len);
			float vx[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f}, vy[4];
			for (int j = 0; j < 4; j++)
				vy[j] = r.range(0.35f, 1.f);
			Spline v;
			v.set(vx, vy, 4);
			for (int i = 0; i < len; i++) {
				if (!euclid(i, k, len, rot))
					continue;
				tr.step[i].on = true;
				tr.step[i].vel = clampf(v.eval(len > 1 ? (float)i / (float)(len - 1) : 0.f), 0.2f, 1.f);
				if (r.chance(0.1f))
					tr.step[i].ratchet = (uint8_t)r.between(2, 3);
			}
			break;
		}
		case STYLE_PERC:
		case STYLE_TEXTURE:
		case STYLE_FREE:
		default: {
			float chance = tr.style == STYLE_PERC ? 0.08f + d * 0.22f : (tr.style == STYLE_TEXTURE ? 0.05f + d * 0.14f : 0.08f + d * 0.25f);
			int maxRatchet = tr.style == STYLE_PERC ? 4 : (tr.style == STYLE_TEXTURE ? 8 : 3);
			float microRange = tr.style == STYLE_PERC ? 0.25f : 0.5f;
			bool any = false;
			for (int i = 0; i < len; i++) {
				if (!r.chance(chance))
					continue;
				any = true;
				MutateStep& s = tr.step[i];
				s.on = true;
				s.vel = tr.style == STYLE_TEXTURE ? r.range(0.3f, 0.7f) : r.range(0.45f, 1.f);
				s.micro = std::round(r.range(-microRange, microRange) * 20.f) / 20.f;
				if (r.chance(tr.style == STYLE_TEXTURE ? 0.8f : 0.35f))
					s.ratchet = (uint8_t)r.between(tr.style == STYLE_TEXTURE ? 3 : 2, maxRatchet);
				if (tr.style == STYLE_FREE && r.chance(0.5f)) {
					s.cond = r.chance(0.6f) ? COND_PROB : COND_EVERY_N;
					s.p = r.range(0.4f, 0.9f);
					s.n = (uint8_t)r.between(2, 4);
				}
			}
			if (!any) { // never leave a track silent
				MutateStep& s = tr.step[r.below(len)];
				s.on = true;
				s.vel = 0.7f;
			}
			break;
		}
	}
	version++;
}

void MutateCore::generate(const MutateParams& in) {
	for (int t = 0; t < 8; t++)
		generateTrack(t, in);
	home = pat;
	seed = pendingSeed;
	mutRng = Rng((uint64_t)seed).fork("mutate");
	version++;
}

// ---------------------------------------------------------------- mutation

int MutateCore::pickHit(int t, bool preferQuiet) {
	const MutateTrack& tr = pat.track[t];
	int idx[MutateTrack::MAX_STEPS], n = 0, quiet[MutateTrack::MAX_STEPS], nq = 0;
	for (int i = 0; i < tr.length; i++) {
		if (!tr.step[i].on)
			continue;
		idx[n++] = i;
		if (tr.step[i].vel < 0.5f)
			quiet[nq++] = i;
	}
	if (n == 0)
		return -1;
	if (preferQuiet && nq > 0)
		return quiet[mutRng.below(nq)];
	return idx[mutRng.below(n)];
}

void MutateCore::mutate(int rule) {
	int t = mutRng.below(8);
	MutateTrack& tr = pat.track[t];
	int len = clampi(tr.length, 1, MutateTrack::MAX_STEPS);
	switch (rule) {
		case RULE_ROTATE: {
			if (mutRng.chance(0.5f)) { // one step later
				MutateStep last = tr.step[len - 1];
				for (int i = len - 1; i > 0; i--)
					tr.step[i] = tr.step[i - 1];
				tr.step[0] = last;
			}
			else { // one step earlier
				MutateStep first = tr.step[0];
				for (int i = 0; i < len - 1; i++)
					tr.step[i] = tr.step[i + 1];
				tr.step[len - 1] = first;
			}
			break;
		}
		case RULE_NUDGE: {
			int i = pickHit(t);
			if (i >= 0)
				tr.step[i].micro = clampf(tr.step[i].micro + (mutRng.chance(0.5f) ? 0.15f : -0.15f), -0.5f, 0.5f);
			break;
		}
		case RULE_RATCHET: {
			int i = pickHit(t);
			if (i >= 0)
				tr.step[i].ratchet = (tr.step[i].ratchet > 1 && mutRng.chance(0.5f)) ? 1 : (uint8_t)mutRng.between(2, 4);
			break;
		}
		case RULE_THIN_FILL: {
			int hits = 0;
			for (int i = 0; i < len; i++)
				hits += tr.step[i].on ? 1 : 0;
			bool thin = hits > 1 && (hits >= len || mutRng.chance(0.5f));
			if (thin) {
				int i = pickHit(t, true); // quiet hits go first
				if (i >= 0)
					tr.step[i].on = false;
			}
			else {
				int gaps[MutateTrack::MAX_STEPS], n = 0;
				for (int i = 0; i < len; i++)
					if (!tr.step[i].on)
						gaps[n++] = i;
				if (n > 0) {
					MutateStep& s = tr.step[gaps[mutRng.below(n)]];
					s.on = true;
					s.vel = mutRng.range(0.5f, 0.9f);
					s.ratchet = 1;
					s.micro = 0.f;
					s.cond = COND_ALWAYS;
				}
			}
			break;
		}
		case RULE_MIRROR: {
			int span = std::min(mutRng.between(4, 8), len);
			int start = mutRng.below(len);
			for (int a = 0, b = span - 1; a < b; a++, b--)
				std::swap(tr.step[(start + a) % len], tr.step[(start + b) % len]);
			break;
		}
		case RULE_COPY: {
			int from = (t + 1 + mutRng.below(7)) % 8;
			const MutateTrack& src = pat.track[from];
			int srcLen = clampi(src.length, 1, MutateTrack::MAX_STEPS);
			int span = mutRng.between(4, 8);
			int start = mutRng.below(len);
			for (int k = 0; k < span; k++) {
				MutateStep& dst = tr.step[(start + k) % len];
				float a = dst.lockA, b = dst.lockB; // the destination keeps its own sound locks
				dst = src.step[(start + k) % srcLen];
				dst.lockA = a;
				dst.lockB = b;
			}
			break;
		}
		case RULE_LOCKWALK: {
			int run = std::min(mutRng.between(2, 6), len);
			int start = mutRng.below(len);
			float da = mutRng.chance(0.5f) ? 0.1f : -0.1f;
			float db = mutRng.chance(0.5f) ? 0.1f : -0.1f;
			for (int k = 0; k < run; k++) {
				MutateStep& s = tr.step[(start + k) % len];
				s.lockA = clampf(s.lockA + da, 0.f, 1.f);
				s.lockB = clampf(s.lockB + db, 0.f, 1.f);
			}
			break;
		}
		default: break;
	}
	version++;
}

bool MutateCore::returnStep() {
	int total = 0;
	for (int t = 0; t < 8; t++)
		total += differing(t);
	if (total == 0)
		return false;
	int pick = mutRng.below(total);
	for (int t = 0; t < 8; t++) {
		MutateTrack& tr = pat.track[t];
		for (int i = 0; i < tr.length; i++) {
			if (tr.step[i].same(home.track[t].step[i]))
				continue;
			if (pick-- == 0) {
				tr.step[i] = home.track[t].step[i];
				version++;
				return true;
			}
		}
	}
	return false;
}

int MutateCore::differing(int t) const {
	int n = 0;
	const MutateTrack& tr = pat.track[t];
	for (int i = 0; i < tr.length; i++)
		if (!tr.step[i].same(home.track[t].step[i]))
			n++;
	return n;
}

float MutateCore::distance(int t) const {
	int len = std::max(pat.track[t].length, 1);
	return (float)differing(t) / (float)len;
}

void MutateCore::mutatePass(const MutateParams& in) {
	int ops = (int)std::lround(clampf(in.amount, 0.f, 1.f) * 4.f);
	float total = 0.f;
	for (int k = 0; k < RULE_COUNT; k++)
		total += std::max(in.weight[k], 0.f);
	for (int n = 0; n < ops; n++) {
		if (!mutRng.chance(in.drift)) {
			returnStep(); // a step back toward home
			continue;
		}
		if (total <= 0.f)
			continue;
		// pick a rule; a rule with a bigger weight is picked more often
		float x = mutRng.uniform() * total;
		int rule = RULE_COUNT - 1;
		for (int k = 0; k < RULE_COUNT; k++) {
			x -= std::max(in.weight[k], 0.f);
			if (x < 0.f) {
				rule = k;
				break;
			}
		}
		mutate(rule);
	}
}

// ---------------------------------------------------------------- hand edits

void MutateCore::setStepOn(int t, int i, bool on) {
	if (t < 0 || t >= 8 || i < 0 || i >= MutateTrack::MAX_STEPS)
		return;
	pat.track[t].step[i].on = on;
	version++;
}
void MutateCore::setStepVel(int t, int i, float vel) {
	if (t < 0 || t >= 8 || i < 0 || i >= MutateTrack::MAX_STEPS)
		return;
	pat.track[t].step[i].vel = clampf(vel, 0.05f, 1.f);
	version++;
}
void MutateCore::setLength(int t, int len) {
	if (t < 0 || t >= 8)
		return;
	len = clampi(len, 1, MutateTrack::MAX_STEPS);
	pat.track[t].length = len;
	home.track[t].length = len;
	version++;
}
void MutateCore::setMute(int t, bool m) {
	if (t < 0 || t >= 8)
		return;
	pat.track[t].mute = m;
	home.track[t].mute = m;
	version++;
}
void MutateCore::setStyle(int t, int style) {
	if (t < 0 || t >= 8)
		return;
	pat.track[t].style = clampi(style, 0, STYLE_COUNT - 1);
	home.track[t].style = pat.track[t].style;
	version++;
}

// ---------------------------------------------------------------- playing

float MutateCore::offsetOf(const MutateParams& in, int64_t g, const MutateStep& s) const {
	float off = s.micro + in.microCv;
	if (g & 1)
		off += clampf(in.swing, 0.f, 0.5f);
	return clampf(off, -0.95f, 1.5f);
}

// Decide whether step g of track t plays, and if so queue its hits.
void MutateCore::resolve(const MutateParams& in, int t, int64_t g) {
	resolved[t] = g;
	const MutateTrack& tr = pat.track[t];
	int len = clampi(tr.length, 1, MutateTrack::MAX_STEPS);
	const MutateStep& s = tr.step[(int)(g % len)];
	int64_t pass = g / len;
	bool fire = s.on && !tr.mute;
	if (fire) {
		switch (s.cond) {
			case COND_PROB:
				// the dice depend only on seed, track and step, so a run can be repeated exactly
				fire = Rng((uint64_t)seed).fork("cond", (uint64_t)t * 0x100000000ull + (uint64_t)g).chance(s.p);
				break;
			case COND_EVERY_N: fire = (pass % std::max<int>(s.n, 1)) == 0; break;
			case COND_FIRST: fire = pass == 0; break;
			case COND_PREV: fire = lastFired[t]; break;
			default: break;
		}
	}
	lastFired[t] = fire;
	if (!fire)
		return;
	float off = offsetOf(in, g, s);
	int n = clampi(s.ratchet, 1, 8);
	float exponent = std::pow(2.f, 2.f * clampf(in.ratchetCurve, -1.f, 1.f)); // -1: 0.25 (speeds up), +1: 4 (slows down)
	float vel = s.vel;
	for (int k = 0; k < n; k++) {
		if (pendingCount[t] >= PENDING)
			break;
		float sub = n > 1 ? std::pow((float)k / (float)n, exponent) : 0.f;
		Pending& p = pending[t][pendingCount[t]++];
		p.pos = (double)g + (double)off + (double)sub;
		p.vel = vel;
		p.lockA = s.lockA;
		p.lockB = s.lockB;
		vel *= clampf(in.ratchetDecay, 0.f, 1.f);
	}
}

void MutateCore::barLine(const MutateParams& in, int64_t bar) {
	if (pendingSeed != seed) {
		seed = pendingSeed;
		mutRng = Rng((uint64_t)seed).fork("mutate");
	}
	if (wantHome) {
		wantHome = false;
		pat = home;
		version++;
	}
	else if (wantMutate || (bar > 0 && bar % std::max(in.every, 1) == 0)) {
		mutatePass(in);
	}
	wantMutate = false;
}

void MutateCore::process(const MutateParams& in, const Crossings& entered, double pos, bool backward, MutateOut& out) {
	out.accent = false;
	out.bar = false;
	for (int t = 0; t < 8; t++)
		out.gate[t] = false;

	if (in.steps != lastSteps) {
		// STEPS was turned: the step numbering has changed, so start counting afresh from where we are
		lastSteps = in.steps;
		int64_t here = (int64_t)std::floor(pos) - 1;
		for (int t = 0; t < 8; t++) {
			pendingCount[t] = 0;
			resolved[t] = std::min(resolved[t], here);
		}
	}
	if (backward) {
		// running backwards plays nothing; forget what was queued and don't replay the step we land in
		int64_t here = (int64_t)std::floor(pos);
		for (int t = 0; t < 8; t++) {
			pendingCount[t] = 0;
			resolved[t] = here;
		}
		stepNow = here;
	}
	else {
		int perBar = clampi(in.steps, 4, 32);
		for (int e = 0; e < entered.count; e++) {
			int64_t g = entered.index[e];
			if (g < 0)
				continue;
			stepNow = g;
			if (g % perBar == 0) {
				out.bar = true;
				barLine(in, g / perBar); // mutations land exactly on the bar line
			}
			for (int t = 0; t < 8; t++) {
				if (resolved[t] < g)
					resolve(in, t, g);
				// a hit that is pulled early belongs to the next step but has to start inside this one
				const MutateTrack& tr = pat.track[t];
				int len = clampi(tr.length, 1, MutateTrack::MAX_STEPS);
				const MutateStep& next = tr.step[(int)((g + 1) % len)];
				if (resolved[t] < g + 1 && next.on && offsetOf(in, g + 1, next) < 0.f)
					resolve(in, t, g + 1);
			}
		}
		// fire everything whose time has come
		for (int t = 0; t < 8; t++) {
			int n = pendingCount[t];
			for (int i = 0; i < n;) {
				if (pending[t][i].pos <= pos) {
					out.gate[t] = true;
					velHeld[t] = pending[t][i].vel;
					lockAHeld[t] = pending[t][i].lockA;
					lockBHeld[t] = pending[t][i].lockB;
					if (pending[t][i].vel > 0.8f)
						out.accent = true;
					pending[t][i] = pending[t][--n];
				}
				else {
					i++;
				}
			}
			pendingCount[t] = n;
		}
	}
	for (int t = 0; t < 8; t++) {
		out.vel[t] = velHeld[t];
		out.lockA[t] = lockAHeld[t];
		out.lockB[t] = lockBHeld[t];
	}
}

} // namespace codex
