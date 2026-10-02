# Scout report: card W-N, the UI probes under node (Web Sprint D)

Every `ui.*` probe already compiles, links and passes as wasm32 under node against `tests/golden/base` with no overlay and no source fix. The one costly finding is the synchronous preview: 85-420 ms per request, far over the plan's 8 ms threshold.

Paths: `FC` = /Users/seanfunk/audio/plugins/FCompressor (web/sprint-c, 9746e6d), `FG` = /Users/seanfunk/audio/libraries/FunkGui (web/sprint-c, bc072e4), `S` = /private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-d/wn.

**What I ran, all under `S`** (both repositories still show an empty `git status --short`):
- A copy of FC (`S/fc`) with the CMake proposed below, configured with `-DFCOMPRESSOR_WEB=ON` into `S/build`; `fcmp_probe_web` and `fcmp_web` built at `-j4`.
- The copy's `Scripts/verify.sh --strict S/build` (211 tests), then every `ui.*` test once more by hand under node and once with a copy of `build-lead`'s native `fcmp_probe_plugin` (`S/native`), stdout diffed row by row (`S/cmp`).
- The original and the patched CMake configured as DSP-only and as headless (no build) and compared.
- Two scratch programs for the preview cost (`S/cost/previewcost.cpp`, wasm and native; `S/cost/zz_dragcost.cpp`, a scratch probe).
- Relinks with 64 KB and 1 MiB stacks; drift, overlay and `golden.py report` on a scratch golden copy; `ui.dump` under node.
- **Side effects outside `S`:** 24 small files in Emscripten's shared cache (`cache/js_output`, `cache/symbol_lists`; no system library). One native `ui.dump` run of mine had no `FCMP_PREFS_DIR` and read the user's real `~/Library/Application Support/FCompressor/preferences.settings`; its mtime is unchanged (30 Sep).

## 1. Facts

**1.1 The probe machinery today**
1. Web skips it entirely: `FCOMPRESSOR_WEB` forces `DSP_ONLY` (FC/CMakeLists.txt:90-92) and `include(FcmpProbes)` is behind `if(NOT FCOMPRESSOR_WEB)` (:130-132). So web has no `fcmp_probes`, no `lint.deps`, no `built-from-probes.txt`.
2. `fcmp_probe_dsp` is unconditional (FC/cmake/FcmpProbes.cmake:76-79). `fcmp_probe_plugin` is a JUCE console app (:82-98). Both go through `fcmp_probe_target_common` (:46-57): probes/common and generated includes, `fcdsp fcmp_flags FunkGui::harness`, `-flto` link in Release, `EXCLUDE_FROM_ALL`.
3. Test command (:145-149): `/bin/sh -c <wrapper> fcmp-probe <build>/probe-results/<name>.json $<TARGET_FILE:exe> <layer>.<name> [--mode k] --golden-root <src>/tests/golden --arch ${FCMP_RUN_ARCH} --bless-to <build>/golden-candidates --results <build>/probe-results`. The wrapper (:121) turns exit 2/3 into 0 when the JSON agrees. Nothing prefixes an emulator: CMake only does that for a target named as the command.
4. Grammar (:167-168): `layer=(dsp|proc|ui) name=… scope=(global|mode) timeout=N[ platform=(apple|linux)]`, one platform only (:177-183). Labels `verify;<layer>;global|mode:<key>;probe:<layer>.<name>` (:152). Environment `FCMP_PREFS_DIR`, `FCMP_PRESETS_DB`, `FCMP_UI_THEME=0` (:154). Timeout scale 5 for sanitizers, else 1 (:124-128).
5. Lints: `lint.deps` is a CMake script (:204-205). `lint.headers` runs `check-headers.sh` with the native flag list (:210-221).
6. `FCMP_RUN_ARCH` is `wasm32` on web and `FCMP_CAN_RUN_PROBES` is ON (FcmpArch.cmake:33-34, :198-214; the configure line says "runnable: ON") [S].
7. The Harness accepts `--arch wasm32` (FG/include/funkgui/test/Harness.h:866-868); the overlay is `<root>/wasm32/<scope>/<probe>.txt` (:1116). `ScopedFtz` is empty on wasm (:147-149). `.lines` sidecars are base-only (:58).
8. Only three kinds of golden file exist for `ui.*`: `ui.geometry.txt` per Mode, `ui.font.txt` / `ui.font_linux.txt`, and the `ui.a11y.*` / `ui.input.taborder.*` `.lines` sidecars. Every other `ui.*` probe is spec-only.

