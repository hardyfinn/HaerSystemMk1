# Patch recipes

Four starter patches (spec §17), plus one that uses the listening layer. They need only Codex and VCV Fundamental and Core, which come with Rack. Knob settings are starting points: move them.

Every patch starts the same way: **WARP** for time, and an **Audio** module (VCV Core) at the end. Set WARP's RUN on.

Where a recipe says "Split", use VCV Fundamental **Split** to take single channels out of a poly cable; "Merge" is Fundamental **Merge**, and "Mix" is Fundamental **Mix** (or **VCA Mix**).

---

## 1. Bent electro

Hard, bent drum programming with a plucked FM bass, in the spirit of elseq and the NTS sessions.

**Modules:** WARP, MUTATE, STRIKE (two), FMX, DEPTH, STEER, Split (two), Audio.

1. **WARP:** BPM 96, SHAPE 2 (S-curve), DEPTH 35 %, DRIFT 6 %.
2. **WARP 1 → MUTATE PHASE.** On MUTATE's screen, right-click the track labels and set track 1 to kick, track 2 to snare, track 3 to hat, track 4 to perc. Press GEN until you like the beat. Set AMT and DRIFT to about 30 %.
3. **Voices.**
   - MUTATE **1** (kick) → **FMX TRIG**. FMX: ALGO 1, RATIO 1 / 1 / 2 / 3, TONE well to the right, TIME about 250 ms. For a low note, send a steady -2 V into FMX V/OCT (for example from Fundamental **8vert**).
   - MUTATE **2** (snare) → **STRIKE A TRIG**, MATERIAL near the left (wood), DECAY short.
   - MUTATE **3** and **4** (hat and perc) → Merge → **STRIKE B TRIG**, MATERIAL around the middle (metal).
4. **Locks:** MUTATE **LOCK A** → Split; channel 2 → STRIKE A **MATER**. MUTATE **LOCK B** → Split; channel 4 → STRIKE B **POS**. Every hit now sounds slightly different.
5. **Space:** FMX L/R → DEPTH source 1 (DIST 10 %). STRIKE A L/R → source 2 (DIST 40 %). STRIKE B L/R → source 3 (DIST 80 %). DEPTH OUT L/R → Audio.
6. **STEER:** press MAP 1 and click WARP's DEPTH; press MAP 1 again and click MUTATE's AMT. Press MAP 2 and click DEPTH's INSTAB. Macro 1 now tightens or loosens the whole groove; macro 2 opens the room.

**Listen for:** the beat bending inside each bar and landing on the downbeat; the bass dry and close, the perc far back.

---

## 2. Interference

Slowly phasing glassy textures, like Exai.

**Modules:** WARP, CONTOUR, STRIKE, FMX, SPLICE, DEPTH, Audio.

1. **WARP 1 → CONTOUR PHASE.** CONTOUR: MODE in the middle (MOIRE), SPREAD 1.5 %, THRESH 6, COINC 4.
2. **CONTOUR TRIG → STRIKE TRIG.** STRIKE: MATERIAL about three quarters of the way up (glass), BURST 4, RATE about 30 Hz, DECAY medium.
3. **FMX as a bell drone:** nothing in TRIG or PHASE (so it sounds all the time), ALGO 3, operator 2 RATIO 3.5, operator 4 RATIO 7, INDEX low. FMX L/R → **SPLICE IN L/R**.
4. **CONTOUR COINC → SPLICE STUT.** SPLICE: WARP 2 → PHASE, DIV 1/8, HOLD 2, SMEAR 40 %, PITCH a little up.
5. STRIKE L/R → DEPTH source 1 (DIST 30 %), SPLICE L/R → source 2 (DIST 70 %). DEPTH → Audio.

**Listen for:** the glass pattern sliding out of phase and back over a minute or two; every time the phasors line up, the drone stutters and smears.

---

## 3. Communicating voices

Three melodic lines that listen to each other, like Oversteps and Quaristice.

