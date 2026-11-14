# Build instructions: the "Codex" VCV Rack plugin

**Audience:** an Opus model implementing this plugin from scratch.
**Owner:** Haer. Haer is fluent in VCV Rack but does **not** read C++. Haer checks each milestone by building and listening in Rack, using the checklists in this file. Write everything a C++ reader would usually be trusted to infer.

Companion files in the same folder:
- `01-research-notes.md`: what is known about Autechre's tools. It is background only.
- `02-module-proposal.md`: the design history. Where it disagrees with **this** file, **this file wins**.

---

## 0. Ground rules for the implementer

1. **Build in milestones (§12).** At the end of each one: the plugin compiles with no warnings at `-Wall -Wextra`, all unit tests pass, and you hand Haer a short note. The note covers what to install, what to patch, and what they should hear, taken from that module's "Haer's checklist".
2. **Keep the DSP separate from Rack.** All signal processing lives in `src/dsp/` as plain C++17. It may include `<rack.hpp>` only for `rack::simd` when needed, and must never touch Module, Widget or APP. Module classes in `src/modules/` are thin wrappers. This is what makes unit testing possible.
3. **No allocation, locks, file I/O or logging in `process()`.** Resize buffers in `onSampleRateChange()` or in the constructor.
4. **All randomness is seeded and repeatable** (§3.4). Two runs of the same patch with the same seeds give identical output.
5. **Every parameter has a tooltip, a unit and a sensible default.** Every port has a description (`configInput(id, "Name")`, `configOutput`). Right-click menus explain what an option does.
6. **Don't invent features.** If the spec is ambiguous, pick the simplest reading, write it in `docs/DECISIONS.md`, and tell Haer at the milestone hand-off.
7. **CPU budget:** each module stays under 1.5% of one core at 48 kHz on a mid-range 2020 laptop. Rack's CPU meter shows this (View → Show CPU meter). The exceptions are STRIKE at 8-voice polyphony (under 4%) and DEPTH (under 3%).
8. Don't copy code from other plugins unless their licence is compatible (GPL-3.0-or-later). If you do, credit it in `docs/CREDITS.md`.

---

## 1. Setup

