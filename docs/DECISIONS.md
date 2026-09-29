# FCompressor decision log

Status: ADR-style log, synthesised 2026-09-22 (design phase; no code exists). Every decision behind
`docs/ARCHITECTURE.md` and the design appendices `docs/design/01-core-contracts.md` (01),
`02-funkgui-and-ui.md` (02) and `03-build-verify-process.md` (03) is recorded here with the chosen option, the
rejected options and the rationale. Sources: research reports `docs/research/{A..F}-*.md` (cited `A §x` … `F §x`),
critiques `docs/design/K{1,2,3}-*.md` (cited `K1 #n` …), HardwareReverb (HR, read-only).

Conventions:
- **[USER 2026-09-22]** marks a decision the user made; it is binding and not re-litigated here.
- **Accepted** decisions were made in design (Drafts 1–3 or this synthesis). **Pending Qn** means accepted as the
  default but listed for the user's confirmation in §"Open questions for the user".
- All non-user decisions are dated 2026-09-22.

---

## A. User decisions

### ADR-01 Every library through CMake FetchContent [USER 2026-09-22]
- **Decision:** JUCE 8.0.4 and bgfx.cmake v1.153.9385-561 (as HR pins them) and FunkGui come in through
  `FetchContent`; no vendored or copied third-party or shared sources.
- **Consequences:** `cmake/FcmpDeps.cmake` declares all three and asserts versions and SHAs after population (03 §2.3);
  a read-only machine cache `~/audio/.deps` feeds `FETCHCONTENT_SOURCE_DIR_*` (ADR-37). System SDK libraries (SQLite3,
  like Metal and CoreAudio) and prebuilt build tools are outside the rule pending Q2 and Q8.

### ADR-02 FunkGui is its own repository and CMake project [USER 2026-09-22]
- **Decision:** `/Users/seanfunk/audio/libraries/FunkGui`, its own git repo; CMake project and target `FunkGui`;
  namespace `funkgui`; consumed by `FetchContent_Declare(FunkGui GIT_REPOSITORY /Users/seanfunk/audio/libraries/FunkGui
  GIT_TAG <tag>)`; dev/agent builds may override with `-DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<path>`.

### ADR-03 FunkGui is seeded from a snapshot of HardwareReverb, then generalised [USER 2026-09-22]
- **Decision:** the seed is HR `Source/gui` plus shaders and the bundled font; generalisation follows in FunkGui.
- **Implementation:** ADR-32 (commit sequence, `SEED.tsv` provenance).

### ADR-04 HardwareReverb is read-only; it migrates after FCompressor v1 [USER 2026-09-22]
- **Decision:** no edits to HR and no builds in its build directories; HR moves onto FunkGui only after FCompressor v1,
  once its goldens prove identical. Nothing permanent depends on HR's `build/` contents.
- **Consequences:** `.deps` is populated from upstream (a one-time `--seed-from` copy is a read); the FunkGui seed is
  verified by sha256 on the day of the copy; FunkGui's `v1.0.0` is the tag HR migrates onto.

### ADR-05 Characteristics = always-visible band + full-panel screen [USER 2026-09-22]
- **Decision:** the main panel has a band (GR history, transfer curve, meters) that is always visible, plus an expand
  toggle opening a full-panel Characteristics screen with internals (detector/envelope, step responses, sidechain
  filter, colour transfer).
- **Implementation:** 02 §6.5, §7. The reading that "full-panel" keeps the chrome is Q4.

### ADR-06 Editor 960×640, fixed [USER 2026-09-22]
- **Decision:** no resize; HR's fixed-panel and never-relayout rules. `setResizable(false, false)` and one
  `setSize` (02 §5.1, §6.1).

### ADR-07 Product identity [USER 2026-09-22]
- **Decision:** manufacturer `Funk`, plugin code `Fcmp`, bundle id `com.funk.fcompressor`, formats AU/VST3/Standalone,
  macOS.

### ADR-08 Parallel work and ownership of commits and goldens [USER 2026-09-22]
- **Decision:** at most 3 agents run concurrently; FCompressor agents use git worktrees; FunkGui agents use their own
  `git worktree add`; commits at sprint boundaries by the lead after review; agents never bless goldens.
- **Implementation:** 03 §4 (agents never commit; `golden.py adopt` refuses outside the main checkout and without
  `FCMP_ALLOW_BLESS=1`). Whether the lead session counts toward the 3 is Q1.

### ADR-09 One UI for every Mode; Modes may lock or step parameters [USER 2026-09-22]
- **Decision:** all Modes share the same parameters and visual aids; some Modes restrict a parameter to finite
  increments (e.g. 2-4-10 ratio).
- **Implementation:** ADR-11, ADR-12, ADR-13; 02 §6.4 places every parameter once.

---

## B. Architecture and contracts

### ADR-10 Three layers with a JUCE-free DSP library
- **Decision:** `fcdsp` (JUCE-free STATIC library: parameters, resolver, registry, engine, oversampler, telemetry,
  analysis) ← `plugin` (JUCE processor) ← `editor` (JUCE + FunkGui). Every probe links `fcdsp`.
- **Rejected:** DSP inside the JUCE processor (probes would not test shipped object code; C §5.14); a JUCE-dependent
  engine using `juce::dsp` (E §9.2 row 10).
- **Rationale:** one archive for plugin and probes; bit-reproducibility; enforced by the target graph (01 §1–2).

### ADR-11 A 29-parameter universal superset; 128 Mode slots
- **Decision:** 22 Mode-filtered parameters + 7 globals (01 §3.1); ratio stored as slope S ∈ [0, 2]; integer lists of
  capacity 8; `mode` = `AudioParameterInt 0..127`, non-automatable, with a `modeId` key string in state; D's `in` is a
  remapped `thr` plus a character-only `drive`; no `topo` host parameter (folded into `voice`/`physical()`).
- **Rejected:** 64 slots (B §7.4.3); F's 15-slot panel; D's 24-slot list with `in`, `topo`, `lshape`, `out`.
- **Rationale:** 35 Modes already catalogued; `thr` keeps one meaning across Modes; saves a UI slot n/a in 6 of 8 Modes.

### ADR-12 The raw value is truth; snap on read; a Mode switch writes only `mode`
- **Decision:** every reader resolves raw values through the active Mode with one pure `resolve()`; the UI writes only
  exact detent values; "switch + load Mode defaults" is an explicit Alt-click inside a batch.
- **Rejected:** B §7.4.2 (rewrite raw values on a Mode switch); F §3.2's stepped-only variant.
- **Rationale:** A→B→A exact; one host gesture; no audio-thread writes; deterministic D3; a lane between detents shows
  the snapped text (accepted cost). The one hazard is closed by ADR-25.

### ADR-13 A Mode is a descriptor plus a traits struct
- **Decision:** constexpr `ModeDescriptor` (a `ParamSpec` per parameter + metadata) and a traits struct choosing one
  policy per stage slot, fused into `ModeEngine<T>`; one virtual call per 64-sample chunk; engines placement-constructed
  into two 8 KiB arena slots (01 §4.3, §5.3; E §3.5–3.6). `Remapped` and "program-dependent" are attributes, not kinds.
- **Rejected:** a virtual call per sample; per-Mode classes with bespoke UIs; C's `ModeSpec` and F's hooks (subsumed).

### ADR-14 Kernel crossfade with per-path state
- **Decision:** a change of Mode or of a kernel-selecting value (`det`, `stmode`, `voice`, topology, effective external
  key) runs a 20 ms equal-gain crossfade. Each path has a `PathState` (outgoing `EngineParams` frozen; its own
  preGain/makeup smoother); one `up()` and one `down()` per chunk; the new engine is seeded from `Carry`, with lanes
  merged by `max` when the M/S domain changes; crossfade starts ≥ 50 ms apart (01 §5.5).