**Modules:** WARP, CONTOUR, CHAIN, FMX, DEPTH, Split, Merge, Audio.

1. **WARP 1 → CONTOUR PHASE.** CONTOUR: HARM mode (switch up), CHANS 3, THRESH 6.
2. **CONTOUR TRIG → Split.** Channels 1, 2 and 3 → CHAIN **ST 1, ST 2, ST 3**.
3. **CHAIN:** INTVL 80 %, HOME 50 %, CHAR 1 low (around 20 %), CHAR 2 and 3 in the middle, CENTER 1 at -1. SCALE minor.
4. **CHAIN POLY → FMX V/OCT.** CHAIN GATE 1, 2 and 3 → Merge → **FMX VEL** (or leave VEL empty).
5. **WARP:** set RATIO 3 to 3/4. **WARP 3 → FMX PHASE.** Each FMX note now opens and closes on a 3/4 cycle against the bar.
6. FMX L/R → DEPTH source 1 (DIST 35 %). DEPTH → Audio.

**Listen for:** three lines in different rhythms that keep landing in thirds and sixths and keep coming home to the root.

**Add TUNE MAP:** put TUNE MAP between CHAIN POLY and FMX V/OCT. FIELD A 2 (12-TET minor), FIELD B 7 (harmonics 16-32), and turn MORPH very slowly.

---

## 4. Keep then play

The workflow: let the generators play, keep what is good, then perform with what you kept.

**Modules:** recipe 1 or 3, plus VAULT and STEER.

1. Build recipe 1. Add **VAULT**, and WARP 1 → **VAULT PHASE**.
2. Record: MUTATE **GATES → VAULT lane 1 GATE**, MUTATE **VEL → lane 1 CV**. CHAIN (or FMX's source) **GATE 1 → lane 2 GATE**, **PITCH 1 → lane 2 CV**; right-click VAULT, lane 2, tick "CV is pitch". Use Rack's stackable cables so the voices keep playing as well.
3. Let it run. Whenever you like what you hear, press **KEEP** (BARS at 8). Keep five or six clips.
4. Unpatch MUTATE from the voices. Patch **VAULT lane 1 GATE → Split → the voices' TRIG inputs** (channel 1 to FMX, channel 2 to STRIKE A, and so on) and lane 1 CV → Split → their VEL. Lane 2 GATE/CV → a melody voice.
5. Choose clips with LANE and CLIP and press LAUNCH, or double-click a slot.
6. **STEER:** press LINK and click any knob on VAULT. Set SCENE to 1; right-click STEER, "Scene 1", set lane 1 to clip 1 and lane 2 to clip 2, then "Store the 8 macros in scene 1". Do the same for scene 2 with other clips. Turn SCENE and press **GO**: everything changes on the next bar.

**Listen for:** the kept clips following WARP's current tempo and bending, not the tempo they were recorded at.

---

## 5. The rack listens (extra)

Voices that react to each other through sound, with a fluid between them.

**Modules:** recipe 1, plus EAR and FLOW.

1. **EAR:** STRIKE A's L → **IN A**, FMX's L → **IN B**. WARP 1 → **PHASE**.
2. **FLOW:** EAR **ONSET A → FLOW PUFF A**, EAR **ONSET B → PUFF C**. FLOW **DENSITY → Split**: channel 1 → STRIKE B **MATER**, channel 2 → DEPTH **DIST CV 3**.
3. Every snare hit now stirs the fluid, the fluid changes the metal voice and moves it in the room, and the sound drifts for seconds after each hit.
4. **Groove:** play any drum loop into EAR IN A instead (any sample player). Hold **LEARN** for 2 bars and let go. Patch EAR **GROOVE → MUTATE PHASE** in place of WARP 1: MUTATE takes on the loop's swing. Or right-click EAR, "Send groove to WARP", and set WARP's SHAPE to 4: the whole rack takes it on.
