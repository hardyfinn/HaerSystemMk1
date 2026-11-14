// Repeatable random numbers (spec §3.4).
// xoshiro128** seeded through splitmix64. Never use rack::random for anything
// musical: the same seed must always give the same music.
#pragma once
#include <cstdint>
#include <cmath>

namespace codex {

inline uint64_t splitmix64(uint64_t& x) {
	x += 0x9E3779B97F4A7C15ull;
	uint64_t z = x;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

// FNV-1a, used to turn a stream name such as "drift" into a number.
inline uint64_t hashTag(const char* s) {
	uint64_t h = 0xCBF29CE484222325ull;
	for (; *s; ++s) {
		h ^= (uint8_t)*s;
		h *= 0x100000001B3ull;
	}
	return h;
}

struct Rng {
	uint32_t s[4] = {1, 2, 3, 4};
	uint64_t origin = 0; // the value this generator was seeded from; fork() derives from it

	Rng() { seed(0); }
	explicit Rng(uint64_t seedValue) { seed(seedValue); }

	void seed(uint64_t seedValue) {
		origin = seedValue;
		uint64_t x = seedValue;
		uint64_t a = splitmix64(x);
		uint64_t b = splitmix64(x);
		s[0] = (uint32_t)a;
		s[1] = (uint32_t)(a >> 32);
		s[2] = (uint32_t)b;
		s[3] = (uint32_t)(b >> 32);
		if ((s[0] | s[1] | s[2] | s[3]) == 0)
			s[0] = 1; // the all-zero state would stay at zero for ever
	}

	// An independent stream for a track, voice or purpose. It depends only on
	// the original seed and the tag, not on how many numbers were drawn so far,
	// so changing one track never changes another track's sequence.
	Rng fork(uint64_t tag) const {
		uint64_t x = origin ^ (tag * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull);
		return Rng(splitmix64(x));
	}
	Rng fork(const char* tag) const { return fork(hashTag(tag)); }
	Rng fork(const char* tag, uint64_t index) const { return fork(hashTag(tag) + 0x632BE59BD9B4E019ull * (index + 1)); }

	static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

	uint32_t next() {
		uint32_t result = rotl(s[1] * 5u, 7) * 9u;
		uint32_t t = s[1] << 9;
		s[2] ^= s[0];
		s[3] ^= s[1];
		s[1] ^= s[2];
		s[0] ^= s[3];
		s[2] ^= t;
		s[3] = rotl(s[3], 11);
		return result;
	}

	// 0 <= x < 1
	float uniform() { return (float)(next() >> 8) * (1.f / 16777216.f); }
	// -1 <= x < 1
	float bipolar() { return uniform() * 2.f - 1.f; }
	float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
	// integer in 0 .. n-1
	int below(int n) { return n <= 1 ? 0 : (int)(((uint64_t)next() * (uint64_t)n) >> 32); }
	// integer in lo .. hi, both included
	int between(int lo, int hi) { return lo + below(hi - lo + 1); }
	bool chance(float p) { return uniform() < p; }
	// roughly normal, mean 0, standard deviation 1
	float gauss() {
		float u = 1.f - uniform(); // (0, 1]
		float v = uniform();
		return std::sqrt(-2.f * std::log(u)) * std::cos(6.2831853f * v);
	}
};

// The SEED knob rule: a changed seed waits for the next bar (or other natural
// boundary) before it takes effect.
struct SeedLatch {
	int active = 0;
	int pending = 0;

	// Knob value 0..999 plus optional CV, 0..10 V mapped onto 0..999.
	static int read(float knob, float cvVolts, bool cvConnected) {
		float v = cvConnected ? cvVolts * 99.9f : knob;
		int i = (int)std::lround(v);
		return i < 0 ? 0 : (i > 999 ? 999 : i);
	}
	void set(int seedValue) { pending = seedValue; }
	// Call at the boundary. True when the seed changed and generators must be re-created.
	bool commit() {
		if (pending == active)
			return false;
		active = pending;
		return true;
	}
};

} // namespace codex
