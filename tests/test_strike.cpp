#include "doctest.h"
#include "util.hpp"
#include "dsp/StrikeCore.hpp"
#include "common/Rng.hpp"
#include <vector>
#include <cmath>
#include <initializer_list>

using namespace codex;
using namespace testutil;

static const float SR = 48000.f;

// One hit at sample 0, then n samples of ringing.
static std::vector<float> hit(StrikeVoice& v, const StrikeGlobal& g, StrikeVoiceIn in, int n) {
	v.setSampleRate(SR);
	std::vector<float> out((size_t)n);
	for (int i = 0; i < n; i++) {
		in.trig = i == 0;
		out[(size_t)i] = v.process(g, in);
	}
	return out;
}

TEST_CASE("STRIKE: a single mode rings at the asked-for pitch") {
	for (float f : {110.f, 440.f, 1234.5f, 3000.f}) {
		StrikeVoice v;
		StrikeGlobal g;
		g.exciter = 0.f;
		g.solo = 0;
		StrikeVoiceIn in;
		in.freq = f;
		in.decay = 2.f;
		auto x = hit(v, g, in, 48000);
		// the strongest of nearby frequencies must be within 0.5 % of f
		float best = 0.f, bestF = 0.f;
		for (float d = -0.02f; d <= 0.0201f; d += 0.001f) {
			float s = goertzel(x, f * (1.f + d), SR);
			if (s > best) {
				best = s;
				bestF = f * (1.f + d);
			}
		}
		CHECK(std::fabs(bestF / f - 1.f) <= 0.005f);
		// and it is a clean sine: one upward zero crossing per cycle
		CHECK(std::abs(zeroCrossings(x) - (int)std::lround(f)) <= 2);
	}
}

TEST_CASE("STRIKE: a single mode's ring time matches DECAY") {
	for (float decay : {0.05f, 0.5f, 2.f}) {
		StrikeVoice v;
		StrikeGlobal g;
		g.exciter = 0.f;
		g.solo = 0;
		StrikeVoiceIn in;
		in.freq = 500.f;
		in.decay = decay;
		in.material = 2.f; // metal: the reference material
		auto x = hit(v, g, in, (int)(SR * decay * 1.5f) + 4800);
		CHECK(t60(x, SR) == doctest::Approx(decay).epsilon(0.1));
	}
}

TEST_CASE("STRIKE: higher modes sit at the material's ratios") {
	StrikeVoice v;
	StrikeGlobal g;
	g.exciter = 0.f;
	g.solo = 1;
	StrikeVoiceIn in;
	in.freq = 200.f;
	in.material = 2.f;
	in.decay = 3.f;
	in.damp = 0.f;
	auto x = hit(v, g, in, 24000);
	CHECK(goertzel(x, 200.f * 2.756f, SR) > 10.f * goertzel(x, 200.f * 2.f, SR));
	// INHARM stretches the ratio: r' = r^(1 + 0.1)
	StrikeVoice v2;
	g.inharm = 1.f;
	auto y = hit(v2, g, in, 24000);
	float f2 = 200.f * std::pow(2.756f, 1.1f);
	CHECK(goertzel(y, f2, SR) > 10.f * goertzel(y, 200.f * 2.756f, SR));
}

TEST_CASE("STRIKE: materials differ in length and brightness") {
	float len[5];
	for (int m = 0; m < 5; m++) {
		StrikeVoice v;
		StrikeGlobal g;
		StrikeVoiceIn in;
		in.material = (float)m;
		in.freq = 220.f;
		in.decay = 1.f;
		auto x = hit(v, g, in, 96000);
		CHECK(maxAbs(x) > 0.5f);
		CHECK(maxAbs(x) <= 10.f);
		len[m] = t60(x, SR);
	}
	CHECK(len[1] < len[0]); // rubber is shorter than wood
	CHECK(len[0] < len[4]); // wood is shorter than a drum skin
	CHECK(len[4] < len[2]); // which is shorter than metal
}

TEST_CASE("STRIKE: every exciter, at full velocity, stays inside 10 V and makes sound") {
	Rng rng(5);
	for (float ex : {0.f, 0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f}) {
		for (float hard : {0.f, 0.5f, 1.f}) {
			for (float pitch : {20.f, 220.f, 5000.f}) {
				StrikeVoice v;
				v.setSampleRate(SR);
				StrikeGlobal g;
				g.exciter = ex;
				g.hardness = hard;
				g.burst = 4;
				g.burstRate = 100.f;
				StrikeVoiceIn in;
				in.freq = pitch;
				in.vel = 1.f;
				in.bright = 1.f;
				float peak = 0.f;
				bool finite = true;
				for (int i = 0; i < 24000; i++) {
					in.trig = i == 0;
					in.ext = rng.bipolar() * 2.f; // 10 V of noise at the EXCITE input
					float o = v.process(g, in);
					finite = finite && std::isfinite(o);
					peak = std::fmax(peak, std::fabs(o));
				}
				CHECK(finite);
				CHECK(peak <= 10.f);
				if (pitch < 5000.f && hard >= 0.5f)
					CHECK(peak > 0.05f);
			}
		}
	}
}

