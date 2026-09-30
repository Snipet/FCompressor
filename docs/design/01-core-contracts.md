# 01: Core contracts (FCompressor, design appendix A)

Status: **design appendix, synthesised 2026-09-22 from Draft 1 and critiques K1 (consistency), K2 (risk) and K3
(parallel build). No code exists yet.** The top-level document is `docs/ARCHITECTURE.md`; every decision and its
rejected alternatives are logged in `docs/DECISIONS.md` (cited as `ADR-nn`). Where this appendix and 02/03 overlap, the
owner is: **01** = parameters, Mode descriptor, engine, telemetry, analysis, registry, state; **02** = FunkGui and the
editor; **03** = build, verification and process.

This document defines the interfaces that must be frozen before parallel implementation starts:
- the module boundaries;
- the universal host-parameter superset;
- the Mode descriptor and the one resolver;
- the engine and stage interfaces;
- telemetry, the analysis API, the Mode registry, and state and presets;
- the first eight Modes, written as descriptor tables.

The headers are written as near-final C++20. Bodies are left out unless the exact code is the contract. Every frozen
header must compile standalone (`Scripts/check-headers.sh`, CTest `lint.headers`, 03 §2.9), and every `constexpr`
helper is defined inline in its header (K3 #20).

Sources are cited as `<letter> §<section>`:
- A = `docs/research/A-gui-stack.md`
- B = `B-plugin-build.md`
- C = `C-verification.md`
- D = `D-mode-catalogue.md`
- E = `E-dsp-engine.md`
- F = `F-ui-ux.md`
- K1/K2/K3 = `docs/design/K{1,2,3}-*.md` (the critiques this revision resolves)
- HR = HardwareReverb, which is read-only. Its preset headers were read on 2026-09-22.

Tags:
- **[H]** marks a heuristic constant. It must be fitted or checked before the Mode claims fidelity (E's tag).
- **[DECIDED]** marks a point where the research reports disagree and this document picks one answer. §11 lists them;
  `docs/DECISIONS.md` carries the full record.

---

## 0. What freezes when

| Level | Contents | May change |
|---|---|---|
| **v1-forever** | Host parameter string IDs, plain ranges, normalised maps, defaults, `kApvtsOrder` (§3.2), the Mode slot table (`Modes.def`), `modeId` keys, the state XML layout, the preset payload format, `kStdLatency`/`kHqLatency` (§5.6), and — once a Mode is listed in `tests/fixtures/modes-ever.tsv` — that Mode's step `plain` values and `DisplayMap`s | Never after the v1 tag. The only allowed change is appending a parameter with a new version hint and a neutral default (HR pattern, B §1.2). |
| **Sprint-frozen** | `Pid`, `Setup.h`, `ParamSpec`/`Step`/`ModeDescriptor`, `resolve()` semantics, `EngineParams`, stage concepts (incl. `FbAffine` and, FZ0 errata, `LevelCtl` and `ScShapePolicy`), `core/Rt.h`'s `FCDSP_NONBLOCKING` rule (§2.2 rule 6), `IEngine`/`Carry`/`ControlIo`/`EngineTelemetry`/`AudioIo`, `TestTap`, `EngineHost` public API, `DefineMode.h`, `UiFrame`, `HistoryColumn`, the analysis signatures, `Source/plugin/ProcessorFacade.h` (02 §9.5) | Only through a lead-approved revision of this document between sprints, at the freeze points FZ0–FZ4 (03 §4.9). Agents never change them inside a sprint. |
| **Owner-local** | Stage policy internals, [H] constants, Mode `physical()` bodies, colour shapers | Freely, by the owning agent, covered by probes and goldens. **Exception (K2 #10):** once a Mode is in `modes-ever.tsv`, a change that moves its `dsp.print.<key>` default hash requires `ModeDescriptor::revision++` and an entry in `docs/modes/<key>.md` (§9.1). |

Before the v1 tag, v1-forever items can still change, because no user session exists yet. They are written here as if
they were final.

---

## 1. Decisions on one screen

1. **Three layers, one direction of dependency:** `fcdsp` (JUCE-free static library) ← `plugin` (JUCE processor, APVTS glue, state, presets) ← `editor` (JUCE + FunkGui).
   - `fcdsp` holds everything that decides a number: the parameter table, the resolver, the Mode registry, the engine, the oversampler, telemetry and analysis.
   - Every probe links `fcdsp`, so the probes test the same object code the plugin ships (C §5.14). The test tap is a runtime pointer (`EngineHost::setTap`, §5.4), never a compile flag.
2. **29 host parameters.** There are 22 Mode-filtered parameters and 7 globals (§3).
   - Ratio is stored as the slope `S = 1 − 1/R` ∈ [0, 2] (D §0.2, E §2.2).
   - `mode` is `AudioParameterInt 0..127`. It is non-automatable and append-only, and the state carries a stable `modeId` string (E §4.3). [DECIDED] 128 slots, not 64.
   - `Pid` order is internal; the host order is the separate, v1-forever `kApvtsOrder` (K2 #9).
3. **The raw value is truth, and snapping happens on read.** A Mode change never writes a parameter other than `mode`.
   - One pure function, `resolve(mode, raw) → {ParamView, EngineParams}`, serves the audio thread, the UI, the curve views, host `valueToText` and the probes. `RawParams` carries the configured lookahead budget, so `look` is clamped in one place (§4.4).
   - The UI snaps on write. [DECIDED] This confirms E §4.2 and overrules B §7.4.2; the reasons are in §4.5.
4. **A Mode is a descriptor plus a traits struct**, in its own directory `Source/fcdsp/modes/<key>/`.
   - The `ModeDescriptor` is a constexpr table: a `ParamSpec` per parameter, plus analytic and UI metadata.
   - The traits struct picks one policy per stage slot.
   - The two compile into `ModeEngine<Traits>`, which the host calls through **one virtual call per 64-sample chunk** (E §3.5–3.6). `FCDSP_DEFINE_MODE(Traits)` instantiates it in the Mode's own TU (§8.2).
   - Engines are placement-constructed into two preallocated 8 KiB arena slots.
5. **Kernel crossfade.** A change of Mode, or of a kernel-selecting value (`det`, `stmode`, `voice`, topology, or the effective external key), runs a **20 ms equal-gain crossfade** between two engine paths. Each path has its own frozen parameters and gain staging (`PathState`, §5.5). The new engine is seeded from the old one's `Carry` (E §5.1). Crossfade starts are at least 50 ms apart.
6. **Latency depends only on the global setup:** `latency = L_lookahead(budget, fs) + L_os(quality)`.
   - `quality` ∈ {ECO, STD, HQ} and `labudget` ∈ {OFF, 5 ms, 20 ms} are non-automatable globals, applied by the processor's message-thread `SetupWatcher` and in `prepareToPlay` (§5.6).
   - Latency never depends on the Mode (E §5.2).
7. **`fcdsp` owns its oversampler** (polyphase IIR 2× plus a Thiran fractional delay, FIR halfband 4×). [DECIDED] It does not use `juce::dsp::Oversampling`, which E §2.9 recommends, because `fcdsp` must stay JUCE-free.
   - Dry/wet mixing happens inside the OS domain (E §5.3). The dry path is never pre-gained.
8. **GR is positive dB of attenuation everywhere** in DSP, telemetry and analysis (E §2.1). The UI negates it for display.
9. **Telemetry has two channels.**
   - `UiFrame`: 72 words, a seqlock over atomic words (HR pattern), published per block.
   - `HistoryRing`: SPSC, one 32-byte column per 1 ms of audio time, 4096 deep, with min/max GR and a claim-word tear guard.
   - Both are gated by an editor-lifetime attach **count**.
10. **Analysis is pure and reuses the engine's own template code** (E §6): `staticGain`, `staticGr`, `localRatio`, `stepResponse`, `scResponse` and `colourCurve`. They serve both the always-visible band and the full-panel Characteristics screen. Every entry point opens `ScopedFtz`.
11. **`Modes.def` is the single Mode list.** The C++ registry and the CTest matrix derive from it (C §5.10). Sources are collected by per-directory globs, so adding a Mode edits no CMake and no shared C++ file (03 §2.1).
12. **No libm on the audio path, and no `UndoManager`.** Transcendentals come from `FastMath.h` (fma-only polynomials); the APVTS is built with a `nullptr` undo manager, and undo belongs to the host (K2 #7, #14).

---

## 2. Module boundaries

### 2.1 Canonical source tree, targets, namespaces

This is **the** FCompressor tree (K1 #1, K3 #1). 03 §1.1 refers to it; manifests' `OWNS`/`FROZEN` globs are written
against it.

```
FCompressor/
  CMakeLists.txt  CMakePresets.json  CLAUDE.md  README.md  LICENSE
  cmake/          FcmpArch.cmake FcmpDeps.cmake FcmpSources.cmake FcmpPlugin.cmake FcmpProbes.cmake FcmpProduct.h.in
  Source/
    fcdsp/                        STATIC lib `fcdsp`, namespace fcdsp. Depends on: std only (+ <arm_neon.h>/<immintrin.h>)
      core/       Simd.h FastMath.h Units.h ScopedFtz.h Smoother.h ControlTicker.h Sanitize.h
      params/     Pid.h Setup.h HostParams.{h,cpp} ParamSpec.h EngineParams.h Resolve.{h,cpp} Text.{h,cpp}
      engine/     Stage.h IEngine.h ModeEngine.h TestTap.h EngineHost.{h,cpp} Oversampler.{h,cpp}
        host/     ScFilter.h Router.h Delay.h Ramps.h Crossfade.h TelemetryAccum.h    (pure components, one owner each)
        stages/   detector/ gain/ link/ ballistics/ stage2/ colour/ scshape/ law/ combinators/
                  ONE policy per header, e.g. stages/ballistics/OptoCell.h, stages/colour/FetColour.h (no umbrella headers)
      modes/      ModeDescriptor.h ModeKit.h DefineMode.h Registry.{h,cpp} Modes.def
        <key>/    <Traits>.h (traits) <Traits>Desc.cpp (descriptor, physical(), specs) <Traits>.cpp (FCDSP_DEFINE_MODE)
                  e.g. modes/clean/Clean.h, modes/fet-76/Fet76Desc.cpp; a policy used by one Mode may live here
      telemetry/  Seqlock.h UiFrame.h HistoryRing.h
      analysis/   Analysis.{h,cpp}
    plugin/                       JUCE (namespace fcmp); compiled into FCompressor AND fcmp_probe_plugin
      Processor.{h,cpp} ProcessorFacade.h SetupWatcher.h ParamLayout.cpp HostText.cpp
      State.{h,cpp} StateMigration.cpp Presets.cpp factory/FactoryBank.cpp factory/<key>.inc
      CreateEditorGpu.cpp CreateEditorGeneric.cpp        (exactly one per target, chosen by CMake, no #if)
    editor/                       GPU-free UI (namespace fcmp::ui); JUCE + FunkGui::core
      Panel.{h,cpp} SubView.h Layout.h Tags.h SlotModel.{h,cpp} HistoryStore.h PreviewWorker.{h,cpp}
      views/      Header DisplayRow SlotGrid Band HistoryPlot TransferPlot MeterColumn CharScreen ControlPathPlot
                  StepPlot SidechainPlot ColourPlot Readouts ModeBrowser PresetStrip PresetBrowser Footer (.h/.cpp each)
      gpu/        Editor.{h,cpp}  (class fcmp::ui::Editor : public funkgui::EditorHost; GPU configuration only)
  Tools/
    probes/common/  ProbeMain.cpp ProbeRegistry.h Signals.h Measure.{h,cpp} Tolerances.h AllocCounter.cpp EngineRig.{h,cpp}
    probes/dsp/     <name>.cpp        one self-registering probe per file → test dsp.<name>[.<key>]
    probes/plugin/  <name>.cpp        proc.* and ui.* probes; FakeFacade.{h,cpp}
    bench/Bench.cpp
  tests/golden/{base,arm64,x86_64}/{global,modes/<key>}/<layer>.<name>.txt   (+ <layer>.<name>.<key>.lines sidecars)
  tests/fixtures/   state/*.bin  modeparam.tsv  modes-ever.tsv
  Scripts/          deps.sh verify.sh validate.sh golden.py gui-live.sh release.sh check-headers.sh sprint/ownership.py
  Resources/        FCompressor.entitlements
  docs/             ARCHITECTURE.md DECISIONS.md design/ research/ sprints/s<N>.md modes/<key>.md
```

Source collection is by per-directory `file(GLOB_RECURSE … CONFIGURE_DEPENDS)` rooted at `${PROJECT_SOURCE_DIR}/Source`
and `/Tools` (never `**` from the repo root, because `.claude/worktrees/` lives inside the checkout). The glob → target
map is 03 §2.1; it is written once in Sprint 0 and never edited again (K3 #2).

Targets (the full table, with link lines, is 03 §2.7):

| CMake target | Kind | Links | Notes |
|---|---|---|---|
| `fcmp_flags` | INTERFACE | – | ISA flags (`-mcpu=apple-m1` / `-mavx2 -mfma`), `-O3 -fno-math-errno -fno-trapping-math -ffp-contract=off`, and no `-ffast-math` (B §4, B §7.2, C §5.12). |
| `fcdsp` | STATIC | `fcmp_flags` (PUBLIC), `fcmp_warnings`, `fcmp_lto` | All of `Source/fcdsp`. |
| `FCompressor` | `juce_add_plugin` | `fcdsp`, `FunkGui::core`, `FunkGui::presets`, JUCE modules; `FunkGui::gpu` in the GPU configuration only | Manufacturer `Funk`, code `Fcmp`, `com.funk.fcompressor`, AU/VST3/Standalone (B §6.4). |
| `fcmp_probe_dsp` | console | `fcdsp`, `FunkGui::harness` | every `dsp.*` probe (C §5.14). |
| `fcmp_probe_plugin` | `juce_add_console_app` | `fcdsp`, plugin + editor sources (not `gpu/`), `FunkGui::core`, `FunkGui::presets`, `FunkGui::harness` | every `proc.*` and `ui.*` probe; C's `FcmpProcProbe` and `FcmpUiProbe` merged. |
| `fcmp_bench` | console | `fcdsp`, `FunkGui::harness` | not gating. |

- JUCE-dependent plugin sources are compiled into each consumer. They are never a static library, because JUCE modules would duplicate symbols (B §7.2).
- FunkGui comes in through `FetchContent_Declare(FunkGui GIT_REPOSITORY /Users/seanfunk/audio/libraries/FunkGui GIT_TAG <tag>)`, as the user decided (03 §2.3).

### 2.2 Dependency rules (checked by the `lint.deps` CTest, which greps `#include` lines under `Source/`)

1. Nothing under `Source/fcdsp` includes `<juce_*>`, `funkgui/*` or anything under `plugin/` or `editor/`.
2. `editor/` reads the processor only through `Source/plugin/ProcessorFacade.h` (02 §9.5). It never reaches `fcdsp::EngineHost` directly.
3. `plugin/` never includes `editor/` headers, except `CreateEditorGpu.cpp`, the one TU that constructs `fcmp::ui::Editor`. The facade lives in `plugin/` because the processor implements it (K1 #1).
4. **No libm on the audio path** (K2 #14). Under `Source/fcdsp/{core,engine,modes}` the lint rejects the regex
   `(^|[^A-Za-z0-9_])((std::)?(a?(sin|cos|tan)h?f?|atan2f?|exp(2|m1)?f?|log(2|10|1p)?f?|powf?|cbrtf?|hypotf?|erfc?f?|[lt]gammaf?))[ \t]*\(` (FZ0 errata: widened by the S0 review, R-B0 #4). Everything goes through
   `fcdsp::log2/exp2/tanh/logCosh/tanPi/sinPi/cosPi` (§5.1), so arm64 and x86 goldens can match exactly and macOS
   updates cannot move `print.*` hashes (E §0.9, C §5.12). `params/` (host maps) and `analysis/` (plot-only extras) are
   exempt; the analysis functions that must be bit-identical to the audio path call the policies, so they inherit the
   rule.
5. Nothing under `Source/fcdsp` has a function-local static, a lazily initialised global or a static constructor (C D12).
6. **Real-time annotation (FZ0 errata, review R-F0 #1).** `Source/fcdsp/core/Rt.h` defines `FCDSP_NONBLOCKING`
   (`[[clang::nonblocking]]` where supported, else empty); `Simd.h` and every header that uses it include it (it moved
   out of `IEngine.h`, so `core/` can use it without an upward include). Every declaration the audio thread reaches
   carries it: `EngineHost::process`/`reset` and its any-thread calls, **every** `IEngine` virtual including
   `~IEngine`, and each `ModeEngine` override, `ModeEngine::construct` and the `ModeEntry::construct` pointer type,
   the registry lookups, `Oversampler::reset/up/down`, `lookaheadSamples`, and the core helpers (`Simd` ops,
   `FastMath`, `lawFactor`/`alphaFromTau`, `ScopedFtz`, `sanitize`, `Smoother4`, `LinearRamp`, `ControlTicker`,
   `Seqlock`, `HistoryRing`). Virtual calls, calls through function pointers and calls to out-of-line definitions
   cannot be inferred, so those declarations must carry it; an out-of-line definition repeats it. Stage policies and
   Traits hooks are header-inline, so `-Wfunction-effects` (rtsan preset, `fcdsp` only) infers them; they may carry it
   explicitly. The macro sits after `noexcept` and before `override`, `= 0`, `= default` or the body.

`ProcessorFacade` (defined in 02 §9.5) exposes `port(Pid)`, `currentRaw()`, `readUiFrame`, `history()`,
`setUiAttached`, `uiState()`, `stateNotice()`, `beginBatch/endBatch` and `presets()`. The registry is reached through
the free functions of §8.2, not through the facade.

### 2.3 Threads and ownership

| Object | Written by | Read by | Rule |
|---|---|---|---|
| APVTS raw atomics | host threads, **including the audio thread** (JUCE's VST3 wrapper applies parameter changes inside `process()`, K2 #6), and the message thread (UI gestures, state load) | audio thread (relaxed poll once per block, HR B §1.1), host-text lambdas, UI | the processor registers **no parameter listeners** and no `AsyncUpdater` |
| `RawParams` snapshot | audio thread (per block), UI (per frame), host-text lambda (per call) | same thread | a value copy; `resolve()` is pure and thread-agnostic |
| `SetupWatcher` (`plugin/SetupWatcher.h`) | message thread, 20 Hz `juce::Timer` owned by the processor | – | applies `quality`/`labudget` (reconfigure + latency) and calls `updateHostDisplay` after a `mode` change (§5.6) |
| `EngineHost` | audio thread (`process`, `reset`); `configure` from `prepareToPlay` or from `SetupWatcher` under `suspendProcessing(true)` | – | `configure` is the only allocation point |
| Batch counter (`beginBatch/endBatch`) | message thread (state load, preset apply, `tapMany`) | audio thread | while > 0 the audio thread reuses the previous `BlockParams` (K2 #23) |
| `UiFrame` seqlock | audio thread, single writer | any reader | while the attach count is > 0 |
| `HistoryRing` | audio thread, single producer | the editor, single consumer | while the attach count is > 0 |
| `TestTap` pointer | probes only (`setTap`) | audio thread, once per block | the plugin never sets it |
| Analysis functions | message thread or `PreviewWorker` | – | may allocate; never called from the audio thread; each opens `ScopedFtz` |
| Registry and descriptors | constant-initialised statics | anyone | immutable, no lazy init (C D12 isolation) |

---

## 3. Universal host-parameter superset (v1-forever)

### 3.1 The table

"Map" is the host-normalised ↔ plain mapping, which is implemented once in `fcdsp::toPlain/toNorm` (§3.3). Every parameter is version hint 1. Future parameters are appended with hint ≥ 2 and a neutral default.

The "Unit / host text" column is what the **value text** carries. Every Mode-filtered parameter (#0–21) is created with
the JUCE label `""`, so AU and VST3 hosts do not append a fixed unit to a Mode's own scale ("INPUT 30 dB"); the unit is
part of the text (K2 #25a). Host **names** are universal forever; only the text relabels (K2 #25b).

| # | ID | Name | JUCE type | Plain range | Map | Default | Unit / host text | Autom. | Preset |
|---|---|---|---|---|---|---|---|---|---|
| 0 | `thr` | Threshold | Float | −60 … +24 dBFS | Linear | −18 | dB (Mode may relabel: INPUT, PEAK RED., AC THRESH) | yes | yes |
| 1 | `ratio` | Ratio | Float | S 0 … 2 | Ratio3 | 0.75 (4:1) | "4.0:1", "∞:1", "−2.0:1", or the step text | yes | yes |
| 2 | `knee` | Knee | Float | 0 … 72 dB | Power, centre 12 | 6 | dB | yes | yes |
| 3 | `range` | Range | Float | 0 … 60 dB (60 = OFF) | Linear | 60 | dB / "OFF" | yes | yes |
| 4 | `atk` | Attack | Float | 0.005 … 300 ms | Log | 10 | µs below 1 ms, else ms | yes | yes |
| 5 | `rel` | Release | Float | 1 … 30 000 ms | Log | 200 | ms below 1 s, else s | yes | yes |
| 6 | `tmode` | Time Mode | Int | 0 … 7 | Index | 0 | Mode step text (FET 76 relabels it `GR`: ON/OFF, §10.5) | yes | yes |
| 7 | `hold` | Hold | Float | 0 … 500 ms | Power, centre 50 | 0 | ms | yes | yes |
| 8 | `look` | Lookahead | Float | 0 … 20 ms | Linear | 0 | ms; the resolver clamps it to the configured budget and locks it at 0 while the budget is OFF (§4.4) | yes | yes |
| 9 | `det` | Detector | Int | 0 … 7 | Index | 0 | Mode step text | yes | yes |
| 10 | `schpf` | SC High-Pass | Float | 0 … 500 Hz (< 20 = OFF) | Power, centre 80 | 0 | Hz / "OFF" | yes | yes |
| 11 | `sce` | SC Emphasis | Float | −6 … +6 | Linear | 0 | dB/oct, or Mode step text | yes | yes |
| 12 | `link` | Stereo Link | Float | 0 … 1 | Linear | 1 | % | yes | yes |
| 13 | `stmode` | Stereo Mode | Int | 0 … 7 | Index | 0 | STEREO, M/S, MID, SIDE, M>S, S>M (6/7 reserved) | yes | yes |
| 14 | `voice` | Voice | Int | 0 … 7 | Index | 0 | Mode step text | yes | yes |
| 15 | `drive` | Drive | Float | −24 … +24 dB | Linear | 0 | dB | yes | yes |
| 16 | `makeup` | Makeup | Float | −24 … +36 dB | Linear | 0 | dB (Mode may relabel: OUTPUT, GAIN, CEILING) | yes | yes |
| 17 | `automu` | Auto Makeup | Bool | 0/1 | Bool | 0 | OFF/ON | yes | yes |
| 18 | `mix` | Mix | Float | 0 … 2 | Linear | 1 | % (0–200) | yes | yes |
| 19 | `s2thr` | Stage 2 Threshold | Float | −40 … +24 dBFS (+24 = OFF) | Linear | 24 | dB / "OFF" | yes | yes |
| 20 | `s2atk` | Stage 2 Attack | Float | 0.005 … 300 ms | Log | 1 | as `atk` | yes | yes |
| 21 | `s2rel` | Stage 2 Release | Float | 1 … 30 000 ms | Log | 100 | as `rel` | yes | yes |
| 22 | `mode` | Mode | Int | 0 … 127 | Index | 0 (clean) | Mode name, "—" when unassigned | **no** | via `modeId` |
| 23 | `extkey` | External Key | Bool | | | 0 | OFF/EXT | **no** | no |
| 24 | `listen` | SC Listen | Bool | | | 0 | | **no** | no |
| 25 | `delta` | Delta | Bool | | | 0 | | **no** | no |
| 26 | `bypass` | Bypass | Bool | | | 0 | (`getBypassParameter`) | host | no |
| 27 | `quality` | Quality | Choice | ECO, STD, HQ | Index | STD | | **no** | no |
| 28 | `labudget` | Lookahead Budget | Choice | OFF, 5 MS, 20 MS | Index | OFF | | **no** | no |
| 29 | `output` | Output | Float | −24 … +24 dB | Linear | 0 | dB, one decimal (`fcdsp::formatOutput`); version hint 2 (v1.2, ADR-88) | yes | no |

**Rules.**
- The **APVTS layout order is `kApvtsOrder`** (§3.2), which equals this table's order for v1 and is frozen forever; later parameters are appended to it. `Pid` order is internal and may regroup (K2 #9). The IDs, not the order, are the automation identity: VST3 and AU parameter IDs are hashes of the string IDs, and state is keyed by ID.
- **Monitoring and setup parameters are not automatable and never go into presets:** `listen`, `delta`, `extkey`, `quality`, `labudget`. A monitoring latch baked into a bounce is always a mistake. A preset must not change latency or depend on a routed key. `listen` and `delta` are also reset to 0 by `setStateInformation`, so a session never reopens monitoring the side chain (K2 #25c).
- **`quality` and `labudget` change latency.** They are applied in two places and nowhere else (K2 #6):
  1. `prepareToPlay` reads both raw atomics, calls `EngineHost::configure`, then `setLatencySamples`, on whatever thread the host calls it from;
  2. `SetupWatcher`, a 20 Hz message-thread `juce::Timer` owned by the processor, compares both raw values with the configured ones and, on a difference, runs `suspendProcessing(true)` → `EngineHost::configure` → `setLatencySamples(EngineHost::latencyFor(cfg))` → `suspendProcessing(false)`.

  The processor has no APVTS listeners and no `AsyncUpdater`: JUCE's VST3 wrapper fires listeners on the audio thread, and `triggerAsyncUpdate` may block there. A Quality or Lookahead change is documented as "one block of silence plus a PDC change". The audio thread keeps running the configured values until then (E §5.2, E §10.7).
- **The processor exposes no host programs:** `getNumPrograms() == 1`, as HR does today (B §1.7). JUCE's VST3 wrapper would otherwise add a "Program" parameter whose step count follows the bank size (K2 #25d). The factory bank lives in FCompressor's own browser (§9.2).
- **Integer lists hold capacity 8,** so `getNumSteps()` never varies per Mode (E §4.3). A Mode with n < 8 entries snaps indices ≥ n to its last entry.
  - Convention: **index 0 is the Mode's reference setting**, so an Init session lands on the reference voice, time mode and detector.

**Differences from D §4, each [DECIDED]:**
- **D's `in` is replaced by two mechanisms.**
  - Units whose Input drives a fixed threshold (1176, Distressor, TG1) **remap `thr`**, so `thr` always means "effective threshold referred to the plugin input". This carries the amount of compression across Modes (F §3.9).
  - A separate **`drive`** is level into the colour stage only. It is level-compensated and never moves the threshold.
- **D's `topo` is not a host parameter.** Only switchable units expose topology (API New/Old, alpha, Pump). They do it through their `voice` list, and `physical()` sets `EngineParams::topo`. This saves a UI slot that would be n/a in 6 of the first 8 Modes.
- **`lshape` (API link shape) and the global Output trim are omitted from v1.** Both are candidate appends (§12).
- **No reference-level parameter.** Calibration is fixed at `kDbuAt0dBFS = 22.0f` (0 VU = +4 dBu = −18 dBFS, D §2, Waves convention [V S5]).
- **`det` is a Mode-defined list** (D §4). F §3.1 proposed a continuous "RMS window 0 = PEAK" instead. The list is chosen because TRUE PEAK, T4 CELL and TUBE are not window lengths.

### 3.2 `Pid` (Source/fcdsp/params/Pid.h)

```cpp
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace fcdsp {

// Internal order: the Mode-filtered block first (ModeDescriptor covers exactly it), then the globals.
// Sprint-frozen, free to regroup between sprints; never persisted and never sent to a host (K2 #9).
enum class Pid : uint8_t {
    thr, ratio, knee, range, atk, rel, tmode, hold, look, det,
    schpf, sce, link, stmode, voice, drive, makeup, automu, mix,
    s2thr, s2atk, s2rel,
    kModeCount,                       // 22: the Mode-filtered set (ModeDescriptor covers exactly these)
    mode = kModeCount, extkey, listen, delta, bypass, quality, labudget,
    kCount                            // 29
};
inline constexpr std::size_t kNumModeParams = static_cast<std::size_t>(Pid::kModeCount);
inline constexpr std::size_t kNumParams     = static_cast<std::size_t>(Pid::kCount);
inline constexpr Pid kNoPid = Pid::kCount;
constexpr std::size_t idx(Pid p) noexcept { return static_cast<std::size_t>(p); }

// Host (APVTS) layout order: v1-forever. v1 equals the §3.1 table; a v2 parameter is added to the Pid block it belongs
// to AND appended here. The dsp.registry lint checks that this is a permutation of every Pid.
inline constexpr std::array<Pid, kNumParams> kApvtsOrder {
    Pid::thr, Pid::ratio, Pid::knee, Pid::range, Pid::atk, Pid::rel, Pid::tmode, Pid::hold, Pid::look, Pid::det,
    Pid::schpf, Pid::sce, Pid::link, Pid::stmode, Pid::voice, Pid::drive, Pid::makeup, Pid::automu, Pid::mix,
    Pid::s2thr, Pid::s2atk, Pid::s2rel,
    Pid::mode, Pid::extkey, Pid::listen, Pid::delta, Pid::bypass, Pid::quality, Pid::labudget /* v2 appends go here */ };

// Resolve order: a ParamEntry's `driver` must appear EARLIER here (registry lint). Derived params are
// evaluated in a second pass after every non-derived param, so "derived from" may point anywhere non-derived.
inline constexpr std::array<Pid, kNumModeParams> kResolveOrder {
    Pid::voice, Pid::tmode, Pid::det, Pid::stmode, Pid::automu,
    Pid::ratio, Pid::knee, Pid::thr, Pid::range, Pid::atk, Pid::rel, Pid::hold, Pid::look,
    Pid::drive, Pid::makeup, Pid::mix, Pid::schpf, Pid::sce, Pid::link,
    Pid::s2thr, Pid::s2atk, Pid::s2rel };

// Snap domain for stepped values (E §4.2.3): midpoints in this domain; ties go to the LOWER step.
// FZ0 errata (R-F0 #4): keyed by NAME, so regrouping Pid can never move a parameter into another domain.
enum class SnapDomain : uint8_t { linear, log, host };
constexpr SnapDomain snapDomain(Pid p) noexcept {
    switch (p) {
        case Pid::atk: case Pid::rel: case Pid::s2atk: case Pid::s2rel: return SnapDomain::log;   // ln(plain)
        case Pid::schpf:                                                return SnapDomain::host;  // toNorm(schpf, plain)
        default:                                                        return SnapDomain::linear;
    }
}
inline constexpr std::array<SnapDomain, kNumModeParams> kSnapDomain = [] {   // GENERATED: kSnapDomain[idx(p)] == snapDomain(p)
    std::array<SnapDomain, kNumModeParams> t{};
    for (std::size_t i = 0; i < kNumModeParams; ++i) t[i] = snapDomain(static_cast<Pid>(i));
    return t;
}();
}
```

**FZ0 errata (R-F0 #4).** Draft 1's `kSnapDomain` was a positional table checked against nothing: moving `hold` ahead
of `atk` in `Pid` would have snapped `atk` in the linear domain without a compile error. `snapDomain(Pid)` is now the
source of truth; `kSnapDomain` survives only as a table generated from it.

`Source/fcdsp/params/Setup.h` holds the two setup enums, so that `RawParams` (§4.4), `EngineHost` (§5.4) and the
processor share them without including the engine:

```cpp
#pragma once
#include <cstdint>
namespace fcdsp {
enum class Quality : uint8_t { eco = 0, std = 1, hq = 2 };
enum class LookaheadBudget : uint8_t { off = 0, ms5 = 1, ms20 = 2 };
constexpr float budgetMs(LookaheadBudget b) noexcept { return b == LookaheadBudget::off ? 0.f : b == LookaheadBudget::ms5 ? 5.f : 20.f; }
}
```

### 3.3 Host ranges (Source/fcdsp/params/HostParams.h)

```cpp
namespace fcdsp {
enum class Map : uint8_t { linear, log, power, ratio3, index, boolean };

struct HostParam {
    Pid pid; const char* id; const char* name; const char* unit;
    Map map; float lo, hi, centre;      // centre: Map::power only
    float def; int versionHint; bool automatable; bool inPresets;
    int numSteps;                       // index/boolean: 8, 128, 2, 3; else 0 (continuous)
    const char* const* choices;         // quality/labudget names, else nullptr
};
extern const std::array<HostParam, kNumParams> kHostParams;   // §3.1, in Pid order

float toPlain(Pid, float norm) noexcept;    // the ONLY normalised→plain map (APVTS lambdas, UI tracks, D3 sweep)
float toNorm (Pid, float plain) noexcept;
float legal  (Pid, float plain) noexcept;   // clamp; round index/bool
}
```

The mappings are exact. `v` is the normalised value in [0, 1].

| Map | Plain from v |
|---|---|
| linear | `lo + (hi − lo)·v` |
| log | `lo·(hi/lo)^v` |
| power | `lo + (hi − lo)·v^k`, where `k = ln((c − lo)/(hi − lo)) / ln 0.5`. This gives knee k = 2.585, hold k = 3.322, schpf k = 2.644. |
| ratio3 (E §4.3) | `v ≤ 0.8`: `S = 1 − 20^(−v/0.8)` (1:1 → 20:1). `0.8 < v ≤ 0.9`: `S = 0.95 + 0.5·(v − 0.8)` (20:1 → ∞). `v > 0.9`: `S = 1 + 10·(v − 0.9)` (∞ → −1:1). |
| index | `round(v·(numSteps − 1))` |

- The JUCE glue (`plugin/ParamLayout.cpp`) builds every `NormalisableRange` with the three-lambda constructor, calling `toPlain`/`toNorm`/`legal` (juce_NormalisableRange.h:104-106, E §4.3).
- The host mapping uses libm. That is allowed, because it is not the per-sample path, and D3 snaps its result anyway. Consequence (K2 #14): golden rows that depend on host-map values (`proc.*`) use `absrel`, never `exact`.

---

## 4. ParamSpec, ModeDescriptor, and the one resolver

### 4.1 `ParamSpec` (Source/fcdsp/params/ParamSpec.h)

```cpp
#pragma once
#include "fcdsp/params/Pid.h"
#include "fcdsp/core/Units.h"          // TimeLaw
#include <span>

namespace fcdsp {

struct ParamView;                      // Resolve.h

enum class Kind : uint8_t {
    continuous,     // clamp to [lo, hi]; `steps` (optional) = soft notches, value unchanged
    stepped,        // exactly one of `steps` (strictly increasing plain)
    hybrid,         // [lo, hi] continuous ∪ `steps` that lie OUTSIDE [lo, hi]; SlotState live inside, stepped on a step.
                    // Dangerous next to a Mode switch (K2 #4): the crossmode.no_off lint (§8.3) guards every use.
    locked,         // `value`; shown, never written (circuit constant)
    derived,        // `derive(view)`; shown read-only, follows `derivedFrom`
    notApplicable,  // hidden; engine gets `value` (neutral). Host text "—"
};

enum StepTag : uint16_t {             // OR-ed into ParamView::tags and EngineParams::tags
    kTagNone = 0,
    kTagOff = 1u << 0,     // circuit disabled (FET attack OFF, Stage 2 OFF)
    kTagAuto = 1u << 1,    // program-dependent switch position (SSL AUTO, Neve a1, digital AUTO)
    kTagAuto2 = 1u << 2,   // second auto variant (Neve a2)
    kTagAll = 1u << 3,     // 1176 all-buttons
    kTagVar = 1u << 4,     // position exposing a continuous sub-range (API VAR)
    kTagTc5 = 1u << 5, kTagTc6 = 1u << 6,   // Fairchild program time constants
    kTagProgram = 1u << 7, // the value is nominal; behaviour is program-dependent
    // bits 8..15: Mode-private, documented in that Mode's header
};

struct Step {
    float       plain;               // canonical HOST PLAIN value (what the UI writes; what snap returns)
    const char* label;               // cell label ("4", "ALL", ".3", "AUTO"); > 6 glyphs prints a registry WARNING;
                                     // the binding gate is the ui.textfit pair-fit rule (02 §8.3)
    const char* text   = nullptr;    // host/readout text; nullptr → formatted from plain ("4:1", "0.3 MS")
    uint16_t    tag    = kTagNone;
    const char* spoken = nullptr;    // a11y; nullptr → text, then label
};

struct DisplayMap {                  // a Mode's own scale over the same plain value ("remap")
    float (*toDisplay)(float plain) noexcept = nullptr;   // nullptr = identity
    float (*toPlain)(float display) noexcept = nullptr;   // exact inverse (lint: round trip ≤ 1e-4)
    const char* unit = nullptr;      // "" for dial numbers, "DBU"; nullptr = universal unit
    int8_t decimals = -1;            // −1 = universal rule
    bool invert = false;             // display rises while plain falls (INPUT and PEAK RED. over thr)
};

enum SpecFlag : uint8_t {
    kFlagExtension  = 1u << 0,       // "+" cell: not on the hardware, neutral default (D §5.1)
    kFlagHwReversed = 1u << 1,       // hardware knob runs the other way (1176): informational only
    kFlagProgram    = 1u << 2,       // locked/derived value is nominal; UI prints live EFF from UiFrame
    kFlagPlotIsPlain = 1u << 3,      // the TRANSFER plot's value for this param IS its plain value (identity DisplayMap):
                                     // knee/range handles may drag absolutely (02 §6.5). Clean sets it on thr/knee/range.
    // bits 4..6 reserved; bit 7 is kClamped (§4.4: ResolvedParam::flags = ParamSpec::flags | kClamped), never a
    // SpecFlag (FZ0 errata, R-F0 #8; static_assert in Resolve.h). (Draft 1's kFlagLatchWord is deleted: the layout
    // owns the words, 02 §6.4; K1 #23.)
};

struct ParamSpec {
    Kind        kind = Kind::notApplicable;
    uint8_t     flags = 0;
    TimeLaw     law = TimeLaw::expDb;               // time params: what the PUBLISHED value means (E §2.3)
    float       lo = 0, hi = 0;                     // continuous/hybrid sub-range, host plain, lo < hi
    std::span<const Step> steps{};                  // stepped: all; hybrid: outside [lo,hi]; continuous: soft notches;
                                                    // locked list params: exactly one step (its label)
    float       value = 0;                          // locked: the value; notApplicable: neutral engine value
    float     (*derive)(const ParamView&) noexcept = nullptr;
    Pid         derivedFrom = kNoPid;               // derived: slot it follows (UI tag, view-switch source)
    float       defaultPlain = 0;                   // Mode default: double-click, default notch, "load Mode defaults"
    const char* label  = nullptr;                   // Mode's slot name ("INPUT"); nullptr = universal
    const char* tag    = nullptr;                   // label-row tag ("FIXED", "= RATIO", "AUTO")
    const char* reason = nullptr;                   // REQUIRED unless continuous/stepped (lint): footer + a11y help
    const char* brief  = nullptr;                   // ≤ 18 glyphs: the locked/n/a sub-line ("CIRCUIT KNEE"); nullptr → tag.
                                                    // ui.textfit lints its width (K1 #25)
    DisplayMap  display{};
};

// Dependent step lists: while `driver` resolves to `driverStep`, `spec` replaces the entry's base spec.
struct Variant { int8_t driverStep; ParamSpec spec; };

struct ParamEntry {
    ParamSpec spec{};                               // base spec
    Pid driver = kNoPid;                            // lint: must precede this Pid in kResolveOrder
    std::span<const Variant> variants{};
};

struct ParamTable {                                 // Mode-filtered Pids only (FZ0 errata, R-F0 #5; asserted)
    std::array<ParamEntry, kNumModeParams> e{};
    constexpr ParamEntry&       operator[](Pid p)       noexcept { assert(idx(p) < kNumModeParams); return e[idx(p)]; }
    constexpr const ParamEntry& operator[](Pid p) const noexcept { assert(idx(p) < kNumModeParams); return e[idx(p)]; }
};
}
```

**How each D/F concept maps onto this [DECIDED]:**

| Concept (D §5.1, F §3.2) | Expressed as |
|---|---|
| continuous with range, skew and detents | `continuous` + `lo/hi`. Skew is always the host map restricted to `[lo, hi]`. Soft notches go in `steps`. Hard "Cd /31" detents are `stepped` with 31 generated steps. |
| stepped, with an exact value list and labels | `stepped` + `steps` |
| locked at a value | `locked` + `value` (+ a one-step span when the parameter is a list) |
| program-dependent (D's `P`) | `locked` or `derived` + `kFlagProgram`. It is not a separate kind, because the UI treatment is the same except for the live EFF readout. |
| derived from another parameter | `derived` + `derive` + `derivedFrom` |
| not applicable | `notApplicable` + a neutral `value` |
| remapped or renamed, with direction | **attributes, not a kind:** `label` + `DisplayMap{toDisplay, toPlain, unit, invert}`. D §5.7 and E §4.2 made "Remapped" a Kind. The attribute form lets a stepped or continuous spec *also* be renamed. |
| extension ("+") | `kFlagExtension` |
| dependent step list | `ParamEntry::driver` + `variants` |
| Stage-2 group | the `s2thr`/`s2atk`/`s2rel` entries + `ModeDescriptor::stage2` |
| voice list | the `voice` entry (stepped) |
| reasons text | `reason` (+ `tag`) |

### 4.2 `EngineParams` (Source/fcdsp/params/EngineParams.h)

This is what the engine, the analysis functions and `UiFrame` consume. It is POD, in natural units, and the Mode's `physical()` produces it.

```cpp
namespace fcdsp {
// Sentinels are the host range ends, so they smooth as finite values (K2 #20): never 1000.
inline constexpr float kRangeOff = 60.f;     // dB: rangeDb ≥ 60 = "no clamp" (the engine smooths min(rangeDb, 60))
inline constexpr float kS2Off    = 24.f;     // dB: s2ThrDb ≥ 24 = stage 2 off (the engine ramps s2On over 20 ms)
enum Topo : uint8_t { kTopoFF = 0, kTopoFB = 1 };
enum EngFlag : uint8_t { kEngAutoMakeup = 1, kEngGrOff = 2, kEngAutoRelease = 4, kEngTruePeak = 8 };

struct EngineParams {
    // level (dB). thrDb is the detector-domain threshold AFTER preGain; effective input threshold = thrDb − preGainDb
    float preGainDb = 0, thrDb = -18, slope = 0.75f, kneeDb = 6, rangeDb = kRangeOff;
    // time: tau (63 %) in ms, already converted from the published value through ParamSpec::law
    float atkTauMs = 10, relTauMs = 200, holdMs = 0, lookMs = 0;
    // gain staging
    float driveDb = 0, makeupDb = 0, mix = 1;
    // side chain and stereo
    float scHpfHz = 0 /*0 = off*/, sceDbOct = 0, link = 1;
    // stage 2
    float s2ThrDb = kS2Off, s2AtkTauMs = 1, s2RelTauMs = 100;
    // Mode-private constants, meanings documented in the Mode's header (e.g. opto emphasis, FET law)
    float m[8] {};
    // discrete (kernel key = slot, topo, det, stmode, voice, effective external key — §5.5)
    uint8_t det = 0, stmode = 0, voice = 0, tmode = 0, topo = kTopoFF, flags = 0;
    uint16_t reserved = 0;
    uint32_t tags = 0;              // OR of the active step tags
};
static_assert(std::is_trivially_copyable_v<EngineParams> && sizeof(EngineParams) == 116);
}
```

### 4.3 `ModeDescriptor` (Source/fcdsp/modes/ModeDescriptor.h)

```cpp
namespace fcdsp {
enum class Group : uint8_t { vca, fet, opto, varimu, diode, modern, limit, other };      // browser columns (F §3.6)
enum class Stage2Kind : uint8_t { none, sharedElementMax, serialPre, postClip };      // E §3.3
enum class DetectorLaw : uint8_t { peak, rms, truePeak, custom };   // curve x-axis calibration (C §5.1, E §6.2)
enum class LinkLaw : uint8_t { independent, max, mean, cvSum };
enum class Rigor : uint8_t { clean, modelled, character };        // selects the tolerance row (C §5.13)
enum class CurveFamily : uint8_t { textbook, custom };            // textbook → D1 checks it against the formula

struct TimeSpec { float seconds; TimeLaw law; float lo = 0, hi = 0; bool program = false; };

struct InternalSpec {                                              // UiFrame::internals[i] meaning
    const char* name; const char* unit; float lo, hi; int8_t decimals;
    bool history;                     // → HistoryColumn::internal0; AT MOST ONE per Mode (lint, K1 #20)
};

struct ModeDescriptor {
    // identity (key is v1-forever once shipped)
    std::string_view key;                        // [a-z0-9-]{1,24}, e.g. "fet-76"; == its directory under modes/
    std::string_view name;                       // UI, e.g. "FET 76"
    Group group;
    uint16_t introducedInStateVersion;
    uint16_t revision = 1;                       // sound revision (K2 #10): ++ when a shipped Mode's print hash moves (§9.1)
    bool provisional = false;                    // generic traits from the descriptor wave (K3 #9): golden.py adopt refuses
                                                 // its modes/<key>/ rows; dsp.registry fails if FCOMPRESSOR_RELEASE=ON
    const char* topologyLine;                    // header caption: "FEEDBACK FET · PEAK"
    const char* specLine;                        // browser/footer one-liner (F §3.6)
    // parameters
    ParamTable params;
    void (*physical)(const ParamView&, EngineParams&) noexcept;   // after kit::physicalDefault; nullptr = none
    // structure (UI and probes)
    Stage2Kind stage2;
    LinkLaw linkLaw;
    DetectorLaw (*detectorLaw)(const EngineParams&) noexcept;
    bool hasColour, colourStatic, wantsLookahead;
    // analytic metadata for the spec probes (C §5.1–5.3)
    Rigor rigor; CurveFamily family;
    TimeSpec (*attackSpec)(const ParamView&, const EngineParams&) noexcept;
    TimeSpec (*releaseSpec)(const ParamView&, const EngineParams&) noexcept;
    float (*tailSeconds)(const EngineParams&) noexcept;            // longest release × 5 (+ opto memory)
    float ctBudgetNsPerSample;                                      // bench budget (E §3.7)
    // telemetry
    std::span<const InternalSpec> internals;                        // ≤ 8 (UiFrame::internals words 8–15 reserved)
};
}
```

This descriptor **replaces C §5.1 `ModeSpec` and F §7.3's hooks.** The mapping:

| C / F field | Here |
|---|---|
| `key`, `displayName` | `key`, `name` |
| `introducedInSchema` | `introducedInStateVersion` (plus `revision`, `provisional`: new) |
| `DetectorLaw`, `LinkLaw`, `Rigor`, `CurveFamily`, `hasColour` | the fields of the same names |
| `Constraint` | the `ParamEntry` table |
| `staticGainDb` | `analysis::staticGain` (§7) |
| `attackSpec`, `releaseSpec` | the fields of the same names |
| `latencySamples` | **gone.** Latency is the host's, per §5.6. |
| `ctBudgetNsPerSample` | the field of the same name |
| F's `presentation()` | `resolveView()` |
| F's `renderStepResponse`, `scResponse`, `colourTransfer` | §7 |
| F's `internals[]`, `group`, `topologyLine`, `specLine` | the fields of the same names |

### 4.4 The resolver (Source/fcdsp/params/Resolve.h)

```cpp
#include "fcdsp/params/Setup.h"
namespace fcdsp {
struct ModeEntry;                                  // Registry.h

enum class SlotState : uint8_t { live, stepped, locked, derived, na };   // F §0.6 five states

struct ResolvedParam {
    float     plain = 0;         // canonical host-plain value after snap/lock/derive
    float     display = 0;       // in the Mode's display scale (DisplayMap), for readouts
    int8_t    step = -1;         // index into the active spec's steps; −1 = continuous
    SlotState state = SlotState::na;
    uint8_t   flags = 0;         // ParamSpec::flags | kClamped (1<<7) when raw lay outside [lo,hi]
    uint16_t  tag = 0;           // the step's tag
};
inline constexpr uint8_t kClamped = 1u << 7;
static_assert((kClamped & (kFlagExtension | kFlagHwReversed | kFlagProgram | kFlagPlotIsPlain)) == 0);   // R-F0 #8

// FZ0 errata (R-F0 #5): every Pid taken here and in Text.h is Mode-filtered, idx(pid) < kNumModeParams; the
// indexers assert it (the 7 globals are never resolved).
struct RawParams {                                   // host plain values as the APVTS raw atomics hold them
    std::array<float, kNumModeParams> v{};
    uint8_t modeSlot = 0;                            // effective slot (resolveSlot applied)
    LookaheadBudget budget = LookaheadBudget::off;   // the CONFIGURED budget (what EngineHost runs); every snapshot
                                                     // (Processor::currentRaw(), the audio thread, probes) fills it (K1 #8)
    float&       operator[](Pid p) noexcept       { assert(idx(p) < kNumModeParams); return v[idx(p)]; }
    const float& operator[](Pid p) const noexcept { assert(idx(p) < kNumModeParams); return v[idx(p)]; }
};

struct ParamView {
    const ModeDescriptor* desc = nullptr;
    uint8_t  slot = 0;
    uint32_t tags = 0;
    std::array<ResolvedParam, kNumModeParams> p{};
    std::array<const ParamSpec*, kNumModeParams> spec{};   // active spec after variants
    const ResolvedParam& operator[](Pid x) const noexcept { assert(idx(x) < kNumModeParams); return p[idx(x)]; }
};

struct Resolution { ParamView view; EngineParams eng; };

struct Snapped { float plain; int8_t step; uint16_t tag; bool clamped; };

Snapped snap(Pid, const ParamSpec&, float rawPlain) noexcept;                 // THE snapping function
int   stepCount(const ParamSpec&) noexcept;
float stepPlain(const ParamSpec&, int i) noexcept;                            // UI snap-on-write
int   stepIndexOf(Pid, const ParamSpec&, float plain) noexcept;               // = snap(...).step
const ParamSpec& activeSpec(const ParamEntry&, const ParamView& partial) noexcept;

void resolveView(const ModeDescriptor&, const RawParams&, ParamView& out) noexcept;   // UI, text, probes
void resolve(const ModeEntry&, const RawParams&, Resolution& out) noexcept;           // + physicalDefault + physical
void modeDefaults(const ModeDescriptor&, RawParams& inOut) noexcept;   // writes defaultPlain for live/stepped only

namespace kit { void physicalDefault(const ParamView&, EngineParams&) noexcept; }
}
```

**`snap()` semantics.** This is the contract that probe D3 enforces (C §5.4).

| Kind | Result |
|---|---|
| continuous | `clamp(raw, lo, hi)`, `step = −1`, `clamped = raw ∉ [lo, hi]`. Soft notches never move the value. |
| stepped | The nearest step in `snapDomain(pid)` (§3.2): log uses `ln(plain)`, host uses `toNorm(pid, plain)`, linear uses plain. Ties go to the **lower** step. There is no hysteresis, so snapping is deterministic. For SSL 2/4/10 in S, the boundaries fall at S = 0.625 and S = 0.825, as E §4.2.3 gives. |
| hybrid | If `lo ≤ raw ≤ hi`, it is continuous (`SlotState::live`). Otherwise the nearest of {each step, `lo`, `hi`} in the snap domain wins. A range edge means clamp. A step means that step (`SlotState::stepped`, K1 #32). |
| locked | `value` (+ step 0 when there is a one-step span) |
| notApplicable | `value` |
| derived | Filled in pass 2: `plain = derive(view)`, `step = −1`. |

**`resolveView` algorithm:**
1. Walk `kResolveOrder`. For each parameter, choose `activeSpec`: the first variant whose `driverStep` equals the already-resolved `driver.step`, otherwise the base spec. Snap every non-derived parameter.
   - **Lookahead budget (K1 #8).** Right after `look` is snapped: if `raw.budget == off` and the spec is not n/a, `look`
     becomes `SlotState::locked`, plain 0, tag `"OFF"`, reason `LOOKAHEAD BUDGET IS OFF — SET 5 MS OR 20 MS (ADDS
     LATENCY)`, brief `BUDGET OFF`. Otherwise `hiEff = min(spec.hi, budgetMs(raw.budget))`, and a snapped value above
     `hiEff` is clamped to it with `kClamped` set. The UI, host text and engine therefore all agree; the engine's clamp
     (§5.4 step 1) is defensive only.
2. Evaluate the derived parameters. A `derive` may read any non-derived parameter, and sees the budget-clamped `look` (Brickwall's attack is derived from it). Derived may not depend on derived (lint).
3. Fill `display` through the `DisplayMap`, set `state`, and OR the tags.

**`kit::physicalDefault(view, eng)`:**
- Copies plain values into the engine fields: `range ≥ 60 → kRangeOff`, `schpf < 20 → 0`, `s2thr ≥ 24 → kS2Off`.
- Converts published times to τ with the spec's law (`expDb`: ÷1, `t10_90`: ÷ln 9, `t0_90`: ÷ln 10, `t50`: ÷ln 2).
- Copies the step indices of `det/stmode/voice/tmode` and sets `automu → kEngAutoMakeup`.

Then `desc.physical` applies the Mode's remaps: `preGain`, the threshold offsets, topology and the private constants.

Cost: 22 snaps plus two small calls, well under 1 µs. The audio thread calls it once per block.

### 4.5 Snap on read, confirmed [DECIDED]

**Rule:** a Mode change writes **no** parameter other than `mode`. Raw values persist. Every reader resolves through the active Mode. The UI writes exact step plain values inside a gesture ("snap on write").

Why this overrules B §7.4.2, which rewrites raw values when the user changes Mode:
1. **A→B→A restores A bit-exactly,** including values that B locks or ghosts. F §3.2 already requires this for locked parameters. Applying it to every parameter removes the special case.
2. **A Mode switch is one host gesture.** It is one `begin/set/end` on `mode`, which the host records (and undoes) as one step, and one "modified" flag change. B's rewrite would push up to 22 host notifications, write automation lanes in latch/write mode, and mark a loaded preset modified. (There is no in-plugin `UndoManager`: K2 #7, §9.1.)
3. **The audio thread never needs a write.** Host automation is snapped on read anyway (B agrees). Doing the same for UI switches keeps a single code path. The D3 sweep and the host text then agree by construction.
4. **Determinism.** Snapping is pure, with no hysteresis and ties to the lower step, so probes can compute expected values.

- **Cost, accepted:** a host lane's *position* can sit between detents while its *text* shows the snapped value (E §4.2.1). HR-style segmented cells draw the snapped state.
- **Optional UX:** "switch + load Mode defaults" (Alt-click in the Mode browser) writes `mode` and then `modeDefaults()` inside one `beginBatch()/endBatch()` bracket (02 §9.5), so the audio thread never resolves a half-written set. B's "normalise on switch" becomes this explicit gesture, never an implicit side effect.

### 4.6 Host text (Source/fcdsp/params/Text.h)

```cpp
namespace fcdsp {
// The UI needs the parts separately (value and unit on a shared baseline, a spoken string for a11y). K1 #15.
struct FormattedValue {
    char value[24];      // "30", "4:1", "250", "AUTO", "−18.0"; NEVER the slot label ("INPUT" is ParamSpec::label)
    char unit[8];        // "DB", "MS", "µS", "%", "HZ", "" (dial numbers)
    char spoken[64];     // "4 to 1", "0.3 milliseconds", "all buttons"
    char prefix;         // 0, '~' (program), '=' (derived), '(' (locked: closed by formatValue)
};
void formatParts(const ParamView&, Pid, FormattedValue&) noexcept;
// UI and host readouts as one string: prefix + value + ' ' + unit ("4:1", "30", "250 µS", "~10 MS", "(= 0.8 MS)").
int formatValue(const ParamView&, Pid, char* out, int cap) noexcept;
// Host valueToText for an arbitrary candidate plain value: resolves `current` with v[pid] = plain.
int formatHost(const ModeEntry&, const RawParams& current, Pid, float plain, char* out, int cap) noexcept;
// Host textToValue: accepts step labels/texts, Mode display numbers and units, universal units. false = no parse.
bool parseHost(const ModeEntry&, const RawParams& current, Pid, std::string_view text, float& plainOut) noexcept;
}
```

**Precondition (FZ0 errata, R-F0 #5).** Every function above takes a **Mode-filtered** `Pid`,
`idx(pid) < kNumModeParams`: `RawParams` and `ParamView` hold only those 22 values, and their indexers assert it.
`formatHost`/`parseHost` are never called for the 7 globals; the plugin's host-text glue (`plugin/HostText.cpp`, P1)
formats those itself:
- `mode`: the Mode's name from the registry, `resolveSlot(slot).entry->desc->name` (the entry is `nullptr` on the null
  row, before slot 0 is registered);
- `quality`, `labudget`: `kHostParams[idx(pid)].choices` ("ECO"/"STD"/"HQ", "OFF"/"5 MS"/"20 MS");
- `extkey`, `listen`, `delta`, `bypass`: "OFF"/"ON" (`kit::kOffOn`'s labels).

- **n/a** prints "–" (U+2013, the same glyph the UI draws; K1 #15).
- **Locked** prints "(10 MS)".
- **Derived** prints "(= 0.8 MS)".
- **Program** prints the nominal value with "~".
- **Minus** is U+2212 in every formatter, UI and host alike. `parseHost` accepts both `-` and U+2212.

The APVTS `withStringFromValueFunction` lambda:
1. captures the processor;
2. snapshots `currentRaw()` (relaxed loads);
3. calls `formatHost`.

This is safe from any thread (E §4.3; `getText` from background threads is exercised by `validate.sh`, 03 §4.8). On a Mode change, `SetupWatcher` (message thread, ≤ 50 ms later) calls `updateHostDisplay(ChangeDetails().withParameterInfoChanged(true))` (E §4.3, K2 #6). Nothing on the audio thread calls it.

---

## 5. Engine

### 5.1 Core (Source/fcdsp/core)

> **S2 lead revision — ramp shape.** Every 20 ms gain transition that can land on a waveform peak (GR OFF, Stage-2
> OFF, and — for F4/F7 — bypass, listen, delta and kernel crossfades) runs a `LinearRamp` whose value passes through
> smoothstep `3t² − 2t³` before it is applied. A straight linear fade measured +40/+48 dB on the C §5.0 click test
> (limit +3); the shaped fade +0.8/+1.3 dB (F3). `oneMinusAlpha(tauMs, fs)` lives in `core/Units.h`, and
> `BallisticsPolicy`/`Stage2Policy` require `grDb(const State&)` for `Carry` (S2 lead revisions).

> **S3 lead revisions.** (1) Ramp shape: **smootherstep** `6t⁵ − 15t⁴ + 10t³` (C2) replaces smoothstep for every 20 ms
> gain transition — smoothstep read +3.3 dB on the bypass click row at 10.5 dB GR (F4); F7 converts the host ramps and
> the next `ModeEngine.h` owner converts `offAmt`/`s2On`; click rows must pass at ≥ 10.5 dB GR. (2) Makeup and mix use
> **two cascaded** 20 ms one-poles (both landing exactly; a single stage clicked +10/+21 dB on step edges, F4).
> (3) Ballistics may expose optional `sense(c, s, v)`/`crestDb(s)` hooks (F9, CrestAuto); `ModeEngine` uses them when
> present. (4) Open: an FB release cannot carry sub-ulp steps through `FbAffine` (≈ 0.07 dB stall on a 3 s FB release at
> 48 kHz); extend `FbAffine` with a base GR before Mu 67 (M4, S10). **Closed by the S10 interface revision (X10):
> `FbAffine::base` (§5.2).**

**`Rt.h`** (FZ0 errata, R-F0 #1) holds `FCDSP_NONBLOCKING` (§2.2 rule 6), so `core/` can annotate without including
`engine/`. Every function declared in `core/` is `FCDSP_NONBLOCKING`; F1 (S1) adds the bodies and may define the
non-SIMD ones inline in their headers (preferred for the per-sample `Smoother4::tick`, `LinearRamp::tick` and
`ControlTicker::advance`, which keeps them inlinable in non-LTO agent builds) or out of line, repeating the macro.

**`Simd.h`** extends HR's `Simd.h` (namespace `fcdsp::simd`; macros `FCDSP_SIMD_NEON`/`FCDSP_SIMD_SSE`; `#error` without FMA).
- **Type:** `f32x4` is a type alias, not a wrapper class. Lanes = `{ch0, ch1, aux0, aux1}` (E §3.2).
- **Ops kept from HR:** `load, store, set1, add, sub, mul, fma(a,b,c)=a+b·c, fms, rsqrte, rsqrts`.
- **Ops added:** `div, min, max, abs, neg, sqrt, floor, gt, ge, sel(mask, t, f), band, bor, lane<i>(v), withLane<i>(v, s)`.
- Every op is declared `inline … noexcept FCDSP_NONBLOCKING` and `Simd.h` includes `Rt.h` (FZ0 errata, R-F0 #1).
- NaN semantics differ between NEON and x86 `min`/`max`. Input sanitisation (§5.8), the floors and the poison check keep NaN out; `dsp.simd` pins the policy.
- The SSE backend compiles in every sprint-end `lead-x86` build but has never executed on this Mac (no Rosetta). v1 therefore ships arm64-only unless an x86 verify has passed (03 §5, K2 #15).

**`FastMath.h`** (E §0.9, E §3.2, K2 #14). Every function is written with `fma` only, and the scalar form is bit-identical to lane 0 of the vector form:

```cpp
namespace fcdsp {
inline constexpr float kDbPerLog2 = 6.02059991f, kLog2PerDb = 0.166096404f;
inline constexpr float kLinFloor = 1e-12f /*−240 dB*/, kMsFloor = 1e-24f;
// Every declaration below carries FCDSP_NONBLOCKING (FZ0 errata, R-F0 #1); shown once here for brevity.
simd::f32x4 log2(simd::f32x4) noexcept;     // minimax poly; |err| ≤ 4e-6 (refit target 4e-7)
simd::f32x4 exp2(simd::f32x4) noexcept;     // input clamped to [−126, 126]; rel err ≤ 9e-5 (refit target 1e-5)
float log2(float) noexcept;                 // == lane 0 of the vector form, bit-identical
float exp2(float) noexcept;
// libm replacements for colour stages and per-tick filter design (targets fixed by the F1 spike, asserted by dsp.simd)
simd::f32x4 tanh(simd::f32x4) noexcept;     // 1 − 2/(exp2(2x·log2e) + 1); odd minimax polynomial for |x| < 0.125
simd::f32x4 logCosh(simd::f32x4) noexcept;  // |x| + ln2·log2(1 + exp2(−2|x|·log2e)) − ln 2 (ADAA-1 antiderivative of tanh)
float tanh(float) noexcept;  float logCosh(float) noexcept;
float tanPi(float x) noexcept;              // tan(π·x), x ∈ [0, 0.499]: SVF g = tanPi(fc/fs) and the tilt prewarp (per tick)
float sinPi(float x) noexcept;  float cosPi(float x) noexcept;   // x ∈ [−1, 1]
inline simd::f32x4 dbFromLin(simd::f32x4 absx) noexcept;   // kDbPerLog2 · log2(max(|x|, kLinFloor))
inline simd::f32x4 dbFromMs (simd::f32x4 ms)   noexcept;   // 0.5·kDbPerLog2 · log2(max(ms, kMsFloor))
inline simd::f32x4 linFromDb(simd::f32x4 db)   noexcept;   // exp2(db · kLog2PerDb)
}
```

**`Units.h`:**

```cpp
namespace fcdsp {
inline constexpr float kDbuAt0dBFS = 22.0f;                              // fixed calibration (§3.1)
enum class TimeLaw : uint8_t { expDb, expLin, t10_90, t0_90, t50, rateDbPerS };   // E §2.3
constexpr float lawFactor(TimeLaw) noexcept FCDSP_NONBLOCKING;   // t_published / tau: 1, 1, ln9, ln10, ln2; rate → 1
inline float alphaFromTau(float tauMs, float fs) noexcept FCDSP_NONBLOCKING;   // exp2(−1000/(tauMs·fs·ln2)); tauMs ≤ 0 → 0
}
```

**`ScopedFtz.h`** is HR's `ScopedFtz` (Harness.h:50-72), made JUCE-free: FPCR bit 24 on arm64, MXCSR 0x8040 on x86. Its constructor and destructor are `FCDSP_NONBLOCKING` (FZ0 errata). **`EngineHost::process` opens one itself**, and so does every `analysis::` entry point (K2 #24), so probes, the plugin and `PreviewWorker` run in the same FP mode no matter who calls them. The processor keeps `juce::ScopedNoDenormals` as well, which does no harm.

**`Sanitize.h`** (K2 #13):

```cpp
namespace fcdsp {
// Before ANY delay line or filter: NaN/inf → 0 (bit test (bits & 0x7f800000) != 0x7f800000), then clamp |x| ≤ 1e6
// (+120 dBFS). Returns how many samples were replaced or clamped (→ UiFrame kUiPoisonReset notice when > 0).
int sanitize(const float* in, float* out, int n) noexcept FCDSP_NONBLOCKING;
}
```

**`Smoother.h` and `ControlTicker.h`.** Block-size invariance is required by C §5.6.3; E §4.6 describes the scheme.

```cpp
namespace fcdsp {
// Every member is FCDSP_NONBLOCKING (FZ0 errata, R-F0 #1).
struct Smoother4 {                          // four independent per-sample one-poles, tau 20 ms, epsilon landing (HR smoothSnap)
    simd::f32x4 cur, tgt, a, eps;
    void prepare(float fs, float tauMs = 20.f, simd::f32x4 eps = simd::set1(1e-5f)) noexcept FCDSP_NONBLOCKING;
    void setTarget(simd::f32x4 t) noexcept FCDSP_NONBLOCKING { tgt = t; }
    void snap() noexcept FCDSP_NONBLOCKING { cur = tgt; }
    simd::f32x4 tick() noexcept FCDSP_NONBLOCKING;   // cur = tgt + a·(cur − tgt); lands exactly when |cur − tgt| < eps
};
struct LinearRamp {                         // 0…1 amount, linear, fixed length; lands exactly on 0 and 1
    float cur = 0, tgt = 0, step = 0;
    void prepare(float fs, float ms = 20.f) noexcept FCDSP_NONBLOCKING;
    void setTarget(float t) noexcept FCDSP_NONBLOCKING;  float tick() noexcept FCDSP_NONBLOCKING;
    bool moving() const noexcept FCDSP_NONBLOCKING;
};
struct ControlTicker {                      // true every kTickSamples at ABSOLUTE sample index multiples
    uint64_t next = 0;
    bool advance(uint64_t sampleIndex) noexcept FCDSP_NONBLOCKING;
};
inline constexpr int kTickSamples = 16;
}
```

- **Per sample, in the engine:** `lvl_ = Smoother4{thrDb, slope, min(rangeDb, 60), –}` and `lvl2_ = Smoother4{clamp(s2ThrDb, −40, 24), kneeDb, –, –}` (K2 #20: sentinels are finite range ends, never 1000).
  - **FZ0 errata (R-F0 #2): they reach the policies as a per-sample `LevelCtl`** (§5.2). Each sample,
    `a = lvl_.tick()`, `b = lvl2_.tick()`, and `l = LevelCtl{bcast<0>(a), bcast<1>(a), bcast<1>(b), bcast<0>(b)}`
    (`thrDb, slope, kneeDb, s2ThrDb`; `bcast<I>(v) = set1(lane<I>(v))`, one DUP on NEON); `range = bcast<2>(a)`.
    `G::target`/`G::solveFb` and `S2::combine` take `l`; `design()` never bakes those four into `Coeffs`, so
    threshold, ratio, knee and stage-2 threshold moves are smoothed per sample, never stepped per tick, and never force
    a per-sample `design()`. `ModeEngine::staticGr` (analysis) builds `l` unsmoothed from the same targets,
    `{set1(thrDb), set1(slope), set1(kneeDb), set1(clamp(s2ThrDb, −40, 24))}`, so the settled engine (the smoothers
    land exactly) and `staticGr` agree bit for bit.
  - Two `LinearRamp`s: `offAmt_` multiplies the target GR and ramps to 0 over 20 ms while `kEngGrOff` is set, so GR OFF never steps (K2 #4 iii); `s2On_` does the same for stage 2 (`s2ThrDb ≥ kS2Off` = off).
- **Per sample, in the host, per path** (`PathState::gain`, §5.5): `{preGainDb, makeupTotalDb (= makeupDb + that engine's autoMakeupDb()), –, –}`. **Host-level:** `mix_` (Smoother4 lane 0, the incoming Mode's value); `bypass_`, `listen_`, `delta_` and the fade weight are `LinearRamp`s of 20 ms. `driveDb` is smoothed per tick by each engine's colour stage, so each path keeps its own.
- **Per tick:** attack/release/hold τ (smoothed in the log domain), Stage-2 times, the SC filter coefficients (host, via `tanPi`).
- **At the next tick, unsmoothed:** step tags and `tmode`. Ballistics state is continuous across a coefficient step (E §4.6). Tag flips that change gain (GR OFF, Stage-2 OFF, AUTO) are ramped as above, and `dsp.zipper` has detent-edge rows for every stepped and hybrid parameter (K2 #4 iv).
- **Kernel-key changes** use the crossfade (§5.5).

### 5.2 Stage concepts (Source/fcdsp/engine/Stage.h)

Every stage is a POD `State` of `f32x4` members, plus a `Coeffs` struct, plus `static` functions (E §3.3). Engines own no resources, so destroying one is a trivial `~IEngine()` (E §3.5).

**Two rates (FZ0 errata, R-F0 #2).** `design()` runs on control ticks only and fills `Coeffs` from `EngineParams`;
it never bakes a `LevelCtl` quantity into `Coeffs`. Threshold, slope, knee and stage-2 threshold arrive **per sample**
as a `LevelCtl`, built by `ModeEngine` from its smoothers (§5.1). Draft 1's concepts gave the smoothed values no way
to reach the gain computer or stage 2, so F3 would have had to call `design()` per sample or step them every 16
samples (zipper), and every later computer would have had to guess the workaround.

```cpp
namespace fcdsp {
struct StageCtx { float fs = 0; float fsOs = 0; int osFactor = 1; std::span<float> scratch{}; };

// Per-sample level controls (FZ0 errata, R-F0 #2): smoothed, each value broadcast to all four lanes by ModeEngine;
// built unsmoothed from EngineParams by ModeEngine::staticGr.
struct LevelCtl {
    simd::f32x4 thrDb{};      // detector-domain threshold, dB (after preGain)
    simd::f32x4 slope{};      // S = 1 − 1/R
    simd::f32x4 kneeDb{};     // knee width W, dB
    simd::f32x4 s2ThrDb{};    // stage-2 threshold, dB: clamp(s2ThrDb, −40, kS2Off)
};

template <class P> concept Designable = requires (typename P::Coeffs& c, const EngineParams& p, const StageCtx& x) {
    { P::design(c, p, x) } noexcept;                       // control ticks only; never bakes a LevelCtl field
};

template <class D> concept DetectorPolicy = Designable<D> &&
requires (const typename D::Coeffs& c, typename D::State& s, simd::f32x4 v) {
    { D::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;         // linear SC in → detector-law dB out
    { D::seed(s, v) } noexcept;                                          // from Carry::detDb
    { D::levelDb(std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
};

// The ballistics' affine map in the feedback loop (K2 #1): given its state, every linear ballistic path maps the
// gain computer's output affinely to the applied GR:  r = A + B·r̂(x − r),  per lane A ≥ 0, 0 ≤ B ≤ 1.
struct FbAffine { simd::f32x4 A, B; };

template <class G> concept GainComputerPolicy = Designable<G> &&
requires (const typename G::Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) {
    { G::target(c, x, l) } noexcept -> std::same_as<simd::f32x4>;       // FF, pure: r̂ ≥ 0 at detector level x
    { G::solveFb(c, x, l, a) } noexcept -> std::same_as<simd::f32x4>;   // FB: THE root of r = A + B·r̂(x − r);
                                                                         // a = {0, 1} → the static FB curve
};

template <class L> concept LinkPolicy =
requires (simd::f32x4 r, float link) {
    { L::apply(r, link) } noexcept -> std::same_as<simd::f32x4>;         // apply(r, link): lanes 0–1 only;
};                                                                       //   link = EngineParams::link ∈ [0, 1]

namespace detail { struct FbSolveArchetype { simd::f32x4 operator()(FbAffine) const noexcept; }; }   // declared only

template <class B> concept BallisticsPolicy = Designable<B> &&
requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 v, detail::FbSolveArchetype solve) {
    { B::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;                        // FF: r̂ → r
    { B::solveFb(c, std::as_const(s), solve) } noexcept -> std::same_as<simd::f32x4>;  // FB: r from ≥ 1 affine solves
    { B::commitFb(c, s, v) } noexcept;                                                  // FB: accept the LINKED r,
                                                                                        //     advance every internal state
    { B::seed(s, v) } noexcept;                                                         // from Carry::grDb
    { B::attackNowMs (c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
    { B::releaseNowMs(c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
    { B::status(std::as_const(s)) } noexcept -> std::same_as<uint8_t>;   // ControlIo::bits b0–1 phase (max lane), b2 auto-slow
};

template <class S2> concept Stage2Policy = Designable<S2> &&
requires (const typename S2::Coeffs& c, typename S2::State& s, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l) {
    { S2::combine(c, s, r1, xDb, l) } noexcept -> std::same_as<simd::f32x4>;   // (r1, xDb, l) → r; s2 GR in aux lanes
    { S2::seed(s, r1) } noexcept;                                              // from Carry::s2GrDb (FZ0 errata)
};

// Mode-internal SC shaping (FZ0 errata, R-F0 #3): Flat, R37Shelf, SlowHp, Thrust. Per sample on the linear SC after
// the host filters (ControlIo::sc), before the detector; magDb feeds scShapeDb / analysis::scResponse.
template <class S> concept ScShapePolicy = Designable<S> &&
requires (const typename S::Coeffs& c, typename S::State& s, simd::f32x4 v, float hz, float fs) {
    { S::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;          // tick(c, s, sc) → shaped sc, linear
    { S::magDb(c, hz, fs) } noexcept -> std::same_as<float>;             // |H(hz)| in dB at rate fs
};

template <class C> concept ColourPolicy = Designable<C> &&
requires (const typename C::Coeffs& c, typename C::State& s, float* x, const float* grDb, int n, int channel,
          float xIn, float grIn) {
    { C::process(c, s, x, grDb, n, channel) } noexcept;   // process(c, s, x, grDb, n, channel): in place, OS rate,
                                                          //   one channel; channel = 0 or 1 (the wet/grDbOs row and
                                                          //   col_[] index), never a drive: drive is in Coeffs
    { C::transfer(c, xIn, grIn) } noexcept -> std::same_as<float>;   // transfer(c, x, grDb) → y (COLOUR view)
    { C::reset(s) } noexcept;
};
}
```

FZ0 errata (R-F0 #3): `Stage2Policy` gains `seed` (a Mode switch with stage 2 engaged seeds it from `Carry::s2GrDb`
instead of restarting at 0 dB and overshooting during the fade); `ScShapePolicy` is new; `ModeEngine` asserts
`LinkPolicy` and `ScShapePolicy` too (§5.3); the colour and link arguments are named.

> **S10 interface revision (X10; additive: every existing policy compiles and runs unchanged; `Stage.h`).**
> 1. **`FbAffine::base`** (`struct FbAffine { simd::f32x4 A, B; simd::f32x4 base{}; };`, ADR-66, F9's open item).
>    The map is `r = base + A + B·r̂(x − r)` and every `G::solveFb` returns **`r − base`**. `FbAffine{A, B}` (base 0)
>    is the FZ0 map and the FZ0 absolute root, bit for bit. A ballistics whose GR moves by less than an ulp per sample
>    passes `base` = its GR and `A` = its move without the gain term (may be negative): the returned increment's
>    rounding then scales with `|A| + B·r̂`, not with the GR. Every computer honours it: `QuadKnee` (base-0 lanes: the
>    FZ0 closed forms; others: `d = A + κu²` in the knee, `(A + Bk·o′)/(1 + Bk)` linear, `o′ = x − T − base`),
>    `FeedbackZdf<G>` (Newton on the increment; a based lane's upper bracket gets 2⁻²⁰ of `|A| + B·r̂` slack),
>    `FeedbackDelayed<G>` (`r̃ = (base + A)/(1 − B)`), and so everything they wrap. **A new computer must honour
>    `base`, or wrap its law in `FeedbackZdf<G>`.** `dsp.fbsolve` `base.*` rows.
> 2. **`SmoothBranching`'s FB sub-ulp carry.** Where FF's carry gate holds for the chosen branch (`k < 2⁻¹⁴`, or the
>    absolute root's step within 2⁻²⁰·|r|), a third, based solve of the same branch, `{lo − k(r + lo), k, base = r}`,
>    returns the value's step; it is split into the float `r′` and the remainder `lo′` as the FF step is, and travels to
>    `commitFb` in scratch (a lane the link raised drops `lo`). Elsewhere the FB step is the FZ0 one bit for bit.
>    `dsp.fbsolve` `carry.*`: a 25 s FB release at 48 and 384 kHz tracks the exact discrete recurrence within 1.1e-6 dB
>    (the FZ0 recurrence: 0.24 / 2.57 dB). A composite that wraps `SmoothBranching` paths gets the carry by calling
>    `Path::solveFb`/`Path::commitFb`; one that forms its own maps (`DualRelease`, `OptoCell`) still solves them absolute.
> 3. **Optional hooks, detected (not in the concepts):** `S2::combineStatic(c, r1, xDb, l)` (`HasCombineStatic`; the
>    settled stage 2 for the analysis curves, every lane an independent abscissa); `G::rhatFb(c, y, l)` (`HasRhatFb`;
>    r̂_fb(y), `G::target` with the loop gain: `QuadKnee`, `FeedbackZdf`, `FeedbackDelayed`); `B::commitFb(c, s, r,
>    rhat)` (`HasCommitFbRhat`; the commit with `rhat = r̂_fb(x − r)` at the linked r: `DualRelease` gives its fast path
>    the exact `A_f + B_f·rhat` where the slow root won, M1's request; `AutoSwitch` forwards it); `B::fbFalls(s)`
>    (`HasFbFalls`; the value's FB fall verdict, which `Hold` uses: with the carry a float root no longer tells whether
>    the value falls; `SmoothBranching`, forwarded by `CrestAuto`).

**The per-sample step inside `ModeEngine::control` (K2 #1, #5), with `l` the sample's `LevelCtl` (§5.1):**

```cpp
v = SH::tick(shc_, sh_, sc[i]);                                  // Mode-internal SC shaping (linear)
x = D::tick(dc_, det_, v);                                       // detector-law dB of the (pre-gained) input
// FB:
auto solve = [&](FbAffine a) noexcept { return G::solveFb(gc_, x, l, a); };
r = B::solveFb(bc_, bal_, solve);                                // per lane; several branches → max of roots
r = L::apply(r, p_.link);                                        // link AFTER the per-lane solve (lanes 0–1)
B::commitFb(bc_, bal_, r);                                       // the linked value becomes the next state
```

- **Closed forms** (quadratic and hard knee; E §2.6 with `α·r1 → A`, `(1−α) → B`): linear region
  `r = (A + B·k·(x − T)) / (1 + B·k)`; knee region `c = b − A`, `κ = B·k/(2W)`; below the knee `r = A`. Other laws
  wrap in `FeedbackZdf<G>` (two fixed Newton steps inside the bisection bracket `[A, A + B·max(0, x − T + W)]`).
- **Ballistics as affine maps:** `SmoothBranching` → `{α·r1, 1−α}` with α from E's predictor. `DualRelease` → fast
  `{α_f·rf1, 1−α_f}`, slow `{α_s·rs1 + (1−α_s)·α_f·rf1, (1−α_s)(1−α_f)}`, `r = max(solve(fast), solve(slow))`.
  `Hold` during hold → `{r1, 0}`. Serial one-pole chains compose affinely; parallel branches take the max of roots.
  **Max of roots is exact:** `g_i(r) = A_i + B_i·r̂(x − r)` is non-increasing in r, so if `r1* ≥ r2*` then
  `g2(r1*) ≤ g2(r2*) = r2* ≤ r1*`, and `r1*` solves `r = max(g1, g2)`.
- **Uniqueness needs a monotone curve (K2 #5a).** Every FB Mode's `r̂_fb` must be non-decreasing on y ∈ [−80, +40] dB
  at every step (registry row `fb.monotone`, sampled every 0.1 dB). FET ALL is realised with a larger k, a threshold
  shift, attack and colour changes — never with a negative-slope region.
- **Link in FB (K2 #5b)** is applied after the per-lane solve and before `commitFb`. `max` and `mean` are
  non-expansive, so each lane stays a contraction; partial link (Bus 25 OLD) needs no 2-D solve. Static curves use the
  per-lane solve. `dsp.link.<key>` has an FB steady-state row.
- The FF step is `x = D::tick; r̂ = G::target(x, l); r̂ = L::apply(r̂, link); r = B::tick(r̂)` (link on targets,
  E §8). Then, for both topologies: `r = S2::combine(r, x, l)`, `r = min(r, rangeSmoothed)`, `r ·= offAmt_`.
- **Real time.** Every policy function is called from `ModeEngine`'s `FCDSP_NONBLOCKING` members. Policies are
  header-inline, so `-Wfunction-effects` infers them; annotating them `FCDSP_NONBLOCKING` is recommended (§2.2 rule 6).

**Policy catalogue** (E §3.3). Names are fixed now; bodies belong to their owners. **One policy per header** at
`Source/fcdsp/engine/stages/<slot>/<Policy>.h`; combinators in `stages/combinators/`; no umbrella headers — a traits
header includes exactly what it uses (K3 #7). A policy used by one Mode may live in `modes/<key>/` until a second Mode
needs it; the lead promotes it between sprints.

| Slot (`stages/…/`) | Policies |
|---|---|
| `detector/` | `PeakLog`, `RmsLog`, `PeakLinRC`, `DualDet`, `OptoSense` |
| `gain/` | `QuadKnee` (ZDF closed form, E §2.6), `ProgressiveKnee`, `TableCurve`, `LimiterCurve`, `OptoCellCurve`; adaptors `FeedbackZdf<G>` (Newton), `FeedbackDelayed<G>` (opto only, below) |
| `link/` | `LinkIndependent`, `LinkMax`, `LinkMean`, `LinkCvSum`, all blended by k (E §8) |
| `ballistics/` | `SmoothBranching`, `SmoothDecoupled`, `DualRelease`, `RateRelease`, `MultiStage3`, `TcSelector`, `OptoCell`, `SlidingMaxBox` (Brickwall) |
| `stage2/` | `NoStage2`, `SharedElementMax<Det, Bal>`, `PostClip<Shaper>` (`SerialPre` is reserved) |
| `colour/` | `ColourNone`, `VcaBus`, `FetColour`, `TubeSym`, `DiodeAsym`, `Bright`, `TubeTransformer`, `DiodeBridge`, `TubePushPull`, `LoudClip`, `Adaa.h` (ADAA-1 residual shapers) |
| `scshape/` | `Flat`, `R37Shelf`, `SlowHp` (Diode 609), `Thrust` |
| `law/` | `law::Vca` (`g = exp2(−r/6.02)`), `law::LdrShunt`, `law::FetVcr`, `law::VariMu`. Everything is expressed as r in dB. |
| `combinators/` | `DetSelect<...>` (by `det`; kernel key), `ColourSelect<...>` (by `voice`), `AutoSwitch<A, B>` (the tag picks B and seeds B from A), `CrestAuto<Inner>`, `Hold<Inner>` |

### 5.3 Traits, `ModeEngine`, `IEngine` (Source/fcdsp/engine)

```cpp
namespace fcdsp {
inline constexpr int    kChunk = 64;              // base-rate samples per control() call
inline constexpr size_t kArenaBytes = 8192;       // per slot, alignas(64) (E §3.6)
inline constexpr int    kInternals = 16;          // UiFrame words; a descriptor declares ≤ 8 (words 8–15 reserved)

struct PrepareInfo { float fs = 0; int osFactor = 1; std::span<float> scratch{}; };   // scratch: host-owned, per slot

enum class LaneDomain : uint8_t { lr = 0, ms = 1 };
// FZ0 errata (R-F0 #7): every member has a default initialiser, so `Carry c;` is a well-defined cold start (the
// same for ControlIo, AudioIo, EngineTelemetry and PrepareInfo). Layout unchanged: 64 bytes.
struct Carry {                                    // Mode/kernel switch hand-over, lanes {c0, c1, aux0, aux1}
    simd::f32x4 grDb{};                           // applied GR (≥ 0)                   → Ballistics::seed
    simd::f32x4 detDb{};                          // detector level, detector-law dB    → Detector::seed
    simd::f32x4 s2GrDb{};                         // stage-2 GR, 0 without stage 2      → Stage2::seed
    float    relNowMs[2]{};                       // lets a program-dependent Mode seed its memory term
    uint8_t  msDomain = 0;                        // LaneDomain of lanes 0–1 in the outgoing engine (K2 #3d)
    uint8_t  pad[3]{};
    uint32_t valid = 0;                           // 0 = cold start (seed() is then a no-op)
};
static_assert(sizeof(Carry) == 64 && alignof(Carry) == 16);

// Engine status bits per sample, in HistoryColumn::bits layout (§6.3): b0–1 phase of the max-GR lane
// (0 idle, 1 attack, 2 hold, 3 release), b2 auto-slow, b3 range-limited, b4 stage-2 active.
struct ControlIo {
    int n = 0;                                    // 1..kChunk
    uint64_t sampleIndex = 0;                     // absolute index of sample 0 (ControlTicker)
    const simd::f32x4* sc = nullptr;              // [n] linear SC after host filters, preGain and encode
    simd::f32x4* grDb = nullptr;                  // [n] out: applied GR per lane (lanes 0–1 consumed by host)
    simd::f32x4* detDb = nullptr;                 // [n] out or nullptr (tap/telemetry): curve-axis level + preGain
    simd::f32x4* tgtDb = nullptr;                 // [n] out or nullptr: static target GR (post link)
    simd::f32x4* s2GrDb = nullptr;                // [n] out or nullptr
    uint8_t* bits = nullptr;                      // [n] out or nullptr: status bits above (replaces Draft 1's `phase`)
    bool keyExternal = false;                     // FB kernels evaluate FF on the key (E §2.6); part of the kernel key
};

struct EngineTelemetry {                          // lanes 0–1, end of the last chunk
    float attackNowMs[2]{}, releaseNowMs[2]{}, crestDb[2]{};
};

struct AudioIo {
    int nOs = 0;                                  // samples at the OS rate
    float* const* wet = nullptr;                  // [2][nOs] in: upsampled delayed main × g (g includes this path's
                                                  //   preGain, §5.4), encoded; out: coloured (pre-makeup)
    const float* const* grDbOs = nullptr;         // [2][nOs] applied GR interpolated to the OS rate
};

// FZ0 errata (R-F0 #1): EVERY member is FCDSP_NONBLOCKING, the destructor included. The host constructs, prepares,
// seeds, runs and destroys engines on the audio thread (§5.5), and a virtual call cannot be inferred.
class IEngine {                                   // one virtual call per chunk, never per sample (E §3.6)
public:
    virtual ~IEngine() FCDSP_NONBLOCKING = default;                       // engines own no resources
    virtual void  prepare(const PrepareInfo&) noexcept FCDSP_NONBLOCKING = 0;   // math only; allocation-free; runs
                                                                               //   the FB stability guard
    virtual void  reset() noexcept FCDSP_NONBLOCKING = 0;
    virtual void  setParams(const EngineParams&) noexcept FCDSP_NONBLOCKING = 0;   // per block: smoother targets
    virtual void  snapParams() noexcept FCDSP_NONBLOCKING = 0;                     // recall: jump smoothers to targets
    virtual Carry carry() const noexcept FCDSP_NONBLOCKING = 0;
    virtual void  seed(const Carry&) noexcept FCDSP_NONBLOCKING = 0;               // Detector/Ballistics/Stage2::seed
    virtual void  control(const ControlIo&) noexcept FCDSP_NONBLOCKING = 0;
    virtual void  colour(const AudioIo&) noexcept FCDSP_NONBLOCKING = 0;
    virtual float autoMakeupDb() const noexcept FCDSP_NONBLOCKING = 0;   // 0 unless kEngAutoMakeup (E §2.2: r̂(0 dBFS)·k)
    virtual int   scDelaySamples() const noexcept FCDSP_NONBLOCKING = 0; // the engine's own SC delay (true-peak
                                                                         //   interpolator), else 0
    virtual void  internals(float out[kInternals]) const noexcept FCDSP_NONBLOCKING = 0;
    virtual void  telemetry(EngineTelemetry&) const noexcept FCDSP_NONBLOCKING = 0;   // K3 #11
    virtual bool  finite() const noexcept FCDSP_NONBLOCKING = 0;                      // poison check (§5.8)
};

// A Mode = one directory with a Traits header (E §3.5). Example shape, Source/fcdsp/modes/fet-76/Fet76.h:
struct Fet76 {
    static constexpr const ModeDescriptor& desc = kFet76;         // defined in Fet76Desc.cpp
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::FetColour>;     // voice picks the revision's constants
    using ScShape    = stage::Flat;                               // Mode-internal SC shaping (not the host filter)
    static constexpr uint8_t kTopologies = 1u << kTopoFB;         // kernels compiled in (bitmask)
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING;   // reads privates
};

template <class M>
class ModeEngine final : public IEngine {
    static_assert(DetectorPolicy<typename M::Detector> && GainComputerPolicy<typename M::Computer> &&
                  LinkPolicy<typename M::Link> && BallisticsPolicy<typename M::Ballistics> &&
                  Stage2Policy<typename M::Stage2> && ColourPolicy<typename M::Colour> &&
                  ScShapePolicy<typename M::ScShape>);                        // FZ0 errata (R-F0 #3): + Link, ScShape
    friend M;                     // FZ0 errata (R-F0 #6): M::internals(engine, out) reads the members below by name
    alignas(16) typename M::Detector::State   det_{};
    alignas(16) typename M::Ballistics::State bal_{};
    alignas(16) typename M::Stage2::State     s2_{};
    alignas(16) typename M::Colour::State     col_[2]{};
    typename M::Detector::Coeffs dc_{}; typename M::Computer::Coeffs gc_{};
    typename M::Ballistics::Coeffs bc_{}; typename M::Stage2::Coeffs s2c_{}; typename M::Colour::Coeffs cc_{};
    typename M::ScShape::State sh_{}; typename M::ScShape::Coeffs shc_{};
    Smoother4 lvl_{}, lvl2_{};        // {thrDb, slope, min(rangeDb,60), –}, {s2ThrDb, kneeDb, –, –} → LevelCtl (§5.1)
    LinearRamp offAmt_{}, s2On_{};
    EngineParams p_{}; ControlTicker tick_{}; StageCtx ctx_{};
public:
    static IEngine* construct(void* arena) noexcept FCDSP_NONBLOCKING;   // placement-new; RT-safe
    void control(const ControlIo& io) noexcept FCDSP_NONBLOCKING override;   // loop: tick → design; FF or FB step
                                                                             // (§5.2); branch per CHUNK on p_.topo
    // … the other overrides forward to the policies; each repeats FCDSP_NONBLOCKING (FZ0 errata, R-F0 #1)
    // Analysis entry points (instantiated from the SAME policies; §7):
    static void staticGr(const EngineParams&, const float* xDetDb, float* grDb, int n) noexcept;
    static void scShapeDb(const EngineParams&, float fs, const float* hz, float* magDb, int n) noexcept;
    static void colourCurve(const EngineParams&, float grDb, const float* x, float* y, int n) noexcept;
    static void staticS2(const EngineParams&, const float* xDetDb, const float* r1Db, float* grDb, int n) noexcept;
};                                                                          // ^ S10 interface revision (X10)
// The size/alignment asserts live in FCDSP_DEFINE_MODE, one per Mode TU (§8.2).
}
```

> **S10 interface revision (X10).** `staticS2(e, x, r1, gr, n)` is what `control()` applies after the computer, stage
> 2 only, settled: `lerp(r1, S2::combineStatic(s2c, r1, x, l), rampShape(settled s2On))` with the unsmoothed
> `LevelCtl`, four abscissae per call, i.e. exactly `r1` while `s2ThrDb ≥ kS2Off`; the identity for a Stage2 without
> `combineStatic` (`NoStage2`); `gr` may alias `r1`. In the FB step, when the ballistics have `commitFb(c, s, r, rhat)`
> and the computer `rhatFb` (§5.2 note), the commit gets `rhat = G::rhatFb(gc_, x − r, l)` at the linked r; otherwise
> `commitFb(c, s, r)` as before. `FbAffine::base` reaches the computer unchanged through the solve lambda.

- **Traits hooks (FZ0 errata, R-F0 #6).** `friend M;` lets the Traits' `internals` hook read `det_`, `bal_`, `s2_`,
  `col_`, `sh_`, the `Coeffs`, `lvl_`/`lvl2_` and `p_` through its `const auto&` (Clean's REL EFF, CREST and PEAK/RMS
  DET, FET's LOOP CV). The member names are therefore part of this frozen declaration. The hook is header-inline and
  must be nonblocking: `ModeEngine::internals` calls it on the audio thread.
- **Lanes (E §3.2).** Lanes 0–1 are the channels, as L/R or M/S after encoding. Lanes 2–3 carry *independent* recurrences: crest detectors, the Stage-2 detector and ballistics, or a second link-shape detector. Serial chains, such as DualRelease's `r_f → r_s`, are never packed.
- **FB (E §2.6).** The loop senses `ℓ(filter(x)) − r`; §5.2 gives the per-sample step and the affine solve.
  - `FeedbackDelayed<G>` (a one-sample-delay loop) is allowed only for the opto cell, whose fastest τ ≈ 10 ms is non-ringing at any k (E §2.7). The guard `k ≤ α/(1−α)` depends on fs and on the level-dependent loop gain, so it is **not** a `static_assert` (K2 #5c): `prepare()` computes the bound at the actual fs for the worst-case loop gain, and if it is violated the engine switches that kernel to `FeedbackZdf`. `dsp.srsweep.opto-2a` has a 22.05 kHz stability row.
- **Scratch.** A Mode with sizeable buffers, such as Brickwall's sliding max over up to 20 ms, gets `PrepareInfo::scratch`. That buffer is host-owned and allocated in `configure`: 4 × nextPow2(L_la,max + kChunk) floats per slot. The 8 KiB arena holds POD state only.

### 5.4 `EngineHost` (Source/fcdsp/engine/EngineHost.h)

This is the Mode-agnostic part (E §3.1, E §9.1). It is JUCE-free, so every probe drives exactly what the plugin runs.
`EngineHost.cpp` is **orchestration only**; the work lives in pure component headers under `engine/host/`, each with
one owner (K3 #12): `ScFilter.h` (TPT SVF HPF + tilt), `Router.h` (6 `stmode`s, key select, encode/decode), `Delay.h`,
`Ramps.h`, `Crossfade.h` (`PathState`, kernel key, latch, minimum gap), `TelemetryAccum.h` (meters, 1 ms columns), plus
`engine/Oversampler.{h,cpp}`.

```cpp
#include "fcdsp/params/Setup.h"
#include "fcdsp/engine/TestTap.h"
namespace fcdsp {
struct HostConfig {
    double fs = 48000; int maxBlock = 512;
    Quality quality = Quality::std; LookaheadBudget budget = LookaheadBudget::off;
    int mainIns = 2, mainOuts = 2, keyChans = 0;       // key 0 = bus inactive
};

// FZ0 errata (R-F0 #7): default initialisers, so the processor's "previous BlockParams" (reused while a batch is
// open, §2.3) is well defined before its first write.
struct BlockParams {                                   // built by the processor each block (reused while a batch is open)
    uint8_t slot = 0;                                  // effective Mode slot
    EngineParams eng{};                                // resolve(slot, raw).eng
    bool bypass = false, delta = false, listen = false, extKey = false;
};

struct ProcessIo {
    const float* const* in = nullptr;  int numIn = 0;  // 1 or 2
    const float* const* key = nullptr; int numKey = 0; // 0, 1 or 2
    float* const* out = nullptr;       int numOut = 0; // 1 or 2; may alias in
    int n = 0;                                         // any length ≥ 0; chunked internally
    bool hostBypassed = false;                         // processBlockBypassed path (HR B §1.6)
};

class EngineHost {
public:
    EngineHost() noexcept;                             // allocates nothing
    ~EngineHost();
    // prepareToPlay's thread, or SetupWatcher (message thread) under suspendProcessing(true)
    void configure(const HostConfig&, const BlockParams& initial);   // the ONLY allocation point; engines start snapped
    static int latencyFor(const HostConfig&) noexcept FCDSP_NONBLOCKING;   // lookaheadSamples + kOs[quality].latency
    int    latencySamples() const noexcept FCDSP_NONBLOCKING;
    double tailSeconds(const BlockParams&) const noexcept;          // desc.tailSeconds + latency/fs (not RT)
    // audio thread. FCDSP_NONBLOCKING = [[clang::nonblocking]] where supported (03 §2.10 rtsan); EngineHost.cpp repeats
    // it on every definition (FZ0 errata, R-F0 #1: reset and the any-thread calls too)
    void process(const ProcessIo&, const BlockParams&) noexcept FCDSP_NONBLOCKING;
    void reset() noexcept FCDSP_NONBLOCKING;
    // any thread, the audio thread included: lock-free
    void requestSnap() noexcept FCDSP_NONBLOCKING;     // release store; consumed (acquire) at the next block START
    void setUiAttached(bool attached) noexcept FCDSP_NONBLOCKING;   // editor ctor(true)/dtor(false); a COUNT
    bool readUiFrame(UiFrame&) const noexcept FCDSP_NONBLOCKING;    // ≤ 8 seqlock attempts
    const HistoryRing& history() const noexcept FCDSP_NONBLOCKING;
    void setTap(TestTap*) noexcept FCDSP_NONBLOCKING;  // probes only; nullptr = off; loaded once per block (K1 #6, K2 #2)
};
}
```

`Source/fcdsp/engine/TestTap.h` — always compiled, never behind a flag; the plugin never calls `setTap`, and a
`lint.deps` row expects 0 hits for `FCMP_TEST_TAP`:

```cpp
namespace fcdsp {
struct TestTap {                                       // per base-rate sample of the INCOMING path; empty span = not tapped
    std::span<simd::f32x4> grDb, detDb, tgtDb, s2GrDb; // engine lanes {c0, c1, aux0, aux1}
    std::span<uint8_t>     bits;                       // ControlIo::bits
    uint64_t firstSample = 0;                          // absolute sample index of element 0 (set by the probe)
    uint64_t written = 0;                              // elements written (EngineHost advances; stops at capacity)
};
}
```

The host holds `std::atomic<TestTap*>`, loads it once per block and forwards the spans through `ControlIo`'s nullable
outputs; `ControlIo` gains no member.

**Per-block order in `process`.** Numbers are the steps; indentation shows what runs per chunk.

0. Open `ScopedFtz`. If the host is not configured, copy input to output (HR's unprepared guard, B §1.7). **Sanitise** main and key into host scratch (`sanitize`, §5.1) before anything else touches them.
1. **Block start.** Consume the snap flag (acquire). `eng.lookMs = min(eng.lookMs, budgetMs(cfg.budget))` (defensive; the resolver already clamped, §4.4). Compute the incoming `KernelKey = {slot, topo, det, stmode, voice, keyExt}`, where `keyExt` = `extKey` ∧ a key bus is active (K2 #22).
   - If the key differs from the incoming path's key: start a crossfade (§5.5), unless a fade is running or fewer than `kMinFadeGapMs` = 50 ms of samples have passed since the last fade start; then latch the request (latest wins).
   - The incoming path takes this block's `eng`; the outgoing path keeps its frozen `eng`. Set each path's `gain` smoother target `{preGainDb, makeupDb + engine->autoMakeupDb()}`. Host `mix_` target = the incoming `eng.mix`.
2. Then, for each chunk of `n ≤ 64`:
   a. **Route.** Mono input is duplicated. SC source = key (if `keyExt`) else main.
   b. **SC filters, host-owned.** A 2-pole Butterworth TPT SVF HPF with an exact bypass at OFF, then the `sce` tilt: 6 first-order sections pivoted at 1 kHz (E §8).
   c. **Lookahead.** Main is delayed by `L_la`. SC is delayed by `L_la − look + D_up(quality) − engine.scDelaySamples()`, floored at 0 (K2 #11b, #21a). Moving `look` never changes latency.
   d. **Upsample once** (K2 #3e): `osMain = os.up(delayed main)`. This is the dry signal too; **it is never pre-gained**.
   e. **Per path** (1, or 2 while fading):
      - SC for this path: × `linFromDb(path.preGainDb)` when the SC is internal (a key is never pre-gained), then encode per the path's `stmode`;
      - `control()` → `grDb[n]` (+ `bits`, `detDb`, `tgtDb`, `s2GrDb` for telemetry and the tap on the incoming path);
      - encode `osMain` at the OS rate;
      - gain, **specified** (K2 #11c): OS sample `F·n + k` takes `grI = gr[n−1] + (k/F)·(gr[n] − gr[n−1])` and `g = linFromDb(preGainDb − grI)`, with `preGainDb` from the path smoother. preGain is folded into the path's gain, so it reaches the wet path only (K2 #3c);
      - `colour()`; the colour policy applies `+driveDb` before its shaper and `−driveDb` after it, so drive is level-compensated (§3.1);
      - makeup: × `linFromDb(makeupTotalDb)` from the path smoother;
      - decode at the OS rate.
   f. **Blend.** `wet = (1 − w)·wetOld + w·wetNew`, where w is linear per OS sample over 20 ms: equal gain, because the two paths are coherent (E §5.1).
   g. **Mix inside the OS domain** against `osMain` (E §5.3): `y = mix·wet + (1 − mix)·osMain`, where `mix` may exceed 1 on Modes that allow it.
      - Delta: `y = mix·(osMain − Σ_p w_p·wet_p·linFromDb(−preGainDb_p − makeupTotalDb_p))`: what the gain reduction (and colour) removed.
   h. **Downsample once**: `out = os.down(y)` (includes the STD Thiran section, §5.6).
   i. **Bypass.** A 20 ms linear ramp against the input delayed by `latencySamples()` at base rate. It is bit-exact at both ends (HR B §1.6).
      - SC listen: the post-filter SC, decoded, delayed by the latency, 20 ms ramp, unity gain (E §8).
   j. **Telemetry accumulation** runs only while attached.
3. Poison check (§5.8). Publish the `UiFrame` and cut history columns while attached. Meter values are floored at −200 dB and clamped at +200 dB before publishing.

### 5.5 Mode and kernel switching (E §5.1, K2 #3)

```cpp
namespace fcdsp {                                     // engine/host/Crossfade.h
struct KernelKey { uint8_t slot, topo, det, stmode, voice, keyExt; bool operator==(const KernelKey&) const = default; };
struct PathState {                                    // one per arena slot, owned by EngineHost
    IEngine*     engine = nullptr;
    KernelKey    key{};
    EngineParams eng{};                               // incoming path: updated every block; outgoing: FROZEN at its last value
    Smoother4    gain{};                              // {preGainDb, makeupTotalDb (= makeupDb + engine->autoMakeupDb()), –, –}
};
inline constexpr float kFadeMs = 20.f;
inline constexpr float kMinFadeGapMs = 50.f;          // between crossfade STARTS, counted in samples (deterministic)
}
```

1. The request (a new `KernelKey`) is seen at a block start.
2. `bySlot(slot)->construct(idleArena)` placement-constructs the engine. Then `prepare`, `setParams`, `snapParams`. This is bounded and allocation-free. The new path's `gain` smoother is snapped to its targets.
3. `seed(active->carry())`. The new ballistics take the old applied GR per lane, the new detector takes the old level in its own domain (dB → mean-square for RMS, → G for opto through the inverse law), and the new stage 2 takes `Carry::s2GrDb` through `Stage2::seed` (FZ0 errata, R-F0 #3), so a switch with stage 2 engaged does not restart it from 0 dB. Every call in steps 2–5 is `FCDSP_NONBLOCKING` (`construct` through the `ModeEntry` pointer, the `IEngine` virtuals, `~IEngine`; R-F0 #1). **Domain rule:** if `carry.msDomain` differs from the new engine's lane domain, every lane of `grDb`, `detDb` and `s2GrDb` takes `max(lane0, lane1)` of the old engine first, which never under-compresses (K2 #3d).
4. Both run for 20 ms (`kFadeMs`). The outgoing path keeps running on its frozen `eng` and its own preGain and makeup, so neither path is driven by the other Mode's gain staging (K2 #3a–b). `mix` stays host-level (the incoming value), because it is applied after the blend. A newer request during the fade, or within `kMinFadeGapMs` of the last start, is latched, and only the latest is kept.
5. The old engine is destroyed with a plain `~IEngine()` and the path pointers are swapped.

The kernel key includes `stmode`, so a Lat/Vert or M/S change crossfades as well, and `keyExt`, so toggling EXT or the
host (de)activating the key bus never flips an FB kernel without a fade (K2 #22). Each path encodes and decodes for
itself (§5.4 e), so the two paths blend in L/R. `dsp.switch` covers, besides the Mode pairs: an `stmode` flip within
Clean (ST → M/S), FET 76 ↔ Clean at mix 0.5, and an auto-makeup Mode ↔ a manual one.

### 5.6 Latency, oversampling, quality (Source/fcdsp/engine/Oversampler.h)

```cpp
namespace fcdsp {
struct OsDesign { int factor; int latency; int dUp; };   // latency, dUp: BASE-rate samples, integer, constexpr
inline constexpr OsDesign kOs[3] = {
    { 1, 0, 0 },                             // ECO: no OS; colour runs ADAA-1 on the nonlinear RESIDUAL at base rate (E §2.9)
    { 2, kStdLatency, kStdUpDelay },         // STD: 2× polyphase allpass IIR halfband (constexpr coefficients) + a constexpr
                                             //      first-order Thiran allpass at base rate inside down(), so the up→down
                                             //      group delay equals kStdLatency within 0.01 samples up to 1 kHz (K2 #11a)
    { 4, kHqLatency, kHqUpDelay },           // HQ : 4× = two cascaded linear-phase FIR halfbands, integer delay by construction
};
class Oversampler {                          // FZ0 errata (R-F0 #1): reset/up/down are FCDSP_NONBLOCKING
public:
    void configure(Quality, int maxBaseBlock, int channels);            // allocates
    void reset() noexcept FCDSP_NONBLOCKING;
    int  up(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING;   // n·factor; ONCE per chunk
    void down(const float* const* osIn, int nOs, float* const* out) noexcept FCDSP_NONBLOCKING;
};
inline int lookaheadSamples(LookaheadBudget b, double fs) noexcept FCDSP_NONBLOCKING {   // ceil(ms·fs/1000); 0 when off
    return b == LookaheadBudget::off ? 0 : int(std::ceil(budgetMs(b) * fs / 1000.0)); }
}
```

- **Policy (E §5.2):** `latency = lookaheadSamples(budget, fs) + kOs[quality].latency`. It is reported through `setLatencySamples` from `prepareToPlay` (right after `configure`) and from `SetupWatcher` — never from the audio thread (K2 #6). `proc.latency.<key>` sets `quality` from a non-message thread while processing and requires `getLatencySamples()` to be correct within 100 ms, with 0 allocations and 0 locks on the audio thread.
- **`dUp`** is the integer base-rate delay of the up stage (ECO 0, STD 2, HQ per design). The SC delay adds it (§5.4 c), so GR for base sample n lands on the upsampled audio that represents sample n at every Quality; `dsp.time.<key>` requires τ measured at ECO, STD and HQ to agree within 1.5 base samples (K2 #11b).
- **`kStdLatency`, `kStdUpDelay`, `kHqLatency`, `kHqUpDelay` are frozen at FZ2** (end of the oversampler spike, F6, 03 §4.9) and are v1-forever from then.
- **FZ2 values (lead revision, 2026-09-23, from F6):** `kStdLatency = 4`, `kStdUpDelay = 2`, `kHqLatency = 61`,
  `kHqUpDelay = 33`. STD: 2× polyphase IIR (up 8 coefficients, 103.7 dB; down 7, 90.8 dB; passband to 20 kHz at
  44.1 kHz) plus a centred first-order Thiran section (D = 0.5365); group-delay error ≤ 0.00502 samples to 1 kHz at
  44.1–192 kHz; τg(10 kHz) = 5.227 samples at 44.1 kHz. HQ: two cascaded linear-phase FIR halfbands,
  (61+51)/2 + (11+9)/4 = 61 exactly; worst images 94.2 dB, aliases 79.9 dB to 20 kHz. Rounding the up-stage delays moves
  side-chain alignment by 0.16 (STD) / 0.25 (HQ) samples. **Latency probes must measure STD by LF phase or group delay,
  never by impulse peak (it peaks at sample 5) or broadband cross-correlation.**
  - Targets are ≤ 4 and ≤ 64 samples, from E's measurements of JUCE's equivalents: 4 (3.14 + Thiran) and 61.
  - `dsp.os` asserts measured == `kOs[q].latency` and reports τg at 10 kHz; `proc.osref` compares passband and image rejection with JUCE only, never latency (K1 #29).
- **Why not JUCE [DECIDED].** Mix, crossfade and colour all run at the OS rate inside `EngineHost`. Putting `juce::dsp::Oversampling` there would make `fcdsp` depend on JUCE, and probes would stop testing the shipped object code. Owning the filters also keeps the coefficients constexpr, which is bit-exact across arches.
- **Consequence for C §5.8 D8a (mix = 0 null)** (K1 #2, K2 #12). At STD and HQ, `mix = 0` yields `down(up(delayed x))`, not a bit-exact delay. The spec is:
  - ECO: bit-exact against `delay(x, L)`;
  - STD and HQ: bit-exact against a control `Oversampler` round trip of the delayed input at the same Quality, plus passband flatness ≤ 0.01 dB to 20 kHz at 44.1 kHz;
  - bypass: bit-exact at every Quality — the bit-exact "transparent" path is **bypass**.
- **Every Mode runs through the filters at STD** (E §5.2). Only the filter path gives dry/wet phase identity. At ECO, FET colour runs ADAA-1 at base rate; nothing is refused at any Quality (K1 #35).

### 5.7 Denormals and floors (E §3.8)

- `ScopedFtz` is open in `process`.
- Before every `log2` the input is floored: `max(|s|, kLinFloor)` and `max(ms, kMsFloor)`.
- GR-domain states decay toward 0 dB, which is a normal float, so they never go denormal. The linear-domain states (SC filters, RMS, opto G) rely on FTZ.

### 5.8 Poison

Input never carries poison past step 0: `sanitize` (§5.1) replaces NaN/inf and clamps ±1e6 before any delay line,
filter or meter, so the dry delay and the bypass path are finite by construction (K2 #13).

Once per block, check `isfinite` on:
- every active engine's `finite()` (its carry lanes and state);
- the SC filter and oversampler states;
- the last output sample.

On failure:
1. output that block's latency-aligned, sanitised dry signal (the bypass delay line; HR FDNReverb.cpp:1263-1267 pattern);
2. reset every engine, the SC filters and the oversampler (clearing a delay line is allowed and bounded, O(L), but not needed: they hold sanitised input);
3. set `kUiPoisonReset` for that block (also set when `sanitize` replaced a sample).

`dsp.hostile.<key>` feeds NaN, inf and 1e30 on main and key and requires `nonfinite_out = 0` and recovery ≤ 1 block.

---

## 6. Telemetry

### 6.1 `Seqlock<T>` (Source/fcdsp/telemetry/Seqlock.h)

This is HR's `publishUiFrame`/`readUiFrame` pattern (B §3), generalised.

```cpp
namespace fcdsp {
template <class T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 4 == 0);
    static constexpr int kWords = sizeof(T) / 4;
    alignas(64) std::atomic<uint32_t> seq_{0};
    std::array<std::atomic<uint32_t>, kWords> words_{};
public:
    // single writer: memcpy→uint32_t[]; seq+1 (release, odd); release fence; relaxed stores; release fence; seq+1 (even)
    void publish(const T&) noexcept FCDSP_NONBLOCKING;          // FZ0 errata (R-F0 #1)
    // any reader, ≤ 8 attempts: acquire seq (skip odd); relaxed loads; acquire fence; relaxed re-check; memcpy.
    // false = contention: caller keeps its previous frame
    bool read(T&) const noexcept FCDSP_NONBLOCKING;
};
}
```

### 6.2 `UiFrame` (Source/fcdsp/telemetry/UiFrame.h) [DECIDED]

This merges E §7 (57 words) and F §7.1 (38 words). It is 72 words: 288 bytes.

```cpp
namespace fcdsp {
enum UiFlag : uint32_t {
    kUiBypassed = 1u << 0, kUiDelta = 1u << 1, kUiListen = 1u << 2, kUiExtKeyActive = 1u << 3,
    kUiMidSide = 1u << 4,       // engine lanes are M/S
    kUiFading = 1u << 5, kUiLookahead = 1u << 6, kUiOutOver = 1u << 7,      // output > 0 dBFS this block
    kUiTopoFB = 1u << 8, kUiGrOff = 1u << 9, kUiAutoSlow = 1u << 10,        // DualRelease slow stage dominant
    kUiRangeLimited = 1u << 11, kUiS2Active = 1u << 12, kUiPoisonReset = 1u << 13,
    kUiLive = 1u << 14,         // input > −70 dBFS or GR > 0.01 dB this block (F: history holds when not live)
    // bits 16–17: phase lane 0, bits 18–19: phase lane 1 (0 idle, 1 attack, 2 hold, 3 release)
};

struct UiFrame {
    // identity and host ─────────────────────────────────────────────────── 8 words
    uint32_t publishCount;
    uint16_t modeSlot;            // slot the audio runs (the incoming one once a fade starts)
    uint16_t fadeFromSlot;        // outgoing slot during a fade; == modeSlot otherwise
    uint32_t flags;               // UiFlag
    float    sampleRate;
    uint32_t latencySamples;
    float    fadeProgress;        // 0…1 kernel crossfade; 1 = none running
    float    bypassAmt;           // bypass ramp position 0 (processed) … 1 (bypassed)
    uint32_t historyWritten;      // low 32 bits of HistoryRing::written() at publish
    // meters, per OUTPUT channel (L/R), dBFS floored at −200; instant attack, 40 ms release (HR) ─ 12 words
    float inPeakDb[2], inRmsDb[2], outPeakDb[2], outRmsDb[2], scPeakDb[2], colourInPeakDb[2];
    // control path, per ENGINE lane (L/R or M/S), end of block; GR ≥ 0 = attenuation ──────── 16 words
    float curveXDb[2];            // operating-point x: plugin-input level in the Mode's detector law (§7)
    float targetGrDb[2];          // static-curve GR at curveXDb (FB: static FB solve)
    float appliedGrDb[2];         // after ballistics, link and stage 2: what multiplies the audio
    float blockMaxGrDb[2];        // max applied GR within the block (short spikes are not lost)
    float s2GrDb[2];              // stage-2 GR, else 0
    float attackNowMs[2];         // effective attack tau (program-dependent Modes)
    float releaseNowMs[2];        // effective release tau
    float crestDb[2];             // CrestAuto, else 0
    // parameters the audio actually used (resolved + smoothed) ───────────────────────────────── 20 words
    float preGainDb, thrDb, slope, kneeDb, rangeDb, atkTauMs, relTauMs, holdMs, lookMs,
          driveDb, makeupEffDb /*incl. auto*/, mix, scHpfHz, sceDbOct, link, s2ThrDb, s2AtkTauMs, s2RelTauMs;
    uint32_t tags;
    uint32_t discrete;            // det | stmode << 8 | voice << 16 | tmode << 24
    // Mode internals; meanings in ModeDescriptor::internals (≤ 8 declared; words 8–15 reserved) ─── 16 words
    float internals[kInternals];
};
static_assert(std::is_trivially_copyable_v<UiFrame> && sizeof(UiFrame) == 72 * 4);

// Live curves (K1 #7, K2 #24). UiFrame does not carry EngineParams::m[8], topo or flags, so the UI (and probe ui.truth)
// never builds an EngineParams from the frame alone. It runs resolve() on the current raw values (cached by hash), then
// copies the smoothed continuous fields (preGainDb … s2RelTauMs; makeupEffDb → makeupDb with kEngAutoMakeup cleared,
// because makeupEffDb already includes the auto part) onto that result. The caller skips the overlay when
// frame.modeSlot != the resolved slot or kUiFading is set.
void overlaySmoothed(const UiFrame&, EngineParams& inOut) noexcept;
}
```

- It is published **once per `process` call** at the end, while the attach count is > 0.
- The meter envelopes use a block-length-compensated coefficient. That is allowed for telemetry only (E §1).
- The editor treats the stream as stale 0.5 s after `publishCount` stops moving. It then falls back to parameter-derived drawing and shows absent values, never guessed ones (HR B §3, F §7.1).

### 6.3 `HistoryRing` (Source/fcdsp/telemetry/HistoryRing.h) [DECIDED]

This merges E §7 (min/max columns) and F §7.2 (32 B points, 4096 deep). A column is **1 ms of audio time**. Columns are cut at absolute sample boundaries with a fractional accumulator, so at 44.1 kHz they are 44 or 45 samples. The strip is therefore independent of block size.

```cpp
namespace fcdsp {
struct HistoryColumn {            // 32 bytes
    float inPeakDb;               // max over the column, both channels
    float outPeakDb;
    float detMaxDb;               // max curve-axis level
    float grMaxDb;                // most applied GR (≥ 0)
    float grMinDb;                // least applied GR: max and min keep 1-ms spikes both ways (E §7)
    float tgtMaxDb;               // max static target GR
    float internal0;              // the Mode's history-flagged internal (last value), else 0
    uint32_t bits;                // b0–1 phase at the max-GR sample; b2 auto-slow; b3 range-limited; b4 s2 active;
                                  // b5 first column after attach (gap); b6 fading; b8–15 mode slot
};
static_assert(sizeof(HistoryColumn) == 32);

class HistoryRing {
public:
    static constexpr uint32_t kCapacity = 4096;          // 4.1 s; power of two; 128 KiB of atomic words
    // every member FCDSP_NONBLOCKING (FZ0 errata, R-F0 #1)
    void push(const HistoryColumn&) noexcept;            // audio thread ONLY (claim-word protocol below)
    uint64_t written() const noexcept;                   // acquire
    // Reader (one editor): copies columns [from, written()) into out, oldest first, at most out.size().
    // Returns the index of the first column delivered. It is > from if the writer lapped the reader (gap).
    // Columns the writer may have overwritten during the copy are dropped (claim check below), never delivered torn.
    uint64_t read(uint64_t from, std::span<HistoryColumn> out, uint32_t& count) const noexcept;
private:
    std::array<std::atomic<uint32_t>, kCapacity * 8> words_{};
    alignas(64) std::atomic<uint64_t> written_{0};
    alignas(64) std::atomic<uint64_t> claim_{0};         // index of the column being written (K2 #8)
};
}
```

**Tear-free protocol (K2 #8).** Draft 1's "re-read `written()` after the copy" accepted a half-overwritten column;
this is the claim-word seqlock variant:

```cpp
void HistoryRing::push(const HistoryColumn& c) noexcept {        // audio thread
    const uint64_t j = written_.load(std::memory_order_relaxed);
    claim_.store(j, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);          // the claim is ordered before the data
    /* 8 relaxed word stores into slot j & (kCapacity − 1) */
    written_.store(j + 1, std::memory_order_release);
}
// read(): w = written_.load(acquire); copy [max(from, w − kCapacity + 1), w) with relaxed loads;
//         std::atomic_thread_fence(acquire); c = claim_.load(relaxed);
//         keep column i iff i + kCapacity > c   (any overwrite the reader saw implies claim ≥ its index + kCapacity)
```

`dsp.telemetry` detects tears: its writer fills every word of a column (and of a `UiFrame`) with a function of the
column index (or `publishCount`), and the reader asserts consistency over 10⁷ reads, under `tsan` too.

**Writer rules:**
- Accumulate and push only while attached.
- On a 0→1 attach transition, reset the column accumulator and the meter envelopes, and set `bits.b5` on the next column.
- `written_` is monotonic for the life of the host object and is never reset.

**Reader rules:**
- The editor drains once per frame, starting from its `lastRead`.
- A lap produces a gap marker in the UI-side store, `HistoryStore`: 1 ms `HistoryColumn` × 20 480 entries (20.48 s, 655 KB per editor), which rebuilds the columns exactly (02 §9.6; K1 #21).
- `UiFrame::historyWritten` gives a cheap splice check.

**Editor-lifetime gate:**
- `setUiAttached(true)` in the editor constructor and `(false)` in the destructor, as HR does (B §3).
- It is a **count**, because a host can hold two editors of one instance.
- It is keyed to lifetime, not visibility.

---

## 7. Analysis API (Source/fcdsp/analysis/Analysis.h)

The functions are pure and use the **same policy code the audio thread runs** (E §6). They run on the message thread or `PreviewWorker` and may allocate. **Every entry point opens `fcdsp::ScopedFtz`** (K2 #24), so the UI computes what the probes (which also run under FTZ) check. Every function takes an `EngineParams`: callers resolve first (and overlay the live smoothed fields with `overlaySmoothed`, §6.2), which is one line.

```cpp
namespace fcdsp::analysis {
struct CurveOpts { bool colour = false; bool stage2 = true; };   // colour: add the describing-function gain (E §6.3)

// Axis contract [DECIDED]: x = PLUGIN-INPUT level (dBFS) in the Mode's DetectorLaw (peak: sine peak; rms: peak − 3.01 dB).
// gainDb = preGainDb − GR(x + preGainDb) [+ colour DF]. Excludes makeup, mix and output.
// TRANSFER draws y = x + gainDb − preGainDb (GR only); the net curve draws y = x + netGainDb(gainDb, makeupEffDb, mix)
// (K1 #22; probe ui.curve uses the same formulas).
void staticGain(const ModeEntry&, const EngineParams&, std::span<const float> xDb, std::span<float> gainDb,
                CurveOpts = {}) noexcept;
// The spec probes' function: GR at the gain computer for DETECTOR-domain x (after preGain). FF: Computer::target over
// four abscissae per call, bit-identical to the DSP symbol (E §6.5). FB: Computer::solveFb with FbAffine{0, 1};
// non-closed-form laws iterate to convergence (≤ 6 Newton steps, bisection bracket [0, max(0, x − T + W)]).
void staticGr(const ModeEntry&, const EngineParams&, std::span<const float> xDetDb, std::span<float> grDb) noexcept;
float localRatio(const ModeEntry&, const EngineParams&, float xDb) noexcept;   // 1/(1 − d(GR)/dx), central diff ±0.05 dB
float netGainDb(float gainDb, float makeupEffDb, float mix) noexcept;          // 20·log10(mix·10^((g+mk)/20) + 1 − mix)
// Input-referred threshold T_in, where the TRANSFER threshold handle and the HISTORY threshold line sit (K1 #9).
// T_in is affine in thr with slope 1 in every Mode (registry lint: |dT_in/dthr − 1| ≤ 1e-4 at 5 points per ratio step),
// so a threshold handle drag writes thr_new = thr_cur + (T_target − T_cur).
inline float inputThresholdDb(const EngineParams& e) noexcept { return e.thrDb - e.preGainDb; }

struct StepStimulus {                  // level-domain stimulus injected AFTER the host SC filters (peak == RMS)
    float fs = 48000;
    float preSec = 0.05f;              // silence
    // Levels are relative to eng.thrDb in the DETECTOR domain (after preGain), because the stimulus is injected after
    // the SC filters (K1 #35).
    float hiDbOverThr = 12, hiSec = 0.5f;
    float loDbUnderThr = 12, loSec = 2.0f;
    int   decimate = 1;                // write every n-th sample (min/max decimation is the caller's)
};
// Runs a PRIVATE ModeEngine (constructed into a local 64-aligned kArenaBytes buffer, with a heap scratch sized as the
// host's) on the stimulus and writes the
// applied GR (≥ 0 dB). Bit-identical to the plugin rendering the same burst (E §6.5). Returns samples written.
int stepResponse(const ModeEntry&, const EngineParams&, const StepStimulus&, std::span<float> grDbOut);
// Time readouts from a response: t63 / t10–90 / t50 per law, for the TIME view and D2 cross-checks.
struct TimeReadout { float attackS, releaseS; TimeLaw law; };
TimeReadout measure(std::span<const float> grDb, const StepStimulus&, TimeLaw) noexcept;

// Detector-path magnitude: host SC HPF + sce tilt + the Mode's internal SC shaping (R37, 33609 SLOW HP, Thrust).
void scResponse(const ModeEntry&, const EngineParams&, float fs, std::span<const float> hz, std::span<float> magDb) noexcept;
// Colour-stage static transfer at a given GR (the COLOUR view); harmonics of shape(A·sin), 64-point DFT.
void colourCurve(const ModeEntry&, const EngineParams&, float grDb, std::span<const float> x, std::span<float> y) noexcept;
// h[k] = harmonic k+1 in dB relative to the fundamental (h[0] = 0): H2…H5 = h[1..4], THD from h[1..7] (02 §9.7 #8).
void harmonicsDb(const ModeEntry&, const EngineParams&, float grDb, float amp, std::span<float, 8> h) noexcept;
}
```

- **Band vs full-panel Characteristics screen.** The user decided on both. They call the same functions.
  - The band uses `staticGain`, `localRatio`, `netGainDb` and the telemetry.
  - The full panel adds `stepResponse`, which runs on `PreviewWorker` and is throttled to ≤ 20 Hz during drags (F §4.3), plus `scResponse`, `colourCurve`, `harmonicsDb` and the single history internal lane (`HistoryColumn::internal0`, K1 #20).
- **Why `stepResponse` is a shipping API, not a test tap:** the UI calls it (F §7.3). Its bit-identity with the plugin render is a D-probe row.

> **S10 interface revision (X10; F8's request, S5 Outcome).** `CurveOpts::stage2` is honoured: `staticGain` passes the
> computer's GR through `ModeEntry::staticS2` (§8.2) at the same detector-domain abscissae, before the range clamp and
> GR OFF (the order `control()` applies them). A new overload
> `void staticGr(const ModeEntry&, const EngineParams&, std::span<const float> xDetDb, std::span<float> grDb, CurveOpts) noexcept;`
> does the same for GR (`opts.colour` is ignored; `stage2 = false` is the four-argument form, which stays the
> computer alone for the spec probes). With `staticS2 == nullptr` (`NoStage2`, every registered kernel in the S10
> base) stage 2 is the identity: the curves are bit-identical to S9's. `dsp.analysis` `stage2.*` rows.

---

## 8. Mode registry

### 8.1 `Modes.def` (append-only; v1-forever once shipped)

```
// Source/fcdsp/modes/Modes.def — ONE line per slot, APPEND ONLY. A slot and a key are never reused.
// Edited ONLY by the lead in a sprint base, or by that sprint's single descriptor-wave task (K3 #8). A slot is fixed
// the moment its line first appears; Mode DSP tasks never touch this file.
//        slot  key            Traits   (files: modes/<key>/<Traits>.h, <Traits>Desc.cpp, <Traits>.cpp)
FCMP_MODE(0,   "clean",       Clean)
FCMP_MODE(1,   "bus-g",       BusG)
FCMP_MODE(2,   "fet-76",      Fet76)
FCMP_MODE(3,   "opto-2a",     Opto2A)
FCMP_MODE(4,   "mu-67",       Mu67)
FCMP_MODE(5,   "diode-609",   Diode609)
FCMP_MODE(6,   "bus-25",      Bus25)
FCMP_MODE(7,   "brickwall",   Brickwall)
// Retired Modes keep their slot:  FCMP_RETIRED(slot, "key", "successor-key")
```

Grammar (build-facing, 03 §2.9): `FCMP_MODE(<slot>, "<key>", <Traits>)` or `FCMP_RETIRED(<slot>, "<key>",
"<successor-key>")` starting at column 0; anything else is a comment or blank. A malformed `FCMP_` line is a configure
error. The quoted key keeps the X-macro valid C++ (C §5.10's bare `fet-76` would tokenise as `fet - 76`). A commented
reservation (`// FCMP_MODE(…)`) is skipped by the `^FCMP_` regex; it is the fallback for a lone task that must add a
brand-new Mode outside a descriptor wave (03 §4.2).

### 8.2 Registry (Source/fcdsp/modes/Registry.h, DefineMode.h)

A Mode file never knows its slot, and no TU instantiates every Mode (K3 #6). Each Mode's `<Traits>.cpp` ends with one
macro that instantiates its engine and analysis entry points **in that TU**; `Registry.cpp` only collects addresses.

```cpp
namespace fcdsp {
inline constexpr int kModeCapacity = 128;
struct ModeEntry {                                        // one per Mode, defined by FCDSP_DEFINE_MODE in the Mode's TU
    const ModeDescriptor* desc;
    IEngine* (*construct)(void* arena) noexcept FCDSP_NONBLOCKING;   // placement-new ModeEngine<T>; RT-safe (R-F0 #1)
    uint32_t engineBytes, engineAlign;
    void (*staticGr)(const EngineParams&, const float* xDetDb, float* grDb, int n) noexcept;
    void (*scShapeDb)(const EngineParams&, float fs, const float* hz, float* magDb, int n) noexcept;
    void (*colourCurve)(const EngineParams&, float grDb, const float* x, float* y, int n) noexcept;
    void (*staticS2)(const EngineParams&, const float* xDetDb, const float* r1Db, float* grDb, int n) noexcept
        = nullptr;                                        // S10 interface revision (X10): see below
};
struct ModeSlot { uint8_t slot; std::string_view key; const ModeEntry* entry; };   // registry-owned
struct Retired  { uint8_t slot; std::string_view key, successor; };

// FZ0 errata (R-F0 #1): every lookup is a constant-table read and FCDSP_NONBLOCKING (Registry.cpp repeats it).
std::span<const ModeSlot> modeSlots() noexcept FCDSP_NONBLOCKING;   // assigned slots, slot order (FZ0 errata: was modes(), which clashes with namespace fcdsp::modes)
const ModeEntry* bySlot(int slot) noexcept FCDSP_NONBLOCKING;               // nullptr: unassigned or retired
const ModeEntry* byKey(std::string_view key) noexcept FCDSP_NONBLOCKING;    // registered keys only
const ModeSlot&  resolveSlot(int rawSlot) noexcept FCDSP_NONBLOCKING;       // O(1) constexpr map: retired → successor; unassigned → clean
const ModeSlot*  resolveKey(std::string_view key) noexcept FCDSP_NONBLOCKING;   // registered, or retired → successor; nullptr if unknown
int              slotOf(const ModeEntry&) noexcept FCDSP_NONBLOCKING;       // for ParamView::slot and telemetry
std::span<const Retired> retired() noexcept FCDSP_NONBLOCKING;
}

// Source/fcdsp/modes/DefineMode.h — frozen at FZ0. Used as the last line of modes/<key>/<Traits>.cpp:
#define FCDSP_DEFINE_MODE(Traits)                                                                           \
    namespace fcdsp::modes {                                                                                \
    static_assert(sizeof(::fcdsp::ModeEngine<Traits>) <= ::fcdsp::kArenaBytes &&                            \
                  alignof(::fcdsp::ModeEngine<Traits>) <= 64, #Traits " does not fit the engine arena");    \
    extern const ::fcdsp::ModeEntry kEntry_##Traits;                                                        \
    constinit const ::fcdsp::ModeEntry kEntry_##Traits = ::fcdsp::makeModeEntry<Traits>();                  \
    }                                                                                                       \
    static_assert(sizeof(::fcdsp::modes::kEntry_##Traits) != 0, "use FCDSP_DEFINE_MODE at global scope")
// FZ0 errata: a namespace-scope const has internal linkage, so the entry is declared extern and defined constinit
// inside namespace fcdsp::modes; the macro is used at GLOBAL scope and ends with a semicolon: FCDSP_DEFINE_MODE(Clean);
// makeModeEntry<T>() is a constexpr inline template in DefineMode.h that fills the ModeEntry from ModeEngine<T>.
```

> **S10 interface revision (X10).** `ModeEntry::staticS2` (trailing, default `nullptr`, so the entry stays an
> aggregate and constant-initialised) is the settled stage 2 after the computer: `grDb[i]` from the stage-1 GR
> `r1Db[i]` at detector level `xDetDb[i]`, `grDb` may alias `r1Db`. `makeModeEntry<T>()` sets it to
> `&ModeEngine<T>::staticS2` when `T::Stage2` has `combineStatic` (§5.2 note, `HasCombineStatic`), else `nullptr`
> (`detail::staticS2Of<T>()`): a `nullptr` means no static stage 2, i.e. the identity (§7).

`Registry.cpp` (in namespace `fcdsp::modes`) expands `Modes.def` three times:
1. `#define FCMP_MODE(s, k, T) extern const ModeEntry kEntry_##T;` — declarations only;
2. into `constexpr ModeSlot kSlots[] = { {s, k, &kEntry_##T}, … }` — address constants, so the table is
   constant-initialised and C D12 holds;
3. into the constexpr `std::array<uint8_t, 128>` slot map (retired → successor, unassigned → clean).

There is no lazy initialisation and there are no function-local statics (C D12). A Mode edit recompiles one TU plus
`Registry.cpp`'s trivial table, which keeps 03 §2.11's incremental estimate honest. An unregistered Mode's TU may
exist in a worktree; nothing references its entry, so the linker drops it.

### 8.3 CMake and the registry lint

`Modes.def` drives **only** the registry (above) and the CTest matrix (03 §2.9): `FcmpProbes.cmake` parses it into
`FCMP_MODE_KEYS`. Sources are collected by the per-directory globs of 03 §2.1, so there is no `target_sources` loop and
no generated `ModeIncludes.h` (K3 #2, #6; this supersedes Draft 1's §8.3 and K1 #5, see `ADR-19`).

**The `dsp.registry` lint** — spec rows only, no golden rows (K3 #17). This is the canonical list; 03 §3.4 refers to
it. It asserts:
- The CMake key list equals `modeSlots()`. Keys are unique and match `[a-z0-9-]{1,24}`; each Mode's directory name equals its key and `desc.key`. No key or slot is reused against `tests/fixtures/modes-ever.tsv`, retired ones included. Slots are unique and < 128. Retired keys have a registered successor.
- `kApvtsOrder` is a permutation of every `Pid` (K2 #9).
- Every step list is strictly increasing with non-empty labels; a label > 6 glyphs prints a **warning** (the gate is `ui.textfit`, K1 #24). Every non-live spec has a `reason`.
- Variant drivers come earlier in `kResolveOrder`. Derived specs do not depend on derived specs.
- Every `DisplayMap` round-trips to within 1e-4, and `invert` agrees with the sign of the slope.
- `defaultPlain` is a fixed point of `snap` (it resolves to itself).
- The non-nullable pointers are set: `detectorLaw`, `attackSpec`, `releaseSpec`, `tailSeconds`, and `ModeEntry::{construct, staticGr, scShapeDb, colourCurve}`. `physical` may be `nullptr` (K1 #30).
- `internals.size() ≤ 8`, and at most one `InternalSpec` has `history == true` (K1 #20).
- `introducedInStateVersion ≤ kStateVersion`; `revision ≥ 1`; if `FCOMPRESSOR_RELEASE=ON`, no Mode is `provisional`.
- **`crossmode.no_off`** (K2 #4 ii): for every ordered pair (A, B), `modeDefaults(A)` applied over the host defaults and resolved in B gives no `kTagOff` step, no `kEngGrOff`, and `staticGr(T + 12 dB) > 0` unless B is limiter-only.
- **`fb.monotone`** (K2 #5a): every FB Mode's `r̂_fb` is non-decreasing on y ∈ [−80, +40] dB at every step, sampled every 0.1 dB.
- **`thr.slope`** (K1 #9): `|dT_in/dthr − 1| ≤ 1e-4` at 5 points per Mode and per ratio step (`analysis::inputThresholdDb`).
- Every `tests/golden/*/modes/<key>/` directory belongs to a registered or retired key (C §5.10.4).

Engine-fits-arena is a compile-time `static_assert` in `FCDSP_DEFINE_MODE`, not a lint.

### 8.4 Adding a Mode: checklist

A Mode is added in two tasks: its descriptor (in a descriptor wave, DW, 03 §4.9) and its DSP (a Mode task).
1. **Descriptor (DW task or lead):** `Source/fcdsp/modes/<key>/<Traits>Desc.cpp` with the step tables, the `ParamTable`, `physical()`, the time-spec functions, `InternalSpec[]` and the `constexpr ModeDescriptor` (`provisional = true`); `<Traits>.h` with **generic traits** built from existing policies (FB Modes use `QuadKnee::solveFb`); `<Traits>.cpp` ending in `FCDSP_DEFINE_MODE(<Traits>)`; the `FCMP_MODE(slot, "key", Traits)` line. `dsp.registry`, `dsp.quant.<key>` and the text round trip must pass.
2. **DSP (Mode task, owns `modes/<key>/**` plus any new `stages/<slot>/<Policy>.h` named in its manifest):** replace the generic traits with the real policies, clear `provisional`, fit the [H] constants.
3. New stage policies get unit rows in their own probe files `Tools/probes/dsp/<policy>.cpp`, owned by the task that adds the policy (03 §4.6).
4. Nothing in CMake, the UI, the probes or CTest changes: they are descriptor- and `Modes.def`-driven. Spec checks run immediately; missing goldens exit 3 (`golden_missing`, 03 §3.2.4) and are reported as candidates. The lead blesses (C §6.5).
5. Factory presets for the Mode, once the bank exists (P3), go in `Source/plugin/factory/<key>.inc`, owned by the Mode task; no shared file is edited (§9.2).
6. The Mode sheet `docs/modes/<key>.md`, owned by the Mode task, lists the [H] constants, their sources, and every `revision` bump.

---

## 9. State and presets

### 9.1 Session state (the plugin layer; `plugin/State.{h,cpp}`)

```xml
<PARAMS stateVersion="1" modeId="fet-76" modeRev="1" product="FCompressor" build="0.1.0">
  <PARAM id="thr" value="-24"/>                          <!-- APVTS children: one per host parameter, PLAIN units -->
  <PARAM id="mode" value="2"/>                            <!-- the slot, informational: modeId wins -->
  ...
  <PRESET uuid="…" name="…" …> <PARAM id="…" value="…"/> … <ATTR key="modeId" value="…"/> </PRESET>   <!-- via hooks -->
  <UI charExpanded="0" scTab="sidechain"/>                <!-- per-instance editor state, never parameters (K1 #14) -->
</PARAMS>
```

- **`stateVersion`** (`kStateVersion = 1`) is bumped **only** when the meaning of a stored plain value changes, or a parameter is renamed or removed. Adding a parameter never bumps it (absent means default: HR B §1.8, the HR `PresetTypes.h` rule).
- **`modeRev`** is the saved Mode's `ModeDescriptor::revision` (K2 #10). `stateVersion` cannot express a per-Mode sound change; `modeRev` can.
- **The APVTS** is constructed as `apvts(*this, nullptr, "PARAMS", layout())` — **no `UndoManager`**, as in HR (K2 #7). JUCE's APVTS timer would push every change, host automation included, into one ever-growing transaction, and "undo" would revert host automation. Undo belongs to the host: every UI write is already a `begin/set/end` gesture (02 §5.2), which hosts record. An in-plugin undo, if ever wanted, records `(paramId, before, after)` at `GestureController::endDrag`, capped at 64 entries.
- **`State.cpp` owns `<PARAMS>` and never links FunkPresets** (K3 #14). The `<PRESET>` child goes through an optional pair `std::function<void(juce::ValueTree&)> writePreset, readPreset` that stays null until the preset integration task (P3) installs it. `proc.state` therefore does not depend on FunkPresets.

**Save:**
1. `copyState()`;
2. set `stateVersion`, `modeId` and `modeRev` (from the effective slot), `product` and `build`;
3. `writePreset(tree)` if installed;
4. the `<UI>` child;
5. `copyXmlToBinary`.

**Load (`setStateInformation`)** — bracketed by `beginBatch()` … `endBatch()` (K2 #23), so the audio thread keeps using the previous `BlockParams` until the whole set is written:
1. XML → tree. If the type is not `PARAMS`, ignore it (fail-safe).
2. `v = stateVersion`, where absent means 1 (development sessions).
   - `v > kStateVersion`: load best effort and set `StateNotice::newerSession`. The editor footer says so.
   - Otherwise run `kMigrations[v..kStateVersion−1]`. Each is `void(*)(juce::ValueTree&)`, in `plugin/StateMigration.cpp`.
3. Copy the tree, strip `PRESET` and `UI`, then `apvts.replaceState` (HR B §1.8).
4. Absent → default, for every `kHostParams` entry (HR's loop). Then `listen = 0` and `delta = 0` (K2 #25c).
5. **Mode:** `resolveKey(modeId)`. If there is no `modeId`, fall back to the `mode` parameter value through `resolveSlot`. If the key is unknown or retired, use the successor or `clean` and set `StateNotice::{modeMigrated, fromKey, toKey}` for the footer. Set the `mode` parameter if it differs. It never crashes, and it never silently picks slot 0 for a known key (C §5.7.5).
6. **Revision:** if `modeRev` < the loaded Mode's `revision`, set `StateNotice::{modeRevised, savedRev, currentRev}`; the footer shows `FET 76 UPDATED SINCE THIS SESSION (REV 1 → 2)`. Old behaviour is not emulated in v1.
7. `readPreset(tree)` if installed, then the `<UI>` child, then `endBatch()`, which raises `engine.requestSnap()`. The snap flag is raised **after** the last write and consumed before the next block's parameter pull (HR's `render` ordering fix, PluginProcessor.cpp:373-403). `StateNotice::serial` increments.

### 9.2 Preset contract

**The contract is HR's `presets/*.h`, read on 2026-09-22.** The preset core is not DSP and not FCompressor-specific, so it lives in **FunkGui as a separate target, `FunkPresets` (`FunkGui::presets`, namespace `funkgui::presets`)**, which does not depend on the GUI targets (02 §1.2). **This extends the user's "GUI library" decision and is listed for confirmation** (`DECISIONS.md` open question Q2). Fallback if declined: an FCompressor-owned `Source/plugin/presets/`, re-implemented from HR's headers (not copied). Either way, FCompressor depends only on the contract below, and the task that writes it (G8, 03 §4.9) is scheduled last because HR's `presets/` was being edited during research.

Changes to HR's headers, made only by adding, as their comment requires:

```cpp
namespace funkgui::presets {
struct Attribute { juce::String key, value; };                // NEW: product-defined string attributes
struct Preset {                                               // HR PresetTypes.h fields, unchanged, plus:
    // uuid, name, category, author, notes, isFactory, format = kFormat(1), params (plain units, keyed by id),
    // createdMs, modifiedMs, lastUsedMs, tags  — exactly as HR
    std::vector<Attribute> attributes;                        // FCompressor: { "modeId", "fet-76" }, { "modeRev", "1" }
    const Attribute* attr(const juce::String& key) const noexcept;
};
struct ProductConfig {                                        // NEW: replaces HR's hard-coded literals (B §4.1)
    juce::String productName;     // "FCompressor" → ~/Library/Application Support/FCompressor/Presets.db
    juce::String fileExtension;   // ".fcmppreset"
    juce::String xmlRoot;         // "FCompressorPreset"
    juce::String dbEnvVar;        // "FCMP_PRESETS_DB"
};
struct PresetHooks {                                          // NEW: PresetManager extension points
    std::function<bool(const juce::String& id)> isPresetParameter;
    std::function<void()>              beginApply;            // FCompressor: processor.beginBatch()
    std::function<void(const Preset&)> applyBefore;           // FCompressor: set `mode` from attr("modeId")
    std::function<void(Preset&)>       captureExtra;          // FCompressor: attributes += modeId, modeRev
    std::function<void()>              onApplied;             // FCompressor: processor.endBatch() (raises the snap)
};
// PresetManager(juce::AudioProcessorValueTreeState&, PresetHooks);   PresetStore(const ProductConfig&);
}
```

**FCompressor's use** (`Source/plugin/Presets.cpp` implements the hooks and `fcmp::PresetAccess`, 02 §9.5):
- **Payload.** Parameters are **plain host units keyed by parameter ID** (HR `PresetTypes.h`), plus `attributes["modeId"]` and `attributes["modeRev"]`.
  - The `mode` index is **not** written as a `PARAM`, because `modeId` is authoritative.
  - A missing `modeId` loads `clean`.
- **`isPresetParameter`:** every Mode-filtered parameter (§3.1 `inPresets`). It excludes `mode` (carried by `modeId`), `extkey`, `listen`, `delta`, `bypass`, `quality`, `labudget` and (v1.2) `output`.
- **Apply order:**
  1. `beginApply` opens the batch;
  2. `applyBefore` sets `mode`;
  3. every preset parameter is written, with absent → host default;
  4. `onApplied` closes the batch, which raises the snap.

  Presets store **raw** values. The Mode snaps on read, and stepped values captured through the UI are already exact detents.
- **`isModified`:** normalised tolerance 1e-4, as in HR.
- **File:** `.fcmppreset`, XML: `<FCompressorPreset format="1" uuid name category author notes><ATTR key="modeId" value="…"/><PARAM id value/>…`. Tags and timestamps are never exported (HR `PresetFile.h`).
- **Factory bank** (K1 #31, K3 #18): `Source/plugin/factory/FactoryBank.cpp`, written once by P3, includes the per-Mode files `Source/plugin/factory/<key>.inc` in slot order through a configure-generated `FactoryIncludes.h` (glob + `Modes.def` order, 03 §2.1). Index 0 is "Init" (all defaults, `clean`). UUIDs are fixed in source, in the form `00000000-0000-4000-8000-0000000000NN`. `factoryBankRevision()` returns `FCMP_FACTORY_BANK_REVISION`, the first 32 bits of the SHA-256 of the concatenated `.inc` contents, computed at configure time — never a hand-edited counter.
- **No host programs.** `getNumPrograms() == 1` (§3.1 rules); the bank is reached through FCompressor's own browser. (Draft 1 said "host programs are the factory bank, as HR does"; HR's `getNumPrograms()` returns 1, B §1.7.)
- **Store:** SQLite from the macOS SDK (`find_package(SQLite3)`), message thread only, WAL, with an in-memory fallback (HR `PresetStore.h`). Location: `~/Library/Application Support/FCompressor/Presets.db` or `$FCMP_PRESETS_DB`. SQLite is a system library, not fetched — like the Metal and CoreAudio frameworks; listed for user confirmation with Q2.
- **Sandboxed AU hosts.** `AU_SANDBOX_SAFE` stays `FALSE` (JUCE's default), so sandboxed hosts such as GarageBand load FCompressor out of process, where `~/Library/Application Support` is reachable; the in-memory fallback remains as a safety net (K2 #19).
- **Per-Mode memory** (E §4.3, optional) is **not v1.**

---

## 10. The first Modes

### 10.1 Shared kit (Source/fcdsp/modes/ModeKit.h, header-only)

Every helper is defined inline in the header, so descriptor tables in other TUs can use them in constant initialisers
(K3 #20). `ModeKit.h` is frozen per sprint; Mode-local helpers stay in `modes/<key>/`.

```cpp
namespace fcdsp::kit {
constexpr ParamSpec cont(float lo, float hi, float def, TimeLaw law = TimeLaw::expDb) noexcept
    { ParamSpec s; s.kind = Kind::continuous; s.lo = lo; s.hi = hi; s.defaultPlain = def; s.law = law; return s; }
constexpr ParamSpec stepped(std::span<const Step> st, float def, TimeLaw law = TimeLaw::expDb) noexcept
    { ParamSpec s; s.kind = Kind::stepped; s.steps = st; s.defaultPlain = def; s.law = law; return s; }
constexpr ParamSpec hybrid(float lo, float hi, std::span<const Step> outside, float def, TimeLaw law = TimeLaw::expDb) noexcept
    { ParamSpec s = cont(lo, hi, def, law); s.kind = Kind::hybrid; s.steps = outside; return s; }
constexpr ParamSpec locked(float v, const char* reason, const char* tag = "FIXED") noexcept
    { ParamSpec s; s.kind = Kind::locked; s.value = s.defaultPlain = v; s.reason = reason; s.tag = tag; return s; }
constexpr ParamSpec locked(std::span<const Step> one, const char* reason) noexcept          // list params
    { ParamSpec s = locked(one[0].plain, reason, one[0].label); s.steps = one; return s; }
constexpr ParamSpec derived(float (*fn)(const ParamView&) noexcept, Pid from, const char* tag, const char* reason) noexcept
    { ParamSpec s; s.kind = Kind::derived; s.derive = fn; s.derivedFrom = from; s.tag = tag; s.reason = reason; return s; }
constexpr ParamSpec na(float neutral, const char* reason) noexcept
    { ParamSpec s; s.kind = Kind::notApplicable; s.value = s.defaultPlain = neutral; s.reason = reason; return s; }
// modifiers
constexpr ParamSpec named(ParamSpec s, const char* label, DisplayMap m = {}) noexcept { s.label = label; s.display = m; return s; }
constexpr ParamSpec ext  (ParamSpec s) noexcept { s.flags |= kFlagExtension;  return s; }
constexpr ParamSpec prog (ParamSpec s) noexcept { s.flags |= kFlagProgram;    return s; }
constexpr ParamSpec plotPlain(ParamSpec s) noexcept { s.flags |= kFlagPlotIsPlain; return s; }   // absolute handle drags
constexpr ParamSpec hwrev(ParamSpec s) noexcept { s.flags |= kFlagHwReversed; return s; }
constexpr ParamSpec brief(ParamSpec s, const char* b) noexcept { s.brief = b; return s; }
constexpr ParamSpec withLaw(ParamSpec s, TimeLaw l) noexcept { s.law = l; return s; }
// shared step tables
inline constexpr Step kOffOn[]    = { {0, "OFF"}, {1, "ON"} };
inline constexpr Step kDualLink[] = { {0, "DUAL", "DUAL MONO"}, {1, "LINK", "LINKED"} };
inline constexpr Step kStereoMs[] = { {0, "ST", "STEREO"}, {1, "M/S", "MID/SIDE"} };
inline constexpr Step kPeak[]     = { {0, "PEAK"} };
// the neutral table: every Mode param n/a with host-neutral values
constexpr ParamTable allNa(const char* reason) noexcept {
    ParamTable t;
    for (auto& en : t.e) en.spec = na(0, reason);
    t[Pid::ratio].spec  = na(0.75f, reason);   t[Pid::knee].spec = na(6, reason);
    t[Pid::range].spec  = na(60, reason);      t[Pid::atk].spec  = na(10, reason);   t[Pid::rel].spec = na(200, reason);
    t[Pid::link].spec   = na(1, reason);       t[Pid::mix].spec  = na(1, reason);
    t[Pid::s2thr].spec  = na(24, reason);      t[Pid::s2atk].spec = na(1, reason);   t[Pid::s2rel].spec = na(100, reason);
    t[Pid::thr].spec    = na(-18, reason);
    return t;
}
// standard extensions (D §5.1 hardware defaults; E §4.2.5)
constexpr ParamSpec extSchpf()  noexcept { return ext(cont(0, 500, 0)); }        // < 20 Hz = OFF
constexpr ParamSpec extLink()   noexcept { return ext(cont(0, 1, 1)); }
constexpr ParamSpec extMix()    noexcept { return ext(cont(0, 1, 1)); }          // hardware Modes: 0–100 %
constexpr ParamSpec extRange()  noexcept { return ext(cont(0, 60, 60)); }        // FF Modes only
constexpr ParamSpec extStereo() noexcept { return ext(stepped(kStereoMs, 0)); }
constexpr ParamSpec extDrive()  noexcept { return ext(cont(-24, 24, 0)); }
// shared time-spec functions (inline, bodies in the header): published value + the spec's law; `program` from kFlagProgram
inline TimeSpec attackFromView(const ParamView&, const EngineParams&) noexcept;
inline TimeSpec releaseFromView(const ParamView&, const EngineParams&) noexcept;
inline float    tailFromRelease(const EngineParams&) noexcept;                   // 5 × relTau (+ latency by host)
}
```

### 10.2 State matrix for the first eight

These are D §6.3's first eight. Key: **L** live, **S** stepped, **H** hybrid, **X** locked, **D** derived, **–** n/a, **+** extension, *"NAME"* = relabelled.

| Pid | Clean | Bus G | FET 76 | Opto 2A | Mu 67 | Diode 609 | Bus 25 | Brickwall |
|---|---|---|---|---|---|---|---|---|
| thr | L −60…0 (plot=plain) | L dial ±15 | L *INPUT* 0–48, inverted | L *PEAK RED.* 0–100, inverted | L *AC THRESH* 0–10, inverted | S −20…+10 dBu /2 | L −20…+20 dBu | L −30…0 |
| ratio | L 1…∞ | S 2/4/10 | S 4/8/12/20/ALL | S COMP/LIMIT | D progressive (EFF) | S 1.5/2/3/4/6 | S 1.5…∞ (7) | X ∞ |
| knee | L 0–72 (plot=plain) | D = RATIO | D = RATIO | – | L *DC THRESH* 0–10 | X progressive | S HARD/MED/SOFT | L 0–6 |
| range | L 0–60/OFF (plot=plain) | + | – | – | – | – | + | – |
| atk | L .005–250 ms | S 6 | L .02–.8 ms (hw-reversed) | X ~10 ms | D = TIME | S FAST/SLOW | S 7 | D = LOOKAHEAD |
| rel | L 5 ms–5 s | S 4 + AUTO | L 50–1100 ms | X ~60 ms→50 % | S *TIME* 1–6 | S 4 + A1/A2 | S 6 / L VAR 50–3000 | L 1–1000 ms |
| tmode | S MAN/AUTO | – | S *GR* ON/OFF (steps 0/7) | – | – | – | S FIXED/VAR | S MAN/AUTO |
| hold | L 0–500 | – | – | – | – | – | – | – |
| look | L 0–20 (budget-clamped) | – | – | – | – | – | – | L 0.5–20 (budget-clamped; X 0 when OFF) |
| det | S PEAK/RMS/PK+RMS | X PEAK | X PEAK | X T4 CELL | X TUBE | X PEAK | X RMS | S PEAK/TRUE PK |
| schpf | L | + | + | + | + S OFF/50/100/200/350 | + | + | – |
| sce | L *SC TILT* ±6 | – | – | L *EMPHASIS* 0–10 | – | – | S NORM/MED/LOUD | – |
| link | L | + | S DUAL/LINK | S DUAL/LINK | S IND/LINK | S DUAL/STEREO | S IND/50…100 | L |
| stmode | S 6 | + ST/MS | + ST/MS | + ST/MS | S L/R, LAT/VERT | + ST/MS | + ST/MS | S ST/MID/SIDE |
| voice | S OFF/TUBE/DIODE/BRIGHT | S VCA/CLEAN | S LN/A/F | X TUBE | X TUBE | X DIODE | S NEW/OLD | S CLEAN/LOUD |
| drive | L (– if voice OFF) | + (– if CLEAN) | – | + | *INPUT* S −20…0 | + | + | – |
| makeup | L ±24 | L −5…+15 | *OUTPUT* 0–48 | *GAIN* 0–100 | + ±12 | S 0–20 /2 | L 0–24 | *CEILING* −12…0 |
| automu | S OFF/ON (AUTO word) | – | – | – | – | – | S MAN/AUTO (AUTO word) | – (ceiling sets output) |
| mix | L 0–200 % | + | + | + | + | + | L 0–100 % | X 100 % |
| s2thr | – | – | – | – | – | S +4…+15 dBu /1 + OFF | – | – |
| s2atk | – | – | – | – | – | S FAST/SLOW | – | – |
| s2rel | – | – | – | – | – | S 50/100/200/800/A1/A2 | – | – |

`automu` is always drawn as the AUTO word on MAKEUP, and `tmode` always has its own slot (02 §6.4); the layout owns
that, not a descriptor flag. Three [DECIDED] rules come out of this matrix:
- **AUTO positions that sit on a hardware release switch are `rel` steps** tagged `kTagAuto`/`kTagAuto2`, with a plain value equal to the slow constant: SSL AUTO, Neve a1/a2. **`tmode` is only for switches that are separate on the hardware** (API FIXED/VAR, CL 1B, TG1) and for digital MAN/AUTO.
  - Why: one hardware control stays one host parameter; step lists stay strictly increasing; and there are no multi-parameter composite gestures.
  - This supersedes D §5.4 (SSL `tmode`), D §5.7's composite knobs and F §10.4.
- **The Fairchild TC goes on `rel`** (D §5.4), not on `atk` as F §3.4 proposed. Step plain values must be strictly increasing. The TC release times are (0.3, 0.8, 2, 5, 10, 25 s), which are monotone. The attacks (0.2, 0.2, 0.4, 0.8, 0.4, 0.2 ms) are not.
- **No "circuit off" step may sit where another Mode's default resolves to it** (K2 #4). FET 76's attack-knob OFF detent is therefore not an `atk` step (in the log domain every raw attack above 0.894 ms would snap to it, so selecting FET 76 from Clean, Bus G, Bus 25 or Diode 609 would disable gain reduction). It moves to FET's `tmode` slot, relabelled `GR`, with ON at 0 and OFF at 7: every other Mode writes `tmode` ∈ {0, 1}, which resolves to ON. The `crossmode.no_off` lint (§8.3) enforces the rule for every future Mode.

### 10.3 Clean (slot 0, `clean`), full

```cpp
// Source/fcdsp/modes/clean/CleanDesc.cpp   (Clean.h = traits; Clean.cpp = FCDSP_DEFINE_MODE(Clean))
namespace fcdsp::modes {
using namespace kit;
constexpr Step kTm[]    = { {0, "MAN", "MANUAL"}, {1, "AUTO", "AUTO RELEASE", kTagAuto} };   // CrestAuto (E §2.5a)
constexpr Step kDet[]   = { {0, "PEAK"}, {1, "RMS"}, {2, "PK+RMS", "PEAK + RMS"} };
constexpr Step kSt[]    = { {0, "ST", "STEREO"}, {1, "M/S", "MID/SIDE"}, {2, "MID", "MID ONLY"},
                            {3, "SIDE", "SIDE ONLY"}, {4, "M>S", "MID KEYS SIDE"}, {5, "S>M", "SIDE KEYS MID"} };
constexpr Step kVoice[] = { {0, "OFF"}, {1, "TUBE"}, {2, "DIODE"}, {3, "BRIGHT"} };          // Pro-C 3 precedent (D §2.8)
constexpr Variant kDriveByVoice[] = { {0, na(0, "NO COLOUR STAGE WHILE VOICE IS OFF")} };

constexpr ParamTable kCleanParams = [] {
    ParamTable t = allNa("NOT USED BY CLEAN");
    t[Pid::thr]    = { plotPlain(cont(-60, 0, -18)) };
    t[Pid::ratio]  = { cont(0, 1, 0.75f) };                 // S: 1:1 … ∞:1 (no negative ratios in Clean)
    t[Pid::knee]   = { plotPlain(cont(0, 72, 6)) };         // absolute knee/range handle drags (02 §6.5)
    t[Pid::range]  = { plotPlain(cont(0, 60, 60)) };
    t[Pid::atk]    = { cont(0.005f, 250, 10) };
    t[Pid::rel]    = { cont(5, 5000, 200) };
    t[Pid::tmode]  = { stepped(kTm, 0) };                    // its own slot, TIME MODE (02 §6.4)
    t[Pid::hold]   = { cont(0, 500, 0) };
    t[Pid::look]   = { cont(0, 20, 0) };                     // the resolver clamps to the budget; locked 0 when OFF
    t[Pid::det]    = { stepped(kDet, 0) };
    t[Pid::schpf]  = { cont(0, 500, 0) };
    t[Pid::sce]    = { named(cont(-6, 6, 0), "SC TILT") };
    t[Pid::link]   = { cont(0, 1, 1) };
    t[Pid::stmode] = { stepped(kSt, 0) };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { cont(-24, 24, 0), Pid::voice, kDriveByVoice };
    t[Pid::makeup] = { cont(-24, 24, 0) };
    t[Pid::automu] = { stepped(kOffOn, 0) };                 // drawn as the AUTO word on MAKEUP
    t[Pid::mix]    = { cont(0, 2, 1) };                       // up to 200 % (D §2.8, E §10.6)
    return t;
}();

static void cleanPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    if (v[Pid::tmode].tag & kTagAuto) e.flags |= kEngAutoRelease;
}
static DetectorLaw cleanLaw(const EngineParams& e) noexcept { return e.det == 1 ? DetectorLaw::rms : DetectorLaw::peak; }
constexpr InternalSpec kCleanInt[] = { {"REL EFF", "MS", 1, 5000, 0, true}, {"CREST", "DB", 0, 30, 1, false},
                                       {"PEAK DET", "DB", -60, 6, 1, false}, {"RMS DET", "DB", -60, 6, 1, false} };

constexpr ModeDescriptor kClean {
    .key = "clean", .name = "CLEAN", .group = Group::modern, .introducedInStateVersion = 1,
    .topologyLine = "FEED-FORWARD · LOG DOMAIN", .specLine = "CLEAN   FEED-FORWARD · PEAK/RMS · 5 µS–250 MS · 1:1–∞ · 0–200 % MIX",
    .params = kCleanParams, .physical = &cleanPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &cleanLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::clean, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 40, .internals = kCleanInt };
}
// Traits (Clean.h): Detector DetSelect<PeakLog, RmsLog, DualDet>; Computer QuadKnee; Link LinkMax;
// Ballistics Hold<CrestAuto<SmoothBranching>>; Stage2 NoStage2; Colour ColourSelect<ColourNone, TubeSym, DiodeAsym, Bright>;
// ScShape Flat; kTopologies = FF. Rigor clean ⇒ D8 (b) bit-exact below-threshold null when voice = OFF.
```

### 10.4 Bus G (slot 1, `bus-g`), full

```cpp
namespace fcdsp::modes {
using namespace kit;
constexpr Step kRatio[] = { {0.50f, "2", "2:1"}, {0.75f, "4", "4:1"}, {0.90f, "10", "10:1"} };      // S; bounds 0.625/0.825
constexpr Step kAtk[]   = { {0.1f, ".1", "0.1 MS"}, {0.3f, ".3", "0.3 MS"}, {1, "1", "1 MS"},
                            {3, "3", "3 MS"}, {10, "10", "10 MS"}, {30, "30", "30 MS"} };             // D §2.4 [V~ S5]
constexpr Step kRel[]   = { {100, ".1", "0.1 S"}, {300, ".3", "0.3 S"}, {600, ".6", "0.6 S"}, {1200, "1.2", "1.2 S"},
                            {2400, "AUTO", "AUTO", kTagAuto | kTagProgram} };                         // AUTO = 5th position
constexpr Step kVoice[] = { {0, "VCA", "VCA + CONSOLE"}, {1, "CLEAN"} };                              // Waves "Analog" precedent
constexpr Variant kDriveByVoice[] = { {1, na(0, "CLEAN VOICE HAS NO COLOUR STAGE")} };

static float kneeFromRatio(const ParamView& v) noexcept {   // [H] E §2.2 auto knee; T-dependence open (D §8.3)
    constexpr float w[] = { 10.f, 6.f, 3.f }; const int s = v[Pid::ratio].step; return w[s < 0 ? 1 : s]; }
static float thrDial (float p) noexcept { return p + 15.f; }  // dial −15…+15 ↔ −30…0 dBFS [H: dial 0 = −15 dBFS]
static float thrPlain(float d) noexcept { return d - 15.f; }

constexpr ParamTable kBusGParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-30, 0, -15), nullptr, { &thrDial, &thrPlain, "", 1 }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
    t[Pid::knee]   = { derived(&kneeFromRatio, Pid::ratio, "= RATIO", "KNEE FOLLOWS RATIO: SOFT AT 2, MEDIUM AT 4, HARD AT 10") };
    t[Pid::range]  = { extRange() };
    t[Pid::atk]    = { stepped(kAtk, 10) };
    t[Pid::rel]    = { stepped(kRel, 300) };
    t[Pid::tmode]  = { na(0, "AUTO IS THE FIFTH RELEASE POSITION") };
    t[Pid::det]    = { locked(kPeak, "PEAK DETECTOR, FIXED IN THIS CIRCUIT") };   // [U] D §8.3
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { extLink() };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { ext(cont(-12, 12, 0)), Pid::voice, kDriveByVoice };
    t[Pid::makeup] = { cont(-5, 15, 0) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

static void busGPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    if (v[Pid::rel].tag & kTagAuto) { e.m[0] = 100.f; e.m[1] = 300.f; e.m[2] = 1200.f; }  // [H] DualRelease τRf, τC, τRs (E §2.5b)
}
constexpr InternalSpec kBusGInt[] = { {"FAST ENV", "DB", 0, 30, 1, false}, {"SLOW ENV", "DB", 0, 30, 1, true},
                                      {"AUTO SLOW", "", 0, 1, 0, false} };
constexpr ModeDescriptor kBusG {
    .key = "bus-g", .name = "BUS G", .group = Group::vca, .introducedInStateVersion = 1,
    .topologyLine = "VCA BUS · FEED-FORWARD · PEAK", .specLine = "BUS G   VCA BUS · 2/4/10 · .1–30 MS · .1–1.2 S + AUTO",
    .params = kBusGParams, .physical = &busGPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = [](const EngineParams&) noexcept { return DetectorLaw::peak; },
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::modelled, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 40, .internals = kBusGInt };
}
// Traits: PeakLog; QuadKnee; LinkMax; AutoSwitch<SmoothBranching, DualRelease>; NoStage2; ColourSelect<VcaBus, ColourNone>; FF.
// Release spec: AUTO → TimeSpec{program, lo 0.1 s, hi 1.2 s} (C §5.3 character-style range check).
```

### 10.5 FET 76 (slot 2, `fet-76`), full

```cpp
// Source/fcdsp/modes/fet-76/Fet76Desc.cpp
namespace fcdsp::modes {
using namespace kit;
constexpr float kT0 = -12.f;                     // [H] internal threshold at 4:1, dBFS (D §8.1)
// INPUT dial 0…48, unity 24 (D §2.1 [V S2]); gain = dial − 24 dB [H]; host thr = effective input threshold at 4:1
static float inDial  (float thr) noexcept { return 24.f + (kT0 - thr); }
static float inPlain (float d)   noexcept { return kT0 - (d - 24.f); }
static float outDial (float mu)  noexcept { return mu + 24.f; }          // OUTPUT dial 0…48, unity 24
static float outPlain(float d)   noexcept { return d - 24.f; }
constexpr Step kRatio[]  = { {0.75f, "4", "4:1"}, {0.875f, "8", "8:1"}, {0.91667f, "12", "12:1"}, {0.95f, "20", "20:1"},
                             {1.0f, "ALL", "ALL BUTTONS", kTagAll | kTagProgram, "all buttons"} };
// The attack knob's CCW OFF detent (D §2.1 [V S2]) lives on tmode, relabelled GR (K2 #4): ON = 0, OFF = 7, boundary 3.5,
// so every other Mode's tmode ∈ {0, 1} resolves to ON.
constexpr Step kGrSwitch[] = { {0, "ON", "GAIN REDUCTION ON"},
                               {7, "OFF", "ATTACK OFF (NO GAIN REDUCTION, COLOUR ONLY)", kTagOff, "gain reduction off"} };
constexpr Step kVoice[]  = { {0, "LN", "REV D/E LN"}, {1, "A", "REV A"}, {2, "F", "REV F/H"} };   // D §2.1 revisions
constexpr Step kPeakFb[] = { {0, "PEAK", "PEAK · FEEDBACK"} };

static float kneeFromRatio(const ParamView& v) noexcept {   // [H] D §5.3: 4:1 softer … 20:1 near-hard
    constexpr float w[] = { 8.f, 5.f, 4.f, 2.f, 2.f }; const int s = v[Pid::ratio].step; return w[s < 0 ? 0 : s]; }

constexpr ParamTable kFet76Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-36, 12, -18), "INPUT", { &inDial, &inPlain, "", 0, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
    t[Pid::knee]   = { derived(&kneeFromRatio, Pid::ratio, "= RATIO", "KNEE IS SET BY THE RATIO BUTTON (FEEDBACK FET)") };
    t[Pid::range]  = { na(60, "NO GAIN-REDUCTION LIMIT INSIDE THIS FEEDBACK LOOP") };
    t[Pid::atk]    = { hwrev(cont(0.02f, 0.8f, 0.2f)) };    // raw > 0.8 clamps to the slowest attack (OFF's neighbour)
    t[Pid::rel]    = { hwrev(cont(50, 1100, 400)) };
    t[Pid::tmode]  = { named(stepped(kGrSwitch, 0), "GR") };  // B4 slot shows "GR"; OFF = the knob's OFF detent
    t[Pid::det]    = { locked(kPeakFb, "PEAK SENSING AFTER THE GAIN ELEMENT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { na(0, "INPUT DRIVES THE FET STAGE; THERE IS NO SEPARATE DRIVE") };
    t[Pid::makeup] = { named(cont(-24, 24, 0), "OUTPUT", { &outDial, &outPlain, "", 0 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

static void fetPhysical(const ParamView& v, EngineParams& e) noexcept {
    constexpr float thrOffset[] = { 0.f, 2.f, 4.f, 6.f, 4.f };   // [H] "higher ratios raise the threshold" (D §2.1 [V S2])
    const int r = v[Pid::ratio].step < 0 ? 0 : v[Pid::ratio].step;
    e.preGainDb = kT0 - v[Pid::thr].plain;                       // the INPUT knob's gain; also drives the FET colour
    e.thrDb     = kT0 + thrOffset[r];                            // fixed internal threshold (detector domain)
    e.topo      = kTopoFB;
    if (v[Pid::tmode].tag & kTagOff) e.flags |= kEngGrOff;       // audio still passes the colour stage; GR ramps
                                                                 // to 0 over 20 ms (offAmt_, §5.1), never steps
    if (v[Pid::link].step == 1 && e.atkTauMs < 0.04f) e.atkTauMs = 0.04f;   // linked: 40 µs minimum (D §2.1 [V S2])
    // ALL: custom curve and slower attack come from kTagAll inside the policies (E §2.7 [H]).
}
constexpr InternalSpec kFetInt[] = { {"LOOP CV", "V", 0, 10, 2, true}, {"FET R", "KOHM", 0, 100, 1, false},
                                     {"H2", "DB", -100, 0, 0, false}, {"H3", "DB", -100, 0, 0, false} };
constexpr ModeDescriptor kFet76 {
    .key = "fet-76", .name = "FET 76", .group = Group::fet, .introducedInStateVersion = 1,
    .topologyLine = "FEEDBACK FET · PEAK", .specLine = "FET 76   FEEDBACK FET · PEAK · 20–800 µS · 4/8/12/20/ALL · INPUT DRIVES A FIXED THRESHOLD",
    .params = kFet76Params, .physical = &fetPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = [](const EngineParams&) noexcept { return DetectorLaw::peak; },
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 60, .internals = kFetInt };
}
// Traits: PeakLog; QuadKnee (solveFb closed form, E §2.6); LinkMax; SmoothBranching; NoStage2;
// ColourSelect<FetColour> (voice = revision constants); law::FetVcr [H]; kTopologies = FB. At ECO the FET colour runs
// ADAA-1 at base rate (§5.6); STD or HQ is recommended (D F14) but nothing is refused (K1 #35).
```

### 10.6 Opto 2A (slot 3, `opto-2a`), full

```cpp
namespace fcdsp::modes {
using namespace kit;
static float prDial   (float thr) noexcept { return (24.f - thr) / 0.64f; }   // PEAK RED. 0…100 ↔ +24…−40 dBFS [H]
static float prPlain  (float d)   noexcept { return 24.f - 0.64f * d; }
static float gainDial (float mu)  noexcept { return (mu + 10.f) / 0.4f; }     // GAIN 0…100 ↔ −10…+30 dB [H]
static float gainPlain(float d)   noexcept { return -10.f + 0.4f * d; }
static float emDial   (float s)   noexcept { return s * (10.f / 6.f); }        // R37 set-screw 0…10
static float emPlain  (float d)   noexcept { return d * 0.6f; }
constexpr Step kRatio[] = { {0.6667f, "COMP", "COMPRESS"}, {0.90f, "LIMIT", "LIMIT"} };   // nominal 3:1 / 10:1 [C: 4:1] (D §2.2)
constexpr Step kT4[]    = { {0, "T4", "T4 CELL"} };
constexpr Step kTube[]  = { {0, "TUBE", "TUBE + TRANSFORMERS"} };

constexpr ParamTable kOpto2AParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-40, 24, -1.6f /*PR 40*/), "PEAK RED.", { &prDial, &prPlain, "", 0, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.6667f) };
    t[Pid::knee]   = { na(6, "THE KNEE IS THE T4 CELL'S; SEE THE CURVE") };
    t[Pid::range]  = { na(60, "NO GAIN-REDUCTION LIMIT INSIDE THIS FEEDBACK LOOP") };
    t[Pid::atk]    = { prog(locked(10, "T4 CELL: ABOUT 10 MS ON AVERAGE, PROGRAM-DEPENDENT")) };
    t[Pid::rel]    = { prog(withLaw(locked(60, "T4 CELL: 60 MS TO 50 %, THEN 1–15 S BY PROGRAM HISTORY", "AUTO"), TimeLaw::t50)) };
    t[Pid::tmode]  = { na(0, "NO TIME CONTROLS ON THIS CIRCUIT") };
    t[Pid::det]    = { locked(kT4, "THE OPTICAL CELL SENSES THE OUTPUT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::sce]    = { named(cont(0, 6, 0), "EMPHASIS", { &emDial, &emPlain, "", 1 }) };   // D §2.2 [V S31] R37
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kTube, "TUBE LINE AMP AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { named(cont(-10, 30, 6.f /*GAIN 40*/), "GAIN", { &gainDial, &gainPlain, "", 0 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

static void optoPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                               // FeedbackDelayed: safe at τ ≈ 10 ms (E §2.7), guarded in prepare()
    e.m[0] = v[Pid::sce].plain * (10.f / 6.f);      // R37: LF desensitisation 0…10 dB below 1 kHz (D §2.2 [V S31]) [H corner]
    e.m[1] = (v[Pid::ratio].step == 1) ? 1.f : 0.f; // LIMIT: EL-panel drive law change [H]
    // OptoCell constants (E §2.7 [H]): w = 0.5, τon 10 ms, τoff,f 87 ms, τoff,s(m) = 0.3 + 3.2·m s, memory 5 s up / 20 s down
}
static DetectorLaw optoLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }
static float optoTail(const EngineParams&) noexcept { return 15.f; }        // memory: up to 15 s (E §5.2)
constexpr InternalSpec kOptoInt[] = { {"LIGHT", "", 0, 1, 2, false}, {"G FAST", "", 0, 1, 2, false},
                                      {"G SLOW", "", 0, 1, 2, false}, {"MEMORY", "%", 0, 100, 0, true},
                                      {"LDR", "KOHM", 0, 1000, 0, false} };
constexpr ModeDescriptor kOpto2A {
    .key = "opto-2a", .name = "OPTO 2A", .group = Group::opto, .introducedInStateVersion = 1,
    .topologyLine = "OPTICAL · FEEDBACK · PROGRAM-DEPENDENT", .specLine = "OPTO 2A   T4 CELL · COMP/LIMIT · ~10 MS · 60 MS→50 %, THEN 1–15 S",
    .params = kOpto2AParams, .physical = &optoPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &optoLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView,       // {0.010 s, expDb, lo 0.005, hi 0.020, program}
    .releaseSpec = &releaseFromView,     // {0.060 s, t50,   lo 0.040, hi 0.080, program}
    .tailSeconds = &optoTail, .ctBudgetNsPerSample = 50, .internals = kOptoInt };
}
// Traits: Detector OptoSense (rectified, emphasis-shaped output); Computer OptoCellCurve (steady state of law::LdrShunt,
// used by staticGr and the telemetry target); Ballistics OptoCell, fused in FeedbackDelayed<OptoCellCurve> — prepare()
// checks k ≤ α/(1−α) at the actual fs and falls back to FeedbackZdf if violated (§5.3, K2 #5c); ScShape R37Shelf;
// Colour TubeTransformer; kTopologies = FB.
```

### 10.7 The other four, sketched

Their descriptors are written **in full** by the descriptor-wave task DW (03 §4.9) with generic traits and
`provisional = true`, so the schema is proven on all eight Modes before it freezes at FZ3 (K3 #9); the Mode tasks M1–M7
then replace the traits and fit the [H] constants. Each lives in `Source/fcdsp/modes/<key>/`. [H] values are placeholders.

**As built (S10–S11 revision).** The sketches below were the DW starting point. For the final traits and fitted
constants, the Mode sheets `docs/modes/<key>.md` are authoritative. The main differences:

- **Mu 67.** `ProgressiveKnee` carries its own safeguarded Newton (`solveFb`), so the Mode does not instantiate
  `FeedbackZdf<ProgressiveKnee>`; that remains a valid wrapper. The knee and TC constants were refitted, and AC THRESH
  is not inverted.
- **Bus 25.** The detector is `bus25::RmsCatch` (an RMS window of the release / 50, with a 20 dB jump catch), not
  `RmsLog`. The OLD link is solved inside the FB loop (`bus25::CvSumBranching`, the ADR-67 fix), not after the
  per-lane solve.
- **Brickwall.** `D_tp` = 12 samples. LOUD adds 2.25 dB of makeup. The STD ceiling spec is judged on a music program;
  the 2× IIR round trip adds the overshoot.

**Mu 67 (slot 4, `mu-67`)** follows D §2.3 and E §2.7. Its traits are `ProgressiveKnee` inside `FeedbackZdf`, with `TcSelector` ballistics.

```cpp
constexpr Step kTc[] = { {300, "1", "TC1 0.3 S"}, {800, "2", "TC2 0.8 S"}, {2000, "3", "TC3 2 S"}, {5000, "4", "TC4 5 S"},
                         {10000, "5", "TC5 2/10 S", kTagTc5 | kTagProgram}, {25000, "6", "TC6 .3/10/25 S", kTagTc6 | kTagProgram} };
// atk: derived(tcAttack, Pid::rel, "= TIME", "ATTACK IS SET BY THE TIME CONSTANT"); tcAttack = {0.2,0.2,0.4,0.8,0.4,0.2} ms
//      (TC4 0.8 ms per S3 [C: 0.4 per S4], D §8.2)
// thr:   named(cont(-40, 24, 0), "AC THRESH", dial 0…10, invert)            (10 = no compression, D §2.3 [V S4])
// ratio: prog(derived(&muRatioNominal, Pid::knee, "PROGRESSIVE", "RATIO RISES WITH LEVEL (REMOTE-CUTOFF TUBE)"))
//        muRatioNominal = the S at T_in + 10 dB for the current DC THRESH; the slot prints the live EFF ratio (K1 #26)
// knee:  named(cont(0, 40, 20), "DC THRESH", dial 0…10)  → physical: o_c = 2 + 0.35·knee dB, S_max = 0.98 − 0.002·knee [H]
// rel:   named(stepped(kTc, 800), "TIME")
// drive: named(stepped(kIn21 /* −20…0 dB, 1 dB */, 0), "INPUT")   (character only: level-compensated, D §2.3 [V S3])
// stmode: stepped({ {0,"L/R","LEFT/RIGHT"}, {1,"LAT/VERT","LATERAL/VERTICAL"} }, 0)   (M/S, D §2.3 AGC)
// link: stepped({ {0,"IND"}, {1,"LINK"} }, 1); schpf: ext(stepped({OFF 0, 50, 100, 200, 350}, 0)); makeup: ext(cont(-12,12,0))
// det: locked({ {0,"TUBE"} }); voice: locked TUBE; group varimu; rigor character; family custom.
// FB solve: FeedbackZdf<ProgressiveKnee>; TcSelector exposes TC1–4 as SmoothBranching {α·r1, 1−α} and TC5/TC6 as
// MultiStage3 branches, solved as the max of roots (§5.2). fb.monotone must hold for every DC THRESH.
```

**Diode 609 (slot 5, `diode-609`)** follows D §2.5. Stage 2 is `SharedElementMax<PeakLog, SmoothBranching>` (E §3.3). Its detector lanes are aux0 and aux1.

```cpp
// thr:    stepped(16 steps: plain = dBu − 22 for dBu −20…+10 /2 → −42…−12 dBFS), display unit "DBU" (+22)
// ratio:  stepped({1.5,2,3,4,6} as S {0.3333,0.5,0.6667,0.75,0.8333}); knee: locked(5, "PROGRESSIVE: TRUE RATIO >5 DB OVER")
// atk:    stepped({ {3,"FAST","FAST ~3 MS"}, {6,"SLOW","SLOW ~6 MS"} }, 3)  → physical: SLOW sets m[0] = 100 Hz Mode-internal SC HP
// rel:    stepped({100,400,800,1500, {2000,"A1","AUTO 1 100 MS/2 S",kTagAuto}, {5000,"A2","AUTO 2 50 MS/5 S",kTagAuto2}}, 400)
// makeup: stepped(0…20 dB /2); link: stepped({ {0,"DUAL"}, {1,"STEREO"} }, 1)
// s2thr:  stepped(+4…+15 dBu /1 → −18…−7 dBFS, then {24,"OFF","LIMITER OUT",kTagOff}), default OFF
// s2atk:  stepped({ {2,"FAST"}, {4,"SLOW"} }, 2); s2rel: stepped({50,100,200,800, A1 2000, A2 5000}, 100)  [U lists, D §8.6]
// det: locked PEAK; voice: locked({0,"DIODE"}); topo FB; stage2 sharedElementMax; group diode.
// A1/A2 use DualRelease inside the FB solve (max of the fast and slow roots, §5.2). Stage 2 turns on and off through
// the 20 ms s2On_ ramp (§5.1), never a step.
```

**Bus 25 (slot 6, `bus-25`)** follows D §2.4 and E §2.7. Its detector is `RmsLog`. Tilt is the host `sce`. Link is `LinkCvSum`. `voice` switches the topology, and the kernel crossfade covers the change.

```cpp
constexpr Step kVoice[] = { {0, "NEW", "NEW (FEED-FORWARD)"}, {1, "OLD", "OLD (FEEDBACK)"} };    // → e.topo
constexpr Step kTm[]    = { {0, "FIXED"}, {1, "VAR", "VARIABLE", kTagVar} };
constexpr Variant kRelByTm[] = { {1, cont(50, 3000, 500)} };         // VAR exposes the continuous pot (D §2.4 [V S6])
// rel:   { stepped({50,100,200,500,1000,2000}, 500), Pid::tmode, kRelByTm }       — the dependent-list example
// thr:   cont(-42, -2, -22) display dBu (+22); ratio: stepped({1.5,2,3,4,6,10,∞→S 1.0})
// knee:  stepped({ {0,"HARD"}, {6,"MED"}, {12,"SOFT"} }, 6)  [H widths]
// atk:   stepped({.03,.1,.3,1,3,10,30}, 1); sce: named(stepped({ {0,"NORM"}, {1.5f,"MED"}, {3.01f,"LOUD"} }, 0), "THRUST")
//        (LOUD = 10 dB/decade [V S6]; MED [C] D §8.5)
// link:  stepped({ {0,"IND"}, {.5,"50"}, {.6,"60"}, {.7,"70"}, {.8,"80"}, {.9,"90"}, {1,"100"} }, 1)
// automu: stepped({ {0,"MAN"}, {1,"AUTO"} }, 0) (the AUTO word); makeup: cont(0, 24, 0); mix: cont(0, 1, 1) (crossfade law)
// det: locked RMS; tmode: stepped(kTm, 0) (TIME MODE slot); kTopologies = FF|FB (branch per chunk on e.topo);
// group vca; rigor modelled. OLD (FB) links with LinkCvSum after the per-lane solve (§5.2).
```

**Brickwall (slot 7, `brickwall`)** follows D §2.8 and E §5.4. The topology is `SlidingMaxBox` over `PrepareInfo::scratch`. True peak uses 4× polyphase on the SC only, so it does not affect latency; its interpolator delay `D_tp` is reported through `IEngine::scDelaySamples()` and subtracted from the host SC delay (§5.4 c, K2 #21a).

```cpp
// thr:    cont(-30, 0, -6); ratio: locked(1.0f, "A LIMITER: RATIO IS INFINITE", "∞"); knee: cont(0, 6, 0)
// atk:    derived([](const ParamView& v) noexcept { return v[Pid::look].plain; }, Pid::look, "= LOOKAHEAD",
//                 "ATTACK HAPPENS INSIDE THE LOOKAHEAD WINDOW")
// rel:    cont(1, 1000, 50); tmode: stepped({MAN, AUTO}, 0); look: cont(0.5f, 20, 5) (budget-clamped by the resolver;
//         locked 0 with the BUDGET OFF reason while labudget = OFF, §4.4 — so the derived attack also reads 0)
// det:    stepped({ {0,"PEAK","SAMPLE PEAK"}, {1,"TP","TRUE PEAK", 0} }, 0) → kEngTruePeak. TP without a budget
//         (L_la − look + D_up < D_tp) cannot align; the footer says overshoot is possible (02 §6.6)
// makeup: named(cont(-12, 0, -0.3f), "CEILING"); automu: na(0, "OUTPUT IS SET BY THE CEILING") — no AUTO word
//         (K1 #27: a locked ON automu would add r̂(0 dBFS)·k on top of the ceiling makeup)
//         physical: makeupDb = ceiling − thrDb (the threshold lands on the ceiling); kEngAutoMakeup never set
// mix:    locked(1, "A LIMITER IS NEVER BLENDED", "100 %"); voice: stepped({ {0,"CLEAN"}, {1,"LOUD"} }, 0) (soft-clip pre-stage)
// stmode: stepped({ST, MID, SIDE} as indices {0, 2, 3}); link: cont(0,1,1); wantsLookahead = true; group limit.
// With labudget OFF, look is locked at 0 (zero latency, overshoot allowed); the footer hint is keyed on
// desc.wantsLookahead && budget == off, never on the Mode's name (02 §6.6, K1 #35).
// Owner specs (K2 #21): HQ 4×-reconstructed output peak ≤ CEILING + 0.1 dB; STD overshoot is documented, not clipped
// (≤ CEILING + 1.0 dB true peak, spec row); the box average re-sums exactly every 4096 samples (a 10-minute
// below-threshold soak row in dsp.null requires GR exactly 0.0); automating look slews the SC read position by ≤ 1
// sample per tick and changes the box length only at ticks.
```

---

## 11. Conflicts resolved

Rows 1–20 are Draft 1's resolutions of the research reports (row 16 is reversed by this synthesis). Rows 21–32 resolve
the critiques. `docs/DECISIONS.md` carries each one with its rejected options.

| # | Positions | Decision | Why |
|---|---|---|---|
| 1 | B §7.4.2 snaps raw on a UI Mode change. E §4.2.1 never rewrites. F §3.2 keeps locked parameters raw. | **Never rewrite; snap on read** (§4.5) | A→B→A exact, one host gesture, no audio-thread writes, deterministic probes |
| 2 | B: 64 Mode slots. E: 128. | **128**, `AudioParameterInt`, non-automatable | 35 Modes are already catalogued (D §5.1) and more will come; the cost is nil |
| 3 | B §7.4.5: latency = max over Modes. E §5.2: f(Quality, lookahead). | **f(quality, labudget)** | Adding Modes never changes latency; D4 `latency_constant` holds by construction |
| 4 | E §2.9 JUCE Oversampling vs a JUCE-free `fcdsp` | **Own `Oversampler`** | Mix, fade and colour run at the OS rate inside `EngineHost`; probes must run the shipped code |
| 5 | C D8a: mix = 0 is bit-exact. E §5.3: mix inside OS. | **Mix in OS.** D8a is redefined: ECO bit-exact vs delay; STD/HQ bit-exact vs a control `Oversampler` round trip (§5.6); 03 §3.4–3.7 now say the same (K1 #2, K2 #12) | Parallel-compression phase identity matters more than a null only a test sees |
| 6 | D `in` (input drive) vs F THRESHOLD→INPUT remap | **Remap `thr` + a character-only `drive`** | `thr` keeps one meaning across Modes; host lanes stay meaningful |
| 7 | D `topo` parameter | **Folded into `voice`/`physical()`** | Only switchable units need it; saves a UI slot |
| 8 | D `det` list vs F DETECT continuous | **List** | TRUE PEAK, T4 CELL and TUBE are not window lengths |
| 9 | SSL AUTO: D `tmode` / E composite knob / F latch | **`rel` step tagged AUTO**; `tmode` only for separate hardware switches, digital AUTO, and FET's GR switch; always its own slot | One control = one parameter; strictly increasing lists |
| 10 | Fairchild TC on `rel` (D) vs `atk` remapped as TIME (F) | **`rel`**, with `atk` derived | Step plain values must be monotone |
| 11 | GR sign: E positive vs F ≤ 0 | **Positive attenuation** | Giannoulis convention; the UI negates |
| 12 | UiFrame: E 57 words vs F 38 words | **72 words** (§6.2) | Union of both needs, plus the fade and bypass state |
| 13 | History: E 16 B min/max × 2048 vs F 32 B points × 4096 | **32 B min/max columns × 4096** | Spikes survive both ways; covers 4 s |
| 14 | `Remapped` as a Kind (D §5.7, E §4.2) | **Attributes** (`label` + `DisplayMap`) | A stepped spec can also be renamed |
| 15 | Program kind (D `P`) | **locked/derived + `kFlagProgram`** | Same UI treatment plus a live EFF readout |
| 16 | HR's APVTS has no UndoManager; E §4.3 adds one | **No `UndoManager`** (reversed, K2 #7) | JUCE's APVTS timer puts host automation into one unbounded transaction; hosts already record gestures |
| 17 | D global reference level | **Fixed +22 dBu = 0 dBFS**, no parameter | Fewer globals; revisit as a v2 append |
| 18 | C `ModeSpec::latencySamples` | **Removed** | Latency is host policy |
| 19 | F `UiFrame` linear meters vs E dB | **dB**, floored at −200 | The UI draws in dB; a handful of logs per block |
| 20 | HR `uiAttached_` bool | **Count** | A host can hold two editors |
| 21 | Draft 1 `Source/fcdsp/` vs Draft 3 `Source/dsp/` + `Source/modes/` | **`Source/fcdsp/`**, one directory per Mode, one header per policy (§2.1) | Directory = namespace = include prefix; file-disjoint parallel work (K1 #1, K3 #1, #7) |
| 22 | Draft 1 `Modes.def`-generated sources vs Draft 3 GLOB; K1 #5 vs K3 #2/#6 | **Per-directory globs + `FCDSP_DEFINE_MODE`**; `Modes.def` drives registry and CTest only (§8) | No shared file per Mode; no all-Modes TU |
| 23 | Test tap: compile flag (Draft 1, C) vs runtime (Draft 3, E) | **Runtime** `EngineHost::setTap(TestTap*)` (§5.4) | One `fcdsp` archive; no ODR hazard |
| 24 | FB interface `alphaFor`/`solveFb(…, α)` (Draft 1) | **`FbAffine` + `B::solveFb/commitFb`** (§5.2) | DualRelease, MultiStage3 and Hold have no single α (K2 #1) |
| 25 | Crossfade sharing host gain staging (Draft 1) | **`PathState` per path**; preGain folded into the wet gain; one `up()`/`down()` per chunk (§5.4–5.5) | K2 #3 (a)–(e) |
| 26 | FET attack OFF as a hybrid step (Draft 1) | **OFF on `tmode` (`GR`)**; `crossmode.no_off` lint; GR OFF ramps (§10.2, §10.5) | K2 #4: every raw attack > 0.894 ms resolved to OFF |
| 27 | Processor `AsyncUpdater` + listeners (Draft 1) | **`SetupWatcher` 20 Hz poller; `prepareToPlay` sets latency** (§3.1) | VST3 fires listeners on the audio thread (K2 #6) |
| 28 | `Pid` order == APVTS order (Draft 1) | **`kApvtsOrder`** separate and v1-forever (§3.2) | A v2 Mode parameter must join the Mode block (K2 #9) |
| 29 | Per-Mode sound changes after release | **`ModeDescriptor::revision` + `modeRev`** (§9.1) | `stateVersion` cannot express it (K2 #10) |
| 30 | `look` clamped only in the engine (Draft 1) vs a UI overlay (Draft 2) | **`RawParams::budget`; the resolver clamps and locks** (§4.4) | UI, host text and engine agree (K1 #8) |
| 31 | `HistoryRing` re-read check (Draft 1) | **Claim-word protocol** (§6.3) | Draft 1's reader could accept a torn column (K2 #8) |
| 32 | libm allowed in colour stages and SVF design | **Banned under `core/engine/modes`**; `FastMath` gains `tanh`, `logCosh`, `tanPi`, `sinPi`, `cosPi` (§2.2, §5.1) | Apple libm differs across arches and macOS releases (K2 #14) |

---

## 12. Open questions

Draft 1's questions, answered by this synthesis:
1. **UI placement of 29 parameters** — answered by 02 §6.4: 21 slots in 3 rows of 7, AUTO/EXT/LISTEN words, globals in the chrome; no compound cells.
2. **Lookahead budget UX** — the resolver locks `look` at 0 with a reason while the budget is OFF, and the footer shows a hint for any Mode with `wantsLookahead` (02 §6.6). Latency is never changed implicitly.
3. **FunkGui scope** — 02 accepts `FunkPresets`; the user must confirm (`DECISIONS.md` Q2).
4. **Candidate appends (v2 hints):** `lshape` (API link shape), `out` (global output trim), a reference-level parameter, per-Mode memory. Each is appended to `kApvtsOrder` with version hint 2. The output trim shipped in v1.2 as `output` (ADR-88).
5. **[H] constants that shape host ranges** (FET T0 = −12 dBFS and the dial law; Opto PEAK RED./GAIN laws; Bus G dial offset) must be fitted before a Mode enters `modes-ever.tsv`; after that, changing them needs `revision++` (§0).
6. **`mix` 0–200 %** — a user-level choice: `DECISIONS.md` Q5 (recommended: keep, Clean only).
7. **`kStdLatency`/`kHqLatency`** — frozen at FZ2 by the oversampler spike F6 (§5.6).

The remaining user-level questions are collected, each with a recommended default, in `docs/DECISIONS.md` §"Open
questions for the user".