### 1.1 Toolchain
- Target **VCV Rack 2.x** and use the newest 2.x Rack SDK from https://vcvrack.com/downloads/ (`Rack-SDK-2.x.x-<platform>.zip`). Read https://vcvrack.com/manual/PluginDevelopmentTutorial and https://vcvrack.com/manual/Building first.
- Platform build environments are described in the Building manual: MSYS2 MinGW64 on Windows, Xcode command-line tools on macOS, and `build-essential`, `git`, `zstd`, `jq` and similar packages on Linux.
- `export RACK_DIR=/path/to/Rack-SDK`, then `make`, `make dist`, and `make install` (which copies the plugin into Rack's user plugin folder).

### 1.2 Repository location
The plugin lives in **https://github.com/hardyfinn/HaerSystemMk1**. These design docs are in `docs/design/` there. Build the plugin at the repository root, laid out as:

```
HaerSystemMk1/
  plugin.json
  Makefile                 # the standard SDK Makefile, plus a `test` target (§11)
  res/                     # panel SVGs, one per module
  src/
    plugin.hpp / plugin.cpp
    common/                # shared code: Phase, Trigger, Rng, Curve, KeepProtocol, Theme
    dsp/                   # pure DSP cores, one header/cpp pair per module
    modules/               # Rack Module + ModuleWidget per module
    widgets/               # custom displays
  tests/                   # doctest unit tests for src/dsp and src/common
  docs/
    MANUAL.md  DECISIONS.md  CREDITS.md  PATCHES.md
```

### 1.3 plugin.json
- `slug`: `"Codex"` (placeholder; ask Haer before the first release). `name`: `"Codex"`. `version`: `"2.0.0"`. `license`: `"GPL-3.0-or-later"`. `author`: `"Haer"`.
- Module slugs: `Warp`, `Mutate`, `Contour`, `Chain`, `Strike`, `Fmx`, `Splice`, `Depth`, `Vault`, `Steer`, `Flow`, `Ear`, `TuneMap` (13 modules).
- Tags: use real Rack tags (`"Clock generator"`, `"Sequencer"`, `"Drum"`, `"Oscillator"`, `"Physical modeling"`, `"Sampler"`, `"Granular"`, `"Reverb"`, `"Recording"`, `"Controller"`, `"Polyphonic"`, `"Random"`).

### 1.4 Panels
- Make each panel SVG by hand with a **`components` layer**. That layer uses colour-coded placeholder shapes: red `#ff0000` for params, green `#00ff00` for inputs, blue `#0000ff` for outputs, magenta `#ff00ff` for lights, and yellow `#ffff00` for custom widgets. Run `$RACK_DIR/helper.py createmodule <Slug> res/<Slug>.svg src/modules/<Slug>.cpp` to generate the starting code. Hide the components layer in the final SVG.
- Convert all text to paths (Rack does not render SVG text). Inkscape can do this from the command line: `inkscape --export-text-to-path`. If Inkscape is not available, use simple hand-drawn path lettering, or draw labels with NanoVG in a widget.
- Visual style: dark grey panel `#1d1d1f`, light text `#d8d8d8`, and one accent colour per layer. Time modules use amber `#e8a33c`, sound modules teal `#3cc8b4`, and system modules violet `#9b7be8`. Use the standard component library: `RoundBlackKnob`, `RoundSmallBlackKnob`, `Trimpot`, `PJ301MPort`, `VCVButton`, `CKSS` and lights.
- Panel widths in HP are given in each module section. They are guidelines; widen a panel if needed and note the change.

---

## 2. Signal conventions (all modules)

| Signal | Range | Rule |
|---|---|---|
| **Phase** | 0 to 10 V ramp | One cycle is one bar unless stated otherwise. Readers must handle backward motion, jumps and audio-rate ramps (§3.1). |
| Gate / trigger | 0 / 10 V | Output triggers last 1 ms (`dsp::PulseGenerator`). Inputs use a Schmitt trigger with thresholds of 0.1 V (low) and 1 V (high). |
| Velocity, accent, lock CV | 0 to 10 V | |
| Pitch | 1 V/oct, 0 V = C4 | |
| Bipolar CV | ±5 V | CV inputs are scaled by an attenuverter wherever a knob has one. |
| Tempo CV | 0 V = 120 BPM, +1 V doubles | The same convention as VCV Clocked and many other clocks. |

**Polyphony:** a mono input feeding a polyphonic module applies to every channel. The output channel count is the maximum of the relevant input channel counts, or a channel-count setting when stated. Use `getPolyVoltage(c)`.

---

## 3. Shared code (`src/common/`)

### 3.1 `PhaseReader`
Turns a 0 to 10 V ramp into a continuous **unwrapped position** `W` in cycles, counting bars.
- `p = clamp(v / 10, 0, 1)`. Let `d = p - p_prev`. If `d < -0.5`, the ramp wrapped forward, so `d += 1`. If `d > 0.5`, it wrapped backward, so `d -= 1`. Then `W += d`.
- `reset()` sets `W = 0`. Expose `bar()` as `floor(W)`, `frac()`, and `crossed(a, b)`, which reports the event positions crossed between the previous `W` and the current `W`.
- **Crossing detection** is the basis of every phase-driven sequencer. To find the step boundaries crossed this sample, use `floor(W_prev * k)` against `floor(W * k)`. Fire one event for each boundary crossed, with a maximum of 8 per sample. If the motion is backward, fire events in reverse order, or not at all if the module's "reverse" option is off.
- Unit tests: a forward ramp; a wrap; a backward wrap; a jump of more than half a cycle (counted as a wrap, which is documented as a limitation); and an audio-rate ramp at 1 kHz, where every step is still detected.

### 3.2 `Curve`
A monotonic cubic (Fritsch–Carlson) spline through N points (2 to 16) on [0,1]→[0,1], plus named shapes:
- `ease(p, k) = p^k` for k from 0.25 to 4.
- `scurve(p, k)`: a normalised logistic.
- `spline(points)`.

`CurveDisplay` is a widget that draws a curve and lets the user drag its points. It is used by WARP and SPLICE.

### 3.3 `TriggerOut` and `SchmittIn`
Thin wrappers around the Rack DSP helpers, working on 16 channels.

### 3.4 `Rng`
- Use **xoshiro128\*\*** seeded through splitmix64. **Do not use `rack::random`** for anything musical, because its output is not repeatable.
- Every module with randomness has a **SEED** knob (integers 0 to 999, snapped) and an optional SEED CV (0 to 10 V mapped onto 0 to 999). A changed seed takes effect at the next bar (or the next natural boundary) and re-creates the generator.
- `Rng::fork(tag)` derives independent streams per track or voice, so changing one track's settings doesn't change another track's random sequence.

### 3.5 `KeepProtocol` (state capture and recall)
This makes "generate first, keep, perform later" possible. Every generator (WARP, MUTATE, CONTOUR, CHAIN) and VAULT implements:

```cpp
struct Keepable {
  virtual json_t* captureState() = 0;           // full musical state: patterns, matrices, seeds, positions
  virtual void requestRestore(json_t* state) = 0; // takes ownership
  virtual std::string keepLabel() = 0;          // e.g. "MUTATE #3"
};
```

- Call `captureState()` **only from the UI thread** (a widget's `step()`). Generators keep a copy of their musical state that is safe to read, copied under a `std::atomic<bool>` "dirty" flag, or protected by a small spinlock that `process()` only ever *try-locks*.
- `requestRestore()` parses the JSON on the UI thread into a prepared state struct. It then sets an atomic pointer that `process()` swaps in at the next step or bar boundary.
- Every generator has a **KEEP** trigger input and button. When it fires, the module stores a snapshot in its own *keep slots* (8 per module, kept in the patch JSON). A small LED row shows which slots are full. A **RECALL** knob (0 to 8, where 0 means live) and a trigger recall a slot at the next bar.
- STEER and VAULT find Keepable modules with `APP->engine->getModuleIds()` and `getModule(id)`, then `dynamic_cast<Keepable*>`, always on the UI thread.

### 3.6 Patch storage
- Use `dataToJson`/`dataFromJson` for small state.
- For anything larger than about 100 KB (VAULT clips, SPLICE buffers), use Rack 2's per-module patch storage: `createPatchStorageDirectory()` and `getPatchStorageDirectory()`, written in `onSave()` and read in `onAdd()`. Follow the Rack 2 API for these exact calls.

---

## 4. WARP: curved master clock (12 HP, amber)

**Purpose:** the time source. Its tempo drifts on curves, and its bars bend internally. It sends out four related phase streams.

### Parameters
| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| BPM | Tempo | 20–300 BPM | 100 | Summed with the TEMPO input (tempo CV convention) |
| BEATS | Bar length | 1.00–16.00 beats | 4.00 | Continuous. Fractional bars are allowed. |
| DRIFT_DEPTH | Tempo drift | 0–25 % | 0 | |
| DRIFT_RATE | Drift period | 1–64 bars | 8 | Period of the drift curve |
| WARP_SHAPE | Warp shape | 0–4 continuous | 0 | 0 ease-in, 1 ease-out, 2 S-curve, 3 drunk, 4 drawn. Morphs between neighbouring shapes. |
| WARP_DEPTH | Warp depth | 0–1 | 0 | |
| RATIO_1..4 | Output ratio | 1/16–8 | 1, 1/2, 3/4, 5/4 | Right-click option per output: snap to the ratio list `1/16 1/8 1/6 1/4 1/3 1/2 2/3 3/4 1 5/4 4/3 3/2 5/3 2 3 4 5 6 7 8`, or free |
| OFFSET_1..4 | Phase offset | 0–1 | 0 | Trimpots |
| FOLLOW_1..4 | Warp follow | 0–1 | 1 | How much each output follows the warp |
| SEED | Seed | 0–999 | 0 | Drives drift and the drunk shape |
| RUN | Run | button (latch) | on | |
| RESET | Reset | button | | |
| KEEP | Keep | button | | §3.5 |

### Inputs
TEMPO (CV), WARP_DEPTH CV, WARP_SHAPE CV (±5 V moves through 0 to 4), RESET, RUN (gate: high = running), KEEP, RECALL, SEED CV.

### Outputs
PHASE_1..4 (0 to 10 V), PHASE_POLY (4 channels), BAR (trigger), BEAT (trigger), TEMPO_OUT (the current effective tempo, as a CV).

### Algorithm
1. Effective BPM = `BPM × 2^TEMPO_cv × (1 + DRIFT_DEPTH × drift(t))`. `drift` is a smooth, repeatable random curve in [-1, 1]: monotone cubic interpolation through one value per DRIFT_RATE bars, from Rng stream "drift".
2. Bar rate in Hz = `BPM_eff / 60 / BEATS`. Advance the linear bar position `B += rate / sampleRate`. Let `p = frac(B)` and `n = floor(B)`.
3. Warp function `w(p)`. Each shape is monotonic with `w(0) = 0` and `w(1) = 1`:
   - ease-in: `p^2.2`
   - ease-out: `1 - (1-p)^2.2`
   - S-curve: normalised logistic with k = 8
   - drunk: a Fritsch–Carlson spline through 5 interior points, drawn new at every bar start from Rng stream "drunk". The points are sorted and kept at least 0.04 apart.
   - drawn: a user spline with 6 points, edited on the display and saved in JSON.
   - Morph between neighbouring shapes linearly; a mix of monotonic functions is still monotonic.
   - Then `w_d(p) = p + WARP_DEPTH × (w(p) - p)`.
4. Output i: `W_i = RATIO_i × (n + p + FOLLOW_i × (w_d(p) - p)) + OFFSET_i`, and output `10 × frac(W_i)`.
5. Beat triggers fire when `floor((n + w_d(p)) × BEATS)` increases. The trigger lands on warped beat positions, so the beats bend too. BAR fires on each bar wrap.
6. RESET sets B to 0, re-seeds drunk/drift from the seed, and fires BAR. When stopped, the phase holds its value.

### Display (yellow widget, 10 HP × 40 mm)
Shows the warp curve with a moving playhead, the drift trace over the last 16 bars, and the four output ratios. You can drag the drawn shape's points when WARP_SHAPE is near 4.

### Keep state
BPM, BEATS, shapes, drawn points, seed, and the drift-curve control points.

### Unit tests
- With depth 0 and no drift, PHASE_1 at 120 BPM over 4 beats completes exactly one cycle in 2.000 s (±1 sample).
- `w_d` is monotonic for 10,000 random parameter sets.
- With RATIO = 3/4, depth 0 and offset 0, the unwrapped position `W` equals 3.0 (±1e-6) after exactly 4 bars.
- The same seed gives an identical drift sequence across two runs.

### Haer's checklist
- Patch PHASE_1 into Fundamental **Scope**. You should see a sawtooth at bar rate. Raise WARP_DEPTH with the S-curve shape: the ramp should bend into an S shape while its period stays the same.
- Patch BEAT into a drum voice. With WARP_DEPTH at 0 the beats are even. As you raise it, the beats should bunch together, then spread out within each bar, and line up again on the downbeat.
- Raise DRIFT_DEPTH to 10%. Over about 30 seconds the tempo should breathe slowly.
- The same seed after a reset should give exactly the same drift. Listen to the first 8 bars twice.

---

## 5. MUTATE: step drums that rewrite themselves (20 HP, amber)

**Purpose:** Untilted/Quaristice-style drum patterns that mutate slowly, with per-step sound locks. **This supersedes the proposal:** MUTATE has **8 tracks**, not 16, because Haer prefers steering to editing.

### Model
- 8 tracks. Each track has a length (1 to 64 steps) and a **steps-per-bar** setting shared by all tracks (STEPS: 4 to 32, default 16). Tracks with different lengths drift against each other (polymeter).
- Each step stores: `on` (bool), `vel` (0–1), `ratchet` (1–8), `micro` (-0.5 to +0.5 of a step), `cond` (always / p% / every Nth pass / first pass only / if the previous step fired), `p` (0–1), `N` (2–8), and two locks `lockA` and `lockB` (0–1).
- **Home pattern:** a saved copy of the pattern. It is set when the pattern is generated or when the HOME SET button is pressed.

### Generation (so Haer doesn't have to enter steps)
A GENERATE button (and trigger input) rebuilds every track from that track's **style**. The style is chosen in the track's right-click menu: kick, snare, hat, perc, texture or free. Generation runs under the seed and a **DENSITY** macro:
- **kick:** a Euclidean pattern E(k, len) with k ≈ density×len/4, rotated by a seeded amount, plus syncopated extra hits with probability density×0.3.
- **snare:** hits near beats 2 and 4 (fractional bars are respected), with seeded ghost notes at vel 0.2–0.4.
- **hat:** a dense Euclidean pattern with a velocity curve from a seeded 4-point spline, plus ratchets of 2 to 3 with probability 0.1.
- **perc / texture / free:** seeded sparse patterns with long ratchets and wide micro-timing.
- `lockA` and `lockB` come from two seeded smooth curves across the steps, so sound changes form shapes rather than noise.
- You can also edit by hand on the grid display: click to toggle a step, and drag vertically to change its velocity. Hand editing is optional, not required.

### Mutation engine
Every `EVERY` bars (1 to 16, default 2), run `round(AMOUNT × 4)` mutation operations. Pick each one with weights from the rule knobs:
| Rule | Action |
|---|---|
| ROTATE | Rotate one track by ±1 step |
| NUDGE | Change one hit's `micro` by ±0.15 |
| RATCHET | Set a hit's ratchet to 2–4 (or back to 1) |
| THIN/FILL | Remove a hit (vel < 0.5 first) or add one where the Euclidean grid has a gap |
| MIRROR | Reverse a section of 4–8 steps in one track |
| COPY | Copy a span of 4–8 steps from one track to another, keeping the destination's locks |
| LOCKWALK | Move lockA/lockB on a run of 2 to 6 steps by ±0.1 |

- **DRIFT** (0 to 1): each operation is a new mutation with probability DRIFT. Otherwise it is a **return step** that sets one step that differs from home back to its home value. At DRIFT 0.5 the pattern wanders around home without leaving it for good. At 1 it wanders freely.
- **HOME** button: returns to the home pattern at the next bar. **HOME SET** stores the current pattern as home.
- Mutations happen only at bar boundaries. Changes are double-buffered so they never apply partway through a bar.

### Timing
- Clock source: the PHASE input (from WARP). Step position = `W × STEPS`. If PHASE is not connected, use CLOCK + RESET inputs, where each clock is one step.
- A hit's trigger time = step boundary + `micro × stepLength`, plus `MICRO_CV × 0.5` if the MICRO curve input is connected. That input is ±5 V and is read at the step. Hits with negative micro are scheduled from the previous step.
- **Ratchets:** n sub-hits within the step, spaced by `RATCHET_CURVE` (-1 to 1). At -1 they accelerate, at 0 they are even, at +1 they decelerate. Sub-hit k sits at `ease(k/n)` with exponent 2^(2×curve). Velocity across the ratchet decays by a factor `RATCHET_DECAY`.
- Conditions are evaluated per pass of each track, with a pass counter per track.

### Parameters (main)
STEPS, DENSITY, SEED, GENERATE, EVERY, AMOUNT, DRIFT, the 7 rule weights (trimpots), HOME, HOME SET, RATCHET_CURVE, RATCHET_DECAY, SWING (0 to 0.5, applied to odd steps before micro), KEEP. Each track has a length set on the display (drag the end marker) and a mute (click the track label).

### Inputs
PHASE, CLOCK, RESET, GENERATE, MUTATE (forces one mutation pass), HOME, DENSITY CV, AMOUNT CV, DRIFT CV, MICRO (curve), KEEP, RECALL, SEED CV.

### Outputs
GATE_1..8 (triggers), GATES_POLY (8 channels), VEL_POLY (8 channels), LOCK_A_POLY and LOCK_B_POLY (8 channels, sample-and-hold at each hit, 0 to 10 V), ACCENT (triggers on hits with vel > 0.8).

### Display
An 8 × up-to-64 grid. Brightness shows velocity, a small tick shows micro offset, dots show ratchets, and a playhead runs on each track. A thin bar per track shows how far it has drifted from home.

### Unit tests
- Generation is repeatable: the same seed, density and styles give an identical pattern.
- With DRIFT = 0, applying N return steps converges to home.
- Ratchet timing: with curve 0, the sub-hits are evenly spaced (±1 sample).
- A phase input running backward produces no triggers (default) and no crash.
- At 1 kHz phase rate with STEPS 32, no step is missed.

### Haer's checklist
- Patch WARP PHASE_1 → MUTATE PHASE. Send GATE_1..4 to four drum voices (STRIKE, or any Fundamental/Befaco drum). Press GENERATE: you should hear a coherent beat.
- Set AMOUNT to 0.5 and DRIFT to 0.5. Over two minutes the beat should keep changing but stay recognisably itself. Press HOME and it should snap back.
- Raise WARP_DEPTH on WARP: the drums should bend in time together.
- Set a hat track to style "hat" and RATCHET_CURVE to -1: the rolls should speed up.

---

## 6. CONTOUR: curve-crossing rhythms with interference (14 HP, amber)

**Purpose:** elseq-style rhythms where hits fall where a continuous curve crosses levels. It includes the MOIRÉ interference source.

### Curve sources (MODE switch, 3 positions)
1. **HARM:** `c(φ) = Σ_{k=1..4} A_k sin(2π(R_k φ + Θ_k))`, normalised by `Σ|A_k|`. Each harmonic k has an amplitude A_k (0 to 1), a ratio R_k (0.25 to 16, with snap-to-integer optional), and a phase Θ_k.
2. **MOIRÉ:** 8 phasors with rates `r_j = base × ratio_j × (1 + SPREAD × δ_j)`.
   - The ratio set is chosen from the menu: harmonic `1..8`, golden ratio `φ^j mod 1 + 1`, primes `2,3,5,7,11,13,17,19 / 2`, or seeded.
   - `δ_j` are seeded values in ±1.
   - The curve is `c = mean_j sin(2π θ_j)`.
   - **Coincidence:** when at least `COINCIDE_N` phasors (2 to 8) are within `WINDOW` (0.001 to 0.05 cycle) of 0 at the same time, fire COINCIDE and output COUNT as `count × 1.25 V`.
3. **EXT:** the curve is the EXT input (±5 V divided by 5).

### Time base
- With PHASE connected, φ = the unwrapped `W` from §3.1, so HARM ratios count per bar.
- Otherwise there is an internal phasor whose RATE runs from 0.01 Hz to 2 kHz on an exponential knob, so CONTOUR becomes an audio-rate source. In MOIRÉ mode, `base` = the bar rate or the RATE.

### Threshold engine
- `N_THRESH` levels (1 to 16) placed in [-1, 1]. `SPREAD_T` (-1 to 1) controls placement: 0 gives even spacing, negative values cluster levels near 0 (more hits near zero crossings), and positive values cluster them near the extremes.
- DIRECTION: up / down / both.
- `HYST` (0 to 0.1) sets the re-arm distance.
- Each threshold crossing fires a trigger on **channel (threshold index mod CHANNELS)**. CHANNELS runs from 1 to 16.
- VEL = `clamp(|dc/dφ| / slopeNorm, 0, 1) × 10 V`, or the curve height, or threshold index / N. The VEL MODE menu chooses.
- DENSITY CV (±5 V) adds or removes thresholds in steps, rounding toward even spacing.

### Outputs
TRIG (poly, CHANNELS), VEL (poly), CURVE (±5 V), SLOPE (±5 V, scaled), COINCIDE (trigger), COUNT, and PHASORS (8-channel poly, 0 to 10 V; MOIRÉ mode only).

### Inputs
PHASE, EXT, DENSITY CV, MORPH CV (applies Θ_k += MORPH × k × 0.25, so the harmonics shift against each other), SPREAD CV, RATE CV (V/oct), KEEP, RECALL, SEED CV.

### Display
Shows the curve across one cycle, the threshold lines, crossing dots and a playhead. In MOIRÉ mode it also shows 8 small phase dials.

### Unit tests
- A sine with 1 threshold at 0 and "both" gives exactly 2 triggers per cycle.
- With N thresholds and a monotonic ramp curve, there are exactly N triggers per cycle.
- Hysteresis stops chattering on a noisy curve (feed in a seeded noise test signal).
- MOIRÉ with SPREAD 0 and the harmonic ratio set repeats exactly every cycle.
- At audio rate (RATE = 440 Hz, 4 thresholds), TRIG has a fundamental of 440 × 4 / 2 Hz under "both" on a sine (check with zero-crossing counting).

### Haer's checklist
- WARP PHASE → CONTOUR PHASE, TRIG → a percussion voice. Raise N_THRESH from 1 to 8: the rhythm should get denser, with bunching where the curve is steep.
- Turn MORPH slowly: the rhythm should reshape smoothly rather than jump.
- In MOIRÉ mode, set SPREAD to 0 for a locked groove. Then raise it to around 0.02: the groove should slide slowly out of phase and back.
- Unpatch PHASE and raise RATE into the audio range, patching TRIG into a VCA or directly into a mixer: you should hear a buzzy, pitched timbre.

---

## 7. CHAIN: Markov melody voices with harmony rules (16 HP, amber)

**Purpose:** Oversteps/Exai/Quaristice-style melodic lines. Up to 4 independent voices that also listen to each other.

### Model
- Pitch classes 0 to 11 plus an octave. **SCALE** is a 12-bit mask taken from presets (chromatic, major, minor, dorian, phrygian, lydian, whole tone, pentatonic min, hirajoshi, "Exai-ish": semitone clusters {0,1,3,7,8}) or from the SCALE poly input, read like VCV Quantizer's convention. **ROOT** sets the transposition.
- Each voice has a 12×12 **transition matrix** of weights (0 to 1). It is **order 1**, with an option for order 2 that uses a sparse map keyed by (prev, cur) pairs. Order 2 is learned only; when unseen, it falls back to order 1.
- Default matrices come from a **CHARACTER** knob per voice. At 0 the matrix is stepwise: nearby degrees are favoured, with weights following a Gaussian on scale-degree distance and σ = 1. At 0.5 it mixes thirds and fifths. At 1 it favours wide leaps and tritones. The weights are then multiplied by a seeded jitter of ±20%.
- **Learn:** while LEARN is high, the voice's matrix counts the transitions of the quantised pitch on LEARN_PITCH, sampled at each step trigger. RESET MATRIX goes back to the character default.

### Step
On a voice's STEP trigger: sample the next pitch class from the matrix row, masked by the scale. Choose the octave as the one nearest the previous note, within RANGE (1 to 4 octaves around CENTER). Then apply the rules below.

### Rules (global, each with a strength of 0 to 1; a rule applies with probability = strength)
1. **SCALE LOCK:** the result must be in the scale. This is always on at strength 1 unless set otherwise.
2. **NO PARALLELS:** no parallel 5ths or octaves against any other voice that moved at the same time.
3. **KEEP INTERVAL:** the interval class to the voice chosen in the menu (default voice 1) must be in a set {3, 4, 8, 9} (imperfect consonances; editable in the menu).
4. **CONTRARY:** move in the opposite direction to the reference voice's last move.
5. **MAX LEAP:** the leap must be ≤ LEAP semitones (2 to 24).
6. **HOME PULL:** the probability of forcing the root pitch class rises linearly with the steps since the last root, reaching 1 after HOME_STEPS (4 to 64).

Resolution: re-sample up to 8 times. If no candidate passes, choose the candidate that violates the fewest active rules (weighted by strength), and fire VIOLATION.

### Parameters
SCALE, ROOT, per-voice CHARACTER ×4, per-voice RANGE ×4 and CENTER ×4 (trimpots), the 6 rule strengths, LEAP, HOME_STEPS, SEED, KEEP. GLIDE (0 to 500 ms, applied to the outputs). ORDER (1/2).

### Inputs
STEP_1..4 (triggers; when only STEP_1 is connected, it clocks all four voices), SCALE (poly), ROOT CV, LEARN (gate), LEARN_PITCH, CHARACTER CV (applies to all voices), RESET, KEEP, RECALL, SEED CV.

### Outputs
PITCH_1..4, PITCH_POLY (4 channels), GATE_1..4 (a 10 ms gate on every step; marked `changed` when the note differs), VIOLATION (trigger).

### Display
Shows the selected voice's matrix as a 12×12 heatmap; click a voice number to select it. Click and drag on a cell to paint weights. Next to it is a small 4-voice piano-roll history covering the last 32 steps.

### Unit tests
- With SCALE LOCK at 1, every output is in the scale (100,000 steps).
- With MAX LEAP at 2, no leap exceeds 2 semitones.
- Over 10,000 steps with no rules, the transition frequencies match the matrix (χ² test, p > 0.01).
- Learning from a repeated C-E-G sequence gives a matrix where C→E, E→G and G→C dominate.
- Same seed, same output.

### Haer's checklist
- Patch MUTATE's GATE_3 → CHAIN STEP_1, and PITCH_1 → FMX V/OCT. Set CHARACTER 1 to 0: the line should mostly move stepwise. Raise CHARACTER to 1: it should leap.
- Clock 2 voices with KEEP INTERVAL at 1: the two lines should hold thirds and sixths against each other while each moves independently.
- Set HOME PULL to 0.7: the melody should keep returning to the root.

---

## 8. STRIKE: modal percussion (12 HP, teal)

**Purpose:** struck and resonant percussion that morphs from wood to rubber to metal to glass to membrane, plus a formant/"glottal" mode. Polyphonic, up to 8 voices.

### Exciter (EXCITER knob, 0 to 3 continuous)
| Value | Exciter |
|---|---|
| 0 | Impulse (a 1-sample click) |
| 1 | Mallet: a raised-cosine pulse, width 0.2–8 ms from HARDNESS (shorter = harder) |
| 2 | Noise burst: seeded white noise, length 1–50 ms, through a 1-pole lowpass whose cutoff follows HARDNESS |
| 3 | External audio: the EXCITE input |

- The exciter level is scaled by velocity.
- **BURST** (1 to 16) and **BURST_RATE** (2 to 200 Hz) re-fire the exciter as a granular-style burst on each trigger.

### Resonator: 32 modes
- Each mode is a 2-pole resonator: a decaying sine resonator, implemented as a biquad bandpass (Rack `dsp::BiquadFilter`) or directly as a recursive oscillator. Process 4 modes at a time with `simd::float_4`.
- **Material tables** give 32 frequency ratios plus a decay multiplier per mode. MATERIAL (0 to 4) interpolates between neighbouring tables, interpolating ratios in log space.
  - **wood:** ratios of a free bar (1, 2.756, 5.404, 8.933, 13.34, then further modes from `(2n+1)^2 × 0.5513` for n ≥ 5), with fast decay that rises strongly with frequency.
  - **rubber:** near-harmonic ratios 1, 1.9, 2.8, ... with very fast decay and low brightness.
  - **metal:** the free-bar ratios with slow decay, and only a mild rise in decay with frequency.
  - **glass:** ratios 1, 2.32, 4.25, 6.63, 9.38, ... with long decay and bright modes.
  - **membrane:** circular membrane ratios 1, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, ... (use the published Bessel-zero ratios; list 32), with medium decay.
- Controls:
  - **PITCH:** fundamental 20 Hz to 5 kHz, plus V/OCT.
  - **DECAY:** global 5 ms to 10 s.
  - **DAMP:** how much higher modes decay faster.
  - **INHARM:** stretches the ratios, `r' = r^(1 + INHARM × 0.1)`.
  - **POSITION:** strike position, 0 to 1. Mode amplitudes are multiplied by `|sin(π × n_k × pos)|`, with n_k = mode index + 1.
  - **BRIGHT:** a spectral tilt across the mode amplitudes.
- Mode frequencies above 0.45 × sample rate are muted.
- **Choke:** CHOKE_IN (trigger) or the menu option "choke on retrigger" fades out the existing ringing over 5 ms.

### Formant mode (MODE switch: Modal / Formant)
- The exciter becomes a glottal pulse train at the pitch set by PITCH/V/OCT. It uses a Rosenberg pulse: `0.5(1 - cos(πt/T_open))` while opening, then `cos(π(t - T_open)/(2 T_close))` while closing. OPEN QUOTIENT (0.3 to 0.8) uses the HARDNESS knob.
- The resonator becomes 5 formant bandpasses. VOWEL (via MATERIAL) morphs through the tables a-e-i-o-u (standard F1–F5 and bandwidths for adult male formants; list them in code with a source comment). SIZE (via INHARM) scales all formants by 0.7 to 1.4.
- The trigger acts as a gate: the pulse train runs with an AR envelope whose release is set by DECAY.

### Polyphony
The voice count is set by the TRIG input's channels (or the V/OCT channels, whichever is greater), up to 8. Each voice has its own resonator state.

### Inputs
TRIG (poly), VEL (poly), V/OCT (poly), MATERIAL CV, DECAY CV, POSITION CV, DAMP CV, BRIGHT CV (all poly-aware), EXCITE (audio), CHOKE.

### Outputs
OUT_L and OUT_R (voices are spread across the stereo field by SPREAD, 0 to 1), and OUT_POLY (each voice separately).

### Unit tests
- An impulse into a single mode (with the others muted in test mode) rings at the requested frequency ±0.5%, and its T60 matches DECAY ±10%.
- No NaN or Inf after 10 s of random parameter changes at audio rate.
- The output stays within ±10 V for any exciter at velocity 10 V (apply soft clipping at the output if needed).

### Haer's checklist
- Trigger it from MUTATE and sweep MATERIAL: you should hear a woodblock become rubber, then a struck metal bar, then glass, then a drum skin.
- Patch MUTATE's LOCK_A_POLY to MATERIAL CV and LOCK_B_POLY to POSITION CV: each hit should sound slightly different, and the differences should shift together over time.
- Set BURST to 6 at around 40 Hz: each hit should become a short rattle or roll.
- In Formant mode, hold a gate and sweep MATERIAL: you should hear vowels.

---

## 9. FMX: phase-driven FM voice (14 HP, teal)

**Purpose:** FM basses, bells and metallic tones whose timbre envelopes follow phase curves. Polyphonic, up to 8 voices.

### Operators
- 4 sine operators. Each has: RATIO (0.25 to 16, continuous; right-click "snap to integer/half" per operator), FINE (±50 cents), LEVEL (0 to 1, the output level or modulation index), and an index envelope amount ENV (0 to 1).
- Operator 4 has FEEDBACK (0 to 1, self-modulation averaged over two samples so it stays stable).
- **ALGORITHM** (1 to 8; carriers are on the right of each arrow):
  1. 4→3→2→1
  2. (4+3)→2→1
  3. 4→3, 2→1; carriers 1 and 3
  4. 4→(3, 2, 1)
  5. (4→3)+(2)→1
  6. 4→3→1, 2→1
  7. 4→1, 3, 2: all carriers, with 4 modulating 1
  8. all four carriers (additive)
  
  The panel shows the current algorithm as a small diagram.
- Use **2× oversampling** with Rack's `dsp::Decimator` (or a half-band polyphase filter). **INDEX** is a global modulation depth from 0 to 10 (in radians, scaled across the modulators).

### Envelopes ("phase-driven")
- Each voice has one envelope position `e`, running from 0 to 1, used for both amplitude and index:
  - If the voice's PHASE input is connected, `e = frac(phase)`, so the envelope follows WARP or CONTOUR curves exactly.
  - Otherwise a TRIG resets `e = 0` and `e` rises over TIME (2 ms to 8 s).
- AMP SHAPE: `amp = (1-e)^k` with k from 0.2 to 8. INDEX SHAPE has the same form with its own k. Each operator's index = `LEVEL × (1 - ENV + ENV × indexShape(e))`.
- **Audio-rate retrigger:** triggers can arrive at up to 2 kHz. Each one resets `e`. A menu option ("reset op phases on trigger") also resets the operator phases, which is good for hard, buzzy rolls.

### Inputs (all poly)
V/OCT, TRIG, PHASE, VEL, INDEX CV, RATIO_2 CV, RATIO_3 CV, RATIO_4 CV (each 1 V = +1 ratio unit, unless snap is on, where it steps through the snap list), FEEDBACK CV, ALGO CV (0 to 10 V across 1 to 8).

### Outputs
OUT_L, OUT_R (stereo spread), OUT_POLY, and OP_1..4 (each operator's output from voice 1, for patching into other modules).

### Unit tests
- Algorithm 8, all ratios 1, index 0: the output is a sine at the V/OCT frequency.
- Algorithm 1 with index > 0 has energy at the expected sidebands: the carrier ± n × the modulator frequency, checked with a Goertzel filter.
- Feedback 1 stays bounded with no NaN.
- At a 2 kHz retrigger rate the output is still bounded and the CPU stays within budget.

### Haer's checklist
- Use FMX as a bass: algorithm 1, ratios 1/1/2/3, a short INDEX SHAPE. It should sound hard and plucked.
- Patch WARP PHASE_2 into PHASE: the timbre should open and close once per cycle, bending with the warp.
- Set FINE on operator 2 to +3 cents: you should hear slow beating.
- Patch CONTOUR's audio-rate TRIG into TRIG with "reset op phases" on: you should hear grainy, fractured tones.

---

## 10. SPLICE: cut-up and smear buffer (12 HP, teal)

**Purpose:** 'spl9'-style chopped and treated audio. It captures live input and stutters, slices, reverses, re-pitches and smears it into grains.

### Buffer
- Stereo, 16 s at the current sample rate, allocated in `onSampleRateChange`.
- **REC** mode (switch: always / gated / off): it writes continuously into a circular buffer. FREEZE stops writing.
- A menu option "save buffer with patch" uses §3.6 patch storage to save a WAV (write minimal WAV headers by hand, or use dr_wav, which is public domain).

### Slicing
- SLICE length = `1 / DIV` of a bar from the PHASE input, with DIV = 1, 2, 3, 4, 6, 8, 12, 16, 24 or 32. Without PHASE, use TIME (10 ms to 2 s).
- When STUTTER goes high (a gate; or a trigger plus the HOLD length setting), capture `start = writePos - sliceLen` (the most recent slice). Then loop that slice:
  - Repeat k lasts `sliceLen × ACCEL^k`, where ACCEL runs from 0.5 to 2. Below 1 the repeats speed up, above 1 they slow down. Repeat lengths are clamped between 2 ms and the full slice.
  - Repeat k is pitch-shifted by `PITCH_RAMP × k` semitones (±2 per repeat), done by changing the playback speed (tape style, so the length changes with it).
  - Each repeat plays reversed with probability REVERSE (seeded).
  - Each repeat's level is multiplied by DECAY (0.5 to 1).
  - Use a 2 ms equal-power crossfade at every boundary.
- **SCRUB** input (0 to 10 V): while connected, the read position within the slice comes from the input, which allows hand-drawn cut-ups.

### Smear (granular)
- SMEAR (0 to 1) crossfades from the stutter output to a granular engine reading around the current slice.
- Grain size is 5 to 200 ms (GRAIN). DENSITY runs from 2 to 100 grains per second. Positions are jittered by JITTER (0 to 1 slice) using a seeded Rng. Each grain uses a Hann window and gets random stereo placement from WIDTH.
- At most 32 active grains.

### Inputs
IN_L, IN_R, PHASE, STUTTER, FREEZE, SCRUB, ACCEL CV, PITCH_RAMP CV, SMEAR CV, DIV CV, SEED CV.

### Outputs
OUT_L, OUT_R, SLICE (a trigger at each repeat start), and REPEAT_INDEX (k × 0.5 V).

### Display
A waveform of the buffer showing the write head, the current slice region and grain positions.

### Unit tests
- With ACCEL 1, PITCH_RAMP 0 and REVERSE 0, a stutter of a test sine reproduces the slice exactly. Boundaries are continuous within the crossfade tolerance.
- With ACCEL 0.5, repeat lengths halve until they hit the 2 ms clamp.
- No clicks: the maximum sample-to-sample jump at boundaries stays below a threshold on a DC input.

### Haer's checklist
- Feed in a breakbeat (from any sample player) or STRIKE's output. Send MUTATE's ACCENT into STUTTER: the accented beats should chop and repeat.
- Set ACCEL to 0.6: the stutters should speed up into a buzz.
- Slowly raise SMEAR: the chops should dissolve into a granular cloud.

---

## 11. DEPTH: spatial reverb with distance placement (16 HP, teal)

**Purpose:** "deep mixing". Four sources are placed at different distances in one shared room, with a reverb tail that can bloom.

### Per-input channel (×4 stereo; a mono input is copied to both sides)
- **DIST** (0 to 1) and its CV. DIST controls:
  - **Pre-delay:** `2 + 60 × DIST` ms.
  - **Air loss:** a 1-pole lowpass whose cutoff goes from 18 kHz down to `1.5 kHz + 16.5 kHz × (1 - DIST)^2`.
  - **Direct gain:** `1 / (1 + 4 × DIST)`.
  - **Early reflections:** a tapped delay with 8 taps. Tap times = `SIZE × {7, 11, 17, 23, 31, 41, 53, 67} ms`, with alternating pan. The ER level rises with DIST.
  - **Send to the tail:** `0.2 + 0.8 × DIST`.
- **PAN** (±1) per channel.

### Tail
- Input diffusion: 4 allpass filters per side (delays in prime numbers of ms, coefficient 0.6).
- **FDN with 16 lines:**
  - Delay lengths = SIZE × a set of 16 mutually prime values between 29 and 157 ms.
  - Mixing uses a 16×16 normalised Hadamard matrix.
  - Each line has a 1-pole damping filter (DAMP) and a feedback gain set from DECAY (T60 from 0.3 to 30 s), using the standard per-line formula `g_i = 10^(-3 × len_i / (T60 × fs))`.
- **MOD:** each line's delay is modulated by a slow sine (0.05 to 2 Hz, each line at a different seeded rate) through fractional delay reads (cubic interpolation). Depth is 0 to 2 ms.
- **INSTABILITY** (0 to 1): feedback gain × `(1 + 0.08 × INSTABILITY)`, and the Hadamard matrix is rotated by an angle that drifts slowly. A `tanh` soft clip in each line limits the energy. At 1 the tail grows and smears but never runs away beyond the soft clip.
- **FREEZE:** input to the tail is 0, damping is bypassed, and feedback is set to the gain that holds the level (lossless).

### Outputs
OUT_L, OUT_R (dry direct + ER + tail, with MIX setting the overall wet/dry balance), and WET_L, WET_R (ER + tail only).

### Inputs
IN_1..4 L/R, DIST_1..4 CV, SIZE CV, DECAY CV, INSTABILITY CV, FREEZE (gate), MOD CV.

### Unit tests
- With INSTABILITY 0, the measured T60 of an impulse response is within ±15% of the DECAY setting (SIZE 1).
- With INSTABILITY 1, the output stays below ±12 V after 60 s of white-noise input.
- With FREEZE on, the level after 10 s stays within 3 dB of the level when freeze was engaged.

### Haer's checklist
- Send four voices in. Set DIST at 0.1, 0.4, 0.7 and 0.95: you should hear a clear front-to-back layout, not just more or less reverb.
- Modulate DIST_1 with a slow LFO: the sound should move away and come back.
- Raise INSTABILITY on a sustained sound: the tail should swell and smear but never run away.

---

## 12. VAULT: rolling capture, KEEP and clip launcher (20 HP, violet)

**Purpose:** generate first, keep the good moments, and perform them later. VAULT records control streams (gates + CV), not audio.

### Recording
- **8 lanes.** Each lane has a GATE input (poly, up to 16 channels) and a CV input (poly). The CV can carry pitch, velocity or a lock.
- Timing reference: the PHASE input (one cycle = one bar), unwrapped (§3.1). If PHASE is not connected, VAULT uses the transport time from its own TEMPO knob, plus a BEATS setting.
- **Rolling capture of the last 64 bars, as events:**
  - **Gate events:** each rising or falling edge per channel, stored as `{double barPos, uint8 lane, uint8 ch, bool on, float cv}`. `cv` is the lane's CV for that channel at the edge.
  - **CV moves:** stored as `{barPos, lane, ch, value}` when the value has changed by more than 1 mV since the last stored value, at most every 1/384 bar per channel.
  - The ring buffer is pre-allocated at 262,144 events. When it is full, the oldest events are dropped.
- **KEEP** (button/trigger): the last `KEEP_BARS` bars (1, 2, 4, 8, 16, 32 or 64) are copied **on the UI thread** into the next free clip slot. Slot positions are measured relative to the clip's first bar boundary.
  - Each clip stores: its events, its length in bars, the BPM and BEATS at capture time (for reference only), a timestamp, a name (default "clip NN"; editable from the right-click menu), and an optional link to a STEER version-tree node (§13).
- **KEEP ALL** (menu): also asks every Keepable module in the rack to capture its state into the clip's metadata (§3.5). Recalling the clip can then restore those generators.

### Clip bank
- 64 slots, shown as an 8×8 grid on the display.
  - Empty slots are dark. Full slots are lit. Playing slots are pulsing.
  - Click a slot to select it. Use the right-click menu to rename, delete, duplicate, export the clip as JSON, import JSON, or "recall linked states".
- Clips are saved in patch storage (§3.6) as `clips.json`.

### Playback: 8 playback lanes
- Each playback lane has a CLIP selector (0 to 64; 0 = off) and a LAUNCH trigger. Launch is quantised to the next bar, or the next beat, chosen in the menu.
- **Timing comes from the current PHASE input:** clip position = `(W - launchBar) mod length`. A kept clip therefore follows WARP's current bending.
- Per playback-lane controls on the menu, plus 4 CV inputs:
  - TRANSPOSE (±24 semitones, added to the CV only if the lane is marked "pitch")
  - THIN (0 to 1: skip each note-on with probability THIN, seeded)
  - REVERSE (play `length - pos`)
  - WINDOW (loop only a sub-range: START and LENGTH as a fraction of the clip)
- Global knobs act on the selected playback lane: CLIP, THIN and TRANSPOSE.
- **Outputs:** 8 lanes, each a GATE (poly) and a CV (poly), plus END (a trigger when each lane's clip wraps).

### Inputs
PHASE, GATE_1..8, CV_1..8, KEEP, LAUNCH_1..8, CLIP_CV (selects the clip for the selected lane), THIN CV, TRANSPOSE CV, STOP_ALL.

### Unit tests
- Record a known gate pattern over 4 bars, keep 4 bars, and play it back with the same phase: the events match within 1 sample.
- Play back with phase at half speed: the events land at the same bar positions, so twice as far apart in time.
- When the ring buffer overflows, the oldest events go first and nothing crashes.
- Clips survive a JSON round trip unchanged (save to patch storage, then reload).

### Haer's checklist
- Patch MUTATE's GATES_POLY → VAULT GATE_1, and CHAIN's PITCH_POLY/GATE → lane 2. Let the patch run for a few minutes and press KEEP (8 bars) whenever you like something.
- Save the patch, close Rack, reopen it: the clips should still be there.
- Unpatch the generators. Patch VAULT's lane outputs into STRIKE and FMX, then launch clips: you should hear the kept material, following WARP's current tempo and bending.
- Raise THIN: the clip should play sparser versions of itself.

---

## 13. STEER: macros, scenes and version tree (16 HP, violet)

**Purpose:** perform by steering. 8 macros move many parameters. Scenes recall whole setups. A version tree (formerly BRANCH) stores and morphs between whole-rack states.

### Macros
- 8 macro knobs (0 to 1), each with a CV input (0 to 10 V added, then clamped).
- Each macro has up to 16 **mappings**:
  - **To map:** click the small MAP button under a macro, then click any parameter on any module in the rack. Use `ParamHandle` the same way VCV's MIDI-Map does (read the MIDI-Map source in Rack's `src/core/MIDIMap.cpp` for the pattern). Right-click the macro to list its mappings and remove them.
  - Each mapping stores a min, a max and a response curve: linear, exponential, logarithmic, S-shaped, or "step" (quantised to N steps). Edit these in a small editor popup, or in the right-click menu with sliders.
  - The parameter value = `min + (max - min) × curve(macro)`.
- **Gesture recorder:** each macro has an ARM toggle in the menu. While RECORD is held, armed macro movements are recorded against the PHASE input as bar positions, for 1 to 16 bars. PLAY loops the gesture. **LOOSE** (0 to 1) adds seeded timing jitter to the playback position, up to ±1/16 bar.

### Scenes (16 slots)
- A scene stores: the 8 macro values, and optionally VAULT launch commands. Its menu sets "launch clip X on lane Y" for up to 8 lanes, and STEER sends these to a linked VAULT through the module-link mechanism below.
- Recall a scene with SCENE (knob 1 to 16) + GO (trigger), or with the SCENE CV.
  - The morph to the new macro values takes MORPH_TIME (0 to 32 bars, measured from PHASE, or in seconds if PHASE is not connected), using MORPH_CURVE.
  - VAULT launches happen at the next bar.

### Module link
- A LINK button followed by a click on a VAULT module stores that module's ID. Do the same to link Keepable generators.
- Links are saved in JSON by module ID. They are remapped on paste or duplicate using Rack's ID remapping hook, if one exists; otherwise links break, and that limitation is documented.

### Version tree (formerly BRANCH)
- **SNAP** (button/trigger) stores a node, which contains:
  - (a) the value of every parameter of every module in **scope**. Scope is set in the menu: all modules in the rack, Codex modules only, or modules with at least one mapping plus the linked modules.
  - (b) `captureState()` from the linked Keepable modules.
  
  The new node's parent is the current node.
- **Display:** the tree is drawn as nodes and edges. Click a node to make it the **target**.
- **MORPH:**
  - The MORPH knob (0 to 1) blends the current node's parameters toward the target node's parameters.
  - Continuous parameters are interpolated.
  - Snapped or switch parameters (`ParamQuantity::snapEnabled`, or integer ranges) change at a blend of 0.5.
  - Keepable states change at 0.5, at the next bar.
  - GO TO (trigger) fully commits the target over MORPH_TIME, and it becomes the current node.
- **WALK** (0 to 1): when above 0, every `WALK_BARS` bars (1 to 32), STEER moves to a neighbouring node (parent or child) with probability WALK, chosen with the seeded RNG. It morphs there over MORPH_TIME. The patch keeps wandering, but only among versions Haer has approved.
- **Performance:** write parameters from `process()` through ParamHandles or `module->params[i].setValue()` in the same way MIDI-Map does. Interpolate at most every 32 samples, and smooth the changes. Snapping a large rack (at most 2,000 parameters) must not cause audio glitches. Copy the values on the UI thread.
- Save nodes in patch storage as `tree.json`.

### Inputs
MACRO_1..8 CV, PHASE, SCENE CV, GO, SNAP, GOTO, MORPH CV, WALK CV, RECORD (gate), PLAY (gate), SEED CV.

### Outputs
MACRO_1..8 (0 to 10 V of the current macro value, so macros can also drive cables) and SCENE_CHANGED (trigger).

### Unit tests
- Curve mapping: each curve type is monotonic and hits its min and max at 0 and 1. The DSP part is testable without Rack.
- Tree operations: add a node, set its parent, serialise it to JSON, and parse it back so it matches.
- A walk with the same seed takes the same path.

### Haer's checklist
- Map macro 1 to WARP's WARP_DEPTH, MUTATE's AMOUNT and DEPTH's DIST_2. One knob should now tighten or loosen the whole piece.
- Make 3 snapshots that differ a lot, set them as targets, and turn MORPH: the patch should slide between them.
- Turn on WALK with 4 snapshots: the patch should wander between them over a few minutes.
- Set up scene 1 to launch two VAULT clips and set macros, then trigger GO: everything should change on the next bar.

---

## 13A. FLOW: fluid simulation as a modulation source (14 HP, amber)

**Purpose:** motion that is "controllable but beautiful", after Booth's interest in Navier-Stokes simulation. A small 2D fluid moves the modulation for the whole rack.

### Simulation
- Jos Stam's "Stable Fluids" on a **24×24 grid** (N = 24, plus a boundary): velocity fields u and v plus one dye-density field d.
- Each step runs: add sources → diffuse velocity (Gauss–Seidel, 10 iterations) → project → advect velocity → project → diffuse dye → advect dye → apply decay.
- Add **vorticity confinement** with strength VORT (0 to 1), so the fluid keeps its swirls.
- Run it on a **control-rate tick** of `RATE` steps per second (5 to 200, default 60). Use a sample counter in `process()`, with dt = 1/RATE × SPEED. Use double buffers. The grid is small, so running inside `process()` at control rate is acceptable. Measure the CPU: if a tick costs more than 0.5 ms, spread the iterations across samples.
- Outputs are smoothed between ticks with a 1-pole filter at 10 ms so they never step.

### Controls
VISCOSITY (0 to 0.01), DIFFUSION (0 to 0.01), VORT, DECAY (dye half-life 0.1 to 60 s), SPEED (0.1 to 4), RATE, WALLS (menu: closed box or wraparound torus), SEED (for the initial noise field), KEEP.

### Injectors ×4
- Each injector has inputs for X and Y (0 to 10 V across the grid), ANGLE (0 to 10 V = 0 to 360°), FORCE (0 to 10 V), DYE (0 to 10 V), and a PUFF trigger. PUFF adds a burst of force and dye at once, 50 times the continuous amount, spread with a Gaussian of radius 1.5 cells.
- The panel has 4 small XY pads, one per injector, for when nothing is patched. Drag to move an injector. Click to puff.

### Probes ×8
- Each probe has a position, set by dragging it on the display (saved in JSON) or by PROBE_X and PROBE_Y poly CV inputs with 8 channels.
- Each probe reads, by bilinear interpolation: dye density (0 to 10 V), u and v (±5 V, scaled by a GAIN knob), and the local curl (±5 V).

### Outputs
DENSITY_POLY, U_POLY, V_POLY and CURL_POLY (8 channels each), ENERGY (the total kinetic energy, 0 to 10 V, normalised to a soft maximum), and PEAK (a trigger when ENERGY crosses above a THRESH knob).

### Display
The dye field as a greyscale-to-amber image, with velocity arrows drawn every 3 cells, the injectors as circles, and the probes as numbered dots. Redraw at the UI frame rate from a copy that is safe to read.

### Keep state
The full u, v and d fields (24×24×3 floats, under 10 KB), probe positions and settings.

### Unit tests
- After `project()`, the divergence over the grid is below 1e-3.
- A single puff with VISCOSITY 0 and DECAY ∞ conserves total dye within 5% over 100 steps in a closed box.
- 10 minutes of random puffs give no NaN.
- Same seed and same inputs, same outputs.

### Haer's checklist
- Patch DENSITY_POLY channel 1 (through Fundamental Split) to STRIKE MATERIAL CV and channel 2 to DEPTH DIST_1. Patch MUTATE ACCENT → PUFF 1. Each accent should stir the fluid, and the sound should drift for seconds afterwards before settling.
- Raise VORT: the motion should become swirlier and last longer.
- Drag the probes about while it runs: the modulation should change character.

---

## 13B. EAR: listening and groove capture (12 HP, violet)

**Purpose:** gives modules a way to react to sound, not only to shared clocks. It also learns the micro-timing of any audio and turns it into a warp curve for WARP.

### Analysis (per input; 2 inputs, A and B)
- **Onset detection:** spectral flux on a 512-point FFT with a hop of 128, Hann window. Use Rack's `dsp::RealFFT` (PFFFT). Adaptive threshold = median of the last 16 flux values × SENS (1 to 4) + a floor. Minimum gap: 30 ms. Outputs: ONSET (trigger) and STRENGTH (sample-and-hold, 0 to 10 V).
- **Loudness:** RMS over 50 ms, in dB mapped to 0 to 10 V over -60 to 0 dBFS.
- **Centroid:** spectral centroid mapped as log2(f/20 Hz) on 1 V/oct-like scaling, clamped to 0 to 10 V.
- **Noisiness:** spectral flatness, 0 to 10 V.
- **Pitch:** YIN on a 2048-sample window. Outputs 1 V/oct and CONFIDENCE (0 to 10 V). Pitch is held when confidence < 0.5.
- All analysis runs on a ring buffer filled in `process()`, with the FFT/YIN done every hop. Keep it inside `process()` but amortised across samples. Do not use a background thread.

### Groove capture
- Takes the PHASE input (from WARP or anything else) and listens to the onsets on input A. LEARN_BARS sets the length: 1, 2, 4 or 8.
- While LEARN is held, every onset's phase position within the bar is recorded, along with its nearest grid position at GRID (8, 12, 16, 24 or 32 divisions).
- On release, it builds a **groove curve**: a monotonic spline through `(grid_pos, mean observed pos)` pairs, with (0,0) and (1,1) added and the result forced to be monotonic (sort, then clamp each point to at least 0.002 above the previous).
- Outputs:
  - GROOVE_PHASE: the input phase sent through the groove curve, 0 to 10 V. A sequencer clocked from it plays with the captured feel.
  - The curve's 6 to 16 points stored in JSON. A menu item "send groove to WARP" (via the module-link mechanism, like STEER's §13 links) copies it into WARP's *drawn* warp shape.
- GROOVE_AMT (0 to 1) blends between the straight phase and the grooved phase.

### Inputs
IN_A, IN_B, PHASE, LEARN (gate), SENS CV.

### Outputs (×2 sets for A and B where marked)
ONSET A/B, STRENGTH A/B, LOUD A/B, CENTROID A/B, NOISE A/B, PITCH A, CONF A, GROOVE_PHASE.

### Unit tests
- A click train at 120 BPM: ONSET fires once per click with jitter under 3 ms.
- A sine at 220 Hz: PITCH reads 220 Hz ±0.5% with CONFIDENCE > 0.9.
- Groove learning on a synthetic swung click track (every other 16th delayed by 30%) gives a curve that reproduces the delay within ±2% of a step.

### Haer's checklist
- Play a drum loop (any sample player) into IN_A, with WARP PHASE_1 → PHASE. Hold LEARN for 2 bars, then clock MUTATE from GROOVE_PHASE: MUTATE should pick up the loop's feel.
- Patch ONSET A from STRIKE's output into FLOW PUFF or CHAIN STEP: voices should start reacting to each other.

---

## 13C. TUNE MAP: harmonic fields that morph (10 HP, amber)

**Purpose:** harmony that changes without key changes. Any pitch CV is pulled into a "field" of allowed pitches, and that field morphs over time.

### Fields
- A field is a set of up to 24 pitches per octave, held as cents within the octave (0 to 1200) with weights. Fields are either octave-repeating or **non-repeating** across 4 octaves (for partial-series fields).
- Built-in fields (FIELD A and FIELD B selectors, 1 to 16):
  - 12-TET major, 12-TET minor
  - Pentatonic
  - 5-limit just major (ratios 1, 9/8, 5/4, 4/3, 3/2, 5/3, 15/8)
  - 7-limit "blue" (adds 7/4 and 7/6)
  - Harmonic series 8 to 16 (non-repeating)
  - Harmonic series 16 to 32 (non-repeating)
  - Bohlen–Pierce-ish 13 steps on 3:1 (non-octave; the menu notes this is approximate)
  - A slendro approximation: 5 roughly equal steps
  - Pelog approximation
  - 19-TET subset
  - "Cluster" (0, 50, 100, 150 cents)
  - Quarter-tone minor
  - Two USER slots, editable by clicking on a circular pitch display and saved in JSON
  - LEARNED (below)
- **LEARN FIELD:** while the LEARN gate is high, it takes the 8 strongest spectral peaks from the AUDIO input (FFT 4096, peak picking with parabolic interpolation, averaged over the gate time) and turns them into a field.

### Morph
- The MORPH knob and CV (0 to 1) crossfade between field A and field B. Each pitch of A is paired with its nearest pitch in B (a greedy nearest pairing; unpaired pitches fade their weight in or out).
- The morphed pitch = `lerp(cents_A, cents_B, MORPH)`. Its weight = `lerp(w_A, w_B, MORPH)`. Pitches with weight < 0.1 are not allowed as targets.
- This makes pitches glide continuously into the new tuning instead of jumping.

### Quantising
- 8 polyphonic channels: IN (1 V/oct) → OUT (1 V/oct). Each input snaps to the nearest allowed pitch of the morphed field.
- STRENGTH (0 to 1) blends between the raw input and the snapped pitch. At partial strength this gives "magnetic" pitch rather than hard quantising.
- HYST (0 to 50 cents) stops the output jittering between two targets.
- ROOT CV (1 V/oct) transposes the field. A CHANGED trigger output fires whenever a snapped target changes.

### Inputs
IN (poly), ROOT, MORPH CV, AUDIO (for learning), LEARN (gate), FIELD A CV, FIELD B CV.

### Outputs
OUT (poly), CHANGED (poly triggers), and FIELD (a poly output with up to 16 channels: the allowed pitches of the current field in the first octave above the root, as 1 V/oct values). FIELD can feed CHAIN's SCALE input, approximated to 12-TET by CHAIN, or drive chords.

### Display
A circular pitch display showing field A's and field B's dots, with the morphed positions and the current inputs as moving needles.

### Unit tests
- With MORPH 0, each output lies exactly on a field A pitch (±0.1 cent).
- Each paired pitch moves monotonically from its A value to its B value as MORPH goes from 0 to 1.
- Learning from a sum of sines at 220, 330 and 550 Hz gives a field with points at those pitch classes (±5 cents).

### Haer's checklist
- Put CHAIN's PITCH_POLY through TUNE MAP before FMX. Set field A to 12-TET minor and field B to Harmonic 16–32, and turn MORPH slowly: the melodies should bend into a just, glassy tuning without stopping.
- Feed a drone into AUDIO and hold LEARN: the melody should start following the drone's own partials.

---

## 14. Testing setup

- Use **doctest** (a single header, MIT licence, vendored in `tests/doctest.h`).
- In the `Makefile`, add a `test` target that builds `tests/*.cpp` + `src/dsp/*.cpp` + `src/common/*.cpp` with the host compiler (`-std=c++17 -O1 -g -fsanitize=address,undefined` on Linux/macOS) into `build/tests`, then runs it. If the cores include `rack::simd`, add `-I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include`, and provide stub definitions for any Rack symbols the linker needs. Otherwise keep the cores free of Rack headers.
- A shared helper `tests/util.hpp` provides: render N samples, Goertzel magnitude, T60 estimate, zero-crossing count, max abs, and a NaN check.
- Every module section above lists its required tests. Add more wherever a bug is fixed.
- Before every hand-off, also do a **smoke test inside Rack**: load each module, patch it as in its checklist, and leave it running for 10 minutes with no CPU spikes or NaN warnings in `log.txt`.

---

## 15. Milestones and hand-offs

Each milestone ends with: `make && make test && make dist` clean, a `CHANGELOG.md` entry, and a note to Haer listing what to try (copied from the relevant "Haer's checklist" sections).

| # | Contents | Notes |
|---|---|---|
| M0 | Scaffold, `plugin.json`, shared code (§3), test harness, one placeholder panel | No audible result. Hand-off: "the plugin loads in Rack". |
| M1 | **WARP** + **CONTOUR** | The time layer. Test with Scope and Fundamental drums. |
| M2 | **MUTATE** | First real beats. |
| M3 | **STRIKE** + **FMX** | Our own voices. |
| M4 | **CHAIN** | Melody. |
| M5 | **SPLICE** + **DEPTH** | Treatment and space. |
| M6 | **VAULT** + KEEP protocol on all generators | The generate-then-keep workflow. |
| M7 | **STEER** (macros, scenes, version tree) | Performance. |
| M8 | **FLOW** + **TUNE MAP** | Organic modulation and morphing harmony. |
| M9 | **EAR** + "send groove to WARP" link | The listening and feedback layer. |
| M10 | Polish: final panels, `docs/MANUAL.md`, `docs/PATCHES.md` recipes (§17), CPU pass | Ask Haer for the final plugin name and slug. |

---

## 16. Existing modules that work well alongside Codex

Haer wants Codex to use existing modules where they help, and to document why. None of these are required. Codex works with VCV Fundamental alone.

| Module (plugin) | Use it for | Why, instead of building it |
|---|---|---|
| **Scope, Mixer / VCA Mix, VCA, Merge, Split, Quantizer** (VCV Fundamental) | Checking phases, mixing voices, splitting the poly outputs from MUTATE and VAULT | Standard and free. Codex shouldn't duplicate utilities. |
| **MIDI-Map / MIDI-CC to CV** (VCV Core) | Putting STEER's macro CVs on a hardware controller | STEER takes CV, so any controller can drive it. Keeps hardware support out of Codex. |
| **Plaits / Rings / Elements** (Audible Instruments, ports of Mutable Instruments modules) | More voices: Plaits' FM and "percussion" models, Rings and Elements resonators beside STRIKE | Mature, very good-sounding physical models and FM. Good for variety, or as a reference when tuning STRIKE. |
| **Clouds / Beads-style granular** (Audible Instruments "Texture Synthesizer") | A second granular stage after SPLICE | Clouds is a different kind of granular (texture freezing), so it complements SPLICE's slice-led cut-ups. |
| **Plateau** (Valley) | A big, lush reverb send alongside DEPTH | DEPTH places sounds. Plateau gives a single huge space. Using both gives near-and-far layers. |
| **ChowTape / ChowDer** (ChowDSP) | Tape saturation and bass weight on the mix bus | Adds the analogue grit heard on Untilted and Quaristice. |
| **Surge XT modules** (Surge XT Rack) | FM and wavetable oscillators, waveshapers, and effects such as Nimbus granular and Chow distortion | Many high-quality building blocks if FMX needs company. |
| **8FACE mk2 / STRIP** (stoermelder PackOne) | Preset snapshots of non-Codex modules, saving and loading groups of modules | Complements STEER's version tree for modules outside its scope. |
| **Clocked / Foundry** (Impromptu Modular) | A conventional clock or a classic step sequencer to set against WARP and MUTATE | Gives a "straight" reference layer, so the warping is audible against a grid. |
| **Bogaudio modules** (e.g. Analyzer, Edge, LLFO) | Spectrum analysis while tuning STRIKE and FMX, plus extra utilities | Useful for checking that tuning and spectra are correct. |

Check the current plugin library at https://library.vcvrack.com for availability and exact names before writing them into `docs/PATCHES.md`, because plugins get renamed.

---

## 17. Starter patch recipes (write these into `docs/PATCHES.md`)

1. **"Bent electro"** (elseq / NTS-like)
   - WARP: 96 BPM, S-curve, depth 0.35, drift 6%.
   - WARP → MUTATE. Tracks: kick, snare, hat, perc.
   - MUTATE gates → STRIKE ×2 (wood, metal) and FMX (bass, algorithm 1).
   - MUTATE locks → STRIKE MATERIAL/POSITION.
   - Everything goes into DEPTH, with the kick at DIST 0.1 and the perc at 0.8.
   - STEER macro 1 = WARP_DEPTH + MUTATE AMOUNT. Macro 2 = DEPTH INSTABILITY.
2. **"Interference"** (Exai-like textures)
   - CONTOUR in MOIRÉ mode, SPREAD 0.015 → STRIKE (glass), with burst ×4.
   - COINCIDE → SPLICE STUTTER, with a FMX bell drone feeding into SPLICE.
   - SMEAR at 0.4.
3. **"Communicating voices"** (Oversteps / Quaristice melodic)
   - CHAIN with 3 voices clocked by CONTOUR TRIG channels 1 to 3. KEEP INTERVAL 0.8, HOME PULL 0.5.
   - CHAIN → FMX (poly), with FMX PHASE from WARP PHASE_3 at ratio 3/4.
   - Into DEPTH.
4. **"Keep then play"** (workflow demo)
   - Patch 1 → VAULT lanes. Run it and KEEP clips.
   - Then patch VAULT into the voices and drive the scenes from STEER.

---

## 18. Definition of done

- All 13 modules are built and pass their unit tests, and each has a finished panel, tooltips and right-click documentation.
- `docs/MANUAL.md` has one page per module: what it is for, every control, every port, and three tips.
- `docs/PATCHES.md` has the four recipes above.
- No CPU spikes, NaN outputs or crashes after 1 hour of the "Bent electro" patch with STEER WALK on.
- Haer has signed off on every module's checklist.
