# Changelog

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
