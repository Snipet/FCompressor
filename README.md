# FCompressor

An all-in-one compressor plugin for macOS (AU, VST3 and Standalone; manufacturer `Funk`, plugin code `Fcmp`, bundle
id `com.funk.fcompressor`). Compressor types are called **Modes** — Clean, Bus G, FET 76, Opto 2A, Mu 67, Diode 609,
Bus 25 and Brickwall to start with, and more added continuously — and every Mode shares exactly
the same UI: the same parameters in the same places, with Modes free to lock a parameter or restrict it to hardware
steps. The display (transfer curve, operating point, gain-reduction history, internals) is computed by the same code the
audio thread runs, and the test suite proves it.

**Status:** Sprint 0 (bootstrap). The design is final (`docs/`); the code is being built sprint by sprint
(`docs/SPRINTS.md`). v1 ships arm64-only unless an x86 verify has passed (`docs/DECISIONS.md` ADR-47, Q6).

## Repository map

```
CMakeLists.txt  CMakePresets.json  cmake/   build: pins, glob → target map, plugin, self-registering probes
Source/fcdsp/      JUCE-free DSP library: core, params, engine, modes (Modes.def + one directory per Mode),
                   telemetry, analysis
Source/plugin/     JUCE processor, state, presets glue
Source/editor/     GPU-free panel and sub-views (+ gpu/ for the bgfx editor)
Tools/probes/      fcmp_probe_dsp and fcmp_probe_plugin: one self-registering probe per file
tests/golden/      blessed golden rows (base + per-arch overlays);  tests/fixtures/  write-once fixtures
Scripts/           deps.sh, verify.sh, validate.sh, golden.py, check-headers.sh, sprint/ownership.py, release.sh
docs/              ARCHITECTURE.md, DECISIONS.md, SPRINTS.md, design/, research/, sprints/, modes/
```

Related repositories: **FunkGui** (`/Users/seanfunk/audio/libraries/FunkGui`), the GUI library and test harness,
consumed through CMake FetchContent at a pinned tag; **HardwareReverb**
(`/Users/seanfunk/audio/plugins/HardwareReverb`), read-only, the source of FunkGui's seed.

## Building

Requirements: macOS 14+ on Apple Silicon, Xcode command-line tools (clang, C++20), CMake ≥ 3.30, Ninja, git.

1. **Once per machine:** `Scripts/deps.sh` populates the read-only cache `~/audio/.deps` (JUCE 8.0.4 and bgfx.cmake
   v1.153.9385-561 with verified SHAs, plus prebuilt `shaderc` and `pluginval`). Every build points FetchContent at it,
   so nothing is cloned per build directory (`docs/design/03-build-verify-process.md` §2.4–§2.5).
2. **Configure, build, test** with the presets (03 §2.10):

   ```sh
   cmake --workflow --preset dsp-verify                   # DSP library and its probes only; no JUCE (≈ 2 s configure)
   cmake --workflow --preset agent-verify                 # headless plugin + every probe, RelWithDebInfo
   Scripts/verify.sh build-agent                          # the pass/fail gate over the verify label
   cmake --preset owner && cmake --build --preset owner   # GPU editor, Release+LTO; installs the plugins
   ```

   Build directories are `build-<preset>` next to the sources (`build/` for `owner`). `Scripts/validate.sh` runs
   `auval -strict` and pluginval (strictness 10) on the installed bundles (03 §4.8). The CMake and scripts arrive with
   Sprint 0 (card B0); until then only `Scripts/deps.sh` exists.

## Documentation

- `docs/ARCHITECTURE.md` — the overview: modules, data flow, Modes, UI, threading, latency, state, verification.
- `docs/DECISIONS.md` — every decision (ADR-nn) and the user's answers (Qn).
- `docs/SPRINTS.md` — the executable plan, S0–S13: cards, ownership, acceptance, standard commands.
- `docs/design/01-core-contracts.md`, `02-funkgui-and-ui.md`, `03-build-verify-process.md` — the binding appendices;
  `K1–K3` the critiques they resolved; `docs/research/` the evidence.
- `docs/sprints/s<N>.md` — per-sprint task manifests and outcomes. `CLAUDE.md` — the rules for agent sessions.

## Licence

GPL-3.0 (`LICENSE`), as HardwareReverb.