**1.2 What builds for wasm32** [S]
9. With the CMake of §2, `fcmp_probe_web` built first time with **0 warnings under `-Werror`**: 145 steps, 30 s wall at `-j4`, 106 s CPU. It compiles 39 fcdsp, 31 FunkGui core, 28 editor, 2 portable, 3 facade, 5 probes/common and 28 probes/plugin sources.
10. The 28 probe files are all 23 `layer=ui` files, plus `ui_dump`, `FakeFacade`, `EngineFacade`, `LoopbackLink` (no FCMP_PROBE line). `EngineFacade` is JUCE-free. `AllocCounter.cpp` and `RtInterposer.cpp` link as they are.
11. `proc.webnull` and `proc.webpresets` cannot be JUCE-free: they compare against `plugin/Processor.h` (FC/Tools/probes/plugin/webnull.cpp:72, :91, :899; webpresets.cpp:34, :46, :176). `ui.web` is JUCE-free and runs under node (WebFacade through LoopbackLink into WebEngine, 0.1 s, pass).
12. Output: `fcmp_probe_web.js` 77 KB, `.wasm` 2,406,955 bytes. Link flags used: `-sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 -sENVIRONMENT=node`. Eight heavy probes also pass with a 64 KB and a 1 MiB stack.
13. The patched CMake leaves native trees untouched: `build.ninja` and the full `ctest --show-only=json-v1` list are identical to the original's for DSP-only (228 tests) and headless (527 tests), paths aside.

**1.3 The run under node** [S]
14. `verify.sh --strict` on the scratch web tree: **211 tests, PASS 211, DRIFT 0, MISSING 0** (204 `ui.*`, `lint.deps`, 6 `web.*`). `ui.font`, run by hand and then registered with `platform=apple,web`, also passes (205 `ui.*`).
15. Against `build-lead`'s native results of the same commit: status, `spec_pass`, `spec_fail`, `golden_rows`, `golden_fail`, `golden_new` are equal for all 204 tests.
16. All **182 candidate files** (14 `ui.geometry.txt`: 7,694 exact rows and 392 `abs:0.01` rows; 168 `.lines` sidecars) are byte-identical between wasm32 and arm64.
17. Of **21,360 stdout rows** over 205 runs, **4 differ**: `scale.ticks.deg` in `ui.vu` for fet-76, opto-2a, opto-3a and opto-tube-1b prints 7.90333902e-06 under node and …903e-06 natively. Cause: musl's `atan2` in the probe's own `angleOf` (FC/Tools/probes/plugin/ui_vu.cpp:141-143). It is a spec row with limit 0.01 (:519), so nothing needs an overlay.
18. Causes that did not occur: no 32-bit `size_t` difference, no thread problem, no float-formatting difference in a text row, no atlas difference, no timing row (no `ui_*` file reads `FCMP_TIMING_SCALE`).
19. `ui.dump` under node writes dumps byte-identical to native (mu-67 and clean, `panel` and `chars.colour`, 159-311 KB each) when both runs are sandboxed.
20. Exit codes pass through node: raw exit 2 on a doctored golden, wrapper 0 with `golden_drift` in the JSON, candidate and `.diff` under `golden-candidates/wasm32/`; a hand-written `wasm32` overlay row makes it pass; an unknown Mode exits 4 with a RESULT line.

**1.4 `ui.font`**
21. It checks that every string any Mode can show has its glyphs, and holds one golden row, `font.atlas.hash` (FC/Tools/probes/plugin/ui_font.cpp:233-262). It is `platform=apple` (:1) because a JUCE build rasterises the atlas natively, so the hash differs by platform (:18-20).
22. Without JUCE nothing is baked (FG/src/nojuce/FontAtlasBake.cpp:10-18): FontService loads the committed `fonts/FunkGuiAtlas-macos.bin` (FG/src/text/FontService.cpp:53-54). Under wasm32 the hash is **b744c79b9bb755d0**, which is FC/tests/golden/base/global/ui.font.txt:3; 17 spec rows pass [S].

