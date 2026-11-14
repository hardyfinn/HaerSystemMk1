# HaerSystemMk1

A VCV Rack 2 plugin of modules for procedural, Autechre-influenced music: bending time, self-rewriting rhythm, physical and FM voices, and a generate, keep and perform workflow. Working name: **Codex**.

The design and build specification is in [`docs/design/`](docs/design/):

- `01-research-notes.md`: what is publicly known about Autechre's Max-based tools
- `02-module-proposal.md`: design history
- `03-build-instructions.md`: **the spec to implement from**

## Progress

The plugin is built in milestones (§15 of the spec). See [`CHANGELOG.md`](CHANGELOG.md) for what each one added and [`docs/DECISIONS.md`](docs/DECISIONS.md) for choices made where the spec was open.

| Milestone | Contents | State |
|---|---|---|
| M0 | Scaffold, shared code, tests | built |
| M1 | WARP + CONTOUR | built |
| M2 | MUTATE | built |
| M3 | STRIKE + FMX | built |
| M4 | CHAIN | built |
| M5 | SPLICE + DEPTH | built |
| M6 | VAULT + KEEP on all generators | built |
| M7 | STEER | built |
| M8 | FLOW + TUNE MAP | built |
| M9 | EAR | built |
| M10 | Polish, manual, patch recipes | built |

## Documents

- [`docs/MANUAL.md`](docs/MANUAL.md): one page per module.
- [`docs/PATCHES.md`](docs/PATCHES.md): five starter patches.
- [`docs/TESTS.md`](docs/TESTS.md): listening tests, one section per milestone.

## Installing a build

Mac (Apple Silicon) builds are made automatically. Download `Codex-2.x.x-mac-arm64.vcvplugin` from the [releases page](https://github.com/hardyfinn/HaerSystemMk1/releases) and drop it into Rack's plugin folder (in Rack: Help → Open user folder → `plugins-mac-arm64`), then restart Rack.

## Building from source

```
export RACK_DIR=/path/to/Rack-SDK   # Rack SDK 2.x
make            # build
make dist       # make the .vcvplugin
make install    # copy into Rack's plugin folder
make test       # unit tests; does not need the SDK
```

Panels are drawn by `python3 res/make_panels.py` (needs `pip install fonttools` and the Inter font).
