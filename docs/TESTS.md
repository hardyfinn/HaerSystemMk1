# Listening tests

One section per milestone. Each test says what to patch, what to do and what you should hear or see. Tick it when it does that; if it doesn't, write a few words next to it about what happened instead. The tests come from the "Haer's checklist" parts of the build spec, with the panel's own names, plus a few for things added along the way (marked *extra*).

**Before you start.** Install the latest `Codex-2.10.0-mac-arm64.vcvplugin` (drop it into Rack's `plugins-mac-arm64` folder and restart Rack). Every test needs an **Audio** module (VCV Core) for sound and usually a **WARP** for time, with RUN on. "Scope", "Split", "Merge", "Mix" and "LFO" are the VCV Fundamental modules of those names. "Right-click X" means right-click on module X's panel.

None of the screens could be seen while building (Rack can't run on the build server), so please also note anything on a screen that looks wrong, cramped or unreadable.

---

## M0: the plugin loads

- [ ] Rack starts without an error about Codex, and the module browser lists 13 modules under the brand **Haer**: WARP, CONTOUR, MUTATE, STRIKE, FMX, CHAIN, SPLICE, DEPTH, VAULT, STEER, FLOW, TUNE MAP, EAR.
- [ ] Hovering over any knob or jack shows a tooltip that says what it does.

## M1: WARP and CONTOUR (time)

**WARP**
- [ ] WARP **1** → Scope. You see a sawtooth once per bar. Set SHAPE to 2 (S-curve) and raise **DEPTH**: the ramp bends into an S, but each ramp still takes the same time.
- [ ] WARP **BEAT** → a drum voice (STRIKE is fine). DEPTH at 0: the beats are even. Raise DEPTH: beats bunch together, then spread out, within each bar, and always line up again on the downbeat.
- [ ] Set **DRIFT** to 10 %. Over about 30 seconds the tempo breathes slowly faster and slower.
- [ ] Same seed, same drift: note SEED, press **RESET**, listen to 8 bars; press RESET again: the same 8 bars of drift.
- [ ] *extra* SHAPE fully up (4, drawn): six points appear on the screen and can be dragged.

**CONTOUR**
- [ ] WARP **1** → CONTOUR **PHASE**, CONTOUR **TRIG** → a percussion voice. Turn **THRESH** from 1 to 8: the rhythm gets denser, bunching where the curve on the screen is steep.
- [ ] Turn **MORPH** slowly: the rhythm reshapes smoothly, without jumping to a different pattern.
- [ ] MODE to the middle (**MOIRE**). **SPREAD** at 0: a locked groove. Raise SPREAD to about 2 %: the groove slides slowly out of phase and comes back.
- [ ] Unpatch PHASE and turn **RATE** up into the audio range, TRIG into a mixer: a buzzy, pitched tone.

## M2: MUTATE (beats)

- [ ] WARP **1** → MUTATE **PHASE**. MUTATE **1 to 4** → four drum voices (STRIKE or Fundamental drums). Press **GEN**: a coherent beat.
- [ ] **AMT** 50 %, **DRIFT** 50 %. Over two minutes the beat keeps changing but stays recognisably itself. Press **HOME**: it snaps back at the next bar.
- [ ] Raise WARP's **DEPTH**: all the drums bend in time together.
- [ ] On MUTATE's screen, right-click a track's label (the letter at the left of its row), choose style **hat**, and turn **CURVE** fully left: the rolls on that track speed up.
- [ ] *extra* Click a step on the screen to turn it on or off; drag up or down on it to change its loudness.

## M3: STRIKE and FMX (voices)

**STRIKE**
- [ ] MUTATE **1** → STRIKE **TRIG**. Sweep **MATERIAL** slowly from left to right: woodblock, then rubber, then a struck metal bar, then glass, then a drum skin.
- [ ] MUTATE **LOCK A** → STRIKE **MATER**, MUTATE **LOCK B** → STRIKE **POS**. Each hit sounds slightly different, and the differences shift together over time.
- [ ] **BURST** 6, **RATE** about 40 Hz: each hit becomes a short rattle or roll.
- [ ] **MODE** to FORMANT, a held gate into TRIG, sweep MATERIAL: vowels (a, e, i, o, u).

**FMX**
- [ ] A bass: **ALGO** 1, **RATIO** 1 / 1 / 2 / 3, **TONE** well to the right, a trigger into **TRIG**. It sounds hard and plucked.
- [ ] WARP **2** → FMX **PHASE**: the timbre opens and closes once per cycle, and bends when you raise WARP's DEPTH.
- [ ] **FINE 2** to +3 cents: slow beating.
- [ ] CONTOUR running at audio rate (see M1) → FMX **TRIG**; right-click FMX and tick "Reset operator phases on trigger": grainy, fractured tones.

## M4: CHAIN (melody)

- [ ] MUTATE **3** → CHAIN **ST 1**, CHAIN **PITCH 1** → FMX **V/OCT** (and MUTATE 3 into FMX TRIG too). **CHAR 1** at 0: the line mostly moves by step. CHAR 1 full: it leaps.
- [ ] Clock voice 2 as well (another MUTATE output → **ST 2**, PITCH 2 → a second voice). **INTVL** full: the two lines hold thirds and sixths against each other while each moves on its own.
- [ ] **HOME** at 70 %: the melody keeps returning to the root.
- [ ] *extra* Click a cell in the table on CHAIN's screen: that move becomes likely (bright) or impossible (dark).

## M5: SPLICE and DEPTH (treatment and space)

**SPLICE**
- [ ] A breakbeat from any sample player (or STRIKE's output) → SPLICE **IN L**. MUTATE **ACCENT** → SPLICE **STUT**, WARP **1** → SPLICE **PHASE**. Accented beats chop and repeat.
- [ ] **ACCEL** to 0.6x (left of centre): the stutters speed up into a buzz.
- [ ] Slowly raise **SMEAR**: the chops dissolve into a granular cloud.

**DEPTH**
- [ ] Four voices into sources 1 to 4. **DIST** at 10 %, 40 %, 70 % and 95 %: a clear front-to-back layout, not just more or less reverb.
- [ ] A slow LFO into **DIST CV 1**: that sound walks away and comes back.
- [ ] A sustained sound in, **INSTAB** up: the tail swells and smears but never runs away.

## M6: VAULT (keep, then play)

- [ ] MUTATE **GATES** → VAULT lane 1 **GATE**, MUTATE **VEL** → lane 1 **CV**; CHAIN **GATE 1** → lane 2 GATE, CHAIN **PITCH 1** → lane 2 CV. WARP **1** → VAULT **PHASE**. Let it run a few minutes; press **KEEP** (BARS at 8) whenever you like something. Lit slots appear on the screen.
- [ ] Save the patch, quit Rack, open it again: the clips are still there.
- [ ] Unpatch the generators from the voices. VAULT lane 1 GATE → STRIKE TRIG (through Split for separate drums), lane 2 GATE/CV → FMX TRIG/V/OCT. Choose a clip with **LANE** and **CLIP**, press **LAUNCH**: the kept material plays, at WARP's tempo now, and bends with WARP's DEPTH.
- [ ] Raise **THIN**: the clip plays sparser versions of itself.
- [ ] *extra* KEEP on a generator: press KEEP on MUTATE, change the beat, then set RECALL to 1 and send a trigger to its RECALL jack: the kept beat comes back at the next bar. (WARP, CONTOUR, CHAIN and FLOW work the same way.)

## M7: STEER (performance)

- [ ] Press **MAP 1**, click WARP's DEPTH; press MAP 1 again, click MUTATE's AMT; again, click DEPTH's DIST 2. Turning **MACRO 1** now tightens or loosens the whole piece.
- [ ] Make 3 very different settings of the rack and press **SNAP** after each. Click a version on STEER's screen to make it the target and turn **MORPH**: the patch slides towards it.
- [ ] With 4 versions, turn **WALK** up to about 50 % (BARS 4, TIME 2): over a few minutes the patch wanders between them.
- [ ] Press **LINK** and click a knob on VAULT. Right-click STEER, "Scene 1": set two lanes to launch two clips, and "Store the 8 macros in scene 1". Move the macros, then press **GO**: the macros and the clips change on the next bar.

## M8: FLOW and TUNE MAP (motion and harmony)

**FLOW**
- [ ] FLOW **DENSITY** → Split: channel 1 → STRIKE **MATER**, channel 2 → DEPTH **DIST CV 1**. MUTATE **ACCENT** → FLOW **PUFF A**. Each accent stirs the fluid on the screen, and the sound drifts for seconds afterwards before settling.
- [ ] Raise **VORT**: the motion gets swirlier and lasts longer.
- [ ] Drag the numbered probe dots about on the screen while it runs: the modulation changes character.
- [ ] *extra* Drag the lettered injector circles: they move, and point the way you dragged. Click one without moving it: a puff.

**TUNE MAP**
- [ ] CHAIN **POLY** → TUNE MAP **IN**, TUNE MAP **OUT** → FMX **V/OCT**. **FIELD A** to 2 (12-TET minor), **FIELD B** to 7 (Harmonics 16-32). Turn **MORPH** slowly: the melodies bend into a just, glassy tuning without stopping.
- [ ] A drone (any oscillator chord, or a long FMX bell) → TUNE MAP **AUDIO**. Hold **LEARN** for a couple of seconds. Set FIELD B to 16 (Learned) and MORPH up: the melody follows the drone's own partials.
- [ ] *extra* Set FIELD A to 14 (USER 1) and click round the circle on the screen: points appear and disappear, and the melody follows.

## M9: EAR (listening)

- [ ] A drum loop from any sample player → EAR **IN A**, WARP **1** → EAR **PHASE**. Hold **LEARN** for 2 bars (BARS at 2) and let go: a violet curve appears on the screen. EAR **GROOVE** → MUTATE **PHASE** in place of WARP 1: MUTATE picks up the loop's feel.
- [ ] STRIKE's output → EAR **IN B** (or IN A), EAR **ONSET** → FLOW **PUFF** or CHAIN **ST 1**: the voices start reacting to each other.
- [ ] *extra* Right-click EAR, "Send groove to WARP". On WARP set SHAPE fully up (4) and DEPTH up: WARP's screen shows the groove, and everything on WARP's clock takes on the feel.
- [ ] *extra* A steady tone into IN A: EAR's screen shows its pitch in Hz, and **PITCH** tracks it as 1 V/oct.

## M10: the whole system

- [ ] Build each of the five recipes in `docs/PATCHES.md`. Each one makes the sound it describes.
- [ ] Run "Bent electro" for an hour with STEER's WALK on: no clicks, dropouts, silence or crashes, and Rack's CPU meter (F3) stays steady.
- [ ] The final plugin name and slug: tell me what Codex should be called before you start keeping patches you care about.
