# K1: cross-document consistency and completeness review

Lens: K1, cross-document consistency and completeness. Reviewed 2026-09-22.

Inputs, all read in full:
- `01-core-contracts.md` (01);
- `02-funkgui-and-ui.md` (02);
- `03-build-verify-process.md` (03).

The research reports A–F were consulted where a claim needed checking. Citations use `01 §x`, and so on. Severity levels:
- **blocker:** the three docs cannot all be implemented as written, or the Definition of Done becomes impossible;
- **major:** an interface gap or contradiction that would cost a sprint or produce wrong behaviour;
- **minor:** naming, wording or a local defect.

---

## Issues

### 1. Two different source trees [blocker]

**Where:**
- 01 §2.1 (tree);
- 01 §3.2 and §4.1 (`#include "fcdsp/params/Pid.h"`);
- 01 §8.1, §8.3 and §8.4 (paths);
- 03 §1.1 (tree and the include-rules table);
- 03 §2.1, §2.7 and §2.9 (`FCMP_MODES_DEF`);
- 03 §4.2 (manifest example) and §4.6;
- 02 Part 2 intro.

**What is wrong:**
- 01 puts the DSP code in `Source/fcdsp/{core,params,modes,engine,telemetry,analysis}`, with Modes in `Source/fcdsp/modes/` and stages in `engine/stages/`.
- 03 puts it in `Source/dsp/{simd,units,params,stages,sc,os,engine,telemetry,analysis}`, with Modes in a top-level `Source/modes/`.
- The two trees give different include prefixes, a different `Modes.def` path and different OWNS/FROZEN globs.
- The editor class also has three homes:
  - `editor/Editor` (01);
  - `editor/gpu/Editor` (02);
  - `editor/gpu/FcmpEditor` (03).
- `ProcessorFacade.h` is in `editor/` in 01 and in `plugin/` in 03. 01's own rule 2.2‑3 says `plugin/` never includes `editor/`, yet the processor must implement the facade. So 01 contradicts itself.

**Fix:**
- Adopt 01's `Source/fcdsp/` tree, so that directory, namespace and include prefix are all `fcdsp`. Modes go in `Source/fcdsp/modes/` and stage policies in `Source/fcdsp/engine/stages/`.
- Adopt 03's plugin/editor split:
  - `Source/plugin/CreateEditorGpu.cpp` and `CreateEditorGeneric.cpp`;
  - `Source/editor/gpu/Editor.{h,cpp}`, holding `class fcmp::ui::Editor : public funkgui::EditorHost`;
  - `Source/plugin/ProcessorFacade.h`.
- Rewrite these to match: 03 §1.1 (tree and include table), §2.9 (`FCMP_MODES_DEF = Source/fcdsp/modes/Modes.def`), and the path examples in §4.2 and §4.6.

### 2. The mix‑0 and below‑threshold null specs contradict the in‑OS mix decision [blocker]

**Where:**
- 03 §3.4 `dsp.null` ("mix 0 is bit-exact against the delayed input");
- 03 §3.5 `proc.null` ("bit-exact");
- 03 §3.7, the rows "mix 0, bypass passthrough" and "Below-threshold null";
- 01 §5.6 ("Consequence for C §5.8 D8a");
- the comment under 01 §10.3.

**What is wrong:**
- 01 runs mix inside the OS domain. At STD (the default) and at HQ, mix = 0 is therefore `down(up(delayed x))`, not a delay.
- As written, every Mode fails `dsp.null` and `proc.null` with `spec_fail`. `verify.sh` can then never be green.

**Fix.** Replace the rows in 03 §3.4, §3.5 and §3.7 with these:
- **mix = 0, and below threshold for rigor `clean` with voice OFF:**
  - bit-exact against a control `fcdsp::Oversampler` round trip of the delayed input at the same Quality;
  - at ECO, bit-exact against the delayed input;
  - plus the spec |H| within ±0.01 dB to 20 kHz at 44.1 kHz.
- **Bypass:** bit-exact against the delayed input at both ends (unchanged).
- Split the 03 §3.7 row in two to match.

### 3. FunkGui's CMake fails when the prebuilt shaderc is used [blocker]

**Where:**
- 02 §1.4 (`if(NOT TARGET shaderc) message(FATAL_ERROR …)`, which is unconditional);
- 02 §1.5 (`COMMAND $<TARGET_FILE:shaderc>` and `DEPENDS shaderc`, both unconditional in the code);
- 03 §2.5 (`fcmp_select_shaderc` sets `BGFX_BUILD_TOOLS=OFF` when the stamp matches).

**What is wrong:** once `deps.sh` has run, bgfx.cmake creates no `shaderc` target. FunkGui then stops the configure with a FATAL error in every FCompressor GPU build. 02's prose says the check is conditional, but its code is not.

**Fix:**

```cmake
if(NOT FUNKGUI_SHADERC AND NOT TARGET shaderc)
  message(FATAL_ERROR "FunkGui: no shaderc (set FUNKGUI_SHADERC or build bgfx with BGFX_BUILD_TOOLS_SHADER=ON)")
endif()
if(FUNKGUI_SHADERC)
  set(_fg_shaderc "${FUNKGUI_SHADERC}")
  set(_fg_shaderc_dep "")
else()
  set(_fg_shaderc "$<TARGET_FILE:shaderc>")
  set(_fg_shaderc_dep shaderc)
endif()
# funkgui_compile_shader: COMMAND ${_fg_shaderc} …  DEPENDS ${_fg_shaderc_dep} …
```