**1.5 The synchronous preview**
23. Under Emscripten without pthreads `startThread()` returns false at compile time (FC/Source/editor/PreviewWorker.cpp:99-114) and `tick()` computes inline (:189-193). The limiter is 20 Hz of *tick dt* (:36, :183; Layout.h:487).
24. A request is made every frame by each StepPlot with the frame's `engHash` (FC/Source/editor/views/StepPlot.cpp:425-429); a repeat is dropped. The Panel ticks the worker before the views (Panel.cpp:486), so a request runs on the next tick.
25. One request under node, 48 kHz, median of 9, this Mac at load about 2 [S]:

| Mode | wasm (node) | native arm64 | ratio |
|---|---|---|---|
| bus-g | 85 ms | 22 ms | 3.9 |
| opto-tube-1b | 89 ms | 23 ms | 3.9 |
| bus-25 | 94 ms | 22 ms | 4.2 |
| clean | 101 ms | 25 ms | 4.1 |
| octo | 101 ms | 23 ms | 4.4 |
| brickwall | 111 ms | 40 ms | 2.8 |
| console-e | 111 ms | 26 ms | 4.2 |
| fet-76 | 122 ms | 36 ms | 3.4 |
| diode-54 | 139 ms | 36 ms | 3.9 |
| diode-609 | 154 ms | 36 ms | 4.3 |
| mu-mastering | 201 ms | 52 ms | 3.9 |
| opto-2a | 317 ms | 56 ms | 5.7 |
| opto-3a | 320 ms | 56 ms | 5.7 |
| mu-67 | 420 ms | 68 ms | 6.1 |

   At machine load about 5 the same run gave 105-564 ms. At 44.1 kHz the worst is 387 ms; at 96 kHz 988 ms (measured at load 5).
26. So every Mode is **10 to 50 times over the plan's 8 ms threshold**. Opening CHARACTERISTICS costs exactly one request (109 ms for clean). With fixed dt = 1/60 a drag makes 18-20 jobs per 60 frames. With dt = the previous frame's wall time, as in a browser, every frame runs a job: 9.9 fps on clean, 8.7 on fet-76, 3.3 on opto-2a, 2.4 on mu-67 [S].

**1.6 Time**
27. The 204 `ui.*` tests cost 696 s summed under node against 181 s natively (3.8×). `ui.input` alone is 332 s (4.6× native, 48 % of the total). Slowest: `ui.input.mu-67` 56.8 s (native 9.2 s, 6.2×), `ui.input.opto-3a` 35.9 s, `ui.charscreen.mu-67` 19.2 s. Every declared timeout is 300 s or less.
28. Wall: the whole web gate (211 tests, `-j4`, machine load about 5.7) took 3 min 17 s. My own runner at 4 jobs: 176 s under node, 38 s natively.
29. The CI runner executes this wasm about 1.6× slower than this Mac (FC/docs/DECISIONS.md:1080-1081: 20.4× against 12.6× real time).

**1.7 verify.sh, golden.py**
30. verify.sh classifies by label (FC/Scripts/verify.sh:174, :184), reads `probe-results/<name>.json` and needs no change: the summary above came from the unmodified script.
31. With `fcmp_probes` on web, `built-from-probes.txt` is written, so a green `verify.sh build-web` now writes a `verify-passed-<sha>` stamp (:257-271).
32. FG's `golden.py` reports and diffs wasm32 candidates (FG/tools/golden.py:58) [S: `report` shows the drift]. `adopt` refuses a wasm32 primary build (:631-634) and prunes `wasm32/` rows when base moves (:572).

## 2. What must change

### What the lead must land first
Full diff: `S/probes-web.diff` (217 lines; CMakeLists.txt, FcmpProbes.cmake, FcmpWeb.cmake). Exact text of each hunk:

1. **`CMakeLists.txt:130-132`**: drop the guard and keep the order. FcmpWeb.cmake:101 needs `fcmp_probe_plugin` to exist already.
   ```cmake
   include(FcmpProbes)     # probe executables, self-registered CTest tests over Modes.def, lints, verify targets
   include(FcmpWeb)
   ```
