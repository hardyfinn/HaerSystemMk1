// SPLICE: cut-up and smear buffer (spec §10). The tape and the stutter and
// grain engines are in dsp/SpliceCore; this file is the Rack module around it.
#include "plugin.hpp"
#include "modules/SpliceLayout.hpp"
#include "dsp/SpliceCore.hpp"
#include "common/Phase.hpp"
#include "common/Rng.hpp"
#include "common/Theme.hpp"
#include "common/Trigger.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>

using namespace codex;
namespace L = layout_splice;

namespace {

const int DIVS[10] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

// ---- a minimal 16-bit WAV file, for "save buffer with patch". ±10 V is full scale. ----
void put32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}
void put16(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint32_t get16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

// Writes the tape oldest sample first, so the file is one plain 16 second recording.
bool writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, size_t head, float sampleRate) {
	size_t n = std::min(l.size(), r.size());
	if (!n)
		return false;
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	uint8_t h[44];
	uint32_t bytes = (uint32_t)(n * 4);
	std::memcpy(h, "RIFF", 4);
	put32(h + 4, 36 + bytes);
	std::memcpy(h + 8, "WAVEfmt ", 8);
	put32(h + 16, 16);
	put16(h + 20, 1); // PCM
	put16(h + 22, 2); // stereo
	put32(h + 24, (uint32_t)sampleRate);
	put32(h + 28, (uint32_t)sampleRate * 4);
	put16(h + 32, 4);
	put16(h + 34, 16);
	std::memcpy(h + 36, "data", 4);
	put32(h + 40, bytes);
	bool ok = std::fwrite(h, 1, 44, f) == 44;
	std::vector<uint8_t> block(4096 * 4);
	size_t done = 0;
	while (ok && done < n) {
		size_t m = std::min<size_t>(4096, n - done);
		for (size_t i = 0; i < m; i++) {
			size_t at = (head + done + i) % n;
			int a = (int)std::lround(clamp(l[at] * 0.1f, -1.f, 1.f) * 32767.f);
			int b = (int)std::lround(clamp(r[at] * 0.1f, -1.f, 1.f) * 32767.f);
			put16(&block[i * 4], (uint32_t)(a & 0xffff));
			put16(&block[i * 4 + 2], (uint32_t)(b & 0xffff));
		}
		ok = std::fwrite(block.data(), 4, m, f) == m;
		done += m;
	}
	ok = std::fclose(f) == 0 && ok;
	return ok;
}

// Reads a 16-bit mono or stereo WAV. Returns the file's sample rate, or 0 if it can't be read.
float readWav(const std::string& path, std::vector<float>& l, std::vector<float>& r) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return 0.f;
	uint8_t h[12];
	float rate = 0.f;
	uint32_t channels = 0, bits = 0;
	bool ok = std::fread(h, 1, 12, f) == 12 && !std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "WAVE", 4);
	while (ok) {
		uint8_t c[8];
		if (std::fread(c, 1, 8, f) != 8)
			break;
		uint32_t size = get32(c + 4);
		if (!std::memcmp(c, "fmt ", 4) && size >= 16) {
			uint8_t fm[16];
			if (std::fread(fm, 1, 16, f) != 16)
				break;
			if (get16(fm) != 1)
				break; // not plain PCM
			channels = get16(fm + 2);
			rate = (float)get32(fm + 4);
			bits = get16(fm + 14);
			if (size > 16)
				std::fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR);
		}
		else if (!std::memcmp(c, "data", 4)) {
			if (bits != 16 || (channels != 1 && channels != 2) || rate < 1000.f)
				break;
			size_t frames = std::min<size_t>(size / (2 * channels), (size_t)(SpliceCore::SECONDS * 192000.f));
			std::vector<uint8_t> raw(frames * 2 * channels);
			frames = std::fread(raw.data(), 2 * channels, frames, f);
			l.resize(frames);
			r.resize(frames);
			for (size_t i = 0; i < frames; i++) {
				const uint8_t* p = &raw[i * 2 * channels];
				l[i] = (float)(int16_t)get16(p) * (10.f / 32767.f);
				r[i] = channels == 2 ? (float)(int16_t)get16(p + 2) * (10.f / 32767.f) : l[i];
			}
			std::fclose(f);
			return frames ? rate : 0.f;
		}
		else
			std::fseek(f, (long)(size + (size & 1)), SEEK_CUR);
	}
	std::fclose(f);
	return 0.f;
}

