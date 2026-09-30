# FCompressor architecture

Status: **canonical top-level architecture, synthesised 2026-09-22.** Design phase: no code exists yet. This document
is the readable overview. The binding detail lives in three appendices, and every decision (with the options it beat)
is logged in `docs/DECISIONS.md` as `ADR-nn`:

| Appendix | File | Owns |
|---|---|---|
| **01** Core contracts | `docs/design/01-core-contracts.md` | parameters, Mode descriptor, resolver, engine, telemetry, analysis, registry, state and presets, the first eight Modes |
| **02** FunkGui and UI | `docs/design/02-funkgui-and-ui.md` | the FunkGui library (API, CMake, seed), the FCompressor editor (panel, band, Characteristics screen) |
| **03** Build, verify, process | `docs/design/03-build-verify-process.md` | CMake and FetchContent, the verification suite, the agent process, the sprint plan, release |

The research reports under `docs/research/` (A–F) are the evidence base; the critiques `docs/design/K1-3` are the
reviews this synthesis resolved. Where this document and an appendix disagree, the appendix wins and this document is
wrong.

---

## 1. What FCompressor is

An all-in-one compressor plugin for the author's own use (macOS; AU, VST3 and Standalone; manufacturer `Funk`, plugin
code `Fcmp`, bundle id `com.funk.fcompressor`). Compressor types are called **Modes**: Clean, a VCA bus compressor, a
feedback FET, an opto, a vari-mu, a diode bridge, and so on — 35 are catalogued (D §5.1) and more will be added
continuously. **Every Mode shares exactly the same UI**: the same parameters in the same places, the same visual aids.
Some Modes lock a parameter, or restrict it to hardware steps (Bus G's ratio is 2 · 4 · 10).

### 1.1 Goals

1. **One UI, any number of Modes.** Adding a Mode is data plus DSP: no UI code, no CMake, no shared file edited.
2. **Truthful display.** What the UI draws — the transfer curve, the operating point, gain-reduction history, the
   internals — is computed by the same code the audio thread runs, and probes prove it.
3. **Real-time safe and deterministic.** No allocation, lock or syscall on the audio thread; block-size-invariant
   output; bit-identical results across optimisation levels; no libm on the audio path.
4. **Stable sessions forever.** Host parameter IDs, ranges, Mode slots, the state layout and the preset format are
   frozen at v1; automation stays meaningful across Mode switches.
5. **Buildable by at most three agents in parallel**, with disjoint file ownership and a lead who reviews, commits and
   blesses.
6. **A reusable GUI library, FunkGui**, which HardwareReverb migrates onto after FCompressor v1.

### 1.2 Non-goals (v1)

- Windows, Linux, AAX, iOS; a resizable editor; plugin-side undo; host programs (`getNumPrograms() == 1`).
- An Intel slice that has never run: v1 ships arm64-only unless an x86 verify has passed (ADR-47, Q6).
- Per-Mode parameter memory, a link-shape or reference-level parameter (candidate v2 appends; the global output trim
  shipped in v1.2 as `output`, ADR-88).
- Modelling fidelity claims before the [H] (heuristic) constants are fitted (E's tag); every Mode states its `Rigor`.
- Editing HardwareReverb. It is read-only; it migrates onto FunkGui only after FCompressor v1, once its goldens prove
  identical (user decision, ADR-04).

---

## 2. Repository topology

Two first-party repositories and two third-party dependencies, **every one of them brought in through CMake
FetchContent** (user decision, ADR-01). Nothing third-party or shared is copied into FCompressor.

```
                         ┌──────────────────────────────────────────────────────────────┐
                         │ /Users/seanfunk/audio/plugins/FCompressor   (git, branch main)│
                         │   Source/fcdsp   JUCE-free DSP static lib                    │
                         │   Source/plugin  JUCE processor, state, presets glue         │
                         │   Source/editor  GPU-free panel (+ gpu/Editor)               │
                         │   Tools/probes   fcmp_probe_dsp, fcmp_probe_plugin           │
                         │   cmake/FcmpDeps.cmake ── FetchContent, pins, SHA asserts    │
                         └──────┬───────────────────────┬───────────────────────┬───────┘
          FETCHCONTENT_SOURCE_DIR_JUCE          _BGFX (GPU only)        FetchContent_Declare(FunkGui
          → ~/audio/.deps/JUCE-8.0.4      → ~/audio/.deps/bgfx.cmake-…    GIT_REPOSITORY …/FunkGui
                                │                       │                GIT_TAG v0.x.y)
                                ▼                       ▼                       ▼
   ┌────────────────────────────────┐ ┌──────────────────────────────┐ ┌───────────────────────────────────────┐
   │ JUCE 8.0.4                     │ │ bgfx.cmake v1.153.9385-561   │ │ /Users/seanfunk/audio/libraries/FunkGui│
   │ (read-only checkout, deps.sh)  │ │ bgfx · bx · bimg (Metal)     │ │ own git repo; tags v0.MINOR.PATCH      │
   │                                │ │ shaderc: prebuilt in .deps   │ │ FunkGui::core  ::gpu  ::harness        │
   └────────────────────────────────┘ └──────────────────────────────┘ │ ::presets  funkgui_* CMake functions   │
                 ▲ consumer provides JUCE, and bgfx first ─────────────│ seeded from HR Source/gui (snapshot)    │
                 └─────────────────────────────────────────────────────└───────────────────────────────────────┘
   /Users/seanfunk/audio/plugins/HardwareReverb — READ-ONLY; source of FunkGui's seed; never built or referenced by CMake
```

- **Pins** (03 §2.3): JUCE `8.0.4` (SHA `51d11a2b…`), bgfx.cmake `v1.153.9385-561` (SHA `99752df3…`, submodule SHAs
  asserted), FunkGui by **tag** plus its SHA and version (`FCMP_FUNKGUI_TAG/_SHA/_VERSION`). Every dependency is
  asserted after population (header version, `BGFX_API_VERSION` 153, git SHA), because `FETCHCONTENT_SOURCE_DIR_*`
  bypasses `GIT_TAG`.
- **Machine cache** `~/audio/.deps` (03 §2.4): populated once by `Scripts/deps.sh`, read-only afterwards, used as
  `FETCHCONTENT_SOURCE_DIR_{JUCE,BGFX}` defaults. No shared `FETCHCONTENT_BASE_DIR` (concurrent builds would share
  binary dirs). It also holds two build tools compiled from pinned upstream sources: `shaderc` (saves ≈ 2,060 CPU-s per
  GPU build directory) and `pluginval` (Q8).
- **FunkGui override** for development: `-DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<dir>`, allowed only for an agent's own
  FunkGui worktree or a lead-made `FunkGui.wt/pin-<sha7>`; the override must descend from the pinned SHA, is printed
  loudly, and fails the lead's integration verify (03 §4.5).
- **FunkGui scope.** FunkGui is its own git repo, CMake project and target `FunkGui`, namespace `funkgui` (ADR-02). Its
  seed is a snapshot of HR's `Source/gui`, shaders and bundled font (ADR-03). Beyond the GUI it also carries the test
  harness (`FunkGui::harness`) and the preset data layer (`FunkGui::presets`); both extend the user's "GUI library"
  decision and await confirmation (Q2, Q3).

---

## 3. Module map

Three layers with one direction of dependency (01 §1, §2):

```
  ┌──────────────── editor  (namespace fcmp::ui; JUCE + FunkGui::core; GPU part in editor/gpu) ────────────────┐
  │ Panel = fixed composition of SubViews: Header · DisplayRow · SlotGrid · Band · CharScreen · ModeBrowser ·  │
  │         PresetStrip · PresetBrowser · Footer        SlotModel (ValueModel per Pid)   PreviewWorker          │
  │ gpu/Editor : funkgui::EditorHost   (bgfx/Metal, FramePump, A11yBridge)                                      │
  └───────────────────────────────┬─────────────────────────────────────────────────────────────────────────────┘
                                  │ ONLY through Source/plugin/ProcessorFacade.h (ports, telemetry, UiState, batch)
  ┌───────────────────────────────▼──────────── plugin  (namespace fcmp; JUCE) ─────────────────────────────────┐
  │ Processor (APVTS, 29 JuceParamPorts, processBlock) · SetupWatcher (20 Hz) · ParamLayout · HostText ·        │
  │ State / StateMigration · Presets (FunkGui::presets hooks) · factory/*.inc · CreateEditorGpu|Generic         │
  └───────────────────────────────┬─────────────────────────────────────────────────────────────────────────────┘
                                  │ plain C++ API
  ┌───────────────────────────────▼──────────── fcdsp  (namespace fcdsp; std + SIMD intrinsics ONLY) ────────────┐
  │ core/       Simd FastMath Units ScopedFtz Sanitize Smoother ControlTicker                                   │
  │ params/     Pid Setup HostParams ParamSpec EngineParams Resolve Text                                         │
  │ modes/      ModeDescriptor ModeKit DefineMode Registry Modes.def   <key>/ (one directory per Mode)          │
  │ engine/     Stage IEngine ModeEngine<T> TestTap EngineHost Oversampler  host/ (components)  stages/<slot>/  │
  │ telemetry/  Seqlock UiFrame HistoryRing                                                                     │
  │ analysis/   staticGain staticGr localRatio stepResponse scResponse colourCurve …                            │
  └─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

| Layer | Rule | Why |
|---|---|---|
| `fcdsp` | Includes nothing from JUCE, FunkGui, `plugin/` or `editor/`; no libm on the audio path; no static initialisation (lint `lint.deps`) | Every probe links the same archive the plugin ships; results are bit-reproducible (ADR-10, ADR-24) |
| `plugin` | Never includes `editor/` except `CreateEditorGpu.cpp` | The processor must build headless |
| `editor` | Reaches the processor only through `ProcessorFacade` | UI work and UI probes run on a `FakeFacade`, never waiting for the real processor (ADR-40) |

Targets (03 §2.7): `fcdsp` (STATIC), `FCompressor` (`juce_add_plugin`, links FunkGui PRIVATE), `fcmp_probe_dsp`
(JUCE-free), `fcmp_probe_plugin` (processor + panel + FunkGui core, no GPU), `fcmp_bench`. Three configurations:
**GPU** (the real editor), **headless** (no bgfx; the plugin gets JUCE's generic editor; every probe still builds) and
**DSP-only** (no JUCE at all; ≈ 2 s configure).

The canonical source tree, with every directory and file stem, is 01 §2.1. Its properties matter for parallel work:
one directory per Mode (`Source/fcdsp/modes/<key>/`), one header per stage policy
(`Source/fcdsp/engine/stages/<slot>/<Policy>.h`), one file pair per UI sub-view, one file per probe, and per-directory
source globs — so adding any of those edits no shared file (ADR-19, ADR-20, ADR-21).

---

## 4. Data flow

### 4.1 One block, end to end

```
 host automation / UI gesture / state load
        │  writes host-plain values into APVTS raw atomics (VST3: possibly ON the audio thread)
        ▼
 ┌──────────────────────── audio thread, Processor::processBlock ────────────────────────────────────────────┐
 │ 1 snapshot RawParams (relaxed loads + configured lookahead budget)  — skipped while a batch is open        │
 │ 2 slot = resolveSlot(raw mode)                                                                              │
 │ 3 resolve(entry, raw) → Resolution{ ParamView view, EngineParams eng }   pure, < 1 µs                       │
 │       snap per Mode (stepped/locked/derived/n/a) → physicalDefault → desc.physical (remaps, preGain, topo)  │
 │ 4 BlockParams{slot, eng, bypass, delta, listen, extKey}                                                     │
 │ 5 EngineHost::process(io, block)                                                                            │
 │     sanitise input → route → SC filters → lookahead delays → up() once                                      │
 │     per path (1, or 2 during a 20 ms crossfade):  ModeEngine<T>::control()  one virtual call / 64 samples   │
 │         detector → gain computer → link → ballistics (FF) | affine ZDF solve (FB) → stage 2 → range → GR    │
 │         g = linFromDb(preGain − GR) at OS rate → colour() → makeup                                          │
 │     blend paths → mix with the un-pre-gained dry in the OS domain → down() once → bypass / listen ramps     │
 │     telemetry accumulation (while an editor is attached)                                                    │
 │ 6 publish UiFrame (seqlock, 72 words) · push HistoryColumns (1 ms each, claim-word ring)                     │
 └─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
        │ readUiFrame / history().read            (lock-free; gated by an editor-lifetime attach COUNT)
        ▼
 ┌──────────────────────── message thread, editor (60–120 Hz while live, 12 Hz idle) ─────────────────────────┐
 │ LiveFeed (staleness) · HistoryStore (20.48 s of 1 ms columns) · resolveView per frame (cached by hash)     │
 │ curves: resolve(raw) → overlaySmoothed(frame) → analysis::staticGain / netGainDb / localRatio              │
 │ Characteristics: PreviewWorker → analysis::stepResponse; scResponse; colourCurve; harmonicsDb             │
 │ Panel::tick(dt) → Panel::draw(Canvas) → PrimList → BgfxSink → bgfx/Metal   (headless: → dump / fingerprint)│
 └─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

Three properties make the display truthful:
- **One resolver.** `resolve()` (01 §4.4) serves the audio thread, the UI, host `valueToText`, and the probes. There is
  no second place that knows what a Mode does to a parameter.
- **One set of curves.** The analysis functions (01 §7) instantiate the *same policy templates* the audio thread runs;
  `dsp.analysis` proves `staticGr` bit-equal to the audio kernel and `stepResponse` bit-identical to an engine render.
- **What the audio used.** While live, the UI overlays the smoothed values from `UiFrame` onto `resolve()`'s output
  (`overlaySmoothed`, 01 §6.2), so the curve shows what is happening now, and `m[8]`/`topo`/`flags` still come from the
  resolver.

### 4.2 Parameter model in one paragraph

29 host parameters (01 §3.1): 22 **Mode-filtered** ones (`thr ratio knee range atk rel tmode hold look det schpf sce
link stmode voice drive makeup automu mix s2thr s2atk s2rel`) and 7 **globals** (`mode extkey listen delta bypass
quality labudget`). Ranges are universal supersets (ratio is stored as the slope S = 1 − 1/R ∈ [0, 2]); integer lists
have capacity 8; `mode` is `AudioParameterInt 0..127` with a stable `modeId` string in state. The host order is the
frozen `kApvtsOrder`; parameter IDs are the automation identity. Setup and monitoring globals (`quality`, `labudget`,
`listen`, `delta`, `extkey`) are non-automatable and never in presets.

### 4.3 The raw value is truth; snapping happens on read

A Mode switch writes **only `mode`** (ADR-12). Every other raw value persists, and every reader resolves it through the
active Mode: Bus G snaps a raw ratio of 5.3:1 to its 4:1 step; Opto 2A shows its fixed attack while the raw attack
waits, untouched, for the next Mode. Consequences: A→B→A restores A bit for bit; one Mode switch is one host gesture;
the audio thread never writes a parameter; the D3 quantisation probe can compute every expected value. The UI "snaps
on write" — it only ever writes exact detent values — and "switch + load Mode defaults" is an explicit Alt-click
gesture inside a batch. The one hazard of snap-on-read — a raw value from Mode A landing on a "circuit off" step in
Mode B — is removed by design (FET 76's attack OFF moved off the attack slot) and guarded by the `crossmode.no_off`
registry lint (ADR-25).

---

## 5. The Mode system

### 5.1 A Mode = descriptor + traits

```
Source/fcdsp/modes/fet-76/
  Fet76Desc.cpp   constexpr ModeDescriptor kFet76 { key "fet-76", name "FET 76", group fet, revision 1,
                     ParamTable (a ParamSpec per Mode-filtered Pid), physical(), detectorLaw, attack/release specs,
                     internals[], rigor character, family custom, … }
  Fet76.h         struct Fet76 { Detector PeakLog; Computer QuadKnee; Link LinkMax; Ballistics SmoothBranching;
                                 Stage2 NoStage2; Colour ColourSelect<FetColour>; ScShape Flat; kTopologies FB; }
  Fet76.cpp       FCDSP_DEFINE_MODE(Fet76)      // instantiates ModeEngine<Fet76> and its analysis entry points here
Source/fcdsp/modes/Modes.def:   FCMP_MODE(2, "fet-76", Fet76)          // slot + key: v1-forever
```

- The **descriptor** (01 §4.3) is pure data: how each of the 22 Mode-filtered parameters behaves in this Mode, plus
  metadata the UI and probes use (topology caption, spec line, detector law, rigor, time specs, internals).
- The **traits** pick one policy per stage slot from a catalogue of single-header policies (01 §5.2). `ModeEngine<T>`
  (01 §5.3) fuses them into one engine that the host calls through **one virtual call per 64-sample chunk**; engines
  are placement-constructed into two preallocated 8 KiB arena slots, so a Mode switch allocates nothing.
- **`Modes.def`** is the single Mode list: the registry and the CTest matrix are generated from it (01 §8). Slots and
  keys are append-only; retired Modes keep their slot and name a successor.

### 5.2 How one UI serves every Mode

Every Mode-filtered parameter has exactly one place on the panel (02 §6.4): 21 slots in three rows of seven, plus the
AUTO word on MAKEUP. What changes per Mode is the **state** of each slot, derived from the descriptor by the resolver:

| `ParamSpec::Kind` | Slot state | What the user sees (02 §8.1) | Example |
|---|---|---|---|
| `continuous` | live | 1 px track, caret, value; soft notches optional | Clean everything |
| `stepped` | stepped | detent ticks with labels (if they fit), caret snaps, arrows move one detent | Bus G ratio 2 · 4 · 10 |
| `hybrid` | live inside [lo, hi], stepped on an end cell | continuous track with 16 px end cells | (gallery only in v1) |
| `locked` | locked | dotted track, fixed value in ink32, refuses writes, footer gives the reason | Opto 2A attack ~10 ms |
| `derived` | derived | hollow caret at the derived value, "= RATIO" tag, read-only | Bus G knee follows ratio |
| `notApplicable` | n/a | label and "–" in ink16, no track; still reachable for its reason | Opto 2A range |

Orthogonal markers ride on any state: a Mode **label** and **display map** ("INPUT 0–48" over `thr`, inverted so the
track rises with the displayed quantity), **extension** "+" (not on the original unit, neutral at default),
**program-dependent** "~" with a live EFF readout, **clamped** (raw outside the Mode's sub-range), and **dependent
lists** (a variant spec chosen by another parameter's step, e.g. Clean's drive becomes n/a while VOICE is OFF).

**Stepped and locked parameters are exact.** A stepped spec lists canonical host-plain values; `snap()` picks the
nearest in the parameter's snap domain (log for times, S for ratio, linear dB), ties to the lower step, no hysteresis;
the UI writes only those canonical values; the host lane may sit between detents while its text shows the snapped step.
A locked spec has one value and a mandatory `reason`. Every non-live spec must carry a reason (registry lint), and the
UI renders it in the footer and in accessibility help.

### 5.3 Adding a Mode

1. **Descriptor** (a descriptor-wave task or the lead): `modes/<key>/<Traits>Desc.cpp` with the parameter table,
   `physical()`, time specs, internals; `<Traits>.h` with generic traits from existing policies; `<Traits>.cpp` with
   `FCDSP_DEFINE_MODE`; the `FCMP_MODE` line. The Mode is `provisional`. `dsp.registry`, `dsp.quant.<key>` and the text
   round trip must pass. The UI shows it immediately, with no UI change.
2. **DSP** (a Mode task owning `modes/<key>/**` and any new policy headers): real policies, [H] constants fitted,
   `provisional` cleared, `docs/modes/<key>.md` written.
3. **Nothing else.** The per-directory globs pick up the files; 24 per-Mode tests appear (13 dsp, 5 proc, 6 ui); spec
   rows run at once; missing goldens are reported as candidates, and the lead blesses them. Factory presets for the
   Mode go in its own `Source/plugin/factory/<key>.inc`.

After release, a change that moves a shipped Mode's default sound needs `revision++`, and sessions saved with the old
revision get a footer notice (ADR-29).

### 5.4 The first eight Modes

Clean (fully continuous, the golden baseline), Bus G (stepped lists, AUTO release), FET 76 (INPUT drives a fixed
threshold, ALL buttons, feedback), Opto 2A (T4 cell, program-dependent), Mu 67 (progressive ratio, TC selector,
Lat/Vert), Diode 609 (two-stage shared element), Bus 25 (FF/FB switch, CV-sum link), Brickwall (lookahead limiter, true
peak). Their descriptors and the state matrix are 01 §10.

---

## 6. UI structure

### 6.1 FunkGui in four layers

```
  FunkGui::core (no bgfx, no ObjC, no Component)                        FunkGui::gpu
  ┌────────────────────────────────────────────────────────────────┐   ┌───────────────────────────────────────┐
  │ FontService (CPU SDF atlas) → Canvas (recorder) → PrimList      │   │ EditorHost : juce::AudioProcessorEditor│
  │ Panel (tick(dt), draw, input, a11y model)                       │──▶│   converts JUCE events → Panel input   │
  │ widgets: RuleSlider SegmentedSelector LatchToggle AttachedWord  │   │   submitFrame: tick → draw → BgfxSink  │
  │ ValueModel (5 states) · ParamPort · GestureController           │   │ BgfxContext · FramePump · DisplayLink  │
  │ HeadlessHost (fixed dt, replay) · Fingerprint · dump v2         │   │ NativeSurface · A11yBridge             │
  │ SoftRaster (CPU PNG of any frame)                               │   └───────────────────────────────────────┘
  └────────────────────────────────────────────────────────────────┘
```

The canvas records 84-byte primitives (with a semantic tag and a `live` flag) instead of drawing directly, so the same
panel renders headless in a console probe and on the GPU in the plugin, and a live capture fingerprints bit-equal to
its headless twin (02 §3). The GPU path expands the list into HR's exact vertex stream. FunkGui is an INTERFACE
library whose sources compile inside each consumer with the consumer's JUCE configuration (02 §1.3).

### 6.2 The main panel (960×640, fixed)

```
 y 0–60     HEADER     F COMPRESSOR · topology caption │ preset strip ‹ name › │ MODE ‹ BUS G › · group line
 y 64–120   DISPLAY    GAIN REDUCTION −4.2 DB (big readout) │ QUALITY ECO/STD/HQ │ LOOKAHEAD OFF/5/20 │
                       [DELTA] [BYPASS] [CHARACTERISTICS]
 y 124–600  MIDDLE (swaps between PANEL and CHARACTERISTICS)
   PANEL:   BAND  y 140–332   HISTORY 500×192 (IN area, OUT, GR hanging, DET)  │ TRANSFER 192×192 │ METERS IN GR OUT
            ROW P y 362       THRESHOLD  RATIO  KNEE  ATTACK  RELEASE  MAKEUP [AUTO]  MIX
            ROW B y 450       RANGE  DETECT [EXT]  HOLD  LOOKAHEAD  TIME MODE  DRIVE  VOICE
            ROW C y 522       SC HPF [LISTEN]  SC EMPH  LINK  STEREO  S2 THRESH  S2 ATTACK  S2 RELEASE
 y 604      FOOTER     spec line of the hovered item / reasons / notices │ THEME GRAPHITE PAPER
```

- **Fixed panel, never re-laid-out** (HR's rules; user decision ADR-06): `setResizable(false, false)` and one
  `setSize(960, 640)`; the chrome (header, display row, footer) never moves; every slot keeps its coordinates in every
  Mode. All 29 parameters are placed with no compound cells (02 §6.4).
- **The band is always visible** (user decision ADR-05): HISTORY (time runs left, 1 ms columns, min/max so spikes
  survive), TRANSFER (the static curve pre-makeup, the net curve with makeup and mix, ghosts of other detents, the
  operating dot with its GR needle, draggable threshold/knee/ratio/range handles), and METERS. All three share one
  dB→y map (4 px/dB at the default 48 dB scale), so the threshold line runs straight into the transfer handle.
- One colour token, **signal**, means *live gain reduction* and nothing else; accent means *the thing under the hand*.

### 6.3 The Characteristics screen

The `CHARACTERISTICS` latch (or Return, or a double-click on the band) swaps the middle region for the full-panel
screen (02 §7); the chrome stays in identical pixels, so Mode, bypass and quality stay reachable. It shows a larger
HISTORY and TRANSFER (plus the stage-1 curve for two-stage Modes), four meters (IN, SC, GR, OUT), READOUTS (detector
level, over-threshold, target and applied GR, effective ratio/attack/release, crest, phase, and the Mode's internals),
**CONTROL PATH** (target vs applied GR with the min/max band, the phase lane, the Mode's one history internal, event
stripes), **STEP RESPONSE** (attack and release, computed by `analysis::stepResponse` on a worker, with the declared
spec and the measured crossing), and a tabbed **SIDECHAIN | COLOUR** pane (the detector-path filter response with an
SC HPF handle; the colour stage's static transfer and harmonics). Every plotted parameter has a handle that proxies its
slot's model, with identical behaviour and accessibility. Whether "full-panel" may keep the chrome (this design's
reading) is Q4.

### 6.4 Keyboard and accessibility

Every slot is a Tab stop, including locked, derived and n/a ones (so their reason can be heard). Stepped sliders
expose an index space `{0..n−1}` with step 1 (VoiceOver increments work); arrows move one detent; handled keys return
`true` so Logic and Live do not eat them. Probe `ui.a11y` gates the accessibility model per Mode as text.

---

## 7. Threading and real-time rules

| Thread | Does | Never does |
|---|---|---|
| **Audio** (`processBlock`) | relaxed parameter reads, `resolve()`, `EngineHost::process`, seqlock publish, history push | allocate, lock, syscall, log, call `setLatencySamples`/`updateHostDisplay`/`triggerAsyncUpdate`, write a parameter |
| **Message** | UI, gestures, `SetupWatcher` (20 Hz), state load, preset apply, analysis | touch engine state directly |
| **`PreviewWorker`** (UI) | `analysis::stepResponse` off the message thread | draw; hold state the drawing depends on in probes |
| **`prepareToPlay`'s thread** | `EngineHost::configure` (the only allocation point) + `setLatencySamples` | – |

Rules (01 §2.3, §5; K2):
1. **No parameter listeners, no `AsyncUpdater`** in the processor. JUCE's VST3 wrapper applies parameter changes
   inside `process()`, so listeners would fire on the audio thread. A 20 Hz message-thread `SetupWatcher` applies
   `quality`/`labudget` (reconfigure under `suspendProcessing` + new latency) and announces Mode changes to the host.
2. **Batches.** State load, preset apply and multi-parameter UI writes bracket their writes with
   `beginBatch()/endBatch()`; while a batch is open the audio thread reuses the previous `BlockParams`, so it never
   resolves a half-written set (a transient "OFF" or a spurious crossfade).
3. **Telemetry is lock-free and gated.** `UiFrame` is a seqlock over atomic words (reader retries ≤ 8 times, keeps its
   last frame on contention); `HistoryRing` is SPSC with a claim word so a reader never accepts a torn column. Both run
   only while the editor-lifetime attach **count** is > 0.
4. **Determinism.** `ScopedFtz` in `EngineHost::process` and in every analysis entry point; `-ffp-contract=off`, no
   `-ffast-math`; fma-only `log2/exp2/tanh/logCosh/tanPi` instead of libm on the audio path (lint-enforced); input
   sanitised (NaN/inf → 0, ±1e6 clamp) before any delay line; block-size invariance via absolute-index control ticks
   and 1 ms history columns.
5. **Nothing steps.** Every gain-changing transition ramps: parameter smoothing (20 ms one-poles with exact landing),
   GR OFF and Stage-2 OFF (20 ms ramps), bypass/listen/delta (20 ms), kernel crossfades (20 ms, starts ≥ 50 ms apart).
6. **Editor teardown** never outlives what it references: parameter ports belong to the processor; the Panel stops its
   worker before gestures close.
7. **Gates**: an allocation counter in every `dsp.rt.<key>` run; RTSan (or a lock/syscall interposer) and a
   `tsan-agent` preset at milestones; `validate.sh` (auval `-strict`, pluginval level 10) at every sprint end.

---

## 8. Latency policy

`latency = lookaheadSamples(labudget, fs) + kOs[quality].latency` — a function of the two **global setup** parameters
only, never of the Mode (01 §5.6; ADR-15):

| `quality` | Oversampling | Latency | Notes |
|---|---|---|---|
| ECO | none | 0 | colour runs ADAA-1 at base rate |
| STD (default) | 2× polyphase IIR halfband + a Thiran fractional delay | `kStdLatency` (target ≤ 4) | every Mode runs through the filters, so dry and wet are phase-identical |
| HQ | 4× linear-phase FIR halfbands | `kHqLatency` (target ≤ 64) | |

| `labudget` | Lookahead | Effect on `look` |
|---|---|---|
| OFF (default) | 0 | the resolver locks `look` at 0 with a reason; Brickwall runs zero-latency and may overshoot (footer says so) |
| 5 MS / 20 MS | ceil(ms·fs/1000) samples | `look` clamped to the budget; moving `look` never changes latency (the side chain is delayed by `L_la − look + D_up`) |

- Adding a Mode never changes latency; a Mode switch never changes latency; a Quality or budget change is "one block of
  silence plus a PDC change", applied from `prepareToPlay` or `SetupWatcher`, never implicitly.
- Dry/wet mixing happens **inside** the oversampled domain, against an un-pre-gained dry signal, so parallel
  compression is phase-coherent. The price: at STD/HQ, mix = 0 is `down(up(x))`, not a bit-exact delay; the bit-exact
  transparent path is bypass. The null probes are specified accordingly (ADR-17).
- `kStdLatency`, `kHqLatency` and the up-stage delays freeze at FZ2 (end of the oversampler spike) and are v1-forever.

---

## 9. State and presets

**Session state** (01 §9.1) is the APVTS tree in plain units plus identity attributes:

```xml
<PARAMS stateVersion="1" modeId="fet-76" modeRev="1" product="FCompressor" build="0.1.0">
  <PARAM id="thr" value="-24"/> …                       <!-- every host parameter, plain units; absent → default -->
  <PRESET …/>                                           <!-- current preset identity, via hooks installed by P3 -->
  <UI charExpanded="0" scTab="sidechain"/>              <!-- per-instance editor state, never parameters -->
</PARAMS>
```

- `modeId` (a key string) is authoritative over the `mode` slot; unknown or retired keys load the successor (or
  `clean`) with a footer notice; a newer `stateVersion` loads best effort with a notice; an older `modeRev` gets a
  notice. Loading resets `listen` and `delta`, runs inside one batch, and raises the engine snap after the last write.
- No `UndoManager`: undo belongs to the host, which records every UI gesture (ADR-23).

**Presets** (01 §9.2) store **raw** values of the Mode-filtered parameters keyed by ID plus `modeId`/`modeRev`
attributes; the Mode snaps them on read. The store is HR's preset core, generalised into `FunkGui::presets` (SQLite from
the macOS SDK, WAL, in-memory fallback; `~/Library/Application Support/FCompressor/Presets.db`), pending Q2. The factory
bank is built from per-Mode `factory/<key>.inc` files with a content-hashed revision. There are no host programs.

---

## 10. Verification strategy

(03 §3.) Everything runs as CTest tests with the label `verify`; `Scripts/verify.sh` is the agent's pass/fail gate.

1. **Spec rows vs golden rows.** A *spec* row compares a measurement with a value the code declares — the Mode's
   descriptor (curve, knee, time constants, detents), an invariant (null, bit-exactness, zero allocations, latency ==
   declared). It is evaluated in the process and can never be blessed. A *golden* row detects drift against a blessed
   measurement. A new Mode is held to its declared behaviour before any golden exists.
2. **Table-driven over the registry.** Every probe file self-registers (`// FCMP_PROBE layer=dsp name=static
   scope=mode timeout=60`), and every `scope=mode` probe becomes one test per Mode in `Modes.def`: 13 `dsp`, 5 `proc`, 6
   `ui` per Mode; ≈ 207 tests for 8 Modes, ≈ 20–25 s at `-j4`.
3. **Three probe layers.** `dsp.*` (JUCE-free: static curve, time constants, quantisation, link, switch clicks, zipper,
   latency, nulls, hostile input, sample-rate sweep, real-time, analysis bit-identity, fingerprints), `proc.*` (layout,
   host text, state round trips into non-fresh instances, fixtures, bypass, latency under setup changes), `ui.*`
   (geometry fingerprints per view × Mode × dpi with theme invariance, curve-vs-analysis, operating-point truth, a11y
   text, input replay, text fit). UI probes run headless on the recorder; `ui.live` compares a real Standalone capture
   with the headless fingerprint.
4. **Goldens are source**, in `tests/golden/{base,arm64,x86_64}/{global,modes/<key>}/<layer>.<name>.txt`, with
   per-arch overlays holding only differing rows; `xarch.` rows must match across arches. Adding a Mode rewrites no
   other Mode's goldens (a Mode owns its switch pairs with lower slots; global probes are spec-only).
5. **Only the lead blesses** (user decision ADR-08), and structurally: probes cannot write into the tree; they emit
   candidates; `golden.py adopt` runs only in the main checkout with `FCMP_ALLOW_BLESS=1`, refuses provisional Modes
   and pre-freeze UI geometry, and records a reason per group.
6. **Exit codes:** 0 pass, 1 spec fail (blocking), 2 golden drift and 3 golden missing (candidates with a reason), 4
   harness error (blocking).
7. **Beyond `verify`:** `validate.sh` (auval `-strict`, pluginval 10) every sprint end; asan/tsan/tsan-agent/rtsan,
   `gui-live` and a universal build at milestones (S4, S8, S12); `bench` alone.

---

## 11. Build and FetchContent strategy

(03 §1–§2.)
- `cmake_minimum_required(3.30)`; Release default; macOS 14.0 deployment target; Ninja.
- `cmake/FcmpDeps.cmake`: JUCE → bgfx (GPU only, with the prebuilt shaderc and hidden symbols) → FunkGui, each
  asserted; FunkGui configured with normal variables (`FUNKGUI_WITH_BGFX`, `FUNKGUI_WITH_PRESETS`,
  `FUNKGUI_HARNESS_ONLY` for DSP-only, `FUNKGUI_BUILD_TOOLS ON`); `fcmp-deps.txt` records provenance and any override.
- `cmake/FcmpSources.cmake`: per-directory `GLOB_RECURSE CONFIGURE_DEPENDS` → targets, written once in Sprint 0 and
  never edited again. `cmake/FcmpProbes.cmake`: self-registering probe tests over `Modes.def`.
- Flags: one `fcmp_flags` interface target for `fcdsp`, the plugin and every probe (`-O3 -fno-math-errno
  -fno-trapping-math -ffp-contract=off`, ISA flags as `SHELL:-Xarch_` pairs); LTO in Release only, never on JUCE inside
  probes; `-Werror` on our sources only.
- Presets: `agent` (headless, RelWithDebInfo — the agents' default), `agent-gui`, `dsp`, `lead` (Release+LTO, blessing),
  `owner` (installs), `release` (arm64), `universal` (only after an x86 verify), `lead-x86`, `asan`, `tsan`,
  `tsan-agent`, `rtsan`. Agent and lead builds must produce bit-identical hashes; a difference is a determinism bug.
- FunkGui: INTERFACE libraries with INTERFACE sources (`FunkGui::core`, `::gpu`, `::harness`, `::presets`), product
  identity by PRIVATE definitions (`funkgui_configure_product`: product name, env prefix `FCMP_`, ObjC name root,
  preferences folder); ObjC classes registered at runtime under randomised names so two FCompressor binaries in one host
  never collide; shaders compiled by `shaderc` into embedded Metal headers; the bundled JetBrains Mono subset and the
  bgfx licences shipped as bundle resources.
- Release: `Scripts/release.sh` signs (hardened runtime, audio-input entitlement), notarises and packages; it refuses a
  dirty tree, an override, a missing verify/validate stamp, extra exported symbols, or a universal build without an
  x86 verify.

---

## 12. Process: agents, ownership, sprints

(03 §4.)
- **The lead** (the user's main session) plans sprints, writes task manifests, creates the sprint base, reviews,
  commits, merges, tags FunkGui, bumps the pin, blesses, validates and installs. **Agents** (≤ 3 at once across both
  repositories) each own one task, one worktree per repository, their own build directories, and only the paths in
  their manifest's `OWNS` globs; they never commit, bless, install, or touch `~/audio/.deps` or HardwareReverb.
- **FCompressor agents** use harness-made git worktrees; **FunkGui agents** use `git worktree add` into
  `FunkGui.wt/s<N>-<task>` (user decision). FCompressor consumes only **tagged** FunkGui: a FunkGui feature needed in
  sprint N is tagged in sprint N−1.
- **One plan**, S0–S12 (03 §4.9): FunkGui G1–G8 (build + harness, API, recorder, widgets, shapes, cell widgets,
  EditorHost, presets), fcdsp F0–F9 (contracts, math, resolver, engine skeleton, oversampler, feedback, host, SC/link,
  analysis), a **descriptor wave** DW that writes all 8 descriptors before the schema freezes, processor P1–P3, Modes
  M1–M7, UI U1a–U7, hardening H1. Spikes run early: GPU build chain (S0), determinism (S1), recorder parity and
  oversampler (S2), feedback solvers (S3), descriptor schema (S4), live parity (S8).
- **Definition of done:** ownership clean; builds with no warnings; `verify.sh` 0 blocking results, with a reason for
  each candidate group; frozen headers compile standalone; cross-repo verify for FunkGui API changes; PNGs for UI
  changes; a ≤ 40-line handoff.

---

## 13. Frozen interfaces index

"Freeze" is when the interface becomes Sprint-frozen (changed only by a lead-approved revision between sprints); "v1"
marks what is frozen forever at the v1 tag. Freeze points: FZ0 end S0 · FZ1 end S1 · FZ2 end S2 · FZ3 end S4 · FZ4 end
S5 · FZ5 end S12 (UI geometry goldens) · v1 tag (03 §4.9.4).

| Interface | File | Spec | Freeze |
|---|---|---|---|
| Host parameter table, maps, defaults | `Source/fcdsp/params/HostParams.{h,cpp}` | 01 §3.1, §3.3 | FZ0; **v1** |
| `Pid`, `kResolveOrder`, `kSnapDomain`, **`kApvtsOrder`** | `Source/fcdsp/params/Pid.h` | 01 §3.2 | FZ0; `kApvtsOrder` **v1** |
| `Quality`, `LookaheadBudget`, `budgetMs` | `Source/fcdsp/params/Setup.h` | 01 §3.2 | FZ0 |
| `ParamSpec`, `Step`, `DisplayMap`, `SpecFlag`, `Variant`, `ParamEntry`, `ParamTable` | `Source/fcdsp/params/ParamSpec.h` | 01 §4.1 | FZ0; schema proven FZ3 |
| `EngineParams` (116 B) | `Source/fcdsp/params/EngineParams.h` | 01 §4.2 | FZ0 |
| `ModeDescriptor`, `InternalSpec`, `TimeSpec`, enums | `Source/fcdsp/modes/ModeDescriptor.h` | 01 §4.3 | FZ0; schema FZ3 |
| `RawParams`, `ResolvedParam`, `ParamView`, `snap`, `resolveView`, `resolve`, `modeDefaults` | `Source/fcdsp/params/Resolve.h` | 01 §4.4–4.5 | FZ0; semantics FZ1 |
| `FormattedValue`, `formatParts`, `formatValue`, `formatHost`, `parseHost` | `Source/fcdsp/params/Text.h` | 01 §4.6 | FZ0 |
| `Simd`, `FastMath`, `Units`, `ScopedFtz`, `Sanitize`, `Smoother4`, `LinearRamp`, `ControlTicker` | `Source/fcdsp/core/*.h` | 01 §5.1 | FZ0 (op names), FZ1 |
| Stage concepts, `FbAffine`, `StageCtx` | `Source/fcdsp/engine/Stage.h` | 01 §5.2 | FZ0 |
| `IEngine`, `Carry`, `ControlIo`, `EngineTelemetry`, `AudioIo`, `PrepareInfo`, `ModeEngine<T>` | `Source/fcdsp/engine/{IEngine,ModeEngine}.h` | 01 §5.3 | FZ0; behaviour FZ2 |
| `TestTap` | `Source/fcdsp/engine/TestTap.h` | 01 §5.4 | FZ0 |
| `EngineHost`, `HostConfig`, `BlockParams`, `ProcessIo` | `Source/fcdsp/engine/EngineHost.h` | 01 §5.4 | FZ0 |
| `KernelKey`, `PathState`, `kFadeMs`, `kMinFadeGapMs` | `Source/fcdsp/engine/host/Crossfade.h` | 01 §5.5 | FZ0 |
| `OsDesign`, `kOs`, `kStdLatency`, `kHqLatency`, up delays, `Oversampler` | `Source/fcdsp/engine/Oversampler.h` | 01 §5.6 | FZ0; values FZ2 → **v1** |
| `Seqlock<T>` | `Source/fcdsp/telemetry/Seqlock.h` | 01 §6.1 | FZ0 |
| `UiFrame` (288 B), `UiFlag`, `overlaySmoothed` | `Source/fcdsp/telemetry/UiFrame.h` | 01 §6.2 | FZ0 |
| `HistoryColumn` (32 B), `HistoryRing` | `Source/fcdsp/telemetry/HistoryRing.h` | 01 §6.3 | FZ0 |
| Analysis API | `Source/fcdsp/analysis/Analysis.h` | 01 §7 | FZ0 |
| `ModeEntry`, `ModeSlot`, registry functions | `Source/fcdsp/modes/Registry.h` | 01 §8.2 | FZ0 |
| `FCDSP_DEFINE_MODE`, `makeModeEntry<T>` | `Source/fcdsp/modes/DefineMode.h` | 01 §8.2 | FZ0 |
| `ModeKit` helpers and shared step tables | `Source/fcdsp/modes/ModeKit.h` | 01 §10.1 | per sprint |
| `Modes.def` slots and keys | `Source/fcdsp/modes/Modes.def` | 01 §8.1 | **v1** (append-only) |
| State XML, `modeId`, `modeRev`, `<UI>` | `Source/plugin/State.{h,cpp}` | 01 §9.1 | FZ0; **v1** |
| Preset payload and file format | FunkPresets + `Source/plugin/Presets.cpp` | 01 §9.2 | **v1** |
| `ProcessorFacade`, `UiState`, `StateNotice`, `PresetAccess`, `ScTab` | `Source/plugin/ProcessorFacade.h` | 02 §9.5 | FZ1 (needs FunkGui v0.2.0's `ParamPort`) |
| `SubView`, `Screen`, `Overlay`, `ViewSpec`, `views()`, `PanelOptions`, `Panel::setView` | `Source/editor/{SubView,Panel}.h` | 02 Part 2 intro | FZ4 |
| `Layout.h`, `Tags.h` (product tags ≥ 256) | `Source/editor/{Layout,Tags}.h` | 02 §6–§7 | FZ4 |
| `FakeFacade` | `Tools/probes/plugin/FakeFacade.h` | 02 §9.5 | FZ4 |
| FunkGui `Prim` (84 B), `PrimKind`, `PrimList`, `Canvas` (+`emit`), dump v2, `Fingerprint` | `include/funkgui/canvas/*.h` | 02 §3.2–3.9 | FZ1 (G2) |
| FunkGui `Panel`, `HostServices`, `Input`, `HeadlessHost` | `include/funkgui/panel/*.h` | 02 §3.5–3.6 | FZ1 |
| FunkGui `ParamPort`, `GestureController`, `ValueModel`, `ValueView`, `Detent`, widgets, `A11yItem` | `include/funkgui/{params,widgets,a11y}/*.h` | 02 §5 | FZ1 |
| FunkGui `EditorHost`, `EditorConfig` | `include/funkgui/gpu/EditorHost.h` | 02 §5.1 | at G7 (v0.7.0) |
| FunkGui CMake targets and functions | FunkGui `cmake/FunkGuiTargets.cmake` | 02 §1.2–1.9 | FZ0 |
| Harness v2 API, golden format v2, exit codes | FunkGui `include/funkgui/test/Harness.h` | 03 §3.2 | FZ0 |
| `FCMP_PROBE` macro, probe CLI, `// FCMP_PROBE` line grammar | `Tools/probes/common/ProbeRegistry.h` | 03 §2.9 | FZ0 |
| Glob → target map | `cmake/FcmpSources.cmake` | 03 §2.1 | FZ0 |

---

## 14. Glossary

- **Mode** — one compressor type: a `ModeDescriptor` (data) plus a traits struct (policies), registered in `Modes.def`.
- **Slot** — (UI) one fixed control position on the panel; (registry) a Mode's permanent index 0–127.
- **Raw / plain / host01** — the value stored in the host parameter, in plain units / normalised 0–1.
- **Resolve** — map raw values through the active Mode to per-slot states (`ParamView`) and engine values
  (`EngineParams`).
- **Kernel** — the engine instance for a (Mode, topology, detector, stereo mode, voice, key) combination; changing it
  crossfades.
- **Band** — the always-visible HISTORY/TRANSFER/METERS strip on the main panel.
- **Characteristics** — the full-panel internals screen.
- **Spec row / golden row** — a declared-value check that cannot be blessed / a drift check against a blessed value.
- **Lead / agent** — the user's main session / a worker session limited to its task's files.
- **[H]** — a heuristic constant awaiting fitting.
