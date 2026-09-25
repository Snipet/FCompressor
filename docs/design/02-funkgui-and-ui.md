# 02 — FunkGui library and FCompressor UI (design appendix B)

Status: **design appendix, synthesised 2026-09-22 from Draft 2 and critiques K1 (consistency), K2 (risk) and K3
(parallel build). No code exists yet.** The top-level document is `docs/ARCHITECTURE.md`; decisions and rejected
options are in `docs/DECISIONS.md`. It is binding once the lead accepts it.

Evidence base. Research reports are cited as `A §x`, and so on:
- `docs/research/A-gui-stack.md` (A): HR's GUI stack, the SdfCanvas API and the sharing seams.
- `B-plugin-build.md` (B): CMake §4, parameter model §7.4, telemetry §7.5.
- `C-verification.md` (C): the headless-GUI gap §3, the Mode contract §5.1, GUI probes §5.9, agent rules §6.
- `D-mode-catalogue.md` (D): the parameter superset §4, the Mode × Parameter matrix §5, names and groups §6.
- `E-dsp-engine.md` (E): the parameter rules §4, analysis hooks §6, `UiFrame` and history §7.
- `F-ui-ux.md` (F): the whole UX proposal, used here as the starting point.

HardwareReverb (HR) = `/Users/seanfunk/audio/plugins/HardwareReverb`. It is read-only. HR line numbers are approximate
because someone is editing HR.

Sibling appendices: `docs/design/01-core-contracts.md` (01) and `03-build-verify-process.md` (03). 01 owns parameters,
the Mode descriptor, the engine and telemetry. 03 owns build, pins, verification and process (including the one sprint
plan, 03 §4.9). This appendix owns FunkGui's API and the editor. The Draft 2 ↔ 01/03 mismatches and how each was
resolved are listed in §9.7.

Terms:
- "Slot": one fixed control position on the panel.
- "Mode": a compressor type, described by a `ModeDescriptor` (01 §4.3).
- "track": the drawn 1 px rule of a slot.
- "host01": a host-normalised parameter value.

---

## 0. Decisions on one screen

1. **FunkGui is a set of CMake INTERFACE libraries whose sources compile inside each consumer target.** It is not a JUCE
   module and not a STATIC library.
   - `FunkGuiCore` (`FunkGui::core`): CPU only, no bgfx.
   - `FunkGuiGpu` (`FunkGui::gpu`): the bgfx/Metal sink, EditorHost, FramePump and native views.
   - `FunkGui`: the umbrella target.
   - `FunkGuiFonts`: a static binary-data library.
   - `FunkGuiHarness` (`FunkGui::harness`): header-only.
   - `FunkPresets` (`FunkGui::presets`): the preset data layer. 01 §9.2 asked for it and this appendix accepts it;
     because it (and the harness) extends the user's "GUI library" decision, both are listed for the user's
     confirmation (`DECISIONS.md` Q2, Q3).

   **The consumer provides JUCE.** FunkGui fetches bgfx only `if(NOT TARGET bgfx)`, so a consumer can share one bgfx. §1.
2. **The seed is a snapshot of HR, made by the lead (G0).** Commit 1 copies HR `Source/gui` (19 files), the 3 shaders,
   the font and `Harness.h` byte for byte, with `SEED.tsv`; commit 2 applies the sed renames **keeping HR's file stems**
   (`SdfCanvas.h` stays `SdfCanvas.h`); commit 3 adds a minimal CMake exposing `FunkGui::harness`. That is tag
   **`v0.0.1`**, the bootstrap pin. Task G1 adds the real targets and Harness v2 → **`v0.1.0`**; later minors carry the
   generalisation (§2, 03 §4.9; K1 #12, K3 #5).
3. **The canvas is split into a CPU recorder and a GPU sink.** `funkgui::Canvas` records a `PrimList`: 84-byte `Prim`s
   carrying a semantic `tag` and a `flags` word whose bit 0 is `live`. `BgfxSink` expands the list into HR's exact
   64-byte vertex stream.
   - Layout, input and drawing live in a `funkgui::Panel`. It is not a Component, uses no GPU and is advanced by
     `tick(dt)`.
   - `HeadlessHost` drives a Panel inside a plain console tool.
   - Dump format v2 is a strict superset of HR's. §3.
4. **New primitives:**
   - `KIND_AREA = 3`, with edge-stroke flags.
   - `polyline`, `disc`, `areaStrip`, `dotted`, and `axis` records.
   - Transient vertex buffer **32 MiB**. An overflow is counted and made visible.
   - **10 new glyphs** (µ − … ≤ ≥ ≈ Δ — • ←), listed once in `Glyphs.def`. §4.
5. **Widgets lifted from `BgfxEditor`** (re-implemented from the pattern; HR is never edited):
   - `EditorHost` and `ParamPort` + `GestureController`.
   - `ValueModel` with 5 states, rendered by `RuleSlider`.
   - `SegmentedSelector`, `LatchToggle`, `AttachedWord`, `ThemeCells`.
   - `A11yItem` + `A11yBridge`.
   - `DwellSelector` and `ease`.
   - Text fit, `LineEdit`, `MenuLook`, `HintLine`, `LiveFeed` and `SoftRaster`.

   §5.
6. **FCompressor's window is 960×640 and fixed.** The header (y 0–60), the display row (64–120) and the footer (604–616)
   never move. The **middle region (y 124–600) swaps** between two screens:
   - **PANEL**: the always-visible band plus 21 slots in 3 rows of 7.
   - **CHARACTERISTICS**: 7 plots.

   The `CHARACTERISTICS` latch in the display row switches between them. §6, §7.
7. **All 29 host parameters of 01 §3.1 fit, with no compound cells.** This answers 01 §12.1.
   - The 22 Mode-filtered parameters take 21 slots. `automu` is the `AUTO` word on MAKEUP.
   - `tmode` always has its own slot, TIME MODE, directly under RELEASE (FET 76 relabels it `GR`: ON/OFF). The layout,
     not a descriptor flag, owns the words (01's `kFlagLatchWord` is deleted; K1 #23).
   - The 7 globals:
     - `mode`: the header latch.
     - `delta`, `bypass`: display-row latches.
     - `quality`, `labudget`: display-row cells.
     - `extkey`: an `EXT` word on DETECT.
     - `listen`: a `LISTEN` word on SC HPF.
8. **The band is fixed: HISTORY + TRANSFER + METERS, with no view switching.** F's TIME, SIDECHAIN, INTERNALS and COLOUR
   views move to the Characteristics screen, as the user decided. All three band regions share one dB→y map: **4 px/dB at
   the default 48 dB scale**.
9. **Five control states: continuous, stepped, locked, derived, n/a.** They match 01's `SlotState` (`live`, `stepped`,
   `locked`, `derived`, `na`). 01's `hybrid` kind renders as continuous plus end cells (§8.1). Rename (`label` +
   `DisplayMap`) and extension are separate markers on top of any state.
   - **The track always increases the *displayed* quantity to the right.** With `DisplayMap::invert` (INPUT or PEAK RED.
     over `thr`), the host lane moves the other way. This is F §3.2's invariant, which 01 adopted.
10. **Snap policy: E §4.2 beats B §7.4.2.**
    - The raw host value is the truth, and the Mode snaps it on read.
    - The UI writes canonical detent values.
    - **A Mode switch writes only `mode`.**