### 4. Probe executables, test registration and golden paths are defined twice [major]

**Where:**
- 01 §2.1:
  - the targets table: `FcmpDspFlags`, `FcmpDspProbe`, `FcmpProcProbe`, `FcmpUiProbe`;
  - the tree entry `Tools/ Harness.h golden/`.
- 01 §8.3:
  - the `add_test` loop: `--golden-dir ${FCMP_GOLDEN_ROOT}`, no `verify` label, a `dsp.state` test, a bare `registry` test;
  - the lint path `Tools/golden/modes/<key>/`.
- 01 §8.4, steps 3 and 6 ("unit rows in FcmpDspProbe"; "exit 2").
- 02 §8.9 (`Tools/golden/…/ui/a11y/<screen>-<mode>.txt`).
- 03 §2.6–§2.9 and §3.2.2–§3.2.4.

**What is wrong:**
- There are two incompatible registration blocks, flag targets, probe sets and golden roots.
- 01's tests carry no `verify` label, so `ctest -L verify` skips them.
- 01 says a missing golden exits 2. 03 uses exit 3 (`golden_missing`).
- 01 keeps a local `Harness.h`, while 02 and 03 put Harness v2 in `FunkGui::harness`.

**Fix.** 03 owns build and verification:
- targets: `fcmp_flags`, `fcmp_lto`, `fcmp_warnings`, `fcmp_probe_dsp`, `fcmp_probe_plugin`, `fcmp_bench`;
- registration: `fcmp_probe_test()`;
- paths: `--golden-root tests/golden` with base/arch overlays;
- exit codes 0–4;
- the probe lists `FCMP_DSP_MODE`, `FCMP_PROC_MODE` and so on, with `state` as `proc.state` and the registry test as `dsp.registry`.

Changes to 01:
- Delete 01's `add_test` loop and its probe-target rows, and point to 03 §2.7 and §2.9 instead.
- Move 01 §8.3's lint list into the spec of 03's `dsp.registry`, as a union with 03's own items. 01 adds these checks:
  - DisplayMap round trip;
  - `defaultPlain` is a fixed point of `snap`;
  - no derived spec depends on a derived spec;
  - variant drivers come first in the resolve order;
  - the engine fits the arena.

The a11y golden in 02 §8.9 becomes the `lines` sidecar `tests/golden/base/modes/<key>/ui.a11y.<screen>.lines`.

### 5. How Mode sources get into the build [major]

**Where:**
- 01 §8.3 (parses `Modes.def` into `target_sources` and a generated `fcdsp/ModeIncludes.h`), and the comment in 01 §8.1 with step 4 of 01 §8.4 ("lead assigns the slot at merge");
- 03 §1.1, §2.1 and §3.8 step 1 (`file(GLOB CONFIGURE_DEPENDS Source/modes/*.cpp)`);
- 03 §4.2 (slot reservations at sprint start).

**What is wrong:**
- With the GLOB, `Registry.cpp` has no generated include list for its X‑macro expansion.
- A half-written `.cpp` for a Mode that is not yet registered still compiles into `fcdsp`.
- 01 and 03 also disagree on when slots are assigned.

**Fix:**
- Keep 01's approach: `Modes.def` drives `target_sources` and `ModeIncludes.h`.
- Add 03's validation: a malformed `FCMP_` line is a FATAL error.
- Drop the GLOB. Reserved lines stay commented, and the `^FCMP_MODE\(` regex skips them.
- Slots are reserved by the lead at sprint start (03 §4.2). Fix the comments in 01 §8.1 and §8.4 to say so.

### 6. The test tap: compile flag or runtime pointer [major]

