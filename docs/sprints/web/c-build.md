# Scout report: build and tests for Web Sprint C, and the first steps of Sprint D

Paths: `FC` = /Users/seanfunk/audio/plugins/FCompressor (web/sprint-b, 776a598), `FG` = /Users/seanfunk/audio/libraries/FunkGui (web/sprint-b, f2641a9), `S` = /private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-c/build.

**What I ran, all under `S`** (both repositories still show an empty `git status --short`):
- `em++ -fsyntax-only` with FC's full warning list and `-Werror`, over 37 real sources and 25 scratch copies of probes.
- A 30-line program under node (threads, filesystem, environment).
- A scratch FetchContent consumer of FunkGui under Emscripten, with `FETCHCONTENT_SOURCE_DIR_FUNKGUI=FG` (read-only).
- A scratch copy of FC's `cmake/`, `Source/`, `Tools/` with the changes proposed below, configured with `-DFCOMPRESSOR_WEB=ON` and built at `-j3`. The diff is `S/skeleton.diff` (84 changed lines).
- A patched `LintDeps.cmake` (`S/lint/LintDeps.cmake`) run on the real tree and on a fake tree.
- **One side effect outside `S`:** the `-flto` link of my scratch browser module made Emscripten generate `lto/libGL-webgl2.a` in its shared cache (`/opt/homebrew/Cellar/emscripten/6.0.3/libexec/cache`).

## 1. Facts

**1.1 The web configuration today**
1. `FCOMPRESSOR_WEB` picks the toolchain before `project()` (FC/CMakeLists.txt:29-44) and forces `FCOMPRESSOR_DSP_ONLY`, then `HEADLESS` (:90-95). `FcmpPlugin` is not included (:125-127), `FcmpProbes` is not included for web (:130-132), `FcmpWeb` always is (:133).
2. FC/cmake/FcmpDeps.cmake:242-252: the web branch holds only the Emscripten 6.0.3 pin and its provenance row. Everything about FunkGui sits in the `else()` (:252-321). So `build-web/fcmp-deps.txt` has no FunkGui row, and `Scripts/golden.py` exits on such a tree (Scripts/golden.py:22-32).
3. FcmpWeb.cmake targets:
   - `fcmp_web_engine_lib`: STATIC, every configuration (:48-53).
   - `fcmp_web_check`: every configuration, node link flags for web (:56-75).
   - `fcmp_web_engine`: web only (:78-91).
   - `fcmp_web`: web only (:93-100), the `web` build preset's one target (CMakePresets.json:138).
4. Six `FCMP_WEB_TEST` tests exist (Tools/web/enginecheck.cpp:6-9, simd.cpp:1, web/tests/engine.mjs:3), labelled `verify;web;global` (FcmpWeb.cmake:150). Four also run natively in every gate.
5. Pin: `FCMP_FUNKGUI_TAG` = `FCMP_FUNKGUI_SHA` = f2641a9…, version 0.12.0 (FcmpDeps.cmake:31-33). FG's HEAD is that commit; `git describe` says `v0.11.1-13-gf2641a9`, so v0.12.0 is not tagged yet.
6. wasm32 flags: `-msimd128` (FcmpArch.cmake:73). `FCMP_RUN_ARCH` is `wasm32` (:33-34, :52-53). `FCMP_CAN_RUN_PROBES_wasm32` is ON in build-web's cache.

**1.2 FunkGui v0.12.0 as a subproject under Emscripten**
7. Options are FG/CMakeLists.txt:58-65. A consumer sets them as normal variables.
   - `FUNKGUI_WITH_JUCE=OFF` turns bgfx and presets off itself (:84-90).
   - Emscripten with JUCE left ON is a FATAL_ERROR (:91-94).
   - `FUNKGUI_WEB` exists only at top level (:9-30).
   - `FUNKGUI_BUILD_TOOLS` and `FUNKGUI_BUILD_TESTS` default to `PROJECT_IS_TOP_LEVEL` (:62-63).
   - With JUCE off, FunkGuiDeps looks for no JUCE (FG/cmake/FunkGuiDeps.cmake:23-27, :178).