// Keeps the output inside ±12 V: untouched below 10 V, rounded off above.
inline float limit(float v) {
	float a = std::fabs(v);
	if (a <= 10.f)
		return v;
	float c = 10.f + 1.9f * std::tanh((a - 10.f) / 1.9f);
	return v < 0.f ? -c : c;
}

} // namespace

struct Splice : Module {
	enum ParamId {
		REC_PARAM, DIV_PARAM, TIME_PARAM, HOLD_PARAM, ACCEL_PARAM, PITCH_PARAM, REVERSE_PARAM, DECAY_PARAM,
		SMEAR_PARAM, GRAIN_PARAM, DENSITY_PARAM, JITTER_PARAM, WIDTH_PARAM, SEED_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		L_INPUT, R_INPUT, PHASE_INPUT, STUTTER_INPUT, FREEZE_INPUT, SCRUB_INPUT, ACCEL_INPUT, PITCH_INPUT,
		SMEAR_INPUT, DIV_INPUT, SEED_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, SLICE_OUTPUT, REPEAT_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	SpliceCore core;
	SpliceParams p;
	PhaseReader phase;
	SchmittIn stutterIn, freezeIn;
	AdaptiveTrigger sliceOut;
	int divider = 0;
	double avgDelta = 0.0; // bars per sample, smoothed
	bool phaseWas = false;
	bool saveBuffer = false;
	// a recording waiting to be put on the tape (loaded from the patch, or carried over a sample rate change)
	std::vector<float> pendL, pendR;
	float pendRate = 0.f;

	// for the screen
	std::atomic<float> dispOverview[SpliceCore::BINS];
	std::atomic<float> dispGrain[SpliceCore::GRAINS];
	std::atomic<float> dispWrite{0.f}, dispStart{0.f}, dispLen{0.f}, dispPlay{0.f}, dispSeconds{0.25f};
	std::atomic<int> dispRepeat{-1}, dispDiv{0};
	std::atomic<bool> dispRecording{true};
	int overviewNext = 0;

	Splice() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(REC_PARAM, 0.f, 2.f, 2.f, "Record", {"Off (the tape keeps what it has)", "Gated (records only while FREEZE is high)", "Always (FREEZE high stops it)"});
		configParam(DIV_PARAM, 0.f, 9.f, 5.f, "Slice length as a division of the bar (used when PHASE is patched; 0..9 = 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 slices per bar)");
		paramQuantities[DIV_PARAM]->snapEnabled = true;
		configParam(TIME_PARAM, std::log2(0.01f), 1.f, -2.f, "Slice length (used when PHASE is not patched)", " s", 2.f, 1.f);
		configParam(HOLD_PARAM, 0.f, 16.f, 4.f, "Hold: a trigger at STUTTER stutters for this many slice lengths (0 = only while STUTTER is high)", " slices");
		paramQuantities[HOLD_PARAM]->snapEnabled = true;
		configParam(ACCEL_PARAM, -1.f, 1.f, 0.f, "Accel: each repeat is this many times as long as the one before (below 1 speeds up into a buzz, above 1 slows down)", "x", 2.f, 1.f);
		configParam(PITCH_PARAM, -2.f, 2.f, 0.f, "Pitch ramp: each repeat is this much higher or lower than the one before (tape style: higher is also shorter)", " semitones");
		configParam(REVERSE_PARAM, 0.f, 1.f, 0.f, "Reverse: chance that a repeat plays backwards", " %", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.5f, 1.f, 1.f, "Decay: each repeat's level compared to the one before", " %", 0.f, 100.f);
		configParam(SMEAR_PARAM, 0.f, 1.f, 0.f, "Smear: fades from the clean chops to a cloud of grains", " %", 0.f, 100.f);
		configParam(GRAIN_PARAM, std::log2(5.f), std::log2(200.f), std::log2(50.f), "Grain length", " ms", 2.f, 1.f);
		configParam(DENSITY_PARAM, 1.f, std::log2(100.f), std::log2(20.f), "Density", " grains per second", 2.f, 1.f);
		configParam(JITTER_PARAM, 0.f, 1.f, 0.2f, "Jitter: how far grains stray from the play position (100 % = one slice)", " %", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.5f, "Width: how far left and right the grains are scattered", " %", 0.f, 100.f);
		configParam(SEED_PARAM, 0.f, 999.f, 0.f, "Seed (drives which repeats reverse and where grains land; a new seed starts at the next stutter)");
		paramQuantities[SEED_PARAM]->snapEnabled = true;

