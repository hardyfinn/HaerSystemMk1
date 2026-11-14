// PhaseReader (spec §3.1): turns a 0..10 V ramp into a continuous position W,
// counted in cycles (bars). Handles forward and backward motion, wraps and
// audio-rate ramps. Every phase-driven sequencer is built on this.
#pragma once
#include <cmath>
#include <cstdint>

namespace codex {

struct Crossings {
	static const int MAX = 8; // at most 8 events per sample
	int count = 0;
	int dir = 0;             // +1 forward, -1 backward, 0 none
	int64_t index[MAX] = {}; // the step entered by each event, in playing order
};

struct PhaseReader {
	double W = 0.0;     // unwrapped position now, in cycles
	double Wprev = 0.0; // unwrapped position one sample ago
	float pPrev = 0.f;
	bool primed = false;  // false until the first sample after a reset
	bool started = false; // true for exactly the first sample after a reset

	// Back to bar 0. The next sample adopts wherever the ramp is inside its
	// cycle, so step positions stay lined up with the ramp.
	void reset() {
		W = Wprev = 0.0;
		pPrev = 0.f;
		primed = false;
		started = false;
	}

	void process(float volts) {
		float p = volts * 0.1f;
		if (!(p >= 0.f)) // also catches NaN
			p = 0.f;
		if (p > 1.f)
			p = 1.f;
		if (!primed) {
			primed = true;
			started = true;
			W = Wprev = (p >= 1.f) ? 0.0 : (double)p;
			pPrev = p;
			return;
		}
		started = false;
		double d = (double)p - (double)pPrev;
		// A jump of more than half a cycle is read as a wrap. This is a known
		// limit: a real jump that large can't be told apart from a wrap.
		if (d < -0.5)
			d += 1.0;
		else if (d > 0.5)
			d -= 1.0;
		Wprev = W;
		W += d;
		pPrev = p;
	}

	int64_t bar() const { return (int64_t)std::floor(W); }
	double frac() const { return W - std::floor(W); }
	double delta() const { return W - Wprev; }

	// Step boundaries crossed this sample when a cycle is cut into k steps.
	// Forward motion lists the steps entered in rising order. Backward motion
	// lists them in falling order, or reports nothing when allowReverse is off.
	// On the first sample after a reset the step the ramp is standing in is
	// reported once, so a sequence starts on its first step.
	Crossings crossed(double k, bool allowReverse = false) const {
		Crossings c;
		if (!primed || !(k > 0.0))
			return c;
		int64_t now = (int64_t)std::floor(W * k);
		if (started) {
			c.count = 1;
			c.dir = 1;
			c.index[0] = now;
			return c;
		}
		int64_t before = (int64_t)std::floor(Wprev * k);
		if (now > before) {
			c.dir = 1;
			int64_t n = now - before;
			int64_t first = before + 1;
			if (n > Crossings::MAX) { // keep the most recent ones
				first = now - Crossings::MAX + 1;
				n = Crossings::MAX;
			}
			c.count = (int)n;
			for (int i = 0; i < c.count; i++)
				c.index[i] = first + i;
		}
		else if (now < before && allowReverse) {
			c.dir = -1;
			int64_t n = before - now;
			int64_t first = before - 1;
			if (n > Crossings::MAX) {
				first = now + Crossings::MAX - 1;
				n = Crossings::MAX;
			}
			c.count = (int)n;
			for (int i = 0; i < c.count; i++)
				c.index[i] = first - i;
		}
		return c;
	}

	// True when forward motion passed position x (in cycles) this sample.
	// Used for events that sit between step boundaries, such as micro-timing.
	bool passed(double x) const {
		if (started)
			return x == W;
		return Wprev < x && x <= W;
	}
	// The same for backward motion.
	bool passedBackward(double x) const { return !started && W <= x && x < Wprev; }
};

} // namespace codex
