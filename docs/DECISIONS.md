# Decisions

Places where the build spec (`docs/design/03-build-instructions.md`) was ambiguous or had to bend, and what was chosen. Newest milestone last.

## M0: scaffold and shared code

1. **`plugin.json` lists only the modules that exist.** Rack refuses to load a plugin whose manifest names a module the code doesn't define, so the 13 slugs from §1.3 are added one milestone at a time. M0 had a single placeholder, `Blank`; it was removed in M1.
2. **Label sizes follow Haer's panel standard, not the usual tiny Rack lettering:** 3.2 mm for control labels and 2.9 mm for jack labels. Panels get wider when needed (§1.4 allows this) and any change in HP will be noted here.
3. **Panels are drawn by a script** (`res/make_panels.py`, using fontTools to turn lettering into outlines) rather than by hand in Inkscape with a `components` layer and `helper.py` (§1.4). The result is the same kind of SVG; positions are kept in step between the script and each module's widget code. This matches how Haer's other plugins are built and keeps label sizes consistent.
4. **`SchmittIn` and `TriggerOut` are plain C++,** not wrappers around `rack::dsp` (§3.3). They follow the same rules (rise at 1 V, re-arm at 0.1 V, 1 ms pulses). This keeps `src/common/` free of Rack headers so the unit tests build without the SDK.
5. **`PhaseReader::reset()` adopts the ramp's current position.** §3.1 says reset sets `W = 0`. Read literally, a reset while the ramp is mid-bar would leave every step position offset from the ramp for the rest of the run. Instead reset returns to bar 0 and the next sample picks up the ramp's position inside that bar. On that first sample the step the ramp is standing in is reported once, so a sequence starts on its first step.
6. **More than 8 crossings in one sample:** the 8 most recent are kept (§3.1 only says "a maximum of 8").
7. **`PhaseReader::crossed(a, b)`** from §3.1 is provided as two calls: `crossed(k)` for step boundaries when a bar is cut into `k` steps, and `passed(x)` for a single position such as a micro-timed hit.
8. **Keep slots when all 8 are full:** a new KEEP drops the oldest and the rest move down (§3.5 does not say).
9. **Compiler standard:** the Rack SDK asks for C++11; the Makefile adds `-std=c++17` after it, as §0 requires C++17 for the DSP code.
10. **Builds:** the Rack SDK can't be downloaded in the environment the code is written in, so the mac build is made by GitHub Actions (`.github/workflows/build.yml`), which also runs the unit tests and fails on any compiler warning.

## M1: WARP and CONTOUR

1. **Panel widths.** WARP is 24 HP (spec: 12) and CONTOUR is 28 HP (spec: 14). With 3.2 mm control labels and 2.9 mm jack labels, every control in the spec does not fit in less. The panel script now checks that no label touches another label or a control.
2. **KEEP and RECALL work now,** not at M6. Both modules have the KEEP button and jack, RECALL knob and jack, and 8 slot lights from §3.5, storing in their own 8 slots (saved with the patch). Leaving dead controls on the panel until M6 seemed worse. M6 adds VAULT and "KEEP ALL".
3. **RECALL at 0 does nothing.** §3.5 calls 0 "live". Slots 1 to 8 are recalled at the next bar line when the RECALL jack gets a trigger; the right-click menu can also recall a slot directly.
4. **A recall while WARP is stopped waits** until the clock runs and reaches a bar line.
5. **WARP fires BEAT as well as BAR on reset,** so the downbeat is never missing a beat trigger (§4 only mentions BAR).
6. **WARP drift** uses one seeded random value per PERIOD; the value for a given stretch depends only on the seed and where that stretch is, so the drift repeats exactly after a reset. Turning PERIOD while running re-lays the curve and the tempo can step.
7. **WARP drawn shape** has 6 movable points plus the fixed ends (§4 says "6 points"). Internally it accepts up to 14, ready for EAR's groove curves in M9. A point can't be dragged past its neighbours.
8. **WARP shape and depth CV scaling:** 5 V at SHAPE sweeps the whole 0 to 4 range; 5 V at DEPTH is full depth. Both add to the knob.
9. **CONTOUR has a MORPH knob** (range 0 to 4) as well as the MORPH jack. §6 lists only the CV, but Haer's checklist says "turn MORPH slowly".
10. **CONTOUR control names** on the panel: THRESH is `N_THRESH`, BUNCH is `SPREAD_T` (renamed so it isn't confused with the MOIRE SPREAD), CHANS is `CHANNELS`, COINC is `COINCIDE_N`, DIR is `DIRECTION`, DENS is the density CV. The three MOIRE-only knobs are lettered in amber.
11. **CONTOUR levels** sit at the centres of N equal bands, so one level is at 0 and two are at ±0.5. BUNCH bends them with a power curve.
12. **CONTOUR hysteresis** is a Schmitt trigger per level: a level fires when the curve has passed it by HYST, and can fire again only after the curve has come back past it by HYST.
13. **CONTOUR triggers at audio rate.** A fixed 1 ms trigger would stick high when hits come faster than 1 ms apart, so each pulse is shortened to half the gap since the previous hit, and a pulse that is still high dips for one sample before the next. Slow hits still get the standard 1 ms.
14. **The audio-rate unit test** checks what §6 intends: at 440 Hz with 4 levels there are 8 hits per cycle, and the hit pattern repeats twice per cycle, so its fundamental is 880 Hz (440 x 4 / 2) with nothing at 440 Hz. This is measured with a Goertzel filter rather than by counting zero crossings, which would count the 8 pulses.
15. **CONTOUR COUNT** holds the number of phasors in the most recent coincidence (it can rise while more arrive) instead of flickering back to 0 between them.
16. **CONTOUR MOIRE ratios, "seeded" set:** eight values between 1 and 8, rounded to quarters. MOIRE SPREAD runs 0 to 10 %.
17. **CONTOUR density CV:** ±5 V adds or removes up to 8 levels; the levels are then laid out evenly again.
18. **CONTOUR velocity "slope"** is scaled against the steepest slope the current curve can have, so a full-strength hit reads 10 V whatever the harmonic ratios are.
19. **CONTOUR screen:** in HARM mode the curve is drawn from the knob settings, so it updates even when the clock is stopped. In MOIRE and EXT modes it shows the curve as it was actually played over the last cycle.
20. **The unit test "PHASE_1 completes one cycle in 2.000 s"** and the others in §4 and §6 are all present; the 3/4-ratio test is taken after exactly 384,000 samples (4 bars at 120 BPM, 48 kHz).