		configInput(L_INPUT, "Left (or mono)");
		configInput(R_INPUT, "Right");
		configInput(PHASE_INPUT, "Phase (0 to 10 V ramp, one bar; sets the slice length together with DIV)");
		configInput(STUTTER_INPUT, "Stutter (gate or trigger: repeats the slice that has just gone by)");
		configInput(FREEZE_INPUT, "Freeze gate (stops recording; with REC on GATED it is the record gate instead)");
		configInput(SCRUB_INPUT, "Scrub (0 to 10 V = start to end of the slice; while patched, it sets the play position)");
		configInput(ACCEL_INPUT, "Accel CV (5 V = from 1x to 2x)");
		configInput(PITCH_INPUT, "Pitch ramp CV (5 V = 2 semitones per repeat)");
		configInput(SMEAR_INPUT, "Smear CV (10 V = full range)");
		configInput(DIV_INPUT, "Division CV (1 V = one step along the list)");
		configInput(SEED_INPUT, "Seed CV (0 to 10 V = 0 to 999)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(SLICE_OUTPUT, "Slice (a trigger at the start of every repeat)");
		configOutput(REPEAT_OUTPUT, "Repeat number (0.5 V per repeat: 0 V for the first, 0.5 V for the second, and so on)");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);

		for (auto& o : dispOverview)
			o.store(0.f);
		for (auto& g : dispGrain)
			g.store(-1.f);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		saveBuffer = false;
		core.clear();
		phase.reset();
		avgDelta = 0.0;
		stutterIn.reset();
		freezeIn.reset();
		sliceOut.reset();
	}

	// ---- the tape and the sample rate ----

	// Take the tape off as one recording, oldest sample first.
	void liftTape() {
		size_t n = core.size();
		if (!n)
			return;
		pendL.resize(n);
		pendR.resize(n);
		size_t head = core.writeHead();
		for (size_t i = 0; i < n; i++) {
			pendL[i] = core.tapeL[(head + i) % n];
			pendR[i] = core.tapeR[(head + i) % n];
		}
		pendRate = core.rate();
	}

	// Put the waiting recording on the tape so that it ends at the record head.
	void layTape() {
		size_t n = core.size();
		if (!n || pendL.empty() || pendRate <= 0.f)
			return;
		double step = (double)pendRate / (double)core.rate(); // file frames per tape frame
		size_t have = (size_t)((double)pendL.size() / step);
		size_t count = std::min(have, n);
		double first = (double)pendL.size() - (double)count * step; // keep the newest part
		for (size_t i = 0; i < count; i++) {
			double at = first + (double)i * step;
			size_t a = std::min((size_t)at, pendL.size() - 1);
			size_t b = std::min(a + 1, pendL.size() - 1);
			float f = (float)(at - (double)a);
			core.tapeL[i] = pendL[a] + (pendL[b] - pendL[a]) * f;
			core.tapeR[i] = pendR[a] + (pendR[b] - pendR[a]) * f;
		}
		core.setWriteHead(count);
		core.rebuildOverview();
		for (int b = 0; b < SpliceCore::BINS; b++)
			dispOverview[b].store(core.overview[b], std::memory_order_relaxed);
		pendL = std::vector<float>();
		pendR = std::vector<float>();
		pendRate = 0.f;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if (core.size() && core.rate() == e.sampleRate) {
			layTape();
			return;
		}
		// a new sample rate: carry the recording over to the new tape
		if (core.size() && pendL.empty())
			liftTape();
		core.setSampleRate(e.sampleRate);
		for (auto& o : dispOverview)
			o.store(0.f, std::memory_order_relaxed);
		layTape();
	}

	void onAdd(const AddEvent& e) override {
		if (!saveBuffer)
			return;
		std::string dir = getPatchStorageDirectory();
		if (dir.empty())
			return;
		pendRate = readWav(system::join(dir, "buffer.wav"), pendL, pendR);
		// it goes onto the tape in onSampleRateChange, which Rack calls right after this
	}

