# Codex manual

One page per module will be added here as each milestone lands (final version at M10).

## Signals used by every module

| Signal | Range | Notes |
|---|---|---|
| Phase | 0 to 10 V ramp | One cycle is one bar. Modules follow it forwards, backwards and at audio rate. |
| Gate / trigger | 0 / 10 V | Triggers last 1 ms. Inputs fire at 1 V and re-arm below 0.1 V. |
| Velocity, accent, lock | 0 to 10 V | |
| Pitch | 1 V/oct, 0 V = C4 | |
| Bipolar CV | ±5 V | |
| Tempo CV | 0 V = 120 BPM, +1 V doubles | |

## WARP: curved master clock (24 HP)

**What it is for.** WARP is the time source for the whole system. Instead of clock pulses it sends *phase*: a 0 to 10 V ramp that rises once per bar. Its tempo can wander slowly, and the inside of each bar can bend, so beats bunch up and spread out while the downbeat stays put. Everything that follows a WARP phase bends with it.

**Screen.** Top: the warp curve for the current bar (straight diagonal = no bending) with a moving playhead. Middle: the tempo drift over the last 16 bars. Bottom: the tempo right now, then the ratios of outputs 1 to 4.

| Control | What it does |
|---|---|
| BPM | Tempo, 20 to 300. The TEMPO jack adds to it (+1 V doubles). |
| BEATS | Bar length, 1 to 16 beats. Fractions are allowed, e.g. 4.37. |
| DRIFT | How far the tempo wanders, 0 to 25 %. |
| PERIOD | Bars between the drift curve's turning points, 1 to 64. Bigger = slower wander. |
| SHAPE | The bend inside each bar. 0 ease-in (slow start, rush at the end), 1 ease-out, 2 S-curve, 3 drunk (a new random bend every bar), 4 drawn. In-between positions blend neighbours. |
| DEPTH | How much of the bend is applied. At 0 the bar is straight whatever SHAPE says. |
| SEED | Picks the drift curve and the drunk bends. Same seed = same result after a reset. A new seed starts at the next bar. |
| RATIO 1 to 4 | Cycles per bar for each phase output, from 1/16 to 8. Snaps to simple fractions; right-click the module to free an output. |
| OFFSET 1 to 4 | Shifts that output's phase by up to one cycle. |
| FOLLOW 1 to 4 | How much that output follows the bend: 100 % fully bent, 0 % straight. |
| RUN | Start and stop. Stopped, the phases hold where they are. |
| RESET | Back to the start of bar 1. |
| KEEP | Stores all the settings above in the next free slot (8 slots, saved with the patch). SLOTS lights show which are full. |
| RECALL | Choose slot 1 to 8; a trigger at the jack below it recalls that slot at the next bar line. |

**Inputs.** Under their buttons: RUN (gate), RESET, KEEP, RECALL (triggers). TEMPO (CV), SHAPE (±5 V sweeps all shapes), DEPTH (5 V = full), SEED (0 to 10 V).

**Outputs.** 1 to 4: the phase ramps. POLY: all four on one cable. BAR: trigger at each bar. BEAT: trigger on each beat, landing on the bent positions. TEMPO: the tempo now as CV (0 V = 120 BPM), for syncing other clocks.

**Drawing your own bend.** Turn SHAPE fully up. Six points appear on the screen; drag them.

**Three tips**
1. Give one drum voice FOLLOW 100 % and another FOLLOW 0 %, both at RATIO 1: one bends, one stays straight, and they meet on every downbeat.
2. Free an output's ratio (right-click) and set it just off 1, e.g. 1.01, for a part that slides slowly against the bar.
3. Patch TEMPO out to another clock's tempo CV (Clocked uses the same 0 V = 120 BPM rule) to drag a conventional sequencer along with the drift.

## CONTOUR: rhythm where a curve crosses levels (28 HP)

**What it is for.** CONTOUR draws a curve over each bar and lays horizontal levels across it. Every time the curve crosses a level, a trigger fires. Where the curve is steep the hits come fast; where it is flat they thin out. Change the curve and the rhythm reshapes smoothly instead of jumping between patterns.

**Screen.** The amber line is the curve over one cycle. Faint lines are the levels. Dots mark recent hits. In MOIRE mode eight small dials along the top show the phasors; a hand pointing straight up is at the start of its cycle.

| Control | What it does |
|---|---|
| MODE | Where the curve comes from. HARM: four sine harmonics. MOIRE: eight phasors at nearly related speeds. EXT: whatever is patched to EXT. |
| AMP / RATIO / PHASE 1 to 4 | HARM mode: each harmonic's amount, cycles per bar (0.25 to 16) and starting phase. |
| RATE | Speed when nothing is patched to PHASE, 0.01 Hz to 2 kHz. Fast enough to become a pitched buzz. |
| MORPH | Slides the harmonics against each other; the rhythm reshapes as you turn. |
| THRESH | Number of levels, 1 to 16. More levels = more hits. |
| BUNCH | Left: levels crowd round the middle (more hits near the centre of the curve). Right: toward top and bottom. |
| HYST | How far the curve must move away before the same level can fire again. Raise it if a noisy curve makes hits stutter. |
| CHANS | Poly channels. Levels are dealt out across the channels in turn. |
| DIR | Fire on crossings going UP, DOWN or BOTH. |
| SPREAD (amber) | MOIRE: detunes the phasors. 0 is a locked groove; around 2 % it slides slowly out of phase and back. |
| COINC (amber) | MOIRE: how many phasors must line up to fire the COINC output. |
| WINDOW (amber) | MOIRE: how close counts as lined up. |
| SEED | Picks the MOIRE detuning pattern. |
| KEEP / RECALL / SLOTS | As on WARP. |

