# Web demo: the judged plan (ADR-93 design pass, 2026-10-01)

Status: carried out. Sprints A to D and the lead phase ran from 2026-10-01 to 2026-10-02 (PRs #64 to #68); the demo is
published on GitHub Pages from `main`. ADR-93 in `docs/DECISIONS.md` records what was built; where it differs from
this plan, ADR-93 is right.

Synthesised by a judge from three independent designs over the five scout reports in this directory. The lead's
sprint manifests (docs/sprints/web-a.md, ...) are binding where they differ.

## Recommended plan

Paths: FC = /Users/seanfunk/audio/plugins/FCompressor, FG = /Users/seanfunk/audio/libraries/FunkGui. I read code only; nothing was built, so every wasm expectation below is still untested.

**The plan in one paragraph.** Add `web` as a fourth build configuration that produces two wasm modules joined by a MessagePort, with no JUCE, no bgfx, no pthreads and no SharedArrayBuffer. `fcmp-engine.wasm` (fcdsp plus a C ABI) runs in an AudioWorklet. `fcmp-ui.wasm` (the unchanged Panel, a WebFacade, FunkGui core without JUCE, a WebGL2 sink) runs on the main thread. All three designs and all five scouts agree on this shape. I take the "maintainable" design as the base, cut its first milestone down to the "fastest" design's engine check, and add the "parity" design's native bit-equality probes.

**What I checked, and what it changed**
- **The FunkGui pin is already v0.11.1** (FC/cmake/FcmpDeps.cmake:31-33; FG/CMakeLists.txt:18; tag v0.11.1 at 923ec44). The maintainable design's "tag v0.11.1 first" is stale, so the next tags are v0.12.0 and v0.13.0.
- **There are 30 parameters, not 29** (FC/Source/fcdsp/params/Pid.h:15-25, `kCount // 30`). The parameter message and the facade hold 30 plain values.
- **A new `web` probe layer would hide golden drift.** verify.sh treats only `dsp|proc|ui` as probes (FC/Scripts/verify.sh:174, :184), the FCMP_PROBE grammar admits only those (FC/cmake/FcmpProbes.cmake:167), and the wrapper turns exit 2/3 into 0 (:121). The new probes are therefore named `proc.webnull`, `proc.webpresets` and `ui.web`.
- **Plain exit-code tests labelled `verify` already work under verify.sh** (it judges non-probe tests by exit status, verify.sh:184 onward). The fastest design's doubt is resolved.
- **`dsp.print` never goes silent**: a -12 dBFS sweep runs for the whole 4 s (FC/Tools/probes/dsp/print.cpp:10-13, :62-80). Its 112 hashes therefore test the fma arithmetic, not denormal handling, and are the right first oracle.
- **`xarch.simd.ops.hash` cannot pass without emulating denormals-are-zero on every op.** It runs denormal operands through every op (FC/Tools/probes/dsp/simd.cpp:159-171, :198-262) and native arithmetic reads them as zero (FC/Source/fcdsp/core/Simd.h:95-97). Simd.h already documents that arm64 and x86 differ on tininess (:93-94).
- **Six colour stages and the host ramps are scalar code** (no `simd::` in Bright.h, DiodeAsym.h, LoudClip.h, OctoDist.h, TubeSym.h, VcaBus.h, host/Ramps.h, host/Crossfade.h). A SIMD-level flush cannot reach them. fcdsp has no scalar `std::fma`, so musl's fmaf is not on the audio path.
- **The menus are flat lists** of id, label, enabled, checked, separator (FC/Source/editor/views/EditControls.cpp:301-326; PresetBrowser.cpp:1215-1294). A host-neutral `showMenu` is small.
- **All 23 `ui_*` probes use JUCE only for the include and `ScopedJuceInitialiser_GUI`**, plus two `Thread::sleep` calls (e.g. FC/Tools/probes/plugin/ui_geometry.cpp:47, :243, :283). The real probes can run under node.
- **Emscripten's libc++ has thread support with pthread stubs** (`system/lib/libcxx/include/__config_site`:7, :10; `system/lib/pthread/library_pthread_stub.c`). `std::thread` code should compile without `-pthread`; the web build must use `syncPreview` (FC/Source/editor/Panel.h:87).
- **Settings prints "METAL" on Linux too** (FC/Source/editor/views/Settings.cpp:489), so `RenderInfo::renderer` fixes an existing fault.
- **The lo/hi/mid print sets pass through libm** (`std::pow`, `std::log` in FC/Source/fcdsp/params/HostParams.cpp:123-124, :140-141). A musl difference there would move those hashes without any DSP difference.

**Decisions**
1. **Arithmetic.** Add a native WASM SIMD128 backend in Simd.h, ScopedFtz.h and FastMath.h, bodies only.
   - fma/fms are exact (one rounding), behind a switch `FCDSP_WASM_FMA_UNFUSED`.
   - Results of add, sub, mul, div, fma and fms below FLT_MIN become signed zero, detected after rounding (the x86 rule Simd.h already allows).
   - No denormals-are-zero emulation. The C ABI zeroes denormal input samples instead.
   - One untested idea of mine for the card: do the fma as a plain f64 multiply-add and take the full round-to-odd path only when a lane lands exactly on a float midpoint. That is about 20 SIMD operations instead of 55, by my count.
2. **First oracle.** A small executable `fcmp_web_enginecheck` drives the C ABI in 128-frame quanta over the print material and is compared with the existing 112 golden rows. It is built natively too, which separates wrapper errors from arithmetic errors. It also prints a hash of each raw parameter set, so a libm difference is told apart from a DSP one.
3. **DSP probes under node are deferred.** No Harness change is needed for the first milestone and `xarch.simd.ops.hash` is not re-blessed.
4. **Editor.** Menus, file choosers and the clipboard move behind new defaulted `funkgui::HostServices` calls. The views lose JUCE on every platform, menus work in the browser, and HeadlessHost can test them.
5. **Rendering.** A dedicated WebGL2 sink and WebHost live in FunkGui. The shader is generated from the same `.sc` text and the atlas is the committed macOS bake.
6. **Engine link.** One C++ byte protocol behind an `EngineLink`. JavaScript only moves bytes. A loopback link lets native probes prove WebFacade against `Processor`. Telemetry is pulled once per drawn frame in a recycled buffer.
7. **Editor verification.** The real `ui.*` probes run under node against the base goldens, which needs a three-line Harness change (FG/include/funkgui/test/Harness.h:141, :858, :1098).
8. **Preview.** Start with `syncPreview` and measure. A Worker follows only if one request costs more than 8 ms.

**Order.** Four sprints of at most three agents, then a lead phase. FunkGui gets two tags: v0.12.0 (core without JUCE, services, sink, harness arch) and v0.13.0 (WebHost). Each sprint needs your authorisation.

**Your todos**
- Authorise Sprint A (or say you want v1.2.0 tagged first).
- Decide hosting and when the demo goes public.
- Decide the demo audio.
- After Sprint A's numbers: the arithmetic decision, only if the budget is missed.

## Base design

The "maintainable" design (web as a third platform: two wasm modules over a message port, an exact-arithmetic fcdsp backend, a WebGL2 sink in FunkGui).

Why it is the strongest base:
- **The editor is not forked.** The same `FCMP_EDITOR_SOURCES` glob (FC/cmake/FcmpSources.cmake:91-92) builds on every platform, and JUCE leaves the views through `HostServices` rather than through `#if`.
- **The web code cannot rot.** `Source/web/engine` and `Source/web/facade` name neither JUCE nor Emscripten, so they also compile into the native `fcmp_probe_plugin` and run in the ordinary agent gate.
- **The protocol exists once, in C++.** The worklet and page only move bytes.
- **Layout and lint rules match the repository's habits**: per-directory globs, variants chosen in CMake, new lint rules for each new directory.

What I change in it:
- Its phase 1 ports the whole DSP probe suite to node and re-blesses `xarch.simd.ops.hash`. I replace that with the print-hash check on the engine module and defer the probe port.
- Three FunkGui tags become two; its "tag v0.11.1 first" step is already done.
- Its `web` probe layer becomes `proc.web*` and `ui.web`, because verify.sh and the probe grammar know only `dsp|proc|ui`.
- Its second `fcmp-ui.wasm` instance in a Worker for step responses is deferred behind a measurement.
- 29 parameter values become 30.

## Grafted

**From "fastest"**
- **The first milestone needs no FunkGui.** `fcmp_web_enginecheck` drives the C ABI over the `dsp.print` material and is compared with the 112 existing golden rows, natively and under node. This is also the dsp scout's own recommendation.
- **Tests on the shipped binary**: `web.engine.abi` (node instantiates `fcmp-engine.wasm` with the worklet's own import stubs and runs `fcmp_web_selfcheck`), `web.engine.tail` (denormal cost on the x86 runner) and `web.engine.speed` (real-time factor per quality).
- **`PrintProgram.h`**: `program()` and `setOf()` move out of print.cpp (FC/Tools/probes/dsp/print.cpp:59-112) so the check and the probe render the same material.
- **`index.html?selftest=1`**: the worklet self-check, the atlas hash, `readPixels` against SoftRaster, and the flush-to-zero self-test, shown per browser.
- **The page states what differs from the plugin.**
- **Add/sub flushing and the unfused switch are decided by measurement**, not assumed.

**From "parity"**
- **Native bit-equality probes** in the JUCE build: `proc.webnull` (WebFacade, loopback link and WebEngine against `Processor`), `proc.webpresets`, `ui.web`. These names fit the existing probe grammar and verify.sh.
- **Telemetry is pulled**: the UI sends one recycled buffer per drawn frame and the worklet fills and returns it. Nothing is posted while the tab is hidden and the audio thread makes no garbage.
- **`PreviewCompute.{h,cpp}`** split from PreviewWorker.cpp (`compute()` at FC/Source/editor/PreviewWorker.cpp:219-237), so a Worker can be added later without touching the Panel.
- **`commandKeyIsMeta()` on HostServices**, so a Mac browser says CMD-Z (today `#if JUCE_MAC`, FC/Source/editor/views/EditControls.cpp:86, :95).
- **An end-to-end browser audio check**: the print material through the real worklet in an `OfflineAudioContext`, hashed against the goldens (lead-run).
- **A measured go/no-go** on arithmetic cost before the rest is built, folded into the end of Sprint A.

**From the scouts**
- The exact fma construction and the `pmin(b, a)` / `pmax(b, a)` mapping (dsp scout).
- The generated ES 3.00 shader text and the `alpha: false` context (render scout).
- The committed macOS atlas with a stale-blob test (funkgui scout).
- `em-config EMSCRIPTEN_ROOT` for the toolchain file (build scout).

**Mine**
- A fast-path exact fma (f64 multiply-add, slow path only on an exact float midpoint), to be tried in the backend card. Untested.
- `enginecheck` prints a hash of each raw parameter set, so a musl libm difference in `toPlain` is distinguishable from an arithmetic one.
- The lead warms Emscripten's system-library cache once before three agents share it.

## Rejected

**From "fastest"**
- **`#if FCMP_EDITOR_NATIVE_UI` in the four views and three frozen headers.** A per-target macro would change class layout in globbed sources, against the "chosen here, never by #if" rule (FC/cmake/FcmpSources.cmake:100). It also leaves Copy A to B and the Save As category unreachable in the demo. The HostServices route needs the same header revision and removes JUCE for good.
- **WebHost inside FCompressor.** It is the counterpart of `EditorHost`, which lives in FunkGui with the key table, wheel units and frame order. HardwareReverb could not reuse it and FunkGui's gallery could not test it.
- **`Tools/web/uicheck.cpp`, a second copy of `ui.geometry`.** The real probes need only a three-line Harness change and a mechanical JUCE-initialiser swap.
- **`float[29]`.** There are 30 parameters (Pid.h:15-25).
- **`#if defined(__APPLE__)` for the command key.** It prints CTRL in a Mac browser.

**From "parity"**
- **Full AArch64 flush emulation in the shipped backend** (denormal operands read as zero on every op). It taxes every arithmetic op, and its only purpose is `xarch.simd.ops.hash`, which the base plan does not run under wasm. It also cannot make scalar stages match.
- **A separate FCompressor `nojuce` native preset.** Compiling the web sources into `fcmp_probe_plugin` and running the `web` preset under node covers the same ground with one configuration fewer. FunkGui keeps its own `nojuce` preset.
- **A third wasm binary for previews from the start.** Deferred until the synchronous cost is measured.
- **A v0.12.0 tag carrying only the harness arch.** Folded into the JUCE-free core tag.
- **A scratch-only spike before any card.** The backend is about 120 lines; Sprint A delivers the same numbers as merged, tested code.
- **localStorage state and preset upload/download in the first version.** Follow-ups.

**From "maintainable"**
- **Porting `fcmp_probe_dsp` to node as phase 1.** It needs branches in five probe files, thread exclusions, and a re-bless of a contract row, all before the first measurement.
- **Re-blessing `xarch.simd.ops.hash`** with a denormal-free edge set. Not needed unless the probe port is done later.
- **A `web` probe layer.** Drift would be classified PASS (FC/Scripts/verify.sh:174, :184; FC/cmake/FcmpProbes.cmake:121, :167).
- **"Tag v0.11.1 first".** Already tagged and pinned (FC/cmake/FcmpDeps.cmake:31-33).
- **A second `fcmp-ui.wasm` instance in a Worker.** If a Worker is needed, a small standalone module is the lighter choice.
- **An HTML control strip as the no-WebGL2 fallback.** A message is enough on desktop browsers.

**From the scouts**
- **Emscripten's SSE or NEON emulation**: unfused fma, and the SSE `sel` fence (FC/Source/fcdsp/core/Simd.h:177) does not compile for wasm.
- **`-mrelaxed-simd`**: fusion would vary per visitor.
- **bgfx on WebGL2**: larger download, an alpha hazard, no context-loss handling, and it still needs a web branch in the GPU layer.
- **One shared-memory module**: needs cross-origin isolation headers, which rules out plain static hosting.

## Phases

### Sprint A: the engine in wasm, measured; FunkGui core without JUCE

**Deliverable**

**FCompressor**
- A `web` preset limited to fcdsp and the engine module, and `fcmp-engine.wasm`: a standalone module with no JS glue.
- The wasm SIMD128 backend: exact fma/fms behind `FCDSP_WASM_FMA_UNFUSED`, result flush on the six arithmetic ops, empty `ScopedFtz`, wasm bodies for `log2Split`, `scaleByPow2`, `xorSign`.
- `Source/web/engine`: the C ABI and `WebProtocol.h`. Messages: Params (30 plain values plus a snap flag), Attach, Reset. Reply: UiFrame, new history columns, gap flag, latency. A quality or lookahead change reconfigures inside the message handler, between quanta. Denormal input samples are zeroed. A silence gate resets the engine after the tail.
- `fcmp_web_enginecheck` and the node tests.
- A build-and-verify `web` CI job (artifact only), so the tail test runs on x86 hardware.
- ADR-93 draft with a measured table: rows matched, and real-time factor per Mode at ECO, STD and HQ for exact and unfused fma.

**FunkGui (card G-A)**
- Option `FUNKGUI_WITH_JUCE` (default ON), buildable natively and under Emscripten.
- The committed macOS atlas with loader, serialiser, tool mode and the `fg.font.baked` equality test.
- `UiPreferences` behind a storage backend (in-memory without JUCE, `setBackend()` hook).
- An Emscripten branch in `CLocale.cpp`; guards on `printable(juce::String)` and `writePng`.
- `HeadlessGuiScope` (holds JUCE's GUI initialiser when JUCE is present).
- Harness accepts `--arch wasm32` with a no-op `ScopedFtz`; `golden.py` knows `wasm32`.
- A native `nojuce` preset.

**Gate at the end.** Continue with exact fma if the worst Mode at HQ runs at least 4x real time under node on the lead's Mac and on the x86 runner. Otherwise stop and take the arithmetic decision to the user.

**Files**

**FC, lead (before the cards)**
- `CMakeLists.txt`: option `FCOMPRESSOR_WEB`; toolchain file from `em-config EMSCRIPTEN_ROOT` before `project()`; skip the Linux default-compiler block (:27-31) and the PIC property (:92-93) for web.
- `CMakePresets.json`: `web` configure/build/test, workflow `web-verify`.
- `cmake/FcmpPlatform.cmake` (:29-37), `cmake/FcmpArch.cmake` (:33-68: arch `wasm32`, `-msimd128`), `cmake/FcmpDeps.cmake` (no JUCE; FunkGui not fetched for web yet; Emscripten version pin).
- `cmake/FcmpSources.cmake`: globs for `Source/web/engine`, `Tools/web`.
- `cmake/FcmpWeb.cmake` (new): `fcmp_web_engine`, `fcmp_web_enginecheck`, the `web.*` tests, and `lint.deps` for the web configuration.
- `cmake/LintDeps.cmake`: rules `web.juce`, `web.engine`.
- `.github/workflows/ci.yml`: the `web` job. `docs/DECISIONS.md`: ADR-93 draft. `docs/sprints/` manifest.

**FC, card F-W**
- `Source/fcdsp/core/Simd.h`, `ScopedFtz.h`, `FastMath.h` (frozen: bodies only).
- `Source/fcdsp/core/FlushTiny.h` and the scalar stages under `Source/fcdsp/engine/stages/colour/` only if `web.engine.tail` fails.

**FC, card W-E**
- `Source/web/engine/{WebEngine.h,WebEngine.cpp,WebProtocol.h}`.
- `Tools/probes/common/PrintProgram.h`, `Tools/probes/dsp/print.cpp`.
- `Tools/web/enginecheck.cpp`, `web/tests/engine.mjs`.

**FG, card G-A**
- `CMakeLists.txt`, `cmake/FunkGuiDeps.cmake` (:169-173, :193-196), `cmake/FunkGuiTargets.cmake` (:234, :273-281, :304-310), `CMakePresets.json`, `test/CMakeLists.txt`.
- `include/funkgui/text/FontAtlasSdf.h`, `src/text/{FontAtlasSdf,FontService,LineEdit}.cpp`, `fonts/` (the blob), `tools/FontProbe.cpp` or `AtlasDump.cpp`.
- `include/funkgui/prefs/UiPreferences.h`, `src/prefs/**`, `src/core/CLocale.cpp`, `src/canvas/SoftRaster.cpp`.
- `include/funkgui/test/Harness.h` (:141, :858, :1098), `tools/golden.py` (:55), the new `HeadlessGuiScope`, `CHANGELOG.md`.

**Verification**

**Native, FCompressor**
- `cmake --workflow --preset dsp-verify` and `agent-verify` plus `Scripts/verify.sh`: zero golden drift (the three headers gain a branch only; `print.cpp` keeps its hashes).
- `web.engine.print` in the `dsp` preset equals the 112 rows of `tests/golden/base/modes/<key>/dsp.print.txt`. This proves the wrapper's block building before any wasm runs.

**Wasm, FCompressor**: `cmake --workflow --preset web-verify && Scripts/verify.sh build-web`
- `web.engine.print`: the same 112 rows under node. If only lo/hi/mid rows differ and the raw-set hashes differ too, the cause is musl's libm; the lead records it and blesses a web-only expected file with that cause.
- `web.engine.abi`: the shipped binary's import list is fixed and its self-check hash equals enginecheck's.
- `web.engine.tail`: 10 s of post-burst silence costs under 2x active signal per Mode with the gate off. The x86 CI runner is the run that counts.
- `web.engine.speed`: prints the table; fails below the gate.
- `lint.deps` with the two new rules.

**FunkGui**
- `tools/verify.sh` on `build-agent` and `build-agent-gui`: no golden row moves.
- `fg.font.baked` passes on macOS.
- The `nojuce` preset builds warning-free and its JUCE-free `fg.*` tests match the existing goldens.
- An em++ compile of the core succeeds.
- Cross-check: FCompressor's `verify.sh` in the lead-made `ro-` worktree against the candidate.

**Parallelism**

Lead first: the CMake skeleton, the manifest, and one warm-up build so Emscripten's system-library cache exists.

Then three agents at once:
- **F-W** (FCompressor): the fcdsp backend.
- **W-E** (FCompressor): the engine wrapper and checks. It works natively against the goldens from the start; its wasm rows go green after the lead merges F-W.
- **G-A** (FunkGui): core without JUCE.

No file is shared between the three. The lead integrates, runs the gate table, and writes the numbers into ADR-93.

### Sprint B: host-neutral services and the WebGL2 sink (FunkGui v0.12.0); model code out of JUCE

**Deliverable**

**FunkGui card G-B: services on `HostServices`** (non-pure, defaults refuse; the header is at FG/include/funkgui/panel/HostServices.h:34-80)
- `showMenu(items, anchorRect, callback)` and `dismissMenus()`; items are id, label, enabled, checked, separator.
- `chooseFiles(request, callback)`, `copyText(utf8)`.
- `services()`: a capability mask, so a view can disable a cell the host cannot serve.
- `commandKeyIsMeta()`: defaults to the platform.
- `EditorHost` implements them with `juce::PopupMenu` + `MenuLook`, `FileChooser` and `SystemClipboard`.
- `HeadlessHost` reports every service, logs the calls and takes scripted replies.
- `ownerComponent()` stays for HardwareReverb.

**FunkGui card G-C: `FunkGui::web` with `WebGlSink`** (Emscripten only)
- One draw call from `funkgui::expand`, stride 64, one R8 atlas texture, `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, context created with `alpha: false`.
- Rebuilds program, buffer and texture on context loss.
- A CMake script generates the ES 3.00 shader text from `shaders/*.sc`; two new `fg.shader.hash` rows cover it on every host.

**FCompressor card E-1** (no FunkGui dependency, no behaviour change)
- `PreviewWorker.cpp` on `std::thread`; the pure computation in `PreviewCompute.{h,cpp}`.
- `Source/plugin/portable/`: the new JUCE-free `FactoryData` (entry table and default filling) and `EditHistory` moved there. `FactoryBank.cpp` converts from `FactoryData`.
- `FakeFacade` reads `FactoryData`.
- `RenderInfo::renderer` replaces the hard-coded "METAL".

**Lead:** tag FunkGui v0.12.0 (G-A, G-B, G-C) and bump the pin.

**Files**

**FG, card G-B**
- `include/funkgui/panel/HostServices.h`, `include/funkgui/panel/HeadlessHost.h` (frozen at FZ1: additive, lead approval), `src/panel/HeadlessHost.cpp`, `src/gpu/EditorHost.cpp`, new tests under `test/`, `CHANGELOG.md`.

**FG, card G-C**
- `include/funkgui/web/WebGlSink.h`, `src/web/WebGlSink.cpp`.
- `cmake/FunkGuiTargets.cmake` (the Emscripten-only filter, the shader text script), `test/smoke/shader_hash.cpp`, `test/golden/base/global/fg.shader.hash.txt` (lead blesses the new rows).

**FC, card E-1**
- `Source/editor/PreviewWorker.cpp`, `Source/editor/PreviewCompute.{h,cpp}`.
- `Source/plugin/portable/{FactoryData,EditHistory}.{h,cpp}`, `Source/plugin/factory/FactoryBank.cpp`, `Source/plugin/Processor.cpp` and `Processor.h` (include paths).
- `Tools/probes/plugin/FakeFacade.{h,cpp}`.
- `Source/editor/SubView.h` (:168-176), `Source/editor/views/Settings.cpp` (:485-489), `Source/editor/gpu/Editor.cpp` (:72-83).

**FC, lead**
- `cmake/FcmpSources.cmake`: glob `Source/plugin/portable/*.cpp` (the plugin glob at :88 is not recursive).
- `cmake/LintDeps.cmake`: rule `plugin.portable`, and `editor.facade` allowing the moved header path where needed.
- `cmake/FcmpDeps.cmake`: the pin.
- Frozen-header revisions for `SubView.h` and `FakeFacade.h`.

**Verification**

**FunkGui**
- `tools/verify.sh` on `agent`, `agent-gui` and `nojuce`: no existing row moves. New rows only: the two generated-shader hashes and the HeadlessHost service tests.
- An Emscripten build of core plus `FunkGui::web` compiles.
- Cross-check in the `ro-` worktree: FCompressor's `verify.sh` passes against the candidate.

**FCompressor**
- `cmake --workflow --preset agent-verify && Scripts/verify.sh build-agent`: zero golden drift. `skeleton.preview.{sync,async}` and `skeleton.teardown.worker` cover the thread swap; `proc.presets` and the factory revision are unchanged.
- `agent-gui-verify` builds.
- Both Linux CI jobs pass.
- `web-verify` still passes (Sprint A's tests).
- Lead, once in the Standalone: CHARACTERISTICS step responses still update while dragging; Settings shows METAL on macOS.

**Parallelism**

Three agents at once:
- **G-B** (FunkGui): services.
- **G-C** (FunkGui): the sink. It needs G-A merged, which the lead does at the end of Sprint A.
- **E-1** (FCompressor): model code.

G-B and G-C touch disjoint FunkGui files (panel and `gpu/EditorHost.cpp` against `web/` and CMake). The lead tags v0.12.0 and bumps the pin when both pass.

### Sprint C: the views leave JUCE; the web facade proven natively; FunkGui WebHost (v0.13.0)

**Deliverable**

**FCompressor card E-2** (needs pin v0.12.0)
- `EditControls`, `PresetStrip`, `PresetBrowser` and `Settings` use the HostServices calls.
- They lose their JUCE and `MenuLook` includes, the `menuLook_` and `chooser_` members, and the `ownerComponent()` early returns.
- `#if JUCE_MAC` becomes `commandKeyIsMeta()`.
- IMPORT and EXPORT are enabled only when `services()` reports a chooser.
- Every `ui_*.cpp` probe uses `HeadlessGuiScope`.
- New spec rows in `ui.edits` and `ui.presets` exercise Copy A to B, the Save As category and COPY REPORT through HeadlessHost's log.
- Lint rule `editor.juce`.

**FCompressor card W-F**
- `Source/web/facade`: `WebFacade` (30 plain values; ports store `fcdsp::toPlain`; a batch is one message with the snap; a mirror `HistoryRing` fed by posted columns), `WebPresets` (FactoryData rows, then session user rows; apply in one batch with the Mode first), `EngineLink` and `LoopbackLink`, the `EditHistory` host.
- Probes `proc.webnull`, `proc.webpresets`, `ui.web`.

**FunkGui card G-D: `WebHost`**
- requestAnimationFrame clock with the 60/12 Hz cadence and a 10 Hz idle.
- Canvas sizing: `info.dpi = physH / 640`, zoom steps fitted to the window.
- Pointer, wheel (JUCE's units) and key conversion; CSS cursor.
- A DOM menu, `navigator.clipboard`, a localStorage prefs backend.
- A gallery page.

**Lead:** tag v0.13.0 (WebHost) and pin it.

**Files**

**FC, card E-2**
- `Source/editor/views/{EditControls,PresetStrip,PresetBrowser,Settings}.{h,cpp}` (FZ4 headers: private members only, lead revision).
- `Source/editor/Panel.cpp` (the undo chord, :700-701).
- `Tools/probes/plugin/ui_*.cpp`.

**FC, card W-F**
- `Source/web/facade/**`.
- `Tools/probes/plugin/{webnull,webpresets,ui_web}.cpp`.

**FC, lead**
- `cmake/FcmpSources.cmake`: `Source/web/{engine,facade}` into `fcmp_probe_plugin`.
- `cmake/LintDeps.cmake`: `editor.juce`, `web.facade` (no `EngineHost` token, no Emscripten header).
- `cmake/FcmpDeps.cmake`: the pin.
- `tests/golden/**`: bless the new rows.

**FG, card G-D**
- `include/funkgui/web/WebHost.h`, `src/web/**` (C++ and the JS library), `tools/GalleryApp` (web page), `CMakePresets.json` (`web`), `CHANGELOG.md`.

**Verification**

**E-2**
- `agent-verify` plus `verify.sh`: zero drift on every existing row (the `ui.*` rows are FZ5-frozen, so this is the proof of no behaviour change). The new menu rows arrive as candidates with a one-line reason.
- `lint.deps` passes `editor.juce`. `agent-gui-verify` builds. `gui-live.sh` 5/5.
- Lead, in the Standalone: the four menus, import, export and COPY REPORT by hand.
- PNGs of each changed view in one stepped and one continuous Mode.

**W-F**
- `proc.webnull`: output bit-equal to `Processor` over a scripted program with parameter changes, a batch, a preset apply and a quality change.
- `proc.webpresets`: rows, current, modified and values after apply agree with the processor's PresetAccess.
- `ui.web`: presets, undo and redo, A/B, a typed value and the keyboard chords over WebFacade.

**G-D**
- FunkGui `verify.sh` on `agent`, `agent-gui`, `nojuce`; the `web` preset builds.
- Lead, on the gallery page in Chrome and Safari: live PrimList dumps equal the headless ones, `readPixels` within tolerance of SoftRaster, a forced context loss recovers, input behaves as in the native gallery.

**Parallelism**

Three agents at once:
- **E-2** (FCompressor): views and `ui_*` probes.
- **W-F** (FCompressor): new directories and three new probe files.
- **G-D** (FunkGui): WebHost.

E-2 and W-F share no file. The lead tags v0.13.0 and bumps the pin at the end.

### Sprint D: the editor in wasm under node, and the page

**Deliverable**

**Card W-N (with the lead's CMake)**
- The web configuration links FunkGui with `FUNKGUI_WITH_JUCE=OFF`.
- `fcmp_probe_web`: the editor sources, `Source/plugin/portable`, `Source/web/{engine,facade}`, FakeFacade and every `ui_*` probe, run under node with `--arch wasm32`.
- `ui.font` also registers on web and must report the macOS atlas hash.
- A measured cost of one synchronous preview request.

**Card W-U**
- `Source/web/ui`: `WebMain` (facade, `Panel(facade, {.syncPreview = true})`, WebHost, `setRenderInfo` with "WEBGL2", `Panel::shutdown()` on teardown) and `PortLink`.
- The page:
  - a Start overlay;
  - `new AudioContext({ sampleRate: 48000 })` created on the click;
  - the worklet, which receives the wasm bytes and reads the frame count from the output array;
  - a looping source from the built-in loop or a dropped file;
  - the plugin's own BYPASS for comparison;
  - a list of what differs from the plugin;
  - licences and a link to the built commit;
  - `?selftest=1`.
- Targets `fcmp_web_ui` and `fcmp_web_site`; test `web.size`.

**Files**

**FC, lead**
- `cmake/FcmpDeps.cmake`: FunkGui options for web.
- `cmake/FcmpProbes.cmake`: the node emulator inside the `/bin/sh` wrapper (:145-149); `platform=` as a comma list over `apple|linux|web` (:168); `fcmp_probe_web`; a timeout scale; `lint.headers` not registered for web (:204-221).
- `cmake/FcmpWeb.cmake`, `cmake/FcmpSources.cmake` (`Source/web/ui`).
- `cmake/LintDeps.cmake`: rule `web.emscripten` (Emscripten headers only under `Source/web/ui`).

**FC, card W-N**
- `Tools/probes/plugin/ui_font.cpp` (`platform=apple,web`).
- Any `ui_*.cpp` that needs a node-specific fix.

**FC, card W-U**
- `Source/web/ui/{WebMain.cpp,PortLink.cpp}`.
- `web/{index.html,main.js,fcmp-worklet.js,demo.css,tests/}`.

**Verification**

`cmake --workflow --preset web-verify && Scripts/verify.sh --strict build-web` exits 0:
- Every `ui.*` row equals `tests/golden/base` on wasm32 (FakeFacade through HeadlessHost), `ui.font` included. A row that musl's libm moves gets a `wasm32` overlay with its cause.
- Sprint A's engine tests still pass.
- `web.size` is within the budget set from this first full build.

The native gates stay green with no drift.

Preview: if one synchronous request costs more than 8 ms under node, the preview Worker follow-up is scheduled before publishing.

Page, served from 127.0.0.1 in Chrome on the lead's Mac:
- The loop plays and a drag changes the sound.
- `?selftest=1`: the worklet self-check equals the node value, and the atlas hash equals the macOS hash.

**Parallelism**

Two agents plus the lead:
- **W-N**: probes under node. It needs E-2 merged and pin v0.12.0.
- **W-U**: the page. It can start against a lead-made FunkGui pin worktree and needs v0.13.0 for its final run.

They share no file; the lead owns the CMake both depend on and lands it first.

### Lead phase: browser gate, CI, documents, publish

**Deliverable**

- `Scripts/web-live.sh` (lead only, like `gui-live.sh`): serves `build-web/site` on 127.0.0.1 and drives headless Chrome on `?selftest=1`.
- The CI `web` job at full scope, uploading `build-web/site` as an artifact, with emsdk pinned to the version `FcmpDeps.cmake` asserts.
- ADR-93 final: the platform, the exact-fma and flush decision with the measured numbers, the reconfigure-in-the-worklet deviation from the real-time rules, the degradations.
- ARCHITECTURE's platform list, 03's configuration and gate text, README.
- CLAUDE.md: a `[WEB]` command tag, `web/**` and `.github/**` in the lead-only list, the new layer rules.
- `pages.yml` only after the hosting decision.

**Files**

**FC**
- `Scripts/web-live.sh`.
- `.github/workflows/ci.yml`; `.github/workflows/pages.yml` (after the decision).
- `docs/DECISIONS.md`, `docs/ARCHITECTURE.md`, `docs/design/03-build-verify-process.md`.
- `README.md`, `CLAUDE.md`.
- `web/audio/` with a CREDITS file, if loops are supplied.

**Verification**

**`web-live.sh` in headless Chrome**
- PrimList fingerprints of the six views equal the node values.
- `readPixels` is within 2/255 of SoftRaster.
- An `OfflineAudioContext` render through the real worklet equals the print hashes.
- The flush-to-zero self-test result and the worklet load are recorded.

**By hand in Chrome, Safari and Firefox on macOS, and on one Intel or AMD machine**
- Every screen opens.
- Presets, undo, A/B, a typed value, the menus.
- A QUALITY change takes effect.
- A dropped file plays.
- A forced context loss recovers.
- PNGs of each view in one stepped and one continuous Mode.

**CI**
- Green on all five jobs for the pull request.
- The downloaded site artifact passes the self-test from a plain static server.
- After the decision: the public URL loads and plays in a clean browser profile.

**Parallelism**

Lead only. Publishing waits for the user's hosting decision.

### Follow-ups, each only on its trigger

**Deliverable**

- **Preview Worker** (trigger: a synchronous request over 8 ms, or visible jank on CHARACTERISTICS): a small standalone module built from `PreviewCompute.cpp` and fcdsp, in a Web Worker, behind an additive `PanelOptions::previewExecutor`.
- **DSP probes under node** (trigger: print hashes differ and need localising, or the lead wants the full DSP gate on wasm): wasm branches in `units`, `hostile`, `analysis`, `selftest`, `telemetry`; the `dsp.simd` / `dsp.simd_daz` split with one re-blessed row.
- **Persistence** (trigger: you ask for it): user presets and the last state in localStorage.
- **Preset import and export** as upload and download through `chooseFiles`.

**Files**

**FC**
- Preview: `Source/web/preview/**`, `Source/editor/Panel.h` (:84-89), `web/preview-worker.js`.
- Probes: `Tools/probes/dsp/{simd,simd_daz,units,hostile,analysis,selftest,telemetry}.cpp`, `tests/golden/base/global/dsp.simd*.txt`.
- Persistence and files: `Source/web/facade/WebPresets.cpp`, `Source/web/ui/**`.

**FG**
- `src/web/**` for file upload and download.

**Verification**

- Preview: `ui.chars` under node still equals its goldens through the worker path; dragging on CHARACTERISTICS holds the frame rate in Chrome's performance panel.
- Probes: `verify.sh --strict build-web` with every dsp row equal to base or listed in a `wasm32` overlay with its cause; native gates show exactly one re-blessed row.
- Persistence: a reload restores state and user presets.

**Parallelism**

One card each, independent of one another; any of them fits beside other work within the three-agent cap.

## Decisions that are the user's

- **Where is the demo hosted, and when does it become public?**
  Default: Until you say otherwise, CI only uploads the site as an artifact and nothing is published. When you are ready: GitHub Pages on Snipet/FCompressor, deployed on release tags and by manual trigger, not on every push to main. You switch Settings > Pages > Source to GitHub Actions once.
  Why the user's: Publishing makes the product public under your name, and `ci.yml` currently promises that nothing is published (FC/.github/workflows/ci.yml:14). Only you can enable Pages on the repository.
- **What audio does a visitor hear first?**
  Default: Ship the first version with file drop plus one loop the page synthesises itself, so no third-party audio enters the repository. Add 3-4 loops (drums, bass, vocal, mix) with a CREDITS file when you supply recordings you own.
  Why the user's: The repository is public GPL-3.0 and holds no audio today. Whatever ships beside it must be yours or clearly redistributable, and the choice of material is how the product presents itself.
- **Does this work start before v1.2.0 is tagged?**
  Default: Authorise Sprint A now: it only adds a branch to three fcdsp headers, new directories and a FunkGui option, and the native goldens prove nothing moved. Tag v1.2.0 before Sprint B's FCompressor card merges, because Sprints B and C rewrite PreviewWorker, FactoryBank and four views that v1.2 ships.
  Why the user's: You extend sprints one at a time, and v1.2.0 is unreleased (CMakeLists.txt still says 1.1.0 at :33). Whether release work or web work goes first is your priority call.
- **Only if Sprint A shows the exact arithmetic misses the budget: what gives way?**
  Default: If only HQ is too slow, keep the bit-identical arithmetic and leave HQ out of the demo. If STD is also too slow, ship the unfused build (inaudibly different), verified against a web-only expected file with a recorded null depth, and say so in ADR-93 and on the page.
  Why the user's: It trades the claim "the browser runs the plugin's exact DSP" against CPU cost on visitors' machines, and it is an exception to Simd.h's written contract (FC/Source/fcdsp/core/Simd.h:11-12, :19).
- **What may the first public version leave out?**
  Default: Desktop browsers with WebGL2 only (a plain message elsewhere). User presets last for the session. Preset import, export and file drops are disabled. No side-chain key input. DSP load may show a dash. All of this is listed on the page. Menus (Copy A to B, Save As category) and COPY REPORT are included.
  Why the user's: These are product-scope choices about what a visitor sees with your name on it; each can be added later as a follow-up card.

## Top risks

**Could change the design**
- **Exact fma cost is unmeasured.** It is about 55 SIMD operations against 2, at 159 call sites, and the scout's "a few percent to 10 %" is an estimate from operation counts. Sprint A measures it under node before anything else is built on it. Node's figure stands in for Chrome; Safari and Firefox are only measured in the lead phase.
- **Bit-identity under wasm is expected, not shown.** No fcdsp source has been compiled for wasm. The lo/hi/mid print sets also pass through musl's `pow` and `log` (FC/Source/fcdsp/params/HostParams.cpp:123-124, :140-141); the raw-set hashes in enginecheck tell that apart from a DSP difference.
- **Denormals on Intel and AMD.** The SIMD flush cannot reach six scalar colour stages and the host ramps. The silence gate only helps once input is exactly zero for a full tail. `web.engine.tail` on the x86 runner is the only automatic guard; this Mac cannot show the effect.

**Process**
- **Frozen and unreleased code.** Three FZ0 fcdsp headers, private members of FZ4 view headers, `SubView.h`, `FakeFacade.h`, `HeadlessHost.h` (FZ1) and v1.2's PreviewWorker, FactoryBank and views all change. Each change is body-only or additive, but they are lead revisions.
- **FunkGui is the critical path.** Nothing in the editor links for web before v0.12.0, and two tags mean two pin bumps with a cross-repo verify each.
- **Native menus were never covered by probes.** Moving them behind HostServices needs the lead's manual check on macOS and Linux; the new HeadlessHost rows cover the logic, not JUCE's popup.
- **Duplicated logic.** WebEngine and WebFacade restate `Processor`'s block building and batch rules. `proc.webnull` keeps them in step only while it stays in the gate.
- **The committed atlas needs a Mac** whenever `Glyphs.def` or the font subset changes; `fg.font.baked` catches a stale blob.

**Build (nothing was compiled)**
- Emscripten 6.0.3 is Clang 23; the `-Werror` list has not met it.
- Untested under Emscripten: `std::thread` and `std::mutex` without `-pthread` (I read the stubs, I did not link them), over-aligned `operator new` for EngineHost's Impl, `std::filesystem` under NODERAWFS, wasm exceptions in the harness.
- Whether emsdk offers exactly 6.0.3 for CI was not checked.
- Three agents share one Emscripten system-library cache.
- All wasm sizes are guesses until the first build.

**Browser facts taken from memory, not checked**
- `performance.now()` is missing in the worklet scope, so DSP load may not be shown.
- Compiling the wasm bytes inside the worklet constructor is allowed.
- `AudioContext({ sampleRate: 48000 })` is honoured everywhere; the code must fall back to the actual rate.
- An `OfflineAudioContext` source node copies samples exactly.
- Trackpad against notched wheel is a heuristic, and widgets branch on it.
- A QUALITY or LOOKAHEAD change allocates once on the audio thread and may click.
- Mobile GPUs, WebGL1 and touch are out of scope.

**Editor in the browser**
- **Synchronous preview** renders about 27.85 s of control-path audio per request on the main thread while CHARACTERISTICS is open. Sprint D measures it; the Worker follow-up is the remedy.
- **musl's libm may move a `ui.*` fingerprint** in the plots. Linux/glibc does not; the remedy is an overlay row with the cause.

**Legal**
- The page distributes GPL-3.0 object code and the OFL font, so it must carry both licences and a link to the exact source commit.
