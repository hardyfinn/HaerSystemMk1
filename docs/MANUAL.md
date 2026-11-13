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

## Codex Blank

A placeholder panel from milestone 0. It does nothing; it is there to show the plugin loads.
