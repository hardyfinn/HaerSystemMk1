// TUNE MAP (spec §13C): harmonic fields that morph, and a quantiser that pulls
// pitches into them. Plain C++, no Rack.
#pragma once
#include <cstdint>
#include <string>

namespace codex {

// A set of allowed pitches, in cents above the root. The pattern repeats every
// `period` cents: 1200 for ordinary fields, 4800 for the partial-series fields
// (which are different in each of their 4 octaves), and a 3:1 for Bohlen-Pierce.
struct TuneField {
	static constexpr int MAX = 48;
	int n = 0;
	float period = 1200.f;
	float cents[MAX];
	float weight[MAX];

	void clear(float p = 1200.f) {
		n = 0;
		period = p;
	}
	// Adds a point (wrapped into 0..period). Ignored when the field is full.
	void add(float c, float w = 1.f);
	// Sorts the points low to high.
	void sort();
};

// The fields, numbered as on the FIELD A and FIELD B knobs (1 to 16).
enum TuneFieldId {
	FIELD_MAJOR = 1, FIELD_MINOR, FIELD_PENTATONIC, FIELD_JUST_MAJOR, FIELD_BLUE, FIELD_HARM_8_16, FIELD_HARM_16_32,
	FIELD_BOHLEN_PIERCE, FIELD_SLENDRO, FIELD_PELOG, FIELD_19TET, FIELD_CLUSTER, FIELD_QUARTER_MINOR,
	FIELD_USER_1, FIELD_USER_2, FIELD_LEARNED, FIELD_COUNT = FIELD_LEARNED
};
// Fills f with built-in field id (1..13). Returns false for USER and LEARNED, which the module owns.
bool tuneBuiltIn(int id, TuneField& f);
const char* tuneFieldName(int id);

// Field A and field B, unrolled over 4 octaves and paired point by point, so a
// morph can glide each pitch of A to its partner in B.
struct TuneMorph {
	static constexpr float SPAN = 4800.f;
	static constexpr int MAX = 192;
	struct Pair {
		float a, b;   // cents in 0..SPAN; b may lie just outside, so the glide takes the short way round
		float wa, wb; // a point with no partner has 0 weight on the other side
	};
	Pair pair[MAX];
	int n = 0;

	// Greedy nearest pairing: the closest A-B pair is joined first, then the next closest of what is left.
	void build(const TuneField& A, const TuneField& B);
	// The morphed field at m (0 = A, 1 = B): point k is pair k, in cents over 0..SPAN, with its weight.
	void at(float m, float outCents[MAX], float outWeight[MAX], int& count) const;
};

// The quantiser for one channel.
struct TuneChannel {
	int target = -1; // index into the morphed field, or -1 for none yet
	int octave = 0;  // which 4-octave span the target is in
	bool changed = false;
};

struct TuneQuantiser {
	// Points with less weight than this are not allowed as targets (spec).
	static constexpr float MIN_WEIGHT = 0.1f;
	float cents[TuneMorph::MAX];
	float weight[TuneMorph::MAX];
	int n = 0;
	// the allowed points, low to high, so the nearest one is found by bisection
	int order[TuneMorph::MAX];
	float sorted[TuneMorph::MAX];
	int allowed = 0;

	void set(const TuneMorph& morph, float m);
	// in, root: 1 V/oct. Returns the output in 1 V/oct. Sets ch.changed when the target changed.
	float process(TuneChannel& ch, float in, float root, float strength, float hystCents) const;
	// The allowed pitches in the first octave above the root, low to high, as cents (at most max of them).
	int firstOctave(float* out, int max) const;
};

// Learns a field from audio: the 8 strongest spectral peaks, averaged over the gate time.
struct TuneLearner {
	static constexpr int FFT = 4096;
	static constexpr int HOP = 2048;
	static constexpr int PEAKS = 8;

	TuneLearner();
	void start();
	// Feed one sample while the gate is high. Does an FFT every HOP samples.
	void feed(float x);
	// Turns what was heard into a field of up to 8 pitches (octave repeating, cents above C4 = 0 V).
	// Returns false when nothing clear was heard.
	bool finish(float sampleRate, TuneField& out) const;
	int frames() const { return nFrames; }

private:
	float ring[FFT];
	float window[FFT];
	float re[FFT], im[FFT];
	float avg[FFT / 2 + 1];
	int pos = 0, sinceHop = 0, filled = 0, nFrames = 0;
	void analyse();
};

// In-place radix-2 FFT (n a power of two).
void tuneFft(float* re, float* im, int n);

} // namespace codex