**Where:**
- 01 §5.4 (`#if FCMP_TEST_TAP void setTap(TestTap*)`, where `TestTap` is never defined);
- 01 §1.1 (probes test the shipped object code);
- 03 §0.5, the note under §3.4 ("`ControlIo::tap`", which 01's `ControlIo` does not have), and §3.10 #4 (runtime).

**What is wrong:** 01's `#if` needs a second `fcdsp` build, which breaks 01's own rule and 03's decision. The type is also undefined.

**Fix.** Put this in `Source/fcdsp/engine/TestTap.h`, always compiled:

```cpp
struct TestTap {                       // probes only; the plugin never sets it
    float*   grDb[2]; float* detDb[2]; float* tgtDb[2]; float* s2GrDb[2];   // per engine lane, base rate
    uint8_t* phase;                    // max lane
    int      capacity;                 // samples per array
    int      written = 0;              // advanced by EngineHost
};
void EngineHost::setTap(TestTap*) noexcept;   // null = off; checked once per chunk; fills ControlIo's nullable outputs
```

Also correct 03's wording from "`ControlIo::tap`" to "`EngineHost::setTap` feeding `ControlIo`'s nullable outputs".

### 7. The live curves cannot be rebuilt from `UiFrame` [major]

**Where:**
- 02 §9.1, the row "`preGainDb … tags, discrete`" ("the UI builds an `EngineParams` from these fields");
- 02 §0.11 ("the UI needs no field 01 lacks");
- 01 §6.2.

**What is wrong:** `UiFrame` carries none of these:
- `EngineParams::m[8]`;
- `topo` (only the `kUiTopoFB` flag);
- `EngineParams::flags` (`kEngAutoMakeup`, `kEngTruePeak`, `kEngAutoRelease`).

Several Modes keep curve-shaping constants in `m[]`:
- Opto 2A: the LIMIT law `m[1]` and R37 `m[0]`;
- Mu 67: `o_c` and `S_max`;
- Diode 609: SLOW SC‑HP.

Their TRANSFER, ghosts and knee marks would therefore be wrong while the audio is live.

**Fix.** Leave the `UiFrame` layout unchanged, and add to 01 §6.2 (`telemetry/UiFrame.h`):

```cpp
// Copies the smoothed continuous fields (preGainDb … s2RelTauMs, makeupEffDb→makeupDb) onto an EngineParams
// produced by resolve(). The caller skips it when frame.modeSlot != eng's slot or kUiFading is set.
void overlaySmoothed(const UiFrame&, EngineParams& inOut) noexcept;
```

- The UI runs `resolve()` on the current raw values, cached by hash, and then calls `overlaySmoothed`.
- Probe `ui.truth` does the same.
- Correct 02 §0.11 and §9.1 to describe this.

### 8. `look` and `labudget` are invisible to the resolver [major]

**Where:**
- 01:
  - §3.1, the `look` row ("clamped to the budget");
  - §4.4 (`RawParams`);
  - §4.6 (`formatHost`);
  - §5.4 step 1;
  - §10.7 Brickwall (`look` is `cont(0.5, 20, 5)`; `atk` is derived from `look`).
- 02 §6.4 B3 (a UI overlay "locked at 0"), §9.4 and §9.7 #6.

**What is wrong:** the resolver, the UI, host text and the engine disagree:
- The engine clamps `look`.
- The UI fakes a locked state, which 01 has no concept of.
- Host text prints the unclamped value.
- Brickwall's derived attack shows "= LOOKAHEAD 5 MS" while the engine runs 0.
- Brickwall's `lo = 0.5` contradicts "locked at 0".

**Fix:**
- Add `LookaheadBudget budget = LookaheadBudget::off;` to `RawParams`. Every caller fills it from `labudget`, just as it fills `modeSlot`. This must happen before the Sprint-frozen `RawParams` is frozen.
- In `resolveView`, after `look` is snapped:
  - compute `hiEff = min(spec.hi, budgetMs)`;
  - if the budget is `off`, set state `locked`, plain 0, tag `"OFF"`, and reason `LOOKAHEAD BUDGET IS OFF — SET 5 MS OR 20 MS (ADDS LATENCY)`;
  - otherwise clamp to `hiEff` and set `kClamped`.
- Derived specs such as Brickwall's `atk` then read the clamped value.
- The clamp in 01 §5.4 step 1 becomes a debug assert.
- Delete 02's B3 overlay.

### 9. Absolute handle drags map plot values through the wrong function [major]

**Where:**
- 02 §6.5, the "Handles" bullet ("absolute … every continuous `thr`/`knee`/`range` through `DisplayMap::toPlain`");
- 02 §5.3 (`plotToHost01`);
- 01 §4.1 (`DisplayMap` is dial ↔ plain);
- 01 §10.5 (FET `thrOffset[ratio]`);
- 01 §10.7 (Mu 67 knee = DC THRESH → `o_c`).

**What is wrong:**
- Handles sit at `T_in = eng.thrDb − eng.preGainDb` in input-referred dBFS, not on the dial scale.
- For FET 76, `T_in = thr + thrOffset[ratio]`.
- For Mu 67, the knee width is not the knee plain value.
- An absolute drag therefore jumps by the offset or uses the wrong units.

**Fix:**
- Add to 01 §7: `float analysis::inputThresholdDb(const EngineParams& e) noexcept { return e.thrDb - e.preGainDb; }`.
- **Threshold handle:** `thr_new = thr_cur + (T_target − T_cur)`.
  - This is correct because `T_in` is affine in `thr` with slope 1 in every Mode.
  - Add that slope as a `dsp.registry` lint: `|dT_in/dthr − 1| ≤ 1e‑4`, at 5 points per Mode and per ratio step.
- **Knee and range handles:** absolute only when the spec has an identity `DisplayMap` and the new flag `kFlagPlotIsPlain = 1u << 4` (01 §4.1 `SpecFlag`), which Clean sets. Otherwise the drag is relative at 240 px per track.
- 01 §10.3 sets the flag on Clean's `thr`, `knee` and `range`.

### 10. The UI API that the probes drive is never defined [major]

**Where:**
- 03 §3.6:
  - `panel.setView(v)`, `panel.settled()`, `Panel::views()`, `funkgui::Recorder`;
  - the view list "main, each band sub-view, chars + internal panels";
  - `ui.truth` "each INTERNALS lane";
  - `ui.curve` `x + staticCurve(x)`.
- 03 §4.7 item 5 (`ui.dump --view`), and gui-live's `FCMP_UI_VIEW` (03 §3.6).
- 02 §3.10 ("view list, setScreen", supplied by the product).
- 02 §5.1 (`FCMP_UI_SCREEN`, `FCMP_UI_BROWSER`), §5.10 (`--png`) and §3.7 #6 (`hint.skip()`).

**What is wrong:**
- 02 never declares `fcmp::ui::Panel`'s public API.
- The band has no sub-views (02 §0.8).
- The "INTERNALS lanes" do not exist.
- The docs use two different sets of environment variables.
- The live capture would show the first-run hint, which the headless run skips.

**Fix.** Add to 02 Part 2 (`Source/editor/Panel.h`):

```cpp
namespace fcmp::ui {
enum class Screen  : uint8_t { panel, characteristics };
enum class Overlay : uint8_t { none, modeBrowser, presetBrowser };
struct ViewSpec { const char* id; Screen screen; ScTab tab; Overlay overlay; };
std::span<const ViewSpec> views() noexcept;   // "panel", "chars.sidechain", "chars.colour", "modebrowser", "presetbrowser"
struct PanelOptions { bool skipHint = false; bool syncPreview = false; bool ignoreLive = false; };
class Panel final : public funkgui::Panel {
public:
    Panel(ProcessorFacade&, PanelOptions);
    void setView(const ViewSpec&, bool instant = true);   // probes; the user path uses the latch/browser
};
}
```

Changes in 03:
- §3.6 uses `funkgui::HeadlessHost::settle()` and `funkgui::Canvas`, with the view ids above.
- `ui.truth` checks the CONTROL PATH `internal0` lane and READOUTS rows 11–18.
- `ui.curve` checks `x + staticGain(x) − preGainDb`.

One set of environment variables:
- `FCMP_UI_VIEW=<ViewSpec::id>` replaces `FCMP_UI_SCREEN` and `FCMP_UI_BROWSER`;
- `FCMP_UI_NO_HINT=1` is new;
- `gui-live.sh` also sets `FCMP_UI_FIXED_DT` and `FCMP_UI_NO_LIVE`.

One dump command: `fcmp_probe_plugin ui.dump --view <id> --mode <key> --out x.dump [--png x.png]`.

### 11. `PreviewWorker` makes headless frames non-deterministic [major]

**Where:**
- 02 §7.1 and §9.3 (`PreviewWorker` is a `juce::Thread`);
- 02 §3.7 (the determinism rules, which do not cover threads);
- 03 §3.6 (G1 goldens of the Characteristics views).

**What is wrong:** `HeadlessHost::settle()` ticks with a fixed `dt` and runs faster than real time. The step panes then depend on when the worker thread happens to finish, so `ui.geometry.*` is flaky.

**Fix:**
- Add determinism rule 7 to 02 §3.7: "No drawn state may depend on another thread's completion time."
- With `PanelOptions::syncPreview` set (probes and `FCMP_UI_FIXED_DT` captures), `tick()` runs `stepResponse` inline whenever its inputs change.
- `wantsFullRate()` returns true while a job is pending.

### 12. FunkGui's seed and first tag are specified two ways, and 02 overrides onto live worktrees [major]

**Where:**
- 02 §0.2 and §2.1:
  - commit 1 is already sed-renamed and tagged `v0.0.1`;
  - `v0.1.0` = the full generalisation.
- 02 §10, UI‑A Agent 3 ("uses `FETCHCONTENT_SOURCE_DIR_FUNKGUI` → agent 1/2 worktrees").
- 03 §2.3 (the comment "first pin v0.1.0 = end of S0‑H").
- 03 §4.5 ("Never another agent's live worktree").
- 03 §4.9:
  - the lead commits a raw HR snapshot;
  - S0‑H renames and adds CMake and Harness v2, giving `v0.1.0`;
  - S0‑A does the recorder split, giving `v0.2.0`.

**What is wrong:**
- The two docs give different contents for the same tags, and a different seed commit.
- 02's UI plan breaks 03's override rule.

**Fix.** One sequence:
1. The lead's commit 1 is a byte-identical HR copy plus `SEED.tsv`, untagged. It is the provenance baseline.
2. S0‑H applies the sed renames and adds CMake and Harness v2 → **`v0.1.0`**, the first pin, as in 03.
3. Canvas, AREA and glyphs → `v0.2.0`.
4. Widgets → `v0.3.0`.

Drop `v0.0.1`. FCompressor UI work consumes only tags, or a lead-made detached `FunkGui.wt/pin-<sha7>` worktree. So the FCompressor Panel starts in the sprint **after** the FunkGui widgets are tagged (see #13).

### 13. There is no single sprint plan, and the agent limit is not provably kept [major]

**Where:**
- 01 §0 (the freeze table);
- 02 §10 (sprints UI‑A and UI‑B; its day‑1 frozen FunkGui headers);
- 03 §4.2 (a freeze list taken from E §9.2) and §4.9 (S0, then S1 = E rows 4–13 plus the first 8 Modes).

**What is wrong:**
- UI‑A/B and S1+ are not placed on one timeline. Running them as written can exceed 3 concurrent agents.
- The three freeze lists differ.

**Fix.** Give 03 §4.9 one table in which every row has ≤ 3 agents across both repositories. For example:

| Sprint | Agents |
|---|---|
| S0 | S0‑H; then S0‑A canvas/AREA, S0‑B probes, S0‑C fcdsp core |
| S1 | engine host + OS; stage policies; Clean + Bus G |
| S2 | FunkGui widgets and `EditorHost`; FET 76 + Opto 2A; `fcmp_probe_plugin` proc suite |
| S3 | FCompressor Panel + band (on the S2 tag); Mu 67 + Diode 609; Bus 25 + Brickwall |
| S4 | Characteristics + `PreviewWorker`; Mode browser + presets strip + a11y; UI probes G1–G6 |

There is one frozen-interface list, with file paths: 01 §0's Sprint-frozen rows, plus 02 §10's FunkGui headers, plus `Source/plugin/ProcessorFacade.h`.

### 14. `ProcessorFacade` types are undefined, and the `<UI>` state child is stale [major]

**Where:**
- 02 §9.5 (`UiState`, `StateNotice`, neither defined, although 02 §10 freezes this header on day 1);
- 01 §2.2 (`registry()`);
- 01 §9.1 (`<UI hView tView charExpanded>`);
- 02 §9.7 #2–#3.

**Fix.** Put this in `Source/plugin/ProcessorFacade.h`:

```cpp
namespace fcmp {
enum class ScTab : uint8_t { sidechain, colour };
struct UiState    { bool charExpanded = false; ScTab scTab = ScTab::sidechain; };  // <UI charExpanded="0|1" scTab="sidechain|colour"/>
struct StateNotice { uint32_t serial = 0; bool newerSession = false;             // serial bumps on each load
                     char migratedFromKey[25] {}; char migratedToKey[25] {}; };
}
```

- Drop `registry()` from 01 §2.2, since the free functions suffice.
- The `<UI>` child in 01 §9.1 becomes `charExpanded` + `scTab`.

### 15. `formatValue` returns one string, but the UI needs value, unit and spoken text [major]

**Where:**
- 01 §4.6 (`formatValue(..., char* out)`; its example "INPUT 30");
- 02 §5.3 (`ValueText{value, unit, sub, spoken}`);
- 02 §6.3 (value and kUnit on a shared baseline);
- 02 §8.9 (spoken strings such as "4 to 1" and "0.3 milliseconds");
- 01 §4.6 prints n/a as "—" (U+2014), but 02 §8.1 prints "–" (U+2013).

**Fix.** Add to 01 §4.6:

```cpp
struct FormattedValue { char value[24]; char unit[8]; char spoken[64]; char prefix; };  // prefix: 0, '~', '=', '('
void formatParts(const ParamView&, Pid, FormattedValue&) noexcept;   // formatValue() = prefix+value+' '+unit
```

- `value` never contains the slot label: "30", not "INPUT 30".
- UI and host text both use U+2212 for minus, and U+2013 for n/a.
- `parseHost` accepts both `-` and U+2212.

### 16. FunkGui's scope goes beyond the user's decision [major; needs the user's confirmation]

**Where:**
- 01 §9.2 (`FunkPresets`);
- 02 §0.1, §1.2 and §2.2 (HR `presets/` seeded into FunkGui);
- 03 §0.1 and §6 Q2 (the harness in FunkGui);
- the user decision ("seeded from HR `Source/gui` + shaders + bundled font"; "every library via FetchContent");
- the SQLite dependency: `find_package(SQLite3)` in 02 §1.2 and "system library" in 01 §9.2.

**What is wrong:** the presets data layer and the test harness are two unrequested extensions of a "GUI library". SQLite is a library that does not come through FetchContent.

**Fix:**
- Keep both extensions, because they avoid copying shared sources into FCompressor. List them as explicit user confirmations in the synthesised doc.
- Name the fallbacks:
  - presets: an FCompressor `Source/plugin/presets/`, re-implemented from HR's headers, not copied;
  - harness: stays in FunkGui either way.
- Declare macOS SDK libraries (SQLite3, like the Metal and CoreAudio frameworks) as outside the FetchContent rule, subject to user confirmation.
- Optionally, take the bgfx/bx/bimg licence texts from `${bgfx_SOURCE_DIR}` at configure time instead of copying them from HR (02 §1.1 `licences/`).

### 17. 03's link lines leave out FunkPresets [major]

**Where:**
- 03 §2.7:
  - the `FCompressor` row links `FunkGui::core/gpu` only in the GPU configuration, and never `FunkGui::presets`;
  - the `fcmp_probe_plugin` row has "SQLite3 if adopted" and no `FunkGui::presets`.
- 03 §2.8 (`funkgui_add_font` in the GPU configuration only);
- 02 §1.9 (links `FunkGui FunkGui::presets` and calls `funkgui_add_font` unconditionally).

**What is wrong:** the headless plugin still needs the preset store. As written, it fails to link.

**Fix:**
- `FCompressor` links `FunkGui::presets` in every configuration except DSP-only, and adds `FunkGui` (the GPU parts) only when not headless.
- `fcmp_probe_plugin` links `FunkGui::core FunkGui::presets FunkGui::harness`.
- SQLite comes in transitively.
- Put `funkgui_add_font` inside `if(NOT FCOMPRESSOR_HEADLESS)` in 02 §1.9, which is 03's rule.

### 18. Symbol rename table: the same thing has different names [major, because the headers are frozen]

| Where | Name used | Canonical |
|---|---|---|
| 01 §2.1; 03 §1.1, §1.2, §2.10 | `funkgui::EditorShell` | `funkgui::EditorHost` (02 §5.1) |
| 03 §3.6 | `funkgui::Recorder` | `funkgui::Canvas` + `PrimList` |
| 03 §1.1, §3.4 `dsp.analysis`, §3.6, §3.7; 02 §3.10 | `staticCurve`, `stepPreview`, `renderStepResponse` | `analysis::staticGain` / `staticGr`, `analysis::stepResponse` |
| 03 §1.1 | `CompressorEngine`, "arena" | `EngineHost`, `ModeEngine<T>` |
| 03 §3.1, §3.8, §4.6 | `ModeSpec` | `ModeDescriptor` |
| 03 §3.4 `dsp.registry`; §1.1 fixtures `<schema>` | `introducedInSchema`, "schema" | `introducedInStateVersion`, `stateVersion` |
| 03 §2.6 | `fcmp::simd::fma` | `fcdsp::simd::fma` |
| 03 §4.2 | `Resolved` | `ResolvedParam`, `Resolution` |
| 01 §2.1, §8.3, §8.4 | `FcmpDspProbe`, `FcmpProcProbe`, `FcmpUiProbe`, `FcmpDspFlags` | `fcmp_probe_dsp`, `fcmp_probe_plugin`, `fcmp_flags` |
| 03 §1.1 | `FcmpEditor` | `fcmp::ui::Editor` |
| 03 preamble | "Draft 1 owns FunkGui; Draft 2 owns the engine" | 02 owns FunkGui + UI; 01 owns the contracts |
| 02 §5.1 vs 01 §5.4 | `funkgui::HostConfig` vs `fcdsp::HostConfig` | rename FunkGui's to `funkgui::EditorConfig` |
| 02 §3.2 vs 01 §4.1 | `funkgui::Kind` vs `fcdsp::Kind` | rename FunkGui's to `funkgui::PrimKind` |

### 19. FunkGui presets, test names and deps script differ between 02 and 03 [minor]

**Where:**
- 02 §1.1 (CMake presets `dev` and `cpu`) and §1.10 (tag gate);
- 02 §3.11 (`font.probe`, `gallery.*`, `canvas.*`);
- 03 §1.2 (presets `agent`, `agent-gui`, `lead`; FunkGuiDeps.cmake is top-level only);
- 03 §2.5 (`fg.shader.hash`) and §4.9 (`fg.harness.self`, `fg.font.atlas`, `fg.recorder.roundtrip`, `fg.prefs`);
- 02 §1.3 (the top-level JUCE fetch has no `.deps` default and no SHA asserts).

**Fix:**
- Use 03's preset names. The tag gate is `agent` + `agent-gui` green.
- Prefix every FunkGui test with `fg.`, and take the union of both lists.
- `FunkGuiDeps.cmake`:
  - when consumed, it only checks versions (plus 02's guarded bgfx fallback);
  - when top-level, it uses the same `.deps` defaults and SHA asserts as `FcmpDeps.cmake`.

### 20. History internals: plural vs singular, missing lints, and a 16 vs 8 cap [minor]

**Where:**
- 01 §7 ("per-internal history lanes");
- 01 §4.3 (`InternalSpec::history`);
- 01 §8.3 lint ("≤ 16 internals");
- 02 §7.3 (READOUTS shows `internals[0..7]` only; CONTROL PATH has one `internal0` lane);
- 03 §3.6 (`ui.truth`).

**Fix:**
- 01 §7 becomes "the single history internal lane (`HistoryColumn::internal0`)".
- Add two registry lints:
  - at most one `InternalSpec` with `history == true`;
  - `internals.size() ≤ 8`, with `UiFrame` words 8–15 reserved.

### 21. The size of the UI-side history store [minor]

**Where:** 01 §6.3 reader rules (cites F §7.2: 5 ms × 4096) vs 02 §9.6 (1 ms × 20,480, 655 KB).

**Fix:** 01 cites 02 §9.6, which exactly rebuilds the columns.

### 22. The TRANSFER axis contract comment [minor]

**Where:**
- 01 §7 ("The UI draws y = x + gainDb");
- 02 §6.5 (`y = x + gainDb − preGainDb`) and §9.7 #4;
- 03 §3.6 `ui.curve`.

**Fix.** 01 §7's comment becomes: "TRANSFER draws `y = x + gainDb − preGainDb` (GR only). The net curve draws `y = x + netGainDb(gainDb, makeupEffDb, mix)`." 03's G2 uses the same formula.

### 23. `kFlagLatchWord` [minor]

**Where:**
- 01 §4.1 (the flag), §10.1 (`latch()`), §10.3 (Clean `tmode`) and §10.7 (Bus 25 and Brickwall `tmode`);
- 01 §12.1;
- 02 §0.7 and §6.4 (the words are fixed by the layout).

**Fix:**
- Delete `kFlagLatchWord` and `kit::latch`. The layout owns the words: AUTO = `automu`, EXT = `extkey`, LISTEN = `listen`.
- Define the AUTO word's states: hidden when `automu` is n/a; disabled with the reason when it is locked.

### 24. The step-label lint fails 01's own table [minor]

**Where:** 01 §8.3 (labels ≤ 6 glyphs) vs 01 §10.7 Mu 67 `LAT/VERT` (8 glyphs); 02 §8.3 and §9.7 #9.

**Fix:** the 6-glyph rule becomes a printed warning. G6's pair-fit rule is the gate.

### 25. The "short reason" is referenced but not defined [minor]

**Where:** 02 §8.1, locked sub-line ("short reason ≤ 18 chars, e.g. `CIRCUIT KNEE`"); 01 §4.1 `ParamSpec` has only `tag` and `reason`.

**Fix:**
- Add `const char* brief = nullptr; // ≤ 18 glyphs; locked/na sub-line; nullptr → tag` to `ParamSpec`.
- G6 lints its width.

### 26. Mu 67 ratio: n/a in 01, derived in 02 [minor]

**Where:** 01 §10.2 matrix and §10.7 (`na(0.95f, …)`); 02 §9.3 ("derived RATIO value (Mu 67)").

**Fix:**
- Change 01 to `prog(derived(&muRatioNominal, Pid::knee, "PROGRESSIVE", "RATIO RISES WITH LEVEL (REMOTE-CUTOFF TUBE)"))`.
- The slot then shows the live EFF ratio, per 02 §8.1's derived column.
- Update the matrix cell to "D progressive".

### 27. Brickwall applies makeup twice [minor]

**Where:**
- 01 §10.7 (`automu: locked(1, …, "ON")`, and `physical: makeupDb = ceiling − thrDb`);
- 01 §4.4 (`physicalDefault` maps `automu` to `kEngAutoMakeup`);
- 01 §5.4 d (the smoothed total = makeup + autoMakeup);
- 02 §6.4 P5 (the "AUTO +x" sub).

**What is wrong:** the output exceeds the ceiling by `r̂(0 dBFS)·k`.

**Fix:**
- Brickwall `automu` becomes `na(0, "OUTPUT IS SET BY THE CEILING")`, and the AUTO word is hidden.
- Or keep it locked, and have `brickwallPhysical` clear `kEngAutoMakeup`.

### 28. Axis label for `DetectorLaw::custom` [minor]

**Where:** 02 §6.5 labels (`PK`, `RMS`, `TP` only); 01 §10.6 (Opto) and §10.7 (Mu 67) are `custom`.

**Fix:** for `custom`, label the axis with the active `det` step label (`0 DB T4`, `0 DB TUBE`).

### 29. Oversampler latency specs [minor]

**Where:**
- 01 §5.6 (targets ≤ 4 and ≤ 64, frozen when the filters land);
- 03 §3.4 `dsp.os` ("Std 4, HQ 61");
- 03 §3.5 `proc.osref` ("latency equal to JUCE's integer latency");
- 02 §8.9 (help text hard-codes "4 samples").

**Fix:**
- `proc.osref` compares passband and image rejection only.
- The latency spec is "measured = `kOs[q].latency`".
- The help text is formatted from `kOs`.

### 30. The registry lint's "every function pointer is set" [minor]

**Where:** 03 §3.4 `dsp.registry` vs 01 §4.3 (`physical` may be nullptr).

**Fix:** the lint requires only the non-nullable pointers:
- `detectorLaw`, `attackSpec`, `releaseSpec`, `tailSeconds`;
- `ModeEntry::{construct, staticGr, scShapeDb, colourCurve}`.

### 31. Mode-agent ownership contradicts 01's checklist [minor]

**Where:**
- 01 §8.4:
  - step 3 (unit rows in the DSP probe);
  - step 5 (edits to the shared `plugin/FactoryPresets.cpp`, and `factoryBankRevision()`, which is never defined);
  - step 7 (`docs/modes/<key>.md`).
- 03 §4.6 (`Tools/probes/**` and `docs/**` are lead-only; no owner for `FactoryPresets`).

**Fix.** Add rows to 03 §4.6:
- `docs/modes/<key>.md` is owned by the Mode agent.
- `Tools/probes/dsp/stages/<Policy>.cpp` is owned by the stage-policy task named in its manifest.
- Factory presets per Mode go in `Source/plugin/presets/<key>.inc`, included in `Modes.def` order, so no shared file is edited.
- Define `uint32_t factoryBankRevision()` as the FNV‑1a hash of the concatenated `.inc` contents.

### 32. Hybrid naming inside 02, and hybrid state in 01 [minor]

**Where:** 02 §8.2 (`nEndCells`/`endCells`) vs §5.3 (`nEndLo/nEndHi/endLo/endHi/activeEnd`); 01 §4.4 never says which `SlotState` a hybrid resolves to.

**Fix:**
- Use §5.3's names throughout 02.
- In 01, a hybrid resolves to `live` inside `[lo, hi]` and to `stepped` when on a step (`step ≥ 0`).

### 33. FunkGuiCore's links are incomplete [minor]

**Where:** 02 §1.2 (Core links only `juce_gui_basics` and `juce_data_structures`); §1.1 and §5.2 (`JuceParamPort` in Core wraps `juce::RangedAudioParameter`).

**Fix:** add `juce::juce_audio_processors` to FunkGuiCore's INTERFACE links.

### 34. Fixed size and the "full-panel" reading [minor]

**Where:** 02 §6.1 ("`setSize` exactly once"); §5.1 (`EditorHost`); §7.1 and §11 Q1 (the chrome stays on the Characteristics screen).

**Fix:**
- `EditorHost` calls `setResizable(false, false)` before its single `setSize`.
- The synthesised doc asks the user to confirm that "full-panel" may keep the header, display row and footer, which is 02's reading.

### 35. Mode-specific hard-coding where the descriptor already has the data [minor]

**Where:**
- 02 §6.6 (the Brickwall lookahead hint names the Mode);
- 01 §4.3 (`wantsLookahead`, which nothing reads);
- 01 §10.5 ("Needs STD or HQ (D F14)", with ECO behaviour undefined);
- 01 §7 (`StepStimulus::hiDbOverThr`: which threshold is not stated).

**Fix:**
- Key the hint on `desc.wantsLookahead && budget == off`.
- State that at ECO, FET colour runs ADAA‑1 at the base rate (01 §5.6) and nothing is refused. Delete "Needs".
- State that the `StepStimulus` levels are relative to `eng.thrDb`, in the detector domain, because the stimulus is injected after the SC filters.

---

## Checked and consistent (no change needed)

- **Snap policy.** All three docs: raw values are the truth, Mode snapping happens on read, and a Mode switch writes only `mode` (01 §4.5, 02 §0.10 and §8.4, 03 §3.10 #1 with spec rows).
- **Core policies.** One answer in all three docs for each of these:
  - GR is positive in dB;
  - 128 slots;
  - latency = f(quality, labudget);
  - JUCE-free `fcdsp` with its own oversampler.
- **Parameter IDs.** The parameter IDs and all 29 host parameters are placed identically in 01 §3.1 and 02 §6.4: 21 slots, plus AUTO, EXT and LISTEN, plus the chrome.
- **`UiFrame` and `HistoryColumn`.** `UiFrame` has 72 words, `HistoryColumn` is 32 B, and `EngineParams` is 116 B. The arithmetic checks.
- **Pins and FunkGui naming.** These match the user's decisions:
  - FetchContent pins: JUCE 8.0.4 and bgfx.cmake v1.153.9385‑561;
  - FunkGui naming, its repository path and the `FETCHCONTENT_SOURCE_DIR_FUNKGUI` override;
  - Funk, Fcmp, com.funk.fcompressor, AU/VST3/Standalone.
- **Remaining user decisions.** The following are honoured in all three docs:
  - HR is read-only;
  - only the lead blesses;
  - only the lead commits;
  - FCompressor agents work in worktrees;
  - FunkGui agents run `git worktree add`;
  - the band plus a full screen, at 960×640.
- **Brief coverage.** These requirements from the brief are covered:
  - stepped Modes (Bus G 2/4/10, 01 §10.4; UI 02 §8.1; probe `dsp.quant`);
  - one shared UI for every Mode (02 §6.4);
  - the Characteristics content: detector/envelope, step response, SC filter, colour (02 §7.3).

---

## Top 5 changes the synthesiser must make

1. **One tree, one build and verify vocabulary** (#1, #4, #5, #18).
   - Use 01's `Source/fcdsp/` tree, with `ProcessorFacade.h` in `plugin/`.
   - Use 03's targets, test registration, golden root and exit codes.
   - Mode sources and includes are generated from `Modes.def`, with no GLOB.
   - Apply the rename table everywhere.
2. **Make the specs agree with the DSP decisions** (#2, #6, #29).
   - mix‑0 and below-threshold nulls are compared against an `Oversampler` round trip.
   - The test tap is always compiled (`EngineHost::setTap`, `TestTap` defined).
   - `osref` does not dictate latency.
3. **Fix the FunkGui build and delivery path** (#3, #12, #13, #17).
   - Guard shaderc on `FUNKGUI_SHADERC`.
   - Use one tag sequence: raw snapshot, then `v0.1.0` (renames + CMake + harness), then `v0.2.0`, and so on.
   - FCompressor consumes only tags.
   - Use one sprint table with ≤ 3 agents per row and one frozen-interface list.
   - Link FunkPresets in headless builds.
4. **Close the 01 → 02 data-contract gaps** (#7, #8, #9, #14, #15).
   - Live curves use `resolve()` plus `overlaySmoothed(UiFrame)`.
   - Add `LookaheadBudget` to `RawParams`, so the resolver, UI and host text agree.
   - Threshold handles map through `inputThresholdDb`, with a slope‑1 lint.
   - Define `UiState` and `StateNotice`.
   - Add `formatParts` (value, unit, spoken).
5. **Define the product UI surface the probes drive** (#10, #11).
   - `fcmp::ui::{Screen, Overlay, ViewSpec, views(), PanelOptions, Panel::setView}`.
   - One `FCMP_UI_VIEW` variable.
   - A synchronous preview in probes.
   - Correct the view names in 03 §3.6.

Also carry #16 into the user questions: presets and the harness inside FunkGui, and SQLite as a system library outside FetchContent.
