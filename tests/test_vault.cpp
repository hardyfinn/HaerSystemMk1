#include "doctest.h"
#include "dsp/VaultCore.hpp"
#include <cmath>
#include <memory>
#include <vector>

using namespace codex;

namespace {

// A test pattern: lane 0 has a gate on channel 0 at these bar positions, each 0.05 bars long,
// with a CV that says which note it is.
const double HITS[] = {0.0, 0.25, 0.5, 0.8125, 1.0, 1.375, 1.5, 2.25, 2.5, 2.75, 3.0, 3.3, 3.9};
const int NHITS = (int)(sizeof(HITS) / sizeof(HITS[0]));
const double STEP = 1.0 / 96000.0; // bars per sample: 2 s bars at 48 kHz

bool gateAt(double W) {
	double bar4 = W - 4.0 * std::floor(W / 4.0);
	for (int i = 0; i < NHITS; i++)
		if (bar4 >= HITS[i] && bar4 < HITS[i] + 0.05)
			return true;
	return false;
}
float noteAt(double W) {
	double bar4 = W - 4.0 * std::floor(W / 4.0);
	int n = 0;
	for (int i = 0; i < NHITS; i++)
		if (bar4 >= HITS[i])
			n = i;
	return (float)n * 0.25f;
}

// Runs the recorder over `bars` bars of the test pattern, starting at bar 0. Returns the final W.
double record(VaultRecorder& rec, double bars) {
	bool was = false;
	double W = 0.0;
	long n = (long)std::llround(bars / STEP);
	for (long i = 0; i <= n; i++) {
		W = (double)i * STEP;
		bool g = gateAt(W);
		float v = noteAt(W);
		rec.cv(W, 0, 0, v);
		if (g != was)
			rec.gate(W, 0, 0, g, v);
		was = g;
	}
	return W;
}

struct Edge {
	double W;
	bool on;
	float cv;
};

// Plays lane 0 of a clip from W = from for `bars` bars, moving `step` bars a sample.
std::vector<Edge> play(const VaultClip* clip, const VaultLaneSettings& s, double from, double bars, double step, int* ends = nullptr) {
	VaultPlayer pl;
	pl.lane = 0;
	pl.launch(clip, 0, from);
	std::vector<Edge> out;
	bool was = false;
	long n = (long)std::llround(bars / step);
	for (long i = 0; i < n; i++) {
		double W = from + (double)i * step;
		pl.process(W, s);
		bool g = pl.gateVolts(0) > 1.f;
		if (g != was)
			out.push_back({W - from, g, pl.cvVolts(0)});
		was = g;
		if (ends && pl.end)
			(*ends)++;
	}
	return out;
}

} // namespace

TEST_CASE("VAULT: a recorded gate pattern, kept and played back on the same phase, lands within 1 sample") {
	VaultRecorder rec;
	double W = record(rec, 4.2);
	std::unique_ptr<VaultClip> clip(rec.keep(W, 4));
	REQUIRE(clip);
	CHECK(clip->bars == 4);
	CHECK(clip->channels[0] == 1);
	VaultLaneSettings s;
	std::vector<Edge> edges = play(clip.get(), s, 8.0, 4.0, STEP);
	REQUIRE(edges.size() == (size_t)NHITS * 2);
	for (int i = 0; i < NHITS; i++) {
		CHECK(edges[2 * i].on);
		CHECK(std::fabs(edges[2 * i].W - HITS[i]) <= 1.01 * STEP);
		CHECK(!edges[2 * i + 1].on);
		// (the test pattern's own note ends can fall a sample late from rounding, so allow recording + playback)
		CHECK(std::fabs(edges[2 * i + 1].W - (HITS[i] + 0.05)) <= 2.01 * STEP);
		CHECK(edges[2 * i].cv == doctest::Approx((float)i * 0.25f));
	}
}

TEST_CASE("VAULT: at half speed the events land at the same bar positions, twice as far apart in time") {
	VaultRecorder rec;
	double W = record(rec, 4.2);
	std::unique_ptr<VaultClip> clip(rec.keep(W, 4));
	REQUIRE(clip);
	VaultLaneSettings s;
	std::vector<Edge> edges = play(clip.get(), s, 3.0, 4.0, STEP * 0.5);
	REQUIRE(edges.size() == (size_t)NHITS * 2);
	for (int i = 0; i < NHITS; i++)
		CHECK(std::fabs(edges[2 * i].W - HITS[i]) <= 1.01 * STEP);
	// samples between the first two hits: twice as many as when recorded
	double samples = (edges[2].W - edges[0].W) / (STEP * 0.5);
	CHECK(samples == doctest::Approx(2.0 * 0.25 / STEP).epsilon(0.001));
}

