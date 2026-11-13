# Changelog

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
