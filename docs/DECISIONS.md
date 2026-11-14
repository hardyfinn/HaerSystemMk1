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

## M2: MUTATE

1. **Panel width.** MUTATE is 38 HP (spec: 20). The 64-step grid, 12 knobs, 7 rule trimmers and 26 jacks with full-size labels need it.
2. **Kick generation.** §5 says the Euclidean kick pattern is "rotated by a seeded amount". A kick turned by an arbitrary number of steps rarely sounds like a beat, so the pattern starts on the downbeat 85 % of the time and otherwise sits on the off-beats. "Extra hits with probability density x 0.3" is applied once per group of four steps, on an off-beat step, not per step.
3. **Snare generation** assumes four beats to the bar (MUTATE does not know WARP's BEATS): backbeats land a quarter and three quarters of the way through each bar, rounded to a step. Ghost notes appear with chance density x 0.18 per empty step, and four in ten of them only play some of the time.
4. **Default tracks:** kick, snare, hat, perc, hat, perc, texture, free. Tracks 1 to 5 are 16 steps; tracks 6, 7 and 8 are 12, 20 and 14 steps so there is polymeter from the first press of GEN. A generated track is never left empty.
5. **GEN acts at once,** not at the next bar, so the result is audible while the clock is stopped. Mutations, HOME and recalled slots act only on bar lines. HOME SET acts at once.
6. **Rule details where §5 was open.** THIN/FILL: a coin toss between removing a hit (quiet ones first; never the last hit) and adding one on an empty step. RATCHET: a hit that already rolls goes back to a single hit half the time. MIRROR and COPY spans wrap round the end of the track. LOCK moves both locks of a run of steps by 0.1, each up or down.
7. **"Every Nth pass", "first pass only"** count passes of each track since the last reset. **"p %"** dice depend only on the seed, the track and the step number, so a run repeats exactly after a reset.
8. **Hits pulled early** (negative micro-timing, including the MICRO input) are started from the step before, as §5 asks. A step that has already been started this way is not changed by a mutation landing on the bar line it belongs to.
9. **MICRO input:** ±5 V shifts every hit by up to half a step. It is read when each step is scheduled.
10. **Density, amount and drift CVs:** ±5 V covers the whole range and adds to the knob.
11. **CLOCK mode** (nothing in PHASE): each pulse is one step. Between pulses the position glides at the speed of the previous pulse, so ratchets and micro-timing still work. The first pulse after a reset is step 1.
12. **Turning STEPS while running** renumbers the steps; MUTATE carries on from where it is rather than waiting for the old count.
13. **Triggers** use the same audio-rate-safe pulses as CONTOUR (M1 decision 13).
14. **Screen editing:** click a step to turn it on or off; drag up or down on a step to set its velocity; click past a track's end to lengthen it, or drag the bright end line; click a track label to mute; right-click a label for its style. Changing a track's style rewrites that track. Ratchets, micro-timing and conditions are shown but not edited by hand: §5 makes hand editing optional and Haer prefers steering to editing.
15. **KEEP stores** the playing pattern, the home pattern and the 16 musical knobs (STEPS to SWING).
16. **Not in the spec:** right-click, "Write this track afresh", which regenerates a single track from the current seed and density.

## M3: STRIKE and FMX

### STRIKE

1. **Panel width.** STRIKE is 20 HP (spec: 12) for 12 knobs and 13 jacks with full-size labels. It has no screen; the spec does not ask for one.
2. **Bar ratios after the fifth.** §8 gives 1, 2.756, 5.404, 8.933, 13.34 and then "(2n+1)² × 0.5513", which does not continue that series (it would jump to 66). The series is carried on with its own rule, (2k+3)² / 9.066, giving 18.64, 24.82 and so on.
3. **Glass ratios after the fifth** follow the same curve as the five in the spec: 0.91 × (k+1)^1.45.
4. **DECAY is the ring time of the lowest mode in metal.** Other materials ring for a fixed fraction of it (wood 12 %, rubber 5 %, glass 80 %, drum skin 35 %), which is what makes MATERIAL sound like a change of material and not only of tuning. Blends between materials are smooth.
5. **Resonators** are the "recursive oscillator" option of §8, in the rotating form whose loudness does not jump when the pitch or material is changed while it rings. They are written as plain loops the compiler turns into 4-at-a-time code, so the engine stays free of Rack and can be unit tested.
6. **Level.** Each hit is scaled so that sparse and dense materials come out about equally loud, and the output is rounded off smoothly above 6 V so it can never pass 10 V. Hard hits on drum skin reach that rounding; turn the velocity down for a cleaner tone.
7. **POSITION** never quite reaches either end (3 % to 97 % of the length), because at the very end the formula gives silence.
8. **Noise exciter length** (1 to 50 ms) has no knob in the spec, so it follows HARD: hard = short and bright, soft = long and dull. The noise is the same every hit, so hits are repeatable.
9. **Exciter loudness.** Click, mallet and noise are matched for the energy they put in. A soft mallet on a high pitch is very quiet: that is the physics (a pillow on a triangle), not a fault.
10. **External exciter.** With TRIG patched, each trigger opens a short window on the EXCITE audio (4 to 200 ms, set by HARD). With nothing in TRIG, the audio feeds the resonators all the time, so STRIKE can be used as a resonator effect.
11. **Choke on retrigger** fades the old ringing over 5 ms in a second set of resonators while the new hit starts at once in the first, so the new hit is not delayed or dulled.
12. **BURST** hits are all the same strength.
13. **CV scaling** (the spec gives none, and there are no attenuverters): MATERIAL 10 V = two materials; POSITION 10 V = half the length; DAMP and BRIGHT 10 V = full range; DECAY 2 V = double. Chosen so MUTATE's 0 to 10 V locks give "slightly different" hits as the checklist asks. Knobs and CVs are read 6000 times a second and afresh on every hit.
14. **Formant mode.** Vowel tables are the bass-voice tables from the Csound manual. The pulse is measured per cycle so all pitches are about equally loud. The voice sounds while TRIG is high, with a 5 ms attack and a release set by DECAY. HARD is the open quotient, INHARM the size, as §8 says.
15. **L and R** use a balance law: with SPREAD at 0 every voice is equally in both; at full, voice 1 is hard left and the last voice hard right.

### FMX

16. **Panel width.** FMX is 24 HP (spec: 14) for the 16 operator knobs, 7 main knobs, 17 jacks and the algorithm screen.
17. **Algorithm 5.** §9 writes it as "(4→3)+(2)→1", which is the same wiring as algorithm 6. So that there are eight different algorithms, 5 is: operator 4 feeds both 3 and 2, and those two feed 1.
18. **Level and ENV.** For an operator you hear, LEVEL is loudness; for one that modulates, LEVEL × INDEX is the depth in radians. ENV shapes both kinds with the tone curve.
19. **Feedback** at full is 2 radians.
20. **Nothing patched to TRIG or PHASE:** FMX sounds continuously, like an oscillator, so it can be tuned and used as a drone. Patch TRIG and it is silent until the first trigger.
21. **PHASE patched:** TRIG and TIME are ignored. Each time the ramp wraps counts as a hit (velocity is read, and phases reset if that option is on). A ramp that runs backwards plays the envelope backwards.
22. **Attack.** Loudness is smoothed over a fifth of a millisecond so the instant attack does not click; this is far shorter than one cycle of a 2 kHz retrigger.
23. **Pitch.** There is no pitch knob in the spec: 0 V is middle C and the V/OCT input does the rest. An octave switch would be handy for basses; not added because the spec does not have one. Say if you want it.
24. **Ratio CV with snap on** moves through whole numbers and halves, like the knob (and 0.25 at the very bottom).
25. **Labels.** AMP SHAPE and INDEX SHAPE are lettered AMP and TONE on the panel to fit the label size.
26. **OP 1 to 4 outputs** follow each operator's LEVEL and ENV but not the loudness envelope, so they keep running between notes.
27. **L and R** are limited at 12 V. Eight loud voices can reach that; lower the levels or use the POLY output into a mixer.
28. **CPU.** 2× oversampling with a 31-point half-band filter. Eight voices measured about 3 % of one core on the build server, so one to four voices fit the 1.5 % budget; a full eight-voice chord will be over it on a 2020 laptop.

## M4: CHAIN

1. **Panel width.** CHAIN is 34 HP (spec: 16): the table-and-history screen, 12 per-voice controls, 15 knobs and 23 jacks with full-size labels.
2. **Tables are counted from the root,** not from C. Turning ROOT transposes a melody's habits with it, and a learned melody can be played in any key.
3. **CHARACTER.** At 0: a bell curve one scale step wide, as §7 says; staying on the same note is allowed but made unlikely (weight 0.3) so lines keep moving. At 50 %: weights by distance in semitones, favouring thirds, fourths and fifths. At 100 %: favouring fourths, fifths and tritones, and up to a quarter of the notes are also thrown an octave away, which is where the wide leaps come from (a table of 12 notes cannot hold "an octave and a fifth").
4. **Which voice learns.** There is one LEARN jack and four voices: the voice selected on the screen learns. Its first lesson starts from an empty table; later lessons add to it. "Reset table" in the right-click menu goes back to CHARACTER. Where a learned table has never seen what follows a note, the CHARACTER table is used for that step.
5. **ORDER 2** is learned only, as §7 says, and stored as a full table per voice (144 × 12 numbers), not a sparse map; it is small enough and has no memory work on the audio thread.
6. **SCALE input.** "VCV Quantizer's convention" is not a pitch-cable format, so the input follows §13C instead: every pitch on the poly cable is an allowed note, in any octave, rounded to the nearest semitone. This is what TUNE MAP's FIELD output will send.
7. **Rules.** Each rule's dice are thrown once per step per voice. SCALE LOCK works by leaving out-of-scale notes out of the draw. NO PARALLELS looks at voices that stepped on the same sample and moved the same way. KEEP INTERVAL and CONTRARY do not apply to the reference voice itself. CONTRARY allows staying put.
8. **When eight tries all break a rule,** the module does not just keep the least bad of those eight: it looks at every note in the voice's range and takes the one that breaks least (then the one the table likes most, then the nearest). So a rule at 100 % is only ever broken when it truly cannot be met, and VIOLATION fires only then.
9. **HOME PULL** forces the root with a chance of strength × (steps since the root ÷ STEPS). The forced root still has to pass the other rules; if it cannot in any octave, they win.
10. **RESET** sends every voice to the root and rewinds the random streams, so the same melody plays again. It does not touch the tables.
11. **Gates.** §7 says gates are "marked changed when the note differs". Read as an option: right-click, "Gates only when the note changes", off by default. Gates shorten themselves at very fast step rates, like the other trigger outputs.
12. **GLIDE** takes the same time whatever the distance.
13. **Recalled slots** take over at the next step of any voice (CHAIN has no bar line). KEEP stores the tables, each voice's position and place in its random stream, the knobs and the right-click choices.
14. **Painting** on the screen: a dim cell is painted full and a bright one emptied; dragging carries on with the same. Finer weights come from learning. Painting a voice that follows CHARACTER first copies that table, so nothing jumps.
15. **Labels.** SCALE LOCK, NO PARALLELS, KEEP INTERVAL, CONTRARY and HOME PULL are lettered IN KEY, NO PAR, INTVL, CONTRA and HOME; LEAP and HOME_STEPS are SEMIS and STEPS next to the rule they belong to; STEP 1 to 4 are ST 1 to ST 4; LEARN_PITCH is PITCH; RECALL's jack is RECL.

## M5: SPLICE and DEPTH

### SPLICE

1. **Panel width.** SPLICE is 26 HP (spec: 12): the tape screen, 14 controls and 15 jacks with full-size labels.
2. **REC "gated".** The spec lists the switch position but no record-gate jack. Read as: on GATED the FREEZE jack becomes the record gate (high = record). On ALWAYS, FREEZE high stops recording, as §10 says. On OFF nothing is recorded and FREEZE does nothing.
3. **Recording goes on during a stutter.** The slice being repeated was taken before the stutter started and the tape is 16 s long, so the repeats are not disturbed. Hold FREEZE as well if the tape should stay as it is.
4. **What "the most recent slice" is.** The slice ends with the sample that arrived together with the STUTTER edge. Its length is rounded to whole samples so a plain repeat is an exact copy.
5. **Slice length from PHASE.** The bar length is measured from how fast the ramp is moving right now (averaged over 20 ms), then divided by DIV. With WARP bending its bars, slices taken in a fast part of the bar are shorter than those from a slow part. If the ramp stands still, TIME is used. The length is fixed when the stutter starts.
6. **HOLD** is a knob, 0 to 16 slice lengths, default 4. A stutter lasts as long as STUTTER is high, and at least HOLD slices; so MUTATE's 1 ms ACCENT trigger gives four slices' worth of repeats. At 0 only the gate counts.
7. **ACCEL** repeat lengths never go below 2 ms or above the slice. A repeat raised in pitch is also shorter (tape style), and the 2 ms floor applies after that too.
8. **REVERSE, DECAY, SEED.** SEED is not in the spec's list of controls but §3.4 says every random module has one; added as a knob. A new seed starts at the next stutter. The same seed gives the same pattern of reversed repeats and the same grain cloud.
9. **Crossfades** are 2 ms equal-power, as §10 says. Between two repeats of the same material that is a bump of up to 3 dB for 2 ms; it is not audible as a click.
10. **SCRUB** takes over the play position whenever a cable is in the jack (stutter or not): 0 V is the start of the slice, 10 V the end, smoothed over 3 ms. Without a stutter the slice is the newest one, just behind the record head.
11. **SMEAR.** Grains are read around the play position (or the scrub position), strayed by JITTER; without a stutter, around the newest slice, so SMEAR alone turns the live input into a cloud with a short delay. Grains play at normal pitch. The cloud is turned down as grains overlap so it stays about as loud as the source.
12. **Outputs** are limited at 12 V (clean below 10 V). REPEAT INDEX stops at 10 V (repeat 20).
13. **Save buffer with patch** is off by default. The tape is written as a 16-bit stereo WAV (±10 V = full scale, about 3 MB at 48 kHz), oldest sample first, into the patch's storage folder; the 16-bit rounding is far below what can be heard. A patch saved without the option removes the file. Changing Rack's sample rate keeps the tape (it is resampled).
14. **Mono.** A cable in IN L alone feeds both sides. Poly cables are summed.
15. **Labels.** STUTTER, REVERSE, DENSITY, PITCH_RAMP and REPEAT_INDEX are lettered STUT, REV, DENS, PITCH and RPT.

### DEPTH

16. **Panel width.** DEPTH is 24 HP (spec: 16): four source strips of five parts each, six room knobs and nine more jacks with full-size labels.
17. **Mono and unplugged sources.** A cable in either jack of a source feeds both sides. A source with no cable adds nothing; its last sound is allowed to finish in the room. Poly cables are summed.
18. **Air loss.** §11's wording gives the cutoff as 1.5 kHz + 16.5 kHz × (1 − DIST)², which is 18 kHz at DIST 0 and 1.5 kHz at DIST 1. At DIST 0 the filter is switched out completely.
19. **Early reflections** are taken from the source after its pre-delay, as a mono sum, at SIZE × the eight times in §11. Odd taps lean right and even taps left; PAN leans all of them. Their level rises from 10 % (close) to 100 % (far). Each later tap is a little quieter.
20. **PAN** places the direct sound (and leans the reflections). It is a balance control for stereo sources. The tail is shared and stays wide.
21. **Tail input.** Left and right sends go through their own four allpasses (2, 3, 5, 7 ms and 3, 5, 7, 11 ms) and then into alternate lines.
22. **Line lengths** are SIZE × 29, 37, 43, 53, 61, 71, 79, 89, 97, 103, 109, 127, 131, 139, 149 and 157 ms. Turning SIZE glides the lines to their new lengths (a tape-like pitch bend in the tail) instead of jumping.
23. **DECAY** uses §11's formula; measured decay times are within 2 % of the knob at SIZE 1. DAMP makes the top die faster than that, so the knob reads true for the low end. DAMP at 0 is no filter at all.
24. **MOD** speeds are fixed (not from a SEED knob; DEPTH has none), so a patch always sounds the same.
25. **INSTABILITY.** Gain × (1 + 0.08 × INSTABILITY), and pairs of lines are slowly turned into each other by up to 40°. The limiter inside each line is clean below 3 V and stops at 5 V, so ordinary reverb is untouched and a swelling tail levels off as a loud, dense wash. All four outputs are also limited to 12 V (clean below 9 V). With DECAY short, INSTABILITY only lengthens the tail; with INSTABILITY full it swells without input once DECAY is above roughly 8 s at SIZE 1.
26. **FREEZE** is a gate jack only (the spec has no button). While it is high nothing new enters the tail, damping, movement and instability are off, and the level holds (measured: 0.04 dB change in 10 s). The direct sound and reflections still pass, so you can play over a frozen tail.
27. **MIX** at 50 % is equal parts direct sound and room (equal-power). WET L and WET R ignore MIX.
28. **CV ranges.** DIST, INSTAB, MOD: 10 V = full range. SIZE and DECAY: 1 V doubles.
29. **CPU.** About 2.5 % of one core on the build server with four sources and MOD up, inside §11's 3 % budget.

## M6: VAULT

1. **Panel width.** VAULT is 40 HP (spec: 20): 47 jacks, 8 launch buttons, 8 knobs and the clip screen with full-size labels. Each lane is one column: record inputs at the top, launch in the middle, play outputs at the bottom.
2. **Which bars KEEP takes.** The whole bars before the bar you are in: press KEEP in bar 13 with BARS at 8 and the clip is bars 5 to 12. So a clip always starts and ends on bar lines and loops cleanly. Nothing waits for the current bar to finish.
3. **A lane plays its own lane of a clip.** Playback lane 3 plays what was recorded on lane 3 of the chosen clip. Different lanes can play different clips at once.
4. **Launch buttons.** The spec gives each lane a LAUNCH trigger; a button is added next to each jack so clips can be launched by hand. Double-clicking a full slot on the screen plays it on the chosen lane.
5. **CLIP, THIN and TRANS** show and set the lane chosen with the LANE knob (or by clicking a lane on the screen); turning LANE moves the three knobs to that lane's values. Their CV inputs act on the chosen lane too. TRANS CV is 1 V per octave.
6. **SEED** is a knob (§3.4), not in §12's list. THIN gives every note a fixed number from the seed; a note is left out when THIN is above its number. So raising THIN only ever removes more notes, and the same setting always gives the same sparse version.
7. **CV is also stored at the start of every bar,** besides §12's "when it has moved 1 mV, at most every 1/384 bar". That makes every clip stand on its own: a pitch that was set long before the clip is still right.
8. **A gate held across the start of a clip** starts again on the clip's first sample. When a clip wraps, all its gates drop for one sample, so a note that runs over the loop point is re-struck.
9. **A new note on a gate that is still high** makes the gate drop for one sample first, so the note is heard.
10. **REVERSE** plays note ends as note starts. **WINDOW** start and length are set in eighths and fractions from the lane's right-click menu.
11. **Without PHASE** VAULT runs its own bar count from TEMPO and "Beats in a bar" (right-click, default 4). Plugging or unplugging PHASE starts the bar count again and empties the rolling recording (kept clips are untouched).
12. **A stopped clock** means launches wait: they happen at a bar or beat line, and no line comes.
13. **Clip numbers.** KEEP uses the lowest free slot. When all 64 are full, KEEP does nothing and the screen says so.
14. **"Keep all"** is in the right-click menu. It stores the state of every Keepable module (WARP, CONTOUR, MUTATE, CHAIN, later FLOW) with the clip. "Recall linked states" in the clip's menu puts them back, at their next bar. If a module has been deleted since, it is skipped.
15. **VAULT's own kept state** (for STEER) is which clip each lane plays and the lane settings, not the clips themselves.
16. **clips.json** is written only when a clip changed, since it can be large. Each clip is also valid on its own as an exported file.
17. **Deleting or initialising** while a clip plays stops that lane.

## M7: STEER

1. **Panel width.** STEER is 38 HP (spec: 16): 8 macro strips, 8 more knobs, 14 buttons, 28 jacks and the tree screen with full-size labels.
2. **Mapping.** Press a macro's MAP button (its light comes on), then click any control. One click, one mapping; press MAP again for the next. Mapped controls get Rack's small coloured square, as with MIDI-Map. Ranges, curve and removal are in STEER's right-click menu under "Macro N", with sliders. Min and max are in per cent of the control's travel; min above max turns the control backwards.
3. **Macros write only when they move.** A mapped knob can still be turned by hand while its macro is still; the macro takes it back when it next moves. Macro movement is smoothed over 5 ms and sent 1,500 times a second.
4. **Without PHASE** STEER counts two seconds as a bar for gestures and the walk, and TIME is in seconds, as §13 says.
5. **Scenes** are stored from the right-click menu ("Store the 8 macros in scene N", N being the SCENE knob). GO on an empty scene does nothing. With a cable in SCENE CV the knob is ignored and every change of scene from the CV recalls it. MORPH_TIME is the TIME knob and MORPH_CURVE the CURVE switch (linear, slow start, fast start, S); they also time GO TO and the walk.
6. **VAULT launches** in a scene are set per lane: leave alone, stop, or a clip number. They go to the linked VAULT and happen at the next bar.
7. **Linking.** Press LINK, then click any control on the module (Rack has no way to notice a click on a bare panel). The right-click menu also lists every linkable module with a tick. A VAULT is linked both as the scene target and as a generator (its lane setup is part of a snapshot). Links are stored by module id; Rack gives no hook to renumber them when modules are copied and pasted, so links (not mappings) in a pasted copy point at the originals, as §13 allows.
8. **Gestures.** Arm macros in the menu. Hold REC (button or gate): armed macros are recorded until release, up to 16 bars, and the loop length is rounded up to whole bars. PLAY (a latching button, or a gate) loops every recorded gesture, armed or not; "Clear its recorded gesture" removes one. While a gesture plays, that macro's knob moves by itself. LOOSE shifts the play position by a smooth random amount that is different for each macro.
9. **What a snapshot holds.** The 8 macros always, every bounded parameter of the modules in scope (default: Codex modules; menu: every module, or only mapped and linked ones), and the state of each linked generator. STEER's other knobs are not part of a snapshot.
10. **Morphing is done on the screen thread, about 60 times a second,** not from the audio thread as §13 suggests. Reason: a snapshot can name 2,000 parameters in modules that may be deleted at any moment; from the screen thread that is safe, and it is as smooth as turning the knobs by mouse. The timing (TIME in bars) is still counted on the audio thread. Parameters that a macro drives are left out of the morph: the macro itself is morphed instead.
11. **MORPH knob.** It slides from the current version towards the target and back; nothing is committed. GO TO runs the same slide by itself over TIME and then the target becomes the current version. If MORPH is left turned up when a new target is clicked, the rack jumps to that blend at once: bring MORPH back to zero first. Switches and stepped knobs flip at 50 %, and generator states are handed over at 50 % (the generators apply them at their next bar).
12. **WALK.** Every BARS bars a seeded dice roll decides whether to move, and to which neighbour (the parent or a child, all equally likely). A decision that falls while a move is still under way is skipped.
13. **Screen.** Click a version to make it the target (click it again or click the current one to clear). Right-click a version to rename it, jump to it at once, or delete it (its children move up to its parent).
14. **Labels.** MORPH_TIME, MORPH_CURVE, WALK_BARS, RECORD, SCENE_CHANGED and MACRO outputs are lettered TIME, CURVE, BARS, REC, CHANGED and M1 to M8.