TEST_CASE("VAULT: the clip loops, with an END trigger at each wrap and a gap before the first note") {
	VaultRecorder rec;
	double W = record(rec, 4.2);
	std::unique_ptr<VaultClip> clip(rec.keep(W, 4));
	REQUIRE(clip);
	VaultLaneSettings s;
	int ends = 0;
	std::vector<Edge> edges = play(clip.get(), s, 0.0, 12.0, STEP, &ends);
	CHECK(ends == 2);
	CHECK(edges.size() == (size_t)NHITS * 2 * 3);
}

TEST_CASE("VAULT: KEEP takes the whole bars before the current one, measured from the first bar line") {
	VaultRecorder rec;
	double W = record(rec, 6.5); // bars 4 and 5 are the pattern's bars 0 and 1 again
	std::unique_ptr<VaultClip> clip(rec.keep(W, 2));
	REQUIRE(clip);
	std::vector<double> ons;
	for (const VaultEvent& e : clip->lane[0])
		if (e.type == VaultEvent::ON)
			ons.push_back(e.pos);
	REQUIRE(ons.size() == 7);
	for (int i = 0; i < 7; i++)
		CHECK(ons[i] == doctest::Approx(HITS[i]).epsilon(1e-9));
	// nothing recorded yet, or less than one bar: nothing to keep
	VaultRecorder empty;
	CHECK(empty.keep(10.0, 4) == nullptr);
	VaultRecorder brief;
	double w2 = record(brief, 0.6);
	CHECK(brief.keep(w2, 1) == nullptr);
}

TEST_CASE("VAULT: a gate held across the start of the clip starts again on the clip's first sample") {
	VaultRecorder rec;
	rec.cv(0.1, 2, 3, 1.5f);
	rec.gate(0.9, 2, 3, true, 1.5f); // held from bar 0 into bar 1
	rec.gate(1.2, 2, 3, false, 1.5f);
	rec.cv(1.0, 2, 3, 1.5f);
	std::unique_ptr<VaultClip> clip(rec.keep(2.1, 1));
	REQUIRE(clip);
	CHECK(clip->channels[2] == 4);
	REQUIRE(clip->lane[2].size() == 3);
	CHECK(clip->lane[2][0].type == VaultEvent::ON);
	CHECK(clip->lane[2][0].pos == 0.0);
	CHECK(clip->lane[2][0].value == 1.5f);
}

TEST_CASE("VAULT: CV moves are stored at most every 1/384 bar, and once at every bar line") {
	VaultRecorder rec;
	// a ramp that changes every sample for 3 bars
	long n = (long)std::llround(3.2 / STEP);
	for (long i = 0; i < n; i++)
		rec.cv((double)i * STEP, 1, 0, (float)i * 0.0001f);
	uint64_t ramp = rec.written();
	CHECK(ramp <= 3.2 * 384 + 8);
	CHECK(ramp >= 3.2 * 384 * 0.9);
	// a value that never changes: one event per bar
	VaultRecorder still;
	for (long i = 0; i < n; i++)
		still.cv((double)i * STEP, 1, 0, 2.f);
	CHECK(still.written() == 4);
	// changes smaller than 1 mV are not stored
	VaultRecorder tiny;
	for (long i = 0; i < 20000; i++)
		tiny.cv((double)i * STEP, 1, 0, 2.f + 0.0005f * (float)(i & 1));
	CHECK(tiny.written() == 1);
}

TEST_CASE("VAULT: when the recording overflows, the oldest events go first and nothing breaks") {
	VaultRecorder rec;
	// 400,000 short gates, 1/1000 bar apart: far more than the 262,144 events that fit
	const int N = 400000;
	for (int i = 0; i < N; i++) {
		double W = (double)i * 0.001;
		rec.gate(W, 0, 0, true, (float)i);
		rec.gate(W + 0.0005, 0, 0, false, (float)i);
	}
	CHECK(rec.written() == (uint64_t)N * 2);
	double W = (double)N * 0.001; // bar 400
	// the newest bars are all there
	std::unique_ptr<VaultClip> recent(rec.keep(W, 64));
	REQUIRE(recent);
	CHECK(recent->lane[0].size() == 64 * 2000);
	CHECK(recent->lane[0].front().value == (float)(N - 64000));
	// bars from before the overflow are gone: 131 bars fit, so of the last 200 only those are found
	std::unique_ptr<VaultClip> old(rec.keep(W, 200));
	REQUIRE(old);
	size_t found = old->lane[0].size();
	CHECK(found <= VaultRecorder::CAPACITY);
	CHECK(found >= VaultRecorder::CAPACITY - 64);
	CHECK(old->lane[0].back().value == (float)(N - 1));
	// after a clear, nothing older is kept
	rec.clear();
	CHECK(rec.keep(W, 8) == nullptr);
}