- **Rejected:** Draft 1's shared host gain staging (the old path ran on the new Mode's preGain/makeup; K2 #3);
  unseeded switches; an FB↔FF flip without a fade on EXT toggles (K2 #22).

### ADR-15 Latency depends only on Quality and the lookahead budget; setup is applied off the audio thread
- **Decision:** `latency = lookaheadSamples(labudget) + kOs[quality].latency`. `quality` and `labudget` are
  non-automatable globals applied in `prepareToPlay` and by a 20 Hz message-thread `SetupWatcher` (reconfigure under
  `suspendProcessing`, then `setLatencySamples`). The processor has no APVTS listeners and no `AsyncUpdater`.
- **Rejected:** latency = max over Modes (B §7.4.5); Draft 1's `AsyncUpdater` path (VST3 fires listeners on the audio
  thread; `triggerAsyncUpdate` may block; K2 #6).

### ADR-16 FCompressor owns its oversampler; mixing happens in the OS domain
- **Decision:** `fcdsp::Oversampler` (2× polyphase IIR + a constexpr Thiran fractional delay; 4× linear-phase FIR),
  integer latencies frozen at FZ2; dry/wet mix inside the OS domain against an un-pre-gained dry; the side chain is
  delayed by `L_la − look + D_up − engine SC delay`; gain interpolated linearly in dB at the OS rate (01 §5.4, §5.6).
- **Rejected:** `juce::dsp::Oversampling` in the engine (E §2.9; breaks the JUCE-free rule); rounding the STD latency up
  without a fractional delay (bypass/PDC comb; K2 #11a); applying preGain before the dry tap (K2 #3c).

### ADR-17 The mix-0 and below-threshold null specs
- **Decision:** ECO: bit-exact against `delay(x, L)`; STD/HQ: bit-exact against a control `Oversampler` round trip plus
  passband ≤ 0.01 dB to 20 kHz; bypass bit-exact at every Quality (01 §5.6; 03 §3.4, §3.7).
- **Rejected:** C D8a / Draft 3 "mix 0 bit-exact against the delayed input" at every Quality (can never pass with mix
  in OS; K1 #2, K2 #12).

### ADR-18 The feedback-solver interface is affine
- **Decision:** `FbAffine{A, B}`; `G::solveFb(c, x, a)` returns the root of `r = A + B·r̂(x − r)`; ballistics expose
  `B::solveFb(c, s, solve)` (max of roots over branches) and `B::commitFb(c, s, r)`; link is applied between them; FB
  curves must be monotone (`fb.monotone`); the opto one-sample-delay guard runs in `prepare()` at the actual fs with a
  ZDF fallback (01 §5.2–5.3).
- **Rejected:** Draft 1's `alphaFor`/`solveFb(…, α)` (no single α for DualRelease, MultiStage3, Hold; K2 #1); K2's
  single `stepFb(solve)` (adapted: it had no place for link between solve and commit, K2 #5b); a `static_assert` guard
  (α depends on fs at runtime).

### ADR-19 Sources by per-directory globs; `FCDSP_DEFINE_MODE`; `Modes.def` drives only the registry and CTest
- **Decision:** `file(GLOB_RECURSE … CONFIGURE_DEPENDS)` per directory (`cmake/FcmpSources.cmake`, written once);
  each Mode TU ends with `FCDSP_DEFINE_MODE(Traits)`, which instantiates its engine and defines `kEntry_<Traits>`;
  `Registry.cpp` collects addresses from `Modes.def` (01 §8.2; 03 §2.1).
- **Rejected:** Draft 1's `Modes.def`-driven `target_sources` + generated `ModeIncludes.h` and **K1 #5**, which
  recommended keeping it (every new file would still need a lead CMake edit; one TU would instantiate every Mode;
  K3 #2, #6). K1 #5's concern — an unregistered Mode's half-written `.cpp` compiling into `fcdsp` — is real but cheap:
  it exists only in its owner's worktree, must compile for that task's DoD anyway, and its unreferenced entry is dropped
  by the linker. Draft 3's "glob only `Source/modes/*.cpp`" is also superseded.

### ADR-20 The canonical source tree
- **Decision:** `Source/fcdsp/{core,params,engine{,/host,/stages/<slot>},modes{,/<key>},telemetry,analysis}`,
  `Source/plugin/` (with `ProcessorFacade.h`), `Source/editor/` (with `gpu/Editor`), one directory per Mode, one header
  per stage policy, one file pair per UI sub-view (01 §2.1).
- **Rejected:** Draft 3's `Source/dsp/{simd,units,…}` + `Source/modes/`; `editor/ProcessorFacade.h` (the processor
  implements it); per-slot multi-policy headers (every Mode sprint would edit the same files; K3 #7).
- **Rationale:** directory = namespace = include prefix; file-disjoint parallel work (K1 #1, K3 #1).

### ADR-21 Self-registering probe files; names and golden paths
- **Decision:** one file per probe with a `// FCMP_PROBE layer=… name=… scope=… timeout=…` line and a static
  `FCMP_PROBE(layer, name)` registration; tests `<layer>.<name>[.<key>]`; golden files
  `tests/golden/{base,<arch>}/{global,modes/<key>}/<layer>.<name>.txt`; exit codes 0–4; executables
  `fcmp_probe_dsp`, `fcmp_probe_plugin`, `fcmp_bench` (03 §2.9, §3.2).
- **Rejected:** Draft 3's CMake lists and dispatch table; Draft 1's `FcmpDspProbe/FcmpProcProbe/FcmpUiProbe`,
  `--golden-dir`, `Tools/golden/`, "missing golden exits 2" (K1 #4, K3 #3); `<probe>.txt` without the layer (`dsp.null`
  and `proc.null` would collide).

### ADR-22 The test tap is a runtime pointer
- **Decision:** `EngineHost::setTap(TestTap*)`, always compiled; `TestTap` holds spans per sample; the host loads the
  pointer once per block and feeds `ControlIo`'s nullable outputs; the plugin never sets it (01 §5.4).
- **Rejected:** `#if FCMP_TEST_TAP` (C §5.1, Draft 1): a second `fcdsp` build or an ODR hazard (K1 #6, K2 #2). K1's
  raw-pointer `TestTap` and K2's span version differ only in form; spans chosen.

### ADR-23 No `UndoManager`
- **Decision:** `apvts(*this, nullptr, …)`; undo belongs to the host, which records every UI gesture (01 §9.1).
- **Rejected:** Draft 1 / E §4.3's `UndoManager` (JUCE's APVTS timer pushes host automation into one unbounded
  transaction; undo would revert automation; K2 #7). `ProcessorFacade::beginUndoTransaction` is deleted.

### ADR-24 No libm on the audio path
- **Decision:** `FastMath` gains fma-only `tanh`, `logCosh`, `tanPi`, `sinPi`, `cosPi` (scalar == lane 0); `lint.deps`
  rejects libm transcendentals under `fcdsp/{core,engine,modes}`; host-map-derived `proc.*` rows use `absrel`
  (01 §2.2, §5.1).
- **Rejected:** libm in colour stages and SVF design (Apple libm differs across arches and macOS releases; K2 #14).

### ADR-25 No "circuit off" step where another Mode's default lands; nothing steps
- **Decision:** FET 76's attack-knob OFF detent moves to FET's `tmode` slot, relabelled `GR` (ON = 0, OFF = 7);
  `atk` becomes continuous 0.02–0.8 ms; a `crossmode.no_off` registry lint checks every ordered Mode pair; GR OFF and
  Stage-2 OFF ramp over 20 ms; `dsp.zipper` gains detent-edge rows (01 §10.2, §10.5; K2 #4). User-visible: Q9.
- **Rejected:** Draft 1's hybrid `atk` with OFF at 1.0 ms (every raw attack > 0.894 ms resolved to OFF, so switching to
  FET 76 from Clean, Bus G, Bus 25 or Diode 609 disabled gain reduction).

### ADR-26 Telemetry: seqlock frame, claim-word history, lifetime count, overlay for live curves
- **Decision:** `UiFrame` 72 words (seqlock); `HistoryRing` 4096 × 32 B columns of 1 ms with min/max GR and a claim
  word (tear-free); both gated by an attach **count**; live curves = `resolve()` + `overlaySmoothed(frame)`
  (01 §6; 02 §9.1).
- **Rejected:** E's 57-word and F's 38-word frames; 2048 × 16 B history; a bool attach flag (HR); Draft 1's re-read
  check (accepted torn columns; K2 #8); building `EngineParams` from `UiFrame` alone (no `m[8]`, `topo`, `flags`;
  K1 #7).

### ADR-27 The lookahead budget is visible to the resolver
- **Decision:** `RawParams::budget` (the configured budget); `resolveView` locks `look` at 0 with a reason while the
  budget is OFF and clamps it otherwise; derived specs read the clamped value; the engine clamp is defensive only
  (01 §4.4).
- **Rejected:** an engine-only clamp (host text and UI disagreed with audio) and Draft 2's UI "locked" overlay (K1 #8).

### ADR-28 Host order is `kApvtsOrder`, separate from `Pid`
- **Decision:** `Pid` is internal and may regroup; `kApvtsOrder` is v1-forever and append-only (01 §3.2).
- **Rejected:** "Pid order == APVTS order, append only" (a v2 Mode-filtered parameter could not join the Mode block;
  K2 #9).

### ADR-29 Per-Mode sound revisions
- **Decision:** `ModeDescriptor::revision`; state and presets store `modeRev`; once a Mode is in `modes-ever.tsv`, a
  moved default print hash requires `revision++`; older sessions get a footer notice; step plain values and DisplayMaps
  of shipped Modes are v1-forever (01 §0, §9.1).
- **Rejected:** relying on `stateVersion` (cannot express a per-Mode change; K2 #10); emulating old revisions in v1.

### ADR-30 An early descriptor wave with provisional Modes
- **Decision:** task DW writes all 7 remaining descriptors in full with generic traits and `provisional = true` in S4,
  so the schema freezes (FZ3) on evidence from all 8 Modes and the UI never waits for DSP-complete Modes; `golden.py`
  refuses provisional rows; release builds refuse provisional Modes (01 §4.3, §8.4; 03 §4.9).
- **Rejected:** Draft 1's "four sketched, finished by their owners" (the hardest schema cases would arrive after UI code
  was built on the schema; K3 #9).

### ADR-31 FunkGui is a set of INTERFACE libraries; the consumer provides JUCE and bgfx
- **Decision:** `FunkGui::core`, `::gpu`, `::harness`, `::presets` (INTERFACE + INTERFACE sources), `FunkGuiFonts`
  (binary data); the consumer provides JUCE; bgfx is fetched only `if(NOT TARGET bgfx)` using normal variables;
  shaderc is either `FUNKGUI_SHADERC` or the bgfx target, guarded (02 §1).
- **Rejected:** a JUCE module (unity layout, no bgfx/shader/font channels); a STATIC library (compiles JUCE headers
  under FunkGui's flags; A §6.3); Draft 2's unconditional `shaderc` FATAL (broke every prebuilt-shaderc configure; K1
  #3, K2 #17); `CACHE … FORCE` in the fallback (leaks into the consumer's cache).

### ADR-32 FunkGui tag sequence and the pipelining rule
- **Decision:** lead pre-work G0 = verbatim HR copy + `SEED.tsv` (untagged), sed renames keeping HR file stems, a
  harness-only CMake → **v0.0.1**; G1 (targets + Harness v2) → v0.1.0; G2…G8 → v0.2.0…v0.8.0; FCompressor consumes only
  tags, one sprint behind (02 §2.1; 03 §4.5, §4.9).
- **Rejected:** **K1 #12**'s "drop v0.0.1; S0-H alone renames and builds, v0.1.0 first pin" (leaves two agent slots idle
  in S0 and keeps a bootstrap cycle; K3 #5); Draft 2's "v0.1.0 = full generalisation"; Draft 2's UI agent building on
  other agents' live worktrees (K3 #4).

### ADR-33 The FunkGui pin stays a tag; overrides must descend from it
- **Decision:** `GIT_TAG ${FCMP_FUNKGUI_TAG}` as the user decided, plus a post-population SHA assert; an override must
  pass `git merge-base --is-ancestor ${FCMP_FUNKGUI_SHA} HEAD`; `verify.sh --integration` fails with any override
  (03 §2.3).
- **Rejected:** **K2 #26a** (pin FunkGui by SHA in `GIT_TAG`): it contradicts the user's `GIT_TAG <tag>` decision, and
  the SHA assert already refuses a moved tag (fix: delete `_deps/funkgui-*`). Draft 3's "override VERSION ≥ pin"
  (VERSION changes only in tagging commits, so it cannot detect a stale worktree; K2 #26b).

### ADR-34 One test harness, in FunkGui — pending Q3
- **Decision:** Harness v2 (`FunkGui::harness`, header-only, JUCE-free): spec vs golden rows, tolerance grammar,
  duplicate-key errors, candidates only (`--bless-to`), `RESULT` JSON line (03 §3.2).
- **Rejected:** an FCompressor-owned harness with FunkGui duplicating a subset (two harnesses to keep in step, and HR's
  migration needs FunkGui's).

### ADR-35 The preset data layer lives in FunkGui; state does not depend on it — pending Q2
- **Decision:** `FunkGui::presets` (HR `presets/` generalised with `Attribute`, `ProductConfig`, `PresetHooks`), no GUI
  dependency, SQLite3 from the macOS SDK; written by G8 in the last sprint; `State.cpp` owns `<PARAMS>` and reaches
  `<PRESET>` only through optional hooks installed by P3 (01 §9; 02 §1.2).
- **Rejected:** copying HR's preset sources into FCompressor (violates ADR-01); putting FunkPresets on the state path
  (`proc.state` would wait on HR's preset work settling; K3 #14).
- **Fallback:** an FCompressor-owned `Source/plugin/presets/`, re-implemented from HR's headers.

### ADR-36 Prebuilt build tools in the machine cache — pending Q8
- **Decision:** `deps.sh` builds `shaderc` (pinned bgfx.cmake SHA, stamped) and `pluginval` (pinned tag) into
  `~/audio/.deps/tools`; builds fall back to building shaderc from the fetched bgfx.cmake automatically (03 §2.5).
- **Rationale:** the shaderc toolchain is 97.5 % of a cold bgfx build (2,060 of 2,112 CPU-s measured).

### ADR-37 A read-only machine dependency cache
- **Decision:** `~/audio/.deps` populated by `deps.sh`, used through `FETCHCONTENT_SOURCE_DIR_{JUCE,BGFX}` defaults;
  never `FETCHCONTENT_FULLY_DISCONNECTED` (03 §2.4).
- **Rejected:** per-build-dir clones (823 MB each); a shared `FETCHCONTENT_BASE_DIR` (concurrent builds share binary
  dirs); pointing at HR's `build/_deps` (forbidden by ADR-04); `FCompressor/.deps` or `plugins/.deps` (C §6.3, B §7.6).

### ADR-38 ObjC classes are registered at runtime under randomised names
- **Decision:** `juce::ObjCClass<NSView/NSObject>` with name roots `<OBJC_PREFIX>RenderView_` / `…DisplayLinkTarget_`
  (02 §1.8).
- **Rejected:** Draft 2's static `FcmpRenderView` (a prefix separates products, not binaries: AU + VST3 in one host, or
  dev + installed builds, would collide; K2 #16).

### ADR-39 The FCompressor panel is a fixed composition of sub-views with a probe-facing view API
- **Decision:** `SubView` per region, each in its own files; `Layout.h` and `Tags.h` complete and frozen at FZ4;
  `fcmp::ui::{Screen, Overlay, ViewSpec, views(), PanelOptions, Panel::setView}` with view ids `panel`,
  `chars.sidechain`, `chars.colour`, `modebrowser`, `presetbrowser`; one environment set `FCMP_UI_VIEW`,
  `FCMP_UI_NO_HINT`, `FCMP_UI_NO_LIVE`, `FCMP_UI_FIXED_DT` (02 Part 2 intro, §5.1).
- **Rejected:** Draft 2's monolithic `Panel.cpp` (a hot file for five UI tasks; K3 #15); Draft 3's undefined
  `setView/settled/views()` and band sub-views that do not exist (K1 #10); K3's `setScreen/setOverlay` pair (merged into
  `ViewSpec`, because the SC|COLOUR tab also changes geometry); `FCMP_UI_SCREEN`/`FCMP_UI_BROWSER`.

### ADR-40 `ProcessorFacade` lives in `plugin/` and hands out ports
- **Decision:** `Source/plugin/ProcessorFacade.h`: `port(Pid)` (29 `JuceParamPort`s owned by the processor, outliving
  every editor), `currentRaw()`, telemetry, `UiState`, `StateNotice`, `beginBatch/endBatch`, `PresetAccess`; a
  `FakeFacade` for probes and UI tasks (02 §9.5).
- **Rejected:** `apvts()` in the facade (every UI task would need a prepared processor; K3 #13); ports owned by the
  editor (use-after-free in `~EditorHost`; K2 #27 proposed Panel-owned ports — processor ownership satisfies both
  critiques); `registry()` (free functions suffice; K1 #14).

### ADR-41 No drawn state depends on thread timing
- **Decision:** determinism rule 7; with `PanelOptions::syncPreview` (probes, fixed-dt captures) step responses are
  computed inside `tick()`; `wantsFullRate()` stays true while a job is pending (02 §3.7).
- **Rejected:** Draft 2's worker-only `PreviewWorker` in probes (flaky geometry goldens; K1 #11).

### ADR-42 Handle drags in the plot's own units
- **Decision:** threshold handles sit at `analysis::inputThresholdDb(eng)` and drag by `thr_new = thr_cur + ΔT`
  (T_in is affine in `thr` with slope 1 in every Mode — the `thr.slope` lint); knee/range drag absolutely only with
  `kFlagPlotIsPlain` and an identity display map (Clean), otherwise relatively (02 §6.5; 01 §7).
- **Rejected:** Draft 2's absolute drags through `DisplayMap::toPlain` (wrong for FET's threshold offsets and Mu 67's DC
  THRESH; K1 #9).

### ADR-43 Formatting contract
- **Decision:** `formatParts` → `{value, unit, spoken, prefix}`; the value never contains the slot label; minus is
  U+2212 everywhere (parse accepts `-`); n/a prints U+2013; Mode-filtered parameters carry the JUCE label `""` and the
  unit in the text; host names are universal forever (01 §3.1, §4.6).
- **Rejected:** a single `formatValue` string (the UI needs value/unit/spoken; K1 #15); a fixed JUCE unit label (hosts
  would append "dB" to "INPUT 30"; K2 #25a).

### ADR-44 One sprint plan for both repositories
- **Decision:** 03 §4.9: S0–S12, ≤ 3 agent tasks per sprint across both repositories, disjoint ownership, FunkGui one
  sprint ahead; freeze points FZ0 (S0) … FZ5 (S12); spikes early (GPU chain S0, determinism S1, recorder and
  oversampler S2, FB solvers S3, schema S4, live parity S8).
- **Rejected:** Draft 2 §10 and Draft 3 §4.9 (incompatible, neither under one global cap; K3 #4); **K1 #13**'s five-sprint
  example table (not sized: several of its rows are larger than one agent session, and it has no descriptor wave).

### ADR-45 Golden churn and gate cadence
- **Decision:** `ui.geometry` goldens are adopted only at the UI freeze FZ5; FunkGui is tagged only in sprints that
  merge a FunkGui task; asan/tsan/tsan-agent/rtsan, `gui-live` and a universal build run at milestones (S4, S8, S12)
  (03 §4.8).
- **Rejected:** re-blessing UI geometry at every pin bump while the UI is still changing (K3 #21).

### ADR-46 Host-validation and real-time gates
- **Decision:** `Scripts/validate.sh` (auval `-strict`, pluginval strictness 10, repeat 2, randomised) every sprint end
  and before release; an `rtsan` preset with `[[clang::nonblocking]]` on `EngineHost::process`, the `IEngine` per-chunk
  calls and `processBlock`, falling back to an interposer; a headless `tsan-agent` preset running `proc.*`/`ui.*`
  (03 §2.10, §4.8).
- **Adapted from K2 #18:** `-Wfunction-effects` applies to `fcdsp` only; enabling it on `Processor::processBlock` would
  flag every call into JUCE, whose functions carry no effect annotations.

### ADR-47 v1 ships arm64-only unless an x86 verify has passed — pending Q6
- **Decision:** the `release` preset is arm64; `release.sh` accepts a universal build only with
  `build-lead-x86/verify-passed-<sha>`; `lead-x86` keeps compiling the SSE backend at sprint ends (03 §5).
- **Rejected:** shipping a universal binary whose SSE path has never executed (no Rosetta on this Mac; K2 #15).

### ADR-48 The layout owns the attached words; `tmode` is always a slot
- **Decision:** AUTO = `automu` on MAKEUP, EXT = `extkey` on DETECT, LISTEN = `listen` on SC HPF; a word is hidden when
  its parameter is n/a, disabled with the reason when locked; `kFlagLatchWord` and `kit::latch` are deleted (02 §6.4).
- **Rejected:** Draft 1's descriptor-driven latch words (K1 #23); compound cells (Draft 1 §12.1).

### ADR-49 No host programs; monitoring latches never persist
- **Decision:** `getNumPrograms() == 1`; `listen` and `delta` reset to 0 on state load (01 §3.1, §9.1).
- **Rejected:** "host programs are the factory bank" (Draft 1; HR returns 1 program, B §1.7; JUCE's VST3 program
  parameter would remap with the bank size; K2 #25d).

### ADR-50 Input sanitisation before any delay line
- **Decision:** NaN/inf → 0 and |x| ≤ 1e6 at `EngineHost::process` entry; the poison fallback outputs the sanitised,
  latency-aligned dry; meters are floored and clamped (01 §5.8).
- **Rejected:** checking only engine state (a NaN input would sit in the delay lines and re-emerge as the "dry"
  fallback; K2 #13).

### ADR-51 `AU_SANDBOX_SAFE` stays FALSE
- **Decision:** sandboxed hosts load FCompressor out of process, where the preset DB and preferences are reachable; the
  in-memory fallback remains (01 §9.2; K2 #19 asked for an explicit choice).
- **Rejected:** TRUE (in-process in the host sandbox: `~/Library/Application Support` unreachable, presets in memory).

### ADR-52 A new Mode rewrites no other Mode's goldens
- **Decision:** `dsp.switch.<key>` owns every pair with a lower-slot Mode, both directions; `dsp.registry` and
  `ui.browsers` are spec-only; each Mode's browser row is fingerprinted in its own `ui.geometry.<key>` (03 §3.4, §3.6).
- **Rejected:** Draft 3's "every pair with this Mode as source" and count/hash global rows (K3 #17).

### ADR-53 Factory presets per Mode, hashed bank revision
- **Decision:** P3 writes `Source/plugin/factory/FactoryBank.cpp` once; each Mode adds its own `factory/<key>.inc`; a
  configure-generated include list in slot order; `factoryBankRevision()` = first 32 bits of the SHA-256 of the `.inc`
  contents (01 §9.2).
- **Rejected:** a shared `FactoryPresets.cpp` with a hand-bumped counter (every Mode task edits it; K1 #31, K3 #18);
  K1's path `Source/plugin/presets/<key>.inc` (confusable with the FunkPresets fallback directory).

### ADR-54 Agents build RelWithDebInfo; the lead blesses from Release+LTO
- **Decision:** a hash difference between the two is a determinism bug, never a golden update; the F1 spike proves
  equality in S1 (03 §2.6, §2.10).
- **Fallback:** agents build Release with `FCOMPRESSOR_LTO=OFF`.

### ADR-55 Finite sentinels and the smoothing layout
- **Decision:** `kRangeOff = 60`, `kS2Off = 24` (the host range ends); the engine smooths `{thrDb, slope, min(range,
  60)}` and `{s2ThrDb, kneeDb}` per sample and ramps `offAmt`/`s2On` over 20 ms; each path smooths preGain and total
  makeup; drive is smoothed per tick by each engine's colour stage (01 §4.2, §5.1).
- **Rejected:** 1000 dB sentinels passing through smoothers (7 τ to leave OFF; an unsmoothed ON step; K2 #20).

### ADR-56 Descriptor corrections from the critiques
- **Decision:** Mu 67 ratio = `prog(derived(...))` showing the live EFF ratio (K1 #26); Brickwall `automu` n/a so
  makeup is not applied twice (K1 #27); `DetectorLaw::custom` axes labelled with the active `det` step (K1 #28);
  `ParamSpec::brief` for the locked sub-line (K1 #25); at most one history internal and ≤ 8 internals (K1 #20); step
  labels > 6 glyphs are a warning, the pair-fit rule is the gate (K1 #24).

### ADR-57 Probes that do not wait for the host; a componentised host
- **Decision:** `EngineRig` drives static/time/quant/link/analysis probes on a bare `ModeEngine`; `EngineHost.cpp` is
  orchestration over single-owner components in `engine/host/` (01 §5.4; 03 §3.4).
- **Rejected:** every spec probe through `EngineHost` (policies and host would serialise; K3 #10); a single-owner
  `EngineHost` holding every host feature (K3 #12).

### ADR-58 Batches for multi-parameter writes
- **Decision:** `beginBatch/endBatch` (a counter) around state load, preset apply, Alt-click defaults and `tapMany`;
  the audio thread reuses the previous `BlockParams` while a batch is open; `endBatch` raises the snap (01 §2.3; 02 §9.5).
- **Rejected:** unbracketed sequential writes (intermediate kernel crossfades or a transient OFF; K2 #23).

### ADR-59 Fixed calibration and sign conventions
- **Decision:** 0 dBFS = +22 dBu (0 VU = +4 dBu = −18 dBFS), no reference-level parameter; GR is positive dB of
  attenuation everywhere below the UI; meters are published in dB floored at −200 (01 §3.1, §6.2).
- **Rejected:** a global reference-level parameter (D; a candidate v2 append); F's GR ≤ 0 and linear meters.

### ADR-60 Oversampling-reference probe compares filters, not latency
- **Decision:** `proc.osref` checks passband and image rejection against JUCE; latency is `dsp.os`'s
  declared-equals-measured row; the Quality help text is formatted from `kOs` (03 §3.5; K1 #29, K2 #28).

### ADR-61 FunkGui licences from the fetched sources
- **Decision:** the bgfx, bx, bimg and bgfx.cmake licence texts are taken from `${bgfx_SOURCE_DIR}` at configure time
  and installed as bundle resources by `funkgui_add_font` in GPU configurations (02 §1.6; K1 #16).
- **Rejected:** copying HR's `Resources/licences/` into FunkGui (a copied shared source).

---

## C. Critique points not adopted as written

| Critique | Proposal | What was done instead | Why |
|---|---|---|---|
| K1 #5 | Keep `Modes.def`-driven `target_sources` + `ModeIncludes.h`; drop the glob | Globs + `FCDSP_DEFINE_MODE` (ADR-19) | Removes every shared CMake/C++ edit per Mode; the unregistered-TU concern is harmless |
| K1 #12 | Drop `v0.0.1`; first pin `v0.1.0` after a lone S0-H | Lead-made `v0.0.1` harness-only bootstrap (ADR-32) | S0 can run three agents; no bootstrap cycle |
| K1 #13 | A five-sprint example table | 03 §4.9's 13-sprint plan (ADR-44) | Sized tasks, descriptor wave, pipelining rule |
| K1 #6 / K2 #2 | `TestTap` with raw pointers / with spans | Spans (ADR-22) | Same contract; spans carry their capacity |
| K1 #10 / K3 #15 | View ids + `setView` / `setScreen` + `setOverlay` | `ViewSpec` + `setView` + sub-view composition (ADR-39) | The SC|COLOUR tab is part of a view's geometry |
| K1 #31 / K3 #18 | `plugin/presets/<key>.inc` / `plugin/factory/<key>.inc` | `plugin/factory/<key>.inc` (ADR-53) | Avoids clashing with the FunkPresets fallback path |
| K2 #1 | `B::stepFb(solve)` owning solve and commit | `B::solveFb` + `B::commitFb` with link between (ADR-18) | K2 #5b's link placement needs a seam between the two |
| K2 #18 | `[[clang::nonblocking]]` + `-Wfunction-effects` on `processBlock` | `-Wfunction-effects` on `fcdsp` only (ADR-46) | JUCE functions are unannotated; the warning would fire on every call |
| K2 #26a | Pin FunkGui by SHA in `GIT_TAG` | Tag in `GIT_TAG` + SHA assert (ADR-33) | Contradicts the user's `GIT_TAG <tag>`; the assert already catches moved tags |
| K2 #27 / K3 #13 | Panel-owned ports / facade `port(Pid)` | Processor-owned ports behind `port(Pid)` (ADR-40) | Outlives every editor; satisfies both |
| K2 #21b | STD: base-rate safety clip or documented overshoot | Documented overshoot (≤ ceiling + 1 dB TP spec), HQ ≤ +0.1 dB | A post-`down()` clip would sit outside the engine's colour stage and the Mode abstraction |

---

## Open questions for the user

Only genuine user-level choices. Each has a recommended default, which the design already assumes; answering
"default" changes nothing.

**Q1. Does the lead session count toward the 3-agent limit?**
The plan (03 §4.9) runs 3 agent tasks per sprint with the lead outside the count: 13 sprints (S0–S12).
*Recommended default: the lead does not count.* If it does, run 2 agents per sprint in the same order (≈ 20 sprints);
no design change.

**Q2. May FunkGui carry the preset data layer (`FunkGui::presets`), and may it link SQLite3 from the macOS SDK?**
This extends "a shared GUI library" to a GUI-independent sibling target, and SQLite would be a system library outside
the FetchContent rule (like Metal and CoreAudio). *Recommended default: yes to both* — it avoids copying HR's preset
code into FCompressor and gives HR's migration one preset core. Fallback: an FCompressor-owned
`Source/plugin/presets/`, re-implemented from HR's headers (ADR-35).

**Q3. May FunkGui carry the test harness (`FunkGui::harness`)?**
*Recommended default: yes* — one harness for FunkGui's own tests, FCompressor's probes and HR's later migration
(ADR-34). Alternative: an FCompressor-owned harness, with FunkGui keeping a subset.

**Q4. Does "full-panel Characteristics" keep the header, display row and footer?**
The design swaps only the middle region (y 124–600), so Mode, preset, bypass, quality and the toggle itself stay in
the same place (02 §7.1). *Recommended default: yes.* Alternatives: a truly full-window screen with its own close
control; or keep the chrome and add a strip of the 7 primary slots at y 560–600 (02 §11 Q1).

**Q5. Keep a 0–200 % mix (Clean only; hardware Modes clamp at 100 %)?**
The host range 0–2 is v1-forever once shipped. *Recommended default: keep it* (D §2.8, E §10.6; parallel "more than
wet" is a Pro-C-style feature). If not wanted, the range becomes 0–1 now, before v1.

**Q6. Intel support: install Rosetta and ship universal, or ship v1 arm64-only?**
Rosetta is not installed, so the x86 build compiles but has never run. *Recommended default: arm64-only for v1*
(ADR-47); if you want Intel, run `softwareupdate --install-rosetta --agree-to-license` and the lead adds `lead-x86`
verify to each sprint end. Apple has announced that full Rosetta support ends after macOS 27.

**Q7. Preferences (theme, meter scale, history span): per product or shared by all Funk plugins?**
*Recommended default: per product* (`~/Library/Application Support/FCompressor/`), so HR keeps its own when it
migrates (02 §11 Q3).

**Q8. Do prebuilt build tools fit "every library via FetchContent"?**
`shaderc` and `pluginval` are compiled once from pinned upstream sources into `~/audio/.deps/tools` — tools, not
libraries linked into the plugin. *Recommended default: yes* (saves ≈ 2,060 CPU-s per GPU build directory; the
build-from-source fallback for shaderc is automatic) (ADR-36).

**Q9. FET 76: move the attack knob's OFF detent to a separate `GR ON/OFF` switch?**
On the hardware, OFF is the attack knob's end stop. Kept there, every Mode switch into FET 76 from a Mode with an
attack above 0.9 ms would silently disable gain reduction (ADR-25). *Recommended default: yes, a separate switch* in the
TIME MODE slot, labelled `GR`.

**Q10. Fixed calibration 0 dBFS = +22 dBu, with no reference-level parameter in v1?**
*Recommended default: yes* (ADR-59); a reference-level parameter remains a candidate v2 append.

## Resolutions recorded at Sprint 0 (2026-09-22)

The user asked the lead to proceed through Sprints 0–3 without waiting, so the recommended defaults stand until the
user says otherwise: **Q1** the lead does not count toward the 3-agent cap · **Q2** FunkGui may carry
`FunkGui::presets` and link SQLite3 (re-confirm by the end of S6) · **Q3** the harness lives in FunkGui · **Q4** the
Characteristics screen keeps the chrome · **Q5** mix 0–200 % · **Q6** arm64-only for now (Rosetta is not installed; the
user may install it with `softwareupdate --install-rosetta --agree-to-license` to enable the universal target) ·
**Q7** preferences per product · **Q8** prebuilt `shaderc`/`pluginval` in `~/audio/.deps/tools` · **Q9** FET 76 gets a
separate `GR` switch · **Q10** fixed calibration 0 dBFS = +22 dBu.

## FZ3 — schema frozen on eight descriptors (2026-09-23, end of Sprint 4)

All eight first-wave descriptors (Clean final; Bus G, FET 76, Opto 2A, Mu 67, Diode 609, Bus 25, Brickwall provisional
on generic traits) fit `ParamSpec`/`ModeDescriptor` with **no declaration change**. The lead's rulings on DW's evidence:

- **ADR-62 `kTagOff` means only "main gain reduction disabled"** (FET 76's `GR` switch). Stage-2 OFF is expressed by
  `s2thr` = 24 (`kS2Off`), never by `kTagOff` — otherwise `crossmode.no_off` fails every pair into Diode 609.
- **ADR-63 Feedback time constants.** A Mode's `TimeSpec` declares the **published (closed-loop, measured)** time — the
  value `dsp.time` checks. A feedback Mode's `physical()` converts it to the open-loop ballistics τ (a loop with gain
  k runs ≈ (1 + c·k) faster). No schema field.
- **ADR-64 Tags shared by two Pids** (e.g. Diode 609's A1/A2 `kTagProgram` on RELEASE and LIMIT RELEASE) are
  disambiguated in `physical()` (route through `m[…]`), since `EngineParams::tags` is one OR over all parameters.
- **ADR-65 Declined for v1:** `hasColour` as a function of `EngineParams` (the UI shows an identity colour curve when a
  voice has no colour); a per-`EngineParams` topology caption (the UI reads `EngineParams::topo`); zeroing
  `makeupDb` in the null probe (Brickwall stays rigor *character*).
- **ADR-66 DW's probe relaxations approved** (hostile big-sample tail as NOTE; srsweep long-release on FF only; time
  GR-OFF threshold tracking and 2-ulp carry; zipper attack hold; static voice-topology NOTE). Revisit srsweep when the
  `FbAffine` base-GR item lands (Mu 67).
- **ADR-67 F5 rulings.** The 6-section bilinear tilt is judged over 200 Hz–2.5 kHz (±0.1 dB); outside, it deviates up
  to ~1 dB (40 Hz–10 kHz) and is drawn from its real `responseDb()`, so the UI stays truthful. `LinkCvSum` is the
  normalised node (own + k·other)/(1 + k). Router M/S decode is residual. **Known issue for M6 (Bus 25):** in a feedback
  loop, partial link commits and re-links every sample, so 40–90 % link behaves almost like 100 % (measured 99.9 % at
  40 %); a fix links the gain-computer term inside the affine map (a coupled solve) — M6 decides.
- **Mu 67** `AC THRESH` reads 0–10 with 10 = no compression (not inverted); 01 §10.2's matrix cell is corrected.
- **G5:** `WordModel : ToggleModel` with `visible()` accepted for AUTO/EXT/LISTEN words; RuleSlider API additions accepted.

- **Q6 answered by the user (2026-09-24): v1 ships arm64-only; Rosetta skipped.** The `universal` preset stays a compile gate; `release.sh` builds, signs and notarises arm64 only. Intel is a post-v1 backlog item.

## Ableton test feedback (user, 2026-09-24)

The user tested the Sprint 10 build in Ableton Live ("worked and functioned incredibly well") and reported three issues:

- **ADR-68 UI zoom (user decision).** Text at 10–11 px was too small. A machine-wide zoom preference 100 / 125 / 150 /
  175 % (default **125 %**) scales the whole panel uniformly — window size, render density, input and accessibility
  coordinates — with the layout unchanged (logical 960×640). Revises ADR-06's "one `setSize`": the editor resizes when
  the zoom changes. FunkGui `EditorHost` gains the zoom (card G7c → v0.8.0); FCompressor adds ZOOM cells beside THEME in
  the footer (card UF1b). Headless fingerprints stay in logical coordinates; `gui-live` runs at a pinned zoom.
- **ADR-69 No "disabled" look when audio stops (user report).** When the host stops calling `processBlock`, telemetry
  goes stale and the panel dimmed live content, which read as disabled/frozen. From now on: controls, labels and chrome
  are never dimmed by staleness; HISTORY keeps scrolling (silence / gap) at wall-clock rate; meters and live readouts
  fall to zero; the operating dot fades; the UI runs at full frame rate while the user interacts (card UF1a).
- **ADR-70 GAIN REDUCTION readout = peak hold + live bar (user decision).** The big readout shows the maximum GR over the
  last 1 s (holds, then falls), refreshed at ≈ 4 Hz, with a thin live GR bar beside it (card UF1a). The THRESHOLD slot's
  DET readout uses the operating dot's 10 ms rule (no jitter).
- **ADR-68a Zoom steps that do not fit (lead, from G7c).** FunkGui draws a zoom whose window exceeds the user area of
  the editor's display at the largest step that fits. On the user's MacBook (user area 1470 × 849 pt) only 100 % and
  125 % fit FCompressor's 960 × 640 panel. A silent fallback would read as a broken button, so FunkGui v0.8.0 adds
  `HostServices::zoomFits()`. The ZOOM cells draw a step that does not fit as unavailable, with a footer hint, and a
  click on it does nothing (UF1b).

- **ADR-72 GR VU meter (user request, 2026-09-25).** The band's HISTORY caption becomes a two-cell switch, **HISTORY ·
  VU**. VU replaces the scrolling GR history, in the same plot rectangle (40, 140, 500 × 192), with one large
  analog-style needle meter for gain reduction.
  - **Look:** panel style (the user's choice). Theme inks, a thin arc scale with ticks, a needle and a pivot; it
    follows GRAPHITE/PAPER.
  - **Scale and ballistics:** the classic GR-on-a-VU scale, with the needle at rest on 0 and swinging left as GR grows
    (0, −1, −2, −3, −5, −7, −10, −20 dB). VU ballistics: 99 % of a step in 300 ms, 1–1.5 % overshoot.
  - **Other settings:** the span cells do not apply while VU is shown. The choice is a machine-wide preference, like
    the span. Nothing else on the panel moves, and the history keeps recording while it is hidden. The Characteristics
    screen's HISTORY is unchanged. When audio stops, the needle falls back to rest with the same ballistics (ADR-69).
    Card UF2.
  - **As built (UF2):** the preference key is `grView`, and the switch has its own Tab stop before the span group (a
    radio group "GR view"). Deflection is 10^((dB − 3)/20), with 0 dB at 70.8 % of full scale. The needle is a
    mass-spring with ζ 0.8127 and ω0 13.51 rad/s: 99 % in 300 ms, 1.25 % overshoot. It is drawn on a display clock
    40 ms behind the newest audio, so host blocks never make it step.

- **ADR-73 PAPER contrast (user request, 2026-09-25).** The user asked for the white theme's contrast to be
  "majorly increased". FunkGui's `Theme::paper()` is HR's palette and stays as it is, since HR's goldens hold it.
  FCompressor draws with its **own** high-contrast PAPER instead: the Panel maps `themeIndex()` 1 to a product palette
  (`Source/editor`), and FunkGui changes nothing. The ground (the clear colour) stays `EDEBE6`, so EditorHost's clear
  and fallback screen still match. GRAPHITE is unchanged.
  - Measured on the old PAPER (WCAG ratio against the ground, with GRAPHITE's in brackets): ink70 5.47 (9.00), ink52
    3.76 (5.45), ink32 1.84 (2.66), ink16 1.27 (1.50), accent 3.33 (5.75), accentDim 1.42 (2.10), signal 3.93 (10.66).
    Every level was weaker than GRAPHITE's.
  - **Targets for the new PAPER (spec rows):** ink100 ≥ 15, ink70 ≥ 9, ink52 ≥ 6, ink32 ≥ 3.5, ink16 ≥ 1.8, accent
    ≥ 4.5, accentDim ≥ 2.2, signal ≥ 5.5. The ladder stays strictly ordered (ink100 > ink70 > ink52 > ink32 > ink16).
    `textGamma` is re-tuned so thin dark text does not read lighter than its ink.
  - Card H1a (S13), first deliverable. It adds a `ui.contrast` probe and PAPER PNGs of every view.

## After v1 (user requests, 2026-09-26)

- **ADR-74 Linked slots sit in a box (v1.1).** The user asked that a parameter that depends on another one, and so
  cannot be edited on its own, be visibly marked. Every slot in the derived state (for example Bus G's KNEE "= RATIO",
  Mu 67's RATIO "PROGRESSIVE" and ATTACK "= TIME", and Brickwall's ATTACK "= LOOKAHEAD") is drawn inside a box.
  - The box is the slot's hit rectangle, radius 4, with a 1 px ink32 border and an ink16 fill at 35 %, drawn under the
    slot (`SlotGrid::draw`, tag `LINKED_BOX`).
  - The tag that names what it follows, the "FOLLOWS …" sub-line and the footer spec stay as they were.
  - Locked slots (fixed by the Mode) keep their dotted track and are not boxed; they depend on no other control.
- **ADR-75 Each Mode has its own colour (v1.1).** The user asked for each compressor type to have its own colour
  theming, or at least a creative distinction between Modes.
  - Each Mode has one colour per theme (`ProductTheme.h` `kModeColours`), and it replaces the `signal` ink.
    Everything live about gain reduction therefore takes the Mode's colour, with no conditional in the views: the GR
    readout and live bar, the VU needle, the GR meter, the history's GR trace, the operating dot and RANGE's bar.
  - The rule under the Mode name in the header, now 2 px, is the Mode's colour, and each Mode browser row carries a
    swatch after its name, so the colour and the Mode are learnt together.
  - A Mode change eases the colour over 0.25 s (smootherstep).
  - The hues sit around the wheel, away from the accent's vermilion: FET 76 amber, Opto 2A lime, Mu 67 green, Clean
    cyan (the former signal), Bus G azure, Bus 25 periwinkle, Diode 609 violet, Brickwall magenta. Each is at least
    8:1 on GRAPHITE and 5.8:1 on PAPER (`ui.contrast` rows `contrast.mode.*`, targets 7 and 5.5).
  - A Mode not in the table draws Clean's colour.
  - The accent (the control under the hand) is unchanged.

- **ADR-76 Hardware-style GR meter faces (v1.1).** The user asked that each Mode's analog GR meter look like the
  equivalent hardware. The band's VU meter (ADR-72) now wears a face per Mode (`views/MeterFaces.h`). Each face
  evokes the meter of the hardware class the Mode models, drawn from general knowledge of those meters, with no maker's
  name or logo:
  - FET 76: an ivory VU with a red zone, on a black panel.
  - Opto 2A: a big warm backlit VU with its lamp glow, on a silver-grey panel.
  - Mu 67: a black meter in a bright ring bezel, DB COMPRESSION 0 → 20.
  - Diode 609: a dark grey-blue meter with cream print, GAIN REDUCTION 0 → 20 on a 1 dB ruler.
  - Bus G: a black console meter, COMPRESSION 20 … 0, with the needle resting right.
  - Bus 25: a cream console meter, GAIN REDUCTION 20 … 0, resting right.
  - Brickwall: a digital limiter's LED ladder, 24 × 1 dB, amber with red from 12 dB.
  - Clean keeps the panel face (ADR-72).
  The rules:
  - The hardware plates keep fixed colours in both themes, as hardware does. A small lamp lit in the Mode colour
    (ADR-75) sits in each plate's corner.
  - The needle keeps the ADR-72 ballistics, integrated in the face's own scale position (the needle's mass acts on its
    dial), so every needle face takes 300 ms to 99 % with 1.25 % overshoot.
  - The LED ladder shows the GR at once (after the display lag) and falls at 20 dB/s.
  - A needle's base hides under a shroud, and hardware needles stop at their pins.
  - `ui.vu` judges every face against its own law, recomputed in the probe, and the ladder with its own `led.*` rows.
- **ADR-77 Wave 2 Modes: Octo first (v1.2).** The user asked which Modes to add next and chose the hybrid VCA (D §2.7,
  M18) to start. The lead adds Wave 2 Modes directly, one branch each, with no sprint card. Each gets its
  `Modes.def` line (slot 8 onward, permanent), its traits, descriptor, factory `<key>.inc`, colour, meter face,
  a `docs/modes/<key>.md` sheet and its first goldens. Octo reuses the engine's policies: `QuadKnee`, `PeakLog`,
  `LinkMean`, and Bus G's `DualRelease` behind an `AutoSwitch` that engages only at the 10:1 OPTO step. It adds two
  stages to the catalogue:
  - `scshape/BandEmphasis.h`, a side-chain peaking bell;
  - `colour/OctoDist.h`, the AUDIO distortion generator with an optional 65 Hz third-order high-pass.

  INPUT is a gain into a fixed −18 dBFS threshold, so the host's threshold parameter keeps its meaning while the panel
  shows the unit's dial. The AUDIO voices are static, so the describing-function model holds. The Mode joins
  `modes-ever.tsv`, `modeparam.tsv` and the state fixtures when a release first ships it; until then it is
  revision 1. Its colour is rose, and its meter is a red LED ladder. Adding it touched 10 source files (808 lines,
  including the probe) plus its sheet, and no CMake or shared engine code.
- **ADR-78 Console E, and a Mode-local AUTO makeup (v1.2).** The second Wave 2 Mode is the SSL E/G channel dynamics
  (D §2.4, M10). It adds one stage, `ballistics/VcaChannel.h`: a program-dependent AUTO attack of 3–30 ms, and a LOG
  or LIN release, on SmoothBranching's step, with 20 ms rate blends on each switch. Decisions:
  - **AUTO makeup is referenced to 0 VU, not to 0 dBFS.** The unit's makeup keeps the output constant as the
    threshold falls. The engine's r^(0 dBFS) law would lift a 0 VU signal by up to 18 (1 − 1/R) dB, +12.6 dB at the
    defaults. So `physical()` clears `kEngAutoMakeup` and adds the static curve's GR at −18 dBFS to `makeupDb`. That
    is Brickwall's pattern, with no engine change.
  - **No colour stage.** The channel VCA is clean. VcaBus at 0 dB drive adds H3 near −105 dB at 0 VU. A live DRIVE
    exposed `VoiceDrive`'s per-tick steps against this Mode's clean RMS output (dsp.zipper 4.5 dB, limit 3). That is
    a shared smoothing change for every Mode with DRIVE, left for later.
  - **Two probe rows were made robust, not loosened.** `dsp.srsweep`'s long-release reference now follows the tapped
    target, so an RMS detector's settling is not charged to the ballistics. `dsp.time`'s GR OFF renders clear AUTO
    makeup, because the Rig applies makeup unsmoothed while the host ramps it. For every earlier Mode both give the
    same results as before.
- **ADR-79 Opto 3A is Opto 2A's cell with the LA-3A's constants (v1.2).** The third Wave 2 Mode is the UREI LA-3A
  (D §2.2, M03). It adds no new stage.
  - It runs Opto 2A's OptoSense, FeedbackDelayed<OptoCellCurve> and OptoCell. The differences are constants in `m[]`
    and the descriptor: attack 1.5 ms, and a memory that charges and releases in 2 s instead of 5 s.
  - LIMIT is 4:1, not 10:1, so COMP and LIMIT stay within 0.5 dB until heavy GR, as the LA-3A's manual says. The cell's
    law ties the knee to the exponent, so a milder exponent is the only way to keep the two close.
  - The rear HF pot is the R37 shelf.
  - The voice is a new instance of a now-templated TubeTransformer: `TubeTransformerT<kEvenPermille>`, 40 for Opto 3A.
    `TubeTransformer` stays 100 for Opto 2A, held bit-identical by a `static_assert`; no Opto 2A golden moved.
  - The cost was 3 source files, a template parameter, a probe, a preset file and two table rows.
- **ADR-80 Diode 54, and the first hybrid parameter on screen (v1.2).** The fourth Wave 2 Mode is the Neve 2254
  (D §2.5, M17), on Diode 609's engine with the 2254's switches:
  - RECOVERY .1/.2/.8 s + AUTO. AUTO's τ_Rs is fitted to 1.35 s so the measured 1/e lands inside the published 1.5 s.
  - The limiter runs to +20 dBu in 2 dB steps.
  - There is no SLOW high-pass.
  - ATTACK is the 2254/R's fixed 5 ms or its FAST pot, 0.1–2 ms. That is `Kind::hybrid`, the first use of a kind the
    contracts have had since Sprint 0.

  Two editor paths had never seen a hybrid, and both now follow the slider's view, where a hybrid is continuous even
  on its step:
  - `SlotModel::plotToHost01` accepts a hybrid attack or release: the marker drags on the time axis within the range.
  - `StepPlot::accessibility` rewrites the handle's value interface on the slider's view state, as SlotGrid already
    does.

  `ui.chars` and `ui.charscreen` caught both. No other Mode's output changed.
- **ADR-81 More detail on the hardware meter faces (v1.2).** The user asked for another pass on the analog GR meters
  to add more detail. Every hardware plate (ADR-76) now carries, under a new tag `METER_DETAIL` (334):
  - **Bezel and window:** a slotted screw in each corner of the bezel, each slot at its own angle. The window gets a
    recess (a stepped shadow under its top edge) and a glass sheen: five nested wedges from the top-left corner, so it
    fades with no hard edge.
  - **Lamp:** the Mode lamp (ADR-75) becomes a jewel, with a halo of its light, a metal rim and a specular point.
  - **Needle and shroud:** the needle casts a soft shadow onto the face and has a thicker base above the shroud. The
    shroud's top catches the light, and it carries a zero-adjust screw.
  - **VU faces (FET 76, Opto 2A, Opto 3A):** the classic lower scale, 0–100 % of 0 VU's voltage, on its own arc: ticks
    at 10 %, labels at 20–100 %, and "%" at the end.
  - **GR faces:** a 1 dB fine ruler where their marks are coarser (Mu 67, Bus G, Bus 25).
  - **LED ladders:** a bloom under the lit segments, a lens highlight on every segment (brighter when lit), smoked glass
    and a tick over each label.

  Rules for the detail:
  - Every detail derives from the face's own colours, so the MeterFace table did not change.
  - The panel face (Clean) stays flat and modern.
  - `METER_DETAIL` keeps the detail out of `ui.vu`'s needle, tick, label and LED-segment finders, so the law and
    ballistics rows are unchanged, while its "inside the plot" and "outside" raster rows still bound it. All 11 faces
    pass `ui.vu`.
  - No golden holds the VU view, so no golden moved.
- **ADR-82 Mu Mastering: LIMIT as a compressor followed by a limiter (v1.2).** The fifth Wave 2 Mode is the Manley
  Variable Mu, Mastering version (D §2.3, M06). Its LIMIT runs 4:1, rising toward 20:1 beyond about 12 dB of GR, which
  the manual itself calls "like a compressor followed by a limiter". Neither of our gain computers makes that shape:
  QuadKnee has one slope, and ProgressiveKnee rises smoothly with no 4:1 stretch. So it is built from what the diode
  Modes already run:
  - QuadKnee 4:1 in feedback, plus a 20:1 section on the same element: `SharedElementMaxT`, now templated on slope and
    knee.
  - The diode limiter stays `SharedElementMax` = 990 / 50, asserted bit-identical.
  - The rise shares the side chain's open-loop times.
  - Fitted: 4.00:1 at 6 dB of GR, 8:1 passed at 12.25 dB, 18.8:1 at 25 dB.

  `dsp.static` now judges an always-in stage 2:
  - The declared curve is `analysis::staticGr` with stage 2, the identity wherever stage 2 is off, so no other Mode
    moved.
  - Its per-sample FB rows step aside where stage 2 shares the element, as they already do for program-dependent
    ballistics.

  Two items are not modelled:
  - The unit's narrower COMPRESS threshold span, because the input threshold must stay affine with slope 1 (K1 #9).
  - The T-Bar tube.
- **ADR-83 Opto Tube 1B, and `tmode` 0 is manual timing everywhere (v1.2).** The sixth and last Wave 2 Mode is the
  Tube-Tech CL 1B (D §2.2, M04): an optical sensor, a real threshold with an OFF step (a hybrid), and a continuous 2–10:1
  ratio, feed-forward. Its ATTACK/RELEASE SELECT:
  - FIXED locks the knobs at 1 / 50 ms.
  - MANUAL uses the knobs.
  - FIX/MAN is DualRelease: the fixed 50 ms fast path, and the ATTACK knob relabelled DELAY (a Variant) setting a slow
    path that charges over 2 × DELAY. So a peak shorter than DELAY releases fast, and a longer one hands the GR to
    RELEASE.

  Decisions:
  - **`tmode` 0 is every Mode's manual timing.** A switch keeps the raw `tmode`. MANUAL first sat at 1, which is Clean's
    and Brickwall's AUTO, and their unseeded crest detectors made the hand-over click (`dsp.switch`). Future Modes put
    their manual position at 0.
  - **The tube stage runs 12 dB under Opto 2A's drive.** At Opto 2A's level its OS-rate gain moved a 0.5 s release by
    2.2 samples across Qualities.
  - **`ui.curve`'s span row skips a curve that only grazes the frame and draws nothing,** as its drawn row already
    allows.

  With this Mode, Wave 2 (SPRINTS §5) is complete: 14 Modes.
- **ADR-84 Lists scroll by the pixel, with the content (v1.2).** The user found the preset browser scrolling against a
  Mac trackpad, "very sensitive and rigid". Three causes:
  - **Direction.** Both browsers multiplied JUCE's delta by −1 when `isReversed` was set. That is the rule for a value
    control (JUCE's Slider, FunkGui's RuleSlider): a knob follows the fingers whatever the system setting. JUCE's delta
    already carries the system's direction, natural scrolling included, so a list must take it as it comes. Under
    natural scrolling, the macOS default, the list ran against the fingers.
  - **Rigidity.** The list moved in whole 20 px rows, so small travel did nothing and then jumped a row.
  - **Sensitivity.** A trackpad delta was scaled to rows (12 rows per unit), unrelated to the fingers' travel, so a flick
    and its momentum crossed the bank.

  Decisions:
  - The preset list keeps its offset in logical px. A precise (trackpad) delta moves it 1:1: JUCE's macOS delta is
    `scrollingDelta / 512` points, divided by the UI zoom (ADR-68). The system's momentum events carry on the same way,
    clamped at the ends.
  - A wheel notch stays 3 rows (HR's value) and glides there (τ 0.05 s). Keys and a selection brought into view jump at
    once, as before. The filter column moves a row per 18 px of trackpad travel.
  - A row cut by the list's edge (y 88–308) is drawn clipped and is hit, listed and focused by the part that shows. The
    clip is new in FunkGui v0.9.0: `Canvas::pushClip`/`popClip` crops recorded primitives on the CPU, interpolating
    their local coordinates, so every pixel inside draws as before. The shader, the dump format and `SoftRaster` are
    unchanged (`fg.canvas.clip`: identical pixels inside, ground outside). The browser pushes a clip only while a row is
    cut, so a list at rest on a whole row draws the frame it drew before, and no golden moved.
  - The Mode browser's pages follow the same direction rule, and a trackpad swipe with its momentum turns one page.
  - Value controls (slots, the Mode latch, the preset strip's name) keep following the fingers.
  - `ui.presets` `scroll.*` holds the direction under both settings, the 1:1 travel at 150 % zoom, the clamp, the clip
    and the glide; `ui.dump --wheel <points>` renders a scrolled list for review.
- **ADR-85 A settings screen: audio in detail, new-instance defaults, diagnostics (v1.2).** The user asked for a
  dedicated settings screen with audio settings for advanced users and diagnostics. It is a third overlay beside the
  two browsers (`Overlay::settings`, a tenth sub-view `ViewIndex::settings`, the view "settings"). It covers the whole
  region between the header and the footer and behaves as a browser does: first in the hit order, the whole Tab order
  while open, Esc and a click outside close it, the items it covers are hidden from accessibility, and it fades in and
  out. `views/Settings.h` has the layout.

  Decisions:
  - **The entry is a gear right of the wordmark,** the header's second Tab stop, after the Mode latch, which stays the
    panel's first. The footer has no room: the ZOOM spec line (480 px) fills the 554 px line that a SETTINGS cell would
    shorten.
  - **AUDIO · THIS INSTANCE** shows the three setup parameters that already exist, QUALITY, LOOKAHEAD and SIDECHAIN
    (`quality`, `labudget`, `extkey`). Each has a table of what it costs: oversampling factor, filter, latency in samples,
    the rate the engine runs at, and the lookahead's samples at the current rate. Below them are what the host routes
    to the key input and the total latency the host is told. The cells write exactly what the display row's do: one
    tap, and SetupWatcher applies the setup.
  - **NEW INSTANCES · THIS COMPUTER** is new: QUALITY and LOOKAHEAD for an instance the host creates. They are machine
    preferences (`kPrefNewQuality`, `kPrefNewLookahead` in `UiPreferences`' file). The processor reads them in its
    constructor through a `PropertiesFile` of its own, since a host may construct it on any thread and `UiPreferences`
    belongs to the message thread. A session or a state load then sets its own, and presets never carry these two.
    `proc.diagnostics` holds this, including the rule that a damaged or out-of-range value leaves the table's default.
  - **An offline-render quality was not added.** The engine's latency depends on QUALITY, and a latency change at the
    start of a bounce is exactly what hosts compensate worst.
  - **DIAGNOSTICS** has fifteen rows from the new `ProcessorFacade::diagnostics()`. It is an additive, defaulted virtual
    and a `Diagnostics` struct: versions, format and host, rate, block, channels, the configured setup, the reported
    latency, and a DSP load. The rows also use the frame state (audio running or stopped), the registry, `PresetAccess`
    and, in the live editor, `EditorHost::diagnostics()` through `Panel::setRenderInfo` (display rate, zoom, backing
    scale, dropped frames). FakeFacade returns fixed values, so no headless frame depends on the build or the version.
  - **The DSP load is measured on the audio thread** around `EngineHost::process`: `juce::Time::getHighResolutionTicks`
    (`mach_absolute_time`: no syscall, no lock) against the block's real time. It is smoothed over 0.5 s with a peak
    falling over 2 s, and overruns are the blocks that took longer than real time. The values are relaxed atomics, and
    prepareToPlay zeroes them. `proc.latency`'s allocation rows still read 0 on the audio thread.
  - **COPY REPORT** puts the rows on the clipboard as text for a bug report. Headless it copies nothing.
  - `FcmpProduct.h` gains `kFunkGuiVersion` and `kJuceVersion`. Probes: `ui.settings` (53 rows) and `proc.diagnostics`
    (22 rows). The gear moves every Mode's `ui.*` goldens, and "settings" is a new golden view for every Mode.

## HardwareReverb migration

- **ADR-71 HR's preset schema moves to v2 (lead, 2026-09-24).** FunkPresets (FunkGui v0.8.0) writes schema v2: a
  `preset.attributes` column, which FCompressor needs for `modeId`/`modeRev`, with `schema_min_reader` still 1, so
  older builds keep reading and writing the file. Moving HR's presets onto it (migration phase 2, HR PR #2) changes
  HR's **schema-version rows**. `schema.user_version`, `corrupt.notadb.user_version`, `migrate.v0_empty.user_version`
  and `migrate.v0_foreign.user_version` move from 1 to 2, and `migrate.newer_readable.user_version_kept` moves from 2 to
  3 with its fixture. This is a deliberate, versioned and backward-compatible format change, not a regression. It is
  the one approved exception to the gate's "never edit HR's expectations" rule; every other preset golden held unedited.
  A real v1 database, written by HR's own pre-migration store, migrates in place losing nothing (30 spec rows).
