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
  (v1.2: the plugin now keeps its own edit history without an UndoManager, ADR-91; this rejection still stands.)
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
- **ADR-86 DRIVE glides per sample in every driven voice (v1.2).** Console E found that the shared driven voice
  (`detail::VoiceDrive` and `detail::processDriven`, TubeSym.h) held its input scale k for a whole `colour()` call. The
  engine designs k per control tick but runs colour once per chunk, so a moving DRIVE stepped the residual's level at
  the chunk rate. Against Console E's clean, RMS-detected output that read 4.5 dB (ECO) and 7.0 dB (HQ) on
  `dsp.zipper`'s DRIVE edge (limit 3). Other Modes masked it under their own colour. The user left the call to the lead.
  - **k now glides linearly across each call from where the last one ended,** the rule TubeTransformer, TubePushPull
    and FetColour's ALL already follow. The last k lives in the new `detail::DrivenChannel`, which replaces
    `adaa::Channel` in the six driven voices' states (TubeSym, DiodeAsym, Bright, VcaBus, OctoDist, LoudClip, whose
    clip level moves with THRESHOLD). A static DRIVE takes exactly k and 1/k, so the steady output is bit-identical:
    `dsp.print`, `dsp.static` and every other golden held; only `zipper.ramp.drive.err_db` moved, 8 to 9 dB better on
    Bus G and Octo.
  - **Checked where it failed:** with a temporary live DRIVE on Console E (VcaBus, not committed), the DRIVE edge read
    −0.76 dB (ECO), 0.79 dB (STD) and −0.41 dB (HQ), all under the limit.
  - **Console E keeps DRIVE n/a.** The channel it models has no drive control, so the fix does not reopen the slot.
  - **Review revision (v1.2, with ADR-91):** LoudClip's safety clip still cut at the call's final 1/k while the shaper
    glided, so a falling THRESHOLD in VOICE LOUD met a hard corner under the soft one (0.1 of 1/k at a 1 dB fall in a
    call), pulsing at the chunk rate. It now clips each sample at its own 1/k; a static T is bit-identical.
    `dsp.slidingmax` gains `loudclip.glide.*`. A snap lands the drive but keeps the channel's last k, so the first call
    after it still glides (one chunk, 64 samples at most); `reset()` clears it (analysis entry points, a new engine).
    That is now the stated behaviour (TubeSym.h) rather than a snap hook in every glide voice.