	void onSave(const SaveEvent& e) override {
		if (saveBuffer) {
			std::string dir = createPatchStorageDirectory();
			if (!dir.empty())
				writeWav(system::join(dir, "buffer.wav"), core.tapeL, core.tapeR, core.writeHead(), core.rate());
		}
		else {
			std::string dir = getPatchStorageDirectory();
			if (!dir.empty())
				system::remove(system::join(dir, "buffer.wav"));
		}
	}

	// ---- audio ----

	void readParams(const ProcessArgs& args) {
		float rec = params[REC_PARAM].getValue();
		p.rec = rec > 1.5f ? SpliceParams::REC_ALWAYS : (rec > 0.5f ? SpliceParams::REC_GATED : SpliceParams::REC_OFF);
		int div = clamp((int)std::lround(params[DIV_PARAM].getValue() + inputs[DIV_INPUT].getVoltage()), 0, 9);
		bool barKnown = inputs[PHASE_INPUT].isConnected() && avgDelta > 1e-9;
		if (barKnown)
			p.sliceSeconds = (float)(1.0 / (avgDelta * (double)args.sampleRate)) / (float)DIVS[div];
		else
			p.sliceSeconds = std::pow(2.f, params[TIME_PARAM].getValue());
		p.sliceSeconds = clamp(p.sliceSeconds, 0.002f, 8.f);
		p.holdSlices = params[HOLD_PARAM].getValue();
		p.accel = std::pow(2.f, clamp(params[ACCEL_PARAM].getValue() + inputs[ACCEL_INPUT].getVoltage() * 0.2f, -1.f, 1.f));
		p.pitchRamp = clamp(params[PITCH_PARAM].getValue() + inputs[PITCH_INPUT].getVoltage() * 0.4f, -2.f, 2.f);
		p.reverse = params[REVERSE_PARAM].getValue();
		p.decay = params[DECAY_PARAM].getValue();
		p.smear = clamp(params[SMEAR_PARAM].getValue() + inputs[SMEAR_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		p.grain = std::pow(2.f, params[GRAIN_PARAM].getValue()) * 0.001f;
		p.density = std::pow(2.f, params[DENSITY_PARAM].getValue());
		p.jitter = params[JITTER_PARAM].getValue();
		p.width = params[WIDTH_PARAM].getValue();
		p.scrubOn = inputs[SCRUB_INPUT].isConnected();
		core.setSeed(SeedLatch::read(params[SEED_PARAM].getValue(), inputs[SEED_INPUT].getVoltage(), inputs[SEED_INPUT].isConnected()));
		dispDiv.store(barKnown ? DIVS[div] : 0, std::memory_order_relaxed);
		dispSeconds.store(p.sliceSeconds, std::memory_order_relaxed);
	}

	void updateScreen() {
		size_t n = core.size();
		if (!n)
			return;
		float inv = 1.f / (float)n;
		dispWrite.store((float)core.writeHead() * inv, std::memory_order_relaxed);
		bool on = core.stuttering();
		double start = on ? core.sliceStart() : (double)core.writeHead() - core.sliceLength();
		dispStart.store((float)(start * (double)inv), std::memory_order_relaxed);
		dispLen.store((float)(core.sliceLength() * (double)inv), std::memory_order_relaxed);
		dispPlay.store((float)(core.playHead() * (double)inv), std::memory_order_relaxed);
		const SpliceCore::Grain* g = core.grainList();
		for (int i = 0; i < SpliceCore::GRAINS; i++)
			dispGrain[i].store(g[i].on ? (float)(g[i].pos * (double)inv) : -1.f, std::memory_order_relaxed);
		// the waveform is copied a few columns at a time, and always the column being recorded
		for (int i = 0; i < 4; i++) {
			dispOverview[overviewNext].store(core.overview[overviewNext], std::memory_order_relaxed);
			overviewNext = (overviewNext + 1) % SpliceCore::BINS;
		}
		int b = (int)(core.writeHead() * SpliceCore::BINS / n);
		dispOverview[b].store(core.overview[b], std::memory_order_relaxed);
	}

	void process(const ProcessArgs& args) override {
		// how long a bar is, from how fast the PHASE ramp moves
		bool phaseOn = inputs[PHASE_INPUT].isConnected();
		if (phaseOn) {
			if (!phaseWas) {
				phase.reset();
				avgDelta = 0.0;
			}
			phase.process(inputs[PHASE_INPUT].getVoltage());
			double d = std::fabs(phase.delta());
			if (avgDelta <= 0.0)
				avgDelta = d;
			else
				avgDelta += (d - avgDelta) * (double)(1.f / (0.02f * args.sampleRate));
		}
		phaseWas = phaseOn;

		bool rise = stutterIn.process(0, inputs[STUTTER_INPUT].getVoltage());
		freezeIn.process(0, inputs[FREEZE_INPUT].getVoltage());
		bool tick = divider == 0;
		divider = (divider + 1) & 31;
		// the knobs are read about a thousand times a second, and afresh when a stutter starts
		if (tick || rise)
			readParams(args);
		p.stutter = stutterIn.isHigh(0);
		p.freeze = freezeIn.isHigh(0);
		p.recGate = freezeIn.isHigh(0);
		p.scrub = inputs[SCRUB_INPUT].getVoltage() * 0.1f;

		float inL = inputs[L_INPUT].getVoltageSum();
		float inR = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() : inL;
		SpliceOut out;
		core.process(p, inL, inR, out);
		outputs[L_OUTPUT].setVoltage(limit(out.l));
		outputs[R_OUTPUT].setVoltage(limit(out.r));
		if (out.sliceTrig)
			sliceOut.fire(0);
		outputs[SLICE_OUTPUT].setVoltage(sliceOut.process(0, args.sampleTime));
		outputs[REPEAT_OUTPUT].setVoltage(std::min((float)out.repeat * 0.5f, 10.f));

		if (tick) {
			dispRepeat.store(core.stuttering() ? out.repeat : -1, std::memory_order_relaxed);
			dispRecording.store(p.rec == SpliceParams::REC_ALWAYS ? !p.freeze : (p.rec == SpliceParams::REC_GATED && p.recGate), std::memory_order_relaxed);
			updateScreen();
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "saveBuffer", json_boolean(saveBuffer));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "saveBuffer");
		if (s)
			saveBuffer = json_is_true(s);
	}
};

// The tape from oldest (left edge of the 16 seconds) to newest, with the
// record head, the slice being repeated and the grains.
struct SpliceDisplay : Widget {
	Splice* module = nullptr;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, theme::screen());
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			nvgScissor(vg, 0, 0, box.size.x, box.size.y);
			NVGcolor teal = theme::sound();
			const float w = box.size.x, h = box.size.y;
			const float top = 15.f, bottom = h - 4.f;
			const float mid = (top + bottom) * 0.5f, half = (bottom - top) * 0.5f;
			const int BINS = SpliceCore::BINS;

