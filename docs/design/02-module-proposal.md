# Module proposal (draft 1)

Working plugin name: **Codex** (placeholder, easy to change).

This is a design draft, not the build spec. The build instructions come after Haer answers the scope questions. Each module says where it comes from:

- **From the sources**: a direct response to something Autechre have said about their system (see `01-research-notes.md`).
- **Original**: my own idea, in the same spirit but not copying anything they've described.

## Design principles

1. **Time is a curve, not a pulse.** The modules share time as phase ramps (0–10 V sawtooth signals), not clock pulses. Any module can bend, fold or warp a ramp, and anything that reads ramps follows. This is the basis for Booth's "curves rather than discrete events".
2. **One idea per module.** Each sequencer is small and opinionated, like Brown's "hard-wired piece of notation". You get variety by combining and swapping modules, not from one huge sequencer.
3. **No hard line between event rate and audio rate.** Every generator runs both slowly and at audio rate, so a rhythm can be pushed until it becomes a timbre ("textures are sequences").
4. **Randomness is careful and repeatable.** Every random source has a seed. Making the same patch twice gives the same result unless you choose otherwise.
5. **Steer, don't play.** The performer moves macros, curves and scenes, not notes.
6. **Polyphonic throughout.** Voices are channels on one polyphonic cable, so voices can "listen" to each other.

## Signal conventions

| Signal | Range | Notes |
|---|---|---|
| Phase | 0–10 V ramp | One cycle is one bar (or one "unit"); polyphonic allowed |
| Gate/trigger | 0 / 10 V | 1 ms triggers |
| Velocity / accent | 0–10 V | |
| Pitch | 1 V/oct | |
| Bipolar CV | ±5 V | |

---

## A. Time and rhythm layer

### 1. WARP: curved master clock (from the sources)
A master clock whose tempo and inner timing are both curves.
- **Controls:** BPM; tempo drift (a slow curve of ±%); bar-length morph (continuous, e.g. 4.0 → 4.37 beats); **warp shape** (rubato curve inside each bar: ease-in, ease-out, S-curve, "drunk" spline, and user-drawn); warp depth; per-output phase offset and ratio (e.g. 3:4, 5:7, 1.01:1).
- **Inputs:** tempo CV, warp-depth CV, warp-shape CV (morphs between shapes), reset, run.
- **Outputs:** 4 phase outputs (each with its own ratio and warp), master tempo CV, bar trigger, beat triggers.
- **Why:** this gives the tempo that bends inside a bar. Shared phase also keeps everything locked while it bends.

### 2. CONTOUR: threshold-crossing rhythm generator (from the sources)
Hits fall where a continuous curve crosses thresholds, instead of on a step grid.
- **How it works:** it reads a phase input and draws a curve over one cycle: a sum of up to 4 shaped harmonics, or a drawn curve. N thresholds (1–16) are spread across its range, and every crossing fires a trigger. More thresholds give more hits, and a steeper curve packs hits more tightly. That is how you get rolls that speed up or slow down.
- **Controls:** curve harmonics (amplitude, ratio and phase ×4), threshold count, threshold spread (linear/exponential), hysteresis, accent source (slope, height or crossing index), polyphony (one channel per threshold band).
- **Inputs:** phase, curve morph CV, density CV, external curve input (any CV can become rhythm).
- **Outputs:** triggers (poly), velocity (poly), the curve itself, a "slope" CV.
- **Audio rate:** fed with a fast phase, it puts out pulse trains that make buzzing or granular timbres.

### 3. MOIRÉ: interference pattern generator (from the sources: Brown's "moire effects", plus original ideas)
A bank of 8 phasors running at almost-related rates. The music comes from how they beat against each other.
- **Controls:** base rate (or phase-locked to WARP), spread (how far the ratios drift from simple ones), a ratio set (harmonic, golden ratio, primes, user), coincidence window.
- **Outputs:** 8 phases (poly); sum, product and min/max combinations; **coincidence triggers** that fire when 2 or more phasors line up within the window, with a count output saying how many lined up; and a slow "interference envelope".
- **Why:** the patterns repeat over very long, readable cycles without any randomness. Turning spread from 0 upward turns a locked groove slowly into drift.

