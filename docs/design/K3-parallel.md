# K3 — Critique: can the design be built in parallel sprints?

Status: review of Drafts 01, 02 and 03, written 2026-09-22. It does not edit them.

Lens: can the design be built in sprints of at most 3 concurrent agents, with **disjoint file ownership** and as little
cross-agent waiting as possible?

Sources are cited as file + section:
- `01` = `01-core-contracts.md`, `02` = `02-funkgui-and-ui.md`, `03` = `03-build-verify-process.md`;
- `A`…`F` = `docs/research/*.md`.

Severity:
- **blocker**: parallel work cannot start, or two agents must edit the same file, until this is fixed;
- **major**: forces serial work or cross-agent waiting, or makes a sprint plan unexecutable;
- **minor**: friction, wasted agent time, or a latent conflict.

Sizes are estimates:
- **S** ≈ half an agent session, ≤ ~600 new lines including tests;
- **M** ≈ one session, ~600–1,500 lines;
- **L** is more than one session and **must be split**.

Assumption: the lead is not one of the 3 agents. See open question Q1 at the end.

---

## 0. Verdict

- The contracts in 01 are close to freezable, and 03's process (worktrees, candidate-only blessing, ownership check)
  is sound.
- The design is **not buildable in parallel as written**, for five reasons:
  1. The three drafts describe three different source trees.
  2. Every new source or probe file requires an edit to a lead-owned CMake list.
  3. Mode registration funnels through shared files: `Registry.cpp`, the multi-policy stage headers,
     `FactoryPresets.cpp`, and every other Mode's switch goldens.
  4. 02 §10 and 03 §4.9 are two incompatible sprint plans. Neither respects one global 3-agent cap, and 02's plan
     consumes live worktrees, which 03 forbids.
  5. Several chains serialise without need: spec probes wait for `EngineHost`; the UI waits for a real processor, for
     DSP-complete Modes and for FunkPresets (blocked on HR's owner).
- All five can be fixed with mechanical changes (§1). The resulting plan is **13 sprints (S0–S12) × 3 tasks**
  (§3). It is throughput-bound (≈ 38 M-sized tasks). The longest dependency chain is 7 sprints, so there is slack for
  re-ordering.

---

## 1. Issues

### 1. [blocker] Three incompatible source trees

**Where:**
- 01 §2.1: `Source/fcdsp/{core,params,modes,engine,telemetry,analysis}`; `Tools/{FcmpDspProbe,FcmpProcProbe,FcmpUiProbe}`;
  `Tools/golden/`; `editor/Editor.{h,cpp}`; `editor/ProcessorFacade.h`.
- 03 §1.1, the include table, §2.7, §2.9 and the §4.2 manifest example: `Source/dsp/{simd,units,params,stages,sc,os,…}`
  plus a separate `Source/modes/`; `Tools/probes/{common,dsp,plugin}`; `tests/golden/`; `fcmp_probe_dsp` and
  `fcmp_probe_plugin`; `editor/gpu/FcmpEditor`.
- 02 Part 2: `editor/gpu/Editor.{h,cpp}`.

**Problem.** Manifest `OWNS`/`FROZEN` globs, `ownership.py`, and the `#include` paths already written in 01's headers
(`"fcdsp/params/Pid.h"`) all need one tree. With the current drafts, a Sprint-1 manifest cannot be written without
contradicting one of them.

**Fix.** The synthesiser writes one canonical tree in 01 §2.1, and 03 §1.1 refers to it.

| Path | Rule |
|---|---|
| `Source/fcdsp/…` | 01's layout (the directory matches the namespace, and the includes are already written that way) |
| `Source/fcdsp/modes/<dir>/` | one directory per Mode: `<Traits>.h`, `<Traits>Desc.cpp` (descriptor data, `physical()`, specs), `<Traits>.cpp` (the `FCDSP_DEFINE_MODE` line, #6). A manifest then owns `modes/fet76/**` |
| `Source/fcdsp/engine/stages/<slot>/<Policy>.h` | one policy per header (#7) |
| `Source/fcdsp/engine/host/*.h` | host components (#12) |
| `Tools/probes/{common,dsp,plugin}`, `tests/golden`, `tests/fixtures` | 03's layout |
| probe executables | `fcmp_probe_dsp`, `fcmp_probe_plugin` (03 §2.7 merge accepted) |
| GPU editor | `Source/editor/gpu/Editor.{h,cpp}` |
| facade | `Source/plugin/ProcessorFacade.h` (#13) |

### 2. [blocker] Explicit source lists plus lead-owned `cmake/**` turn every new file into a lead edit

**Where:**
- 03 §1.1: "Every other source list is explicit";
- 03 §4.6: `cmake/**` belongs to the lead;
- 01 §8.3: `target_sources` per `Modes.def` line, plus the generated `ModeIncludes.h`;
- 02 §1.1/§1.2: the FunkGui `src/**` lists are not specified.

**Problem.** An agent that adds `views/StepPlot.cpp`, `stages/ballistics/OptoCell.h` with a `.cpp`, or a probe file
cannot compile it without editing a file it does not own. With 3 agents this means either ownership violations or
lead edits in the middle of a sprint.

**Fix.** Directory → target mapping by `file(GLOB_RECURSE … CONFIGURE_DEPENDS)`. B0 (§2) writes it once in S0, and it
is never edited again.

| Glob, always rooted at `${PROJECT_SOURCE_DIR}/…` | Target |
|---|---|
| `Source/fcdsp/**/*.cpp` | `fcdsp` |
| `Source/plugin/*.cpp`, minus `CreateEditor*.cpp` (chosen per configuration) | the plugin's shared code and `fcmp_probe_plugin` |
| `Source/editor/**/*.cpp`, minus `gpu/` | the plugin and `fcmp_probe_plugin` |
| `Source/editor/gpu/*.{cpp,mm}` | the GPU plugin only |
| `Tools/probes/common/*.cpp`, plus `dsp/*.cpp` or `plugin/*.cpp` | the matching probe executable |

- FunkGui uses the same rule per `src/<module>/**`.
- Ninja re-checks `CONFIGURE_DEPENDS` globs on every build, so adding a file needs no reconfigure.
- Drop 01 §8.3's `target_sources` loop and `ModeIncludes.h` (#6 makes them unnecessary). `Modes.def` then drives only
  the registry and the CTest matrix.

### 3. [blocker] Probe and test registration is a shared list

**Where:**
- 03 §2.9: six CMake lists, `FCMP_DSP_GLOBAL` … `FCMP_UI_MODE`;
- 03 §1.1: `ProbeMain.h` does the subcommand dispatch;
- 03 §4.9, last line: each block lands with its probe;
- 01 §8.3: two different hard-coded lists (`state` sits in dsp; `analysis` and `print` are missing; `--golden-dir`
  instead of `--golden-root`).

**Problem.** Every block task that adds a probe edits two shared files: `FcmpProbes.cmake` (lead-owned) and the
dispatch table. The two drafts also disagree on probe names and on the CLI.

**Fix.** One file per probe, and the file registers itself.
- Path: `Tools/probes/<layer>/<probe>.cpp`.
- Its first line is `// FCMP_PROBE layer=dsp scope=mode timeout=60`.
- `FcmpProbes.cmake` globs these files, reads that line with `file(STRINGS … REGEX "^// FCMP_PROBE")`, and generates
  `<layer>.<probe>[.<key>]` over `FCMP_MODE_KEYS`.
- In C++, `FCMP_PROBE(static)(funkgui::test::Probe&, const ProbeCtx&) { … }` links a static intrusive list node into
  `ProbeMain`. That is legal in a probe executable; the no-static-init rule (C D12) applies only to `fcdsp`.
- 03's CLI and test names win.

Adding a probe is then one owned file.

### 4. [blocker] Two incompatible sprint plans, neither under one global cap

**Where:**
- 02 §10: UI-A and UI-B, each with 2 FunkGui agents and 1 FCompressor agent;
- 03 §4.9: S0-H, then S0-A/B/C, then "Sprint 1 = E §9.2 rows 4–13";
- 02 §2.1, 02 §10 agent 1 and 03 §4.9 step 1 each give the FunkGui seed to a different owner.

**Problem.**
- The recorder/sink split and `KIND_AREA` are owned twice (02 UI-A agent 1 and 03 S0-A).
- No plan interleaves DSP, FunkGui and FCompressor-UI work under a single 3-agent cap.
- 02 UI-A agent 3 builds against "agent 1/2 worktrees until the lead tags v0.1.0", which 03 §4.5 forbids ("never
  another agent's live worktree"). That forbidden dependency is also a guaranteed wait.

**Fix.** Replace both plans with §3. The **pipelining rule**:
- FCompressor tasks consume only *tagged* FunkGui.
- A FunkGui feature needed by an FCompressor task in sprint N is scheduled in sprint N−1 or earlier.
- The only exception is a single agent that owns both worktrees, which 03 §4.5(a) allows.

### 5. [major] The harness dependency makes a bootstrap cycle, and the FunkGui version numbers conflict

**Where:**
- 03 §0.1 and §2.3: "Until then only overrides can configure";
- 03 §4.9 steps 1–3: S0-H runs alone, and two agent slots sit idle;
- 02 §0.14 and §2.1: v0.0.1 is the snapshot, and **v0.1.0 comes after generalisation**, whereas 03 pins v0.1.0 right
  after S0-H.

**Problem.** Even a DSP-only FCompressor configure needs `FunkGui::harness`, so nothing on the FCompressor side can
build before FunkGui has CMake and Harness v2. The two tag plans also disagree on what v0.1.0 means.

**Fix.**
- **Lead pre-work (G0):**
  - verbatim copy plus `SEED.tsv`, as its own commit;
  - the 02 §2.3 sed renames, as a second commit, **keeping HR file stems** (see #16);
  - a 10-line `CMakeLists.txt` that exposes `FunkGui::harness` (INTERFACE, HR's harness renamed) and
    `FUNKGUI_VERSION`;
  - tag `v0.0.1`;
  - a detached worktree `FunkGui.wt/pin-v0.0.1`.
- **S0 then runs three agents at once:**
  - G1: FunkGui build and Harness v2, tagged v0.1.0;
  - F0: the fcdsp contracts;
  - B0: the FCompressor build skeleton, built against the pin override. Its probe main uses only `ScopedFtz` until
    v0.1.0.
- The lead moves the pin to v0.1.0 when it integrates S0.
- **Numbering:** 03's wins (v0.1.0 = build + harness). 02's "generalised" milestones become later minors.
- Harness v2 stays in FunkGui (03 §6 Q2), frozen at v0.1.0. Probe helpers specific to FCompressor go in
  `Tools/probes/common`, never in the harness, so the harness does not become a cross-repo edit point.

### 6. [major] `Registry.cpp` instantiates every ModeEngine: a shared compile hotspot

**Where:**
- 01 §8.2: `Registry.cpp` expands `Modes.def` into `kEntries[]` with `static_assert(sizeof(ModeEngine<T>))` per Mode;
- 01 §8.3: `ModeIncludes.h`;
- 01 §5.3: the analysis statics.

**Problem.**
- One TU includes every Mode header and every policy, and instantiates every `ModeEngine<T>` together with its
  analysis entry points.
- Any policy edit recompiles all Modes in that TU. At 35 Modes and `-O3` it becomes a multi-minute serial step.
- 03 §2.11's "3–6 s incremental for a Mode edit" does not hold under this design.

**Fix.**
- Each `modes/<dir>/<Traits>.cpp` ends with `FCDSP_DEFINE_MODE(Fet76)`. The macro lives in
  `fcdsp/modes/DefineMode.h` and is frozen in S0. It defines `extern const ModeEntry kEntry_Fet76` and instantiates
  `construct`, `staticGr`, `scShapeDb` and `colourCurve` **in that TU**, with the arena and alignment
  `static_assert`s there too.
- `Registry.cpp` does `#define FCMP_MODE(s,k,T) extern const ModeEntry kEntry_##T;`, includes `Modes.def`, and then
  builds `constexpr std::array<const ModeEntry*,128> kBySlot` from a second expansion.
- These are address constants, so they are constant-initialised and C D12 still holds.
- `ModeEntry::slot` moves into the registry table. A Mode file never knows its slot. The lint checks that
  `desc.key` equals the key in `Modes.def`.

### 7. [major] Stage policies are grouped per slot into shared headers

**Where:**
- 01 §2.1: `engine/stages/{Detector,…,Colour,Limiter}.h`;
- 01 §8.4 step 3: new policies go in `engine/stages/*.h`;
- 03 §4.2 example manifest: `OWNS …/GainComputer.h …/Feedback.h`.

**Problem.** The Mode tasks each add policies to the same few headers:

| Mode | Adds |
|---|---|
| Bus G | `DualRelease` |
| Opto 2A | `OptoCell`, `TubeTransformer` |
| Mu 67 | `TcSelector`, `ProgressiveKnee`, `TubePushPull` |
| Diode 609 | `SharedElementMax`, `DiodeBridge` |
| Brickwall | `SlidingMaxBox`, `LoudClip` |

Every Mode sprint therefore has 2–3 agents writing `Ballistics.h` and `Colour.h`.

**Fix.**
- **One header per policy:** `stages/<slot>/<Policy>.h`, for example `stages/ballistics/OptoCell.h` or
  `stages/colour/FetColour.h`.
- Combinators go in `stages/combinators/{DetSelect,ColourSelect,AutoSwitch,Hold,CrestAuto}.h`.
- **No umbrella headers.** A traits header includes exactly what it uses.
- A policy used by only one Mode may live in `modes/<dir>/` until a second Mode needs it. The lead then promotes it
  between sprints.

### 8. [major] The `Modes.def` edit protocol contradicts itself

**Where:**
- 01 §8.1: "Slots are assigned by the lead at merge time; agents use the planned slot";
- 01 §8.4 step 4;
- 03 §4.2 item 2 and §3.8 step 1: commented reservations at sprint start, uncommented by agents.

**Problem.** A Mode agent's CTest set, `proc.modeparam`, and the browser order all depend on the slot, so assigning
slots at merge time is too late. The two drafts also give different edit styles.

**Fix.** `Modes.def` is edited only:
- (a) by the lead, in the sprint base; or
- (b) by that sprint's single descriptor-wave task (#9).

Mode DSP tasks never touch it. A slot is fixed when its line first appears. 03's reservation scheme remains only as
the fallback for a lone task that must add a brand-new Mode.

### 9. [major] The descriptor schema is proven on 4 of 8 Modes before it freezes, and the UI waits for DSP-complete Modes

**Where:**
- 01 §0: `ParamSpec`/`ModeDescriptor` are Sprint-frozen;
- 01 §10.7: "the other four, sketched … finished by their owners";
- 02 §6.4: the UI state table is 01 §10.2;
- 03 §3.6: `ui.*` runs per Mode.

**Problem.**
- The hardest schema cases arrive last:
  - Mu 67: DC THRESH → knee, attack derived from `rel`, the 21-step INPUT;
  - Diode 609: the s2 group and the conditional SC HP;
  - Bus 25: the variant on `tmode`;
  - Brickwall: `stmode` {0, 2, 3}, attack derived from `look`.
- If any of them breaks the schema in S8–S11, a Sprint-frozen interface changes under UI and probe code already built
  on it.
- The UI's `textfit` and `a11y` gates need all 8 descriptors, not only Clean.

**Fix.**
- Add an early **descriptor wave** task, DW (§2). It:
  - writes all 7 remaining descriptors in full (data, `physical()`, time specs, internals);
  - gives them **generic traits** built from Clean's policies (FB Modes use `QuadKnee::solveFb` from F9);
  - activates their `Modes.def` lines;
  - passes `dsp.registry`, `dsp.quant` and the text round trip.
- Add `bool provisional` to `ModeDescriptor`. It is Sprint-frozen, not v1-forever.
  - `golden.py adopt` refuses rows under `modes/<key>/` for provisional Modes.
  - `dsp.registry` has a spec row that fails when `FCMP_RELEASE=ON` and any Mode is provisional.
- Mode tasks later replace the traits in their own directory and clear the flag.
- **Freeze point FZ3:** the schema is frozen at the end of S4, with evidence from all 8 Modes.

### 10. [major] Spec probes run through `EngineHost`, which is built in parallel with the policies

**Where:**
- 01 §2.1: `FcmpDspProbe` runs "D1–D5, D7–D12 at engine level";
- 01 §5.4: "every probe drives exactly what the plugin runs";
- 03 §3.4, and the §4.2 manifest where s1-gc owns `Static.cpp`.

**Problem.** If D1 (static), D2 (time), D3 (quant) and D9 (link) need `EngineHost`, the gain-computer and ballistics
tasks cannot pass their DoD until the host task's work is merged. Policies and host then serialise.

**Fix.** Add `Tools/probes/common/EngineRig.{h,cpp}`, owned by F3. It:
- constructs a `ModeEntry` into an arena;
- calls `prepare`, `setParams` and `snapParams`;
- runs `control()` and `colour()` at base rate in 64-sample chunks;
- taps `ControlIo`.

This is the same ModeEngine object code the plugin runs.

| Probes | Driven by |
|---|---|
| D1, D2, D3, D9, `dsp.analysis` | `EngineRig` |
| D4, D5, D7, D8, D10, D12, `print` | `EngineHost` |

### 11. [major] `IEngine` cannot deliver the telemetry `EngineHost` must publish; the test tap is specified twice

**Where:**
- 01 §5.3: `IEngine`, and `ControlIo::phase` only;
- 01 §6.2: `UiFrame::attackNowMs`, `crestDb`, and the flags `kUiAutoSlow`, `kUiRangeLimited`, `kUiS2Active`;
- 01 §6.3: `HistoryColumn` bits b2–b4;
- 01 §5.4: `#if FCMP_TEST_TAP setTap`;
- 03 §3.10 #4: the tap is a runtime pointer.

**Problem.**
- With no accessor, the `EngineHost` task and every ballistics or Mode task would have to change `IEngine` or
  `ControlIo` mid-sprint.
- A compile-flag tap needs a second `fcdsp` build, which doubles build time and stops the probes from testing the
  shipped archive.

**Fix, before F0 freezes:**
- Replace `uint8_t* phase` with `uint8_t* bits`, in `HistoryColumn`'s layout: b0–1 phase, b2 auto-slow, b3
  range-limited, b4 s2-active.
- Add `struct EngineTelemetry { float attackNowMs[2], releaseNowMs[2], crestDb[2]; };` and
  `virtual void telemetry(EngineTelemetry&) const noexcept = 0;`.
- Delete the `#if FCMP_TEST_TAP` and keep a runtime `setTap(TestTap*)`.

### 12. [major] `EngineHost` is one file, but five blocks of E §9.2

**Where:**
- 01 §5.4: `EngineHost.{h,cpp}` holds routing, SC filters, lookahead, per-engine encode, OS, crossfade, mix, delta,
  bypass, listen, telemetry and poison;
- 01 §2.1: `Delay.h` and `Router.h` exist, but the SC filter sits under `stages/`;
- E §9.2 rows 8–12.

**Problem.** This is L in size, has a single owner, and serialises every host feature.

**Fix.** Pure component headers, each with its own owner:
- `engine/host/{ScFilter.h, Router.h, Delay.h, Ramps.h, Crossfade.h, TelemetryAccum.h}`;
- `engine/Oversampler.{h,cpp}` and `stages/colour/Adaa.h`.

`EngineHost.cpp` becomes orchestration only. The work splits into:
- F4: the ECO skeleton;
- F5: SC filter, router, link and delays;
- F6: the oversampler spike;
- F7: integration (mix in the OS domain, lookahead budget, crossfade and carry, delta, listen).

### 13. [major] `ProcessorFacade` is in the wrong layer, uses undefined types, and ties the UI to a real processor

**Where:**
- 01 §2.1 puts it in `editor/`, but 01 §2.2 rule 3 says plugin code never includes `editor/`;
- 03 §1.1 puts it in `plugin/`;
- 02 §9.5 adds `UiState&` and `StateNotice`, which are never defined, and drops `registry()`;
- 02 §10 wants it "frozen on day 1".

**Problem.**
- The processor implements the facade, so the facade must live in `plugin/`.
- Undefined types cannot be frozen.
- `apvts()` means every UI agent and every UI probe needs a prepared JUCE processor, so UI sprints wait for P1.

**Fix.** Put it at `Source/plugin/ProcessorFacade.h` and freeze it in F0, with these changes:
- `funkgui::ParamPort& port(fcdsp::Pid)` (29 ports) replaces `apvts()`.
- `struct UiState { uint8_t charExpanded, scTab; };`
- `struct StateNotice { bool newerSession; char migratedFromKey[25]; };`
- An abstract `PresetAccess& presets()`, so the preset strip (U6) can be built in parallel with preset integration
  (P3).

U1a owns `Tools/probes/plugin/FakeFacade.{h,cpp}`: in-memory ports, scripted `UiFrame`s and a real `HistoryRing`. The
processor's implementation wraps `JuceParamPort`.

### 14. [major] FunkPresets sits on the session-state path, but HR's owner blocks it

**Where:**
- 01 §9.1: Save step 3 and Load step 6 (`PresetManager::writeState/readState`);
- 01 §9.2;
- 02 §2.2 last row and §11.7: "snapshot only after HR's preset work settles";
- 03 §2.7: `fcmp_probe_plugin` links SQLite.

**Problem.** `proc.state`, including the v1-forever state format, depends on an unscheduled external event.

**Fix.**
- `State.cpp` owns `<PARAMS>`. The `<PRESET>` child goes through an optional read/write `std::function` pair that is
  null until P3.
- State tests never link FunkPresets.
- G8 and P3 are scheduled last.
- The lead chooses a fallback by S6: seed from HR's headers only, and re-implement.

### 15. [major] FCompressor's `Panel.cpp`, `Layout.h` and `Tags.h` are hot files for five UI tasks

**Where:**
- 02 Part 2 intro: `Panel.{h,cpp}` "owns the chrome and both screens", `Layout.h` holds "every constant", plus
  `Tags.h`;
- 02 §10 UI-B;
- 03 §3.6 expects `Panel::views()`, `setView()` and `settled()`, none of which exist in 02.

**Problem.** The band, Characteristics, browser and chrome tasks all edit `Panel.cpp` for hit order, draw order, Tab
order and a11y ids, so the UI work serialises. The probe API is also undefined.

**Fix.** U1a creates `fcmp::ui::Panel` as a **fixed composition** of sub-views:
- `struct SubView { tick; draw; hit; pointer*; wheel; key; accessibility; focusOrder; }`.
- Stub files exist from U1a for Header, DisplayRow, SlotGrid, Band, CharScreen, ModeBrowser, PresetStrip and Footer.
- `Layout.h` and `Tags.h` are written **complete** from 02 §6.3, §7.3 and §8, then frozen.
- a11y ids are `(subView << 16) | local`.
- The probe API is frozen as `setScreen(Screen)`, `setOverlay(Overlay)` and `kProbeViews` (main, chars, modebrowser,
  presetbrowser). Probes settle with `HeadlessHost::settle()`.
- Drop "each band sub-view" from 03 §3.6: 02 §0.8 has none.

### 16. [major] The FunkGui freeze omits concrete helpers that parallel widget tasks need working, and the snapshot takes the `Canvas.h` name

**Where:**
- 02 §10 frozen list;
- 02 §5.2 (the concrete `GestureController`), §5.7 `ease`, §5.8 `text`/`fmt`;
- 02 §4.3: shader and mirror "in the same commit";
- 02 §2.2: the snapshot maps `SdfCanvas.h` to `canvas/Canvas.h`;
- 02 §1.1: a single `test/gallery/GalleryPanel.cpp`.

**Problem.**
- Every widget task needs a working `GestureController`, `ease`, `text::fits` and Canvas recording.
- `SoftRaster` is created by the recorder task and then changed by the AREA task.
- Freezing the new `Canvas.h` would break the still-bgfx-coupled snapshot, which uses that name.
- One gallery file is edited by every widget task.

**Fix.**
- G2 **implements** `GestureController`, `ease`, `fmt`, `text::width/fits/fitEllipsis` and `a11yDumpLine`, and does
  not just declare them.
- `Canvas.h` declares `protected: Prim& emit(Kind)`. HR's primitives go in `Canvas.cpp` (G3) and the new primitives
  in `CanvasShapes.cpp` (G4).
- G4 owns `SoftRaster`, the FrameRender CLI, the AREA shader, its CPU mirror and `Glyphs.def`.
- Gallery sections are self-registering, one per widget: `test/gallery/<Widget>Gallery.cpp`.
- v0.0.1 keeps HR's file stems (`SdfCanvas.h`), so a sed-normalised diff against HR is empty *and* the new names are
  free.

### 17. [major] Adding a Mode rewrites other Modes' goldens

**Where:**
- 03 §3.4 `dsp.switch`: "every pair with this Mode as the source";
- 03 §3.1 item 2: "adding a Mode never rewrites another Mode's goldens";
- 03 §3.6 `ui.browsers` hash;
- 03 §3.4 `registry.count` and `registry.hash`;
- 03 §6 Q8.

**Problem.** Two Mode tasks in one sprint each produce different candidates for `modes/clean/switch.txt`,
`global/registry.txt` and `global/browsers.txt`, and the lead merges them by hand.

**Fix.**
- `dsp.switch.<key>` covers {key ↔ other} for every `other` with a **lower slot**, in both directions. A new Mode
  owns all of its pairs.
- `dsp.registry` and `ui.browsers` become spec-only, with no count or hash rows.
- The browser rows are fingerprinted per Mode, under `modes/<key>/`.

### 18. [major] Factory presets and Mode docs are shared files in the Mode checklist

**Where:**
- 01 §8.4 step 5: `FactoryPresets.cpp`, plus a hand bump of `factoryBankRevision()`;
- 01 §8.4 step 7: `docs/modes/<key>.md`;
- 03 §4.6: `docs/**` belongs to the lead.

**Problem.** Every Mode task edits the same file and the same counter, which guarantees conflicts. The docs rule
contradicts the ownership table.

**Fix.**
- Mode tasks write no presets. P3 writes the bank once.
- Later Modes add `Source/plugin/factory/<key>.inc`, taken in slot order. The bank revision is a compile-time hash of
  the bank, not a hand-edited counter.
- `docs/modes/<key>.md` is owned by the Mode's task, as an explicit exception in 03 §4.6.

### 19. [major] The GPU path is proven last in both plans

**Where:**
- 02 §10: `EditorHost` is buried in UI-A agent 2;
- 03 §3.6: `ui.live` is the lead's job at sprint end;
- A §6.6 lists the risks: INTERFACE sources with ObjC, the class prefix, shader embedding, bgfx inside a plugin.

**Problem.** A failure in FetchContent'd FunkGui plus the GPU path (surface, retry or live-vs-headless parity) would
surface after the whole UI is built on the recorder.

**Fix.** Two spikes:
- G1's DoD includes building `FunkGuiGpu` and the shaders in `agent-gui`, with the prebuilt shaderc and
  `fg.shader.hash`.
- G7 includes a FunkGui gallery Standalone (no-op processor) and shows `capture-frame.sh` output equal to the
  `HeadlessHost` fingerprint, **before** FCompressor's U7.

### 20. [minor] The frozen "headers" are document snippets, not compiling code

**Where:**
- 01 §10.1: `constexpr ParamTable allNa(...)` is declared in `ModeKit.{h,cpp}` with no body;
- 01 §5.3: `static_assert(sizeof(ModeEngine</*every Mode*/…>))`;
- 03 §4.2.1: "frozen interfaces".

**Problem.** A `constexpr` function defined in a `.cpp` cannot be used in another TU's constant initialisers, and
pseudo-code cannot be frozen.

**Fix.** F0 and G2 DoD:
- every frozen header compiles standalone (`Scripts/check-headers.sh` runs `clang++ -std=c++20 -fsyntax-only` per
  header, and CTest `lint.headers` runs it);
- every `constexpr` helper is defined inline;
- the size asserts are live: `EngineParams` 116, `UiFrame` 288, `HistoryColumn` 32, `Prim` 84 (all four recounted
  here and correct).

### 21. [minor] Sprint-end lead work is the real serial bottleneck, and pin bumps churn UI goldens

**Where:** 03 §4.8's ten steps, and 02 §1.10.

**Problem.** A sprint's wall time is its slowest task plus the lead's integration. Every FunkGui minor re-blesses
FCompressor's UI geometry while the UI is still changing.

**Fix.**
- No `ui.geometry` golden is adopted before the UI freeze in S12 (H1). Until then `ui.*` runs spec rows only, and
  `golden_missing` is expected.
- FunkGui is tagged only in sprints that merge a G task.
- asan/tsan and `gui-live` run at milestones only: S4, S8 and S12.
- Tasks within a sprint are balanced to M.

### 22. [minor] FunkGui agents have nowhere to run FCompressor's verify

**Where:** 03 §4.7 item 4 and §4.4.

**Fix.** The lead creates a detached FCompressor worktree at the sprint base for each FunkGui task that changes
rendering or API: `.claude/worktrees/ro-<task>`. The agent builds inside it and never edits it.

### 23. [minor] Probes scheduled before their inputs exist

**Where:** 03 §4.9 S0-B: `proc.layout` and `ui.font`.

**Problem.** `proc.layout` needs the processor. `ui.font` needs every Mode's strings. Neither exists in S0.

**Fix.** Move `proc.layout` to P1, and `ui.font` to U1a (after DW).

### 24. [minor] v1-forever state XML names band views that no longer exist

**Where:** 01 §9.1 `<UI hView tView charExpanded>`, against 02 §9.7 #2.

**Fix.** Settle it in F0 as `<UI charExpanded scTab>`.

### 25. [minor] Naming drift that will leak into manifests

**Where:**
- 03 uses `ModeSpec`, `Recorder`, `EditorShell`, `CompressorEngine`, `staticCurve`, `stepPreview` and
  `introducedInSchema`, and swaps "Draft 1" and "Draft 2";
- 01 uses `EditorShell`, `FcmpDspProbe` and `--golden-dir`.

**Fix.** The synthesiser does one global rename to the 01/02 symbols, and to 03's executable and CLI names.

### 26. [minor] Worktrees live inside the FCompressor checkout

**Where:** 03 §1.3 `.claude/worktrees/*`, and 01 §2.2 `lint.deps`, which greps `#include` lines.

**Fix.** Every glob and lint is rooted at `${PROJECT_SOURCE_DIR}/Source` or `Tools`, never at `**` from the repo root.
`ownership.py` ignores `.claude/`.

---

## 2. Work items, dependencies, critical path

### 2.1 Work items

- **Repo:** FG = FunkGui, FC = FCompressor.
- **"Owns"** gives the manifest roots. Tasks share no file within a sprint.
- **\*** marks a spike: a task that settles an early risk.

| ID | Repo | Scope | Owns (roots) | Depends on | Size |
|---|---|---|---|---|---|
| G0 | FG | **Lead.** Verbatim copy + `SEED.tsv`; sed rename keeping HR file stems; harness-only CMake; tag v0.0.1; `pin-v0.0.1` worktree | whole repo | – | S |
| G1\* | FG | FunkGuiDeps/Targets.cmake (globs); Core/Gpu/Fonts/Harness/Shaders targets; Harness v2; `golden.py`, `verify.sh`; `fg.harness.self`, `font.probe`, `prefs.check`; GPU target and shaders build in `agent-gui` → **v0.1.0** | `cmake/ tools/{golden.py,verify.sh} include/funkgui/test/ test/CMakeLists.txt` | G0 | M |
| G2 | FG | API contracts: `Prim`, `PrimList`, `Canvas` (with `emit`), `Panel`, `Input`, `HostServices`, `ParamPort`, `ValueModel`/`ValueView`/`Detent`, `CellModel`/`ToggleModel`, widget declarations, `A11yItem`, `Tags`. **Implemented:** `GestureController`, `ease`, `fmt`, `text` utilities, `a11yDumpLine` | the new headers + `src/{params,core,text}` | G1 | M |
| G3\* | FG | Recorder: `Canvas.cpp` (HR primitives), dump v2 write/parse, `Fingerprint`, `FontService`, `HeadlessHost`, `BgfxSink` expansion + submit, `BgfxContext` fonts; `canvas.parity`, `canvas.expansion` (HR vertex bit-identity) | `src/canvas/{Canvas,PrimList,Fingerprint}.cpp src/panel/ src/gpu/{BgfxSink,BgfxContext}.cpp` | G2 | M |
| G4 | FG | `CanvasShapes.cpp` (area, areaStrip, polyline, disc, dotted, axis); `fs_ui.sc` AREA + `SoftRaster` mirror; FrameRender CLI; `Glyphs.def` + subset regeneration; 32 MiB transient buffer | `src/canvas/{CanvasShapes,SoftRaster}.cpp shaders/ tools/FrameRender.cpp fonts/ include/funkgui/text/Glyphs.def` | G3 | M |
| G5 | FG | `RuleSlider` (5 states, hybrid, detent fit rule), `AttachedWord`, `FocusRing` + gallery sections | `src/widgets/{RuleSlider,AttachedWord,FocusRing}.cpp test/gallery/{RuleSlider,Word}Gallery.cpp` | G3 | M |
| G6 | FG | `SegmentedSelector`, `LatchToggle`, `ThemeCells`, `HintLine`, `DwellSelector`/`ScreenFader`, `LiveFeed`, `UiPreferences` generic keys, `MenuLook`, `LineEdit` + gallery | `src/widgets/{Segmented,Latch,Theme,Hint,Dwell}* src/live src/prefs src/juce` | G3 | M |
| G7\* | FG | `EditorHost`, `A11yBridge`, `FramePump`/`DisplayLink`/`NativeSurface` with the ObjC prefix, `CaptureConfig`/env, `capture-frame.sh`; gallery Standalone live = headless parity | `src/gpu/{EditorHost,A11yBridge,FramePump,…} tools/capture-frame.sh tools/GalleryApp/` | G3 | M |
| G8 | FG | FunkPresets: seed + `Attribute`/`ProductConfig`/`PresetHooks` (01 §9.2), or the header-only re-implementation fallback | `include/funkgui/presets/ src/presets/` | G1; HR presets settled | M |
| F0 | FC | fcdsp contracts as compiling headers (01 §3–§8 plus #11, #13, #9's `provisional`, `DefineMode.h`). **Implemented:** `HostParams.cpp` (`toPlain`/`toNorm`/`legal`), `ModeKit` constexpr, `Units`, `Seqlock`/`UiFrame`/`HistoryRing` (header-only); `ProcessorFacade.h`; `check-headers.sh` | `Source/fcdsp/**/*.h Source/fcdsp/params/HostParams.cpp Source/plugin/ProcessorFacade.h` | – | M |
| B0 | FC | Build skeleton: `CMakeLists.txt`, `cmake/*` (globs #2, file-driven probes #3, `Modes.def` → CTest), `CMakePresets.json`, `Scripts/{deps.sh,verify.sh,golden.py,ownership.py}`, `probes/common/{ProbeMain,Signals,Tolerances,AllocCounter}`, `dsp.selftest`, stub `Processor` + `CreateEditorGeneric` | `CMakeLists.txt cmake/ CMakePresets.json Scripts/ Tools/probes/common/ Source/plugin/{Processor.*,CreateEditorGeneric.cpp}` | G0 | M |
| F1\* | FC | `Simd` full op set, `FastMath` poly fit, `ScopedFtz`, `Smoother4`, `ControlTicker`; `dsp.simd`, `dsp.units`. **Spike:** the agent-preset (RelWithDebInfo) and lead-preset (Release+LTO) hashes are bit-equal | `Source/fcdsp/core/ probes/dsp/{simd,units}.cpp` | F0, B0 | M |
| F2 | FC | `Resolve.cpp` (`snap`, `resolveView`, `resolve`, `modeDefaults`, `physicalDefault`), `Text.cpp` (`formatValue`/`formatHost`/`parseHost`), Clean descriptor; `dsp.quant` | `params/{Resolve,Text}.cpp modes/clean/CleanDesc.cpp probes/dsp/quant.cpp` | F0, B0 | M |
| F3 | FC | Walking skeleton: `ModeEngine<T>` FF path; `PeakLog`, `QuadKnee` (FF), `LinkMax`, `SmoothBranching`, `ColourNone`, `Flat`; `Registry.cpp`; Clean traits; `Modes.def` slot 0; `EngineRig`, `Measure`; `dsp.registry`, `dsp.static`, `dsp.time` | `engine/ModeEngine.h stages/…(those 6) modes/{Registry.cpp,Modes.def} modes/clean/{Clean.h,Clean.cpp} probes/common/{EngineRig,Measure}* probes/dsp/{registry,static,time}.cpp` | F1, F2 | M |
| F6\* | FC | `Oversampler` (IIR 2× polyphase, FIR 4×, constexpr coefficients), ADAA shapers; **freezes `kStdLatency`/`kHqLatency`**; `dsp.os`, `proc.osref` | `engine/Oversampler.* stages/colour/Adaa.h probes/dsp/os.cpp probes/plugin/osref.cpp` | F1 | M |
| F9\* | FC | Clean complete: `RmsLog`, `DualDet`, `DetSelect`, `Hold`, `CrestAuto`, `ColourSelect` + `TubeSym`/`DiodeAsym`/`Bright`; **FB:** `QuadKnee::solveFb` closed form, `FeedbackZdf` (Newton), `FeedbackDelayed`, stability guard | `stages/{detector,combinators,colour}/… engine/Feedback.h modes/clean/` | F3 | M+ |
| F4 | FC | `EngineHost` ECO skeleton: chunking, route, mono duplication, key select, `Smoother4` pre-gain/makeup/mix, colour call, mix at base rate, bypass ramp, poison, snap flag, telemetry accumulation and publish; `dsp.null`, `dsp.hostile`, `dsp.rt`, `dsp.zipper`, `dsp.telemetry` | `engine/EngineHost.cpp engine/host/{Ramps,TelemetryAccum}.h probes/dsp/{null,hostile,rt,zipper,telemetry}.cpp` | F3 | M |
| DW\* | FC | Descriptor wave: 7 descriptors in full + generic traits + `provisional`; `Modes.def` slots 1–7 | `modes/{busg,fet76,opto2a,mu67,diode609,bus25,brickwall}/** modes/Modes.def` | F2, F3, F9 | M |
| F5 | FC | `ScFilter` (TPT SVF HPF, tilt), `Router` (6 `stmode`s, key bus, link blend), `LinkIndependent`/`Mean`/`CvSum`, `Delay`; `dsp.sc`, `dsp.link` | `engine/host/{ScFilter,Router,Delay}.h stages/link/* probes/dsp/{sc,link}.cpp` | F1 | M |
| F8 | FC | Analysis: `staticGain`, `staticGr`, `localRatio`, `netGainDb`, `stepResponse`, `measure`, `scResponse`, `colourCurve`, `harmonicsDb`; `dsp.analysis` | `Source/fcdsp/analysis/ probes/dsp/analysis.cpp` | F3, F5 | M |
| F7 | FC | `EngineHost` complete: OS domain + mix-in-OS, lookahead and budget, `Crossfade` + carry + kernel key, delta, listen; `dsp.switch` (#17 rule), `dsp.latency`, `dsp.print` | `engine/EngineHost.cpp engine/host/Crossfade.h probes/dsp/{switch,latency,print}.cpp` | F4, F5, F6 | M+ |
| P1 | FC | Processor: buses, configure, `AsyncUpdater` latency, `processBlock`/`Bypassed`, facade implementation, `ParamLayout`, `HostText`; `proc.{layout,text,chunk,latency,bypass,null}` | `Source/plugin/{Processor.*,ParamLayout,HostText}.cpp probes/plugin/{layout,text,chunk,latency,bypass,null}.cpp` | F7, F2 | M |
| P2 | FC | `State.cpp`, `StateMigration.cpp`, `modeId` resolution, snap ordering (#14); `proc.state`, `proc.modeparam`, `proc.fixtures` | `plugin/{State*,StateMigration}.cpp probes/plugin/{state,modeparam,fixtures}.cpp` | P1 | M |
| P3 | FC | Preset integration (hooks), factory bank (#18); `proc.presets`, `proc.prefs` | `plugin/{Presets.cpp,factory/**} probes/plugin/{presets,prefs}.cpp` | P2, G8 | M |
| M1 | FC | Bus G: `DualRelease`, `AutoSwitch`, `VcaBus` | `modes/busg/** stages/…/DualRelease.h …` | DW | M |
| M2 | FC | FET 76: `FetColour`, `law::FetVcr`, ALL, linked minimum attack | `modes/fet76/** …` | DW, F7 | M |
| M3 | FC | Opto 2A: `OptoSense`, `OptoCell`, `R37Shelf`, `TubeTransformer` | `modes/opto2a/** …` | DW | M |
| M4 | FC | Mu 67: `ProgressiveKnee` in `FeedbackZdf`, `TcSelector`, `TubePushPull`, Lat/Vert | `modes/mu67/** …` | DW, F5 | M+ |
| M5 | FC | Diode 609: `SharedElementMax`, `DiodeBridge`, auto releases | `modes/diode609/** …` | DW | M |
| M6 | FC | Bus 25: FF/FB per chunk, `LinkCvSum`, `tmode` variant, 3 knees | `modes/bus25/** …` | DW, F5 | M |
| M7 | FC | Brickwall: `SlidingMaxBox` + scratch, true-peak SC 4×, `LoudClip` | `modes/brickwall/** …` | DW, F7 | M |
| U1a | FC | UI contracts + slots: `Panel` composition (#15), `SubView`, `Layout.h`, `Tags.h`, `SlotModel`, `SlotGrid` (21 slots, words, landing carets), `FakeFacade`; `ui.textfit`, `ui.a11y`, `ui.input`, `ui.font`, `ui.geometry` | `Source/editor/{Panel*,SubView.h,Layout.h,Tags.h,SlotModel*,views/SlotGrid*} + stub views; probes/plugin/{FakeFacade*,ui_*}` | G5, F2, DW | M+ |
| U1b | FC | Chrome: Header (Mode latch), DisplayRow (cells, latches), Footer (spec line, notices, THEME) | `views/{Header,DisplayRow,Footer}*` | U1a, G6 | M |
| U2 | FC | Band: `HistoryStore`, `HistoryPlot`, `TransferPlot` (ghosts, handles, curve landing), `MeterColumn`, live slot subs; `ui.curve`, `ui.truth` | `editor/HistoryStore.h views/{Band,HistoryPlot,TransferPlot,MeterColumn}* probes/plugin/ui_{curve,truth}.cpp` | U1a, G4, F8 | M+ |
| U3 | FC | Characteristics A: `CharScreen`, `ControlPathPlot`, `Readouts`, fader | `views/{CharScreen,ControlPathPlot,Readouts}*` | U2 | M |
| U4 | FC | Characteristics B: `StepPlot` + `PreviewWorker`, `SidechainPlot`, `ColourPlot` | `views/{StepPlot,SidechainPlot,ColourPlot}* editor/PreviewWorker*` | U1a, F8 | M |
| U5 | FC | `ModeBrowser`, Tab-order and a11y audit across sub-views, browser `ui.input` rows | `views/ModeBrowser*` | U1b | M |
| U6 | FC | `PresetStrip` + preset browser over `PresetAccess` | `views/{PresetStrip,PresetBrowser}*` | U1b, G8 | M |
| U7 | FC | GPU `Editor` (EditorHost subclass), `CreateEditorGpu`, `gui-live.sh`, `FCMP_UI_FIXED_DT`/`NO_LIVE` | `Source/editor/gpu/ plugin/CreateEditorGpu.cpp Scripts/gui-live.sh` | G7, P1, U1b | M |
| H1 | both | Hardening: asan/tsan fixes, UI geometry goldens adopted, bench, [H]-fit backlog triage | assigned by the lead | all | M |

### 2.2 Dependency graph and critical path

```
FunkGui     G0 ─► G1 ─► G2 ─► G3 ─┬─► G5 ─────────────────────┐
                                  ├─► G4 ──────────────────┐  │
                                  ├─► G6 ──────────┐       │  │
                                  └─► G7 ────────┐ │       │  │
            G1 ─► G8 ─────────────────────┐      │ │       │  │
FCompressor                               │      │ │       │  ▼
            F0,B0 ─► F1 ─┬─► F3 ─► F9 ─► DW ─────┼─┼──────►U1a ─┬─► U2 ─► U3
                  └─► F2 ┘     │         │ ▲     │ │       ▲    ├─► U4 ◄─ F8
                         F1 ─► F6 ─┐     │ └─ F2 │ └─► U1b ◄────┤
                         F1 ─► F5 ─┼─► F8│       │      ├─► U5  │
                         F3 ─► F4 ─┴─► F7 ─► P1 ─┴──────┴─► U7  │
                                       │     └─► P2 ─► P3 ◄─ G8 └─► U6 ◄─ G8
                                       └─► M2, M7   DW ─► M1, M3, M4, M5, M6
```

- **Critical chain:** F1/F2 → F3 → F9 → DW → U1a → U2 → U3, which is 7 sprints (S1–S8).
- DW sits on the critical path because the UI needs all 8 descriptors, and the generic FB traits need F9.
- The FunkGui chain G1 → G2 → G3 → G7 → U7 has slack. So does the Mode work: M1–M7 need only DW, plus F5 or F7 for
  M2, M4, M6 and M7.
- The plan is **throughput-bound**: 39 tasks over 3 slots is 13 sprints. The lead should schedule the DAG, pulling
  forward any task whose dependencies are merged, and give priority to the critical chain.

### 2.3 Interface freeze points

| Freeze | End of | What becomes Sprint-frozen | Evidence required |
|---|---|---|---|
| FZ0 | S0 | 01 §3–§8 headers (with #6, #9, #11, #13, #24 applied); `DefineMode.h`; `ProcessorFacade.h`; Harness v2 API, golden format v2, exit codes; FunkGui CMake target and function names; probe registration macro and CLI; CMake glob map | `check-headers.sh` green; `fg.harness.self`; `dsp.selftest` |
| FZ1 | S1 | FunkGui canvas, panel and widget API (G2); `resolve`/`snap` semantics | `dsp.quant.clean`; G2 syntax + utility unit tests |
| FZ2 | S2 | `ModeEngine<T>`/`EngineRig` behaviour; `kStdLatency`/`kHqLatency` (**v1-forever** from here on) | `dsp.static.clean`, `dsp.os` |
| FZ3 | S4 | `ParamSpec`/`ModeDescriptor` schema, proven by all 8 descriptors | DW: `dsp.registry` + `dsp.quant.*` for 8 Modes |
| FZ4 | S5 | FCompressor UI composition: `SubView`, `Layout.h`, `Tags.h`, `FakeFacade`, probe views | `ui.textfit.*`, `ui.a11y.*` for 8 Modes |
| FZ5 | S12 (H1) | UI geometry: `ui.geometry` goldens adopted | `gui-live` parity |
| v1 | v1 tag | `kHostParams`, `Modes.def` slots and keys, state XML, preset payload | 01 §0 |

### 2.4 Spikes, and when they run

| Spike | Task / sprint | Question it answers | Fallback if it fails |
|---|---|---|---|
| GPU build chain | G1 / S0 | Do `FunkGuiGpu`, the INTERFACE `.mm` sources, the ObjC prefix macro and the shaders build via the prebuilt shaderc? | Build shaderc from source (03 §2.5 fallback) |
| Config determinism | F1 / S1 | Are agent (RelWithDebInfo) and lead (Release+LTO) hashes bit-equal (03 §2.6)? | Agents build Release with `FCOMPRESSOR_LTO=OFF` (03 §6 Q5) |
| Recorder parity | G3 / S2 | Is the `BgfxSink` expansion bit-identical to HR's vertex stream (02 §3.2)? | Keep HR's `SdfCanvas` path under the recorder until it is fixed |
| Oversampler | F6 / S2 | Integer latency ≤ 4 / ≤ 64 samples, constexpr coefficients, passband spec (01 §5.6) | Accept JUCE's measured latencies as the targets |
| FB solvers | F9 / S3 | Closed-form and Newton ZDF: stable, and `staticGr` bit-equal to the audio path (E §2.6, §6.5) | `FeedbackDelayed` for opto only |
| Descriptor schema | DW / S4 | Does the `ParamSpec` schema express all 8 Modes (#9)? | Revise the schema at FZ3, before any UI depends on it |
| Live parity | G7 / S8 | Is a live capture equal to the headless fingerprint (C G1)? | Parity gate on geometry only, with text excluded |
| FunkPresets availability | lead / by S6 | Has HR's preset code settled? | Seed from HR's headers and re-implement (02 §11.7) |

### 2.5 Shared-file hotspots and how each is removed

| File | Who would collide | Mechanism |
|---|---|---|
| `CMakeLists.txt`, `cmake/*.cmake`, FunkGui `cmake/` | every task that adds a file | per-directory globs (#2); never edited after S0 |
| `FcmpProbes.cmake` lists, `ProbeMain` dispatch | every probe-adding task | file-driven, self-registering probes (#3) |
| `Modes.def` | Mode tasks | lead in the sprint base, or the single DW task (#8) |
| `Registry.cpp`, `ModeIncludes.h` | Mode tasks | `FCDSP_DEFINE_MODE` externs; no generated include list (#6) |
| `stages/{Detector,Ballistics,Colour,…}.h` | Mode and policy tasks | one header per policy, no umbrella (#7) |
| `ModeKit.h` | Mode tasks adding helpers | frozen per sprint; Mode-local helpers stay in `modes/<dir>/` |
| `FactoryPresets.cpp`, `factoryBankRevision()` | Mode tasks | P3 only; later `factory/<key>.inc`; hashed revision (#18) |
| `modes/clean/switch.txt`, `global/registry.txt`, `global/browsers.txt` | Mode tasks' candidates | pairs owned by the higher slot; spec-only globals (#17) |
| `EngineHost.cpp` | host features | components in `engine/host/*.h`; one owner per sprint (#12) |
| `Processor.cpp` | P1, P2, P3 | P1 owns it; state and presets through their own files and hooks |
| `editor/Panel.cpp`, `Layout.h`, `Tags.h` | UI tasks | sub-view composition; complete and frozen in U1a (#15) |
| FunkGui `GalleryPanel.cpp`, `Glyphs.def`, `Canvas.cpp` | G4, G5, G6 | per-widget gallery files; G4 alone owns glyphs; `emit()` + `CanvasShapes.cpp` (#16) |
| `Tolerances.h`, `tests/golden/**`, `tests/fixtures/**`, `CMakePresets.json`, `CLAUDE.md` | – | lead only (as in 03 §4.6) |
| `docs/modes/<key>.md` | Mode tasks | owned by that Mode's task (#18) |

---

## 3. Sprint skeleton

Rules:
- ≤ 3 agent tasks per sprint, each sized M.
- A task depends only on work merged at an earlier sprint boundary. For FunkGui, that means **tagged**.
- **Lead pre-work** is G0, the `.deps` cache and the base commits.

| Sprint | Slot 1 | Slot 2 | Slot 3 | FunkGui tag at end | Lead extras |
|---|---|---|---|---|---|
| S0 | G1\* FunkGui build + Harness v2 (M, FG) | F0 fcdsp contracts (M) | B0 build skeleton (M) | v0.1.0 | pin v0.1.0; FZ0 |
| S1 | F1\* math core (M) | F2 resolver/text/Clean descriptor (M) | G2 FunkGui API + utilities (M, FG) | v0.2.0 | FZ1; first `lead`-preset determinism check |
| S2 | F3 walking-skeleton engine (M) | F6\* oversampler (M) | G3\* recorder (M, FG) | v0.3.0 | FZ2; first Clean DSP goldens |
| S3 | F9\* Clean complete + FB (M+) | F4 EngineHost ECO (M) | G5 RuleSlider/words (M, FG) | v0.4.0 | – |
| S4 | DW\* descriptor wave (M) | F5 SC/router/link/delay (M) | G4 shapes/AREA/raster/glyphs (M, FG) | v0.5.0 | FZ3; asan/tsan; the lead reads HR's preset status |
| S5 | F8 analysis (M) | U1a UI contracts + slots (M+) | G6 cell widgets (M, FG) | v0.6.0 | FZ4 |
| S6 | F7 EngineHost complete (M+) | U2 band (M+) | U1b chrome (M) | – | FunkPresets fallback decision |
| S7 | P1 processor (M) | U4 Characteristics B (M) | M1 Bus G (M) | – | – |
| S8 | M2 FET 76 (M) | U3 Characteristics A (M) | G7\* EditorHost + gallery parity (M, FG) | v0.7.0 | asan/tsan; gui-live (gallery) |
| S9 | M3 Opto 2A (M) | M4 Mu 67 (M+) | U5 browser + Tab/a11y (M) | – | – |
| S10 | M5 Diode 609 (M) | M6 Bus 25 (M) | U7 GPU editor + live parity (M) | – | first FCompressor gui-live |
| S11 | M7 Brickwall (M) | P2 state/migration (M) | G8 FunkPresets (M, FG) | v0.8.0 | – |
| S12 | P3 presets + factory bank (M) | U6 preset strip (M) | H1 hardening + UI golden freeze (M) | – | FZ5; universal build; install smoke test |

Notes:
- **Re-ordering is safe** when it keeps the dependency column of §2.1. The likeliest pull-forwards:
  - P2 into S8, swapped with U3 (state is v1-forever, so earlier evidence is worth more);
  - M1 into S5 if U1a slips.
- **Worst balance:**
  - S3 (F9 is M+): if F9 runs long, F4 and G5 still merge. The lead may carry F9 into S4 and push DW to S5, which
    delays the UI chain by one sprint.
  - S6 has two M+ tasks. Start U2 and F7 first in the sprint.
- **Mode waves after v1** follow the same pattern: one DW task per wave (≤ 6 descriptors, which also owns the wave's
  `Modes.def` lines), then ≤ 3 Mode DSP tasks per sprint, with no shared files.

---

## Top 5 changes the synthesiser must make

1. **One canonical tree, and a build that needs no shared edits** (#1, #2, #3, #26).
   - Write the §1 #1 tree into 01 §2.1 and 03 §1.1.
   - Replace every explicit source list with per-directory `CONFIGURE_DEPENDS` globs (FunkGui too).
   - Register probes and tests from self-describing probe files instead of the CMake lists.
2. **Make Mode work file-disjoint** (#6, #7, #8, #17, #18).
   - `FCDSP_DEFINE_MODE` extern entries instead of a `Registry.cpp` that instantiates everything.
   - One header per stage policy; `modes/<dir>/` per Mode.
   - `Modes.def` edited only by the lead or the descriptor-wave task.
   - Switch pairs owned by the higher slot; spec-only global goldens.
   - No factory presets in Mode tasks; the Mode sheet owned by its task.
3. **Complete the S0 contracts before any fork** (#11, #13, #16, #20, #24).
   - `ControlIo::bits` + `IEngine::telemetry()`; runtime tap only.
   - `ProcessorFacade` in `plugin/` with `port(Pid)`, defined `UiState`/`StateNotice`, and `PresetAccess`.
   - FunkGui API with a *working* `GestureController`/`ease`/`text`, and `Canvas::emit`.
   - Every frozen header compiles standalone (`check-headers.sh`).
4. **Break the needless serial chains** (#9, #10, #12, #14, #15, #19).
   - `EngineRig` for the D1/D2/D3/D9 spec probes.
   - `EngineHost` split into F4, F5, F6 and F7 components.
   - An early descriptor wave with `provisional` Modes, so schema freeze FZ3 rests on all 8 Modes.
   - `FakeFacade` and sub-view composition for the UI.
   - FunkPresets off the state path.
   - The GPU spikes in G1 and G7.
5. **Replace 02 §10 and 03 §4.9 with the §3 skeleton** (#4, #5, #21, #22).
   - Lead-made v0.0.1 with a harness-only CMake, so S0 runs 3 agents with no bootstrap cycle.
   - The pipelining rule: FCompressor consumes only tagged FunkGui, one sprint behind.
   - 03's version numbering.
   - UI goldens adopted only at the UI freeze.
   - Read-only FCompressor worktrees for FunkGui agents.
   - Milestone-only asan/tsan and gui-live.
