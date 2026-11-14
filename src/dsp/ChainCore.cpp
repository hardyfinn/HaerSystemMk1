#include "dsp/ChainCore.hpp"
#include <cmath>
#include <algorithm>

namespace codex {

namespace {

inline int mod12(int x) { return ((x % 12) + 12) % 12; }

constexpr uint16_t mask(std::initializer_list<int> notes) {
	uint16_t m = 0;
	for (int n : notes)
		m = (uint16_t)(m | (1 << n));
	return m;
}

// For every one of the 12 notes above the root: where it sits among the scale's
// notes. Notes of the scale get 0, 1, 2 ...; notes outside it sit between
// their neighbours. Also returns how many notes the scale has.
int degreePositions(uint16_t scale, float pos[12]) {
	int deg[12], n = 0;
	for (int i = 0; i < 12; i++)
		if ((scale >> i) & 1)
			deg[n++] = i;
	if (n == 0) {
		for (int i = 0; i < 12; i++)
			pos[i] = (float)i;
		return 12;
	}
	for (int i = 0; i < 12; i++) {
		// the scale note at or below i, and the next one up (going round the octave)
		int k = -1;
		for (int d = 0; d < n; d++)
			if (deg[d] <= i)
				k = d;
		int lo = k >= 0 ? deg[k] : deg[n - 1] - 12;
		int kLo = k >= 0 ? k : -1;
		int hi = k + 1 < n ? deg[k + 1] : deg[0] + 12;
		pos[i] = (float)kLo + (float)(i - lo) / (float)(hi - lo);
	}
	return n;
}

// how much each distance in semitones (0..6, either direction) is liked
const float THIRDS_FIFTHS[7] = {0.15f, 0.35f, 0.5f, 1.f, 1.f, 0.9f, 0.2f};
const float WIDE[7] = {0.1f, 0.25f, 0.15f, 0.3f, 0.4f, 0.8f, 1.f};

} // namespace

const uint16_t ChainCore::SCALES[10] = {
	0xFFF,
	mask({0, 2, 4, 5, 7, 9, 11}),
	mask({0, 2, 3, 5, 7, 8, 10}),
	mask({0, 2, 3, 5, 7, 9, 10}),
	mask({0, 1, 3, 5, 7, 8, 10}),
	mask({0, 2, 4, 6, 7, 9, 11}),
	mask({0, 2, 4, 6, 8, 10}),
	mask({0, 3, 5, 7, 10}),
	mask({0, 2, 3, 7, 8}),
	mask({0, 1, 3, 7, 8}),
};
const char* const ChainCore::SCALE_NAMES[10] = {
	"Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Whole tone", "Pentatonic minor", "Hirajoshi",
	"Clusters (0 1 3 7 8)"};

bool ChainCore::inScale(uint16_t scale, int root, int note) { return (scale >> mod12(note - root)) & 1; }

void ChainCore::characterMatrix(float m[12][12], float character, uint16_t scale) {
	float pos[12];
	int n = degreePositions(scale, pos);
	float c = character < 0.f ? 0.f : (character > 1.f ? 1.f : character);
	for (int i = 0; i < 12; i++) {
		for (int j = 0; j < 12; j++) {
			// stepwise: a bell curve on the distance in scale steps, one step wide.
			// Staying on the same note is allowed but not favoured.
			float d = std::fabs(pos[i] - pos[j]);
			d = std::min(d, (float)n - d);
			float stepwise = std::exp(-0.5f * d * d);
			if (i == j)
				stepwise = 0.3f;
			int ic = std::abs(i - j);
			ic = std::min(ic, 12 - ic);
			float w;
			if (c <= 0.5f)
				w = stepwise + (THIRDS_FIFTHS[ic] - stepwise) * (c * 2.f);
			else
				w = THIRDS_FIFTHS[ic] + (WIDE[ic] - THIRDS_FIFTHS[ic]) * ((c - 0.5f) * 2.f);
			m[i][j] = w;
		}
	}
}

void ChainCore::refreshDefaults(const ChainParams& p) {
	for (int v = 0; v < VOICES; v++) {
		if (p.character[v] == defCharacter[v] && p.scale == defScale && seed == defSeed)
			continue;
		defCharacter[v] = p.character[v];
		characterMatrix(def[v], p.character[v], p.scale);
		// each voice's table is nudged by up to 20 % either way, the same way every time for a given seed
		Rng jitter = Rng((uint64_t)seed).fork("jitter", (uint64_t)v);
		for (int i = 0; i < 12; i++)
			for (int j = 0; j < 12; j++)
				def[v][i][j] *= 1.f + 0.2f * jitter.bipolar();
		if (!voice[v].custom)
			version++;
	}
	defScale = p.scale;
	defSeed = seed;
}

void ChainCore::applySeed() {
	seed = pendingSeed;
	for (int v = 0; v < VOICES; v++)
		voice[v].rng = Rng((uint64_t)seed).fork("voice", (uint64_t)v);
}

void ChainCore::reset(const ChainParams& p) {
	applySeed();
	for (int v = 0; v < VOICES; v++) {
		ChainVoice& vc = voice[v];
		int lo = (int)std::lround((p.center[v] - 0.5f * (float)p.range[v]) * 12.f);
		int hi = (int)std::lround((p.center[v] + 0.5f * (float)p.range[v]) * 12.f);
		int n = mod12(p.root) + 12 * (int)std::lround(p.center[v]);
		while (n > hi)
			n -= 12;
		while (n < lo)
			n += 12;
		vc.note = n;
		vc.prevPc = -1;
		vc.lastMove = 0;
		vc.sinceRoot = 0;
		vc.learnPrev = vc.learnPrev2 = -1;
		vc.histHead = 0;
		vc.histCount = 0;
	}
	refreshDefaults(p);
	version++;
}

void ChainCore::record(ChainVoice& vc) {
	vc.history[vc.histHead] = (int8_t)std::clamp(vc.note, -127, 127);
	vc.histHead = (uint8_t)((vc.histHead + 1) & 31);
	if (vc.histCount < 32)
		vc.histCount++;
}

void ChainCore::paint(int v, int from, int to, float weight) {
	if (v < 0 || v >= VOICES || from < 0 || from > 11 || to < 0 || to > 11)
		return;
	ChainVoice& vc = voice[v];
	if (!vc.custom) {
		// start from what it was playing
		for (int i = 0; i < 12; i++)
			for (int j = 0; j < 12; j++)
				vc.matrix[i][j] = std::min(def[v][i][j], 1.f);
		vc.custom = true;
	}
	vc.matrix[from][to] = weight < 0.f ? 0.f : (weight > 1.f ? 1.f : weight);
	version++;
}

void ChainCore::resetMatrix(int v) {
	if (v < 0 || v >= VOICES)
		return;
	ChainVoice& vc = voice[v];
	vc.custom = false;
	vc.learnUnit = 1.f;
	vc.learnPrev = vc.learnPrev2 = -1;
	if (vc.hasOrder2) {
		for (int i = 0; i < 144; i++)
			for (int j = 0; j < 12; j++)
				order2[v][i][j] = 0.f;
		vc.hasOrder2 = false;
		order2Version++;
	}
	version++;
}

void ChainCore::stopLearning(int v) {
	if (v >= 0 && v < VOICES)
		voice[v].learnPrev = voice[v].learnPrev2 = -1;
}

// Draw the next scale position (0..11 above the root) for voice v.
int ChainCore::pick(const ChainParams& p, int v, bool lockScale) {
	ChainVoice& vc = voice[v];
	uint16_t allowed = lockScale && p.scale != 0 ? p.scale : 0xFFF;
	int cur = mod12(vc.note - p.root);
	const float* rows[3] = {nullptr, nullptr, nullptr};
	int nRows = 0;
	if (p.order == 2 && vc.hasOrder2 && vc.prevPc >= 0)
		rows[nRows++] = order2[v][vc.prevPc * 12 + cur];
	if (vc.custom)
		rows[nRows++] = vc.matrix[cur];
	rows[nRows++] = def[v][cur];
	for (int r = 0; r < nRows; r++) {
		float sum = 0.f;
		for (int j = 0; j < 12; j++)
			if ((allowed >> j) & 1)
				sum += rows[r][j];
		if (sum <= 1e-9f)
			continue; // nothing learned from here: fall back to the next table
		float x = vc.rng.uniform() * sum;
		int last = 0;
		for (int j = 0; j < 12; j++) {
			if (!((allowed >> j) & 1) || rows[r][j] <= 0.f)
				continue;
			last = j;
			x -= rows[r][j];
			if (x < 0.f)
				break;
		}
		return last;
	}
	// no table has anything allowed: any allowed note
	int count = 0;
	for (int j = 0; j < 12; j++)
		count += (allowed >> j) & 1;
	int k = vc.rng.below(count);
	for (int j = 0; j < 12; j++)
		if ((allowed >> j) & 1) {
			if (k == 0)
				return j;
			k--;
		}
	return 0;
}

void ChainCore::step(const ChainParams& p, const bool stepNow[VOICES], int learnVoice, int learnPc, ChainOut& out) {
	out = ChainOut();
	bool any = false;
	for (int v = 0; v < VOICES; v++)
		any = any || stepNow[v];
	if (!any)
		return;
	if (pendingSeed != seed)
		applySeed(); // a new seed starts at the next step
	refreshDefaults(p);

	int before[VOICES];
	for (int v = 0; v < VOICES; v++)
		before[v] = voice[v].note;
	const int ref = std::clamp(p.refVoice, 0, VOICES - 1);
	const int leapMax = std::clamp(p.leap, 2, 24);

	for (int v = 0; v < VOICES; v++) {
		if (!stepNow[v])
			continue;
		ChainVoice& vc = voice[v];

		// ---- learning: count the move the teacher just made ----
		if (v == learnVoice && learnPc >= 0 && learnPc < 12) {
			if (!vc.custom) {
				// the first lesson starts from an empty table; later lessons add to it
				for (int i = 0; i < 12; i++)
					for (int j = 0; j < 12; j++)
						vc.matrix[i][j] = 0.f;
				vc.custom = true;
				vc.learnUnit = 1.f;
			}
			if (vc.learnPrev >= 0) {
				vc.matrix[vc.learnPrev][learnPc] += vc.learnUnit;
				float mx = 0.f;
				for (int i = 0; i < 12; i++)
					for (int j = 0; j < 12; j++)
						mx = std::max(mx, vc.matrix[i][j]);
				if (mx > 1.f) {
					// keep the table between 0 and 1 without changing its proportions
					for (int i = 0; i < 12; i++)
						for (int j = 0; j < 12; j++)
							vc.matrix[i][j] /= mx;
					vc.learnUnit /= mx;
				}
				if (vc.learnPrev2 >= 0) {
					order2[v][vc.learnPrev2 * 12 + vc.learnPrev][learnPc] += 1.f;
					vc.hasOrder2 = true;
					order2Version++;
				}
			}
			vc.learnPrev2 = vc.learnPrev;
			vc.learnPrev = learnPc;
		}

		// ---- which rules are in force for this step ----
		bool on[ChainParams::RULES];
		for (int k = 0; k < ChainParams::RULES; k++) {
			float s = p.strength[k];
			on[k] = s >= 1.f ? true : (s <= 0.f ? false : vc.rng.uniform() < s);
		}
		int range = std::clamp(p.range[v], 1, 4);
		int lo = (int)std::lround((p.center[v] - 0.5f * (float)range) * 12.f);
		int hi = (int)std::lround((p.center[v] + 0.5f * (float)range) * 12.f);
		auto place = [&](int pc) {
			// the octave nearest the last note, pulled back inside the range
			int n = vc.note + mod12(pc + p.root - vc.note);
			if (n - vc.note > 6)
				n -= 12;
			while (n > hi)
				n -= 12;
			while (n < lo)
				n += 12;
			return n;
		};
		// HOME PULL: the longer since the root, the likelier it is forced
		bool forceRoot = false;
		if (p.strength[ChainParams::HOME_PULL] > 0.f) {
			float want = std::min(1.f, (float)vc.sinceRoot / (float)std::clamp(p.homeSteps, 4, 64));
			forceRoot = vc.rng.uniform() < want * p.strength[ChainParams::HOME_PULL];
		}

		// how badly a candidate note breaks the rules in force (0 = not at all)
		auto costOf = [&](int n) {
			int move = n - vc.note;
			float cost = 0.f;
			if (on[ChainParams::SCALE_LOCK] && p.scale != 0 && !inScale(p.scale, p.root, n))
				cost += p.strength[ChainParams::SCALE_LOCK];
			if (on[ChainParams::NO_PARALLELS] && move != 0) {
				for (int u = 0; u < v; u++) {
					if (!out.stepped[u])
						continue;
					int moveU = voice[u].note - before[u];
					if (moveU == 0 || (moveU > 0) != (move > 0))
						continue;
					int was = std::abs(vc.note - before[u]) % 12, now = std::abs(n - voice[u].note) % 12;
					if (was == now && (now == 0 || now == 7)) {
						cost += p.strength[ChainParams::NO_PARALLELS];
						break;
					}
				}
			}
			if (v != ref) {
				if (on[ChainParams::KEEP_INTERVAL] && !((p.intervals >> (std::abs(n - voice[ref].note) % 12)) & 1))
					cost += p.strength[ChainParams::KEEP_INTERVAL];
				int refMove = voice[ref].lastMove;
				if (on[ChainParams::CONTRARY] && refMove != 0 && move != 0 && (move > 0) == (refMove > 0))
					cost += p.strength[ChainParams::CONTRARY];
			}
			if (on[ChainParams::MAX_LEAP] && std::abs(move) > leapMax)
				cost += p.strength[ChainParams::MAX_LEAP];
			return cost;
		};

		int best = vc.note;
		float bestCost = 1e9f;
		for (int attempt = 0; attempt < 8 && bestCost > 0.f; attempt++) {
			int n;
			if (forceRoot) {
				n = place(0);
				// later tries: the root in the other octaves that fit
				if (attempt > 0) {
					n += 12 * ((attempt + 1) / 2) * (attempt % 2 ? 1 : -1);
					if (n > hi || n < lo)
						continue;
				}
			}
			else {
				int pc = pick(p, v, on[ChainParams::SCALE_LOCK]);
				n = place(pc);
				// high CHARACTER: sometimes take the same note an octave away, for a wide leap
				float wide = (p.character[v] - 0.5f) * 0.5f;
				if (wide > 0.f && vc.rng.uniform() < wide) {
					int alt = n + (vc.rng.chance(0.5f) ? 12 : -12);
					if (alt >= lo && alt <= hi)
						n = alt;
				}
			}
			float cost = costOf(n);
			if (cost < bestCost) {
				bestCost = cost;
				best = n;
			}
		}
		if (bestCost > 0.f) {
			// Eight tries and every one broke a rule. Look at every note in range
			// and take the one that breaks least; among equals, the one the
			// table likes best, then the nearest.
			const float* row = activeMatrix(v)[mod12(vc.note - p.root)];
			float fbCost = 1e9f, fbLike = -1.f;
			int fbDist = 1000, fb = vc.note;
			for (int n = lo; n <= hi; n++) {
				float cost = costOf(n);
				float like = row[mod12(n - p.root)];
				int dist = std::abs(n - vc.note);
				bool better = cost < fbCost - 1e-6f
					|| (cost < fbCost + 1e-6f && (like > fbLike + 1e-6f || (like > fbLike - 1e-6f && dist < fbDist)));
				if (better) {
					fbCost = cost;
					fbLike = like;
					fbDist = dist;
					fb = n;
				}
			}
			if (fbCost < bestCost - 1e-6f) {
				bestCost = fbCost;
				best = fb;
			}
			if (bestCost > 0.f)
				out.violation = true;
		}

		int oldPc = mod12(vc.note - p.root);
		out.stepped[v] = true;
		out.changed[v] = best != vc.note;
		vc.lastMove = best - vc.note;
		vc.note = best;
		vc.prevPc = oldPc;
		vc.sinceRoot = mod12(best - p.root) == 0 ? 0 : vc.sinceRoot + 1;
		record(vc);
	}
	version++;
}

} // namespace codex
