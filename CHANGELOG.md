# Changelog

## 2.10.0, milestone M10 (2026-11-14)

All 13 modules built.

- `docs/PATCHES.md`: the four starter patches (Bent electro, Interference, Communicating voices, Keep then play) and a fifth using EAR and FLOW.
- `docs/MANUAL.md` complete: one page per module and a list of other plugins that work well alongside.
- `docs/TESTS.md`: listening tests for every milestone, to tick off in Rack.
- CPU pass: every module measured (see `docs/DECISIONS.md`, M10).

## 2.9.0, milestone M9 (2026-11-14)

Listening.

- **EAR** (28 HP): listens to two audio inputs. For each: ONSET triggers with the STRENGTH of each hit, LOUDNESS, brightness (CENTROID) and NOISINESS; for input A also PITCH and how sure it is. Hold LEARN while a loop plays against WARP's phase and EAR learns where its hits fall against a 8 to 32 step grid. GROOVE puts that feel onto the phase, from straight to fully grooved; anything clocked from it plays with the loop's timing. "Send groove to WARP" copies the groove into WARP's drawn shape. Screen with the groove, the bar and onset lights.
- WARP accepts a groove from EAR.
- 4 new unit tests (133 in all).

## 2.8.0, milestone M8 (2026-11-14)

Motion and harmony.

- **FLOW** (30 HP): a small fluid (24 by 24 cells) as a modulation source. Four injectors push it and add dye, from CV or by dragging them on the screen; a click or a trigger puffs. Eight probes, dragged on the screen or moved by CV, read the dye, the speed across and down and the swirl where they sit, on four 8-channel outputs. ENERGY says how much the whole fluid is moving and PEAK fires when it rises past a threshold. Viscosity, diffusion, swirl strength, dye half-life, speed and step rate; closed box or wraparound. KEEP and RECALL store the fluid itself.
- **TUNE MAP** (18 HP): pulls pitch CV into one of 16 harmonic fields (12-TET major and minor, pentatonic, just, 7-limit blue, two harmonic-series fields, Bohlen-Pierce, slendro, pelog, 19-TET, a cluster, quarter-tone minor, two fields you click in yourself, and one learned from audio). MORPH glides every pitch of field A to its nearest partner in field B. STRENGTH from gentle pull to hard quantise, hysteresis, ROOT, a CHANGED trigger per channel, and the field itself as a poly cable.
- 13 new unit tests (129 in all).

## 2.7.0, milestone M7 (2026-11-14)

Performance.

- **STEER** (38 HP): eight macros, each moving up to 16 parameters anywhere in the rack, with a range and response curve per mapping (linear, exponential, logarithmic, S, steps). Macro CV inputs and outputs. Macro gestures can be recorded against the bar and looped, with LOOSE for timing drift. Sixteen scenes store the macros and can launch clips on a linked VAULT; changes take TIME bars with a chosen curve. A version tree stores whole-rack snapshots: click one as the target and MORPH slides the rack towards it, GO TO moves there, WALK wanders between neighbouring versions.
- 6 new unit tests (116 in all).

## 2.6.0, milestone M6 (2026-11-14)

Generate, keep, perform.

- **VAULT** (40 HP): always records the last 64 bars of gates and CV on 8 lanes, as events measured in bars. KEEP copies the last 1 to 64 bars into one of 64 clip slots. Eight playback lanes launch clips on the next bar or beat; playback follows the current PHASE, so a kept clip bends with WARP. Per lane: THIN, TRANSPOSE, reverse, and a window that loops part of the clip. Clips are saved with the patch, can be renamed, duplicated, exported and imported. "Keep all" also stores the state of every generator, and a clip can put them back.
- 11 new unit tests (110 in all).

## 2.5.0, milestone M5 (2026-11-14)

Treatment and space.

- **SPLICE** (26 HP): 16 seconds of stereo tape that is always recording. A gate or trigger at STUTTER repeats the slice that has just gone by; the slice is a division of WARP's bar, or a time. Repeats can speed up into a buzz or slow down, climb or fall in pitch, play backwards by chance and fade. SMEAR dissolves the chops into a cloud of up to 32 grains. SCRUB plays the slice by hand from a CV. Screen with the tape, record head, slice and grains. The tape can be saved with the patch.
- **DEPTH** (24 HP): a reverb for four stereo sources, each with its own distance. Distance sets how late, how dull and how quiet the direct sound is and how much of the room comes with it. One shared tail with size, decay, damping and movement; FREEZE holds it; INSTABILITY makes it swell and smear without running away. Separate room-only outputs.
- 17 new unit tests (99 in all).