2. **`cmake/FcmpProbes.cmake:76-79`**: no `fcmp_probe_dsp` on web.
   ```cmake
   set(_fcmp_probe_exes "")
   if(NOT FCOMPRESSOR_WEB)                  # the DSP probes under node are a follow-up card (ADR-93)
     add_executable(fcmp_probe_dsp ${FCMP_PROBE_COMMON_SOURCES} ${FCMP_PROBE_DSP_SOURCES})
     fcmp_probe_target_common(fcmp_probe_dsp)
     fcmp_warn_sources(${FCMP_PROBE_COMMON_SOURCES} ${FCMP_PROBE_DSP_SOURCES})
     list(APPEND _fcmp_probe_exes fcmp_probe_dsp)
   endif()
   ```
3. **After :98**, the new executable:
   ```cmake
   if(FCOMPRESSOR_WEB)
     set(FCMP_PROBE_WEB_SOURCES "")
     foreach(_f IN LISTS FCMP_PROBE_PLUGIN_SOURCES)
       file(STRINGS ${_f} _hdr LIMIT_COUNT 1 REGEX "^// FCMP_PROBE ")
       if(NOT _hdr OR _hdr MATCHES "^// FCMP_PROBE layer=ui ")
         list(APPEND FCMP_PROBE_WEB_SOURCES ${_f})
       endif()
     endforeach()
     set(_own ${FCMP_EDITOR_SOURCES} ${FCMP_PLUGIN_PORTABLE_SOURCES} ${FCMP_WEB_FACADE_SOURCES}
              ${FCMP_PROBE_COMMON_SOURCES} ${FCMP_PROBE_WEB_SOURCES})
     add_executable(fcmp_probe_web ${_own})
     fcmp_warn_sources(${_own})
     fcmp_probe_target_common(fcmp_probe_web)
     target_include_directories(fcmp_probe_web PRIVATE ${FCMP_SOURCE_ROOT})
     target_link_libraries(fcmp_probe_web PRIVATE FunkGui::core fcmp_web_engine_lib)
     funkgui_configure_product(fcmp_probe_web PRODUCT ${FCMP_PRODUCT_NAME} OBJC_PREFIX ${FCMP_OBJC_PREFIX}
                               ENV_PREFIX ${FCMP_ENV_PREFIX} PREFS_FOLDER ${FCMP_PREFS_FOLDER})
     target_link_options(fcmp_probe_web PRIVATE
                         -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608
                         -sENVIRONMENT=node)
     list(APPEND _fcmp_probe_exes fcmp_probe_web)
   endif()
   ```
   and at :101 `if(FCMP_BENCH_SOURCES AND NOT FCOMPRESSOR_WEB)`.
4. **:124-128 and :147**, the scale and the emulator:
   ```cmake
   elseif(FCOMPRESSOR_WEB)                  # wasm under node: measured 2-6x the native wall time (ADR-93)
     set(FCMP_PROBE_TIMEOUT_SCALE 3)
   …
   set(_fcmp_probe_run "")
   if(FCOMPRESSOR_WEB)
     set(_fcmp_probe_run ${CMAKE_CROSSCOMPILING_EMULATOR})
     if(NOT _fcmp_probe_run)
       message(FATAL_ERROR "FcmpProbes: the Emscripten toolchain set no CMAKE_CROSSCOMPILING_EMULATOR (node)")
     endif()
   endif()
   …                ${_fcmp_probe_run} $<TARGET_FILE:${exe}> ${layer}.${probe} ${margs}
   ```
5. **:160-201**, the grammar and what registers:
   ```cmake
   function(fcmp_register_probes exe dir layers)     # optional 4th argument: the layers this executable registers
     if(ARGC GREATER 3)
       set(_take "${ARGV3}")
     else()
       set(_take "${layers}")
     endif()
     …
       set(_plat "(apple|linux|web)")
       if(NOT _hdr MATCHES "${_re}( platform=(${_plat}(,${_plat})*))?$")   # message: [ platform=<apple|linux|web>[,...]]
     …
       string(REPLACE "," ";" only "${CMAKE_MATCH_6}")
       list(TRANSFORM only REPLACE "^apple$" "macos")
       if(NOT layer IN_LIST layers)   … FATAL_ERROR as today …   endif()
       if(only AND NOT FCMP_PLATFORM IN_LIST only)
         continue()
       endif()
       if(NOT layer IN_LIST _take)
         continue()                               # web: the proc.* probes
       endif()
   …
   if(FCOMPRESSOR_WEB)
     fcmp_register_probes(fcmp_probe_web plugin "proc;ui" "ui")
   else()
     … the two existing calls …
   endif()
   ```
   A malformed `platform=apple,foo` is still a configure error [S].
