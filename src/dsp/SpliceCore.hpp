// SPLICE (spec §10): records the input into a 16 second loop of tape, and on
// demand repeats the most recent slice (stutter), with repeats that can
// shrink, change pitch, reverse and fade, or dissolve into grains.
// Plain C++, no Rack.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "common/Rng.hpp"

namespace codex {

struct SpliceParams {
	enum Rec { REC_ALWAYS = 0, REC_GATED = 1, REC_OFF = 2 };
	int rec = REC_ALWAYS;
	bool recGate = false;       // REC_GATED: record while true
	bool freeze = false;        // REC_ALWAYS: stop recording while true
	float sliceSeconds = 0.25f; // length of one slice
	bool stutter = false;       // gate: repeat the last slice while true
	float holdSlices = 0.f;     // after a rising edge, keep stuttering at least this many slice lengths
	float accel = 1.f;          // 0.5..2: each repeat is this many times as long as the one before
	float pitchRamp = 0.f;      // semitones added per repeat, -2..2
	float reverse = 0.f;        // chance that a repeat plays backwards
	float decay = 1.f;          // 0.5..1: each repeat's level compared to the one before
	float smear = 0.f;          // 0 stutter only .. 1 grains only
	float grain = 0.05f;        // grain length, seconds (0.005..0.2)
	float density = 20.f;       // grains per second (2..100)
	float jitter = 0.2f;        // how far grains stray, in slices (0..1)
	float width = 0.5f;         // stereo scatter of grains (0..1)
	bool scrubOn = false;       // true: play position comes from `scrub`
	float scrub = 0.f;          // 0..1 through the slice
};

struct SpliceOut {
	float l = 0.f, r = 0.f;
	bool sliceTrig = false; // true on the sample a repeat starts
	int repeat = 0;         // which repeat is playing (0 = the first)
};

struct SpliceCore {
	static const int GRAINS = 32;
	static const int BINS = 256;
	static constexpr float SECONDS = 16.f;
	static constexpr float FADE_SECONDS = 0.002f;

	struct Grain {
		bool on = false;
		double pos = 0.0;
		int age = 0, len = 1;
		float gl = 0.f, gr = 0.f;
		float wc = 1.f, ws = 0.f, dc = 1.f, ds = 0.f; // the window's turning pointer and its step
	};

	// Call when the sample rate is known or changes (not from the audio thread): allocates and clears the tape.
	void setSampleRate(float sr);
	void clear();
	void setSeed(int s) { pendingSeed = s; }
	void process(const SpliceParams& p, float inL, float inR, SpliceOut& out);

	// --- for the screen and for saving ---
	size_t size() const { return frames; }
	size_t writeHead() const { return writePos; }
	bool stuttering() const { return active; }
	double sliceStart() const { return start; }  // frames, may be below 0 or above size(): wrap it
	double sliceLength() const { return sliceLen; }
	double playHead() const { return cur.pos; }
	const Grain* grainList() const { return grains; }
	float overview[BINS] = {}; // loudest sample in each 1/256 of the tape
	std::vector<float> tapeL, tapeR;
	void setWriteHead(size_t pos) { writePos = frames ? pos % frames : 0; }
	void rebuildOverview();
	float rate() const { return sampleRate; }

private:
	struct Player {
		bool live = true;   // true: the input passes straight through
		double pos = 0.0;   // frames along the tape (not wrapped)
		double inc = 1.0;   // frames per sample; negative = backwards
		float level = 1.f;
		int remaining = 0;  // samples until the next repeat starts
	};
	float sampleRate = 48000.f;
	size_t frames = 0;
	size_t writePos = 0;
	int fadeLen = 96;
	Player cur, prev;
	int fadeLeft = 0;
	bool active = false;
	bool gateWas = false;
	double start = 0.0, sliceLen = 12000.0;
	double lenRaw = 0.0;
	float levelNow = 1.f;
	int k = 0;
	double holdLeft = 0.0;
	int seed = 0, pendingSeed = 0;
	Rng rng, grainRng;
	Grain grains[GRAINS];
	float grainTimer = 0.f;
	float scrubOffset = 0.f;
	int binNow = -1;

	void read(double pos, float& l, float& r) const;
	void startRepeat(const SpliceParams& p, SpliceOut& out);
	void play(Player& pl, float inL, float inR, float& l, float& r);
};

} // namespace codex
