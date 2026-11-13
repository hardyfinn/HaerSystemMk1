# Decisions

Places where the build spec (`docs/design/03-build-instructions.md`) was ambiguous or had to bend, and what was chosen. Newest milestone last.

## M0: scaffold and shared code

1. **`plugin.json` lists only the modules that exist.** Rack refuses to load a plugin whose manifest names a module the code doesn't define, so the 13 slugs from §1.3 are added one milestone at a time. For M0 there is a single placeholder, `Blank`, which will be removed when the real modules are in.
2. **Label sizes follow Haer's panel standard, not the usual tiny Rack lettering:** 3.2 mm for control labels and 2.9 mm for jack labels. Panels get wider when needed (§1.4 allows this) and any change in HP will be noted here.
3. **Panels are drawn by a script** (`res/make_panels.py`, using fontTools to turn lettering into outlines) rather than by hand in Inkscape with a `components` layer and `helper.py` (§1.4). The result is the same kind of SVG; positions are kept in step between the script and each module's widget code. This matches how Haer's other plugins are built and keeps label sizes consistent.
4. **`SchmittIn` and `TriggerOut` are plain C++,** not wrappers around `rack::dsp` (§3.3). They follow the same rules (rise at 1 V, re-arm at 0.1 V, 1 ms pulses). This keeps `src/common/` free of Rack headers so the unit tests build without the SDK.
5. **`PhaseReader::reset()` adopts the ramp's current position.** §3.1 says reset sets `W = 0`. Read literally, a reset while the ramp is mid-bar would leave every step position offset from the ramp for the rest of the run. Instead reset returns to bar 0 and the next sample picks up the ramp's position inside that bar. On that first sample the step the ramp is standing in is reported once, so a sequence starts on its first step.
6. **More than 8 crossings in one sample:** the 8 most recent are kept (§3.1 only says "a maximum of 8").
7. **`PhaseReader::crossed(a, b)`** from §3.1 is provided as two calls: `crossed(k)` for step boundaries when a bar is cut into `k` steps, and `passed(x)` for a single position such as a micro-timed hit.
8. **Keep slots when all 8 are full:** a new KEEP drops the oldest and the rest move down (§3.5 does not say).
9. **Compiler standard:** the Rack SDK asks for C++11; the Makefile adds `-std=c++17` after it, as §0 requires C++17 for the DSP code.
10. **Builds:** the Rack SDK can't be downloaded in the environment the code is written in, so the mac build is made by GitHub Actions (`.github/workflows/build.yml`), which also runs the unit tests and fails on any compiler warning.
