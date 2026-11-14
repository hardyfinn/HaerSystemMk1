// MUTATE (spec §5): an 8-track step drum sequencer that writes its own
// patterns and then slowly rewrites them. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include "common/Rng.hpp"
#include "common/Phase.hpp"

namespace codex {

enum MutateCond { COND_ALWAYS = 0, COND_PROB, COND_EVERY_N, COND_FIRST, COND_PREV, COND_COUNT };
enum MutateStyle { STYLE_KICK = 0, STYLE_SNARE, STYLE_HAT, STYLE_PERC, STYLE_TEXTURE, STYLE_FREE, STYLE_COUNT };
enum MutateRule { RULE_ROTATE = 0, RULE_NUDGE, RULE_RATCHET, RULE_THIN_FILL, RULE_MIRROR, RULE_COPY, RULE_LOCKWALK, RULE_COUNT };

struct MutateStep {
	bool on = false;
	float vel = 0.8f;     // 0..1
	uint8_t ratchet = 1;  // 1..8 hits inside the step
	float micro = 0.f;    // -0.5..+0.5 of a step, early or late
	uint8_t cond = COND_ALWAYS;
	float p = 1.f;        // chance, for COND_PROB
	uint8_t n = 2;        // for COND_EVERY_N, 2..8
	float lockA = 0.f;    // per-step sound locks, 0..1
	float lockB = 0.f;

	bool same(const MutateStep& o) const {
		return on == o.on && vel == o.vel && ratchet == o.ratchet && micro == o.micro && cond == o.cond && p == o.p && n == o.n
			&& lockA == o.lockA && lockB == o.lockB;
	}
};

struct MutateTrack {
	static const int MAX_STEPS = 64;
	int length = 16; // 1..64
	int style = STYLE_FREE;
	bool mute = false;
	MutateStep step[MAX_STEPS];
};

struct MutatePattern {
	static const int TRACKS = 8;
	MutateTrack track[TRACKS];
};

struct MutateParams {
	int steps = 16;          // steps per bar, 4..32
	float density = 0.5f;    // 0..1, used when generating
	int every = 2;           // mutate every N bars, 1..16
	float amount = 0.f;      // 0..1: round(amount * 4) operations per pass
	float drift = 0.5f;      // 0..1: chance that an operation moves away from home rather than back
	float weight[RULE_COUNT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
	float ratchetCurve = 0.f;  // -1 speeds up, 0 even, +1 slows down
	float ratchetDecay = 0.8f; // each ratchet hit is this much quieter than the one before
	float swing = 0.f;         // 0..0.5 of a step, on odd steps
	float microCv = 0.f;       // extra shift for every hit, -0.5..+0.5 of a step
};

struct MutateOut {
	bool gate[8] = {};   // true on the sample a track fires
	float vel[8] = {};   // held from each track's last hit, 0..1
	float lockA[8] = {}; // held from each track's last hit, 0..1
	float lockB[8] = {};
	bool accent = false; // a hit with velocity above 0.8
	bool bar = false;    // a bar line was crossed
};

struct MutateCore {
	MutatePattern pat;  // what is playing
	MutatePattern home; // the saved pattern mutations drift away from and return to
	int seed = 0;
	int pendingSeed = 0;
	uint32_t version = 1; // goes up whenever pat or home changes (the screen redraws from it)
	int64_t stepNow = -1; // the global step being played

	MutateCore();
	void setSeed(int s) { pendingSeed = s; }

	// ---- building patterns ----
	void generate(const MutateParams& in);              // rebuild every track from its style; becomes home
	void generateTrack(int t, const MutateParams& in);  // rebuild one track (home is not changed)
	void setHome() { home = pat; version++; }
	void requestHome() { wantHome = true; }             // back to home at the next bar
	void requestMutate() { wantMutate = true; }         // one extra mutation pass at the next bar

	// ---- mutation (also called directly by the unit tests) ----
	void mutatePass(const MutateParams& in);
	void mutate(int rule);
	bool returnStep();                 // set one changed step back to home; false if nothing differs
	int differing(int t) const;        // steps of track t that differ from home
	float distance(int t) const;       // the same as a fraction of the track, 0..1

	// ---- hand edits (audio thread; the module queues them from the screen) ----
	void setStepOn(int t, int i, bool on);
	void setStepVel(int t, int i, float vel);
	void setLength(int t, int len);
	void setMute(int t, bool m);
	void setStyle(int t, int style);

	// ---- playing ----
	void reset();
	// entered: the global steps entered this sample (from PhaseReader::crossed(steps), or one per clock).
	// pos: the continuous position in steps. backward: the phase is running backwards.
	void process(const MutateParams& in, const Crossings& entered, double pos, bool backward, MutateOut& out);

private:
	struct Pending {
		double pos;
		float vel, lockA, lockB;
	};
	static const int PENDING = 32;
	Pending pending[8][PENDING];
	int pendingCount[8] = {};
	int64_t resolved[8];
	bool lastFired[8] = {};
	float velHeld[8] = {}, lockAHeld[8] = {}, lockBHeld[8] = {};
	bool wantHome = false, wantMutate = false;
	int lastSteps = 0;
	Rng mutRng;

	float offsetOf(const MutateParams& in, int64_t g, const MutateStep& s) const;
	void resolve(const MutateParams& in, int t, int64_t g);
	void barLine(const MutateParams& in, int64_t bar);
	int pickHit(int t, bool preferQuiet = false);
};

// E(k, len): k hits spread as evenly as possible over len steps, turned by rot.
bool euclid(int i, int k, int len, int rot);

} // namespace codex