8. Targets (FG/cmake/FunkGuiTargets.cmake):
   - `FunkGui::harness`: INTERFACE, the include root plus `FUNKGUI_HAS_JUCE=0|1` (:51-64).
   - `FunkGui::core`: INTERFACE with INTERFACE **sources** (:430-443), `src/nojuce` in place of `src/juce` (:314-319). Every target that links it compiles the core.
   - `FunkGuiFonts`: STATIC without JUCE, made at build time by FunkGuiEmbed.cmake from the TTF, the licence and the committed `fonts/FunkGuiAtlas-macos.bin` (:377-403).
   - `FunkGui::web`: defined only `if(EMSCRIPTEN)`; INTERFACE, links FunkGuiCore, adds `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS=0`, depends on `FunkGuiShaderText` (:569-577).
   - `funkgui_configure_product` (:76-124) sets the four product definitions and the per-source include directories of FunkGui's sources. Every target that compiles FunkGui sources must call it.
9. Measured in the scratch consumer (normal variables `WITH_JUCE OFF`, `HARNESS_ONLY OFF`, `WITH_BGFX OFF`, `WITH_PRESETS OFF`, `BUILD_TOOLS OFF`):
   - Configure in 0.3 s; harness, core, web, FunkGuiFonts, FunkGuiShaderText exist; gpu, presets and the tools do not.
   - A node executable over core + harness passes a `ui.font` stand-in as `--arch wasm32` against FC's golden root. `font.atlas.hash` = b744c79b9bb755d0, which is FC/tests/golden/base/global/ui.font.txt.
   - A browser executable over core + web links.
10. Measured: a STATIC library that links `FunkGui::core` (or `::web`) PRIVATE compiles the 32 core sources once. Executables linking that library compile none of them and still get FunkGuiFonts and the WebGL2 link options.

**1.3 Probe grammar, goldens, blessing**
11. A probe is one file in `Tools/probes/plugin/` (flat glob, FcmpSources.cmake:100). Its first line matching `^// FCMP_PROBE ` registers it (FcmpProbes.cmake:163).
    - Grammar: `layer=(dsp|proc|ui) name=[a-z0-9_]+ scope=(global|mode) timeout=N[ platform=(apple|linux)]` (:167-168).
    - `scope=mode` registers one test per `Modes.def` key (:187-190); there are 14.
    - Labels: `verify;<layer>;global|mode:<key>;probe:<layer>.<name>` (:152).
    - Environment: `FCMP_PREFS_DIR`, `FCMP_PRESETS_DB`, `FCMP_UI_THEME=0` (:154).
12. The test command is `/bin/sh -c <wrapper> … $<TARGET_FILE:exe> <layer>.<name> [--mode k] --golden-root <src>/tests/golden --arch ${FCMP_RUN_ARCH} --bless-to <build>/golden-candidates --results <build>/probe-results` (:145-149). The wrapper turns exit 2/3 into 0 when the results JSON agrees (:121).
13. Goldens are `tests/golden/base/{global,modes/<key>}/<layer>.<name>.txt`, plus `.lines` sidecars. `tests/golden` holds only `base/`: there is no arm64, x86_64 or wasm32 overlay today.
14. A probe with spec rows only and no golden file passes (FG/include/funkgui/test/Harness.h:43). Most proc probes, `ui.edits` and `ui.presets` are like that: `base/global` has no file for them.
15. verify.sh treats a test as a probe when its labels meet `{dsp, proc, ui}` (Scripts/verify.sh:174, :184). Other `verify` tests are judged by exit code (:192-197). `--strict` fails on DRIFT or MISSING (:282-285); CI uses it.
16. Blessing: `FCMP_ALLOW_BLESS=1 Scripts/golden.py adopt <build> [--x86 <build-x86>] --only '<globs>' --reason '<why>'`, from the main checkout. It runs the `tools/golden.py` of the FunkGui row in `fcmp-deps.txt`. That tool refuses wasm32 candidates (FG/tools/golden.py:27-30, :631-634).
17. `fcmp_probe_plugin` (FcmpProbes.cmake:83-97):
    - Sources: plugin, editor, `CreateEditorGeneric`, probes.
    - Include directories: probes/common, generated, `Source/` (:47, :89).
    - Links: fcdsp, fcmp_flags, harness, core, presets, JUCE.
    - Our warning list is applied per file (:87), so FunkGui's and JUCE's sources in the target never get `-Werror`.