			float write = module ? module->dispWrite.load() : 0.62f;
			float start = module ? module->dispStart.load() : 0.56f;
			float len = module ? module->dispLen.load() : 0.06f;
			int repeat = module ? module->dispRepeat.load() : -1;
			bool recording = module ? module->dispRecording.load() : true;

			// the slice: bright while it is being repeated, faint while it only follows the record head
			auto band = [&](float a, float b, NVGcolor c) {
				nvgBeginPath(vg);
				nvgRect(vg, a * w, top, std::max((b - a) * w, 1.f), bottom - top);
				nvgFillColor(vg, c);
				nvgFill(vg);
			};
			start -= std::floor(start);
			len = clamp(len, 0.f, 1.f);
			NVGcolor bandColor = nvgTransRGBAf(teal, repeat >= 0 ? 0.35f : 0.12f);
			if (start + len <= 1.f)
				band(start, start + len, bandColor);
			else {
				band(start, 1.f, bandColor);
				band(0.f, start + len - 1.f, bandColor);
			}

			// centre line and waveform (10 V = full height)
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, mid);
			nvgLineTo(vg, w, mid);
			nvgStrokeColor(vg, theme::grid());
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			nvgBeginPath(vg);
			for (int b = 0; b < BINS; b++) {
				float a;
				if (module)
					a = module->dispOverview[b].load(std::memory_order_relaxed);
				else {
					// in the module browser: a made-up drum loop
					float t = (float)(b % 32) / 32.f;
					a = b < 160 ? 7.f * std::exp(-6.f * t) + 0.6f : 0.f;
				}
				float y = clamp(a * 0.1f, 0.f, 1.f) * half;
				if (y < 0.4f)
					continue;
				float x = ((float)b + 0.5f) * w / (float)BINS;
				nvgMoveTo(vg, x, mid - y);
				nvgLineTo(vg, x, mid + y);
			}
			nvgStrokeColor(vg, theme::text());
			nvgStrokeWidth(vg, std::max(1.f, w / (float)BINS * 0.8f));
			nvgStroke(vg);

			// grains: short teal ticks along the bottom
			if (module) {
				nvgBeginPath(vg);
				for (int i = 0; i < SpliceCore::GRAINS; i++) {
					float g = module->dispGrain[i].load(std::memory_order_relaxed);
					if (g < -0.5f)
						continue;
					g -= std::floor(g);
					nvgRect(vg, g * w - 1.f, bottom - 7.f, 2.f, 7.f);
				}
				nvgFillColor(vg, teal);
				nvgFill(vg);
				// play position inside the slice
				if (repeat >= 0) {
					float ph = module->dispPlay.load();
					ph -= std::floor(ph);
					nvgBeginPath(vg);
					nvgRect(vg, ph * w - 0.5f, top, 1.f, bottom - top);
					nvgFillColor(vg, teal);
					nvgFill(vg);
				}
			}

			// record head: amber while recording, grey while the tape is held
			nvgBeginPath(vg);
			nvgRect(vg, write * w - 0.75f, top - 2.f, 1.5f, bottom - top + 2.f);
			nvgFillColor(vg, recording ? theme::time() : theme::dim());
			nvgFill(vg);

			// text line
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, 10.f);
				int div = module ? module->dispDiv.load() : 8;
				float seconds = module ? module->dispSeconds.load() : 0.3f;
				std::string s = div > 0 ? string::f("SLICE 1/%d BAR  %.0f ms", div, seconds * 1000.f) : string::f("SLICE %.0f ms", seconds * 1000.f);
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgFillColor(vg, theme::dim());
				nvgText(vg, 4.f, 3.f, s.c_str(), NULL);
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
				if (repeat >= 0) {
					nvgFillColor(vg, teal);
					nvgText(vg, w - 4.f, 3.f, string::f("REPEAT %d", repeat + 1).c_str(), NULL);
				}
				else {
					nvgFillColor(vg, recording ? theme::time() : theme::dim());
					nvgText(vg, w - 4.f, 3.f, recording ? "REC" : "HELD", NULL);
				}
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}
};