## 2.4.0, milestone M4 (2026-11-14)

Melody.

- **CHAIN** (34 HP): up to four melody voices. Each has a table saying how likely every note is to follow every other; CHARACTER sets it from stepwise through thirds and fifths to wide leaps, or the voice can learn it from a melody you play in, or you can paint it on the screen. Six rules with a strength each: stay in the scale, no parallel fifths or octaves, keep thirds and sixths against a reference voice, contrary motion, a leap limit, and a pull back to the root. Ten scales or a scale from a poly cable, range and centre per voice, glide, a "two notes back" memory for learned melodies, a trigger when the rules cannot be met. Screen with the table and the last 32 notes of all voices. KEEP and RECALL.
- 12 new unit tests (82 in all).

## 2.3.0, milestone M3 (2026-11-13)

Our own voices.

- **STRIKE** (20 HP): struck percussion built from 32 tuned resonators per voice. MATERIAL morphs wood, rubber, metal, glass and drum skin. Four exciters (click, mallet, noise burst, external audio) that blend into each other, BURST for rattles and rolls, strike position, damping, brightness, inharmonic stretch, choke. A formant mode turns it into a held vowel voice. Up to 8 voices, stereo spread, poly out.
- **FMX** (24 HP): four-operator FM voice with 8 algorithms shown on its screen. One envelope position drives both loudness and brightness; it follows a phase ramp from WARP or CONTOUR when PHASE is patched, otherwise a trigger and TIME. Retriggers cleanly up to audio rate. Ratio snapping per operator, feedback, CV over ratios, index, feedback and algorithm. Up to 8 voices, plus each operator of voice 1 on its own jack.
- 21 new unit tests (70 in all).

## 2.2.0, milestone M2 (2026-11-13)

First real beats.

- **MUTATE** (38 HP): 8-track step drum sequencer. Writes its own patterns from a seed, a density and a style per track (kick, snare, hat, perc, texture, free). Seven mutation rules rewrite the pattern on bar lines; DRIFT sets whether changes wander off or return to a saved home pattern. Rolls with a speed-up or slow-down curve, micro-timing, swing, play conditions, two sound locks per step, different track lengths. Runs from WARP's phase or from an ordinary clock. Grid screen with optional hand editing. KEEP and RECALL.
- 13 new unit tests (49 in all).

## 2.1.0, milestone M1 (2026-11-13)

The time layer. First modules you can patch and hear.

- **WARP** (24 HP): master clock sending phase ramps. Tempo drift, five bar-bending shapes including a new random bend every bar and one you draw on the screen, four outputs with their own ratio, offset and follow, bar and beat triggers, tempo CV out.
- **CONTOUR** (28 HP): triggers where a curve crosses levels. Curve from four harmonics, from eight beating phasors (MOIRE, with coincidence triggers) or from any CV. Poly triggers and velocities. Runs from WARP's phase or from its own RATE up to audio rate.
- KEEP and RECALL with 8 slots on both modules.
- The placeholder "Codex Blank" is gone.
- 15 new unit tests (36 in all).

## 2.0.0, milestone M0 (2026-11-13)

Nothing to hear yet. This milestone is the foundation the 13 modules are built on.

- Plugin scaffold: `plugin.json`, Makefile, placeholder "Codex Blank" panel, mac build on GitHub Actions.
- Shared code in `src/common/`: `PhaseReader` (follows 0 to 10 V phase ramps forwards, backwards and at audio rate), `Curve` (ease, S-curve and a monotone spline), `Rng` (seeded, repeatable random numbers with independent streams), `SchmittIn` / `TriggerOut`, the KEEP protocol (`Keepable`, 8 keep slots, lock-free state hand-over) and the colour theme.
- `CurveDisplay` widget for drawing and dragging curves (used from M1).
- Unit tests: 21 test cases, run with `make test`.