6. **:206-221**: wrap `lint.headers` (its flag strings, `add_test` and properties) in `if(NOT FCOMPRESSOR_WEB)`; `lint.deps` keeps its own `set_tests_properties`.
7. **`cmake/FcmpWeb.cmake:131`**: add `fcmp_probes` to the `fcmp_web` dependency list. The probe targets are `EXCLUDE_FROM_ALL`, and the `web` build preset builds only `fcmp_web`.
8. Comments and documents: FcmpProbes.cmake:1-30 (grammar, web), CMakeLists.txt:12, FcmpWeb.cmake:26-28, `docs/design/03` §2.9's grammar line.
9. Nothing in `CMakePresets.json`, `Scripts/verify.sh`, `Scripts/golden.py`, `LintDeps.cmake` or `ci.yml` is needed for this card.

### What the card must do
Order: after the lead's CMake the web gate is already green with 204 `ui.*` tests; then:
1. `Tools/probes/plugin/ui_font.cpp:1`: `platform=apple,web`. Extend the comment at :18-20: on web the atlas is FunkGui's committed macOS bake, so the row is macOS's.
2. No `ui_*.cpp` needs a node-specific fix.
3. The measured preview cost in the handoff, per Mode, as fact 25. If the lead wants it kept in the gate, a new `Tools/probes/plugin/ui_previewcost.cpp` (see §4).
4. Optional and cosmetic: 20 `ui_*.cpp` files still say "FontService bakes the atlas through JUCE's fonts" beside `HeadlessGuiScope`.

## 3. Traps
1. **Include order.** Moving `include(FcmpProbes)` after `FcmpWeb` to "define the engine library first" silently drops the native hook (FcmpWeb.cmake:101-109). `target_link_libraries` may name `fcmp_web_engine_lib` before it exists.
2. **No preferences file on web.** FunkGui's JUCE-free default backend is in memory (FG/src/nojuce/PrefsDefaultBackend.cpp:9-12). Under node the sandboxes hold 0 files against 99 natively [S]. A future probe that reads `preferences.settings` from disk would pass natively and fail on web.
3. **No PNG under node.** `writePng` returns false without JUCE (FG/src/nojuce/WritePng.cpp:9-12), so `ui.dump --png` exits 1 [S]. Render PNGs natively, or from the dump with a native `funkgui_framerender`.
4. **Always sandbox a hand run.** A native probe without `FCMP_PREFS_DIR` reads the user's real preferences; my first `ui.dump` comparison differed only because of that (the band drew a VU face).
5. **`.lines` sidecars have no overlay** (Harness.h:58). If a tab order or an a11y list ever differed on wasm32, only a probe change could express it. Today they are identical.
6. **`golden.py adopt` cannot write a wasm32 overlay** (fact 32). A row that musl moves needs a hand-written `tests/golden/wasm32/<scope>/<probe>.txt` holding only that row; its key must exist in base and must not be `xarch.*` (Harness.h:1136-1139).
7. **Exceptions.** The build has Emscripten's default exception model: a `catch` never runs. `EngineRig`, `Measure` and `LoopbackLink` throw on errors (EngineRig.cpp:37, :98-102; LoopbackLink.cpp:22); under node that is an abort without a RESULT line, which verify.sh reports as "no results file". I did not provoke one.
8. **`FunkGui::core` is INTERFACE sources.** Use `fcmp_warn_sources`, never `fcmp_warnings` target-wide, and call `funkgui_configure_product` (as FcmpWeb.cmake:120-126 does).
9. **`ctest -L web` is a regex**: it also selects `probe:ui.web`. Use `-L '^web$'`.
10. **The workflow plus verify.sh runs the node probes twice** (the test step, then the script): about 2 × 3 minutes here.
11. **`ProbeMain.cpp:77`** still prints `--arch arm64|x86_64` (frozen at FZ0; cosmetic).

## 4. How to verify

**Existing coverage, now on wasm32 under node**
- All 204 `ui.*` tests and `ui.font`, against `tests/golden/base` with no overlay.
- `ui.web`: the facade, the byte protocol and the engine wrapper inside one wasm module.
- `ui.geometry`'s `skeleton.preview.{sync,async}` rows: the inline path where no thread can exist.
- `lint.deps`, new to the web tree (2 s).

