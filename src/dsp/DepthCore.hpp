// DEPTH (spec §11): four sources placed at different distances in one room.
// Each source gets its own pre-delay, air loss, direct level and early
// reflections from its DIST setting; all four feed one shared reverb tail
// (a 16-line feedback delay network). Plain C++, no Rack.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace codex {

struct DepthParams {
	static const int CH = 4;
	float dist[CH] = {0.1f, 0.4f, 0.7f, 0.95f}; // 0 close .. 1 far
	float pan[CH] = {0.f, 0.f, 0.f, 0.f};       // -1 left .. 1 right
	bool connected[CH] = {true, true, true, true};
	float size = 1.f;        // room size, 0.25..2
	float decay = 2.f;       // seconds for the tail to fall 60 dB, 0.3..30
	float damp = 0.3f;       // 0 bright tail .. 1 dark tail
	float mod = 0.2f;        // 0..1 = 0..2 ms of slow movement in the tail
	float instability = 0.f; // 0..1
	float mix = 0.5f;        // 0 dry .. 1 wet
	bool freeze = false;
};

struct DepthOut {
	float outL = 0.f, outR = 0.f; // direct sound + early reflections + tail, balanced by MIX
	float wetL = 0.f, wetR = 0.f; // early reflections + tail only
};

struct DepthCore {
	static const int CH = DepthParams::CH;
	static const int LINES = 16;
	static const int TAPS = 8;

	DepthCore();
	// Call when the sample rate is known or changes (not from the audio thread): allocates and clears.
	void setSampleRate(float sr);
	void clear();
	void process(const DepthParams& p, const float inL[CH], const float inR[CH], DepthOut& out);

	// Keeps a signal inside ±11.9 V: untouched below 9 V, rounded off above.
	static float softClip(float v);
	// The 16 delay times (ms at SIZE 1) and the 8 early reflection times.
	static const float LINE_MS[LINES];
	static const float TAP_MS[TAPS];

private:
	struct Delay {
		std::vector<float> buf;
		size_t mask = 0, pos = 0;
		void resize(size_t minLen);
		void clear();
		void write(float v) {
			buf[pos] = v;
			pos = (pos + 1) & mask;
		}
		// d samples ago (d >= 1); the sample written last is d = 1
		float at(size_t d) const { return buf[(pos - d) & mask]; }
		float linear(float d) const;
		float cubic(float d) const;
	};
	struct Allpass {
		Delay line;
		size_t len = 1;
		float process(float x, float g);
	};
	struct Channel {
		Delay left, right, mono;
		float lpL = 0.f, lpR = 0.f;
		float dist = 0.f;      // smoothed
		float airCoef = 1.f, direct = 1.f, send = 0.2f, panL = 1.f, panR = 1.f;
		float erL[TAPS] = {}, erR[TAPS] = {};
		int drain = 0;          // samples left to play out after the input is unplugged
	};

	float sampleRate = 48000.f;
	int control = 0;
	Channel ch[CH];
	Allpass diffL[4], diffR[4];
	Delay line[LINES];
	float lp[LINES] = {};
	float delayNow[LINES] = {};   // samples, glides to delayWant
	float delayWant[LINES] = {};
	float gain[LINES] = {};
	float modPhase[LINES] = {}, modRate[LINES] = {};
	float modNow[LINES] = {}, modInc[LINES] = {};
	float modDepth = 0.f;         // smoothed, samples
	float rotPhase[LINES / 2] = {}, rotRate[LINES / 2] = {};
	float rotCos[LINES / 2], rotSin[LINES / 2];
	bool rotating = false;
	float size = 1.f;             // smoothed
	float dampCoef = 1.f;
	float dryGain = 0.7f, wetGain = 0.7f;
	float tapSamples[TAPS] = {};

	void updateControl(const DepthParams& p);
};

} // namespace codex