11. **Telemetry is exactly 01 §6:** the 72-word `UiFrame` and the 32 B `HistoryColumn`. GR is **positive =
    attenuation**. Analysis is exactly 01 §7. Live curves are drawn from `resolve()` of the current raw values with the
    smoothed fields overlaid by `fcdsp::overlaySmoothed(frame, eng)` (01 §6.2), because `UiFrame` carries no `m[8]`,
    `topo` or `flags` (K1 #7). The Characteristics screen is designed to 01's single history internal (§7.3, §9).
12. **Stepped detent labels use an adjacent-pair fit rule.** If the labels fail it, the slot shows ticks only. Probe G6
    lints this per Mode (§8.3).
13. **The Mode browser is an overlay at `{40,64,880,286}`:** 8 group columns × 12 rows = 96 Modes before it pages
    horizontally (§8.6).
14. **FunkGui versioning is `v0.MINOR.PATCH`.**
    - MINOR = any change a consumer's dump can see.
    - FCompressor pins an exact tag and **consumes only tags**: a FunkGui feature an FCompressor task needs in sprint N
      is tagged in sprint N−1 or earlier (the pipelining rule, 03 §4.5).
    - Only the lead moves the pin, at a sprint boundary.
15. **The FCompressor `Panel` is a fixed composition of sub-views** (Header, DisplayRow, SlotGrid, Band, CharScreen,
    ModeBrowser, PresetStrip, PresetBrowser, Footer), each in its own files, with `Layout.h` and `Tags.h` written complete
    and frozen by the first UI task (K3 #15). Probes drive it through `fcmp::ui::views()` and `Panel::setView()`
    (Part 2 intro).

### 0.1 Conflicts between the research reports, and how this draft resolves them

| Conflict | Choice | Why |
|---|---|---|
| B §7.4.2 snaps raw values when the Mode changes. E §4.2.1 keeps raw values and snaps on read. F §3.2 snaps stepped params only | **E**, as 01 §4.5 also decides | (a) A→B→A restores exactly. (b) One Mode change is one host gesture (one host undo step). (c) A UI click on MODE never writes up to 22 other automation lanes, which B's approach does in latch or write mode. (d) C's D3 probe stays deterministic. The cost: a host lane can sit between detents while its text shows the snapped label. The UI always draws the snapped state |
| F §3.1 has 15 slots. D §4 has 24. 01 §3.1 has 22 Mode parameters + 7 globals | **01** (21 slots, 3×7, §6.4) | 01 owns the superset (`in`→remapped `thr` + `drive`; no `topo`, `lshape` or `out`) |
| F §4.1 says the band is enough and makes EXPAND optional. The user wants a band **plus** a full-panel screen | **User** | The band has no views. The deep views are on the full screen (§7). HR rule 10 ("no disclosure") is waived by the user for this one screen only |
| F §3.2 / §10.3: may a remap invert the track (INPUT up = threshold down)? | **Yes: the track follows the displayed quantity** | 01 §4.1 `DisplayMap::invert`. Moving right always increases what the label names. Dial numbers from `DisplayMap` are the value text |
| F §7.1 has GR ≤ 0. E §2.1 has `r ≥ 0` | **E** (01 §1.8) | E is the producer. Formatters print "−4.2 DB" |
| B §7.5 uses history points. F §7.2 uses 32 B points. E §7 uses min/max columns | **01 §6.3** (32 B min/max, 4096 deep) | Min/max columns never lose a 1 ms spike |
| E §6.2 draws the live dot at `x − r + M` (post-makeup). F §4.4 plots the curve pre-makeup | **F** | Unity and the threshold handle stay makeup-independent, and needle length equals GR. Makeup and mix get their own "net" curve |
| A §6.3 offers a JUCE module or an INTERFACE library | **INTERFACE** (§1.3) | |
| A §6.4 proposes the namespace `sdfui` | **`funkgui`** (user decision) | |
| A §6.4.2: preferences folder per plugin or shared | **Per product** (`FCompressor`) | HR keeps `HardwareReverb` when it migrates. No cross-product preference coupling (§1.8) |
| D §5.7 / E §4.2.2 composite knobs (release + `tmode`=Auto on one rule). F §3.4 uses an AUTO latch | **01 §10.2**: a hardware AUTO is a `rel` step. `tmode` covers separate switches and digital AUTO, and gets its own slot under RELEASE | One control = one parameter keeps automation, undo and a11y trivially right |
| 03 §1.2 CMake spellings (`FunkGui::core/gpu/harness`, `funkgui_configure_product`, `funkgui_compile_shaders`, `funkgui_add_font`, `SEED.tsv`, `test/`, worktrees in `FunkGui.wt/`) | **Adopted** | The spellings cost nothing, and 03's pin and override machinery wraps them. The C++ names stay this draft's (`EditorHost`, `Canvas`) |

---

# PART 1 — FunkGui (`/Users/seanfunk/audio/libraries/FunkGui`)

## 1. Repository, CMake, versioning

### 1.1 Layout

```
/Users/seanfunk/audio/libraries/FunkGui/            git repo; branch main; annotated tags v0.MINOR.PATCH
├── CMakeLists.txt                  project(FunkGui VERSION 0.1.0 LANGUAGES C CXX); VERSION changes only in tagging commits
├── CMakePresets.json               "agent" (headless: FUNKGUI_WITH_BGFX=OFF), "agent-gui" (GPU), "lead" (03 §1.2 names)
├── README.md  CLAUDE.md  CHANGELOG.md  SEED.tsv (provenance, §2.1)  LICENSE (GPL-3.0, as HR)
├── cmake/
│   ├── FunkGuiDeps.cmake           consumed: version checks only (+ the guarded bgfx fallback, §1.4);
│   │                               top-level: the same ~/audio/.deps defaults and SHA asserts as FcmpDeps.cmake (K1 #19)
│   └── FunkGuiTargets.cmake        per-directory source globs (src/<module>/**, CONFIGURE_DEPENDS, K3 #2);
│                                   funkgui_configure_product(), funkgui_compile_shaders(), funkgui_add_font(),
│                                   funkgui_add_tool() (03 §1.2 spellings); custom target FunkGuiShaders
├── include/funkgui/
│   ├── core/    Col.h Theme.h TypeScale.h TagPalette.h Geometry.h Ease.h Config.h Env.h Format.h
│   ├── text/    TextStyle.h FontAtlasSdf.h FontService.h BundledFont.h Glyphs.def TextFit.h LineEdit.h
│   ├── canvas/  Prim.h PrimList.h Canvas.h Tags.h Axis.h Fingerprint.h SoftRaster.h
│   │            (v0.0.1 keeps HR's SdfCanvas.h stem; the new Canvas.h name is free, and the snapshot is retired by G3)
│   ├── panel/   Input.h Panel.h HostServices.h CaptureConfig.h HeadlessHost.h
│   ├── params/  ParamPort.h GestureController.h JuceParamPort.h
│   ├── widgets/ ValueModel.h RuleSlider.h SegmentedSelector.h LatchToggle.h AttachedWord.h
│   │            ThemeCells.h DwellSelector.h HintLine.h FocusRing.h
│   ├── a11y/    A11yItem.h
│   ├── live/    LiveFeed.h
│   ├── prefs/   UiPreferences.h
│   ├── juce/    MenuLook.h
│   ├── gpu/     BgfxContext.h BgfxSink.h FramePump.h DisplayLink.h NativeSurface.h EditorHost.h A11yBridge.h
│   ├── presets/ PresetTypes.h PresetManager.h PresetStore.h PresetFile.h   (FunkPresets; 01 §9.2 contract)
│   └── test/    Harness.h                    (Harness v2, C §5.11; header-only, no JUCE)
├── src/         core/ text/ canvas/ panel/ params/ widgets/ a11y/ prefs/ juce/ presets/  (*.cpp)
│                canvas/Canvas.cpp (HR primitives, G3) and canvas/CanvasShapes.cpp (new primitives, G4) are separate
│                gpu/ (*.cpp, DisplayLink.mm, NativeSurface.mm)
├── shaders/     vs_ui.sc  fs_ui.sc  varying.def.sc
├── fonts/       JetBrainsMono-Regular-subset.ttf  JetBrainsMono-LICENSE.txt  upstream/JetBrainsMono-Regular.ttf
│                (no licences/ directory: the bgfx/bx/bimg/bgfx.cmake licence texts are taken from ${bgfx_SOURCE_DIR}
│                 at configure time by funkgui_add_font, never copied from HR; K1 #16)
├── tools/       FontProbe.cpp AtlasDump.cpp FrameRender.cpp PrefsCheck.cpp GalleryProbe.cpp GalleryApp/ (Standalone, G7)
│                subset-font.sh  capture-frame.sh  golden.py (golden format v2, 03)  verify.sh (FunkGui DoD gate, 03)
└── test/
    ├── CMakeLists.txt              CTest registration (top-level builds only); every test is named fg.*
    ├── gallery/GalleryPanel.{h,cpp}  hosts self-registering sections, one file per widget:
    │           gallery/<Widget>Gallery.cpp (RuleSliderGallery.cpp, WordGallery.cpp, …; K3 #16)
    └── golden/{base,arm64,x86_64}/ fg.font.probe.txt fg.prefs.check.txt fg.gallery.*.txt … (03 §3.3 layout)
```

**What goes where.** Anything that `#include`s bgfx or ObjC lives under `gpu/`. Everything else compiles with
`FUNKGUI_WITH_BGFX=OFF`. `juce/MenuLook.h` and `params/JuceParamPort.h` need JUCE modules but no GPU, so they belong to
Core.

### 1.2 Targets

| Target | Kind | Contents | Links (INTERFACE) |
|---|---|---|---|
| `FunkGuiFonts` | STATIC (`juce_add_binary_data`) | `funkguifonts::JetBrainsMonoRegularsubset_ttf[Size]`, licence text | – |
| `FunkGuiHarness` (alias `FunkGui::harness`) | INTERFACE, header-only, JUCE-free; available even in DSP-only consumers (03 §1.2) | `funkgui/test/Harness.h` | – |
| `FunkGuiCore` (alias `FunkGui::core`) | INTERFACE + INTERFACE sources | core, text, canvas (recorder, dump, fingerprint, SoftRaster), panel, params, widgets, a11y, live, prefs, juce | `juce::juce_gui_basics juce::juce_data_structures juce::juce_audio_processors FunkGuiFonts` (`JuceParamPort` wraps `juce::RangedAudioParameter`, K1 #33); `cxx_std_20` |
| `FunkGuiGpu` (alias `FunkGui::gpu`) | INTERFACE + INTERFACE sources; exists only if `FUNKGUI_WITH_BGFX` | BgfxContext, BgfxSink, FramePump, DisplayLink.mm, NativeSurface.mm, EditorHost, A11yBridge | `FunkGuiCore bgfx bx bimg juce::juce_audio_processors`; include dir `${FUNKGUI_GENERATED_DIR}`; definition `FUNKGUI_HAS_BGFX=1` |
| `FunkPresets` (alias `FunkGui::presets`) | INTERFACE + INTERFACE sources; if `FUNKGUI_WITH_PRESETS` | HR `presets/` PresetTypes, PresetManager, PresetStore (SQLite, WAL, in-memory fallback), PresetFile, generalised with 01 §9.2's `Attribute`, `ProductConfig` and `PresetHooks`. Namespace `funkgui::presets`. **It does not depend on the GUI targets.** Written by task G8, scheduled last; user confirmation pending (`DECISIONS.md` Q2) | `juce::juce_audio_processors juce::juce_data_structures SQLite3::SQLite3` (`find_package(SQLite3 REQUIRED)`: the macOS SDK library, as HR; outside the FetchContent rule like Metal and CoreAudio) |
| `FunkGui` (umbrella) | INTERFACE | – | `FunkGuiCore` (+ `FunkGuiGpu` when enabled) |
| `FunkGuiShaders` | custom target | `${FUNKGUI_GENERATED_DIR}/funkgui/shaders/{vs_ui,fs_ui}.mtl.h` | `FUNKGUI_SHADERC` if set, else the `shaderc` target |
| Tools (top-level, or when `FUNKGUI_BUILD_TOOLS`, which FCompressor's `FcmpDeps.cmake` sets ON) | `juce_add_console_app`, `EXCLUDE_FROM_ALL` | `funkgui_framerender` (03's name), `FunkGuiFontProbe`, `FunkGuiAtlasDump`, `FunkGuiPrefsCheck`, `FunkGuiGalleryProbe`, `FunkGuiGalleryApp` (Standalone, GPU) | core + harness |

There is no `install()`, `export()` or package config. FetchContent is the only way to consume FunkGui.

### 1.3 How FunkGui gets JUCE: the consumer provides it

**Decision.** FunkGui uses INTERFACE libraries with INTERFACE sources, and it requires the JUCE targets to exist already.
- With `FUNKGUI_HARNESS_ONLY=ON` (03's DSP-only builds), FunkGui defines only `FunkGui::harness` and never looks for
  JUCE.
- As a sub-project, FunkGui never fetches JUCE. `cmake/FunkGuiDeps.cmake` checks:
  ```cmake
  if(NOT TARGET juce::juce_gui_basics OR NOT COMMAND juce_add_binary_data)
    message(FATAL_ERROR "FunkGui: call FetchContent_MakeAvailable(JUCE) before FunkGui")
  endif()
  FetchContent_GetProperties(JUCE SOURCE_DIR _fg_juce_src)
  file(STRINGS "${_fg_juce_src}/modules/juce_core/system/juce_StandardHeader.h" _fg_v REGEX "#define JUCE_(MAJOR|MINOR)_VERSION|#define JUCE_BUILDNUMBER")
  # parse to 8.0.4; FATAL_ERROR unless it is 8.0.4 or FUNKGUI_ALLOW_OTHER_JUCE=ON
  ```
- When FunkGui is the top-level project (`PROJECT_IS_TOP_LEVEL`, for its own tools and tests), it fetches JUCE itself:
  `FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)`,
  with the same `~/audio/.deps` defaults for `FETCHCONTENT_SOURCE_DIR_{JUCE,BGFX}` and the same SHA asserts as
  FCompressor's `FcmpDeps.cmake` (03 §2.3; K1 #19).

**Why not a JUCE module (`juce_add_module`).** A module forces a unity layout (`funkgui.cpp`/`funkgui.mm` including
every TU, plus a module header with a declaration block). It can only declare JUCE-module dependencies, so bgfx, the
generated shader headers and the font library would each need a side channel. An INTERFACE library with INTERFACE
sources has the property A §6.3 actually asks for: the sources compile inside each consumer with that consumer's JUCE
config flags, so there is no ODR mismatch. It keeps the normal `include/`/`src/` tree, and `.mm` files compile as in
HR.

**Why not STATIC.** A static library would compile `juce_*` includes under FunkGui's own flags, not the plugin's
(A §6.3).

**Cost.** Each consumer target compiles about 5–6 kLOC of FunkGui. FCompressor has three such targets: the plugin,
`fcmp_probe_plugin` (C's `FcmpUiProbe`) and `funkgui_framerender`. That is seconds of compile time.

### 1.4 How FunkGui gets bgfx: a guarded fetch, shareable with the consumer

```cmake
# cmake/FunkGuiDeps.cmake (runs only when FUNKGUI_WITH_BGFX)
if(NOT TARGET bgfx)
  # HR CMakeLists.txt:229-235 settings, as NORMAL variables (CMP0077): a CACHE … FORCE write would outlive this block in
  # the consumer's cache (K2 #17).
  set(BGFX_BUILD_TOOLS ON)
  set(BGFX_BUILD_TOOLS_SHADER ON)
  set(BGFX_BUILD_TOOLS_TEXTURE OFF)
  set(BGFX_BUILD_TOOLS_GEOMETRY OFF)
  set(BGFX_BUILD_TOOLS_BIN2C OFF)
  set(BGFX_BUILD_EXAMPLES OFF)
  set(BGFX_INSTALL OFF)
  FetchContent_Declare(bgfx GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
                            GIT_TAG v1.153.9385-561 GIT_SHALLOW TRUE)   # no-op if the consumer declared bgfx first
  FetchContent_MakeAvailable(bgfx)
  foreach(t bgfx bx bimg)                                               # hidden symbols (K2 #26f)
    set_target_properties(${t} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  endforeach()
endif()
# shaderc: a prebuilt binary (FUNKGUI_SHADERC, 03 §2.5) OR the bgfx.cmake target. Draft 2 checked the target
# unconditionally, which failed every GPU configure that uses the prebuilt shaderc (K1 #3, K2 #17).
if(FUNKGUI_SHADERC)
  if(NOT EXISTS "${FUNKGUI_SHADERC}")
    message(FATAL_ERROR "FunkGui: FUNKGUI_SHADERC=${FUNKGUI_SHADERC} does not exist")
  endif()
elseif(NOT TARGET shaderc)
  message(FATAL_ERROR "FunkGui: no shaderc (set FUNKGUI_SHADERC or build bgfx with BGFX_BUILD_TOOLS_SHADER=ON)")
endif()
FetchContent_GetProperties(bgfx SOURCE_DIR _fg_bgfx_src)
file(STRINGS "${_fg_bgfx_src}/bgfx/include/bgfx/defines.h" _fg_api REGEX "define BGFX_API_VERSION UINT32_C\\(153\\)")
if(NOT _fg_api)
  message(FATAL_ERROR "FunkGui: bgfx API 153 required (bgfx.cmake v1.153.9385-561)")
endif()
```

- **Shared bgfx.** FetchContent is first-declaration-wins. If the consumer declares `bgfx` before
  `FetchContent_MakeAvailable(FunkGui)`, FunkGui's `Declare` is ignored and the consumer's pin is used. If the consumer
  already called `MakeAvailable(bgfx)`, `TARGET bgfx` exists and FunkGui skips the whole block. Either way there is one
  bgfx.
- **FCompressor always provides bgfx first.** 03 §2.3 `FcmpDeps.cmake` runs JUCE → bgfx (with its SHA asserts and
  `fcmp_select_shaderc`) → FunkGui. For FCompressor the fallback fetch is therefore a no-op, and 03's statement "never
  runs FetchContent when consumed" holds in practice.
  - The guarded fallback stays for consumers without their own deps script, such as FunkGui's gallery-only
    experiments. The task asks for it.
- The pin check exists because `FETCHCONTENT_SOURCE_DIR_BGFX` bypasses `GIT_TAG` (B §7.2). `BGFX_API_VERSION
  UINT32_C(153)` was verified in the v1.153.9385-561 sources.
- **`FUNKGUI_SHADERC`.** If set to an existing `shaderc` binary (03 §2.5), the shader rules use it and `shaderc` is not
  built. Building shaderc cost 183 s in B §7.6's measurement.
- `bgfx::Init::limits.maxTransientVbSize` exists in that version (`bgfx.h:573`). §4.5 uses it.

### 1.5 Shader compilation and embedding

```cmake
# cmake/FunkGuiTargets.cmake  (HR's hrvb_compile_shader, CMakeLists.txt:243-277, generalised)
set(FUNKGUI_GENERATED_DIR ${FunkGui_BINARY_DIR}/generated CACHE INTERNAL "")
set(_fg_out ${FUNKGUI_GENERATED_DIR}/funkgui/shaders)
file(MAKE_DIRECTORY ${_fg_out})
if(FUNKGUI_SHADERC)
  set(_fg_shaderc "${FUNKGUI_SHADERC}")
  set(_fg_shaderc_dep "")
else()
  set(_fg_shaderc "$<TARGET_FILE:shaderc>")
  set(_fg_shaderc_dep shaderc)
endif()
function(funkgui_compile_shader NAME TYPE)
  add_custom_command(OUTPUT ${_fg_out}/${NAME}.mtl.h
    COMMAND ${_fg_shaderc} -f ${FunkGui_SOURCE_DIR}/shaders/${NAME}.sc -o ${_fg_out}/${NAME}.mtl.h
            --type ${TYPE} --platform osx -p metal
            --varyingdef ${FunkGui_SOURCE_DIR}/shaders/varying.def.sc
            -i ${_fg_bgfx_src}/bgfx/src --bin2c ${NAME}_mtl
    DEPENDS ${_fg_shaderc_dep} ${FunkGui_SOURCE_DIR}/shaders/${NAME}.sc ${FunkGui_SOURCE_DIR}/shaders/varying.def.sc
    COMMENT "FunkGui shaderc: ${NAME}.sc -> metal")
endfunction()
funkgui_compile_shader(vs_ui vertex)
funkgui_compile_shader(fs_ui fragment)
add_custom_target(FunkGuiShaders DEPENDS ${_fg_out}/vs_ui.mtl.h ${_fg_out}/fs_ui.mtl.h)
```

- The `COMMAND` is `${FUNKGUI_SHADERC}` when set, else `$<TARGET_FILE:shaderc>`. `DEPENDS shaderc` applies only in
  the second case. `fg.shader.hash` fingerprints the generated headers, so a shaderc built from the wrong bgfx fails a
  test, not the renderer (03 §2.5).
- Only `src/gpu/BgfxContext.cpp` includes the headers, as `<funkgui/shaders/vs_ui.mtl.h>`.
- Headless builds never touch shaders.
- `funkgui_compile_shaders(<tgt>)` = `add_dependencies(<tgt> FunkGuiShaders)`. An INTERFACE library cannot carry a
  build-order dependency, so each GPU consumer calls it.

### 1.6 Bundled font and licences

- The font data comes from:
  ```cmake
  juce_add_binary_data(FunkGuiFonts HEADER_NAME FunkGuiFonts.h NAMESPACE funkguifonts
      SOURCES fonts/JetBrainsMono-Regular-subset.ttf fonts/JetBrainsMono-LICENSE.txt)
  ```
  `BundledFont.h` is the single place that names the face (HR's `BundledFont.h` contract). The symbol stays
  `JetBrainsMonoRegularsubset_ttf`.
- `funkgui_add_font(<juce plugin target>)` adds `fonts/JetBrainsMono-LICENSE.txt` → `Resources/` and the bgfx, bx, bimg
  and bgfx.cmake licence texts, taken from `${bgfx_SOURCE_DIR}` at configure time (`bgfx/LICENSE`, `bx/LICENSE`,
  `bimg/LICENSE`, `LICENSE`, copied into the build tree as `<name>-LICENSE.txt`), → `Resources/licences/`, as
  `MACOSX_PACKAGE_LOCATION` sources. It does this for each of `<target>_AU`, `<target>_VST3` and `<target>_Standalone`
  that exists, and only in GPU configurations (a headless plugin has no FunkGui GPU code and no bundled font).
  - They are resources, **never** a POST_BUILD copy. A copy after JUCE's ad-hoc codesign broke the seal (B §4, HR
    `CMakeLists.txt:320-332`).
  - OFL 1.1 clause 2 asks for a stand-alone licence file.

### 1.7 Options

| Option / cache var | Default | Effect |
|---|---|---|
| `FUNKGUI_WITH_BGFX` | `ON` | `OFF`: no bgfx fetch, no shaders, no `FunkGuiGpu`. `FunkGui` = `FunkGuiCore`. This is the agent/headless configuration (C §6.2, HR's `build-dsp` equivalent) |
| `FUNKGUI_BUILD_TOOLS` | `${PROJECT_IS_TOP_LEVEL}` | Tool targets (always `EXCLUDE_FROM_ALL`). FCompressor sets it ON (03 §2.3), because `verify-gui-live` and the DoD need `funkgui_framerender` (K2 #26d) |
| `FUNKGUI_BUILD_TESTS` | `${PROJECT_IS_TOP_LEVEL}` | CTest suite (§3.11) |
| `FUNKGUI_TRANSIENT_VB_MIB` | `32` | Passed as `FUNKGUI_TRANSIENT_VB_BYTES=(32<<20)` to `BgfxContext::initBackend` |
| `FUNKGUI_WITH_PRESETS` | `ON` | `FunkPresets` (needs `SQLite3`) |
| `FUNKGUI_HARNESS_ONLY` | `OFF` | Only `FunkGui::harness` is defined, and JUCE is not required. For 03's `FCOMPRESSOR_DSP_ONLY` |
| `FUNKGUI_SHADERC` | empty | Path to a prebuilt `shaderc` (§1.4) |
| `FUNKGUI_ALLOW_OTHER_JUCE` | `OFF` | Downgrades the JUCE 8.0.4 check to a warning |
| `FUNKGUI_VERSION` (INTERNAL, output) | project version | FunkGui sets `set(FUNKGUI_VERSION ${PROJECT_VERSION} CACHE INTERNAL "")` because `project()` variables do not reach the parent scope |

### 1.8 Per-consumer configuration: the seams

INTERFACE sources compile inside the consumer, so product identity is a set of **PRIVATE definitions on each consumer
target**:

```cmake
funkgui_configure_product(<target>
    PRODUCT      FCompressor      # FUNKGUI_PRODUCT_NAME="FCompressor"   fallback screen, logs
    OBJC_PREFIX  Fcmp             # FUNKGUI_OBJC_PREFIX=Fcmp             -> name roots FcmpRenderView_…, FcmpDisplayLinkTarget_…
    ENV_PREFIX   FCMP_            # FUNKGUI_ENV_PREFIX="FCMP_"           -> FCMP_CANVAS_DUMP, FCMP_PREFS_DIR ...
    PREFS_FOLDER FCompressor)     # FUNKGUI_PREFS_FOLDER="FCompressor"   -> ~/Library/Application Support/FCompressor/preferences.settings
```

- `include/funkgui/core/Config.h` does `#error "call funkgui_configure_product()"` if any of the four is missing, so an
  unconfigured target fails to compile rather than colliding at runtime.
- **ObjC classes are registered at runtime under randomised names** (K2 #16). Class names are process-global (A §6.1),
  and a product prefix separates products but **not binaries**: FCompressor AU and VST3 in one Live or Reaper process,
  or an installed build next to a dev build, would both define `FcmpRenderView`, and by-name lookups would reach the
  other image's `FramePump`/`BgfxContext` singletons. So `NativeSurface.mm` and `DisplayLink.mm` define
  `JUCE_CORE_INCLUDE_OBJC_HELPERS 1` (`juce_core.h:376-379`) and build their classes as `juce::ObjCClass<NSView>` /
  `juce::ObjCClass<NSObject>` (JUCE's own mechanism, `juce_ObjCHelpers_mac.h:307, 365`), held in function-local statics
  inside those `.mm` files, with name roots `FUNKGUI_OBJC_PREFIX "RenderView_"` and `FUNKGUI_OBJC_PREFIX
  "DisplayLinkTarget_"`. The macro stays only as a readable root. HR's migration goldens are unaffected: class names
  never appear in dumps.
- **Environment variables** are read only through `funkgui::env("CANVAS_DUMP")`, which prepends `FUNKGUI_ENV_PREFIX`
  and caches the value on first use. They are read once, into `CaptureConfig`, at `EditorHost` construction. **No
  `getenv` in drawing code** (C §3.3.3). HR today reads `HRVB_UI_THEME` and `HRVB_UI_VIEW` every frame (A §1.3).
- The C++ namespace `funkgui` is the same in every product. Hidden visibility makes that safe (A §6.1). Each plugin binary
  also gets its own bgfx, `FramePump` and `BgfxContext` singletons.

### 1.9 How FCompressor consumes it

The pins, declarations and assertions are 03 §2.3's `cmake/FcmpDeps.cmake`: JUCE → bgfx (GPU) → FunkGui, with
`FCMP_FUNKGUI_TAG`/`_SHA`/`_VERSION` and `GIT_SHALLOW FALSE`. This appendix adds only the FunkGui-facing lines; the link
lines are 03 §2.7's (K1 #17):

```cmake
# before FetchContent_MakeAvailable(FunkGui), inside FcmpDeps.cmake (normal variables, never cached)
if(FCOMPRESSOR_HEADLESS OR FCOMPRESSOR_DSP_ONLY)      # 03 §2.2 options
  set(FUNKGUI_WITH_BGFX OFF)
else()
  set(FUNKGUI_WITH_BGFX ON)
endif()
if(FCOMPRESSOR_DSP_ONLY)                              # DSP-only: no JUCE, so FunkGui provides FunkGui::harness only
  set(FUNKGUI_WITH_PRESETS OFF)
  set(FUNKGUI_HARNESS_ONLY ON)                        # FunkGui skips every JUCE check and target (03 §2.2)
else()
  set(FUNKGUI_WITH_PRESETS ON)
endif()
set(FUNKGUI_BUILD_TOOLS ON)                           # funkgui_framerender for the DoD and verify-gui-live (K2 #26d)

# cmake/FcmpPlugin.cmake — every JUCE configuration links core + presets; gpu only when not headless.
# PRIVATE only: a configure assert fails if INTERFACE_LINK_LIBRARIES of FCompressor contains FunkGui* (K2 #26e),
# because a PUBLIC link would compile FunkGui's .mm sources into every format wrapper.
target_link_libraries(FCompressor PRIVATE FunkGui::core FunkGui::presets)          # juce_add_plugin target
funkgui_configure_product(FCompressor PRODUCT FCompressor OBJC_PREFIX Fcmp ENV_PREFIX FCMP_ PREFS_FOLDER FCompressor)
if(NOT FCOMPRESSOR_HEADLESS)
  target_link_libraries(FCompressor PRIVATE FunkGui::gpu)
  funkgui_compile_shaders(FCompressor)
  funkgui_add_font(FCompressor)                       # licences as bundle resources, GPU configurations only
endif()
# cmake/FcmpProbes.cmake: the plugin/UI probe links core only, never bgfx
target_link_libraries(fcmp_probe_plugin PRIVATE FunkGui::core FunkGui::presets FunkGui::harness)
funkgui_configure_product(fcmp_probe_plugin PRODUCT FCompressor OBJC_PREFIX Fcmp ENV_PREFIX FCMP_ PREFS_FOLDER FCompressor)
```

- **No `GIT_SHALLOW` for FunkGui** (03 uses `FALSE`). Git ignores `--depth` for a plain local path and prints a
  warning. A local clone hard-links objects, so it is cheap anyway.
- **Dev and agent override.** Configure with `-DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<dir>`, where `<dir>` is either the
  agent's **own** FunkGui worktree (a task that owns both repos) or a lead-made detached `FunkGui.wt/pin-<sha7>`
  worktree — never another agent's live worktree (03 §4.5). 03's `fcmp_assert_funkgui` then requires
  `git -C <dir> merge-base --is-ancestor ${FCMP_FUNKGUI_SHA} HEAD` (the override descends from the pin; K2 #26b) and
  warns with `git describe`. The override is sticky in the cache; `verify.sh --integration` (the lead's run) fails
  when `fcmp-deps.txt` shows one (K2 #26c).
- **Offline builds** use the same mechanism as JUCE and bgfx (B §7.6): `FETCHCONTENT_SOURCE_DIR_{JUCE,BGFX}` point at a
  read-only shared copy, never at HR's `build/_deps`.

### 1.10 Versioning, tags, worktrees

- **Tags** are annotated `v0.MINOR.PATCH` on `main`. `project(FunkGui VERSION x.y.z)` matches the tag, and the tag
  commit updates `CHANGELOG.md`.
- **MINOR bump:** any change that can alter a consumer's dump or behaviour. That covers:
  - a public-header API change;
  - dump or fingerprint format changes;
  - shader output for existing kinds;
  - glyph set or atlas bytes (UVs change, so every text hash changes);
  - theme values;
  - any widget geometry, ink or interaction rule.

  **PATCH:** fixes and refactors that leave every GalleryProbe dump byte-identical.
- Every CHANGELOG entry states its **golden impact**: `none`, `atlas`, or `geometry: <widgets>`. The consumer's lead uses
  it to plan re-blessing.
- **Tag gate.** A tag needs `ctest -L verify` green in the top-level FunkGui build, on both the `agent` (headless) and
  `agent-gui` presets (K1 #19).
- **FCompressor pin.** FCompressor pins the exact tag, its SHA and its version (`FCMP_FUNKGUI_TAG/_SHA/_VERSION`, 03
  §2.3). Moving the pin is a **lead-only commit at a sprint boundary**. FunkGui is tagged only in sprints that merge a
  FunkGui task. FCompressor UI geometry goldens are not adopted before the UI freeze FZ5 (03 §4.9), so pin bumps do
  not churn them (K3 #21).
- **Parallel work** (at most 3 agents across both repos). A FunkGui agent works in
  `git -C /Users/seanfunk/audio/libraries/FunkGui worktree add ../FunkGui.wt/s<N>-<task> -b s<N>/<task>` (03 §1.3).
  **FCompressor tasks consume only tagged FunkGui** (the pipelining rule): a FunkGui feature an FCompressor task needs
  in sprint N is scheduled and tagged in sprint N−1 or earlier. The only exception is one agent that owns both
  worktrees. The lead merges FunkGui first, tags it, then bumps the pin (03 §4.8).
- **FunkGui agents verify FCompressor** in a lead-made, detached, read-only FCompressor worktree at the sprint base,
  `.claude/worktrees/ro-<task>`, configured with the override to their own FunkGui worktree; they build there and never
  edit it (K3 #22).
- **Pre-1.0 promise.** None across minors. **`v1.0.0` = the tag HR migrates onto**, after FCompressor v1 (user
  decision).

---

## 2. Seeding FunkGui from the HR snapshot

### 2.1 Procedure (the commit sequence is part of the design)

One sequence, shared with 03 §4.9 (K1 #12, K3 #5). G0 is **lead pre-work**, not an agent task; it removes the
bootstrap cycle in which nothing on the FCompressor side could configure before FunkGui had CMake and a harness.

1. **G0, commit 1: "Snapshot of HardwareReverb GUI" (untagged provenance baseline).**
   - Copy the files in §2.2 into FunkGui **byte for byte**, keeping HR's file stems (`SdfCanvas.h` stays `SdfCanvas.h`).
   - `SEED.tsv` (03 §1.2) records, for every file: the HR source path, its mtime, the sha256 of the HR file, and the
     FunkGui path. HR has no git (C §1), so this file is the only provenance. At the time of writing (2026-09-22
     ~21:00), HR's `Source/gui` files were not the ones being edited: the edits were in `BgfxEditor.cpp`,
     `PresetPanel.cpp`, `presets/` and `Tools/`. The lead checks the sha256s against HR again on the day of the copy.
2. **G0, commit 2: "Mechanical renames".** Only the sed renames of §2.3, **keeping HR's file stems**, so a
   sed-normalised diff against HR is empty and the new names (`Canvas.h`, …) stay free for G2/G3 (K3 #16). The sed
   script sits at the top of `README.md`'s "Provenance" section. This is the baseline HR's later migration diffs against.
3. **G0, commit 3: minimal CMake**, about 15 lines: `project(FunkGui VERSION 0.0.1)`, an INTERFACE `FunkGui::harness`
   over HR's renamed harness (`funkgui::test`, still the v1 API), **empty placeholder** INTERFACE targets
   `FunkGui::core`, `FunkGui::gpu` and `FunkGui::presets` (so FCompressor's final link lines configure from day one),
   and `FUNKGUI_VERSION`. Tag **`v0.0.1`**; the lead creates the detached worktree `FunkGui.wt/pin-v0.0.1` and pins
   FCompressor to it for Sprint 0.
4. **G1 → `v0.1.0`:** `FunkGuiDeps/Targets.cmake` (globs), Core/Gpu/Fonts/Harness/Shaders targets (`FunkGui::presets`
   stays an empty placeholder until G8), **Harness v2**,
   `tools/golden.py`, `tools/verify.sh`, `fg.harness.self`, `fg.font.probe`, `fg.prefs.check`; the GPU target and the
   shaders build in `agent-gui` with the prebuilt shaderc (the GPU build-chain spike, K3 #19).
5. **G2…G8 → `v0.2.0`…`v0.8.0`:** the generalisation of §3–§5 in the task order of 03 §4.9 (API contracts, recorder,
   widgets, shapes/AREA/glyphs, cell widgets, `EditorHost`, FunkPresets). Each tag keeps GalleryProbe and FontProbe
   green, and its goldens are blessed by the lead.
6. **Do not copy** HR's `build/generated/shaders/fs_{ring,rrect,text}.mtl.h`. They are stale artefacts (A §1.1).

### 2.2 File list (HR → FunkGui) and per-file actions

| HR source (LOC) | FunkGui path (after generalisation; v0.0.1 keeps HR's stems) | Snapshot (v0.0.1) | Generalisation (G1–G8, v0.1.0+) |
|---|---|---|---|
| `Source/gui/Col.h` (37) | `include/funkgui/core/Col.h` | ns | + `fade(Col,a)` (HR has two copies, at `BgfxEditor.cpp:83` and `PresetPanel.cpp:56`) and `premix(Col ground, Col c, float a)`, the opaque blend for beads-free curves (A §2.6) |
| `Theme.h` (58) | `core/Theme.h` | ns | `ice`→`signal` (§2.3). Values unchanged, bit for bit |
| `TypeScale.h` (23) | `core/TypeScale.h` | ns → `funkgui::type` | Include `text/TextStyle.h` instead of the canvas, so no bgfx. Add `kUnit{14,1.0,.03,false}`, which is ad hoc in HR (`BgfxEditor.cpp:~1173`) |
| `TagPalette.h` (27) | `core/TagPalette.h` | ns | – (the preset tag colours) |
| `FontAtlasSdf.h/.cpp` (108/280) | `text/FontAtlasSdf.h`, `src/text/FontAtlasSdf.cpp` | ns | `kExtraChars` generated from `Glyphs.def` (§4.6). Add `bool baked() const` |
| `BundledFont.h` (21) | `text/BundledFont.h` | `<FunkGuiFonts.h>`, `funkguifonts::` | – |
| `SdfCanvas.h/.cpp` (111/344) | `canvas/Canvas.h`, `src/canvas/Canvas.cpp` | ns only (still bgfx-coupled) | **Split** into the recorder `Canvas` + `Prim`/`PrimList` + dump + `gpu/BgfxSink` (§3). `TextStyle` moves to `text/TextStyle.h` |
| `BgfxContext.h/.cpp` (111/295) | `gpu/BgfxContext.h`, `src/gpu/BgfxContext.cpp` | ns, shader include path | Font texture comes from `FontService` pixels. `init.limits.maxTransientVbSize`. `configure(Config)` (§4.5) |
| `FramePump.h/.cpp` (105/195) | `gpu/FramePump.h`, `src/gpu/FramePump.cpp` | ns | – (`kIdleHz 12`, `kFullHz 60` kept) |
| `DisplayLink.h/.mm` (28/76) | `gpu/DisplayLink.h`, `src/gpu/DisplayLink.mm` | ns, ObjC macro | – |
| `NativeSurface.h/.mm` (13/57) | `gpu/NativeSurface.h`, `src/gpu/NativeSurface.mm` | ns, ObjC macro | – |
| `UiPreferences.h/.cpp` (45/74) | `prefs/UiPreferences.h`, `src/prefs/UiPreferences.cpp` | folder macro, `funkgui::env("PREFS_DIR")` | Generic int keys (§5.9) |
| `shaders/vs_ui.sc` (23), `varying.def.sc` (12) | `shaders/` | verbatim | – |
| `shaders/fs_ui.sc` (115) | `shaders/fs_ui.sc` | verbatim | KIND_AREA plus the branch-chain rewrite (§4.3) |
| `Resources/fonts/*` (subset 10,288 B, upstream 114,904 B, licence) | `fonts/` | verbatim | Subset regenerated with the §4.6 glyphs |
| `Resources/licences/{bgfx,bx,bimg,bgfx.cmake}-LICENSE.txt` | **not copied**: taken from `${bgfx_SOURCE_DIR}` at configure time (§1.6; K1 #16) | – | – |
| `Tools/Harness.h` (197) | `include/funkgui/test/Harness.h` | ns `funkgui::test`; exposed as `FunkGui::harness` by G0's minimal CMake | Harness v2 (G1, 03 §3.2): spec rows, tolerance grammar, dup-key FAIL, `--bless-to` candidates only, `RESULT` JSON line. User confirmation pending that the harness lives in FunkGui (`DECISIONS.md` Q3) |
| `Tools/FontProbe.cpp` (98), `AtlasDump.cpp` (188), `PrefsCheck.cpp` (121) | `tools/` | ns, env prefix | PrefsCheck sandboxes `<PREFIX>PREFS_DIR` |
| `Tools/FrameRender.cpp` (311) | `tools/FrameRender.cpp` (CLI) + `src/canvas/SoftRaster.cpp` (the rasteriser) + `src/canvas/Fingerprint.cpp` | ns | AREA, flags, tags, axes. View size from the dump. `--legacy-hr` hash (§3.9) |
| `Scripts/capture-frame.sh` (31) | `tools/capture-frame.sh` | – | Args `<app-binary> <out.dump> <ENV_PREFIX>` |
| `BgfxEditor.cpp` regions (A §1.2 table) | `gpu/EditorHost`, `params/GestureController`, `widgets/*`, `a11y/*`, `widgets/DwellSelector`, `core/Ease` | not in the snapshot | Lifted in §5. HR itself is untouched |
| `PresetPanel.cpp` `printable` (:66), `fit` (:81), `MenuLook` (:126), `editKey` (:386) | `text/TextFit`, `text/LineEdit`, `juce/MenuLook` | not in the snapshot | §5.8 |

| `Source/presets/PresetTypes.h` (80), `PresetManager.h/.cpp` (67/308), `PresetStore.h/.cpp` (96/1382), `PresetFile.h/.cpp` (22/251) | `presets/`, `src/presets/` (FunkPresets) | **not in G0**: snapshotted by task G8 (the last sprint), only after HR's preset work settles | 01 §9.2: `Attribute`, `ProductConfig` (name, extension, XML root, DB env var), `PresetHooks`; HR literals → `ProductConfig`. `presets/` was being edited during research (C, B §4.1). The lead decides by S6 whether G8 snapshots or re-implements from the headers alone (03 §4.9) |

Not taken:
- `PluginEditor.*` (the classic editor).
- `FactoryPresets.*` (product content; FCompressor has `plugin/factory/`, 01 §9.2).
- `PresetPanel` (product-coupled UI, A §6.5). FCompressor draws its own strip in v1 on FunkPresets + `LineEdit` +
  `MenuLook`.
- Every DSP file.

### 2.3 Rename rules

| Item | HR | FunkGui |
|---|---|---|
| Namespace | `hrvbgui`, `hrvbgui::type` | `funkgui`, `funkgui::type` |
| Font binary data | `HardwareReverbFonts.h`, `hrvbfonts` | `FunkGuiFonts.h`, `funkguifonts` |
| Harness | `hrvb::` | `funkgui::test::` |
| ObjC classes | `HrvbRenderView`, `HrvbDisplayLinkTarget` | v0.0.1: the sed-renamed static classes; G7: `juce::ObjCClass` runtime names with roots `<OBJC_PREFIX>RenderView_`/`…DisplayLinkTarget_` (§1.8) |
| Prefs folder | `"HardwareReverb"` (`UiPreferences.cpp:29`) | `FUNKGUI_PREFS_FOLDER` |
| Env vars | `HRVB_*` (12 names, A §1.3) | `FUNKGUI_ENV_PREFIX` + generic suffix (§5.1 table) |
| Theme token | `ice` "reserved for exactly one thing: Freeze" | **`signal`**: "reserved for exactly one job, which the product names". FCompressor: **live gain reduction** (F §9) |
| Canvas type | `SdfCanvas` (kept as-is at v0.0.1) | `Canvas` (recorder) + `BgfxSink` (G3 onwards) |
| Capture statics | `SdfCanvas::dumpNextFrameTo` (a process-global one-shot) | `CaptureConfig` per EditorHost (§5.1). The PrimList writes itself |

---

## 3. Headless architecture

C §3.1 names the gap: drawing needs a `BgfxContext`, text needs a GPU texture, and eases read wall-clock `dt` inside an
`AudioProcessorEditor`. The target is that **layout, input and drawing run in a console process with a font atlas and a
fixed `dt`, and nothing else**.

### 3.1 Layers

```
            ┌────────────────────── FunkGuiCore (no bgfx, no ObjC, no Component) ───────────────────────┐
            │ FontService ─(CPU atlas)─> Canvas ──records──> PrimList ──> writeText / Fingerprint /       │
            │                              ▲                         SoftRaster (PNG)                    │
            │ Panel (product) ── tick(dt) ─┤ draw(Canvas&, Theme)                                        │
            │   widgets: RuleSlider, SegmentedSelector, LatchToggle, … ── ValueModel / ParamPort          │
            │   input: PointerEvent / WheelEvent / KeyEvent    a11y: std::vector<A11yItem>                │
            │ HeadlessHost ── implements HostServices, drives Panel with a fixed dt, replays input         │
            └──────────────────────────────────────────────────────────────────────────────────────────────┘
            ┌──────────── FunkGuiGpu ─────────────────────────────────────────────────────────────────────┐
            │ EditorHost (juce::AudioProcessorEditor, FrameClient, HostServices)                           │
            │   juce events ─convert─> Panel input;  A11yBridge: A11yItem -> invisible juce::Components     │
            │   submitFrame: Panel::tick → Canvas → PrimList → BgfxSink::submit (6-vertex quads) → bgfx    │
            │ BgfxContext (singleton, windows, program, font texture) · FramePump · NativeSurface · DisplayLink │
            └──────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 `Prim` and `PrimList`

```cpp
namespace funkgui {
enum class PrimKind : uint8_t { rrect = 0, text = 1, segment = 2, area = 3 };   // not `Kind`: fcdsp::Kind exists (K1 #18)
namespace pflag {                         // stored in d2[3] as a small integer-valued float
    constexpr uint32_t live         = 1u << 0;  // animated/live data: excluded from geometry fingerprints
    constexpr uint32_t strokeBottom = 1u << 1;  // AREA: stroke the bottom edge instead of the top
    constexpr uint32_t strokeBoth   = 1u << 2;  // AREA: stroke both edges
}
using Tag = uint16_t;                     // 0 = untagged; 1..255 FunkGui; 256.. product (§3.8)

struct Prim {                             // 84 bytes. Exactly one dump line. Expands to one axis-aligned quad.
    float    x0, y0, x1, y1;              // quad TL / BR in logical px, AA apron included
    uint32_t c0, c1;                      // RGBA8, r | g<<8 | b<<16 | a<<24 (HR pack())
    float    d0[4];                       // TL varyings: local x,y (text: u0,v0), hw, hh
    float    e0[2];                       // BR local x,y (text: u1,v1)
    float    d1[4];                       // per kind (A §2.2 table; AREA §4.1)
    float    d2[4];                       // [0] border/stroke half-width, [1] softness, [2] PrimKind, [3] flags
    Tag      tag;                         // CPU-only, never uploaded
    uint16_t reserved = 0;
};
static_assert(sizeof(Prim) == 84 && std::is_trivially_copyable_v<Prim>);

struct AxisMap { float px0, px1, v0, v1; bool log = false; };      // value v0 at px0, v1 at px1
struct AxisRec { Tag tag; AxisMap x, y; bool hasX, hasY; };

struct FrameInfo {
    int   logicalW, logicalH; float dpi;   // dpi = physH / logicalH (HR)
    Col   clear; float textGamma; int theme;
    float seconds;                         // u_viewSize.w
    uint32_t frame; float dt; bool fixedClock;
};

struct PrimList {
    FrameInfo info{};
    std::vector<Prim>    prims;
    std::vector<AxisRec> axes;
    uint32_t missingGlyphs = 0;            // codepoints not in the atlas (HR drops them silently, A §2.5)
    uint32_t missingFirst[8] = {};         // first distinct missing codepoints, for the dump
    void clear();                          // keeps capacity
    bool writeText(std::FILE*) const;      // dump v2 (§3.8)
    static bool parseText(std::string_view, PrimList&);   // accepts HR v1 and v2
};
}
```

**The vertex expansion is exactly HR's.** HR's four corners carry local coordinates `(d0.x,d0.y)`, `(e0.x,d0.y)`,
`(e0.x,e0.y)` and `(d0.x,e0.y)` for every kind: rrect `SdfCanvas.cpp:108-111`, segment `:143-146`, text `:252-255`. So
`Prim` is lossless, and `BgfxSink` rebuilds the identical 64-byte `Vtx` stream (a,b,c,a,c,d). **The GPU sees
bit-identical vertices to HR for the same calls.**

### 3.3 `Canvas`: the CPU recorder

```cpp
class Canvas {
public:
    explicit Canvas(const FontAtlasSdf& atlas);
    void begin(const FrameInfo&);                  // clears the list (capacity kept)
    const PrimList& end();                         // valid until the next begin()

    // HR primitives; maths byte-identical to SdfCanvas.cpp
    void  rrect (float x, float y, float w, float h, float radius, Col fill,
                 float borderW = 0, Col border = {}, float softness = 0);
    void  rrect4(float x, float y, float w, float h, float rTL, float rTR, float rBR, float rBL,
                 Col fill, float borderW = 0, Col border = {}, float softness = 0);
    void  hairlineH(float x, float y, float w, Col);   // floors y to a device px (HR)
    void  hairlineV(float x, float y, float h, Col);
    void  segment(float x0, float y0, float x1, float y1, float width, Col, float softness = 0);
    float snapY(float y) const;  float snapX(float x) const;          // round to device px
    void  text(const char* utf8, float x, float yTop, const TextStyle&, Col, Align = Align::left);
    float textWidth(const char* utf8, const TextStyle&) const;
    float capCentreTop(float centreY, const TextStyle&) const;
    float sharedBaselineTop(float otherTop, const TextStyle& other, const TextStyle& mine) const;

    // new (§4)
    void area(float x0, float x1, float yTop0, float yTop1, float yBot0, float yBot1,
              Col fill, float strokeW = 0, Col stroke = {}, AreaEdge edge = AreaEdge::top);
    void areaStrip(const float* xs, int n, const float* yTop, const float* yBot /*nullable*/, float yBase,
                   Col fill, float strokeW = 0, Col stroke = {}, AreaEdge edge = AreaEdge::top);
    void polyline(const float* xs, const float* ys, int n, float width, Col opaque);
    void disc(float cx, float cy, float r, Col fill, float ringW = 0, Col ring = {});
    void dotted(float x, float y, float w, float step, Col);           // 1-device-px dots
    void axis(Tag, const AxisMap* x, const AxisMap* y);                // recorded for probes, not drawn

    // stamped onto every primitive until changed
    void setTag(Tag);   Tag  tag()  const;
    void setLive(bool); bool live() const;
    struct Scope { Scope(Canvas&, Tag, bool live); ~Scope(); /* restores */ Canvas& c; Tag t; bool l; };

    float dpi() const;  int missingGlyphs() const;
protected:
    Prim& emit(PrimKind);                          // appends one Prim stamped with the current tag/live flag (K3 #16):
                                                   // HR primitives live in Canvas.cpp (G3), new ones in CanvasShapes.cpp (G4)
};
```

- The canvas is a **member** of its host, not constructed per frame, so `prims` keep their capacity (A §2.1).
- `text()` works whenever `atlas.baked()` is true. It no longer needs a GPU texture (C §3.1).

### 3.4 `FontService` and `BgfxSink`

```cpp
class FontService {                                  // Core. Process-wide; never destroyed (HR idiom)
public:
    static FontService& get();
    const FontAtlasSdf& atlas();                     // bakes on first call from BundledFont (11–23 ms, A §0)
    bool     ok() const;                             // baked from the embedded face
    uint64_t atlasHash() const;                      // FNV-1a of the R8 pixels == FontProbe's font.atlas
};

class BgfxSink {                                     // Gpu
public:
    enum class Result : uint8_t { submitted, empty, droppedOverflow };
    // setViewFrameBuffer/Rect/Clear(clear from info)/Mode(Sequential)/touch, expand, 1 transient VB, 1 submit.
    Result   submit(const PrimList&, bgfx::ViewId, bgfx::FrameBufferHandle, int physW, int physH);
    uint32_t overflowCount() const;                  // lifetime count, shown in diagnostics (§4.5)
private:
    std::vector<Vtx> scratch_;                       // 6 * prims, capacity kept; Vtx = HR's 64-byte layout
};
```

`BgfxContext::createResources()` uploads `FontService::get().atlas().pixels()` into the R8 texture. HR bakes inside
`BgfxContext`.

### 3.5 `Panel`: the GPU-free UI unit

```cpp
namespace funkgui {
struct Mods { bool shift = false, cmd = false, alt = false, ctrl = false; };
struct PointerEvent { float x, y; Mods mods; int clicks = 1; bool popup = false; };   // popup = right/ctrl-click
struct WheelEvent   { float x, y, dx, dy; bool smooth, reversed, inertial; Mods mods; };
enum class Key : uint8_t { character, tab, up, down, left, right, pageUp, pageDown, home, end,
                           escape, enter, backspace, del, space };
struct KeyEvent     { Key key; Mods mods; char32_t ch = 0; };
enum class Cursor : uint8_t { normal, leftRight, upDown, pointingHand, crosshair };

class HostServices {                                 // what a Panel may ask of its host
public:
    virtual ~HostServices() = default;
    virtual void   setUnboundedDrag(bool on) = 0;    // enableUnboundedMouseMovement(true, true) in EditorHost
    virtual void   showParamMenu(ParamPort&, float x, float y) = 0;   // host context menu for a parameter
    virtual void   nudgeFullRate() = 0;              // FramePump::nudgeFullRate
    virtual double nowSeconds() const = 0;           // wall clock (EditorHost) or simulated (HeadlessHost)
};

class Panel {
public:
    virtual ~Panel() = default;
    virtual void  attach(HostServices&) = 0;         // called once by the host before the first tick
    virtual int   width() const = 0;                 // fixed logical size
    virtual int   height() const = 0;
    virtual void  tick(float dt) = 0;                // every eased value; seconds, never frames
    virtual void  idle(double nowSec) {}             // >= 10 Hz even while the display link is stopped: closes idle
                                                     // wheel gestures (HR BgfxEditor.h:119-124 lesson)
    virtual void  draw(Canvas&, const Theme&) = 0;
    virtual bool  wantsFullRate() const = 0;
    virtual void  pointerMove(const PointerEvent&) {}
    virtual void  pointerExit() {}
    virtual void  pointerDown(const PointerEvent&) {}
    virtual void  pointerDrag(const PointerEvent&) {}
    virtual void  pointerUp(const PointerEvent&) {}
    virtual void  doubleClick(const PointerEvent&) {}
    virtual bool  wheel(const WheelEvent&) { return false; }
    virtual bool  key(const KeyEvent&) { return false; }      // true = consumed (Logic/Live must not swallow it)
    virtual Cursor cursor() const { return Cursor::normal; }
    virtual void  accessibility(std::vector<A11yItem>&) const = 0;
    virtual uint32_t a11yRevision() const = 0;       // bumps when items appear/disappear/move
    virtual void  a11yAction(uint32_t id, A11yAction, double value = 0) = 0;
    virtual void  closeGestures() = 0;               // host closes the editor mid-gesture (HR dtor rule)
    virtual bool  filesInterest(const std::vector<std::string>&) const { return false; }
    virtual void  filesDropped(const std::vector<std::string>&) {}
};
}
```

### 3.6 `HeadlessHost`

```cpp
class HeadlessHost final : public HostServices {
public:
    HeadlessHost(Panel&, int themeIdx = 0, float dpi = 2.0f);
    void  tick(int frames, float dt = 1.0f / 60.0f);          // Panel::tick + simulated clock
    int   settle(int maxFrames = 600, float dt = 1.0f / 60.0f); // tick until !wantsFullRate(); returns frames used
    const PrimList& draw();                                    // one frame into the owned Canvas
    void  move(float x, float y);
    void  click(float x, float y, Mods = {});
    void  doubleClick(float x, float y, Mods = {});
    void  drag(float x0, float y0, float x1, float y1, int steps = 8, Mods = {});
    void  wheel(float x, float y, float dy, bool smooth = false, Mods = {});
    void  keys(const char* spec);          // HR replayKeys grammar + home,end,pageup,pagedown
    std::vector<A11yItem> accessibility() const;
    bool  writeDump(const char* path) const;
    bool  writePng (const char* path, int supersample = 2) const;   // SoftRaster: agents can look at frames
    struct Log { int menus = 0; bool unbounded = false; int nudges = 0; } log;   // HostServices calls, for asserts
    // HostServices: records into log; nowSeconds() returns the simulated clock
};
```

### 3.7 Determinism rules (every widget and every FCompressor view follows them)

1. There is no wall clock inside a Panel. Time comes only from `tick(dt)` and `HostServices::nowSeconds()`.
2. Every ease goes through `funkgui::ease` (§5.7) and **snaps** within ε. With a fixed `dt`, state converges in a finite
   number of ticks, and `settle()` is exact.
3. `draw()` is `const` in spirit: it reads state and never advances it.
4. No `getenv` and no preferences file access in `tick`/`draw`. Capture overrides arrive through `CaptureConfig` (§5.1),
   set by the host.
5. Live data enters a Panel only through the product's facade, which the headless probe can fake (`FakeFacade`, §9.5).
6. The first-run hint is a Panel state (`HintLine`, §5.8). Probes construct panels with `PanelOptions::skipHint`, and
   live captures set `FCMP_UI_NO_HINT=1`, so both start without it (K1 #10).
7. **No drawn state may depend on another thread's completion time** (K1 #11). Anything computed on a worker (the step
   responses) is computed synchronously inside `tick()` when the panel runs with `PanelOptions::syncPreview` (probes and
   `FCMP_UI_FIXED_DT` captures); `wantsFullRate()` stays true while a job is pending, so `settle()` waits for it.

### 3.8 Dump format v2 (a superset of HR's)

```
funkgui-dump 2
clear 16171a
view 960 640 dpi 2 theme 0 frame 61 dt 0.0166667 clock fixed fps 0.0 rate full
glyphs missing 0
axis LEVEL x 556 748 -42 6 lin y 332 140 -42 6 lin
p 38.5 38.5 57.5 49.5  c0 ffe4eaec c1 ffe4eaec  d0 -9.5 -5.5 8 4  e0 9.5 5.5  d1 0 0 0 0  d2 0 0 0 0
p 40 137.75 42 162.25  c0 ff3f3928 c1 ffe8d47f  d0 -1 -12.25 1 12.25  e0 1 12.25  d1 -10 -10 8 10  d2 0.75 0 3 3  t HIST_GR
```

- The `view W H dpi D` prefix is unchanged, so HR's `sscanf("view %f %f dpi %f")` still parses it.
- A `p` line is HR's 20 fields, optionally followed by `t <TAGNAME>`. HR's parser stops after 20 matches
  (`FrameRender.cpp:95-101`), so v2 files load in HR's tool.
- Floats are written with **`%.9g`** (an exact float round trip). HR uses `%g`, which quantises incidentally (C §2.2).
- An `axis` line is `axis <TAG> x <px0> <px1> <v0> <v1> <lin|log> y <py0> <py1> <w0> <w1> <lin|log>`, with `-` for an
  absent axis. C §3.3.4 asks for this, for probes G2 and G3.
- `glyphs missing N [U+XXXX …]` is new. Probes assert `N == 0`.
- **Tags.** FunkGui owns 1..255, and products register names for 256..:
  `funkgui::registerTagNames(std::span<const TagName>)`, where `TagName{Tag, const char*}`.
  FunkGui's own tags: `SLOT_LABEL SLOT_VALUE SLOT_SUB SLOT_TRACK SLOT_CARET SLOT_NOTCH DETENT_TICK DETENT_LABEL WORD
  LATCH CELL FOCUS_RING HINT`.

### 3.9 Fingerprint

```cpp
struct FingerprintOptions {
    bool  excludeLive = true;            // flags bit 0
    bool  themeInvariant = true;         // text d1[2] (gamma) hashed as 0 -> hash(theme 0) == hash(theme 1) (C G1)
    float quantum = 1.0f / 1024.0f;      // every float hashed as int32 lrint(v / quantum)
    bool  legacyHr = false;              // HR FrameRender.cpp:108-150 algorithm, for HR's migration proof only
};
struct Fingerprint {
    uint64_t geometry;                   // FNV-1a over non-text, non-live prims: x0 y0 x1 y1 d0[4] e0[2] d1[4] d2[4]
    uint64_t text;                       // same fields for text prims
    int statics, live, texts, rrects, segments, areas; float maxX, maxY;
    std::vector<std::pair<Tag, int>> tagCounts;
};
Fingerprint fingerprint(const PrimList&, const FingerprintOptions& = {});
void addMetrics(std::vector<test::Metric>&, std::string_view prefix, const Fingerprint&);  // <prefix>.geometry …
```

- **Live/headless parity (C G1).** `%.9g` makes the dump exact. A capture from the real Standalone and a headless frame
  of the same state therefore give **bit-equal** `PrimList`s and equal hashes.
- The 1/1024 px quantum absorbs libm last-bit noise in curve vertices (C §5.12). This matters because hashes are
  compared across arches only through the x86 overlay.

### 3.10 Mapping to C's GUI probes

| C probe | Provided by FunkGui | Supplied by the product |
|---|---|---|
| G1 geometry per view × Mode × dpi, theme invariance, live parity | `fingerprint`, `HeadlessHost::settle`, dump v2, `capture-frame.sh` | `fcmp::ui::views()`, `Panel::setView`, Mode selection (Part 2 intro) |
| G2 curve vs analytic | `axis` records + `TRANSFER_CURVE` tags in the PrimList | the probe maths (`analysis::staticGain`, 01 §7) |
| G3 dot and meters truthful | tags `OP_DOT`, `METER_GR`, `HIST_GR` + axes | real processor run |
| G4 a11y golden text | `A11yItem`, `a11yDumpLine()` (§5.6) | per-Mode golden files |
| G5 key/pointer replay | `HeadlessHost::keys/drag/wheel` | assertions |
| G6 text-fit lint | `text::fits()`, `Canvas::missingGlyphs()` | per-Mode labels (§8.3) |

### 3.11 FunkGui's own tests (CTest, top-level builds only; every name starts with `fg.`, K1 #19)

The list is the union of Draft 2's and Draft 3's:
- `fg.harness.self` (G1): the tolerance grammar, duplicate keys, overlays, exit codes and atomic candidate writes.
- `fg.shader.hash` (G1, GPU presets): fingerprints the generated `vs_ui/fs_ui.mtl.h`.
- `fg.font.probe` (= Draft 3's `fg.font.atlas`): FontProbe `--check`. It covers the atlas hash, metrics,
  `font.missing = 0` for every `Glyphs.def` codepoint, and subset≡upstream.
- `fg.prefs.check` (= `fg.prefs`): PrefsCheck, sandboxed.
- `fg.gallery.<state>.dpi{1,2}`: GalleryProbe renders `test/gallery` in 8 scripted states:
  - rest, hover, drag, focus-ring, locked/derived/na, stepped-with-labels, AREA strips, glyph sheet.
  - It fingerprints each state and checks theme invariance.
  - `a11y-gallery.txt` is compared line by line.
- `fg.canvas.parity` (= `fg.recorder.roundtrip`): record → `writeText` → `parseText` → fingerprint must equal the
  in-memory fingerprint.
- `fg.canvas.expansion`: `BgfxSink`'s CPU expansion (a pure function, testable without a GPU) against hand-computed HR
  vertices for one prim of each kind, bit for bit (the recorder-parity spike, G3).
- FunkGui's own test targets compile with `-ffp-contract=off`, so gallery goldens stay arch-neutral (K2 #26g).
- Agents run `--check` only. `FUNKGUI_ALLOW_BLESS=1` is required to write goldens inside the tree. **The lead blesses.**

---

## 4. New primitives, shader, budget, glyphs

### 4.1 `KIND_AREA = 3`

This is A §2.6's proposal, plus F §6.2's edge flags. It draws a vertical span between two straight edges across one
column, with an optional stroke along one edge or both. **The quad stays axis-aligned**, so the dump and FrameRender's
two-corner rebuild still work.

| Field | Value |
|---|---|
| `x0, x1` | exactly the column's `[x0, x1]`, **with no x apron**. Neighbouring columns tile with no gap and no overlap, because the rasteriser's top-left fill rule gives each pixel to exactly one quad |
| `y0, y1` | `min(yTop0, yTop1) − 1.5 − strokeW/2` and `max(yBot0, yBot1) + 1.5 + strokeW/2` |
| `d0` | `(lx, ly, hw, hh)`: centre-relative local position, as for rrect. `e0` holds the BR corner |
| `d1` | `(yTop0, yTop1, yBot0, yBot1)`, centre-relative |
| `d2` | `(strokeW/2, 0, 3, flags)` |
| `c0` / `c1` | fill (α 0 = stroke-only line) / stroke |

`enum class AreaEdge : uint8_t { top, bottom, both };` sets flags bit 1 (`strokeBottom`) or bit 2 (`strokeBoth`).
`Canvas::setLive(true)` ORs in bit 0.

### 4.2 CPU wrappers (no shader change)

- `areaStrip(xs, n, yTop, yBot, yBase, …)` emits `n−1` AREA quads between consecutive `xs`, with linearly interpolated
  edges. When `yBot == nullptr` the bottom is the flat `yBase`.
  - History callers pass column-centre abscissae, with the first and last clamped to the plot edges.
  - A stroke-only strip is a **seam-free line that fades without beads**. Use it for HISTORY OUT/DET and the CONTROL
    PATH traces.
- `polyline(xs, ys, n, w, c)` emits `n−1` capsule `segment`s. In Debug it asserts `c.a == 255`: overlapping capsule ends
  bead at any α < 1 (A §2.6), so fades use `premix(ground, c, a)`.
- `disc(cx, cy, r, fill, ringW, ring)` = `rrect(cx−r, cy−r, 2r, 2r, r, …)`, HR's ring trick (A §2.1).
- `dotted(x, y, w, step, c)` draws 1-device-px `rrect` dots at `snapX(x + k·step)`, `snapY(y)`. This is the locked
  track.
- `axis(tag, x, y)` records the plot mapping for probes. It draws nothing.

### 4.3 Shader (`shaders/fs_ui.sc`): the new branch chain

Kinds 0–2 keep HR's maths exactly (`fs_ui.sc:39-112`). Only the chain is reordered, because under HR's `> 1.5` test
kind 3 would fall into the segment branch (A §2.3).

```glsl
#define KIND_RRECT 0.0
#define KIND_TEXT 1.0
#define KIND_SEGMENT 2.0
#define KIND_AREA 3.0
float kind = v_data2.z;
if (kind < 0.5)      { /* rrect   — HR :39-79, unchanged */ }
else if (kind < 1.5) { /* text    — HR :95-112, unchanged */ }
else if (kind < 2.5) { /* segment — HR :80-94, unchanged */ }
else
{   // ---- area: a column between two straight edges; optional stroke on top / bottom / both
    vec2  p  = v_data0.xy;  float hw = max(v_data0.z, 1e-4);
    float t  = clamp(p.x / (2.0 * hw) + 0.5, 0.0, 1.0);
    float yT = mix(v_data1.x, v_data1.y, t);
    float yB = mix(v_data1.z, v_data1.w, t);
    float sT = (v_data1.y - v_data1.x) / (2.0 * hw);
    float sB = (v_data1.w - v_data1.z) / (2.0 * hw);
    float dT = (yT - p.y) * inversesqrt(1.0 + sT * sT);          // > 0 above the top edge
    float dB = (p.y - yB) * inversesqrt(1.0 + sB * sB);          // > 0 below the bottom edge
    float fa = (1.0 - smoothstep(-aa, aa, max(dT, dB))) * v_color0.a;
    float fl     = v_data2.w;
    float bottom = mod(floor(fl * 0.5  + 0.01), 2.0);            // flags bit 1
    float both   = mod(floor(fl * 0.25 + 0.01), 2.0);            // flags bit 2
    float dS = both > 0.5 ? min(abs(dT), abs(dB)) : (bottom > 0.5 ? abs(dB) : abs(dT));
    float hs = v_data2.x;
    float sa = hs > 0.0 ? (1.0 - smoothstep(hs - aa, hs + aa, dS)) * v_color1.a : 0.0;
    float oA = sa + fa * (1.0 - sa);                                // source-over, as the rrect border (HR :69-77)
    col = vec4((v_color1.rgb * sa + v_color0.rgb * fa * (1.0 - sa)) / max(oA, 1e-5), oA);
}
```

The flags decode was checked for 0…7: bit 0 (live) never changes the result.

**CPU mirror.** `SoftRaster` (the FrameRender rasteriser, now a library) gets the same branch. It classifies kinds by
range (`<0.5`, `<1.5`, `<2.5`, else) and decodes flags as `int(d2[3] + 0.5)`. A §2.6 #4 applies: **the shader and the
mirror land in the same commit**, or FrameRender will call areas "segments". One task (G4) owns both, plus
`SoftRaster`, the FrameRender CLI and `Glyphs.def` (K3 #16).

### 4.4 Recorder and FrameRender changes

| Where | Change |
|---|---|
| `Canvas` | `area`, `areaStrip`, `polyline`, `disc`, `dotted`, `axis`, `setTag`/`setLive`/`Scope`, missing-glyph count, `snapX` |
| `PrimList::writeText` / `parseText` | v2 format (§3.8), `%.9g`, `t TAG`, `axis`, `glyphs missing` |
| `tools/FrameRender` | a thin CLI over `parseText` + `SoftRaster` + `fingerprint`: `FrameRender <dump> <out.png> [ss]`, `--fingerprint <dump> [--check g]`, `--legacy-hr` (HR's y-band exclusion and `%g` hash, for HR's migration only). **HR's hard-coded Rank band (`FrameRender.cpp:122-127`) is gone.** Live primitives are excluded by flag |

### 4.5 Transient budget: 32 MiB, and overflow made visible

`BgfxContext::initBackend` sets `init.limits.maxTransientVbSize = FUNKGUI_TRANSIENT_VB_BYTES` (32 MiB). HR relies on
bgfx's compiled default of 6 MiB (`config.h:427`). `kMaxWindows` stays at 16.

Primitive counts come from §6–§7. Every primitive costs 64 B × 6 = 384 B, and the buffer is shared by every editor in the
process for one `bgfx::frame()` (A §2.4).

| Frame | Typical prims | Worst prims | Worst bytes | Editors before drop @ 6 MiB | @ 32 MiB |
|---|---|---|---|---|---|
| PANEL (≈750 text, 250 slot chrome, 750–1000 history, 200–450 transfer, 15 meters) | 2.0K | 2.6K | 1.0 MB | 6 | 33 (> kMaxWindows 16) |
| CHARACTERISTICS (§7.3) | 3.8K | 4.3K | 1.65 MB | 3 | 20 |
| PANEL↔CHARACTERISTICS crossfade (≤ 0.4 s) | – | 6.9K | 2.65 MB | 2 | 12 |

- **Overflow.** `BgfxSink::submit` returns `droppedOverflow` (HR drops silently, `SdfCanvas.cpp:322-329`). In response:
  - `EditorHost` increments `Diagnostics::overflows`;
  - it logs once through `juce::Logger` when `<PREFIX>GPU_LOG=1`;
  - it `jassertfalse`s in Debug;
  - the next dump carries `overflow N`.
- **Indexed quads** (268 B/prim, A §2.6 #6 ii) are deferred to a later MINOR. At 32 MiB they are not needed.

### 4.6 Glyph additions and subset regeneration

`include/funkgui/text/Glyphs.def` is the **single source** for the atlas list. `kExtraChars` is generated from it, the
subset script greps it, and FontProbe checks every entry. It is an X-macro and **append-only**, because order changes
the atlas packing:

```
FUNKGUI_GLYPH(0x00B0, degree)       FUNKGUI_GLYPH(0x00B1, plusMinus)   FUNKGUI_GLYPH(0x00D7, times)
FUNKGUI_GLYPH(0x2013, enDash)       FUNKGUI_GLYPH(0x2192, rightArrow)  FUNKGUI_GLYPH(0x221E, infinity)
FUNKGUI_GLYPH(0x00B7, middleDot)    FUNKGUI_GLYPH(0x2191, upArrow)     FUNKGUI_GLYPH(0x2193, downArrow)   // HR's 9, in HR's order
FUNKGUI_GLYPH(0x00B5, micro)        // µS attack readouts (FET 20–800 µs)
FUNKGUI_GLYPH(0x2212, minus)        // every negative number (policy below)
FUNKGUI_GLYPH(0x2026, ellipsis)     // fitEllipsis(); PresetPanel fakes it with ".."
FUNKGUI_GLYPH(0x2264, lessEqual)    // "≤ 0.1 DB"
FUNKGUI_GLYPH(0x2265, greaterEqual) // "≥ 20:1"
FUNKGUI_GLYPH(0x2248, almostEqual)  // "≈ THR −28 DB" remap sub-readouts
FUNKGUI_GLYPH(0x0394, delta)        // DELTA latch caption, ΔGR
FUNKGUI_GLYPH(0x2014, emDash)       // footer separators
FUNKGUI_GLYPH(0x2022, bullet)       // browser current-row marker alternative
FUNKGUI_GLYPH(0x2190, leftArrow)    // "← PANEL" hints, stereo lanes
```

- **Count and occupancy.** 103 → 113 baked glyphs. Atlas occupancy goes from ≈43 % to ≈47 %, and A §4 puts 20–30
  added glyphs as safe. All 10 are present in upstream JetBrains Mono (A §4 cmap check).
- **Regeneration** (`tools/subset-font.sh`, A §4's command generalised; fontTools is not installed, so the script makes a
  throwaway venv under `$TMPDIR`):
  ```sh
  U=$(grep -o 'FUNKGUI_GLYPH(0x[0-9A-F]*' include/funkgui/text/Glyphs.def | sed 's/.*0x/U+/' | paste -sd, -)
  "$VENV/bin/pyftsubset" fonts/upstream/JetBrainsMono-Regular.ttf --output-file=fonts/JetBrainsMono-Regular-subset.ttf \
    --unicodes="U+0020-007E,$U" --layout-features='' --no-hinting --name-IDs='0,13,14' --drop-tables+=DSIG --notdef-outline
  ```
- Then FontProbe runs against the upstream face to prove the subset bakes **bit-identically**. The lead re-blesses
  `fontprobe.txt` and every gallery golden (UVs are hashed). This is a **MINOR** bump, golden impact `atlas`.
- **Minus policy.** Every numeric formatter (`funkgui::fmt`, §5.8, and FCompressor's formatters) writes U+2212. ASCII
  `-` stays only inside words ("PRE-DELAY"). Typed text such as preset names stays ASCII through `printable()`
  (A §4.4).
- **Missing glyphs are errors now.** `Canvas` counts undecodable or unknown codepoints (HR drops them without an
  advance, A §2.5). The dump reports them, and G6 and GalleryProbe assert zero.

---

## 5. Reusable widgets lifted from `BgfxEditor`

The line references are HR's `BgfxEditor.cpp` as of 2026-09-22 (A §1.2 regions). **None of this edits HR**: the code is
re-implemented in FunkGui from the pattern.

### 5.1 `EditorHost` (Gpu): surface lifecycle, fallback, retry, backing scale, FramePump client, diagnostics

Draft 1 and Draft 3 called this class `EditorShell`, after A §6.5. The canonical name is **`EditorHost`** (this appendix
owns FunkGui's spellings); 01 and 03 now use it (K1 #18). Its config struct is `EditorConfig`, not `HostConfig`,
because `fcdsp::HostConfig` exists (K1 #18).

```cpp
namespace funkgui {
struct EditorConfig {
    int width, height;                               // fixed: setResizable(false, false), then setSize() exactly once (HR :146)
    const char* fallbackTitle;                       // "FCOMPRESSOR"
    std::function<void(bool)> setUiAttached;         // telemetry gate keyed to editor lifetime (B §3)
};

class EditorHost : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget,
                   private FrameClient, private juce::Timer, private HostServices {
public:
    EditorHost(juce::AudioProcessor&, EditorConfig, std::unique_ptr<Panel>);
    ~EditorHost() override;       // panel->closeGestures(); setUiAttached(false); FramePump::remove; detachSurface()
                                  // (the derived Editor's members are already gone here: see teardown rule below)
    Panel& panel() noexcept;
    struct Diagnostics { uint32_t frames, overflows, attachFailures; bool displayLinked; float fps; double scale; };
    Diagnostics diagnostics() const;

    void paint(juce::Graphics&) override;            // fallback screen only (HR :789-812), strings from EditorConfig
    void resized() override;  void moved() override;  void parentHierarchyChanged() override;
    void mouseMove(const juce::MouseEvent&) override;   void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;   void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;     void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;    // Panel::key result; true keeps Logic/Live from eating keys
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    Result submitFrame(float dt) override;           // FrameClient
    void*  frameClockView() const override;
    void   timerCallback() override;                 // 100 ms: panel_->idle(now) — wheel-gesture close (HR :1685)
    void   setUnboundedDrag(bool) override;          // HostServices
    void   showParamMenu(ParamPort&, float x, float y) override;
    void   nudgeFullRate() override;
    double nowSeconds() const override;
    void   attachSurfaceIfPossible();  void detachSurface();  void refreshDrawableScale();
    double backingScaleFor(void* view) const;        // honours UI_SCALE / UI_SCALE_AFTER

    std::unique_ptr<Panel> panel_;  Canvas canvas_;  BgfxSink sink_;  A11yBridge a11y_;  CaptureConfig capture_;
    Theme theme_;  int themeIdx_ = 0;  uint32_t prefsRevision_ = 0;
    void* renderView_ = nullptr;  bool surfaceOk_ = false, bgfxFailed_ = false, fallbackPainted_ = false;
    int retryCount_ = 0;  float retrySec_ = 0;  int physW_ = 0, physH_ = 0;  double attachedScale_ = 0;
    void* attachedPeer_ = nullptr;  juce::MouseInputSource* dragSource_ = nullptr;  Diagnostics diag_{};
};
}
```

**Teardown rule (K2 #27).** The derived `fcmp::ui::Editor` is destroyed before `~EditorHost` runs its body, so nothing
the base destructor touches may live in the derived class: the parameter ports belong to the processor (§9.5), and
`PreviewWorker` belongs to the Panel, whose destructor stops it (`stopThread(1000)`) before anything else.
`fcmp::ui::Editor::~Editor()` calls `panel().shutdown()` (stop the worker, then `closeGestures()`) first.

**Ported behaviours.** Every one of these is an HR bug fix that must not be lost:
- A full slot (16 editors) is a wait, not a failure (`:656-660`). Five consecutive failures show the fallback; retries
  run every 2 s, or every 10 s once failed (`:838-872`).
- `!owns(view)` after a failed primary rebuild → detach and retry on the next tick (`:866-872`).
- On re-parent, re-attach to the new peer (`:745-757`). `detachSurface` releases the display link **before** it destroys
  the view (`:727-743`).
- The backing scale is polled every frame and on `moved()` (`:697-724`).
- `!isShowing()` → no work (`:838`). The fallback `repaint()` happens once per state change.
- The destructor closes open gestures (`:163-175`).
- The theme follows `UiPreferences::revision()` within the process (`:826-834`).
- Hit-tested wheel, host parameter menu on right-click or ctrl-click, keys return `true` (A §3.2–3.7). These live in the
  widgets, and EditorHost only converts the events.

**`submitFrame(dt)` order:**
1. prefs revision → theme (skipped under the `UI_THEME` override);
2. showing gate;
3. surface attach/retry/owns;
4. scale;
5. first frame only: `UI_KEYS` replay → `A11Y_DUMP`;
6. `panel_->tick(capture_.fixedDt > 0 ? capture_.fixedDt : dt)`;
7. `canvas_.begin(info)` → `panel_->draw(canvas_, theme_)` → `canvas_.end()`;
8. dump, if this is frame `CANVAS_DUMP_AFTER`;
9. `sink_.submit`;
10. a11y sync (structure on `a11yRevision()` change, values ≤ 10 Hz);
11. return `{true, panel_->wantsFullRate()}`.

The cursor is updated from `panel_->cursor()` after every input event.

**Diagnostics environment.** Read once into `CaptureConfig` at construction, as `FUNKGUI_ENV_PREFIX` + suffix. The
FCompressor names are shown:

| Variable | Effect | HR equivalent |
|---|---|---|
| `FCMP_CANVAS_DUMP=path`, `FCMP_CANVAS_DUMP_AFTER=n` | dump v2 of frame n; pins FramePump to its fallback clock | `HRVB_CANVAS_DUMP(_AFTER)` |
| `FCMP_UI_THEME=0\|1` | theme override, not persisted | `HRVB_UI_THEME` |
| `FCMP_UI_SCALE=s`, `FCMP_UI_SCALE_AFTER=n` | fake backing scale | `HRVB_UI_SCALE(_AFTER)` |
| `FCMP_UI_KEYS=spec` | key replay before the first frame; grammar = HR's + `home,end,pageup,pagedown` | `HRVB_UI_KEYS` |
| `FCMP_UI_FIXED_DT=sec` | the Panel ticks with this dt, so a live capture equals a headless frame | new |
| `FCMP_A11Y_DUMP=path` | a11y dump after the first frame (§5.6 format) | `HRVB_A11Y_DUMP` |
| `FCMP_PREFS_DIR=dir` | preferences sandbox | `HRVB_PREFS_DIR` |
| `FCMP_GPU_LOG=1` | logs attach, retry, owns, scale and overflow events | new |

FCompressor reads its product-level hooks through the same `funkgui::env()`, once, into `PanelOptions` (one set of
variables, K1 #10): `FCMP_UI_VIEW=<ViewSpec::id>` (`panel`, `chars.sidechain`, `chars.colour`, `modebrowser`,
`presetbrowser`; replaces Draft 2's `FCMP_UI_SCREEN`/`FCMP_UI_BROWSER`), `FCMP_UI_NO_HINT=1` (skip the first-run hint),
`FCMP_UI_NO_LIVE=1` (ignore telemetry, for parity captures) and `FCMP_PRESETS_DB`. `gui-live.sh` sets `FCMP_UI_VIEW`,
`FCMP_UI_FIXED_DT`, `FCMP_UI_NO_HINT` and `FCMP_UI_NO_LIVE`.

### 5.2 `ParamPort` and `GestureController` (Core)

```cpp
class ParamPort {                                    // one host parameter, as the widgets see it
public:
    virtual ~ParamPort() = default;
    virtual float value01() const = 0;               // host-normalised
    virtual float default01() const = 0;             // host default (NOT the Mode default: that is ValueModel's)
    virtual int   numSteps() const = 0;
    virtual void  beginGesture() = 0;
    virtual void  setValue01(float) = 0;             // setValueNotifyingHost
    virtual void  endGesture() = 0;
    virtual const char* id() const = 0;
    virtual void* native() const = 0;                // juce::RangedAudioParameter* (host menu); nullptr in fakes
};
class JuceParamPort final : public ParamPort { public: explicit JuceParamPort(juce::RangedAudioParameter&); /* … */ };

class GestureController {                            // the begin/end discipline of A §3.5, in one place per Panel
public:
    explicit GestureController(HostServices&);
    ~GestureController();                            // closeAll()
    void beginDrag(ParamPort&);                      // beginGesture + host.setUnboundedDrag(true)
    void dragTo(float host01);                       // clamp; write only when the bits change (ease::sameBits)
    void endDrag();                                  // endGesture + setUnboundedDrag(false)
    bool dragging() const;  ParamPort* dragParam() const;
    void tap(ParamPort&, float host01);              // begin/set/end; nothing at all if unchanged (HR :1712-1720)
    void tapMany(std::span<const std::pair<ParamPort*, float>>);   // composite writes, one bracket per param; the
                                                                     // product brackets the call with beginBatch/endBatch
    void wheelTo(ParamPort&, float host01, double nowSec);         // one gesture per burst (HR :1805-1848)
    void poll(double nowSec);                        // closes the wheel gesture after kWheelIdle
    void closeAll();                                 // drag + wheel; host closing the editor
    static constexpr double kWheelIdle = 0.5;        // HR kWheelIdleMs
};
```

### 5.3 `ValueModel` (Core): the five-state contract FCompressor implements

```cpp
enum class ValueState : uint8_t { continuous, stepped, locked, derived, na };

struct Detent {
    float       host01;        // canonical host-normalised value this detent writes (exact)
    const char* label;         // drawn: "4", "ALL", ".3", "COMP"
    const char* spoken;        // a11y: "4 to 1", "all buttons", "0.3 milliseconds"
};
struct ValueText { char value[24]; char unit[8]; char sub[40]; char spoken[64]; };   // no heap per frame (A §3.1)

struct ValueView {
    ValueState  state = ValueState::continuous;
    const char* label  = "";        // Mode term ("PEAK RED.")
    const char* aka    = nullptr;   // universal label when remapped ("THRESHOLD")
    const char* tag    = nullptr;   // "FIXED", "= RATIO", "AUTO", "+" (extension)
    const char* reason = nullptr;   // REQUIRED for locked/derived/na: footer + a11y help
    float track = 0, trackDefault = 0;                         // 0..1 on the drawn track
    bool  bipolar = false, atDefault = false, clamped = false, live = false;
    int   nDetents = 0, detent = -1;  const Detent* detents = nullptr;   // stepped, index space
    int   nNotches = 0;  const float* notches = nullptr;       // soft notches, track 0..1
    int   nEndLo = 0, nEndHi = 0;                              // hybrid: steps below lo / above hi (16 px end cells)
    const Detent* endLo = nullptr;  const Detent* endHi = nullptr;
    int   activeEnd = 0;                                       // 0 = inside [lo,hi]; -k = k-th low cell; +k = k-th high cell
    ValueText text{};
};

class ValueModel {
public:
    virtual ~ValueModel() = default;
    virtual uint64_t   key() const = 0;                        // changes whenever view() would; widgets re-read only then
    virtual void       view(ValueView&) const = 0;
    virtual ParamPort* port() = 0;                             // nullptr: display-only
    virtual float      host01FromTrack(float t) const = 0;     // continuous: track -> host01 (Mode sub-range + skew)
    virtual float      defaultHost01() const = 0;              // the Mode default: double-click, Delete
    virtual void       writeDetent(int i, GestureController&); // default: tap(port, detents[i].host01)
    // Absolute handle drags: plot value → host01. A product returns false when the plot value is not the parameter's
    // own value (then the handle drags relatively). FCompressor: threshold via T_in; knee/range only with
    // kFlagPlotIsPlain (§6.5, K1 #9).
    virtual bool       plotToHost01(float plotValue, float& host01) const { return false; }
};
```

`key()` caching replaces HR's `refreshCache` + `sameBits` (`:562-647`). A widget calls `view()` only when `key()`
changes, so formatting runs only on change.

### 5.4 `RuleSlider` (Core): HR's slot, plus detents and states

```cpp
enum class SlotSize : uint8_t { primary, secondary };
struct SlotGeom {
    float x, top, w;  SlotSize size;
    Rect  hit() const;       // {x-6, top-6, w+12, primary ? 80 : 66}
    float valueTop() const;  // top+18 (kValueP 24) | top+14 (kValueS 18)
    float subTop() const;    // top+46 | top+34     (kMicro: sub-readout | detent labels; clears the tallest caret, §6.1)
    float trackY() const;    // top+64 | top+54
};

class RuleSlider {
public:
    RuleSlider(ValueModel&, SlotGeom, uint32_t a11yId);
    void   setWord(AttachedWord*);                  // optional latch word, carved out of hit() (checked first)
    void   tick(float dt, bool hovered, bool focused, bool alwaysChrome);  // hover 90/160 ms, caret ease, flashes
    void   draw(Canvas&, const Theme&, bool focusRing) const;              // state table §8.1
    bool   contains(Point) const;
    int    detentLabelAt(Point) const;              // -1 unless stepped with labels drawn
    Cursor cursorAt(Point) const;
    void   pointerDown(const PointerEvent&, GestureController&, HostServices&);  // popup -> host menu, never a write
    void   pointerDrag(const PointerEvent&, GestureController&);
    void   pointerUp(const PointerEvent&, GestureController&);
    void   doubleClick(GestureController&);         // Mode default
    bool   wheel(const WheelEvent&, GestureController&, double nowSec);
    bool   key(const KeyEvent&, GestureController&);           // §8.9
    void   a11yAction(A11yAction, double value, GestureController&);
    void   accessibility(A11yItem&) const;          // §8.9 roles
    void   specLine(char* out, size_t n) const;     // footer text for hover/focus/drag
    void   flashLabel(float seconds);               // Mode-switch landing (§8.7)
    const ValueView& view() const;
};
```

**Input rules.**
- **Continuous drag.** HR's rule in **track** space: 240 px per full track (Shift 1200, Cmd 6000). Right or up
  increases. The anchor resets when a modifier toggles (`:1746-1772`). A Mode sub-range therefore gets full resolution.
- **Stepped drag.**
  - Pixels per detent: `p = clamp(240/(n−1), 24, 64)`.
  - A detent commits when travel from the current detent reaches `0.5·p + 6` (6 px hysteresis), then the anchor resets.
  - Modifiers are ignored. The pointer is hidden. One gesture covers the whole drag.
  - A ghost caret (1 px ink32) follows the raw travel (F §3.4).
- **Detent-label click.** Arms on down. If the pointer moves more than 3 px, it becomes a relative drag. Otherwise the
  jump is committed on up through `writeDetent`, and nothing is written if the detent is unchanged.
- **Wheel.**
  - Continuous: HR (`:1805-1848`) in track space. Step 0.025, or 0.005 with Shift. Non-smooth deltas ×4.
  - Stepped: **one detent per discrete wheel event**. Smooth deltas accumulate until `|acc| ≥ kWheelNotch`.
- **Hybrid** (`nEndLo`/`nEndHi` > 0): continuous rules inside `[lo, hi]`. Travel of `0.5·24 + 6 = 18` px past a range
  edge enters the adjacent end cell, which writes that detent. Wheel, keys and a11y cross the edge one cell at a time
  (§8.1).
- **Locked, derived and n/a** refuse every write gesture. The footer shows the reason instead, and the right-click host
  menu still works.

### 5.5 `SegmentedSelector`, `LatchToggle`, `AttachedWord`, `ThemeCells` (Core)

```cpp
class CellModel {                                   // index-based: setup cells, preferences, tabs, stepped params
public:
    virtual ~CellModel() = default;
    virtual int  count() const = 0;
    virtual int  active() const = 0;
    virtual const char* label(int) const = 0;
    virtual const char* spoken(int) const = 0;
    virtual bool enabled(int) const { return true; }
    virtual void select(int, GestureController&) = 0;    // tap semantics; no-op when unchanged
};
enum class CellStyle : uint8_t { text, boxed };     // text = HR theme cells (active ink70, hover ink100, rest ink32)
class SegmentedSelector {
public:
    SegmentedSelector(CellModel&, std::vector<Rect> cells, CellStyle, const char* caption, Point captionAt, uint32_t a11yId);
    void tick(float dt, Point pointer);  void draw(Canvas&, const Theme&, bool focusRing) const;
    int  cellAt(Point) const;  void pointerDown(const PointerEvent&, GestureController&);   // selects on down (HR :1709)
    bool key(const KeyEvent&, GestureController&);   // ←/→ Home/End; one Tab stop for the group
    void accessibility(std::vector<A11yItem>&) const;   // radioGroup + radioButton children
};

class ToggleModel {
public:
    virtual ~ToggleModel() = default;
    virtual bool on() const = 0;
    virtual bool enabled() const { return true; }
    virtual const char* reason() const { return nullptr; }    // shown when disabled
    virtual void set(bool, GestureController&) = 0;
};
class LatchToggle {        // HR Freeze: arm on down, commit on up inside, drag-off cancels (:1724, 1774-1788)
public:
    LatchToggle(ToggleModel&, Rect, const char* label, uint32_t a11yId);
    // off: ink16 fill / ink52 text; on: ink70 fill / ground text; armed: ink32 fill; pressed: accent text
};
class AttachedWord {       // F §3.5: kMicro word in a slot's label row, hit {x+w-34, top-5, 38, 18}
public:
    AttachedWord(ToggleModel&, const SlotGeom&, const char* word, uint32_t a11yId);
    // rest ink32, on ink100, hover ink70, pressed accent, disabled ink16 (refused; footer reason)
};
class ThemeCells;          // a SegmentedSelector(CellStyle::text) bound to UiPreferences::theme(); verbatim HR behaviour
```

### 5.6 Accessibility: `A11yItem` (Core), `A11yBridge` (Gpu)

```cpp
enum class A11yRole : uint8_t { slider, toggleButton, button, radioGroup, radioButton, comboBox,
                                listItem, staticText, image, progressBar };
enum class A11yAction : uint8_t { press, toggle, setValue, increment, decrement, showMenu, focus };
struct A11yItem {
    uint32_t id; uint32_t parent = 0; A11yRole role; Rect bounds;
    std::string title, description, help, value;       // value = the spoken current value
    double v = 0, lo = 0, hi = 1, step = 0;            // value interface; stepped = index space (F §8.2)
    bool enabled = true, readOnly = false, visible = true, checkable = false, checked = false;
};
std::string a11yDumpLine(const A11yItem&);   // "<role:2> <title:24> <x,y,w,h:20> value=<v>|checked|unchecked [ro] [off] help=<…>"
```

- `A11yBridge::sync(juce::Component& editor, const std::vector<A11yItem>&, Panel&)` keeps one invisible child
  `juce::Component` per id (HR `AccessibleItem`, `:249-424`). Each child has `setInterceptsMouseClicks(false,false)` and
  bounds equal to the item.
- The JUCE handler maps each item field:
  - `AccessibilityValueInterface` with `{lo, hi, step}` and `isReadOnly`;
  - `setEnabled` → `isAccessibilityEnabled`;
  - `setHelpText` → `accessibilityHelp`;
  - `setDescription`.
- Actions call `panel.a11yAction(id, …)`.
- **Index space for stepped sliders is mandatory.** JUCE's macOS increment is `current + interval`, so with HR's 0..1 /
  0.01 range VoiceOver's increment rounds back to the same detent and nothing happens (F §8.2).
- Structure syncs on `a11yRevision()` changes. Values sync at ≤ 10 Hz.

### 5.7 `DwellSelector`, `ScreenFader` and `ease` (Core)

```cpp
template <class V> class DwellSelector {    // HR view/target/shown/amt/dwell (:983-1019), generic
public:
    DwellSelector(V pinned, float tau = 0.18f, float dwell = 0.9f);
    void  pin(V, bool instant = false);      // tab click
    void  hold(std::optional<V>);            // while dragging
    void  touch(V);                          // wheel/key/a11y write: hold V for `dwell` s. Hover never calls this.
    void  tick(float dt);                    // amount eases with tau, snaps within 1e-3
    V     incoming() const;  V outgoing() const;  float amount() const;   // draw outgoing at 1-amt, incoming at amt
};
using ScreenFader = DwellSelector<int>;      // pin() only; tau 0.12 s (§7.1)

namespace ease {
float toward(float x, float target, float dt, float tau, float snap = 1e-3f);  // x += (t-x)*min(1, dt/tau), then snap
float hover (float h, bool on, float dt);                                        // 90 ms in, 160 ms out (HR :917-919)
float shown (float s, float target, float dt, float tau = 0.09f, float jump = 0.15f);  // big jumps snap (HR :925-928)
bool  sameBits(float a, float b);                                                // HR :118-122
}
```

### 5.8 Text utilities, `HintLine`, `FocusRing`, `MenuLook`, formatting (Core)

```cpp
namespace text {
float width (const FontAtlasSdf&, const char* utf8, const TextStyle&);                // == Canvas::textWidth, canvas-free
bool  fits  (const FontAtlasSdf&, const char* utf8, const TextStyle&, float maxW);
int   fitEllipsis(const FontAtlasSdf&, const char* utf8, const TextStyle&, float maxW, char* out, size_t n);  // U+2026
juce::String printable(const juce::String&);           // ASCII 32..126 (PresetPanel.cpp:66): typed names only
class LineEdit { public: bool key(const KeyEvent&, int maxLength); std::string buffer; int caret = 0; bool allSelected = false; };  // editKey :386
}
namespace fmt {                                         // fixed buffers; U+2212 minus
int db     (float v, int dp, char* out, size_t n);                    // "−18.0"
int seconds(float s, char* out, size_t n, const char** unit);         // µS < 1 ms ≤ MS < 1 s ≤ S
int percent(float v01, char* out, size_t n);
int hz     (float hz, char* out, size_t n, const char** unit);        // "120" HZ, "1.2" K
}
class HintLine {        // first-run hint, then the persistent spec line (HR :1556-1586); no tooltips anywhere
public:
    explicit HintLine(const char* firstRun, float seconds = 6.0f);
    void tick(float dt);  void pointerMoved();          // cuts the remaining hint to 0.4 s (HR :1668)
    void noteDown();      void noteMove();               // mouseDown with no mouseMove ever -> always-chrome (HR :1488)
    void skip();          bool active() const;  bool wantsFullRate() const;  bool alwaysChrome() const;
    void draw(Canvas&, const Theme&, float x, float y, float maxW, const char* spec) const;   // kLabel ink32
};
void drawFocusRing(Canvas&, Rect hit, Col accent);      // four hairlines on hit.reduced(1) (HR :1542-1551)
class MenuLook : public juce::LookAndFeel_V4 { public: explicit MenuLook(const Theme&); };  // PresetPanel.cpp:126
```

### 5.9 `UiPreferences` (Core), generalised

```cpp
class UiPreferences {       // machine-wide, per product (FUNKGUI_PREFS_FOLDER); never destroyed; write-through
public:
    static UiPreferences& get();
    int  theme() const noexcept;  void setTheme(int);
    int  getInt(const char* key, int fallback, int lo, int hi) const;
    void setInt(const char* key, int value);          // saves immediately, ++revision (HR setTheme semantics)
    void reload();                                    // on editor open (HR :149-151)
    uint32_t revision() const noexcept;
};
```

FCompressor's keys are `theme`, `meterScaleDb` ∈ {12, 24, 48, 72} (default 48) and `historySpanTenths` ∈ {25, 50,
100, 200} (default 50). Keeping these machine-wide follows F §4.2 and Pro-C.

### 5.10 `LiveFeed` and `SoftRaster` (Core)

```cpp
template <class Frame> class LiveFeed {              // HR :934-952 staleness logic; Frame has uint32_t publishCount
public:
    explicit LiveFeed(float staleAfter = 0.5f);
    template <class ReadFn> void poll(ReadFn&& read, float dt);   // read(Frame&) -> bool (the seqlock reader)
    bool live() const;  bool fresh() const;  float staleSeconds() const;  const Frame& frame() const;
};

struct Image { int w = 0, h = 0; std::vector<uint8_t> rgba; };
Image rasterise(const PrimList&, const FontAtlasSdf&, int supersample = 2);   // fs_ui.sc on the CPU, incl. AREA
bool  writePng(const Image&, const char* path);
```

`SoftRaster` gives agents a real picture without a GPU. `fcmp_probe_plugin ui.dump --view <id> --mode <key> --out x.dump
--png x.png` writes the frame it just checked.

### 5.11 What stays out of FunkGui

These stay out of FunkGui in v0.x:
- product layouts and formatters (ratio in the slope domain, Mode dials; 01 §4.6 `formatValue` does the text);
- the Mode browser and the band/Characteristics plots;
- `UiFrame`, `HistoryColumn`, `Seqlock` and `HistoryRing` (01 §6, in `fcdsp`), and the UI-side history store;
- the preset **strip and browser UI**.

The preset **data layer** is in FunkGui as `FunkPresets` (§1.2, per 01 §9.2). HR's `PresetPanel` is product-coupled
(A §6.5), so FCompressor draws its own strip and browser over `FunkPresets` with `LineEdit`, `TextFit` and `MenuLook`. A
generic `PresetPanel` widget is a candidate for FunkGui v0.3 or later, once HR migrates and two products need it.

---


# PART 2 — FCompressor UI

FCompressor-side code lives in `Source/editor/` (the canonical tree is 01 §2.1; namespace `fcmp::ui`):
- `gpu/Editor.{h,cpp}`: `class fcmp::ui::Editor : public funkgui::EditorHost`. GPU configuration only; constructed by
  `Source/plugin/CreateEditorGpu.cpp` (03 §1.1).
- `Panel.{h,cpp}`: `class fcmp::ui::Panel : public funkgui::Panel`. GPU-free. It is a **fixed composition of sub-views**
  and owns no drawing of its own beyond dispatch (K3 #15).
- `SubView.h`: the sub-view interface below.
- `Layout.h`: every constant in §6–§7, written **complete** by the first UI task (U1a) and then frozen (FZ4).
- `Tags.h`: product tags ≥ 256, complete and frozen with `Layout.h`.
- `SlotModel.{h,cpp}`: a `funkgui::ValueModel` over one `Pid`, fed by 01's `resolveView` and `formatParts`.
- `views/<Name>.{h,cpp}`, one sub-view or plot per file pair:
  - sub-views: `Header`, `DisplayRow`, `SlotGrid`, `Band`, `CharScreen`, `ModeBrowser`, `PresetStrip`,
    `PresetBrowser`, `Footer` (stub files exist from U1a, so later tasks edit only their own files);
  - plots: `HistoryPlot`, `TransferPlot`, `MeterColumn` (shared by the band and the Characteristics screen,
    parameterised by their rect and px/dB, so both draw **the same visual aids with the same code**),
    `ControlPathPlot`, `StepPlot`, `SidechainPlot`, `ColourPlot`, `Readouts`.
- `HistoryStore.h`, `PreviewWorker.{h,cpp}` (owned by the Panel; §9.3).
- The processor is reached only through **`ProcessorFacade`** (`Source/plugin/ProcessorFacade.h`, §9.5). Probes use
  `Tools/probes/plugin/FakeFacade.{h,cpp}` (in-memory ports, scripted `UiFrame`s, a real `HistoryRing`), so UI work never
  waits for the real processor (K3 #13).

```cpp
// Source/editor/SubView.h
namespace fcmp::ui {
struct SubView {                                    // one region of the fixed panel; no allocation per frame
    virtual ~SubView() = default;
    virtual void tick(float dt) = 0;
    virtual void draw(funkgui::Canvas&, const funkgui::Theme&) const = 0;
    virtual bool hit(funkgui::Point) const = 0;
    virtual void pointerDown(const funkgui::PointerEvent&) {}  virtual void pointerDrag(const funkgui::PointerEvent&) {}
    virtual void pointerUp(const funkgui::PointerEvent&) {}    virtual void doubleClick(const funkgui::PointerEvent&) {}
    virtual bool wheel(const funkgui::WheelEvent&) { return false; }
    virtual bool key(const funkgui::KeyEvent&) { return false; }
    virtual void accessibility(std::vector<funkgui::A11yItem>&) const = 0;   // ids = (subViewIndex << 16) | local
    virtual int  focusOrder(std::span<uint32_t> out) const = 0;             // this sub-view's Tab stops, in order
    virtual bool wantsFullRate() const { return false; }
};
}

// Source/editor/Panel.h — the probe-facing API (K1 #10, K3 #15)
namespace fcmp::ui {
enum class Screen  : uint8_t { panel, characteristics };
enum class Overlay : uint8_t { none, modeBrowser, presetBrowser };
struct ViewSpec { const char* id; Screen screen; ScTab tab; Overlay overlay; };   // ScTab: ProcessorFacade.h
// "panel", "chars.sidechain", "chars.colour", "modebrowser", "presetbrowser" — the G1 golden views
std::span<const ViewSpec> views() noexcept;
struct PanelOptions {
    bool skipHint    = false;   // probes; FCMP_UI_NO_HINT=1
    bool syncPreview = false;   // step responses computed inside tick() (determinism rule 7, §3.7)
    bool ignoreLive  = false;   // draw as if telemetry were stale; FCMP_UI_NO_LIVE=1
};
class Panel final : public funkgui::Panel {
public:
    Panel(ProcessorFacade&, PanelOptions);
    ~Panel() override;                                       // shutdown() if not yet done
    void setView(const ViewSpec&, bool instant = true);      // probes and FCMP_UI_VIEW; users use the latch/browsers
    void shutdown();                                         // stop PreviewWorker, then closeGestures() (§5.1 teardown)
    // funkgui::Panel overrides: attach, width (960), height (640), tick, idle, draw, wantsFullRate, pointer*, wheel, key,
    // cursor, accessibility, a11yRevision, a11yAction, closeGestures — each dispatches to the sub-views in fixed order
};
}
```

Probes settle a view with `funkgui::HeadlessHost::settle()` and record it into a `funkgui::Canvas` (03 §3.6). One dump
command serves agents and the lead: `fcmp_probe_plugin ui.dump --view <id> --mode <key> --out x.dump [--png x.png]`.

Every parameter fact the UI shows comes from 01: `kHostParams`, `ModeDescriptor`, `ParamSpec`, `resolveView →
ParamView`, `formatParts`/`formatValue`, and the analysis API. The UI never re-derives a parameter rule.

---

## 6. The main panel (960×640, screen PANEL)

### 6.1 Grid

| Constant | Value |
|---|---|
| Window | 960×640 logical, fixed (user decision). Margins 40. Content x 40–920. `setResizable(false, false)`, then `setSize` exactly once |
| Chrome (never moves, both screens) | header y 0–60, display row y 64–120, footer text y 604 |
| Middle region (swaps, §7.1) | y 124–600 |
| Slot columns (7) | `x_i = 40 + 128·i`, i = 0…6 (40, 168, 296, 424, 552, 680, 808). Track width `w = 112`. Hit `{x−6, top−6, 124, h}`, which leaves a **4 px dead gutter** (HR's ≥ 4 px rule, A §3.1) |
| Primary row P, top 362 | label 362 (kLabel), value 380 (kValueP 24), sub 408 (kMicro), track 426. Hit y 356–436 |
| Secondary row B, top 450 | label 450, value 464 (kValueS 18), detent/sub line 484 (kMicro), track 504. Hit y 444–510 |
| Secondary row C, top 522 | label 522, value 536, detent/sub line 556, track 576. Hit y 516–582 |
| Band | captions y 126. Plots y 140–332 (192 px). State lane y 334–337. Axis labels y 340 |
| Shared dB map (band) | `y(db) = 140 + (6 − db)·192/S`, with S = `meterScaleDb` ∈ {12, 24, 48, 72} → 16 / 8 / **4** / 2.67 px/dB. The top is +6 dBFS, the floor 6 − S |

The secondary rows put the detent line at +34 and the track at +54, where F's anatomy had the line at +38. The reason:
the tallest caret top (`track + 3.5 − 11`) must clear the kMicro line. Primary: 56.5 ≥ 56 ✓. Secondary: 46.5 ≥ 44 ✓.

### 6.2 Wireframe (Mode = Bus G per 01 §10.2, default 48 dB scale)

```
x→ 40          138    228                                    600 624   680                             896 920
   ┌──────────────────────────────────────────────────────────────────────────────────────────────────────┐
 18│ F COMPRESSOR        ‹ DRUM BUS 02                    ☆ ›    MODE ‹ BUS G                          › │ header 0–60
 40│ VCA · FEEDFORWARD · PEAK   BUS · DRUMS · MODIFIED              VCA · 2 OF 8                         │
   ├──────────────────────────────────────────────────────────────────────────────────────────────────────┤
 66│ GAIN REDUCTION                      QUALITY    ECO [STD] HQ     ┌───────┐┌────────┐┌───────────────┐ │ display 64–120
 78│ −4.2 DB     IN −8.1 · OUT −11.9     LOOKAHEAD [OFF] 5 MS 20 MS  │ DELTA ││ BYPASS ││CHARACTERISTICS│ │ latches 72–108
   │ (kDisplay 44, signal)                                           └───────┘└────────┘└───────────────┘ │
126│ HISTORY                          2.5 [5] 10 20 S  TRANSFER     12 24 [48] 72 DB   −9.1   6.2  −1.0  │ band captions
140│ ┌──────────────────────────────────────────────────────┐ ┌──────────────────────┐   ▐▐    █    ▐▐    │
   │ │▔▔▔▔╲__╱▔▔▔▔▔╲___╱▔▔▔ GR (signal) hangs from y 140    │ │                    ·╱│   ▐▐    █    ▐▐    │
164│ │0 DBFS ·················································· │ │··················╱···│   ▐▐         ▐▐    │ 0 dBFS
   │ │   ▁▂▃▅▃▂▁▂▅▆▅▃▂  IN area (ink16) · OUT line (ink70)    │ │         __●'  ║needle │                    │
236│ ├━━━━━━━━━━━━━━━━━━━━ threshold −18 (ink32) ━━━━━━━━━━━━━━━━━━━━━━━━━━━◇         │                    │ line → handle
   │ │                                                      │ │      ╱ ⌐= RATIO¬     │                    │
332│ └──────────────────────────────────────────────────────┘ └──────────────────────┘                    │ floor −42
334│ ▬▬ ▬ ▬▬▬▬ ▬  state lane                                                                               │
340│ −5 S      −4        −3        −2        −1         0    −36   −24   −12    0 PK    IN     GR    OUT    │
362│ THRESHOLD      RATIO           KNEE     = RATIO ATTACK          RELEASE         MAKEUP     AUTO MIX     + │ P
380│ −8.0 DB        4:1             6 DB            3 MS            AUTO            +4.0 DB         100 %     │
408│ DET −14.2       2    4    10   FOLLOWS RATIO   .1 .3 1 3 10 30  .1 .3 .6 1.2 AUTO                DRY −INF │
426│ ────┸─┃──────  ──┴───╂───┴──   ─────▯───────   ┴─┴─╂─┴──┴──┴   ┴──┴──┴──┴───╂   ─────┃─────    ────────┃ │
450│ RANGE        + DETECT       EXT HOLD            LOOKAHEAD       TIME MODE       DRIVE        + VOICE      │ B
464│ OFF            PEAK            –               –               –               0.0 DB          VCA       │
484│                FIXED · PEAK                                                                     VCA CLEAN │
504│ ─────────────┃ · · · · · · · ·                                                 ──────┃─────    ─╂─────┴─ │
522│ SC HPF  LISTEN SC EMPH         LINK          + STEREO        + S2 THRESH       S2 ATTACK       S2 RELEASE│ C
536│ OFF            –               100 %           ST              –               –               –         │
556│ +                                                              ST     M/S                                │
576│ ┃────────────                  ────────────┃   ─╂──────┴──                                               │
604│ RATIO   STEPS 2 · 4 · 10   DRAG / WHEEL / ARROWS STEP   CLICK A STEP   DBL-CLICK RESET  GRAPHITE PAPER │ footer
640└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

Character columns are approximate. The coordinates in §6.3 are exact.

### 6.3 Coordinates

| Element | Rect / position | Style |
|---|---|---|
| Wordmark | "F" at (40, 18); "COMPRESSOR" at x = 40 + w("F") + 9 ≈ 55, ending ≈ 138 | kWordmark; ink100 / ink52 |
| Topology caption | (40, 40): `ModeDescriptor::topologyLine` | kCaption ink32 |
| Preset strip | `{228, 12, 372, 44}`: HR's strip geometry (`PresetPanel.h:14-18`) moved 24 px right and 2 px up. FCompressor's `PresetStrip` over `FunkPresets` | as HR |
| Mode latch | caption "MODE" right-aligned at x 648, top 22; ‹ hit `{656,14,24,32}`; name cell `{680,14,216,32}`, text at x 688 on `capCentreTop(30)`; › hit `{896,14,24,32}`; group line (688, 48) | caption kCaption ink52; name kLatch ink100; group kMicro ink32; chevrons = 2 segments each |
| Display caption / value | (40, 66) / (40, 78); unit on the shared baseline | kCaption ink52 / kDisplay 44 + kUnit 14 |
| Display sub-readout | left at x 224, cap-centred at y 100, max width 148 | kLabel ink32 |
| QUALITY (`quality`) | caption (380, 66); cells ECO `{446,62,32,16}`, STD `{482,62,32,16}`, HQ `{518,62,26,16}` | kCaption; `CellStyle::text` |
| LOOKAHEAD (`labudget`) | caption (380, 92); cells OFF `{446,88,32,16}`, 5 MS `{482,88,38,16}`, 20 MS `{524,88,44,16}` | same |
| DELTA (`delta`) / BYPASS (`bypass`) / CHARACTERISTICS (UI state) | `{588,72,88,36}` / `{684,72,96,36}` / `{788,72,132,36}` | kLatch 13 (the "CHARACTERISTICS" label is 116.6 px wide inside 132) |
| HISTORY | plot `{40,140,500,192}`: 250 columns × 2 px. Caption (40,126). Span cells right-aligned to 540 ("2.5 5 10 20 S") | §6.5 |
| TRANSFER | plot `{556,140,192,192}`, square. Caption (556,126). Scale cells right-aligned to 748 ("12 24 48 72 DB"). `x(db) = 556 + (db − 6 + S)·192/S` | §6.5 |
| METERS | IN L/R `{783,140,6,192}` `{791,140,6,192}`; GR `{837,140,10,192}`; OUT L/R `{887,140,6,192}` `{895,140,6,192}`. Centres 790 / 842 / 894. Hold readouts at y 126, labels at y 340 | §8.8 |
| State lane | `{40,334,500,3}` | §6.5 |
| Slots | §6.1 grid, assignment in §6.4 | §8.1 |
| Attached word | `{x+w−34, top−5, 38, 18}`, carved out of the slot hit (hit-tested first) | kMicro |
| Footer spec line | (40, 604), max width 740 | kLabel ink32 |
| THEME cells | GRAPHITE `{790,601,72,16}`, PAPER `{866,601,54,16}` | kCaption, `ThemeCells` |
| Mode browser / preset browser overlay | `{40,64,880,286}` (y 64–350). It covers the display row and the band and leaves every slot row visible | §8.6 |

### 6.4 Slot assignment: 01's superset, laid out once (answers 01 §12.1)

| Row·col | x | Universal label | `Pid` | Word (`Pid`) | Primary sub-readout (live; absent when not live) |
|---|---|---|---|---|---|
| P0 | 40 | THRESHOLD | `thr` | – | `DET −14.2`: `curveXDb` (input-referred), plus a 1×4 ink52 detector tick under the track |
| P1 | 168 | RATIO | `ratio` | – | `EFF 2.7:1` (`analysis::localRatio` at `curveXDb`), or detent labels |
| P2 | 296 | KNEE | `knee` | – | `−21…−15 DB` (T_in ± W/2), detent labels, or the derived reason |
| P3 | 424 | ATTACK | `atk` | – | `EFF 0.8 MS` (`attackNowMs`, `kFlagProgram` Modes), or detent labels |
| P4 | 552 | RELEASE | `rel` | – | `EFF 340 MS` (`releaseNowMs`), or detent labels |
| P5 | 680 | MAKEUP | `makeup` | **AUTO** (`automu`) | `AUTO +4.1` = `makeupEffDb − resolved makeup` while `automu` is on |
| P6 | 808 | MIX | `mix` | – | `DRY −6.0 DB` |
| B0 | 40 | RANGE | `range` | – | (secondary: detent/sub line only). GR-used bar on the track: 1 px `signal` from 0 to the live GR |
| B1 | 168 | DETECT | `det` | **EXT** (`extkey`) | |
| B2 | 296 | HOLD | `hold` | – | |
| B3 | 424 | LOOKAHEAD | `look` | – | While `labudget` = OFF, **`resolveView` itself** returns `look` as locked at 0 with the reason `LOOKAHEAD BUDGET IS OFF — SET 5 MS OR 20 MS (ADDS LATENCY)` (01 §4.4, `RawParams::budget`; K1 #8). No UI overlay: the slot, host text and engine agree |
| B4 | 552 | TIME MODE | `tmode` | – | Directly under RELEASE. It is always a slot. FET 76 relabels it `GR` (ON/OFF: the hardware's attack-knob OFF detent, 01 §10.5) |
| B5 | 680 | DRIVE | `drive` | – | Under MAKEUP: gain staging |
| B6 | 808 | VOICE | `voice` | – | |
| C0 | 40 | SC HPF | `schpf` | **LISTEN** (`listen`) | |
| C1 | 168 | SC EMPH | `sce` | – | |
| C2 | 296 | LINK | `link` | – | |
| C3 | 424 | STEREO | `stmode` | – | |
| C4 | 552 | S2 THRESH | `s2thr` | – | |
| C5 | 680 | S2 ATTACK | `s2atk` | – | |
| C6 | 808 | S2 RELEASE | `s2rel` | – | |

The other globals have fixed chrome places: `mode` (header), `delta`, `bypass`, `quality` and `labudget` (display row).
All **29** parameters of 01 §3.1 are placed. No compound cell is needed.

**Word states** (K1 #23). A word follows its parameter's resolved state: **hidden** when the parameter is n/a in the
Mode (AUTO in Bus G, FET 76, Opto 2A, Mu 67, Diode 609 and Brickwall); **disabled** (ink16, refused, footer = reason)
when it is locked; otherwise rest/on/hover/pressed as §5.5. EXT and LISTEN are globals and are always enabled; EXT is
drawn disabled with the reason `NO SIDECHAIN BUS CONNECTED` while the host has no active key bus.

**Label budget.** `width(label, kLabel) + (word ? width(word, kMicro) + 8 : 0) ≤ 112`. kLabel is 6.6 px per character;
kMicro is 5.945 px per character, minus 1.4 on the last.
- A Mode label (`ParamSpec::label`) can therefore be ≤ 17 characters without a word, and ≤ 12 with AUTO, EXT or LISTEN.
- **Tag placement.** When a slot has both a word and a tag (`FIXED`, `+`, `= RATIO`), the tag moves to the start of the
  sub/detent line.
- `ui.textfit.<key>` (C's G6) lints both rules per Mode, plus the width of every `ParamSpec::brief` (≤ 18 glyphs).

**States come straight from 01 §10.2.** That matrix is the UI's state table: L → continuous, S → stepped, H → hybrid, X →
locked, D → derived, – → n/a, + → extension marker, *NAME* → label + `DisplayMap`. Across the first eight Modes every
state and marker occurs on this one layout:
- continuous: Clean.
- stepped: Bus G ratio.
- hybrid: none of the first eight since FET 76's OFF moved to `tmode` (K2 #4); the FunkGui gallery exercises it (§3.11).
- locked: Opto 2A attack ~10 ms (`kFlagProgram`).
- derived: Bus G knee `= RATIO`.
- n/a: Opto 2A range.
- extension: Bus G mix.
- renamed + inverted: FET 76 INPUT over `thr`.
- dependent list: Clean drive, which becomes n/a while VOICE = OFF.

### 6.5 The band (always visible on PANEL)

**HISTORY** `{40,140,500,192}`: time runs leftwards, and "now" is x 540.
- **Columns.** 250 columns × 2 px. A column covers span/250 = 10 / 20 / 40 / 80 ms of audio. The columns are rebuilt
  every frame from the UI `HistoryStore` of 1 ms `HistoryColumn`s (§9.6), and the column phase scrolls smoothly.
- **Traces:**
  - `HIST_IN`: AREA from the floor to `max(inPeakDb)`; fill ink16, top stroke 1 px ink32.
  - `HIST_OUT`: stroke-only AREA at `max(outPeakDb)`, 1 px ink70.
  - `HIST_GR`: hangs from y 140 to `max(grMaxDb)`; fill `premix(ground, signal, 0.18)`, **bottom** stroke 1.5 px
    `signal`.
  - `HIST_DET`: stroke-only at `max(detMaxDb)`, 1 px ink52. Drawn **only when it can differ from IN**: SC HPF on, SC EMPH
    ≠ 0, `detectorLaw` ≠ peak, `kUiExtKeyActive`, link < 1, or `kUiTopoFB` (F §4.3).
  - All four are `live`.
- **One level axis for HISTORY and TRANSFER.** Both plot **plugin-input-referred** levels in the Mode's detector law,
  which is 01 §7's axis contract.
  - The threshold line and handle sit at `T_in = eng.thrDb − eng.preGainDb` (01 §4.2's own definition of "effective
    input threshold").
  - So the line always runs into the handle. When FET 76's INPUT rises, `T_in` falls and the handle moves left, which is
    the 1176 mental model.
- **Threshold line** (`THRESHOLD_MARK`): `hairlineH` at `snapY(y(T_in))` from x 40 to the TRANSFER handle, crossing the
  16 px gap. Ink32; accent while THRESHOLD is under the hand (slot, handle or line).
- **Grid.** ink16 hairlines every 12 dB (every 6 dB at S ≤ 24). 0 dBFS is always drawn. "0 DBFS" in kMicro ink32 at
  (44, y(0)−12). Time labels at y 340, every 100 px.
- **State lane** `{40,334,500,3}` (`STATE_LANE`): the phase from `bits` b0–1 at each column's max-GR sample. ATTACK
  ink70, HOLD ink100, RELEASE ink32, idle draws nothing. Runs are merged, so it is ≤ 40 rrects.
- **Audio time, not wall time.** HISTORY advances only when columns arrive. When `LiveFeed` goes stale (0.5 s; or
  `kUiLive` clear) it holds and dims to 50 % over 0.4 s. A lap or an attach gap (`bits.b5`) leaves an empty span, never
  interpolated data.
- **Press and hold** anywhere except the threshold line freezes the view. An ink52 cursor line follows the pointer, and
  the display row shows that column (`AT −1.24 S · GR −3.8 · IN −8.1 · OUT −11.9`). Releasing resumes.
- A vertical drag on the threshold line (±4 px) writes THRESHOLD through its `ValueModel` (UpDown cursor).

**TRANSFER** `{556,140,192,192}`: square, so unity is exactly 45°.
- **Unity** is 1 segment ink16. The grid is ink16 hairlines every 12 dB on both axes.
- **Static curve** (`TRANSFER_CURVE`): `y = x + gainDb(x) − eng.preGainDb`, with `gainDb` from
  `analysis::staticGain(entry, eng, x)`. That is GR only, so unity = no reduction and needle length = GR. It is
  pre-makeup and pre-mix (F §4.4).
  - Sampling: 64 points uniform over the axis, + 32 inside [T_in ± W/2], + 16 around the range break. Custom/FB families
    use 120 uniform points. This meets C's G2 chord error ≤ 0.25 px.
  - Style: ink70 1.0 px at rest; **accent 1.5 px** while THRESHOLD, RATIO, KNEE or RANGE (slot or handle) is under the
    hand. Always opaque pre-mixed colours.
- **GR wedge** (`GR_WEDGE`): 48 AREA columns of 4 px between unity and the curve where GR > 0, fill ink16.
- **Knee marks** (`KNEE_MARK`): two ink16 `hairlineV` at x(T_in ± eng.kneeDb/2) from the curve to the floor, plus a floor
  bracket at y 328 with 1×4 ink32 end ticks. Label `KNEE 6` (kMicro ink32), or the spec tag (`FIXED`, `= RATIO`).
  **Nothing is drawn when `knee` is n/a** (absent, not faked).
- **Net curve** (`NET_CURVE`): `y = x + analysis::netGainDb(gainDb, makeupEffDb, mix)`, ink32 1 px. It includes
  pre-gain and makeup. Drawn iff it differs from the static curve by > 0.05 dB anywhere. Accent while MAKEUP, MIX or
  DRIVE is under the hand.
- **Ghosts** (`GHOST_CURVE`): for a stepped RATIO or KNEE, the other detents' curves in ink16, computed with the detent's
  plain value substituted into a copy of `RawParams`. Hovering a detent label previews that curve in ink32. After a Mode
  switch the previous Mode's curve stays in ink16 for 0.9 s.
- **Target dot** (`TARGET_DOT`, E §6.2): a ring of r 2.5, border 1, ink52, at (x(cx), y(cx − targetGrDb)).
- **Operating dot** (`OP_DOT`, live only): a disc of r 3.5, ink100, at (x(cx), y(cx − appliedGrDb)), where `cx` =
  `curveXDb` of the lane with the larger GR.
  - It is drawn hollow while `kUiFading` is set.
  - `GR_NEEDLE`: a 2 px `signal` rrect from unity y(cx) down to the dot. It has the same length as the GR meter and the
    HISTORY GR depth.
  - `OP_TRAIL`: the last 320 ms of the store sampled every 10 ms (`detMaxDb`, `grMaxDb`), drawn as a 1 px polyline
    fading ink70 → ink16 in pre-mixed colours.
  - None of these are drawn when not live.
- **Handles.** They are drawn while the pointer is over the plot or dragging (hover 90/160 ms), and always under
  always-chrome:
  - threshold: a 7×7 ring on unity at x(T_in);
  - knee: two 5×5 rings at x(T_in ± W/2) on the curve;
  - ratio: a 5×5 ring on the curve at x = min(T_in + 12, +3);
  - range: a 5×5 ring at the break point, if it lies inside the plot.
  - Locked or derived → a 1 px ink32 cross that cannot be dragged. N/a → no handle.
  - **Threshold** drags are absolute in the plot's own units (K1 #9): the handle sits at
    `T_in = analysis::inputThresholdDb(eng)`, which is affine in `thr` with slope 1 in every Mode (registry lint
    `thr.slope`), so the drag writes `thr_new = thr_cur + (T_target − T_cur)` — correct for FET 76's
    `thrOffset[ratio]` and for inverted dials alike.
  - **Knee and range** drags are absolute only when the spec has an identity `DisplayMap` and `kFlagPlotIsPlain`
    (Clean). Otherwise (Mu 67's DC THRESH, Bus 25's stepped knee) they are relative at 240 px per full track.
  - Stepped parameters snap with 6 px hysteresis at the handle.
- **Labels.** dB values at y 340 under x(−36/−24/−12/0) for S = 48, plus the unit and detector law at the right end
  (`0 DB PK`, `0 DB RMS`, `0 DB TP`), taken from `detectorLaw(eng)`. For `DetectorLaw::custom` the axis is labelled
  with the active `det` step label (`0 DB T4`, `0 DB TUBE`; K1 #28).
- **Double-click on empty band area** (not on a handle, line or trail) opens CHARACTERISTICS.

**METERS** are specified in §8.8.

### 6.6 Display row and footer

> **S12 revision (UF1b, ADR-68/68a).** The footer carries ZOOM cells beside THEME: a caption at (606, 605) and cells
> 100/125/150/175 at x 638/674/710/746 (32 × 16, y 601, 4 px apart). A step that does not fit the display is drawn
> in ink16 and refuses clicks, and its hover hint reads "<n> % NEEDS A LARGER DISPLAY". The footer spec line narrows
> from 740 to 554 px (`layout::footer::kSpecLineW`). The THEME spec text is "THEME   GRAPHITE · PAPER   MACHINE-WIDE,
> NOT SAVED WITH THE SESSION".

- **Display value precedence** (HR `:983-987`, 0.9 s dwell):
  1. the dragged slot or handle;
  2. else the hovered one;
  3. else the one touched within the last 0.9 s;
  4. else **GAIN REDUCTION**.

  The GAIN REDUCTION default shows the applied GR in `signal` when live and > 0.05 dB, `0.0` in ink32 at zero, and `–`
  in ink16 when not live. The sub-readout shows `IN −8.1 · OUT −11.9`. For a slot, the sub shows the universal name
  when renamed (`THRESHOLD −28 DB` under INPUT), `STORED …`, or `CLAMPED FROM …` (`kClamped`).
- **Footer**, in priority order:
  1. the first-run hint (6 s) `DRAG A VALUE OR THE CURVE. DOUBLE-CLICK TO RESET.`;
  2. a state notice (10 s after load, 01 §9.1): `SESSION FROM A NEWER FCOMPRESSOR — LOADED BEST EFFORT` or `MODE 'X' IS
     RETIRED — LOADED 'Y'`; also `kUiPoisonReset` → `AUDIO RESET AFTER A NON-FINITE SAMPLE` (3 s);
  3. the Mode-switch summary (3 s, §8.7);
  4. the spec line of the hovered, focused or dragged item.

  Example: `RATIO   STEPS 2 · 4 · 10   DRAG / WHEEL / ARROWS STEP   CLICK A STEP   DBL-CLICK RESET   RIGHT-CLICK
  MENU`. For a locked, derived or n/a item: `KNEE   = RATIO   FOLLOWS THE RATIO SWITCH (SOFT AT 2, HARD AT 10)   STORED
  6.0 DB (USED BY OTHER MODES)`. The line is fitted with `fitEllipsis` to 740 px. There are no tooltips anywhere (A §5.2
  #14).
- **A lookahead Mode with `labudget` = OFF:** whenever `desc.wantsLookahead && budget == off` (Brickwall today; keyed on
  the descriptor, never on a Mode name, K1 #35), the footer keeps `<MODE> WITHOUT LOOKAHEAD CAN OVERSHOOT — SET
  LOOKAHEAD 5 MS ABOVE (+5 MS LATENCY)` whenever the pointer is in the band or on LOOKAHEAD. Latency is never changed
  implicitly.

---

## 7. The Characteristics screen (screen CHARACTERISTICS)

### 7.1 Entering, leaving, and what stays

- **Enter:** click the `CHARACTERISTICS` latch, press Return or Space when it is focused, use its a11y press, or
  double-click an empty part of the band. **Leave:** the same latch (shown ON: ink70 fill, ground text), or **Esc**.
  Esc works in this order: close an open browser → hide the focus ring → leave the screen.
- **What stays:** the whole chrome, in identical pixels.
  - Header: wordmark, preset strip, Mode latch.
  - Display row: big readout, QUALITY, LOOKAHEAD, DELTA, BYPASS, the latch.
  - Footer: spec line, THEME.

  So the Mode can be switched and bypass toggled while watching the internals, and the toggle never moves. **The slot
  rows are not drawn.** Every plotted parameter has a handle or marker that proxies its slot's `ValueModel`, with
  identical behaviour and identical a11y value (§7.4).
- **Transition.** The middle region (y 124–600) crossfades with `ScreenFader` (τ 0.12 s, snap at 1e−3). Input goes to
  the target screen immediately. The outgoing layer draws text and rects at 1−a, and its curves use `premix` toward the
  ground. The worst frame is 6.9K prims (§4.5).
- **Persistence.** The screen is per-instance UI state: 01 §9.1's `<UI charExpanded="0|1" scTab="sidechain|colour"/>`,
  held in `fcmp::UiState` (§9.5). They are not parameters and not preset content. The default is PANEL.
  `FCMP_UI_VIEW` forces a view for captures.
- **Cost gating.** `PreviewWorker` (the step responses) runs only while the target screen is CHARACTERISTICS. With
  `PanelOptions::syncPreview` the same work runs inside `tick()` (§3.7 rule 7).

### 7.2 Wireframe

```
x→ 40                                        460 476               716 732   796 812           920
  0 ┌──────────────────────────── header (identical to PANEL) ─────────────────────────────────────┐
 64 │ GAIN REDUCTION −4.2 DB  IN … · OUT …   QUALITY …  LOOKAHEAD …  │DELTA││BYPASS││CHARACTERISTICS▮│  (latch ON)
126 │ HISTORY          2.5 [5] 10 20 S        TRANSFER   12 24 [48] 72 DB   METERS  READOUTS          │
140 │ ┌───────────── HISTORY 420×240 ──────────────┐ ┌── TRANSFER 240×240 ──┐ ┌──────┐ ┌────────────┐│
    │ │▔▔╲_╱▔▔▔╲__╱▔▔ GR (signal)                   │ │                   ·╱ │ │IN SC │ │DET   −14.2 ││
170 │ │0 DBFS ········································ │ │·················╱····│ │GR OUT│ │OVER   +3.8 ││
    │ │   IN area · OUT line · DET line (ink52)     │ │ stage-1 curve __●'   │ │ ▐ ▐  │ │TARGET  5.1 ││
260 │ ├━━━━━━━━━━━━ threshold ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━◇ ghosts   │ │ ▐ █  │ │APPLIED 3.8 ││
    │ │                                              │ │    ╱ ⌐KNEE 6¬        │ │      │ │EFF 3.2:1   ││
380 │ └──────────────────────────────────────────────┘ └──────────────────────┘ └──────┘ │MEMORY 42 % ││
383 │ state lane                                                                         └────────────┘│
390 │ −5 S     −4      −3      −2      −1       0     −36  −24  −12   0 PK    IN SC GR OUT               │
408 │ CONTROL PATH · GR                                ATTACK       RELEASE    [SIDECHAIN] COLOUR        │
422 │ ┌───────────── CONTROL PATH 420×160 ──────────┐ ┌─ 116 ──┐ ┌─ 116 ──┐ ┌──── 188×160 ────────────┐ │
    │ │ TARGET (ink52) · APPLIED min/max (signal)   │ │TGT 7.5  │ │AFTER    │ │ INTERNAL                 │ │
    │ │ GR 0 … S/2 dB over 80 px                     │ │  +24    │ │ 50MS/2S │ │   ___----‾‾‾‾‾‾‾‾‾‾‾‾   │ │
502 │ │                                              │ │ ╲╲+12   │ │ ╲___    │ │  ╱  ○ 120 HZ (handle)    │ │
506 │ │ phase lane                                   │ │  ╲╲ +6  │ │  ╲  ╲__ │ │ ╱                        │ │
514 │ │ internal lane: OPTO MEMORY     0–100 %       │ │  │spec  │ │   spec  │ │                          │ │
550 │ │ events: AUTO SLOW · RANGE · STAGE 2 · FADE   │ │  ○meas  │ │         │ │                          │ │
582 │ └──────────────────────────────────────────────┘ └─────────┘ └─────────┘ └──────────────────────────┘ │
586 │ −5 S … 0                                         .01 1 100MS 1MS .1 10S  20   200   2K   20K HZ     │
604 │ footer (identical)                                                                                    │
```

### 7.3 The panels

The level row shares one map: `y(db) = 140 + (6 − db)·240/S`, which is **5 px/dB at S = 48** (F's band scale). Its
floor is at y 380.

| Panel | Rect | Content |
|---|---|---|
| **HISTORY** (large) | `{40,140,420,240}`; captions y 126; time labels y 390 every 84 px | The band's traces, plus `HIST_DET` **always** drawn. 210 columns × 2 px (span/210 per column, fractional windows over 1 ms entries). The threshold line runs into the TRANSFER handle. Press-and-hold freeze, line drag. State lane `{40,383,420,3}` |
| **TRANSFER** | `{476,140,240,240}`; `x(db) = 476 + (db − 6 + S)·240/S` | Everything in the band's TRANSFER. Two-stage Modes (`stage2 ≠ none`) also draw the **stage-1-only** curve in ink52 (`STAGE_CURVE`): `staticGain` with `CurveOpts{.stage2 = false}`, D §7 |
| **METERS** | `{732,140,64,240}`: bars IN `{734,…,8,240}`, SC `{750}`, GR `{766}`, OUT `{782}`; labels y 390 | Each bar is the max over lanes, with §8.8 rules. SC = `scPeakDb` while `kUiExtKeyActive`, else `–` |
| **READOUTS** | `{812,140,108,240}`: 18 rows at 13 px, kMicro; name ink52 left, value ink100 right-aligned at 920 | Rows: 1 DET (`curveXDb`), 2 OVER (`curveXDb − T_in`), 3 TARGET, 4 APPLIED, 5 S2 GR, 6 EFF RATIO (`localRatio`), 7 ATK EFF (`attackNowMs`), 8 REL EFF (`releaseNowMs`), 9 CREST (`crestDb`), 10 PHASE (flags bits 16–19); 11–18 are `internals[0..7]` with name, unit and decimals from `ModeDescriptor::internals`. Rows show the lane with the larger applied GR. When link < 1 and the lanes differ by > 0.1 dB, DET and APPLIED show `L/R` pairs (`−14.2/−15.0`), or `M/S` under `kUiMidSide`. `–` when not live |
| **CONTROL PATH** | `{40,422,420,160}`; x-aligned with HISTORY (same 210 columns); time labels y 586 | The detector/envelope internals, built from 01's 32 B `HistoryColumn` alone. **GR lane** y 422–502: 0 at top, **0…S/2 dB over 80 px** (3.33 px/dB at S = 48), hanging. Traces: `CP_TARGET` = `tgtMaxDb` (stroke-only ink52, bottom edge); `CP_APPLIED` = area to `grMaxDb` (`premix(ground, signal, 0.18)` + 1.5 px `signal` bottom stroke), plus a 1 px ink32 stroke-only line at `grMinDb`, so intra-millisecond GR movement is visible as the band between them. The gap between target and applied *is* the ballistics. **Phase lane** y 506–509. **Internal lane** y 514–544: `internal0`, the Mode's one history-flagged internal (01 §4.3), normalised `lo…hi`, stroke-only ink70, with name and live value in kMicro inside the lane's top-left. A Mode with none shows `NO HISTORY INTERNAL` in ink32. **Events lane** y 550–582: four 5 px stripe rows from `bits`, each labelled in kMicro ink32 at x 44 — AUTO SLOW (b2, ink52), RANGE (b3, ink70), STAGE 2 (b4, ink100), FADE (b6, ink32); runs merged |
| **STEP RESPONSE** | ATTACK `{476,422,116,160}`, RELEASE `{600,422,116,160}`; y 0 % at 438 → 100 % at 566 (hanging; the fraction of the steady-state GR); labels y 586 | **ATTACK:** log t over 5 µs–500 ms (5 decades, 23.2 px/decade), labels `.01 1 100 MS`. Three curves for steps to +6 / +12 / +24 dB over `T_in`, in ink32 / ink52 / ink70 (+12 in accent while a time control or marker is under the hand). **RELEASE:** log t over 1 ms–10 s (29 px/decade), labels `1MS .1 10S`, recovery after a 50 ms burst (ink52) and after a 2 s burst (ink70). **Markers:** the declared spec (`attackSpec`/`releaseSpec(view, eng)`, drawn at `seconds` under its `TimeLaw`) as an ink32 `hairlineV` (`STEP_SPEC`); the measured crossing (`analysis::measure`) as a 5 px ink100 ring (`STEP_MEAS`); a disagreement is shown, not hidden (C's D2). Stepped time slots draw the other detents' nominal curves as ink16 ghosts. Hold shows as a flat start to the release curve. Caption inside the top-left, kMicro ink32: `TGT 7.5 DB AT +12`. Source: `analysis::stepResponse` on `PreviewWorker` (§9.3) |
| **SIDECHAIN \| COLOUR** | `{732,422,188,160}`; tab cells at y 404: SIDECHAIN `{732,404,62,16}`, COLOUR `{800,404,44,16}` (kCaption, text style, **tab-pinned**, per-instance `scTab`) | **SIDECHAIN:** plot `{732,430,188,136}`, log f 20 Hz–20 kHz (62.7 px/decade), 0…−24 dB (5.67 px/dB). The curve is `analysis::scResponse` (host HPF × `sce` × the Mode's own SC shaping), `SC_CURVE`, in ink70, or accent while SC HPF or SC EMPH is under the hand. A corner handle (5×5 ring at x(scHpfHz), y(−3 dB)) proxies SC HPF. Caption inside: `INTERNAL`, `EXTERNAL · −14 DB PK`, and `LISTENING` in ink100 while `listen` is on. **COLOUR:** plot `{732,430,136,136}`, linear −1…+1 on both axes, identity in ink16. `analysis::colourCurve` at the live applied GR (0 when not live), 128 segments, `COLOUR_CURVE`. ±`colourInPeakDb` ink52 markers. Harmonics column `{876,430,44,136}`: H2 H3 H4 H5 THD from `analysis::harmonicsDb` at −6 dBFS. `NO COLOUR STAGE` centred in ink32 when `!hasColour`; `STATIC APPROXIMATION` when `!colourStatic` |

### 7.4 Interaction on this screen

| Where | Gesture | Effect |
|---|---|---|
| TRANSFER handles (threshold, knee, ratio, range) | drag / wheel / double-click / right-click | As on the band (§6.5). Same `ValueModel`, same `GestureController` discipline |
| HISTORY threshold line | vertical drag | THRESHOLD |
| HISTORY elsewhere | press and hold | Freeze + cursor + column values in the display row |
| STEP spec markers (ATTACK, RELEASE) | horizontal drag (±4 px hit, LeftRight cursor), wheel, double-click | Writes `atk`/`rel`. Continuous: absolute through `plotToHost01` (seconds on the log axis). Stepped: snaps to detents with 6 px hysteresis. Locked/derived: an ink32 cross, refused, footer reason. N/a: no marker |
| SIDECHAIN corner handle | horizontal drag | SC HPF; dragging left past 20 Hz = OFF |
| METERS readouts | click | Reset holds (also on PANEL) |
| Tabs, span, scale words | click | Pin the tab / set the preference |

### 7.5 Keyboard and accessibility on this screen

- **Tab order.**
  1. Mode latch.
  2. Preset strip (‹, name, ›, save).
  3. QUALITY, LOOKAHEAD (one stop per group).
  4. DELTA, BYPASS, CHARACTERISTICS.
  5. The span group, the scale group, the SC|COLOUR tab group, meter reset.
  6. **Handles:** THRESHOLD, KNEE, RATIO, RANGE, ATTACK, RELEASE, then SC HPF (only while the SIDECHAIN tab is shown).
  7. THEME.

  Handles of n/a parameters are skipped. Locked and derived handles are focusable (read-only), so their reason can be
  heard.
- **Keys.** A focused handle takes exactly the keys of its slot (§8.9), through the same `RuleSlider` key logic, bound to
  the same `SlotModel`.
- **Accessibility.** The slot items of the hidden PANEL are `visible=false` (HR skips hidden items in its dump, A §1).
  Handles appear as `slider` items with the slot's title, description, help and value interface. Each plot is an
  `image` whose title is regenerated at ≤ 4 Hz, e.g. `Transfer curve: threshold −18 dB, ratio 4 to 1, knee 6 dB, gain
  reduction 3.2 dB` or `Attack response: 63 percent at 0.31 milliseconds, declared 0.30`.

---

## 8. Controls: states, behaviour, browser, meters, keys

### 8.1 How each state renders and behaves

| Element | continuous (01 `live`) | stepped | locked | derived | n/a |
|---|---|---|---|---|---|
| Label (kLabel) | ink52 → ink70 on hover | same | **ink32** (hover ink52) | ink52 | **ink16** (hover ink32) |
| Tag (kMicro, right-aligned at x+w; moves to the sub line when a word exists) | `+` ink32 if `kFlagExtension` | same | `ParamSpec::tag` (`FIXED`) ink32 | `= RATIO` / `= TIME` / `AUTO` ink32 | – |
| Value (kValueP/S) + unit (`formatValue`) | ink100; ink32 at the Mode default; accent under the hand (HR `:1519-1521`) | step label + unit, same inks | **ink32** fixed value; `~10 MS` with `kFlagProgram` | **ink52**: the live EFF value when `kFlagProgram` and live, else the resolved value | `–` (U+2013) ink16 |
| Sub (primary) / detent line (secondary) | live readout ink32 (§6.4) | detent labels ink32, active ink100, hovered label ink70; ticks only when they do not fit (§8.3) | `ParamSpec::brief` ink32 (≤ 18 glyphs, e.g. `CIRCUIT KNEE`; `tag` when null; K1 #25) | `EFF 2.4 S` or `FOLLOWS RATIO`, ink32 | – |
| Track | 1 px hairline ink16; Mode-default notch 1×3 ink32; caret 2×(7+4h), eased (HR) | hairline + detent ticks 1×5 ink32 at cell centres (none if a cell is < 4 px), active tick ink100; caret on the active cell centre, eased τ 60 ms; **ghost caret** 1 px ink32 at the raw pointer travel while dragging | **dotted** (`dotted`, step 3 px, ink16) + a 1×5 ink32 notch at the fixed value if it lies inside the track | hairline ink16 + **hollow caret** (3×9 rrect, fill α 0, border 1 ink52) at the derived value | none |
| Soft notches (continuous `steps`) | 1×3 ink32 ticks; a drag sticks within ±4 px | – | – | – | – |
| Hover fill (accentDim) | 0 → caret (bipolar: from the notch) | 0 → active cell centre | none | none | none |
| Cursor | LeftRight | LeftRight; PointingHand over a detent label | Normal | Normal | Normal |
| Drag / wheel / keys / double-click | HR in track space (§5.4) | index stepping (§5.4, §8.9) | **refused**: no gesture, no write, footer shows the reason | refused | refused |
| Right-click / ctrl-click | host parameter menu (HR `:1692-1700`) | same | same (the raw value exists and other Modes use it) | same | same |
| Focus ring | yes | yes | yes | yes | yes (to reach the reason) |
| a11y | `slider`, range = Mode track in display units | `slider`, **index space** 0…n−1, step 1 | `slider` read-only, **disabled**, help = reason | `slider` read-only, enabled | `staticText`, disabled, help = reason |

**Hybrid (01 `Kind::hybrid`, e.g. FET 76 attack .02–.8 ms + OFF).** The track reserves a **16 px end cell** for each
step outside `[lo, hi]`: steps below `lo` go on the left, steps above `hi` on the right. A 4 px gap separates the cell
from the continuous part, which spans the rest.
- The cell draws a 1×5 tick at its centre and its label on the sub/detent line, ink32, or ink100 when active.
- When the resolved step is that cell, the caret sits in the cell.
- **Drag** is continuous inside the range. Pushing past the range edge by `0.5·24 + 6 = 18` px of travel enters the
  cell.
- **Keys:** Home/End reach the outermost position. Arrows at a range edge step into the cell.
- **Wheel:** one notch past the edge enters the cell.
- **a11y:** a slider over `[lo, hi]` in display units, whose value string reads "Off" in the cell. Increment and
  decrement at an edge cross into the cell.

Additional cues, all inside the ink budget above. Nothing adds a colour:
- **Renamed / remapped** (`ParamSpec::label` + `DisplayMap`). `label` is the Mode term, and the aka is the universal
  name from `kHostParams` (`THRESHOLD`).
  - The footer reads `INPUT (THRESHOLD)   DRIVES A FIXED −12 DBFS THRESHOLD   …`, and the a11y description is "controls
    threshold".
  - The value text is the display scale (`formatParts` value `30`; the label row already says `INPUT`, K1 #15). The primary sub shows the universal equivalent
    (`THRESHOLD −28 DB`).
  - **The track follows the display**: `t = invert ? 1 − u : u`, where u is the position in the Mode's `[lo, hi]` under
    the host map. INPUT therefore rises to the right while the `thr` lane falls. `kFlagHwReversed` is informational only.
- **Extension** (`kFlagExtension`). Tag `+`. The footer adds `EXTENSION — NOT ON THE ORIGINAL UNIT — NEUTRAL AT DEFAULT`.
- **Clamped** (`ResolvedParam::flags & kClamped`). A raw value outside the Mode's sub-range pins the caret to the track
  end. The value shows the effective value, and the footer and display sub show `CLAMPED FROM 5 µS`.
- **Stored.** Locked and n/a slots never write, and their raw value survives. The footer adds `STORED 3.0 MS (USED BY
  OTHER MODES)`.
- **Dependent lists** (01 `ParamEntry::driver`/`variants`, e.g. Clean drive n/a while VOICE = OFF). `SlotModel::key()`
  includes the driver's resolved step. When the active spec changes, the caret eases, and the label flashes if the
  state or label changed (§8.7).

### 8.2 01 kinds → FunkGui `ValueState`

| 01 `Kind` / `SlotState` | FunkGui `ValueState` | Notes |
|---|---|---|
| continuous / `live` | `continuous` | soft notches from `steps` |
| stepped / `stepped` | `stepped` | `Detent{host01 = toNorm(pid, step.plain), label, spoken ?: text ?: label}` |
| hybrid / `live` inside `[lo, hi]`, `stepped` on a step (01 §4.4) | `continuous` + `ValueView` end cells | `nEndLo`/`nEndHi`/`endLo`/`endHi`/`activeEnd` (§5.3; K1 #32) |
| locked (+`kFlagProgram`) | `locked` | one-step span → its label is the value |
| derived | `derived` | `derivedFrom` gives the tag and the view-switch source |
| notApplicable / `na` | `na` | – |
| `label` + `DisplayMap` | markers `label`/`aka`, track inversion | – |

### 8.3 The detent-label fit rule

This is C's G6, made exact. It is dpi-independent because every width is in logical px.
- For a stepped slot of width `w = 112` with `n` detents, the cell is `c = w/n` and label *i* is centred at
  `x + c·(i + ½)`. Let `wᵢ = width(labelᵢ, kMicro)`, where kMicro is 5.945 px per character minus 1.4 on the last.
- **Labels are drawn iff both hold:**
  - for every adjacent pair, `(wᵢ + wᵢ₊₁)/2 + 2.5 ≤ c`;
  - the end labels stay within `[x − 2, x + w + 2]`.
- Otherwise the slot shows **ticks only**. The value line shows the active label, and the footer lists every step.
- Ticks are omitted when `c < 4`.
- The same rule applies to both slot sizes (both 112 wide).
- `fcmp_probe_plugin textfit` asserts every Mode × stepped slot. 01's registry lint ("labels ≤ 6 glyphs") is a style
  rule; this rule is the real gate.

Worked cases, from 01 §10.2–10.3's lists:

| Case | n | c | Worst pair → need | Result |
|---|---|---|---|---|
| Bus G RATIO 2·4·10 | 3 | 37.3 | "4","10" → 10.0 | labels |
| FET 76 RATIO 4·8·12·20·ALL | 5 | 22.4 | "20","ALL" → 16.0 | labels (ALL ends at x+109) |
| Bus 25 RATIO 1.5·2·3·4·6·10·∞ | 7 | 16 | "1.5","2" → 13.0 | labels (the first starts at x−0.2) |
| Bus G ATTACK .1·.3·1·3·10·30 | 6 | 18.7 | "10","30" → 13.0 | labels |
| Bus 25 ATTACK .03·.1·.3·1·3·10·30 | 7 | 16 | ".03",".1" → 16.0 | labels |
| Bus G RELEASE .1·.3·.6·1.2·AUTO | 5 | 22.4 | "1.2","AUTO" → 21.9 | labels (AUTO ends at x+112.0) |
| Opto 2A RATIO COMP·LIMIT | 2 | 56 | → 27.9 | labels |
| Clean VOICE OFF·TUBE·DIODE·BRIGHT | 4 | 28 | "DIODE","BRIGHT" → 33.8 | **ticks only** |
| Clean DETECT PEAK·RMS·PK+RMS | 3 | 37.3 | "RMS","PK+RMS" → 27.9 | labels (PK+RMS ends at x+110.5) |
| Clean STEREO ST·M/S·MID·SIDE·M>S·S>M | 6 | 18.7 | "M/S","MID" → 18.9 | **ticks only** |
| Mu 67 SC HPF OFF·50·100·200·350 | 5 | 22.4 | "100","200" → 18.9 | labels |
| Mu 67 DRIVE (*INPUT*) −20…0 in 1 dB | 21 | 5.3 | – | **ticks only** (ticks drawn, c ≥ 4) |
| Diode 609 RELEASE .1·.4·.8·1.5·A1·A2 (s) | 6 | 18.7 | ".8","1.5" → 16.0 | labels. Labelled in ms (…800·1500…), it would be ticks only; the lint tells the author |
| Bus 25 LINK IND·50·60·70·80·90·100 | 7 | 16 | "IND","50" → 16.0 | labels |

### 8.4 Snap policy: the UI's obligations under 01 §4.5

1. The caret, value text, a11y value and host text all show the **resolved** value (`resolveView`, `formatValue`,
   `formatHost`).
2. A UI write to a stepped slot always writes `toNorm(pid, stepPlain(spec, i))`, the canonical value of that detent, so
   recorded automation holds exact detents.
3. **A Mode switch writes only `mode`**: one `tap` (one `begin/set/end` gesture, which the host records and undoes as
   one step; there is no in-plugin `UndoManager`, K2 #7). Carets ease to the newly resolved positions (§8.7). A host
   lane may sit between detents; the UI never "fixes" it.
4. **Alt-click (or Alt-Return) on a browser row** = "switch + load Mode defaults" (01 §4.5): `beginBatch()`, then
   `mode`, then `modeDefaults()` for every live or stepped parameter through `GestureController::tapMany`, then
   `endBatch()`, so the audio thread never resolves a half-written set (K2 #23).
5. Locked and n/a slots never write, from any input path, including a11y `setValue`.
6. Double-click and Delete write the **Mode default** (`ParamSpec::defaultPlain`), not the host default. The
   Mode-default notch is drawn at that position.

### 8.5 Mode selector (header)

- ‹ and › step through the **global order**: `Group` order, then slot order within a group. The wheel over the name
  also steps (hit-tested, one gesture per burst).
- A click on the name opens the browser. The name is not accented at rest; it is accent only while pressed.
- The group line reads `VCA · 2 OF 8`: the group, then the position in the global order out of the number of assigned
  slots.
- A11y: `comboBox`, value = the Mode name, press = open the browser.

### 8.6 Mode browser, for a growing list

- **Overlay** `{40,64,880,286}`, opaque ground, eased in τ 0.12 s. The controls stay visible, so switching Modes shows
  the constraints landing (HR's preset-browser convention).
- **Columns = 01's `Group`** in enum order: VCA, FET, OPTO, VARI-MU, DIODE, MODERN, LIMIT, OTHER.
  - 8 columns of 110 px at `x = 40 + 110·k`.
  - Heading at y 72 (kCaption ink52, `FET  3`).
  - Rows from y 92 on a 20 px pitch, 12 rows (y 92…312). Row hit `{colX, rowY−4, 106, 20}`.
- **Row styles.** Name in kLabel ink52. Hover ink100. The current Mode is ink100 with a 2×12 ink100 bar at x−6.
  Retired slots never appear.
- **Capacity: 96 Modes** before anything changes.
  - A group with > 12 Modes wraps into the next column under a `VCA (2)` heading.
  - With more than 8 columns, the browser **pages horizontally by column**: wheel, ←/→ at the edge, and a `‹ 1/2 ›`
    control at y 334.
  - D catalogues 35 Modes; its largest group is VCA with 11. 01 allows 128 slots.
- **Hover** puts `ModeDescriptor::specLine` on the footer.
- **Commit:**
  - Click, or Return on the highlight, commits (§8.4.3).
  - Alt-click or Alt-Return also loads the Mode's defaults (§8.4.4).
  - Esc, clicking the latch again, or clicking outside cancels.
- **Keys:** a 2-D highlight (↑↓ within a column, ←→ across), Home/End, and type-ahead with a 1 s buffer.
- **A11y:** rows are `listItem` (selected = current, help = `specLine`); headings are `staticText`.

### 8.7 Mode-switch landing (truthful, no overshoot)

- Carets ease to their new resolved positions with τ 90 ms. This overrides HR's "> 0.15 jumps snap" only for moves
  caused by a Mode change. Preset recall still snaps.
- Slots whose `ValueView` state, label or tag changed flash their label in ink100 for 0.6 s, then ease back (τ 0.16 s).
- The footer shows the summary for 3 s: `BUS G · 6 STEPPED · 1 DERIVED · 6 EXTENSION · 8 N/A`.
- The TRANSFER curve eases between the old and new sample arrays over 160 ms. The old curve stays in ink16 for 0.9 s.
- While `kUiFading` is set (the 20 ms kernel crossfade), the operating dot is hollow. `modeSlot`/`fadeFromSlot` tell the
  UI which Mode the audio is running.

### 8.8 Meters

Band: IN L/R, GR, OUT L/R. Full screen: IN, SC, GR, OUT. **Both use their screen's shared dB map.**

- **IN/OUT:**
  - The peak fill (`inPeakDb`/`outPeakDb`) is ink32. Inside it sits the RMS (`inRmsDb`/`outRmsDb`) as a 2 px ink70 bar
    (band) or 4 px (full).
  - The part above 0 dBFS is ink100 (`kUiOutOver` also latches the OUT readout). **No red.**
  - The peak-hold tick is 1 px ink100. It holds 1.5 s, then falls at 20 dB/s (wall-clock seconds).
- **GR** hangs from the plot top, filled with `signal`. The max-hold tick is ink100 and uses `blockMaxGrDb`, so a spike
  between frames is not lost.
- **Readouts** at y 126 on the band: max IN peak, max GR and max OUT peak since reset, in kMicro. The widest, `−12.3`, is
  28.3 px wide on a 52 px pitch. **One click on any readout resets all three.**
- **Not live** → the bars fall at 20 dB/s to the floor and the readouts show `–`.
- A11y: `progressBar`, value "−4.2 dB".
- Tags `METER_IN/OUT/GR/SC/HOLD` (live).

### 8.9 Keyboard, Tab order, accessibility

> **S12 revision (UF2, ADR-72).** The band's HISTORY caption is a HISTORY · VU switch (preference `grView`) with its
> own Tab stop before the span group; under VU the span cells, "S" and the time labels are hidden.
>
> **S12 revision.** The footer's last two Tab stops are ZOOM, then THEME (UF1b): a radio group "Zoom" of buttons
> "ZOOM 125 %" with checked and enabled states. The preset strip has four stops: ‹, name, › and SAVE (U6). The
> browsers are not in the Panel's Tab order yet; H1a adds them.

**Tab order on PANEL:**
1. Mode latch.
2. Preset strip (‹, name, ›, save).
3. QUALITY group, LOOKAHEAD group.
4. DELTA, BYPASS, CHARACTERISTICS.
5. HISTORY span group, TRANSFER scale group, meter reset.
6. P0…P6, then B0…B6, then C0…C6, **each followed by its attached word**.
7. THEME group.

Every locked, derived and n/a slot is in the order. HR walks only its controls (`:1880-1893`); F §8.1 lists the gap.

| Key | Continuous slot / handle | Stepped slot / handle | Locked / derived / n/a | Latch, word, cell group, Mode latch |
|---|---|---|---|---|
| ↑ / → | +0.01 of the **track** (Shift +0.001); hybrid crosses into an end cell at the edge | **next detent** (fixes HR's gap, A §3.7) | no write; footer shows the reason | groups: next cell; Mode latch: next Mode |
| ↓ / ← | −0.01 / −0.001 | previous detent | same | previous |
| PageUp / PageDown | ±0.1 | ±1 detent | – | – |
| Home / End | track min / max (hybrid: the outermost cell) | first / last detent | – | first / last |
| Delete / Backspace | Mode default | Mode-default detent | – | – |
| Return / Space | – | – | – | toggle latch or word; select cell; open the Mode browser |
| Esc | browser → ring → leave CHARACTERISTICS | same | same | same |

- Every write is a `begin/set/end` triple, or one wheel gesture.
- Handled keys return `true`, so Logic and Live do not swallow them.
- Space may belong to host transport, so Return is the documented activation key.

| Item | Role | Value interface | State | Title / description / help |
|---|---|---|---|---|
| Continuous slot / handle | `slider` | Mode track `{lo, hi}` in display units, step = the track step | – | title = Mode label ("Input"), description = the universal name, help = the Mode's reason/spec |
| Stepped slot / handle | `slider` | **index space {0, n−1}, step 1**; `setValue(i)` → `writeDetent(i)`; value string = `Step::spoken` | – | help "3 steps: 2, 4, 10" |
| Hybrid | `slider` | `{lo, hi}` + end-cell crossing; value "Off" in a cell | – | help lists the end steps |
| Locked | `slider` | read-only; "10 milliseconds, fixed" | **disabled** | help = reason |
| Derived | `slider` | read-only live value ("6 decibels, follows ratio") | enabled | help = reason |
| N/A | `staticText` | – | disabled | "Range, not applicable", help = reason |
| AUTO/EXT/LISTEN, DELTA, BYPASS, CHARACTERISTICS | `toggleButton` | – | checkable, checked | disabled words carry the reason |
| QUALITY, LOOKAHEAD, span, scale, THEME, SC\|COLOUR | `radioGroup` + `radioButton` | – | checked = active | help, formatted from `fcdsp::kOs` (never hard-coded; K1 #29), e.g. "Std: 2 times IIR oversampling, 4 samples latency" |
| Mode latch | `comboBox` | value = Mode name | – | press opens the browser |
| Browser rows | `listItem` (headings `staticText`) | – | selected = current | help = `specLine` |
| Plots | `image` | – | – | titles regenerated at ≤ 4 Hz (§7.5) |
| Meters | `progressBar` | "−4.2 dB" | – | – |

The model comes from `fcmp::ui::Panel::accessibility()`. Probe G4 (`ui.a11y.<key>`) gates it **per Mode, as text**:
the `lines` sidecar `tests/golden/base/modes/<key>/ui.a11y.<view>.lines`, in the `a11yDumpLine` format (03 §3.2.3;
K1 #4). A Mode that forgets a `reason` fails in plain text; 01's registry lint also catches it.

### 8.10 Ink assignments (a theme is a pure token swap)

| Token | FCompressor jobs |
|---|---|
| ground | window, clear colour, overlay fills, handle interiors |
| ink16 | grids, unity, HISTORY IN fill, GR wedge, ghost curves, dotted locked tracks, n/a labels and values, latch off-fill |
| ink32 | axis labels, IN top stroke, threshold line at rest, locked labels and values, tags, inactive cells, detent labels, knee bracket, net curve, spec line, STEP spec markers, `grMinDb` line, FADE events |
| ink52 | slot labels, captions, units, DET trace, CONTROL PATH target, target dot, derived values, readout names, freeze cursor, stage-1 curve, AUTO SLOW events |
| ink70 | OUT trace, static curve at rest, handles, active cell, hovered labels, latch on-fill, RMS bars, internal lane, RANGE events |
| ink100 | values, operating dot, active detent tick and label, Mode name, hold ticks, HOLD phase, measured-crossing ring, readout values, STAGE 2 events |
| accent | **the thing under the hand only**: the slot value, caret and fill, the curve that control shapes, the dragged handle or marker, the focus ring, a pressed latch or word |
| accentDim | hover and drag track fill |
| **signal** (was `ice`; `#7FD4E8` / `#1E7E96`) | **live gain reduction only**: HISTORY GR, CONTROL PATH applied GR, GR meter, GR needle, the big GR number, RANGE's GR-used bar |

---

## 9. Telemetry and analysis the UI consumes (01 §6–§7, by name)

### 9.1 `UiFrame` (01 §6.2: 72 words, seqlock, published per `process` while attached)

| Field | UI consumer |
|---|---|
| `publishCount` | `LiveFeed` staleness (0.5 s) |
| `modeSlot`, `fadeFromSlot`, `fadeProgress` | landing (§8.7), hollow dot, display GR during a fade |
| `flags`: `kUiBypassed`, `kUiDelta`, `kUiListen`, `kUiExtKeyActive`, `kUiMidSide`, `kUiFading`, `kUiLookahead`, `kUiOutOver`, `kUiTopoFB`, `kUiGrOff`, `kUiAutoSlow`, `kUiRangeLimited`, `kUiS2Active`, `kUiPoisonReset`, `kUiLive`, phase bits 16–19 | latch truth during ramps, SIDECHAIN caption, L/R vs M/S labels, DET trace condition, OUT readout latch, footer notice, frame-rate policy (`kUiLive`), READOUTS PHASE |
| `sampleRate`, `latencySamples`, `bypassAmt` | `StepStimulus::fs`, QUALITY help text, BYPASS latch shows `bypassAmt` as ink70 fill fraction while ramping |
| `historyWritten` | splice check before draining the ring |
| `inPeakDb[2] inRmsDb[2] outPeakDb[2] outRmsDb[2] scPeakDb[2]` | meters, the display sub-readout |
| `colourInPeakDb[2]` | COLOUR ± markers |
| `curveXDb[2]` | operating-dot x, THRESHOLD `DET` sub and detector tick, READOUTS DET/OVER, `localRatio` abscissa |
| `targetGrDb[2]` | target dot, READOUTS TARGET |
| `appliedGrDb[2]`, `blockMaxGrDb[2]` | operating-dot y, GR needle, display GR, GR meter and its hold (positive = attenuation) |
| `s2GrDb[2]` | READOUTS S2 GR |
| `attackNowMs[2]`, `releaseNowMs[2]` | ATTACK/RELEASE `EFF` subs, derived values under `kFlagProgram`, READOUTS |
| `crestDb[2]` | READOUTS CREST |
| `preGainDb thrDb slope kneeDb rangeDb atkTauMs relTauMs holdMs lookMs driveDb makeupEffDb mix scHpfHz sceDbOct link s2ThrDb s2AtkTauMs s2RelTauMs`, `tags`, `discrete` | **While live, the curves are drawn from what the audio uses**: the UI runs `resolve()` on the current raw values (cached by the raw-snapshot hash), then `fcdsp::overlaySmoothed(frame, eng)` copies these smoothed fields on top (01 §6.2; skipped when `frame.modeSlot` ≠ the resolved slot or `kUiFading` is set). `m[8]`, `topo` and `flags` therefore always come from `resolve()` (K1 #7, K2 #24). When not live, `resolve()` alone. Probe `ui.truth` does the same |
| `internals[16]` | READOUTS rows 11–18 (a descriptor declares ≤ 8, in `ModeDescriptor::internals` order; words 8–15 are reserved) |

### 9.2 `HistoryColumn` (01 §6.3: 32 B, 1 ms of audio, ring of 4096)

| Field | UI consumer |
|---|---|
| `inPeakDb`, `outPeakDb` | `HIST_IN`, `HIST_OUT` |
| `detMaxDb` | `HIST_DET`, trail x |
| `grMaxDb`, `grMinDb` | `HIST_GR`, `CP_APPLIED` area and min line, trail y |
| `tgtMaxDb` | `CP_TARGET` |
| `internal0` | CONTROL PATH internal lane |
| `bits`: b0–1 phase, b2 auto-slow, b3 range-limited, b4 s2 active, b5 gap, b6 fading, b8–15 mode slot | state lanes, events lane, gaps, Mode boundary tick (a 1 px ink32 `hairlineV` in HISTORY where b8–15 changes) |

### 9.3 Analysis API (01 §7) and descriptor fields

| Call / field | UI consumer | Thread / cadence |
|---|---|---|
| `analysis::staticGain(entry, eng, x, gain, {colour=false, stage2=true})` | TRANSFER curve, ghosts, wedge, knee marks; `{stage2=false}` → stage-1 curve | message thread; on a resolved/live-parameter hash change; ≤ 120 points |
| `analysis::localRatio(entry, eng, x)` | RATIO `EFF`, READOUTS EFF RATIO, the live value of Mu 67's derived progressive RATIO (01 §10.7) | per frame while live (1 evaluation) |
| `analysis::netGainDb(gain, makeupEffDb, mix)` | net curve | with the curve |
| `analysis::stepResponse(entry, eng, StepStimulus, out)` | STEP panes. Attack: 3 runs with `hiDbOverThr` ∈ {6, 12, 24}, `hiSec` 1.0, `loDbUnderThr` 24, `loSec` 0.2. Release: 2 runs with `hiDbOverThr` 12, `hiSec` ∈ {0.05, 2.0}, `loSec` 10. `decimate` so each pane gets ≤ 4 K points, then min/max to 116 columns | **`PreviewWorker`** (a `juce::Thread` owned by the Panel), ≤ 20 Hz during drags (01 §7), only while CHARACTERISTICS; double-buffered results. About 1.25 M samples ≈ 12 ms per full recompute. `analysis::stepResponse` opens `ScopedFtz` itself (01 §7). With `PanelOptions::syncPreview` it runs inside `tick()` instead (§3.7 rule 7) |
| `analysis::measure(gr, stim, law)` | STEP measured-crossing rings | with each run |
| `ModeDescriptor::attackSpec/releaseSpec(view, eng)` | STEP declared markers | on change |
| `analysis::scResponse(entry, eng, fs, hz, mag)` | SIDECHAIN (161 log-spaced points) | on change |
| `analysis::colourCurve(entry, eng, grDb, x, y)`, `harmonicsDb(entry, eng, grDb, amp, h)` | COLOUR curve (128) and harmonics column | on change, or when the live GR moves > 1 dB |
| `ModeDescriptor::{name, group, topologyLine, specLine, detectorLaw, hasColour, colourStatic, stage2, internals}` | header, browser, footer, axis unit, COLOUR/STAGE panels, READOUTS, CONTROL PATH | static |
| `ParamSpec::{kind, flags, lo, hi, steps, value, derivedFrom, defaultPlain, label, tag, reason, brief, display}`, `ResolvedParam::{plain, display, step, state, flags, tag}`, `formatParts` (value, unit, spoken, prefix) | every slot, handle and marker via `SlotModel` | `resolveView` once per frame on the message thread, cached by the raw-snapshot hash |

### 9.4 How `SlotModel` builds a `ValueView` (the contract between 01 and FunkGui)

- `key()` = hash of: the mode slot; this `Pid`'s raw bits; its `driver`'s resolved step; the `ResolvedParam` (which
  already reflects the configured lookahead budget through `RawParams::budget`); and, for primary subs, the quantised
  live value it prints (0.1 dB / 1 %).
- `view()`:
  - state: from `ResolvedParam::state`, then hybrid end cells from `activeSpec`.
  - `label`, `tag`, `reason` from the active `ParamSpec`; aka from `kHostParams[pid].name`.
  - track: `u = (toNorm(pid, plain) − toNorm(pid, lo)) / (toNorm(pid, hi) − toNorm(pid, lo))`, and `t = display.invert ?
    1 − u : u`. The host map is restricted to `[lo, hi]`, as 01 §4.1 says.
  - detents: `host01 = toNorm(pid, step.plain)`.
  - text: `formatParts` → `ValueText{value, unit, sub, spoken}` (value never contains the slot label; K1 #15).
  - `atDefault`: `plain == defaultPlain`.
- `host01FromTrack(t)` inverts the same map. `defaultHost01()` = `toNorm(pid, defaultPlain)`.

### 9.5 `ProcessorFacade` (`Source/plugin/ProcessorFacade.h`) — canonical, frozen at FZ1

The facade lives in `plugin/` because the processor implements it (K1 #1, K3 #13). F0 writes it in S0 against the
`funkgui::ParamPort` interface specified in §5.2; it compiles (and `lint.headers` checks it) once FunkGui v0.2.0, which
declares `ParamPort`, is pinned at the end of S1, so it freezes at FZ1. It is abstract, so
`fcmp_probe_plugin` can script telemetry (`FakeFacade`), and it hands out **ports**, not the APVTS, so no UI task or UI
probe needs a prepared JUCE processor. Every type it names is defined here (K1 #14).

```cpp
#pragma once
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"          // RawParams
#include "fcdsp/telemetry/UiFrame.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include <funkgui/params/ParamPort.h>
#include <string>

namespace fcmp {
enum class ScTab : uint8_t { sidechain, colour };
struct UiState {                                   // <UI charExpanded="0|1" scTab="sidechain|colour"/> (01 §9.1)
    bool  charExpanded = false;
    ScTab scTab = ScTab::sidechain;
};
struct StateNotice {                               // footer notices after a state load (01 §9.1)
    uint32_t serial = 0;                           // bumps on every setStateInformation
    bool     newerSession = false;                 // stateVersion > kStateVersion
    bool     modeMigrated = false;                 // modeId unknown or retired → successor or clean
    bool     modeRevised  = false;                 // saved modeRev < the Mode's current revision (K2 #10)
    uint16_t savedRev = 0, currentRev = 0;
    char     fromKey[25] {}, toKey[25] {};         // keys are ≤ 24 chars
};

class PresetAccess {                               // message thread only; the preset strip and browser draw from it
public:
    struct Row { std::string uuid, name, category, modeKey; bool factory = false; };
    virtual ~PresetAccess() = default;
    virtual int      count() const = 0;
    virtual Row      row(int index) const = 0;
    virtual int      current() const = 0;          // −1 = none
    virtual bool     modified() const = 0;         // normalised tolerance 1e-4 (01 §9.2)
    virtual uint32_t revision() const = 0;         // bumps on any list or selection change
    virtual void     apply(int index) = 0;         // brackets itself with beginBatch/endBatch (01 §9.2 hooks)
    virtual void     step(int delta) = 0;          // ‹ ›
    virtual bool     saveAs(std::string_view name, std::string_view category) = 0;
};

class ProcessorFacade {
public:
    virtual ~ProcessorFacade() = default;
    // parameters: 29 ports in Pid order, OWNED BY THE PROCESSOR, so they outlive every editor (K2 #27)
    virtual funkgui::ParamPort& port(fcdsp::Pid) = 0;
    virtual fcdsp::RawParams currentRaw() const = 0;               // relaxed loads + the configured lookahead budget
    // telemetry (01 §6)
    virtual bool readUiFrame(fcdsp::UiFrame&) const = 0;
    virtual const fcdsp::HistoryRing& history() const = 0;
    virtual void setUiAttached(bool) = 0;                          // count-based (01 §6.3)
    // per-instance UI state and notices
    virtual UiState& uiState() = 0;                                // message thread
    virtual StateNotice stateNotice() const = 0;
    // multi-parameter writes: while a batch is open the audio thread reuses the previous BlockParams (K2 #23);
    // endBatch() raises the engine snap. Nestable (a counter).
    virtual void beginBatch() = 0;
    virtual void endBatch() = 0;
    virtual PresetAccess& presets() = 0;                           // an empty implementation until P3 (03 §4.9)
};
}
```

- The processor constructs one `funkgui::JuceParamPort` per APVTS parameter at construction and returns them from
  `port()`. The Panel's `SlotModel`s hold references to them.
- There is **no `apvts()`** (UI code never needs the APVTS), **no `beginUndoTransaction`** (no `UndoManager`, 01 §9.1;
  K2 #7) and **no `registry()`**: the UI uses 01's free functions (`modeSlots()`, `bySlot()`, `resolveSlot()`).
- `FakeFacade` (`Tools/probes/plugin/FakeFacade.{h,cpp}`, owned by U1a) implements the same interface with in-memory
  ports, scripted `UiFrame`s and a real `HistoryRing`.

### 9.6 UI-side stores and rates

| Item | Size / rate |
|---|---|
| `HistoryStore` | 1 ms `HistoryColumn` × 20 480 (20.48 s ≥ the 20 s span) × 32 B = 655 KB per editor. Drained every frame. Laps (`read()` returns > `from`) and `bits.b5` become gap markers. Columns are rebuilt every frame (band: 250 columns × 10–80 entries; full screen: 210 × 12–96) |
| Trail | the last 320 ms of the store, sampled every 10 ms |
| UI frame rate | 60–120 Hz while `kUiLive`, an ease, hover, drag, a screen fade or the first-run hint is active. Otherwise 12 Hz (FramePump) |
| Curves | recomputed on an `EngineParams` hash change (≤ 1 per frame) |
| `SlotModel::view` | per frame, only when `key()` changes |
| A11y | structure on revision change; values ≤ 10 Hz; plot titles ≤ 4 Hz |

### 9.7 Draft 2 ↔ 01/03 mismatches, and how the synthesis resolved each

| # | Where | Mismatch (Draft 2) | Resolution |
|---|---|---|---|
| 1 | 01 §2.1, 03 §1.2 | `funkgui::EditorShell` | **`funkgui::EditorHost`** everywhere; its config is `funkgui::EditorConfig` (K1 #18) |
| 2 | 01 §9.1 | `<UI hView tView charExpanded>` named band views that no longer exist | **`<UI charExpanded scTab>`** in 01 §9.1 and `fcmp::UiState` (§9.5) |
| 3 | 01 §2.2 | `ProcessorFacade` lacked what the editor needs and used undefined types | **§9.5**, in `Source/plugin/`: `port(Pid)`, `UiState`, `StateNotice`, `PresetAccess`, `beginBatch/endBatch`; no `apvts()`, no undo |
| 4 | 01 §7 | `staticGain` includes `preGainDb`; TRANSFER needs GR only | No contract change; 01 §7's comment now states both formulas (TRANSFER `x + gainDb − preGainDb`, net curve `netGainDb`) (K1 #22) |
| 5 | 01 §6.3 | One history internal, no stage-2 GR column | Kept: CONTROL PATH has one internal lane, stage 2 is an event stripe. A `s2GrMaxDb` field stays an open lead-level option (§11 Q8) |
| 6 | 01 §4.4/§4.6 | `look` stayed `live` while `labudget` = OFF; Draft 2 overlaid "locked" in the UI | **`RawParams::budget`; the resolver clamps and locks** (01 §4.4). The B3 overlay is deleted (K1 #8) |
| 7 | 01 §10.2 | `kFlagLatchWord` on `tmode` | **Flag deleted**; the layout owns the words (AUTO = `automu`, EXT = `extkey`, LISTEN = `listen`); `tmode` is always a slot (K1 #23) |
| 8 | 01 §7 | `harmonicsDb` conventions unstated | 01 §7 now states `h[k]` = harmonic k+1 re fundamental, `h[0] = 0` |
| 9 | 01 §8.3 lint | Step labels ≤ 6 glyphs failed Mu 67 `LAT/VERT` | The 6-glyph rule is a printed warning; the §8.3 pair-fit rule (`ui.textfit`) is the gate (K1 #24) |
| 10 | 01 §12.1–12.3 | Open questions addressed to this appendix | 12.1: all 29 placed, no compounds (§6.4). 12.2: resolver lock + footer hint, never an implicit latency change (§6.6). 12.3: `FunkPresets` accepted, pending the user (`DECISIONS.md` Q2) |
| 11 | 03 §0 | Draft numbering | 01 = core contracts, 02 = FunkGui + UI, 03 = build/verify/process |
| 12 | 03 §0 | `funkgui::Recorder` | **`funkgui::Canvas`** (the recorder) with `PrimList` as its output |
| 13 | 03 §1.2 | "FunkGui never runs FetchContent for bgfx when consumed" | Compatible: the fetch is guarded by `if(NOT TARGET bgfx)` and FCompressor always provides bgfx first (§1.4) |
| 14 | C §5.14 vs 03 | `FcmpUiProbe` vs `fcmp_probe_plugin` | **`fcmp_probe_plugin`** |
| 15 | 03 §3.6 | Probes drove an undefined `panel.setView/settled/views()` and non-existent band sub-views | **`fcmp::ui::{Screen, Overlay, ViewSpec, views(), PanelOptions, Panel::setView}`** (Part 2 intro); settle via `HeadlessHost::settle()`; no band sub-views (K1 #10) |
| 16 | 02 §5.1 vs 03 §3.6 | Two sets of capture environment variables | **One set**: `FCMP_UI_VIEW`, `FCMP_UI_NO_HINT`, `FCMP_UI_NO_LIVE`, `FCMP_UI_FIXED_DT` (§5.1) |
| 17 | 02 §1.4 vs 03 §2.5 | Unconditional `shaderc` FATAL broke every prebuilt-shaderc configure | Guarded on `FUNKGUI_SHADERC` (§1.4–1.5; K1 #3, K2 #17) |
| 18 | 02 §10 vs 03 §4.9 | Two sprint plans; Draft 2's consumed live worktrees | **One plan, 03 §4.9**; FCompressor consumes only tags (§1.10; K1 #13, K3 #4) |

---

## 10. Work split

The single sprint plan for both repositories — at most 3 agents per sprint, FunkGui tasks one sprint ahead of their
FCompressor consumers — is **03 §4.9** (K3 §3). The FunkGui and UI tasks in it are:

| Task | Repo | Scope (this appendix) | Tag at merge |
|---|---|---|---|
| G0 | FunkGui | **Lead pre-work**: verbatim snapshot + `SEED.tsv`; sed renames keeping HR stems; harness-only CMake (§2.1) | v0.0.1 |
| G1 | FunkGui | CMake targets and globs, Harness v2, `golden.py`, `verify.sh`, `fg.harness.self`, `fg.font.probe`, `fg.prefs.check`; GPU target + shaders build in `agent-gui` (spike) | v0.1.0 |
| G2 | FunkGui | API contracts: `Prim`, `PrimList`, `Canvas` (with `emit`), `Panel`, `Input`, `HostServices`, `ParamPort`, `ValueModel`/`ValueView`/`Detent`, `CellModel`/`ToggleModel`, widget declarations, `A11yItem`, `Tags`. **Implemented, not just declared:** `GestureController`, `ease`, `fmt`, `text::width/fits/fitEllipsis`, `a11yDumpLine` (K3 #16) | v0.2.0 |
| G3 | FunkGui | Recorder: `Canvas.cpp` (HR primitives), dump v2 write/parse, `Fingerprint`, `FontService`, `HeadlessHost`, `BgfxSink` expansion + submit; `fg.canvas.parity`, `fg.canvas.expansion` (spike: HR vertex bit-identity) | v0.3.0 |
| G5 | FunkGui | `RuleSlider` (5 states, hybrid, detent fit rule), `AttachedWord`, `FocusRing`, per-widget gallery files | v0.4.0 |
| G4 | FunkGui | `CanvasShapes.cpp` (area, areaStrip, polyline, disc, dotted, axis), `fs_ui.sc` AREA + `SoftRaster` mirror, FrameRender CLI, `Glyphs.def` + subset, 32 MiB transient buffer | v0.5.0 |
| G6 | FunkGui | `SegmentedSelector`, `LatchToggle`, `ThemeCells`, `HintLine`, `DwellSelector`/`ScreenFader`, `LiveFeed`, `UiPreferences` keys, `MenuLook`, `LineEdit` + gallery | v0.6.0 |
| G7 | FunkGui | `EditorHost`, `A11yBridge`, `FramePump`/`DisplayLink`/`NativeSurface` with `juce::ObjCClass` names, `CaptureConfig`/env, `capture-frame.sh`; gallery Standalone live == headless parity (spike) | v0.7.0 |
| G8 | FunkGui | FunkPresets (§1.2, 01 §9.2), snapshot or header-only re-implementation (lead decides by S6) | v0.8.0 |
| U1a | FCompressor | `Panel` composition, `SubView`, complete `Layout.h`/`Tags.h`, `SlotModel`, `SlotGrid`, `FakeFacade`, stub sub-views; `ui.textfit`, `ui.a11y`, `ui.input`, `ui.font`, `ui.geometry` (spec rows) | – |
| U1b | FCompressor | Header (Mode latch), DisplayRow (cells, latches), Footer (spec line, notices, THEME) | – |
| U2 | FCompressor | Band: `HistoryStore`, `HistoryPlot`, `TransferPlot` (ghosts, handles, landing), `MeterColumn`, live subs; `ui.curve`, `ui.truth` | – |
| U3 | FCompressor | Characteristics A: `CharScreen`, `ControlPathPlot`, `Readouts`, fader | – |
| U4 | FCompressor | Characteristics B: `StepPlot` + `PreviewWorker`, `SidechainPlot`, `ColourPlot` | – |
| U5 | FCompressor | `ModeBrowser`, Tab order and a11y audit across sub-views | – |
| U6 | FCompressor | `PresetStrip` + `PresetBrowser` over `PresetAccess` | – |
| U7 | FCompressor | GPU `Editor`, `CreateEditorGpu.cpp`, `gui-live.sh`, `FCMP_UI_FIXED_DT`/`NO_LIVE`/`NO_HINT` | – |

Frozen before any parallel UI work (FZ1 for FunkGui's G2 headers and for `ProcessorFacade.h`; FZ4 for `SubView.h`,
`Layout.h`, `Tags.h`, `FakeFacade` and the probe views): see the frozen-interface index in `docs/ARCHITECTURE.md`.

---

## 11. Open questions

Lead-level (this appendix decides a default; the lead may change it between sprints):
1. **The full screen has no slot strip**: handles and markers proxy the slots. Alternative: reserve y 560–600 for a
   compact strip of the 7 primaries, which shrinks row 2 of the plots to 120 px. Tied to the user's reading of
   "full-panel" (`DECISIONS.md` Q4).
2. **Screen transition:** a 0.12 s crossfade (chosen) or an instant snap. The crossfade doubles primitives for about
   0.3 s.
3. **Preferences folder** per product (chosen) or a shared `Funk` folder, so that one theme choice applies to every Funk
   plugin (`DECISIONS.md` Q7).
4. **`kWheelNotch`** for stepped wheel accumulation: this needs one measurement of JUCE 8.0.4's macOS smooth-scroll
   deltas. Until then, 0.10.
5. **Upward compression (D M35, future)** gives GR < 0. HISTORY and the meters clamp at 0 today. A later MINOR could add
   an above-the-line band.
6. **Hardware knob direction** (D §3 "match hardware knob direction" preference) is deferred. Tracks follow the
   displayed quantity (§8.1).
7. **When to seed `FunkPresets`** — scheduled as G8 in the last sprint; the lead decides by S6 between snapshotting HR's
   settled `presets/` and re-implementing from the headers alone (03 §4.9).
8. **Stage-2 history trace** (§9.7 #5): accept the event-stripe design (default), or add a `s2GrMaxDb` column field
   before FZ0.
