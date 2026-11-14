// CHAIN (spec §7): up to four melody voices. Each picks its next note from a
// table of "after this note, how likely is that one", then a set of harmony
// rules may veto the pick. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include "common/Rng.hpp"

namespace codex {

struct ChainParams {
	enum Rule { SCALE_LOCK = 0, NO_PARALLELS, KEEP_INTERVAL, CONTRARY, MAX_LEAP, HOME_PULL, RULES };
	static const int VOICES = 4;

	uint16_t scale = 0xAB5;  // bit i = the note i semitones above the root is allowed (default: major)
	int root = 0;            // 0 = C .. 11 = B
	float character[VOICES] = {0.f, 0.25f, 0.5f, 0.75f}; // 0 stepwise .. 0.5 thirds and fifths .. 1 wide leaps
	int range[VOICES] = {2, 2, 2, 2};                   // octaves, 1..4
	float center[VOICES] = {0.f, 0.f, 0.f, 0.f};         // octaves from C4
	float strength[RULES] = {1.f, 0.f, 0.f, 0.f, 0.f, 0.f};
	int leap = 7;            // MAX LEAP, semitones, 2..24
	int homeSteps = 16;      // HOME PULL, 4..64
	int order = 1;           // 1 or 2
	int refVoice = 0;        // the voice KEEP INTERVAL and CONTRARY listen to
	uint16_t intervals = (1 << 3) | (1 << 4) | (1 << 8) | (1 << 9); // allowed distances to the reference voice
};

struct ChainVoice {
	int note = 0;          // semitones from C4
	int prevPc = -1;       // the note before, as a scale position 0..11 above the root (-1 = none yet)
	int lastMove = 0;      // semitones moved at the last step
	int sinceRoot = 0;     // steps since the root was last played
	bool custom = false;   // true: `matrix` was learned or painted; false: it follows CHARACTER
	bool hasOrder2 = false;
	float matrix[12][12] = {}; // [from][to], counted from the root
	Rng rng;
	// learning
	int learnPrev = -1, learnPrev2 = -1;
	float learnUnit = 1.f; // what one counted move adds to the table
	// the last 32 notes, for the screen
	int8_t history[32] = {};
	uint8_t histHead = 0, histCount = 0;
};

struct ChainOut {
	bool stepped[ChainParams::VOICES] = {};
	bool changed[ChainParams::VOICES] = {};
	bool violation = false;
};

struct ChainCore {
	static const int VOICES = ChainParams::VOICES;
	static const uint16_t SCALES[10];
	static const char* const SCALE_NAMES[10];

	ChainVoice voice[VOICES];
	// second-order memory: [voice][previous * 12 + current][next], learned only
	float order2[VOICES][144][12] = {};
	uint32_t version = 0;       // goes up whenever anything the screen shows has changed
	uint32_t order2Version = 0; // goes up whenever order2 has changed

	ChainCore() { reset(ChainParams()); }
	// Back to the start: every voice on the root, random streams rewound. Matrices are kept.
	void reset(const ChainParams& p);
	void setSeed(int s) { pendingSeed = s; }
	int seedInUse() const { return seed; }

	// Advance the voices whose stepNow flag is set. learnVoice >= 0 with
	// learnPc >= 0 (0..11 above the root) teaches that voice one more note.
	void step(const ChainParams& p, const bool stepNow[VOICES], int learnVoice, int learnPc, ChainOut& out);
	void stopLearning(int v);

	// The table voice v is using right now (its own, or the CHARACTER one).
	const float (*activeMatrix(int v) const)[12] { return voice[v].custom ? voice[v].matrix : def[v]; }
	// Make sure the CHARACTER tables match the knobs (also done inside step()).
	void refreshDefaults(const ChainParams& p);
	void paint(int v, int from, int to, float weight);
	void resetMatrix(int v);

	// The table CHARACTER gives, before the seeded ±20 % jitter.
	static void characterMatrix(float m[12][12], float character, uint16_t scale);
	static bool inScale(uint16_t scale, int root, int note);

private:
	int seed = 0, pendingSeed = 0;
	float def[VOICES][12][12] = {};
	float defCharacter[VOICES] = {-1.f, -1.f, -1.f, -1.f};
	uint16_t defScale = 0;
	int defSeed = -1;
	void applySeed();
	int pick(const ChainParams& p, int v, bool lockScale);
	void record(ChainVoice& vc);
};

} // namespace codex