TEST_CASE("VAULT: clips survive a JSON round trip unchanged") {
	VaultRecorder rec;
	double W = record(rec, 4.2);
	rec.gate(3.123456789012345, 7, 15, true, -3.3333333f);
	std::unique_ptr<VaultClip> clip(rec.keep(W, 4));
	REQUIRE(clip);
	clip->name = "kick \"A\" \\ caf\xc3\xa9\n2";
	clip->bpm = 133.7f;
	clip->beats = 3.5f;
	clip->time = 1794650000;
	clip->node = 42;
	std::string text = vaultClipToJson(*clip);
	VaultClip back;
	REQUIRE(vaultClipFromJson(text, back));
	CHECK(back.name == clip->name);
	CHECK(back.bars == clip->bars);
	CHECK(back.bpm == clip->bpm);
	CHECK(back.beats == clip->beats);
	CHECK(back.time == clip->time);
	CHECK(back.node == clip->node);
	for (int l = 0; l < VaultClip::LANES; l++) {
		CHECK(back.channels[l] == clip->channels[l]);
		REQUIRE(back.lane[l].size() == clip->lane[l].size());
		for (size_t i = 0; i < clip->lane[l].size(); i++) {
			const VaultEvent& a = clip->lane[l][i];
			const VaultEvent& b = back.lane[l][i];
			bool same = a.pos == b.pos && a.value == b.value && a.lane == b.lane && a.ch == b.ch && a.type == b.type;
			CHECK(same);
		}
	}
	CHECK(vaultClipToJson(back) == text);
	// the same data spread out with spaces and extra keys, as another JSON writer might give it
	VaultClip loose;
	REQUIRE(vaultClipFromJson("{ \"extra\" : {\"a\":[1,2,{\"b\":null}],\"c\":true},\n \"bars\" : 2, \"name\" : \"x\\u00e9\", \"events\" : [ [ 0.5 , 1 , 2 , 1 , 1e-1 ] , [0.25,1,2,2,3.0] ] }", loose));
	CHECK(loose.bars == 2);
	CHECK(loose.name == "x\xc3\xa9");
	REQUIRE(loose.lane[1].size() == 2);
	CHECK(loose.lane[1][0].pos == 0.25);
	CHECK(loose.lane[1][1].value == 0.1f);
	CHECK(loose.channels[1] == 3);
	// broken text is refused and leaves the clip alone
	CHECK(!vaultClipFromJson("{\"bars\":2,\"events\":[[0.5,1,2", loose));
	CHECK(!vaultClipFromJson("", loose));
	CHECK(loose.bars == 2);
}

TEST_CASE("VAULT: THIN leaves notes out, more as it rises, and the same ones every time") {
	VaultRecorder rec;
	for (int i = 0; i < 400; i++) {
		rec.gate(i * 0.01, 0, 0, true, 0.f);
		rec.gate(i * 0.01 + 0.004, 0, 0, false, 0.f);
	}
	std::unique_ptr<VaultClip> clip(rec.keep(4.5, 4));
	REQUIRE(clip);
	auto count = [&](float thin, int seed) {
		VaultLaneSettings s;
		s.thin = thin;
		s.seed = seed;
		std::vector<Edge> e = play(clip.get(), s, 0.0, 4.0, 1.0 / 20000.0);
		std::vector<double> at;
		for (const Edge& x : e)
			if (x.on)
				at.push_back(x.W);
		return at;
	};
	std::vector<double> all = count(0.f, 1), some = count(0.3f, 1), few = count(0.7f, 1), none = count(1.f, 1);
	CHECK(all.size() == 400);
	CHECK(some.size() == doctest::Approx(280).epsilon(0.12));
	CHECK(few.size() == doctest::Approx(120).epsilon(0.2));
	CHECK(none.empty());
	CHECK(count(0.3f, 1) == some);
	CHECK(count(0.3f, 2) != some);
	// every note that survives THIN 0.7 also survives THIN 0.3
	size_t j = 0;
	for (double w : few) {
		while (j < some.size() && some[j] < w)
			j++;
		REQUIRE(j < some.size());
		CHECK(some[j] == w);
	}
}