**Natively**
- The `agent`, `lead` and `dsp` gates must show no drift.
- `ui.font` still registers on macOS and not on Linux.

**Worth adding**
- `ui.previewcost` (node; `platform=web` or everywhere): one request per Mode through `PreviewWorker(true)`. A spec row that every job completed, and the milliseconds through `Probe::note`, with no time limit: it runs beside three other tests, and the figure is already known to exceed 8 ms. It proves the inline path completes for all 14 Modes and records the number the follow-up must beat.

**Cannot be automated in this sprint**
- Anything in a browser (headless Chrome is the lead phase's `web-live.sh`).
- Chrome's own wasm speed for the preview: node stands in for it.

**CI**
- No `ci.yml` edit is needed: the `web` job already runs `cmake --build --preset web` and `verify.sh --strict build-web` (FC/.github/workflows/ci.yml:198-203), so it picks the probes up when this merges.
- Added cost, estimated from fact 29: about 150 more translation units and roughly 1,150 s of test CPU, about 5 minutes of wall on four cores. The job's limit is 30 minutes (:165).
- It needs `tests/golden` from the checkout and FunkGui at the pin from GitHub, which it already clones for the compile checks.
- On failure it uploads only `verify-ctest.log` (:214-219); `probe-results` and `golden-candidates` would help, but that edit belongs to the lead phase.

## 5. Open questions for the lead

**Q1. The preview Worker trigger has fired (facts 25-26). What happens before the page is shown?**
- (a) Schedule the plan's follow-up (a Worker behind `PanelOptions::previewExecutor`); it needs the user's authorisation.
- (b) A stopgap in Sprint D: on web, compute only once the value has rested, or at a lower rate. This touches `PreviewWorker` or `Panel`, which are frozen.
- (c) Ship D as planned and accept 2-10 fps while a parameter moves on CHARACTERISTICS.
- I recommend (c) for Sprint D, since nothing is published in it, with (a) reported to the user as due before publishing, as the plan says.

**Q2. Is W-N still an agent card?**
- It is one line, a comment and an optional small probe once your CMake lands.
- I recommend you make the `ui_font.cpp` edit with the CMake and give the agent slot to W-U, or to the preview follow-up if authorised.

**Q3. Timeout scale for web: 1, 2 or 3?**
- The slowest test is 56.8 s against 300 s; about 95 s expected on CI.
- I recommend 3: three agents building at once have slowed this machine by more than 3× before.

**Q4. What becomes of `fcmp_web_editor_check`?**
- (a) Keep it beside `fcmp_probe_web`: 226 build steps, 150 s CPU.
- (b) Drop it: the probe executable compiles the same sources with the same flags.
- (c) Make it the one static library that the probe executable links: 160 steps, 129 s CPU; a 15-test sample passes with identical candidates (`S/probes-web-shared.diff`).
- I recommend (b) now. (c) pays off only if W-U's module links the same library, and I did not test `FunkGui::web` on top of it (it links the core's INTERFACE sources again).

**Q5. Where does the measured preview cost live?**
- A `ui.previewcost` probe (§4), or ADR-93 text only.
- I recommend the probe: the follow-up then has a number in every results JSON.

**Q6. Should FunkGui's `golden.py adopt` learn wasm32 overlays?**
- Not needed today (zero drift). I recommend leaving it until a row actually differs.

## 6. Can it be split?
No. The area is the lead's CMake (worked out and tested above) plus one line of card work. The lead's files and the card's files (`Tools/probes/plugin/ui_font.cpp`, an optional new `ui_previewcost.cpp`) are already disjoint, and no interface needs freezing: the seam is the `platform=` grammar in §2.

## Not checked
- The x86-64 CI runner: its times are an estimate, and its node is emsdk's, not this Mac's v24.15.0.
- Whether GitHub holds the pin commit bc072e4.
- `cmake --workflow --preset web-verify` itself: I ran configure, ninja and the copy's verify.sh, not the presets, and not the test preset's `-j6`.
- The GPU (`lead`) configuration with the patched CMake; only DSP-only and headless were compared.
- Linux native.
- A real throw under node, `-sSTACK_OVERFLOW_CHECK`, and peak memory.
- The whole 205-test set on the shared-library variant (15 tests only) and with a small stack (8 tests only).
- Chrome, Safari or Firefox for anything.
- The preview at load 0: the table was taken at machine load about 2 with other work running.