**Inputs.** PHASE (from WARP), EXT (±5 V curve), RATE (1 V/oct), MORPH, DENS (±5 V adds or removes up to 8 levels), SPREAD, SEED, KEEP, RECALL.

**Outputs.** TRIG (poly triggers), VEL (poly, 0 to 10 V, held from each hit), CURVE (±5 V), SLOPE (±5 V, how steep the curve is), COINC (MOIRE trigger), COUNT (how many phasors met, 1.25 V each), PHASORS (MOIRE, 8 channels of 0 to 10 V ramps).

**Right-click.** What velocity follows (steepness, level height, or which level); snap harmonic ratios to whole numbers; the MOIRE ratio set (harmonic, golden ratio, primes, seeded).

**Three tips**
1. Start with AMP 1 up and the rest down: a single sine gives an even pulse. Bring in harmonic 2 or 3 and listen to hits bunch where the curve steepens.
2. Set CHANS to 4 and split TRIG to four drum voices: low levels play one voice, high levels another, so the curve's height becomes instrumentation.
3. Patch any LFO or envelope to EXT in EXT mode: every CV in your rack can be turned into rhythm.

## MUTATE: step drums that rewrite themselves (38 HP)

**What it is for.** MUTATE writes an 8-track drum pattern for you, then keeps changing it a little at a time. You steer how fast it changes and how far it may stray; you never have to enter a step. Each track can have its own length, so tracks slide against each other.

**Screen.** Eight rows, one per track, up to 64 steps. A lit step plays; brighter = louder. Small dots along the top of a step = a roll (one dot per hit). A tick at the bottom = the hit is early (left) or late (right). A cut-off corner = the step only plays sometimes. The white outline is the playhead. The bright line marks each track's end. The violet bar at the far right shows how far that track has drifted from home. The label at the left is the track number and its style (K kick, S snare, H hat, P perc, T texture, F free).

**Editing on the screen (all optional).** Click a step to turn it on or off. Drag up or down on a step for velocity. Click past the end of a track to make it longer, or drag the end line. Click a label to mute the track. Right-click a label to choose the track's style or have just that track written afresh.

| Control | What it does |
|---|---|
| STEPS | Steps per bar, 4 to 32. |
| DENS | How busy a generated pattern is. Press GEN to hear a change. |
| SEED | Same seed and density give the same pattern and the same mutations. |
| GEN | Writes a new pattern for all 8 tracks from their styles. It becomes the home pattern. |
| EVERY | Bars between rounds of changes, 1 to 16. |
| AMT | How many changes per round: 0 = none, full = four. |
| DRIFT | Where the changes head. 0: every change is a step back toward home. 50 %: it wanders around home without leaving. 100 %: it wanders off freely. |
| HOME | Snap back to the home pattern at the next bar. |
| SET | Make what is playing now the new home. |
| CURVE | Shape of rolls. Left: they speed up. Centre: even. Right: they slow down. |
| DECAY | How much each hit of a roll fades. |
| SWING | Delays every second step, up to half a step. |
| ROTATE, NUDGE, RATCH, THIN, MIRROR, COPY, LOCK | How often each kind of change is chosen. Turn one fully down to rule it out. ROTATE shifts a track by a step. NUDGE pushes a hit early or late. RATCH turns a hit into a roll or back. THIN removes or adds a hit. MIRROR reverses a short section. COPY borrows a section from another track. LOCK walks the sound locks of a few steps. |
| KEEP / RECALL / SLOTS | Stores the pattern, its home and the knobs in one of 8 slots; a trigger at RECALL brings back the chosen slot on the next bar. |

**Inputs.** PHASE (from WARP; one cycle = one bar). CLOCK (one pulse = one step, used when PHASE is empty). RESET. GEN, MUTATE (one extra round of changes at the next bar), HOME (triggers). DENS, AMT, DRIFT (±5 V). MICRO (±5 V shifts every hit by up to half a step: patch a slow curve for push and pull). SEED, KEEP, RECALL.

**Outputs.** 1 to 8: a trigger per track. GATES: all eight on one poly cable. VEL: velocity per track (poly, 0 to 10 V, held from each hit). LOCK A and LOCK B: two per-step values per track (poly, 0 to 10 V, held from each hit) for changing a voice's sound step by step. ACCENT: a trigger whenever any hit is louder than 80 %.

**Three tips**
1. Start with AMT at 50 % and DRIFT at 50 %: the beat keeps changing but stays itself. When it lands somewhere you love, press SET and that becomes the new centre it wanders around.
2. Patch LOCK A (through a Split) to a drum voice's tone or decay CV: because the locks follow smooth curves across the track, the sound changes in shapes, and the LOCK rule slowly reshapes them.
3. Turn ROTATE and COPY down and RATCH and NUDGE up for a beat whose hits stay put while its feel keeps shifting; do the opposite for patterns that slide and trade places.