struct SpliceWidget : ModuleWidget {
	static Vec at(const float p[2]) { return mm2px(Vec(p[0], p[1])); }

	SpliceWidget(Splice* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Splice.svg")));
		addChild(createWidget<ScrewBlack>(Vec(0, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		SpliceDisplay* display = new SpliceDisplay;
		display->module = module;
		display->box.pos = mm2px(Vec(L::DISPLAY[0][0], L::DISPLAY[0][1]));
		display->box.size = mm2px(Vec(L::DISPLAY[1][0], L::DISPLAY[1][1]));
		addChild(display);

		addParam(createParamCentered<CKSSThree>(at(L::REC), module, Splice::REC_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DIV), module, Splice::DIV_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::TIME), module, Splice::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::HOLD), module, Splice::HOLD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::ACCEL), module, Splice::ACCEL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::PITCH), module, Splice::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::REVERSE), module, Splice::REVERSE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DECAY), module, Splice::DECAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SMEAR), module, Splice::SMEAR_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::GRAIN), module, Splice::GRAIN_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::DENSITY), module, Splice::DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::JITTER), module, Splice::JITTER_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::WIDTH), module, Splice::WIDTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(L::SEED), module, Splice::SEED_PARAM));

		addInput(createInputCentered<PJ301MPort>(at(L::L_IN), module, Splice::L_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::R_IN), module, Splice::R_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PHASE_IN), module, Splice::PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::STUTTER_IN), module, Splice::STUTTER_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::FREEZE_IN), module, Splice::FREEZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SCRUB_IN), module, Splice::SCRUB_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::ACCEL_IN), module, Splice::ACCEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PITCH_IN), module, Splice::PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SMEAR_IN), module, Splice::SMEAR_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::DIV_IN), module, Splice::DIV_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::SEED_IN), module, Splice::SEED_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(at(L::L_OUT), module, Splice::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::R_OUT), module, Splice::R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::SLICE_OUT), module, Splice::SLICE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::REPEAT_OUT), module, Splice::REPEAT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Splice* m = getModule<Splice>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Save buffer with patch (the 16 seconds of tape come back when the patch is opened)", "", &m->saveBuffer));
	}
};

Model* modelSplice = createModel<Splice, SpliceWidget>("Splice");