### 4. FLOW: small fluid simulation as a modulation source (from the sources: Booth's Navier-Stokes remark)
A 2D stable-fluids grid (about 24×24, after Jos Stam's method), run at control rate.
- **Controls:** viscosity, diffusion, vorticity, decay, a step-rate multiplier.
- **Inputs:** 4 "injectors", each with x, y, force angle, force amount and dye amount as CVs, plus a trigger that sends a puff.
- **Outputs:** 8 probe points, each giving density, x velocity and y velocity (poly), plus total energy.
- **Display:** the dye field, drawn live.
- **Why:** it gives motion that is controllable but organic. Triggers from CONTOUR can push the fluid, and its probes can modulate everything else.

## B. Pitch and harmony layer

### 5. CHAIN: Markov voices with conditional rules (from the sources: the Oversteps method)
Up to 4 voices. Each walks a Markov chain over scale degrees, and every voice can see the others' current notes.
- **Per voice:** a transition matrix (edited on a grid, or learned by sampling a 1 V/oct input over time), order 1 or 2, register range, and a step trigger input.
- **Conditionals** (rules from a fixed list, each with a strength knob from 0 to 100%):
  - force scale or chord set (from a scale input or a preset)
  - forbid parallel fifths/octaves against voice N
  - keep an interval class against voice N (e.g. always a 3rd or 6th)
  - contrary motion to voice N
  - max leap size
  - return-to-tonic pull, which rises with time since the last tonic
- A rule that fails resamples the step (up to K tries), then falls back to the nearest note that passes.
- **Inputs:** step triggers ×4, scale/root, a learn input, rule-strength CVs, seed.
- **Outputs:** pitch ×4 plus poly, "changed" gates, a rule-violation trigger (useful as an accent).

### 6. TUNE MAP: harmonic field and microtuning (original)
Turns any CV into pitches inside a harmonic field that changes over time: a scale, a just-intonation lattice, or a set of partials.
- A **field morph** CV moves smoothly between two fields, and notes glide to their nearest neighbours in the new field. This gives harmony that changes without a key change.
- The field can be learned from incoming audio (the strongest spectral peaks), so the harmony follows a drone.

## C. Sound layer

### 7. FMX: phase-driven FM voice (from the sources; FM is the fans' reading, not confirmed by the band)
A 4-operator FM voice, polyphonic up to 8 voices.
- 8 algorithms plus operator feedback. Each operator's ratio is a continuous knob with "snap to integer" as a toggle, so you can sit just off a ratio and get slow beating.
- **Index envelope:** not ADSR. It is a curve shape read from a phase input or a trigger, so timbre can follow WARP/CONTOUR curves exactly.
- **Retrigger at audio rate**, so a roll turns into a tone.
- **Inputs:** V/oct, trigger, phase, index CV, ratio CVs, velocity.
- **Output:** stereo, plus an individual output for each operator.

### 8. STRIKE: modal percussion with an exciter morph (from the sources: "wood and rubber" samples, the struck-sound palette)
A physically modelled percussion voice.
- **Exciter:** a choice of impulse, noise burst, mallet (raised cosine with hardness) or audio input. The exciter can also be retriggered in granular bursts.
- **Resonator:** a bank of 32 modes, with materials that morph from wood to rubber, metal, glass and membrane (sets of ratios and decays), plus size, damping and inharmonicity. A position control sets how strongly each mode is excited.
- **Formant/"glottal" mode:** the resonator becomes a vocal-tract-like formant filter driven by a glottal pulse train (a nod to Brown's "glottal" bass).
- **Inputs:** trigger, velocity, pitch, material CV, damping CV, external exciter.
- **Output:** stereo.

### 9. SPLICE: cut-up buffer (from the sources: Brown's cut-up patch)
A live buffer (up to 16 s) with stutter, slice and smear.
- **Slice timing comes from a phase input**, so stutters follow WARP's bent time.
- **Repeat spacing is a curve**, so a roll can speed up or slow down. There is also a reverse probability with a seed, a pitch-per-repeat ramp, and a smear control that crossfades into granular playback (grain size and density).
- **Inputs:** audio L/R, record gate, phase, stutter trigger, CVs for all of the above.
- **Outputs:** L/R, slice trigger.

### 10. DEPTH: spatial reverb and placement (from the sources: Booth's "deep mixing" and 3D sound stage, plus original ideas)
A reverb built around placing sounds in depth.
- **4 stereo inputs, each with a distance control** (and CV). Distance sets the pre-delay, the early-reflection pattern, high-frequency loss and the wet/dry balance, so turning one knob moves a sound back in the room.
- **The tail is a 16-line feedback delay network** with modulation, a size morph, freeze, and an **instability** control. Instability nudges the feedback matrix toward unstable, with a soft limiter, so the tail can bloom and smear.
- **Outputs:** stereo mix, and a separate wet output.

## D. Performance and system layer

### 11. STEER: macros, scenes and gesture memory (from the sources plus original ideas: steering the system live)
The performance hub.
- **8 macro knobs.** Each maps to up to 16 parameters on any module in the rack, picked by clicking a target, as VCV's MIDI-Map does. Each mapping has its own response curve and range.
- **16 scene slots** that store the macro values, with a morph time and a morph curve between scenes.
- **Gesture recorder:** records macro movement against WARP's phase, then plays it back, loops it, or plays it "loosely" with a timing-jitter seed.
- **Inputs:** a CV for each macro, scene select CV, morph trigger.
- **Outputs:** the macro values as CV.

### 12. BRANCH: version tree for the whole rack (original, after Booth's "save tons of versions")
Saves snapshots of every parameter in the rack (or of chosen modules) as a **tree** rather than a list.
- Take a snapshot whenever you like, branch from any one, and **morph between any two nodes** in the tree, crossfading every continuous parameter.
- A **"walk"** mode drifts slowly through the tree on its own, so the patch keeps moving between versions you have approved.
- The display draws the tree.
- **Why:** it builds Autechre's "nothing's frozen" way of working into the system. Jams become paths through versions you made.

### 13. EAR: listening and feedback (original)
Analyses audio and turns what it hears into control, so modules react to each other's sound and not only to shared clocks.
- **Inputs:** audio ×2.
- **Outputs:** onset triggers, onset strength, loudness, spectral centroid, noisiness, pitch (with confidence), and a **groove capture** that learns the micro-timing of onsets against a phase input and plays it back as a warp curve that WARP can use.

## How it fits together

```
WARP ──phase──► CONTOUR ──trig/vel──► STRIKE / FMX ──audio──► SPLICE ──► DEPTH ──► out
  │               ▲                       ▲                     ▲
  │            MOIRÉ (curves)          CHAIN (pitch) ◄── TUNE MAP
  │               ▲                       ▲
  └─────────── FLOW (modulation) ◄── CONTOUR triggers push the fluid
EAR listens to the mix ──► groove ──► WARP; onsets ──► CHAIN steps / FLOW puffs
STEER and BRANCH sit over all of it for live performance and versioning.
```

## Era focus (Haer, 2026-11-13)

Haer's focus is **Quaristice, Exai and elseq 1-5**. Haer also loves **Untilted** (2005) and singled out **'spl9'** from Exai. What that changes:

- **Untilted and Quaristice are drum-machine records.** Quaristice was made from jams with hardware and MIDI, which is documented. Fans say Untilted leaned heavily on an Elektron Machinedrum, but that is unconfirmed. Both point to step patterns that mutate a little on every pass, with per-step sound changes. That adds a new module, **MUTATE** (below).
- **elseq** is rhythm-first, with long tracks whose timing slowly deforms. That keeps WARP and CONTOUR at the centre.
- **'spl9'** is built on chopped and processed samples. Sean has named the sample source. That moves **SPLICE** into the core set.

### 14. MUTATE: step drum sequencer that rewrites itself (new; Untilted/Quaristice)
A 16-track step sequencer, each track with up to 64 steps. Each track can have its own length, so it handles polymeter.
- **Per-step parameter locks:** each step can store up to 4 CV values that go to the voice, such as pitch, decay or material. This is Elektron-style sound locking.
- **Step conditions:** probability, every Nth pass, the first pass only, and "only if the previous step fired".
- **Rewrite rules that run every N bars:** rotate, shift a hit by a micro-step, double a hit into a ratchet, thin, mirror, and copy one track's pattern into another. Each rule has a rate and depth knob, and the rules run from a seed so the results are repeatable. With a **drift** knob and a **home** button, the pattern can wander away and then come back, which is the slow mutation heard across an Untilted track.
- **Inputs:** a phase input from WARP, or a clock. Micro-timing for each step is read from a curve input, so it bends with WARP.
- **Outputs:** 16 gates (also as one poly output), 4 poly lock outputs, and an accent output.

## Workflow: generate, keep, perform (Haer, 2026-11-13)

Haer wants to **steer rather than micromanage**, and to **generate material first, save it, and perform with it later**. This matches how Autechre describe their own process: long jams cut down afterwards, and "enough material in the patches" to fill eight hours. The system is designed for two phases.

1. **Generate:** the generators run with very little input. Haer listens and presses **KEEP** whenever something is good.
2. **Perform:** the saved material is recalled, layered and morphed with macros. The generators can still run alongside it for variation.

What this changes:
- **New core module, VAULT** (below). It saves material as control data, not audio, so a kept pattern can be played through a different sound later.
- **BRANCH moves into the core set.** A kept moment saves the rack's parameter state as well as the event data.
- **Every generator** (MUTATE, CONTOUR, CHAIN, MOIRÉ) gets a **KEEP trigger input** and a **"freeze/recall" expander protocol**. The protocol lets VAULT or BRANCH capture and restore the generator's internal state: its pattern, Markov matrix, seed and step position.
- **STEER becomes performance-first:** its scenes can point to VAULT clips and BRANCH nodes, as well as macro values.

### 15. VAULT: material recorder and clip launcher (new; original, after "jams edited down")
Records the **control streams** that drive the voices, rather than audio.
- **Inputs:** 8 polyphonic lanes. Each lane takes a gate cable and a CV cable, which can carry pitch, velocity or locks. There is also a phase input for timing.
- **Rolling capture:** it is always recording the last 64 bars. Pressing **KEEP** saves the last N bars (1–64, set with a knob) as a clip. Clips stay with the patch file and can be exported as JSON.
- **Clip bank:** 64 slots, each with its name, length, original tempo and an optional BRANCH snapshot link.
- **Performance:** 8 playback lanes. Each lane plays one clip. Playback is **re-timed by the phase input**, so a kept clip follows WARP's current bending, not the curve it was recorded with. Each lane also has transpose, density thinning, reverse and loop-window controls.
- **Outputs:** 8 poly gate/CV lane pairs (to go into the voices), and an end-of-clip trigger for each lane.
- **Audio option (stretch):** also records stereo audio alongside each clip, to use for cut-ups in SPLICE.

## Final set: 10 modules (Haer, 2026-11-13)

Haer asked for **10 modules**. To fit melody and space into 10, two modules are merged into others.

| # | Module | Role | Absorbs |
|---|---|---|---|
| 1 | WARP | Curved master clock | EAR's groove capture becomes a later option |
| 2 | MUTATE | Step drums that rewrite themselves (Untilted/Quaristice) | |
| 3 | CONTOUR | Curve-crossing rhythms (elseq) | **MOIRÉ**: an 8-phasor interference curve source is built in |
| 4 | CHAIN | Markov melody voices with harmony rules (Exai/Quaristice) | |
| 5 | STRIKE | Modal/physical percussion | |
| 6 | FMX | Phase-driven FM voice | |
| 7 | SPLICE | Cut-up buffer and granular smear ('spl9') | |
| 8 | DEPTH | Placement-in-depth reverb ("deep mixing") | |
| 9 | VAULT | Rolling capture, KEEP, clip launcher | |
| 10 | STEER | Macros, scenes and gesture memory | **BRANCH**: the version tree and morphing between nodes go here |

**Parked for later:** FLOW, TUNE MAP, EAR.
