#include "dsp/VaultCore.hpp"
#include "common/Rng.hpp"
#include "common/MiniJson.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace codex {

namespace {

// number of events with pos < x
size_t countBelow(const std::vector<VaultEvent>& ev, double x) {
	size_t lo = 0, hi = ev.size();
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (ev[mid].pos < x)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}
// number of events with pos <= x
size_t countUpTo(const std::vector<VaultEvent>& ev, double x) {
	size_t lo = 0, hi = ev.size();
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (ev[mid].pos <= x)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

} // namespace

std::string vaultClipToJson(const VaultClip& clip) {
	std::string out;
	out.reserve(64 + clip.eventCount() * 40);
	out += "{\"name\":";
	minijson::putString(out, clip.name);
	out += ",\"bars\":";
	minijson::putNumber(out, clip.bars, 9);
	out += ",\"bpm\":";
	minijson::putNumber(out, clip.bpm, 9);
	out += ",\"beats\":";
	minijson::putNumber(out, clip.beats, 9);
	out += ",\"time\":";
	out += std::to_string((long long)clip.time);
	out += ",\"node\":";
	out += std::to_string((long long)clip.node);
	out += ",\"channels\":[";
	for (int l = 0; l < VaultClip::LANES; l++) {
		if (l)
			out += ',';
		out += std::to_string(clip.channels[l]);
	}
	// one event: [position in bars, lane, channel, type (0 gate off, 1 gate on, 2 CV), value]
	out += "],\"events\":[";
	bool first = true;
	for (int l = 0; l < VaultClip::LANES; l++)
		for (const VaultEvent& e : clip.lane[l]) {
			if (!first)
				out += ',';
			first = false;
			out += '[';
			minijson::putNumber(out, e.pos, 17);
			out += ',';
			out += std::to_string((int)e.lane);
			out += ',';
			out += std::to_string((int)e.ch);
			out += ',';
			out += std::to_string((int)e.type);
			out += ',';
			minijson::putNumber(out, e.value, 9);
			out += ']';
		}
	out += "]}";
	return out;
}

bool vaultClipFromJson(const std::string& text, VaultClip& clip) {
	minijson::Reader r(text);
	VaultClip c;
	if (!r.need('{'))
		return false;
	if (!r.eat('}')) {
		do {
			std::string key = r.string();
			if (!r.need(':'))
				return false;
			if (key == "name")
				c.name = r.string();
			else if (key == "bars")
				c.bars = (int)r.number();
			else if (key == "bpm")
				c.bpm = (float)r.number();
			else if (key == "beats")
				c.beats = (float)r.number();
			else if (key == "time")
				c.time = (int64_t)r.number();
			else if (key == "node")
				c.node = (int64_t)r.number();
			else if (key == "channels") {
				r.need('[');
				for (int l = 0; r.ok && !r.eat(']'); l++) {
					if (l)
						r.need(',');
					int n = (int)r.number();
					if (l < VaultClip::LANES)
						c.channels[l] = std::min(std::max(n, 0), 16);
				}
			}
			else if (key == "events") {
				r.need('[');
				bool first = true;
				while (r.ok && !r.eat(']')) {
					if (!first)
						r.need(',');
					first = false;
					r.need('[');
					VaultEvent e;
					e.pos = r.number();
					r.need(',');
					int lane = (int)r.number();
					r.need(',');
					int ch = (int)r.number();
					r.need(',');
					int type = (int)r.number();
					r.need(',');
					e.value = (float)r.number();
					r.need(']');
					if (!r.ok)
						return false;
					if (lane < 0 || lane >= VaultClip::LANES || ch < 0 || ch >= 16 || type < 0 || type > 2 || !std::isfinite(e.pos) || !std::isfinite(e.value))
						continue; // not something VAULT wrote: leave it out
					e.lane = (uint8_t)lane;
					e.ch = (uint8_t)ch;
					e.type = (uint8_t)type;
					c.lane[lane].push_back(e);
				}
			}
			else
				r.skip();
		} while (r.ok && r.eat(','));
		if (!r.need('}'))
			return false;
	}
	if (!r.ok)
		return false;
	c.bars = std::min(std::max(c.bars, 1), 64);
	for (int l = 0; l < VaultClip::LANES; l++) {
		std::stable_sort(c.lane[l].begin(), c.lane[l].end(), [](const VaultEvent& a, const VaultEvent& b) { return a.pos < b.pos; });
		for (const VaultEvent& e : c.lane[l])
			c.channels[l] = std::max(c.channels[l], (int)e.ch + 1);
	}
	clip = std::move(c);
	return true;
}

// ---------------------------------------------------------------- recorder

VaultRecorder::VaultRecorder() {
	ring.resize(CAPACITY);
	for (int l = 0; l < LANES; l++)
		for (int c = 0; c < CH; c++) {
			lastCv[l][c] = 0.f;
			lastCvPos[l][c] = 0.0;
			cvKnown[l][c] = false;
		}
}

void VaultRecorder::clear() {
	base.store(total.load(std::memory_order_relaxed), std::memory_order_release);
	for (int l = 0; l < LANES; l++)
		for (int c = 0; c < CH; c++)
			cvKnown[l][c] = false;
}

void VaultRecorder::push(const VaultEvent& e) {
	uint64_t n = total.load(std::memory_order_relaxed);
	ring[n % CAPACITY] = e;
	total.store(n + 1, std::memory_order_release);
}

void VaultRecorder::gate(double W, int lane, int ch, bool on, float cv) {
	VaultEvent e;
	e.pos = W;
	e.lane = (uint8_t)lane;
	e.ch = (uint8_t)ch;
	e.type = on ? VaultEvent::ON : VaultEvent::OFF;
	e.value = cv;
	push(e);
}

void VaultRecorder::cv(double W, int lane, int ch, float value) {
	float& last = lastCv[lane][ch];
	double& at = lastCvPos[lane][ch];
	if (cvKnown[lane][ch]) {
		bool newBar = std::floor(W) != std::floor(at);
		if (!newBar) {
			if (std::fabs(value - last) <= 0.001f)
				return;
			double gap = W - at;
			if (gap >= 0.0 && gap < 1.0 / 384.0)
				return;
		}
	}
	cvKnown[lane][ch] = true;
	last = value;
	at = W;
	VaultEvent e;
	e.pos = W;
	e.lane = (uint8_t)lane;
	e.ch = (uint8_t)ch;
	e.type = VaultEvent::CV;
	e.value = value;
	push(e);
}

VaultClip* VaultRecorder::keep(double W, int bars) const {
	if (bars < 1)
		bars = 1;
	double hi = std::floor(W), lo = hi - (double)bars;
	if (hi < 1.0)
		return nullptr;
	// copy what is there, then leave out anything the audio thread may have overwritten meanwhile
	uint64_t end = total.load(std::memory_order_acquire);
	uint64_t begin = std::max(base.load(std::memory_order_acquire), end > CAPACITY ? end - CAPACITY : 0);
	if (end <= begin)
		return nullptr;
	std::vector<VaultEvent> copy((size_t)(end - begin));
	for (uint64_t i = begin; i < end; i++)
		copy[(size_t)(i - begin)] = ring[i % CAPACITY];
	uint64_t end2 = total.load(std::memory_order_acquire);
	uint64_t safe = end2 + 16 > CAPACITY ? end2 + 16 - CAPACITY : 0;
	size_t first = safe > begin ? (size_t)std::min<uint64_t>(safe - begin, end - begin) : 0;

	VaultClip* clip = new VaultClip;
	clip->bars = bars;
	// gates already held when the clip starts: they start again on its first sample
	bool held[LANES][CH] = {};
	float heldCv[LANES][CH] = {};
	bool any = false;
	for (size_t i = first; i < copy.size(); i++) {
		const VaultEvent& e = copy[i];
		if (e.lane >= LANES || e.ch >= CH || e.type > VaultEvent::CV)
			continue;
		if (e.pos < lo) {
			if (e.type != VaultEvent::CV) {
				held[e.lane][e.ch] = e.type == VaultEvent::ON;
				heldCv[e.lane][e.ch] = e.value;
			}
			continue;
		}
		if (e.pos >= hi)
			continue;
		VaultEvent k = e;
		k.pos = e.pos - lo;
		clip->lane[e.lane].push_back(k);
		any = true;
	}
	for (int l = 0; l < LANES; l++) {
		std::vector<VaultEvent>& ev = clip->lane[l];
		std::stable_sort(ev.begin(), ev.end(), [](const VaultEvent& a, const VaultEvent& b) { return a.pos < b.pos; });
		std::vector<VaultEvent> starts;
		for (int c = 0; c < CH; c++)
			if (held[l][c]) {
				VaultEvent k;
				k.pos = 0.0;
				k.lane = (uint8_t)l;
				k.ch = (uint8_t)c;
				k.type = VaultEvent::ON;
				k.value = heldCv[l][c];
				starts.push_back(k);
				any = true;
			}
		ev.insert(ev.begin(), starts.begin(), starts.end());
		for (const VaultEvent& e : ev)
			clip->channels[l] = std::max(clip->channels[l], (int)e.ch + 1);
	}
	if (!any) {
		delete clip;
		return nullptr;
	}
	return clip;
}

// ---------------------------------------------------------------- player

void VaultPlayer::launch(const VaultClip* clip, int slot, double atBar) {
	pending = clip;
	pendingSlot = slot;
	pendingBar = atBar;
	hasPending = true;
}

void VaultPlayer::allOff() {
	for (int c = 0; c < CH; c++)
		gate[c] = false;
}

void VaultPlayer::stop() {
	cur = nullptr;
	pending = nullptr;
	hasPending = false;
	allOff();
	posFrac = 0.f;
}

void VaultPlayer::transportReset() {
	launchBar = 0.0;
	if (hasPending)
		pendingBar = 0.0;
	if (cur)
		fresh = true;
}

// THIN: every note has a fixed number between 0 and 1 (from the seed, the lane and the note's place in
// the clip) and is left out when THIN is above it. So turning THIN up only ever removes more notes.
bool VaultPlayer::thinned(size_t index, float thin) const {
	if (thin <= 0.f)
		return false;
	uint64_t x = ((uint64_t)(uint32_t)seedNow << 40) ^ ((uint64_t)lane << 32) ^ (uint64_t)index;
	uint64_t h = splitmix64(x);
	float u = (float)(h >> 40) * (1.f / 16777216.f);
	return u < thin;
}

// The CV each channel should be holding at the play position.
void VaultPlayer::settle(const std::vector<VaultEvent>& ev) {
	for (size_t i = 0; i < next && i < ev.size(); i++)
		if (ev[i].type != VaultEvent::OFF && ev[i].ch < CH)
			cv[ev[i].ch] = ev[i].value;
}

void VaultPlayer::fire(const VaultEvent& e, size_t index, bool backwards, const VaultLaneSettings& s) {
	if (e.ch >= CH)
		return;
	if (e.type == VaultEvent::CV) {
		cv[e.ch] = e.value;
		return;
	}
	// played backwards, the end of a note is where it starts
	bool on = (e.type == VaultEvent::ON) != backwards;
	if (!on) {
		gate[e.ch] = false;
		return;
	}
	if (thinned(index, s.thin))
		return;
	if (gate[e.ch])
		dip[e.ch] = true;
	gate[e.ch] = true;
	cv[e.ch] = e.value;
}

void VaultPlayer::process(double W, const VaultLaneSettings& s) {
	end = false;
	for (int c = 0; c < CH; c++)
		dip[c] = false;
	if (hasPending && W >= pendingBar) {
		hasPending = false;
		cur = pending;
		curSlot = pendingSlot;
		pending = nullptr;
		launchBar = pendingBar;
		allOff();
		for (int c = 0; c < CH; c++)
			cv[c] = 0.f;
		fresh = true;
		seedNow = s.seed;
	}
	if (!cur) {
		channels = 1;
		posFrac = 0.f;
		return;
	}
	const std::vector<VaultEvent>& ev = cur->lane[lane];
	channels = std::min(std::max(cur->channels[lane], 1), CH);
	transposeNow = s.pitch ? s.transpose / 12.f : 0.f;

	// the part of the clip that loops
	double len = (double)cur->bars;
	double a = std::min(std::max((double)s.winStart, 0.0), 1.0) * len;
	double span = std::min(std::max((double)s.winLen, 0.0), 1.0) * len;
	span = std::max(span, 1.0 / 384.0);
	if (a + span > len)
		a = std::max(0.0, len - span);
	double b = std::min(a + span, len);
	span = b - a;
	bool moved = a != ws || b != we || s.reverse != wasReverse;
	ws = a;
	we = b;
	wasReverse = s.reverse;

	double t = W - launchBar;
	double u = t - span * std::floor(t / span);
	double e = s.reverse ? we - u : ws + u;
	posFrac = (float)(e / len);

	if (fresh) {
		// start of the clip: events standing exactly on the start are still to come
		fresh = false;
		next = s.reverse ? countBelow(ev, we) : countBelow(ev, ws);
		settle(ev);
		ePrev = s.reverse ? we : ws;
	}
	else if (moved) {
		allOff();
		next = countUpTo(ev, e);
		settle(ev);
		ePrev = e;
		return;
	}

	double d = e - ePrev;
	size_t n = ev.size();
	if (d < -0.5 * span) {
		// forwards past the end: finish the loop, then start again on the next sample
		while (next < n && ev[next].pos < we) {
			fire(ev[next], next, false, s);
			next++;
		}
		allOff();
		end = true;
		seedNow = s.seed;
		next = countBelow(ev, ws);
	}
	else if (d > 0.5 * span) {
		// backwards past the start
		while (next > 0 && ev[next - 1].pos >= ws) {
			fire(ev[next - 1], next - 1, true, s);
			next--;
		}
		allOff();
		end = true;
		seedNow = s.seed;
		next = countBelow(ev, we);
	}
	else if (d >= 0.0) {
		while (next < n && ev[next].pos <= e && ev[next].pos < we) {
			fire(ev[next], next, false, s);
			next++;
		}
	}
	else {
		while (next > 0 && ev[next - 1].pos > e) {
			fire(ev[next - 1], next - 1, true, s);
			next--;
		}
	}
	ePrev = e;
}

} // namespace codex