TEST_CASE("VAULT: REVERSE plays the clip backwards, WINDOW loops part of it, TRANSPOSE moves pitch lanes only") {
	VaultRecorder rec;
	double W = record(rec, 4.2);
	std::unique_ptr<VaultClip> clip(rec.keep(W, 4));
	REQUIRE(clip);
	VaultLaneSettings s;
	s.reverse = true;
	std::vector<Edge> back = play(clip.get(), s, 0.0, 4.001, STEP); // just past the wrap, where the first note (now the last) ends
	REQUIRE(back.size() == (size_t)NHITS * 2);
	// the last note comes first, and starts where it used to end
	CHECK(back[0].on);
	CHECK(back[0].W == doctest::Approx(4.0 - (HITS[NHITS - 1] + 0.05)).epsilon(1e-4));
	CHECK(back[1].W == doctest::Approx(4.0 - HITS[NHITS - 1]).epsilon(1e-4));
	CHECK(back.back().W == doctest::Approx(4.0 - HITS[0]).epsilon(1e-4));

	// second bar only, looped: its three hits, four times in four bars
	VaultLaneSettings w;
	w.winStart = 0.25f;
	w.winLen = 0.25f;
	int ends = 0;
	std::vector<Edge> loop = play(clip.get(), w, 0.0, 4.0, STEP, &ends);
	CHECK(ends == 3);
	REQUIRE(loop.size() == 3 * 2 * 4);
	CHECK(loop[0].W == doctest::Approx(0.0).epsilon(1e-4));
	CHECK(loop[2].W == doctest::Approx(0.375).epsilon(1e-4));
	CHECK(loop[6].W == doctest::Approx(1.0).epsilon(1e-4));
	CHECK(loop[2].cv == doctest::Approx(5 * 0.25f));

	VaultLaneSettings t;
	t.transpose = 12.f;
	std::vector<Edge> plain = play(clip.get(), t, 0.0, 4.0, STEP);
	CHECK(plain[2].cv == doctest::Approx(0.25f));
	t.pitch = true;
	std::vector<Edge> up = play(clip.get(), t, 0.0, 4.0, STEP);
	CHECK(up[2].cv == doctest::Approx(1.25f));
}

TEST_CASE("VAULT: a launch waits for its bar, a stop is immediate, and a new note on a held gate dips for a sample") {
	VaultRecorder rec;
	double W = record(rec, 4.2);
	std::unique_ptr<VaultClip> clip(rec.keep(W, 4));
	REQUIRE(clip);
	VaultPlayer pl;
	VaultLaneSettings s;
	pl.launch(clip.get(), 5, 2.0);
	CHECK(pl.waitingSlot() == 5);
	pl.process(1.99999, s);
	CHECK(pl.clip() == nullptr);
	CHECK(pl.gateVolts(0) == 0.f);
	pl.process(2.0, s);
	CHECK(pl.clip() == clip.get());
	CHECK(pl.slot() == 5);
	CHECK(pl.gateVolts(0) == 10.f);
	// launching nothing stops at the bar line
	pl.launch(nullptr, -1, 3.0);
	pl.process(2.01, s);
	CHECK(pl.clip() == clip.get());
	pl.process(3.0, s);
	CHECK(pl.clip() == nullptr);
	CHECK(pl.gateVolts(0) == 0.f);
	pl.launch(clip.get(), 5, 3.0);
	pl.process(3.01, s);
	CHECK(pl.gateVolts(0) == 10.f);
	pl.stop();
	CHECK(pl.clip() == nullptr);
	CHECK(pl.gateVolts(0) == 0.f);

	// two note-ons with no note-off between them
	VaultClip two;
	two.bars = 1;
	two.channels[0] = 1;
	VaultEvent a;
	a.type = VaultEvent::ON;
	a.pos = 0.1;
	VaultEvent b = a;
	b.pos = 0.2;
	two.lane[0] = {a, b};
	VaultPlayer p2;
	p2.launch(&two, 0, 0.0);
	p2.process(0.15, s);
	CHECK(p2.gateVolts(0) == 10.f);
	p2.process(0.25, s);
	CHECK(p2.gateVolts(0) == 0.f);
	p2.process(0.26, s);
	CHECK(p2.gateVolts(0) == 10.f);
}