- **ADR-87 CI on GitHub Actions; FunkGui from GitHub when there is no local checkout (v1.2).** The user made both
  repositories public and asked for CI, a build badge and a README image.
  - **`.github/workflows/ci.yml`** runs on every push to main, every pull request and by hand, on `macos-26` (Apple
    Silicon). Job `dsp` is the `dsp` preset through `Scripts/verify.sh`: the quick signal. Job `plugin` is the `lead`
    preset (GPU editor, Release, LTO: what ships) through `Scripts/verify.sh --integration`, the lead's own gate. A
    failing job uploads the CTest log, the probe results and the golden candidates.
  - **Dependencies:** `Scripts/deps.sh --no-pluginval` fills `~/audio/.deps` (JUCE and bgfx.cmake at their pinned
    SHAs, and shaderc), cached under a key that hashes `deps.sh`, where the pins live. FunkGui is cloned at the pinned
    tag.
  - **FunkGui's default repository** (`FCOMPRESSOR_FUNKGUI_REPO`) is the local checkout `~/audio/libraries/FunkGui`
    when it exists, so the lead's and the agents' builds stay offline as before, and `https://github.com/Snipet/FunkGui.git`
    otherwise. The tag, the SHA assertion and the override rules are unchanged.
  - **`FCOMPRESSOR_INSTALL_AFTER_BUILD` defaults to OFF.** Every preset already set it, `owner` to ON; a plain
    `cmake -B` build no longer copies plugins into `~/Library/Audio/Plug-Ins`.
  - **Timing rows scale on CI.** The first run matched every golden bit for bit on the runner (Xcode 26.6), but the two
    wall-clock rows failed there: `latency.live.*_ms` read 100 to 146 ms against 100, and
    `hostile.silence.tail_cost_ratio` 2.05 and 2.20 against 2. `FCMP_TIMING_SCALE` (Tolerances.h; 1 when unset, which
    is the lead's gate) multiplies exactly those limits, and CI sets 3. That still fails a setup change that never
    arrives, and a denormal tail, which costs ten times or more.
  - **Not in CI:** `gui-live` (a window and Metal), `validate.sh` (auval and pluginval), the sanitizer and universal
    builds, signing and releases. The lead's gate still runs them before a merge.
  - **The README image** is a real engine render, not a mock-up: `ui.dump` gains `--preset <name>` (a Mode's factory
    preset) and `--live <seconds>` (the Panel over EngineFacade, a real EngineHost, playing a deterministic groove at
    60 frames per second before the frame is written). `docs/images/fcompressor.png` is
    `fcmp_probe_plugin ui.dump --mode opto-2a --golden-root tests/golden --arch arm64 -- --view panel --out x.dump
    --png docs/images/fcompressor.png --dpi 2 --live 6 --preset "Smooth Vocal"`.
- **ADR-88 A global OUTPUT trim (v1.2).** The user asked for the lead's pick of missing professional features; an output
  trim after the mix was one (ARCHITECTURE listed it as a candidate v2 append).
  - **A new host parameter, `output`:** "Output", −24 … +24 dB, linear, default 0, automatable, not in presets, version
    hint 2. It is the first append to `kApvtsOrder` (index 29; 30 parameters). Old sessions and presets load with it at
    0 dB. Presets never carry it, so it stays where you set it while you browse presets and Modes, and changing it never
    marks a preset MODIFIED. `fcdsp::formatOutput` / `parseOutput` give the host and the UI the same text ("−3.0 DB").
  - **Where it acts:** `BlockParams::outputDb` reaches `EngineHost`, which applies the linear gain after the
    downsampler, through the host's two-stage 20 ms smoother (as the mix), before the poison check, SC listen and
    bypass. So it trims the processed signal, dry and wet alike at any mix, and DELTA; bypass stays the true dry
    reference and SC LISTEN the key as the detector hears it. The OUT meter reads after it. The TRANSFER curve stays
    the Mode's curve, which the trim does not change. At 0 dB the stage touches nothing, so every existing output is
    bit-identical (`dsp.print` and the other goldens held).
  - **On screen:** a compact slider in the display row, over the IN · OUT readout (x 224–352), so it shows on both
    screens: OUTPUT and its value on the captions' line, a rule with the 0 dB notch, a caret. It uses the slots' look
    and input rules (drag, wheel, fine and ultra-fine modifiers, arrow keys in 0.5 dB, Shift 0.1, Page 3, Delete or a
    double-click for 0 dB, the host menu on a right-click). It is the display row's first Tab stop.
  - **Tests:** `dsp.null` gains OUTPUT rows at every Quality: −6 dB is the 0 dB output times the gain bit for bit,
    bypass and listen stay untrimmed bit for bit, a 0 → −12 dB edge at a waveform peak reads under the click limit, and
    an out-of-range or NaN value clamps. `dsp.hostparams`, `proc.state` and `ui.charscreen` count and order the new
    parameter. Goldens move where they list parameters or the display row: `proc.layout`, `proc.text`, `ui.geometry`,
    `ui.a11y`, `ui.input`.
- **ADR-89 Typed values (v1.2).** The second of the lead's pick: a professional user types an exact value ("−18.5").
  Double-click already resets a control to its default (02 §8.4.6), and a click alone starts every drag, so neither
  can open a field.
  - **How it opens:** Return, a digit, '.', '-' or '+' on a focused value control (a slot or OUTPUT). Tab gives the
    focus as before; a press on a control now also gives it the keyboard focus with the ring hidden, so "click, then
    type a number" works (the way many hosts do it). Opened by Return the field holds the current value, selected;
    opened by a character it starts with it. Locked, derived and n/a slots open nothing. The first-run hint says so.
  - **The field** (`views/ValueEntry.h`, FunkGui's state-only `LineEdit`, tag VALUE_ENTRY) sits over the value text in
    the value's own style. While it is open the Panel gives its view every key (`PanelContext::textEntry`): Return or
    Tab sets the value (Tab then moves on), Esc cancels without leaving the screen or hiding the ring, and a click
    elsewhere (or the wheel) sets it first; a click inside it does nothing, a double-click there resets nothing.
  - **Parsing is the host's:** a slot's text goes through `fcdsp::parseHost` with the frame's raw values, so it takes
    what a host lane takes (the Mode's own numbers and step labels, "−18 dB", "500 us", "4:1", "inf"); OUTPUT's goes
    through `parseOutput`. The result is one tap (begin, set, end) on the port. A text that is no value is refused:
    nothing is written, the field stays with the text selected and its border flashes. A Mode change closes it.
  - **Tests:** `ui.entry` (34 rows): each way to open, commit and cancel, the refusal, units, a stepped slot, refused
    slots, a Mode change and OUTPUT. No golden moves: a closed field draws nothing.
  - **Review revision (before release):** a control a click focused takes no typed key while a browser or the settings
    screen covers it (a hidden field there could be committed by the next click), and a locked, derived or n/a slot
    takes none at all, so the host keeps it. Tab after "click, then type" moves on as from a Tab stop. A flick's
    momentum (inertial wheel events) neither sets nor closes an open field. In an open field Cmd-Z takes the typing
    back (the field closes, nothing is written), and the host's other Cmd and Ctrl chords pass, as from the preset
    browser's name edit. Typed keys follow the last press only (`PanelContext::typedTarget`, which every press clears):
    after a press on anything else they go back to the host instead of opening a field on a slot the user left.
    `ui.entry` gains `guard.*` (17 rows).
- **ADR-90 An ANIMATION speed in the settings screen (v1.2).** The user asked for a slider controlling how fast the fading
  animations run, the fastest setting being none.
  - **FunkGui v0.10.0** (`ease::setTimeScale`): every tau `ease::toward`, `hover` and `shown` take is multiplied by a
    process-wide scale, 1 by default (bit for bit the eases as before), 0 for none. That covers every widget, the
    screen crossfade and the browser and settings fades, the Mode texts' crossfade and the preset browser's notch
    glide. The product's own timed fades follow it through `AnimationModel::scaledStep`: the Mode colour (0.25 s),
    the live marks' fade when the audio stops (0.4 s), a typed field's refusal flash; the landing flash of a Mode
    change is scaled likewise. Meters, clocks, dwells and messages (SAVED, COPIED, the first-run hint) are not
    animations and keep their time.
  - **The setting:** INTERFACE · THIS COMPUTER, under NEW INSTANCES: ANIMATION, a stepped slider (a RuleSlider over
    `views/AnimationModel.h`, which is its own `ParamPort` so the slider writes it as it writes a parameter) with
    SLOW (×2), NORMAL (×1, the default), FAST (×0.5), FASTER (×0.25) and OFF. It is a machine preference
    (`animationSpeed` in UiPreferences, the detent index), set at once in the process (every open editor follows)
    and applied by each Panel when it is built. It is the settings screen's Tab stop before COPY REPORT.
  - **Tests:** `ui.settings` gains `anim.*` (the slider, its keys and a label click, the scale at each step, the
    overlay gone one frame after Esc at OFF and still fading at NORMAL, no parameter written); `fg.ease` (FunkGui)
    12 rows. Goldens move only for the settings view (`ui.geometry`, `ui.a11y`).
  - **Review revision (v1.2, with ADR-91):** the Panel applies the scale again whenever UiPreferences' revision moves,
    because the host re-reads the preferences file after the Panel is built (an editor opened after the speed was
    changed in another process ran at the old speed). TRANSFER's Mode-switch curve ease (160 ms) follows the scale too;
    its 0.9 s ghost is a dwell and keeps its time. `anim.*` gains four rows.
- **ADR-91 Undo/redo and an A/B compare (v1.2).** The last of the lead's pick. ADR-23 rejected JUCE's `UndoManager`
  because the APVTS pushes host automation into its transactions, so undo would revert automation. This history is
  the plugin's own and records only what the editor writes.
  - **`plugin/portable/EditHistory.h`** (`plugin/EditHistory.h` until ADR-93's web Sprint B), plain C++ over a small
    Host interface, so the processor and `FakeFacade` run the same code. The processor's ports (a `HistoryPort`
    wrapping `JuceParamPort`) report every gesture's begin and end, and its batches report theirs (a preset, a Mode
    change, any multi-write). At the outermost begin the history reads the tracked raw values and the preset's uuid;
    at the outermost end, whatever changed is ONE entry. A gesture-only
    bracket keeps only the parameters it had a gesture on, so host automation meanwhile is neither recorded nor undone.
    Tracked: the 22 Mode-filtered parameters, `mode`, `extkey`, `output`; never `quality` or `labudget` (latency),
    `bypass` or the monitoring latches. Undo and redo are exact raw writes in one batch, each announced to the host as a
    gesture, then the preset identity comes back (`PresetAccess::currentUuid` / `restoreCurrent`: the preset is current
    again with its values as the baseline, so MODIFIED is recomputed). At most 100 entries; a new edit drops the redo
    branch; a state load clears it (a load serial, since hosts load on their own threads).
  - **A/B:** two slots, each a sound and its preset. B starts as a copy of A; selecting the other slot stores the live
    sound into the active one and loads the other, and is an undo step. `copySlot` fills the other slot. Once B has been
    used, the inactive slot is saved in the session (`<COMPARE>` after `<UI>`, State.h's new hooks; older builds ignore
    it) and a load restores it.
  - **On screen:** UNDO and REDO arrows and A | B on the preset strip's second line under SAVE
    (`views/EditControls.h`), with footer lines naming the step ("UNDO THRESHOLD   CMD-Z"), buttons and a radio group for
    accessibility, Tab stops after SAVE, and a menu on A | B (a right-click) with "Copy A to B". Cmd-Z and Shift-Cmd-Z
    work anywhere in the panel (hosts that keep those keys for themselves still have the buttons); with nothing to
    take back, or under an open browser, the key is left to the host.
  - **Tests:** `proc.history` (30 rows) on a real processor: gestures, automation left alone, batches, the setup not
    recorded, presets and their identity, capacity, state loads, A/B with presets and across a session. `ui.edits`
    (17 rows): the controls, their keys, clicks and footer lines. Goldens move where the strip is drawn (`ui.geometry`,
    `ui.a11y`, `ui.input`).
  - **Review revision (before merge; a four-area review with two skeptics per finding):**
    - *Nothing steps.* Undo, redo and an A/B switch write in a batch that does not raise the engine snap
      (`Processor::finishBatch(false)`), so OUTPUT, MAKEUP, MIX and DRIVE ramp as after any edit. A snap is still
      raised when any batch in the open nest asked for one (a host-thread load overlapping an undo).
    - *The compare state is the load's at once.* A load hands its `<COMPARE>` (or its absence) straight to the slots
      under the mutex (`setLoadedCompare`), so a save that follows before any editor opened keeps B, and never writes
      back a stale one. The slots are read and written only under that mutex.
    - *A bracket open across a load records nothing* (the load serial is kept at its begin); the thread is checked
      before any member is read on a host thread.
    - *Save As names the current sound.* It is no entry, so undo and redo first give the steps either side of the
      current position the live preset's uuid (`adoptIdentity`): undo after an edit and a Save As goes back to the
      preset before, redo lands on the saved one, unmodified (before, redo left the old preset MODIFIED, and SAVE on a
      factory preset then saved a duplicate).
    - *Keys.* Cmd-Z does nothing to the sound while a browser or the settings screen is open (the key is theirs, or the
      host's), and with nothing to take back the host keeps it. An open wheel burst (a gesture until 0.5 s after its
      last notch) is closed into its entry before undo, redo or a switch, so "scroll, then Cmd-Z" works at once.
    - *Tests:* `proc.history` gains a save straight after a load, a load without `<COMPARE>`, a burst open across a
      load, undo and redo around a Save As (in the probe's sandboxed store), and `ramp.*` (a sine through the
      processor: the first millisecond after an undo of OUTPUT −18 dB, and after an A/B switch 18 dB louder, is within
      3 dB of before, and the level lands 18 dB up); `ui.edits` gains the pass-through, the browser and the wheel burst. The OUTPUT gaps the review found in older-session coverage are
      closed too: `state.absent` removes `output`, `proc.fixtures` loads v1 sessions over an OUTPUT away from 0 dB, and
      `proc.presets` checks a preset neither moves OUTPUT nor reads MODIFIED because of it.
- **ADR-92 Linux: the VST3 and the Standalone on x86-64, the editor on Vulkan through X11 (v1.2).** The user asked
  for Linux support with Wayland. ARCHITECTURE §1.2 had Linux as a v1 non-goal; it is now supported.
  - **What runs:** the VST3 and the Standalone (no AU), built with Clang from the same presets, tested on x86-64 (Arch
    Linux, Clang 22, KDE Plasma on Wayland; CI on Ubuntu 24.04). **Wayland is reached through XWayland.** JUCE 8.0.4's
    windows are X11 windows, and a Linux host embeds a VST3 editor by X11 window ID: neither JUCE nor the hosts have a
    Wayland embedding, so every JUCE plug-in on a Wayland desktop runs this way. A native Wayland editor would need a
    JUCE with a Wayland backend. arm64 Linux gets `-march=armv8-a` (every NEON operation Simd.h uses) but is not verified.
  - **The editor (FunkGui v0.11.0):** bgfx on Vulkan into an X11 child window of JUCE's peer, created on JUCE's display
    connection through JUCE's own dynamically loaded Xlib. It selects no events, so pointer, wheel and key events reach
    JUCE's peer window, as for JUCE's own OpenGL child window; it is placed and sized in device pixels at the drawable's
    scale (`setRenderViewScale`, so a `UI_SCALE` capture keeps the window, the swapchain and the drawable one size);
    creating and destroying it runs under an X error trap, because in a plug-in JUCE installs no X error handler and
    GDK's aborts on an untrapped error. Vulkan only: bgfx's OpenGL path (EGL) aborts the process on any initialisation
    failure, its Vulkan path fails softly into EditorHost's fallback screen. No display link on Linux: the FramePump
    runs on its timer (60 Hz while anything moves, 12 at rest). Shaders: Metal and SPIR-V on every host; the Metal pair
    compiled on Linux is the macOS golden byte for byte.
  - **Build** (`cmake/FcmpPlatform.cmake`): macOS or Linux, Clang only (the warning list and lints are Clang's; Linux
    defaults to `clang++` before `project()`). Upstream Clang rejects the layout constructor template in JUCE 8.0.4's
    `juce_AudioPluginInstance.h` in every TU that includes it; where the compiler does, `-fdelayed-template-parsing` and
    the silencing of its C++20 deprecation warning (JUCE configurations only; it goes when the JUCE pin moves). On Linux
    also: bgfx without its Wayland backend and with `-w`, `-Wno-pass-failed` (libstdc++ 15+ puts `#pragma GCC unroll`
    in `std::find_if`), `-Wno-nontrivial-memcall` on JUCE's module TUs (Clang 20+, in JUCE's HarfBuzz and VST3 SDK),
    `--as-needed` (bgfx links X11 and OpenGL, GLU included, that nothing calls) and a version script so the VST3 exports
    only `GetPluginFactory`, `ModuleEntry` and `ModuleExit`. No C++20 module scanning anywhere. `CMAKE_OSX_ARCHITECTURES`
    and `FCOMPRESSOR_UNIVERSAL` mean something on Apple only; the rtsan preset on Linux uses the sanitizer (no
    interposer). The Standalone plays through ALSA (PipeWire's ALSA plug-in on current desktops); JACK stays off because
    JUCE 8.0.4's JACK source warns under its own recommended flags.
  - **Storage on Linux:** preferences and presets in `~/.config/FCompressor/` (`XDG_CONFIG_HOME` is not read), not
    JUCE's defaults (`~/FCompressor/` and `~/.config/Application Support/`); the preset keys' Unicode fold through GLib.
  - **The first x86 run of the SSE backend** (Q6: Rosetta was never installed, so the x86 slice had only been compiled):
    every DSP golden matched the arm64 values bit for bit, save two x86 findings, neither Linux's. (1) Clang lowers
    `sel(gt(a, b), a, b)` to `maxps`, which under DAZ returns a denormal operand flushed where NEON's `bsl` returns its
    bits, so `xarch.simd.ops.hash` differed: the SSE `sel` now fences its mask with an empty asm (every other spelling of
    the blend is matched too), which emits no instruction. (2) `dsp.units` compared the whole MXCSR, whose sticky
    exception flags the probe's own denormal products set on real hardware; it now compares the control bits.
  - **Other portability fixes the first build found:** FunkGui's C-locale number text (`funkgui/core/CLocale.h`;
    xlocale's null `locale_t` is Apple's), the probes' command line (`fcmp::probe::argc()/argv()` from ProbeMain instead
    of `_NSGetArgc`), `AllocCounter`'s thread sentinel (`pthread_t` is an integer on Linux), `lint.headers` with the
    build's ISA flags and the JUCE workaround on Linux.
  - **Goldens:** Linux runs against the same goldens, with no overlay. The one platform-dependent row is the SDF font
    atlas hash: FunkGui rasterises the glyphs with `juce::Graphics` into a native Image, CoreGraphics on macOS and JUCE's
    software renderer on Linux, while every glyph metric agrees. So `ui.font` registers on macOS and `ui.font_linux` (the
    same body) on Linux, through a new `platform=apple|linux` key on the FCMP_PROBE line; FunkGui does the same
    (`fg.font.probe`, `fg.font.probe-linux`). `golden.py adopt` takes arm64 primaries only, so `ui.font_linux.txt` was
    copied from the Linux x86-64 candidate.
  - **Scripts:** `deps.sh` (cp -a, nproc, pluginval's plain executable), `validate.sh` (`~/.vst3`, no AU or auval),
    `gui-live.sh` (flock; the Standalone isolated through HOME with XAUTHORITY kept, and seeded with ALSA's default
    device at 48 kHz, the FakeFacade's rate, since JUCE opens an ALSA device at 44.1 kHz and the Characteristics views
    plot the processor's filters at its rate), `check-headers.sh`; `release.sh` refuses to run on Linux (no Linux
    packaging yet).
  - **CI:** `linux-dsp` and `linux-plugin` (the `lead` preset through `verify.sh --integration`) on ubuntu-24.04.
  - **Tests on Linux:** `verify.sh`: DSP-only 210/210 and headless 477/477, no drift; FunkGui's `agent-gui` 42/42.
    Live: `gui-live.sh` 5/5 views equal between the Standalone's Vulkan editor (XWayland) and the headless probe;
    FunkGui's `fg.gallery.live` 5/5 cases, BgfxSink's overflow path included. A screenshot and synthetic clicks
    (theme, Mode) in a private X server confirmed rendering and input through the child window.
  - **Not done:** LV2 or CLAP; JACK in the Standalone; a Linux release script; arm64 Linux verification; a vsync-driven
    frame clock.
  - **Merged with the rest of v1.2 (the lead, on macOS):** this decision was ADR-91 on its branch and is ADR-92, since
    undo/A-B took 91 on main. Undo's chord is the platform's command key: Cmd-Z on macOS, Ctrl-Z elsewhere
    (`EditControls::commandOnly`; JUCE's command modifier is Ctrl there, so the event carries both flags, and X11 reports
    the chord as the control character 0x1A, which FunkGui v0.11.0's EditorHost now delivers as its key code). The
    footer lines say CTRL-Z there. `gui-live.sh` keeps its ALSA seed, though `FCMP_UI_NO_LIVE` now ignores the device's
    rate (03 §3.6, `ui.nolive`). The key path is covered headless (`ui.edits` `keys.ctrl_cmd_z`); a real Ctrl-Z on a
    Linux desktop has not been pressed yet.
  - **Review before the merge (four areas, two skeptics per finding; eight confirmed, all Linux-side or tests):**
    - *Vulkan or nothing was not enforced* (FunkGui): bgfx's renderer fallback was left on, so a Vulkan that did not
      come up went on to OpenGL through EGL, the very path that aborts, and then to Noop, so init never failed and the
      fallback screen was never shown. FunkGui v0.11.0 initialises without the fallback and counts an init on another
      renderer than its shaders' as failed. Its Linux smoke row for Vulkan could not fail and now reads the compiled-in
      renderers.
    - *`XDG_CONFIG_HOME` is not read:* JUCE 8.0.4 resolves `~/.config` from `user-dirs.dirs`, never from the
      environment, and the branch said the variable was honoured. The texts now say what the code does. Following the
      variable needs FunkGui code and Linux-only tests, left for a session on a Linux machine.
    - *CI and scripts:* the CI jobs run `verify.sh --strict`, which fails on DRIFT or MISSING (on Linux CI is the only
      gate the lead has, and nobody reads a CI log's candidates; the default gate is unchanged). `validate.sh` keeps its
      bundle pairs in an array again (a path with a space was word-split). The README says where CMake 3.30 comes from on
      Ubuntu 24.04.
    - *The first runs of the Linux CI jobs* (Ubuntu 24.04: Clang 18, CMake 3.31, libstdc++) found three things the
      author's Arch machine (Clang 22, CMake 4) could not. `AllocCounter.cpp`'s sized `operator delete`s had no
      declaration (sized deallocation is off by default before Clang 19), failing `-Wmissing-prototypes`. FunkGui linked
      `SQLite3::SQLite3`, a target name FindSQLite3 only has from CMake 4.3 (v0.11.1 links the one that exists). And the
      runner's GNU `ar` indexes LTO bitcode through an LLVM 17 gold plugin that cannot read Clang 18's, so the archives
      had no symbols and the Standalone did not link: on Linux, Release archives are now made with the compiler's own
      `llvm-ar` and `llvm-ranlib` (`cmake/FcmpPlatform.cmake`), which need no plugin; install the `llvm` package.
    - *Not changed:* compiling bgfx with Vulkan alone on Linux (it would also drop the unused GL link), offered by the
      review as defence in depth; it needs a Linux build to check.
- **ADR-93 A browser demo: FCompressor as WebAssembly (on `main` after v1.1.0; published on GitHub Pages from `main`,
  first when PR #68 merged).** The user asked for web builds through WASM, to demo the plugin in a browser. The demo
  runs the real DSP and shows the real editor; it is not a port and not a second code base. Design pass: five scouts,
  three independent designs and a judge (`docs/sprints/web/plan.md` and the reports beside it). Built from 2026-10-01
  to 2026-10-02 in four sprints and a lead phase (manifests `docs/sprints/web-{a,b,c,d}.md` and `web-lead.md`): A, the
  engine in wasm with exact arithmetic (PR #64); B, FunkGui's core without JUCE, its host services and the WebGL2 sink
  (FunkGui v0.12.0, PR #65); C, the editor's views without JUCE, the web facade proven bit-equal to the processor, and
  FunkGui's WebHost (v0.13.0, PR #66); D, the editor as wasm under node, and the page (v0.14.0, PR #67); the lead
  phase, on Sprint D's merge 58f13e9: the browser gate, CI, publishing and the documents (PR #68). Where this entry and
  the plan differ, this entry is right.
  - **Shape.** A fourth configuration, `web` (`-DFCOMPRESSOR_WEB=ON`, Emscripten pinned to 6.0.3, the `web` preset;
    03 §2.12, ARCHITECTURE §3.1), with no JUCE (JUCE has no browser target), no bgfx and no threads. Two wasm modules
    joined by a MessagePort: `fcmp-engine.wasm` (fcdsp behind a C ABI, in an AudioWorklet) and `fcmp-ui.js` +
    `fcmp-ui.wasm` (the unchanged Panel over a web facade, FunkGui's core without JUCE, a WebGL2 sink) on the main
    thread. No SharedArrayBuffer, so no cross-origin isolation headers: any static host serves it. The platform is a
    desktop browser with WebAssembly, AudioWorklet and WebGL2, on an HTTPS or localhost address; only Chrome has been
    measured. The settings screen shows `CMakeLists.txt`'s version, 1.1.0, although the demo carries v1.2's features
    (v1.2.0 is not tagged).
  - **Arithmetic: the plugin's.** fcdsp gains a third backend, WASM SIMD128
    (`Source/fcdsp/core/{Simd.h,FlushTiny.h,ScopedFtz.h,FastMath.h}`; never `-mrelaxed-simd`, whose fused multiply-add
    is implementation-defined). Emscripten's SSE and NEON emulation was rejected: both give an unfused fma, against
    Simd.h's one-rounding contract. wasm has no fused multiply-add, so `fma`/`fms` are exact in software: a multiply-add
    in f64 (the product of two floats is exact there), and a round-to-odd correction only for lanes that land on a float
    rounding boundary (24 SIMD operations and a branch; 33 more on the slow path, taken on 0.1 to 3.5 % of engine calls
    on program material). `-DFCOMPRESSOR_WEB_FMA=unfused` builds the two-rounding form for measurement only. **Equal to
    the plugin bit for bit on what the goldens hold:** the 112 `dsp.print` rows (the module under node, and through
    the shipped worklet in Chrome) and every `ui.*` probe's goldens under node. **Not in the denormal range:** there the
    engine follows x86's flush rule (below), and native arm64's output differs from it by at most 9e-36 (−701 dBFS) in
    bus-g, octo and the three opto Modes, after a source ends or under a ±2^-120 floor (`docs/sprints/web/l-print.md`).
  - **Denormals.** wasm has no flush-to-zero and no denormals-are-zero. The backend makes a tiny result of add, sub,
    mul, div, fma or fms a signed zero, by a speculative test that costs four operations when no lane is near FLT_MIN.
    "Tiny" is x86's rule exactly (MXCSR.FTZ): the exact result, rounded to 24 bits as if the exponent had no lower
    bound, is below FLT_MIN, which is every exact result below FLT_MIN (1 − 2^-25). The float alone does not say:
    gradual underflow rounds everything from FLT_MIN (1 − 2^-24) up to FLT_MIN, so mul, div and fma decide on the exact
    result in double, on the slow path only. (The first version tested the float and kept FLT_MIN in that band, where
    both native backends give zero; the x86 CI run of `web.simd` showed it, and the check's own reference had the same
    mistake.) arm64 decides on the exact result itself, so the three backends differ only in
    [FLT_MIN (1 − 2^-25), FLT_MIN): zero on arm64, FLT_MIN on x86 and wasm, as Simd.h always allowed. Operands are read
    as they are. Chrome (154, arm64) computes denormals unflushed on the main thread, in the worklet's constructor and
    message handler and inside `process()`, offline and live, so the backend's flush is the only one.
  - **Measured** (48 kHz, 128-frame quanta, the `dsp.print` material; real-time factor under node, `web.engine.print`
    and `web.engine.speed`):

    | | rows equal to the native goldens | worst Mode at HQ (mu-67) | clean ECO / STD / HQ |
    |---|---|---|---|
    | exact fma (shipped), the lead's Mac (arm64), Sprint A | **112 of 112** (all 14 Modes × 8) | 20.4× | 168× / 102× / 55× |
    | the same at Sprint D's gate, 2026-10-02 | 112 of 112 | 20.5× | 163× / 104× / 52× |
    | exact fma, the CI runner (x86-64), Sprint A | 112 of 112 | 12.6× | 102× / 62× / 32× |
    | exact fma, the CI runner (x86-64), the lead phase | (lead: the x86 runner's figures from PR #68) | | |
    | unfused (measurement only), the lead's Mac | 0 of 112 | 40.8× | 295× / 187× / 92× |

    Exact arithmetic costs about 2× in engine throughput and stays far above the 4× gate on both machines, so the demo
    runs the plugin's exact DSP. The raw parameter sets hash the same natively and under wasm: musl's `pow` and `log`
    move nothing. On the x86 runner no 2/3 s of a Mode's silent tail costs more than 0.92× its active signal with the
    gate off (`web.engine.tail`): the flush leaves no denormal for V8 to trip over. Natively on x86-64, `web.simd`
    passes on the Linux CI runner, which is where the flush rule above was learned. In Chrome on the lead's Mac (the
    lead phase's `fcmp-tail` page): through the shipped worklet STD runs at 28.3× (mu-67) to 90.3× (brickwall) real
    time and HQ at 19.1× (mu-67) to 45.8× (brickwall); silence with the gate off costs 0.62× (mu-67) to 1.00×
    (console-e) of signal on the main thread, 1.12× at worst across runs (limit 2×).
  - **The engine module** (`Source/web/engine`, portable C++ over fcdsp alone: lint `web.engine`, so the same sources
    build natively for the checks). `fcmp_web_*`: create, configure, process (any frame count; the worklet gives 128),
    post and reply (the byte protocol of `WebProtocol.h`: Params with the 30 plain values and a snap flag, Attach,
    Reset, Pull; the reply carries the UiFrame, the new HistoryRing columns, flags and the latency), latency, the gate
    switch and a self-check. Raw values become BlockParams exactly as `Processor::buildBlockParams` makes them. The
    wrapper zeroes denormal input samples and runs a silence gate: after exactly-zero input for longer than the engine's
    tail (at least 100 ms) it resets the engine and outputs zeros until a sample or a Params record arrives. A record
    counts as activity, so the engine runs new values on the silence for that long again, as the plugin's engine does
    all the time: a Mode change made while idle has finished its crossfade before signal returns, and an edit that
    shortens the tail cannot close the gate before the engine has run it (`web.engine.selfcheck`'s `abi.gate.*` rows
    compare the audio with a fresh engine's, bit for bit). `web.engine.tail` measures the cost of silence with the gate
    off. The module is standalone: no JavaScript glue and no imports; it exports the functions the wrapper marks with
    the compiler's `export_name` attribute, `malloc` and `free` (545,704 bytes; the slow paths of fma and of the flush
    are out of line, which took 130 KB off and made STD about a quarter faster).
  - **The one deviation from the real-time rules** (ARCHITECTURE §7): a quality or lookahead-budget change reconfigures
    (and allocates) inside `fcmp_web_post`, between two render quanta, because the worklet has no other thread, and it
    may click. `fcmp_web_process` itself never allocates, locks or calls libm.
  - **The editor without JUCE.**
    - *FunkGui v0.12.0* (Sprints A and B, cards G-A and G-B; additive, no golden row moves). `FUNKGUI_WITH_JUCE`
      (default ON, nothing changes; OFF gives a JUCE-free core), the committed macOS font atlas with
      `FontAtlasSdf::load`/`serialise` and `fg.font.baked`, preferences behind a storage backend, presets `nojuce` and
      `web`: its JUCE-free tests pass as wasm32 under node against the same goldens. A popup menu, a file chooser and
      the clipboard are plain `HostServices` calls (`services`, `showMenu`, `dismissMenus`, `chooseFiles`, `copyText`,
      `commandKeyIsMeta`), served by `EditorHost` over JUCE and by `HeadlessHost` with scripted replies, so a probe can
      test a menu.
    - *The model code* (Sprint B, card E-1; no behaviour change, zero drift). `Source/plugin/portable/` (lint
      `plugin.portable`: no JUCE, no FunkGui but `ParamPort.h`) holds `EditHistory` and `FactoryData` (the factory
      bank's entry table and default filling as plain rows; `FactoryBank.cpp` converts them, and the bank and its
      revision are byte-identical). `PreviewWorker` runs on `std::thread` with the computation in `PreviewCompute` (no
      exception leaves the worker, as with `juce::Thread`; where threads cannot exist it computes inline). `FakeFacade`
      reads `FactoryData` and has no JUCE in it. `RenderInfo::renderer` replaces the settings screen's hard-coded
      "METAL", which was wrong on Linux.
    - *The views* (Sprint C, cards E-2 and E-3). The four views that showed menus, choosers and the clipboard through
      JUCE (`EditControls`, `PresetStrip`, `PresetBrowser`, `Settings`) ask `HostServices`; `Panel`'s host proxy
      forwards the calls. The same items, anchors, themes and callbacks, so nothing a user sees changes; `EditorHost`
      does with JUCE what the views did. Lint rule `editor.juce`: nothing under `Source/editor` outside `gpu/` includes
      or names JUCE (`#if JUCE_MAC` would turn silently false there, so the command key is the host's
      `commandKeyIsMeta()`). IMPORT and EXPORT are enabled only when the host reports a file chooser, and say so when it
      does not. The UI probes use `funkgui::HeadlessGuiScope`, and `HeadlessHost`'s scripted replies test a menu, a
      chooser and the clipboard (`ui.edits`, `ui.presets`, `ui.settings`: 61 new spec rows; two rows of `ui.settings`
      that asserted "a headless host copies nothing" are replaced by name). These sources compile unchanged into
      `fcmp_web_ui` and `fcmp_probe_web`.
    - *The preview without a thread* (Sprint D; the lead's decision). One step-response preview costs 22 to 72 ms
      natively and 89 to 428 ms under node (`ui.previewcost`'s notes, per Mode: bus-g fastest, mu-67 slowest), far
      over a frame. A browser's main thread has no thread to give it, so there an asynchronous `PreviewWorker` computes
      a request inside `tick()` only once no newer request has replaced it for 0.15 s of tick time: a control that
      moves stays smooth (a 30-frame drag runs 0 jobs; it ran 10), and the attack and release curves of the
      CHARACTERISTICS screen follow 0.24 to 0.58 s after the control rests, with one held frame. With a thread (every
      native build) nothing changes; the synchronous option still computes at once. `ui.previewcost` holds the rule
      natively too, through a switch that refuses the thread (`previewWorkerRefuseThread`, never called by the product).
  - **The web facade** (Sprint C, card W-F; `Source/web/facade`, lint `web.facade`: no Emscripten header, no
    `EngineHost`, the engine reached only through an `EngineLink` that moves `WebProtocol` bytes). `WebFacade` is a
    `ProcessorFacade`: 30 values held as JUCE holds them (`HostValue` restates JUCE's two-value parameter model on
    purpose, so the raw values are the processor's bit for bit, including a one-ulp drag step and Init after an undo);
    nothing is posted while a batch is open, the outermost end posts one Params record with the snap, a write outside a
    batch posts one record; an explicit `pull()` per frame fills a mirror `HistoryRing`; `WebPresets` is the factory
    bank and the session's user presets with the processor's rules. The editor module pulls before each `Panel::tick`;
    when the worklet's port connects it gives `setEngineSetup` the AudioContext's rate and calls `resync()` (until a
    reply arrives the facade knows no rate of its own). **Proven natively:** `proc.webnull` runs a `Processor` beside
    a `WebFacade` over the engine module (a loopback link, 128-frame quanta) through gestures, batches, presets, undo,
    A/B, quality changes and a reset, for all 14 Modes: output, the 30 raw values, every `UiFrame` and every history
    column are equal bit for bit. `proc.webpresets` and `ui.web` cover the presets and a Panel over the facade. The
    protocol carries no DSP load figures and no acknowledgement of a refused record (the worklet counts refusals).
  - **The browser host** (FunkGui).
    - *The WebGL2 sink* (v0.12.0, card G-C): `FunkGui::web`'s `WebGlSink` mirrors `BgfxSink` (one draw call, one
      program, the R8 atlas) with shader text generated from the same `shaders/*.sc` by CMake alone and pinned by
      `fg.shader.web`. In a real browser (Chromium, ANGLE Metal, the lead's Mac): against SoftRaster the frame differs
      by at most 1 per channel, and a forced context loss and restore gives the same frames byte for byte. **One
      difference from native, by rule:** a hard edge through device pixel centres in y is filled one row further down
      by WebGL. Only clips make such an edge, and with a browser's dpi = physical height / 640 whole logical px are not
      always device px, so a view snaps its clip edges with `Canvas::snapY` (FCompressor has one such clip, the preset
      list, snapped since Sprint C; where the dpi is a multiple of 0.25, which is every macOS window and every integer
      Linux scale, the snap changes nothing; at a fractional Linux desktop scale the list's edge can move by one device
      row, onto the row Vulkan and WebGL then agree on). The sink is not corrected instead: that would cost an
      off-screen pass per frame.
    - *WebHost* (v0.13.0, Sprint C, cards G-D and G-E): `EditorHost`'s counterpart on a canvas: one `WebGlSink`,
      `EditorHost`'s frame order on `requestAnimationFrame` (60 Hz, 12 Hz idle, nothing while hidden), the zoom fitted
      to the window, its own DOM listeners (pointer capture, JUCE's modifiers, click counts and wheel units,
      `EditorHost`'s key table), `preventDefault` only for what the Panel consumed. `WebServices` is the popup menu as
      DOM elements with the native menu's metrics and rules, and the clipboard; `WebPrefs` keeps `UiPreferences` in
      localStorage. It has no file chooser, no IME and no accessibility mirror. The gallery is a web page
      (`tools/GalleryWeb`); three pages run in headless Chrome under FunkGui's CTest (`fg.web.page`, `fg.web.host`,
      `fg.web.services`), and the menu was opened by a real click in a real browser. v0.14.0 adds
      `WebHostConfig::beforeTick`, from which the editor module pulls.
  - **The editor module** (Sprint D, card U-1; `Source/web/ui`, lints `web.ui` and `web.emscripten`: the one place an
    Emscripten header may appear). `fcmp-ui.js` + `fcmp-ui.wasm`: the unchanged Panel over a `WebFacade` on a
    `funkgui::WebHost` (the native editor's zoom steps, default and preference key; the fit asks that the canvas fits
    the window), preferences in localStorage, ADR-85's new-instance QUALITY and LOOKAHEAD applied on page load, the
    settings screen's DISPLAY row saying WEBGL2 and its FORMAT row WEB and the browser's name. `PortLink` is the
    `EngineLink` over the worklet's `MessagePort`: a Pull travels in one recycled 16,704-byte `ArrayBuffer`, every other
    record in a buffer of its own size, always transferred and never a view of the module's memory; a record from a
    replaced port or to a destroyed link is ignored. The pull follows the host's cadence (60 Hz, 12 Hz idle, none
    while hidden). `Module.fcmpSelftest()` draws one frame through the sink and reads it back in the same call against
    SoftRaster (the largest difference 1 of 255 at the ratios a display gives; below about 0.8 device px per logical
    px, a browser zoomed far out, a few tens of samples differ and the row says so). The seam between the module and the
    page (the names on `Module`, the wire rule, who pulls) is written down in `docs/sprints/web-d.md`; the lead phase
    adds the gate's two exports and three pins (below).
  - **The page** (Sprint D, card U-2; `web/`, plain ES modules, no framework). Nothing is loaded from another origin;
    the one absolute URL is the footer's link to the source repository and the built commit. START creates the
    AudioContext in the click, loads `fcmp-worklet.js` (the engine instantiated with an empty import object; `process()`
    copies in, calls `fcmp_web_process` and copies out with no allocation, no message and no throw, for any frame count
    and any input shape) and connects the module's port. The source is a loop synthesised in the page (drums, bass and
    a pad; peaks at -3 dBFS, about -14.7 dBFS RMS) or a file the user drops or opens, which never leaves the browser;
    no audio file is in the repository. The page says what it cannot do before START (no WebAssembly, an insecure
    context, no AudioWorklet, no WebGL2, `file:`), shows RESUME when the browser pauses audio, and lists what differs
    from the plugin (below).
  - **The site** (`cmake --build --preset web` makes `build-web/site`; `cmake/FcmpWebSite.cmake`): 13 files, 2,109,424
    bytes, 653,886 gzip (web.size's method, node's zlib at level 9 file by file; at Sprint D 2,096,442 and 648,135):
    the page, the two modules (`fcmp-engine.wasm` 545,704 bytes, 136,515 gzip; `fcmp-ui.wasm` 1,330,842 and 451,250,
    7,086 bytes more than at Sprint D for the gate's exports and pins; `fcmp-ui.js` 55,742 and 16,153), the licences
    (GPL-3.0, the typeface's OFL, and the toolchain's own texts for musl, libc++, libc++abi, compiler-rt and Emscripten
    in `THIRD-PARTY.txt`) and `built-from.txt`, which says `clean` only when FCompressor's tree is clean AND the FunkGui
    in the modules is the pinned commit itself, so the footer never links a commit that is not the source. The gate's
    stamp in the web tree covers the modules and the site; `web.size` holds the site to its 13 files, each within 20 %
    of its measured size.
  - **CI and publishing** (`.github/workflows/ci.yml`, 03 §4.10). The `web` job builds the `web` preset on
    ubuntu-24.04 with emsdk at the pinned version, runs `Scripts/verify.sh --strict build-web`, reports the x86 runner's
    numbers (`simdbench`, `speed`, `tail`) and runs the browser gate under SwiftShader; it keeps the site (`web-site`)
    and the gate's inputs (`web-live`) for 14 days and runs the gate again on the downloaded copy. `web-browsers`
    reports Chrome and Firefox (ubuntu-24.04) and Safari (macos-26) through `Scripts/web/page-check.mjs`, never as a
    gate. **Hosting** (the user's decision, 2026-10-02): on a push to `main` on which `dsp`, `plugin`, `linux-dsp`,
    `linux-plugin` and `web` passed, `publish` deploys the `web-site` artifact the gate tested, byte for byte, to GitHub
    Pages, https://snipet.github.io/FCompressor/, and `published` runs `Scripts/web-live.sh --url` against the public
    page once its `built-from.txt` names the commit. Nothing else is published.
  - **Tests under node** (`cmake/FcmpWeb.cmake`, 03 §2.12). `fcmp_web_check` is built from `Tools/web/*.cpp` in every
    configuration, and its subcommands and `web/tests/*.mjs` register themselves from `// FCMP_WEB_TEST` lines (labels
    `verify;web;global`, judged by exit code). `web.simd` holds the arithmetic contract on every backend (fma and fms
    bit-equal to a one-rounding reference on 12.6 million triples; the flush rule of each backend on both sides of its
    boundary and on the tie; the FastMath functions hashed against native arm64 constants, and lane by lane against
    their scalar forms); `web.engine.print` renders `dsp.print`'s material through the C ABI in 128-frame quanta and
    compares with the same golden rows (in the wasm build a Mode without golden rows fails: no `dsp.print` runs beside
    it); `web.engine.selfcheck` (the module's own hash, the ABI's contract, the silence gate against records), `.tail`,
    `.speed` and `.abi` (node instantiates the shipped module with an empty import object). The two that measure time
    run alone; `.tail` judges the worst pair of adjacent 1/3 s windows over three runs after a warm-up, at ×2 in the
    wasm build and with ADR-87's scale natively. Natively four of them run in every gate, so the wrapper cannot drift
    from the engine. The web tree builds `fcmp_probe_web` (the editor outside `gpu/`, the model code, the facade, the
    engine archive and every `layer=ui` probe) and runs it under node against the SAME goldens, with no drift and no
    overlay (`ui.font` too, since without JUCE the atlas is FunkGui's committed macOS bake; a probe's first line takes
    `platform=` as a comma list over `apple|linux|web`). Beside them: `web.ui.port` (PortLink and a facade as wasm over
    a real MessageChannel to the shipped engine: the wire rule, one carrier for 60 pulls judged by buffer identity, the
    patience rule, a replaced port, a destroyed and a displaced link), `web.worklet` (the shipped script over the
    shipped engine: bit-equal to the module driven directly, every input shape and a source that stops, the reply in
    the buffer the Pull came in, 0 bytes allocated over 35,000 `process()` calls), `web.loop`, `web.size` (exactly the
    expected files, each within its budget, no absolute or cross-origin URL written), `web.page`, `web.site`; and from
    the lead phase `web.worklet.print` (the shipped worklet script in a stand-in scope over the shipped engine: the 112
    print rows, and the expectation tool's output), `web.live.runner` (the gate's server, comparison, usage and exit
    codes, without a browser) and `web.pagecheck` (the WebDriver runner against a fake driver). `lint.docs` holds the
    documents' structural facts to the files that own them. **Counts, 2026-10-02:** the web tree holds 237 tests, 220
    `ui.*` (25 probes, 14 Modes), 15 `web.*`, `lint.deps` and `lint.docs` (the lead's run passed 236 of 236 before
    `lint.docs` existed); a native `agent` tree holds 529: 208 `dsp.*`, 94 `proc.*`, 220 `ui.*`, 3 `lint.*` and 4
    `web.*`.
  - **The browser gate** (the lead phase), `Scripts/web-live.sh`: gui-live's counterpart. It runs on a web build tree,
    on a downloaded artifact (`--dir`), on the published site (`--url`) or only serves (`--serve`); 03 §3.6 has its
    forms, options and exit codes. Headless Chrome, always muted, with a throwaway profile: ANGLE on Metal on macOS,
    SwiftShader elsewhere, and `--gpu swiftshader` forces the software renderer. Its 31 rows:
    - *Twelve capture pages* (six views × two themes at 2×, START never pressed, the pins below): `Module.fcmpFrame()`
      equals the node value (`fcmp_probe_web ui.dump --facade web --host WEB-LIVE --nolive 1 --fp`) in every line but
      `live` and `hooks`, and each frame read back through the sink is SoftRaster's by the pixel rule (below), twelve
      rows of each; one more row for the shipped asynchronous preview (`chars.sidechain` with no `dt` pin reaches the
      same frame).
    - *The page's self-test*, `?selftest=1`, with the context suspended (11 rows) and running (12 rows): the engine's
      self-check hash in a real AudioWorklet and on the main thread, 10 s rendered through the worklet with 0 of
      480,000 frames differing from the engine driven directly (about 100× real time), silence costing no more than
      signal, the atlas hash, the pixel row, frames drawn and replies arriving; an uncaught error is a FAIL that no PASS
      replaces.
    - *The live pages* (`web/live` with the test-only `fcmp-print.wasm` from `Tools/web/live`, served at `/live/` beside
      the site and never part of it): `fcmp-print` runs the 112 blessed `dsp.print` rows through the shipped worklet in
      an `OfflineAudioContext`, each row a fresh engine reconfigured by records (112 of 112 equal, and 28 more rows at a
      render quantum of 320); `fcmp-extra` the 140 rows that have no golden (ECO, HQ, HQ with a lookahead budget,
      44.1 kHz) against the shipped engine under node (140 of 140); `fcmp-tail` the denormal range (the tail and floor
      values of all 14 Modes equal node's; a browser that differs gets a NOTE, not a failure), the floating-point
      environment, the cost of silence and the load through the worklet (their numbers are under **Measured**). `Tools/web/live/expect.mjs` writes
      what they compare with.
    - *The scripted user*, `Scripts/web/scenario.mjs`, in place of the plan's hand checks in Chrome: 79 rows in one
      headless Chrome with real input (mouse, keys, wheel, a dropped file), controls found by name through
      `Module.fcmpA11y()`, each row judged by what reached the engine or by the Panel's own state: START, every screen,
      a drag, a double click, the wheel, typed values, undo and redo by the platform's chord, presets and their menus,
      A|B, a stepped and a continuous Mode, QUALITY and LOOKAHEAD, a dropped file and a bad one, a lost WebGL context,
      a hidden tab, the zoom and its preference, and no uncaught error or error-level console line. With `--png` (81
      rows) it saves each view in one stepped and one continuous Mode. `Scripts/web/scenario/mutants.mjs` reruns it on
      25 mutated sites, each turning its named rows red.

    Measured by the lead on 2026-10-02 (arm64 macOS): `Scripts/web-live.sh build-web` passed 31 of 31 in about 100 s on
    ANGLE Metal (pixel rows largest 1 to 2 of 255, none over 2); the same gate on a copied site with `--gpu
    swiftshader` 31 of 31 in about 106 s (pixel rows largest 5 to 6 of 255, within FunkGui's software bound).
  - **The module's hooks for the gate** (lead phase, card L-M). `Module.fcmpFrame()` settles the host (frames until the
    Panel no longer asks for the full rate, at most 600; while the engine publishes, one frame, so a frame of live audio
    is never reported as settled) and returns the frame's fingerprint as text, written once in
    `Source/web/ui/FrameText.h`, which `ui.dump --fp` shares. `Module.fcmpA11y()` returns the Panel's accessibility list
    and state as JSON. Three pins in the page's address: `nohint=1` (no first-use hint), `nolive=1` (the Panel draws as
    if no telemetry came) and `host=<text>` (1 to 31 characters of `[A-Za-z0-9 ._-]` in place of the browser's name, so
    the settings screen reads the same in every browser); a pinned `dt` makes the preview synchronous. Without a pin
    the page is exactly the page. `ui.webframe` holds the frame `ui.dump --facade web` makes to the blessed
    `ui.geometry` rows of the five gui-live views, natively and under node.
  - **The pixel rule, by renderer class on a still frame** (lead phase, card L-J). The WebGL renderer's name decides: a
    GPU may have no sample over 2 of 255; a software renderer (SwiftShader, llvmpipe, softpipe, a software rasteriser)
    gets FunkGui's bound, none over 16 and at most 10 per mille over 2; an unknown name is judged as a GPU. The
    self-test's frame is a still one, taken before START once the Panel is at rest (`fcmpA11y()`'s `fullRate` 0, about
    5 s after load), because a live frame differs from SoftRaster by a pixel on a moving trace. The gate's pixel rows
    use the same rule.
  - **WebDriver** (lead phase, card L-W). `Scripts/web/page-check.mjs` runs the self-test, the capture pages
    (`--frames`) and the live pages (`--pages`) in Chrome, Firefox or Safari over WebDriver classic, with no
    dependency: CI's `web-browsers` job. `web.pagecheck` holds its protocol to a fake driver; no real driver had been
    met when it merged.
  - **What differs from the plugin** (one list; the page shows the first ten): no preset import or export, and user
    presets last until the page is closed; no side-chain key input; the DSP load in the settings shows a dash; with no
    input the engine idles and the meters stop; a QUALITY or LOOKAHEAD change rebuilds the engine on the audio thread
    (the plugin uses another thread) and may click; where the system reverses the scroll direction the wheel turns a
    value the other way (Chrome gives no scroll-direction flag), and a notched wheel moves a stepped control about two
    steps (it arrives in pixels); typed values take plain keys only, with no input method and no dead keys; no host
    parameter menu on a right-click; desktop browsers with WebGL2 only, and nothing for a screen reader in the editor;
    on the CHARACTERISTICS screen the attack and release curves follow a control once it rests, not while it moves.
    Not on the page: when the curves catch up, one frame is held; a hard clip edge through device pixel centres in y is
    filled one row lower than natively (the fill rule, above); the zoom's fit is measured once, so a page loaded in a
    narrow window keeps a slightly large margin when widened.
  - **Reviews** (every confirmed finding fixed, each with a row shown to fail on the old code):
    - Sprint A: the silence gate's activity rule above (a Mode change while idle, an edit that shortens the tail) came
      from its review; the flush rule's band from the x86 CI run of `web.simd`.
    - Sprint B (six reviewers by area, two skeptics per finding): eight low-severity defects (a separators-only menu
      that was taken but never opened; live menu rows that could not fail; a second sink on one canvas breaking the
      first's recovery; the page runner hanging when the browser died; a shader check with a prefix-match hole; an
      exception on the preview thread terminating the host; the no-thread fallback aborting under wasm; the fill rule
      undocumented).
    - Sprint C: 19 findings, 4 medium and 15 low.
    - Sprint D (ten reviewers by area, two skeptics per finding): 20 low-severity findings; the preview worker,
      FunkGui's hook and the joints between the cards came back clean. Most were rows that could not fail (a stale
      preview result in four Modes, the carrier judged by the link's own counter, a worklet fed its stale input); the
      rest were edges: an editor failure that is not an `abort()` never reached the page, a fault during START was
      dropped, the browser's name came from the brand list's placeholder entry, an out-of-range new-instance preference
      was applied, the self-test compared at a non-proportional buffer, the zoom's fit reserved the header twice.
    - The lead phase: 27 findings, 4 medium and 23 low, and 8 refuted. Among the fixes: `fcmpFrame` on a page whose
      audio runs, which ran up to 600 frames and wiped the HISTORY plot; the self-test's frame taken before the Panel
      was at rest; a print-page row (`modes.count`) that could not fail; a browser left running when its WebDriver died
      first; signals and the results directory in the gate's script; a demo that stops between two groups of the
      scripted user.
  - **Follow-ups, each on its trigger** (`docs/sprints/web/plan.md`, "Follow-ups"): the preview in a Web Worker, so the
    CHARACTERISTICS curves can track a drag; persistence of user presets and the last state in localStorage (when the
    user asks); preset import and export as an upload and a download through `chooseFiles`; the DSP probes under node
    (wasm branches in `units`, `hostile`, `analysis`, `selftest` and `telemetry`); the DSP load figures and an
    acknowledgement of a refused record in the protocol; promoting Firefox or Safari from reported to a gate once each
    has passed several CI runs.
  - **What waits for the user:** listening to it (nobody has: the lead checked it with the output silenced, and sound
    was judged by numbers only; at Sprint D the lead pressed START, dragged THRESHOLD and saw the gain reduction and
    the curve follow, and opened CHARACTERISTICS, in Chrome by hand); Safari and Firefox by hand, and a machine with an
    Intel or AMD GPU (the scripted user has run in headless Chrome only; CI's WebDriver runs of Firefox and Safari are
    reports); recorded loops with a CREDITS file, if the user supplies material of their own; tagging v1.2.0, which the
    version in the settings follows.

## HardwareReverb migration

- **ADR-71 HR's preset schema moves to v2 (lead, 2026-09-24).** FunkPresets (FunkGui v0.8.0) writes schema v2: a
  `preset.attributes` column, which FCompressor needs for `modeId`/`modeRev`, with `schema_min_reader` still 1, so
  older builds keep reading and writing the file. Moving HR's presets onto it (migration phase 2, HR PR #2) changes
  HR's **schema-version rows**. `schema.user_version`, `corrupt.notadb.user_version`, `migrate.v0_empty.user_version`
  and `migrate.v0_foreign.user_version` move from 1 to 2, and `migrate.newer_readable.user_version_kept` moves from 2 to
  3 with its fixture. This is a deliberate, versioned and backward-compatible format change, not a regression. It is
  the one approved exception to the gate's "never edit HR's expectations" rule; every other preset golden held unedited.
  A real v1 database, written by HR's own pre-migration store, migrates in place losing nothing (30 spec rows).