**1.4 What compiles for wasm32 today** (em++ 6.0.3 = Clang 23, FC's warning list, `-Werror`)
18. 33 of 37 pass: 24 of the 28 editor sources outside `gpu/`, both `Source/plugin/portable/*.cpp`, `WebEngine.cpp`, `FakeFacade.cpp`, all five `Tools/probes/common/*.cpp`. The result is the same with FunkGui's headers as non-system includes.
19. The four failures are the JUCE includes of `EditControls.cpp`, `PresetBrowser.cpp`, `PresetStrip.cpp`, `Settings.cpp` (E-2's work). The compiler stops at the include, so their bodies are unchecked.
20. Scratch copies of the 24 `ui_*.cpp` and `EngineFacade.cpp`, with the JUCE include swapped for `<funkgui/panel/HeadlessGuiScope.h>`: 21 of 25 pass. The four that fail:
    - `ui_chars.cpp:782` and `ui_geometry.cpp:243`: `juce::Thread::sleep`.
    - `ui_truth.cpp:498`: `-Wshorten-64-to-32`, a `uint64_t` index where `size_t` is 32 bits.
    - `ui_dump.cpp:36`: includes `plugin/factory/FactoryBank.h`, which needs JUCE.
21. Runtime under node (`-sNODERAWFS=1`, no `-pthread`):
    - `std::mutex`, over-aligned `new`, and `std::filesystem` (create, rename, `weakly_canonical`, `equivalent`, `remove_all`) work on host paths.
    - `getenv` sees the host environment (NODERAWFS enables `NODE_HOST_ENV` in 6.0.3's `settings.js`:1068).
    - Constructing a `std::thread` aborts the program. `PreviewWorker.cpp:101` already compiles the thread out under Emscripten.
    - Emscripten defines `__unix__`, not `__linux__` or `__APPLE__`.
22. The scratch FC copy with `S/skeleton.diff`, `cmake -DFCOMPRESSOR_WEB=ON`:
    - It configures with FunkGui 0.12.0 (harness, core, web).
    - `fcmp_web` builds, and its `fcmp-engine.wasm` is byte-identical to FC/build-web's (545,704 bytes).
    - `fcmp_web_facade_check` builds.
    - `fcmp_web_editor_check` compiles 32 FunkGui sources and everything of ours except the four views.
    - 103 compiles took 15.5 s wall at `-j3`; the tree is 11 MB.

**1.5 CI** (FC/.github/workflows/ci.yml)
23. Jobs: `dsp` (:40-63), `plugin` (:65-95, `lead` preset, `verify.sh --integration --strict`), `linux-dsp` (:97-123), `linux-plugin` (:125-161), `web` (:163-219).
24. The web job runs on ubuntu-24.04 with emsdk 6.0.3 cached. It runs `cmake --preset web`, `cmake --build --preset web`, `verify.sh --strict build-web`, then prints simdbench/speed/tail and uploads `fcmp-engine.wasm`. It clones no FunkGui today.

**1.6 Disk and time**
25. Tree sizes:

| Tree | Size |
|---|---|
| FC `build-agent` (worktree relaxed-franklin, 29 Sep) | 939 MB |
| FC `build-agent-gui` (same worktree) | 1.1 GB |
| FC `build-dsp` | 156 MB |
| FC `build-web` today | 7.8 MB (11 MB with FunkGui and the compile checks) |
| FC `build-lead` | 299 MB, still being built by the gate |
| FC `build-release` | 199 MB |
| FG `build-agent` / `agent-gui` / `nojuce` / `web` / `lead` | 886 MB / 1.2 GB / 205 MB / 27 MB / 507 MB |

26. Free space is 20 GiB (`df`). The machine has 10 cores (4P + 6E) and 16 GB RAM.
27. From `.ninja_log`, a cold agent tree is about 25 core-minutes, and agent-gui about the same. The last agent gate there ran 491 tests in 716 s summed; the `ui.*` tests total 286 s, the slowest single one 16 s (`ui.input`).

## 2. What the card must do: the lead's skeleton, in this order

Everything below is in `S/skeleton.diff`. Steps 1 to 4 keep every native tree as it is; step 5 changes only the web tree.

1. **`cmake/FcmpSources.cmake`**, after :105, two globs:
   - `FCMP_WEB_FACADE_SOURCES` = `Source/web/facade/*.cpp` (flat, like the engine's).
   - `FCMP_PLUGIN_PORTABLE_SOURCES` = `Source/plugin/portable/*.cpp` (the part of `FCMP_PLUGIN_SOURCES` the web shares).
2. **`cmake/FcmpWeb.cmake`**, before the `fcmp_web` block: the native hook, so no edit to FcmpProbes.cmake is needed.
   ```cmake
   if(TARGET fcmp_probe_plugin)
     if(TARGET fcmp_web_engine_lib)
       target_link_libraries(fcmp_probe_plugin PRIVATE fcmp_web_engine_lib)
     endif()
     if(FCMP_WEB_FACADE_SOURCES)
       target_sources(fcmp_probe_plugin PRIVATE ${FCMP_WEB_FACADE_SOURCES})
       fcmp_warn_sources(${FCMP_WEB_FACADE_SOURCES})
     endif()
   endif()
   ```
   The engine comes in as the same archive `fcmp_web_check` holds to the 112 goldens. The facade comes in as sources, because it needs FunkGui's include path and `Source/`, which the target already has.
3. **`cmake/LintDeps.cmake`**: `web.facade` active from the start (no files yet, so it passes). `editor.juce` present but behind `FCMP_LINT_EDITOR_JUCE`, because today it reports 48 violations, all in the four views and `PresetBrowser.h`. Exact text:
   ```cmake
   # in the flags:       set(_in_web_facade FALSE) ... elseif(_rel MATCHES "^web/facade/") set(_in_web_facade TRUE)
   # in elseif(_in_editor), after editor.gpu:
   if(FCMP_LINT_EDITOR_JUCE AND NOT _in_editor_gpu)
     if(_inc MATCHES "(^|/)(juce_[^/]*|JuceHeader\\.h)(/|$)")
       _lint_fail("${_f}" ${_n} editor.juce "editor/ outside gpu/ is JUCE-free: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)funkgui/(juce|presets)/" OR _inc MATCHES "(^|/)funkgui/params/JuceParamPort\\.h$")
       _lint_fail("${_f}" ${_n} editor.juce "editor/ outside gpu/ includes no FunkGui header that needs JUCE: ${_code}")
     endif()
     string(REGEX REPLACE "juce::Component[ \t]*\\*" "" _nojc "${_code}")
     if(_nojc MATCHES "(^|[^A-Za-z0-9_])(juce::|namespace[ \t]+juce([^A-Za-z0-9_]|$)|JUCE_[A-Z0-9_]+|jassert)")
       _lint_fail("${_f}" ${_n} editor.juce "editor/ outside gpu/ names nothing of JUCE's (juce::, JUCE_*, jassert; juce::Component* excepted): ${_code}")
     endif()
   endif()
   # after the web.engine block:
   if(_in_web_facade)
     if(_inc MATCHES "(^|/)emscripten(/|\\.h$)" OR _inc MATCHES "^(wasm_simd128|emscripten)")
       _lint_fail("${_f}" ${_n} web.facade "the web facade includes no Emscripten header: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)editor/")
       _lint_fail("${_f}" ${_n} web.facade "the web facade never includes editor/: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)plugin/" AND NOT _inc MATCHES "(^|/)plugin/(portable/[^/]+|ProcessorFacade\\.h)$")
       _lint_fail("${_f}" ${_n} web.facade "the web facade includes only plugin/portable/* and plugin/ProcessorFacade.h from plugin/: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)funkgui/" AND NOT _inc MATCHES "(^|/)funkgui/params/ParamPort\\.h$")
       _lint_fail("${_f}" ${_n} web.facade "the web facade includes no FunkGui header but ParamPort.h: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)web/engine/" AND NOT _inc MATCHES "(^|/)web/engine/WebProtocol\\.h$")
       _lint_fail("${_f}" ${_n} web.facade "the web facade shares only web/engine/WebProtocol.h with the engine: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)fcdsp/engine/EngineHost\\.h$" OR _code MATCHES "(^|[^A-Za-z0-9_])EngineHost([^A-Za-z0-9_]|$)")
       _lint_fail("${_f}" ${_n} web.facade "the web facade reaches the engine only through an EngineLink, never fcdsp::EngineHost: ${_code}")
     endif()
   endif()
   ```
   - Tested on the real tree: 0 violations with the switch off; with it on, exactly the 48, and `Panel.cpp:174`'s `juce::Component*` forwarder passes.
   - Tested on a fake tree: every clause fires, and a comment naming `EngineHost` does not.
   - Add the two rules to the header comment at :27-39.
   - At E-2's merge, pass `-DFCMP_LINT_EDITOR_JUCE=ON` in FcmpProbes.cmake:204, or drop the switch.
4. **Probe files need no CMake.** W-F adds `Tools/probes/plugin/{webnull,webpresets,ui_web}.cpp` with first lines such as:
   - `// FCMP_PROBE layer=proc name=webnull scope=mode timeout=120`
   - `// FCMP_PROBE layer=proc name=webpresets scope=global timeout=120`
   - `// FCMP_PROBE layer=ui name=web scope=global timeout=300`
5. **Web gets FunkGui** (first step of Sprint D, cheap to land now).
   - `cmake/FcmpDeps.cmake`:
     - Close the web `if` after the Emscripten row (:251) instead of `else()`.
     - Give the FunkGui block a web branch that sets `FUNKGUI_WITH_JUCE OFF`, `FUNKGUI_WITH_BGFX OFF`, `FUNKGUI_WITH_PRESETS OFF`, `FUNKGUI_HARNESS_ONLY OFF`, `FUNKGUI_BUILD_TOOLS OFF`. The existing lines :254-266 become its `else()`.
     - In `_fg_need` (:304-307): `if(FCOMPRESSOR_WEB)` core + web, `elseif(NOT FCOMPRESSOR_DSP_ONLY)` core + presets.
     - The scratch diff uses `if(TRUE)` to keep the diff small; de-indent properly.
   - `cmake/FcmpWeb.cmake`, web only, two compile-only libraries:
     - `fcmp_web_facade_check`: STATIC; portable + facade sources; links `fcdsp FunkGui::harness`; `FCMP_WARNING_FLAGS` target-wide. Add it to `fcmp_web`: it is green at the base and from W-F's first file.
     - `fcmp_web_editor_check`: STATIC, EXCLUDE_FROM_ALL; `FCMP_EDITOR_SOURCES` + portable + facade; `fcmp_warn_sources` per file; links `fcdsp FunkGui::core` PRIVATE; `funkgui_configure_product`. It is red at the base on exactly the four views. E-2 builds it with `cmake --build build-web --target fcmp_web_editor_check`. Add it to `fcmp_web` when E-2 merges.
6. **At integration:** bump the pin to v0.13.0, switch `editor.juce` on, add `fcmp_web_editor_check` to `fcmp_web`, and bless only if a card produced golden rows (see Q2).

**For Sprint D, so C does not block it**
- Follow the native pattern: `fcmp_web_ui` and `fcmp_probe_web` each link `FunkGui::core` and compile everything themselves. That costs about 65 extra translation units, roughly 10 s. A shared static library also works (fact 10).
- The lead's edits in FcmpProbes.cmake:
  - Prefix `${FCMP_NODE}` to the probe command inside the wrapper (:145-149), and move `FCMP_NODE` out of FcmpWeb.cmake:159-165.
  - Accept `platform=` as a comma list with `web` (:167-168).
  - No `fcmp_probe_dsp` and no `lint.headers` for web.
- Choose web probe sources as the files whose `FCMP_PROBE` line says `layer=ui`, plus the JUCE-free helpers (`FakeFacade.cpp`, `EngineFacade.cpp`, the loopback link). `ui_dump.cpp` stays out.

## 3. Traps

1. **`editor.juce` cannot be active in the skeleton.** It fails lint.deps for every card until E-2's views merge (48 violations today). Hence the switch.
2. **`#if JUCE_MAC` goes silently false once the JUCE include is removed.** Sites: `EditControls.cpp:86`, `:95`; `ui_edits.cpp:132`, `:190`. On macOS that flips CMD to CTRL with no compile error. The lint's `JUCE_[A-Z0-9_]+` clause catches the editor ones. For probes, the expectation must come from the host's `commandKeyIsMeta()`, which HeadlessHost can set (FG/include/funkgui/panel/HeadlessHost.h:100-102), or the rows differ under wasm in Sprint D.
3. **An include rule alone is not enough for `editor.juce`.** `<funkgui/prefs/UiPreferences.h>` includes `juce_data_structures` when `FUNKGUI_HAS_JUCE` is 1 (FG/include/funkgui/prefs/UiPreferences.h:5-6). `Panel.cpp:35` and `AnimationModel.cpp:7` include it, so a `juce::String` there would build natively and fail only in the web build. Hence the token clause.
4. **`LoopbackLink` must not live in the directory the browser module globs if it includes `web/engine/WebEngine.h`.** Every `fcmp_web_*` function carries `export_name` under `__wasm__` (Source/web/engine/WebEngine.h:27). Linking `WebEngine.o` into the editor module would export the engine's ABI from it and keep the whole engine alive (about 0.5 MB). The `web.facade` rule above forbids that include; see Q1.
5. **`FunkGui::core` is INTERFACE sources.** Never put `fcmp_warnings` target-wide on a target that links it, or FunkGui's sources meet our `-Werror` list; use `fcmp_warn_sources`. Every such target needs `funkgui_configure_product`. Linking `FunkGui::web` and `FunkGui::core` into two different static libraries would compile the core twice.
6. **The globs are flat** (`web/facade/*.cpp`, `probes/plugin/*.cpp`). A manifest that OWNS `Source/web/facade/**` invites sub-directories that never compile.
7. **CI runs `--strict` and Linux has no overlay.** Any new golden row must be blessed before the PR is green, and must be equal on arm64 macOS and x86-64 Linux, or it is the repository's first overlay. Spec rows avoid both (fact 14).
8. **`ctest -L web` is a regex.** It will also select `probe:ui.web` and `probe:proc.web*`. Use `-L '^web$'` or `-R`.
9. **wasm32 has a 32-bit `size_t`.** `-Wshorten-64-to-32` fires where native builds are silent (`ui_truth.cpp:498`). W-F's facade and E-2's views will meet this only in the web compile checks.
10. **Emscripten's default stack is 64 KB.** `fcmp_web_check` raises it to 1 MiB (FcmpWeb.cmake:72). Sprint D's node probes run a whole Panel and should get a native-sized stack (`-sSTACK_SIZE=8388608`) and `-sSTACK_OVERFLOW_CHECK=1`. Exceptions are compiled but never caught by default, so a throw aborts; that reads as a blocking failure, which is acceptable.
11. **`golden.py adopt` refuses wasm32 candidates.** If musl moves a `ui.*` row in Sprint D, the overlay is written by hand, or FunkGui's tool learns it in v0.13.0.
12. **Shared Emscripten cache.** A first `-flto` link of a new kind generates system libraries there. Sprint C's compile-only libraries link nothing. Before Sprint D the lead should link once; my scratch run already created `lto/libGL-webgl2.a`.
13. **Six agents on 10 cores means 36 compile jobs.** The wall-clock rows (`proc.latency`, `dsp.hostile`, native `web.engine.tail`) read `FCMP_TIMING_SCALE` (Tools/probes/common/Tolerances.h:112-118). Export `FCMP_TIMING_SCALE=3`, as CI does, or expect false failures.
14. **Frozen files stay untouched.** `ProbeMain.cpp` and `ProbeRegistry.h` are FZ0; nothing here needs them changed (the usage text still says `arm64|x86_64`, which is cosmetic). E-2's view headers are in `lint.headers`' list (Scripts/check-headers.sh header comment) and must still compile standalone.

## 4. How to verify

**Existing coverage**
- `lint.deps` runs in every native tree, including dsp, with the new rules.
- `web.engine.*` and `web.simd` hold the engine archive that `fcmp_probe_plugin` now links.
- Every `ui.*` golden proves E-2 changed nothing.
- `skeleton.*` rows in `ui.geometry` cover PreviewWorker.

**New rows worth adding**
- `proc.webnull.<key>`: WebFacade + loopback + WebEngine bit-equal to `Processor`, as spec rows.
- `proc.webpresets`: rows, current, modified and values after apply against the processor's PresetAccess, as spec rows.
- `ui.web`: must be JUCE-free at file level (HeadlessGuiScope, no `Processor`), so Sprint D can run it under node unchanged.
- E-2's menu, chooser and clipboard rows in `ui.edits` and `ui.presets`, as spec rows on HeadlessHost's log.

**Card acceptance commands**
- W-F: `[AGENT]`, plus `cmake --workflow --preset web-verify`, which builds `fcmp_web_facade_check` as wasm32 under `-Werror` in about 20 s.
- E-2: `[AGENT]`, `[GPU]`, the lint script with `-DFCMP_LINT_EDITOR_JUCE=ON` reporting 0 violations, and `cmake --preset web && cmake --build build-web --target fcmp_web_editor_check`.
- G-D's cross-verify can add the same web configure and build against its worktree; it costs 11 MB and 15 s.

**CI**
- `dsp`, `linux-dsp`: unchanged apart from the two lint rules.
- `plugin`, `linux-plugin`: gain the three probes and E-2's rows under `--strict`.
- `web`: configure now clones FunkGui at the pin from GitHub; the build gains the facade check, and the editor check at integration. No workflow edit is needed for Sprint C.

**Cannot be automated**
- The native JUCE menus, choosers and clipboard (lead, by hand, on macOS and Linux).
- Anything that draws in a browser.

## 5. Open questions for the lead

**Q1. Where does `LoopbackLink` live?**
- (a) `Source/web/facade` (the plan). The rule must then allow `web/engine/WebEngine.h`, and Sprint D must filter the file out of the browser module.
- (b) `Tools/probes/plugin/LoopbackLink.{h,cpp}`, a probe helper like `EngineFacade`.
- (c) A new `Source/web/loopback/`.
- I recommend (b): the facade directory is then exactly what ships, and the lint text above holds as written.

**Q2. Spec-only or golden rows for the new probes and E-2's menu rows?**
- I recommend spec-only: no blessing, no overlay question, CI green on the PR. The plan's "bless the new rows" then has nothing to do.

**Q3. Does FunkGui enter the web configuration in Sprint C (step 5) or wait for D?**
- I recommend C: it is 84 lines, tested in scratch, leaves the engine module byte-identical, and gives E-2 and W-F a wasm32 compile check from the first minute.

**Q4. Does `ui_truth.cpp:498` get fixed by E-2 (which owns `ui_*.cpp` in C) or by W-N in D?**
- I recommend E-2, together with the two `juce::Thread::sleep` sites it must replace anyway.

**Q5. How many building agents?**
- Budget 1 GB per JUCE tree, 0.2 GB per JUCE-free native tree, about 0.03 GB per web tree.
- Per card:
  - FC card with `[AGENT]` only: about 1 GB.
  - FC card with `[AGENT]` + `[GPU]`: about 2.1 GB.
  - FunkGui card (agent, agent-gui, nojuce, web) plus its cross-verify FC tree: about 3.3 GB.
- E-2 + W-F + G-D come to about 6.5 GB. Three more `[AGENT]`-only cards make about 9.5 GB, leaving about 10 GB for the lead's trees.
- Six agents fit in 20 GiB only if no more than about 12 JUCE trees exist at once.
- Removing the stale trees in `.claude/worktrees/relaxed-franklin-cbbd5f` would free 2.0 GB; that is your call.

**Q6. A fourth card in a second wave?**
- With six agents allowed, a card could build the node probe executable after E-2 merges. Fact 20 says the probe sources are four small fixes away from compiling. The lead's FcmpProbes edits for web would have to land first.

## Not checked
- The native hook (step 2) was not built: no JUCE configure was run. It rests on reading CMakeLists.txt:130-133 and FcmpProbes.cmake:83.
- No full editor wasm was linked, and no real `ui_*` probe ran under node; only a two-row `ui.font` stand-in did.
- wasm32 warnings inside the four views.
- `FunkGui::web`'s link options on a node executable.
- CI itself: whether GitHub holds the pin commit for the web job, and the emsdk cache.
- Linux native.
- FG's own `web` preset was not re-run.
- `build-lead`'s final size (the gate was mid-build).
- Whether musl moves any `ui.*` row.
- Tree sizes come from a two-day-old worktree and FG's main checkout, not from a fresh build.