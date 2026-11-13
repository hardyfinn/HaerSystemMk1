# Changelog

## 2.0.0, milestone M0 (2026-11-13)

Nothing to hear yet. This milestone is the foundation the 13 modules are built on.

- Plugin scaffold: `plugin.json`, Makefile, placeholder "Codex Blank" panel, mac build on GitHub Actions.
- Shared code in `src/common/`: `PhaseReader` (follows 0 to 10 V phase ramps forwards, backwards and at audio rate), `Curve` (ease, S-curve and a monotone spline), `Rng` (seeded, repeatable random numbers with independent streams), `SchmittIn` / `TriggerOut`, the KEEP protocol (`Keepable`, 8 keep slots, lock-free state hand-over) and the colour theme.
- `CurveDisplay` widget for drawing and dragging curves (used from M1).
- Unit tests: 21 test cases, run with `make test`.