TEST_CASE("STRIKE: 10 seconds of random settings at audio rate never gives NaN or infinity") {
	Rng rng(99);
	StrikeVoice v;
	v.setSampleRate(SR);
	StrikeGlobal g;
	StrikeVoiceIn in;
	bool finite = true;
	float peak = 0.f;
	for (int i = 0; i < 480000; i++) {
		in.freq = 20.f * std::pow(250.f, rng.uniform());
		in.material = rng.range(-1.f, 5.f);
		in.decay = 0.005f * std::pow(2000.f, rng.uniform());
		in.damp = rng.range(-0.5f, 1.5f);
		in.position = rng.range(-0.5f, 1.5f);
		in.bright = rng.range(-0.5f, 1.5f);
		in.vel = rng.uniform();
		in.ext = rng.bipolar() * 3.f;
		in.trig = rng.chance(0.01f);
		in.gate = rng.chance(0.5f);
		in.choke = rng.chance(0.001f);
		if ((i & 1023) == 0) {
			g.exciter = rng.range(-1.f, 4.f);
			g.hardness = rng.range(-0.5f, 1.5f);
			g.inharm = rng.range(-1.5f, 1.5f);
			g.burst = rng.between(0, 20);
			g.burstRate = rng.range(0.f, 300.f);
			g.formant = rng.chance(0.3f);
			g.chokeOnRetrig = rng.chance(0.5f);
			g.extGated = rng.chance(0.5f);
		}
		float o = v.process(g, in);
		finite = finite && std::isfinite(o);
		peak = std::fmax(peak, std::fabs(o));
	}
	CHECK(finite);
	CHECK(peak <= 10.f);
}

TEST_CASE("STRIKE: BURST re-fires the exciter") {
	StrikeGlobal g;
	g.exciter = 0.f;
	g.solo = 0;
	g.burst = 6;
	g.burstRate = 40.f;
	StrikeVoiceIn in;
	in.freq = 1000.f;
	in.decay = 0.01f; // each firing dies before the next
	StrikeVoice v;
	auto x = hit(v, g, in, 12000);
	// count separate bursts of sound
	int bursts = 0;
	bool loud = false;
	float env = 0.f;
	for (float s : x) {
		env = std::fmax(std::fabs(s), env * 0.995f);
		if (!loud && env > 0.5f) {
			loud = true;
			bursts++;
		}
		else if (loud && env < 0.05f)
			loud = false;
	}
	CHECK(bursts == 6);
}

TEST_CASE("STRIKE: choke silences a ringing voice within 5 ms") {
	StrikeVoice v;
	v.setSampleRate(SR);
	StrikeGlobal g;
	StrikeVoiceIn in;
	in.decay = 5.f;
	float before = 0.f, after = 0.f;
	for (int i = 0; i < 9600; i++) {
		in.trig = i == 0;
		in.choke = i == 4800;
		float o = std::fabs(v.process(g, in));
		if (i > 4000 && i < 4800)
			before = std::fmax(before, o);
		if (i > 4800 + 250)
			after = std::fmax(after, o);
	}
	CHECK(before > 0.5f);
	CHECK(after < before * 0.002f);
	CHECK_FALSE(v.isSounding());
}

TEST_CASE("STRIKE: choke on retrigger stops hits piling up") {
	float peak[2];
	for (int choke = 0; choke < 2; choke++) {
		StrikeVoice v;
		v.setSampleRate(SR);
		StrikeGlobal g;
		g.chokeOnRetrig = choke == 1;
		g.exciter = 0.f;
		g.solo = 0;
		StrikeVoiceIn in;
		in.freq = 480.f; // 100 samples per cycle, so hits every 1000 samples add up exactly
		in.decay = 5.f;
		peak[choke] = 0.f;
		for (int i = 0; i < 20000; i++) {
			in.trig = i % 1000 == 0;
			peak[choke] = std::fmax(peak[choke], std::fabs(v.process(g, in)));
		}
	}
	CHECK(peak[0] > 5.f);
	CHECK(peak[1] < 2.5f);
}

TEST_CASE("STRIKE: formant mode sings at the pitch while the gate is held, then fades") {
	StrikeVoice v;
	v.setSampleRate(SR);
	StrikeGlobal g;
	g.formant = true;
	StrikeVoiceIn in;
	in.freq = 110.f;
	in.material = 0.f; // "a"
	in.decay = 0.1f;
	std::vector<float> held, released;
	for (int i = 0; i < 48000; i++) {
		in.trig = i == 0;
		in.gate = i < 24000;
		float o = v.process(g, in);
		if (i >= 4800 && i < 24000)
			held.push_back(o);
		if (i >= 40000)
			released.push_back(o);
	}
	CHECK(maxAbs(held) > 1.f);
	CHECK(maxAbs(held) <= 10.f);
	CHECK(maxAbs(released) < 0.01f);
	// energy sits on the harmonics of 110 Hz, and for "a" the 600 Hz region beats the 2 kHz region
	CHECK(goertzel(held, 660.f, SR) > 5.f * goertzel(held, 715.f, SR));
	CHECK(goertzel(held, 660.f, SR) > goertzel(held, 1980.f, SR));
	// "i" moves the first formant down to 250 Hz
	StrikeVoice v2;
	v2.setSampleRate(SR);
	in.material = 2.f;
	std::vector<float> ee;
	for (int i = 0; i < 24000; i++) {
		in.trig = i == 0;
		in.gate = true;
		float o = v2.process(g, in);
		if (i >= 4800)
			ee.push_back(o);
	}
	CHECK(goertzel(ee, 220.f, SR) > goertzel(ee, 660.f, SR));
	MESSAGE("formant peak: " << maxAbs(held) << " V (a), " << maxAbs(ee) << " V (i)");
}

TEST_CASE("STRIKE: typical hit levels") {
	for (int m = 0; m < 5; m++) {
		for (float ex : {0.f, 1.f, 2.f}) {
			StrikeVoice v;
			StrikeGlobal g;
			g.exciter = ex;
			StrikeVoiceIn in;
			in.material = (float)m;
			auto x = hit(v, g, in, 24000);
			MESSAGE("material " << m << " exciter " << ex << " peak " << maxAbs(x) << " V");
			CHECK(maxAbs(x) > 0.3f);
		}
	}
}
