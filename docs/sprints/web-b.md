# Web Sprint B — host-neutral services and the WebGL2 sink (FunkGui v0.12.0); model code out of JUCE: task manifests

Status: **sprint base**, written by the lead from the judged plan (`docs/sprints/web/plan.md`, "Sprint B") after
Sprint A merged (ADR-93; FCompressor `main` b46960d, FunkGui `main` 35c5ac9). Manifest format as in
`docs/sprints/web-a.md` (read by `Scripts/sprint/ownership.py docs/sprints/web-b.md#<ID> <worktree>`). The user
authorised this sprint ("go"); Sprint C needs a new authorisation.

Goal. Give FunkGui the two things the editor needs before it can leave JUCE: the services a view asks its host for
(menus, a file chooser, the clipboard) as plain `HostServices` calls, and a WebGL2 sink for the frame. In FCompressor,
move the model code the browser demo's facade will share out of JUCE. **Nothing here changes what the plugin does:**
the existing goldens prove it (zero drift is part of every card's acceptance).

**Base.** FCompressor: branch `web/sprint-b` at the commit that adds this file (on `main` b46960d): FZ0–FZ5 frozen;
FunkGui pin v0.11.1 = `923ec44b8c5fb6f5d46f855555eca815ab31f939`. FunkGui: `main` = `35c5ac9` (v0.11.1 plus the
JUCE-free core of Sprint A, untagged: `FUNKGUI_WITH_JUCE`, the committed atlas, the preferences backend, the presets
`nojuce` and `web`).

**The lead has already made** (FCompressor; read them first): the glob for `Source/plugin/portable/*.cpp`
(`cmake/FcmpSources.cmake`) and the lint rule `plugin.portable` (`cmake/LintDeps.cmake`).

**Read before writing anything:** `docs/sprints/web/plan.md` ("Sprint B"; this manifest is binding where they differ),
ADR-93 in `docs/DECISIONS.md`, and the scout report for your area in `docs/sprints/web/` (`map-editor.md`,
`map-funkgui.md`, `map-render.md`). The scouts' line numbers are as of an older `main`: find the code, do not trust
the numbers.

**Emscripten** 6.0.3 is installed (`em++`, `em-config`, node on PATH). Its system-library cache is shared by the
cards: a first build of a new variant prints "generating system library" and takes a while; if a build fails with a
cache lock or a half-written library, wait and retry rather than clearing the cache.

**Frozen set.** FZ0–FZ5 as in `docs/sprints/s13.md`. This sprint's approved revisions: G-B adds non-pure virtuals to
FunkGui's `HostServices.h` and members to `HeadlessHost.h` (FZ1: additive only, nothing existing changes); E-1 adds
one member to `RenderInfo` in `Source/editor/SubView.h` (FZ4) and changes `Tools/probes/plugin/FakeFacade.h`'s
includes and private members. FunkGui API additions are for the MINOR tag v0.12.0, which the lead makes.

**Lead-owned, as always:** FCompressor's `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `docs/**`,
`tests/golden/**`, `.github/**`, `README.md`, `CLAUDE.md`; FunkGui's `CHANGELOG.md`, `test/golden/**`, `SEED.tsv`,
`docs/PROVENANCE.md` and the VERSION in its `CMakeLists.txt` (it stays 0.11.1 until the lead tags). If a lead-owned
file has a bug that blocks you, make the minimal edit, keep its meaning, and list it under "ownership deviations
needing approval" in the handoff.

---

## G-B

```text
TASK    G-B   card: WEB-B.1   repo: FunkGui (lead-made worktree)   size: M
        worktree: /Users/seanfunk/audio/libraries/FunkGui.wt/web-b-gb   branch: web-b/g-b
GOAL    The services a Panel asks its host for (a popup menu, a file chooser, the clipboard) as HostServices calls,
        served by EditorHost over JUCE and by HeadlessHost for tests
OWNS    include/funkgui/panel/HostServices.h include/funkgui/panel/HeadlessHost.h src/panel/HeadlessHost.cpp
        include/funkgui/gpu/EditorHost.h src/gpu/EditorHost.cpp src/gpu/HostServices*.cpp src/gpu/HostServices*.h
        test/unit/host_services*.cpp test/gallery/** tools/GalleryApp/**
FROZEN  FunkGui's public headers are API: additive changes only, each listed in the handoff. HostServices.h and
        HeadlessHost.h are FZ1: add, never change or remove (every existing virtual, member, default and comment
        stays). HostServices.h stays JUCE-free (no JUCE include; std and funkgui core headers only). G-C owns
        cmake/**, CMakeLists.txt, CMakePresets.json, test/CMakeLists.txt, include/funkgui/web/** and src/web/**:
        never edit them (your tests self-register from their first line and need no CMake edit). Never bless: no
        existing golden row may move.
FUNKGUI base main = 35c5ac9 (your branch starts there).
DONE    FunkGui's CLAUDE.md "Done means" plus the Acceptance below.
READS   /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/plan.md (Sprint B, card G-B; Decisions 4)
        /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/map-editor.md (what the four views do with JUCE
        today, with file:line) and the code itself, which is this card's specification by example:
        /Users/seanfunk/audio/plugins/FCompressor/Source/editor/views/{EditControls,PresetStrip,PresetBrowser,Settings}.cpp
        (showMenu, showCategoryMenu, chooseImport, chooseExport, copyReport, commandOnly/commandKeyName)
        include/funkgui/panel/HostServices.h include/funkgui/panel/HeadlessHost.h include/funkgui/juce/MenuLook.h
        src/gpu/EditorHost.cpp (showParamMenu, ownerComponent, the UI zoom's coordinate rule) FunkGui's CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. HostServices.h, new non-pure virtuals whose defaults refuse (a host written before them keeps compiling
           and serves nothing), in the header's existing style (a dated section, a comment per call saying what
           EditorHost and HeadlessHost do):
             services()            a capability mask (menus, file chooser, clipboard), so a view can disable a cell
                                   its host cannot serve. Default: none.
             showMenu(...)         a flat menu: items of id (> 0), UTF-8 label, enabled, checked, separator; an
                                   anchor rectangle in the Panel's own px; the Theme the menu borrows (the caller
                                   passes it: a product's theme is not always Theme::byIndex); a completion
                                   callback that receives the chosen id, or 0 when dismissed.
             dismissMenus()        closes any menu this host is showing; its callback does not run.
             chooseFiles(...)      open (one or several files) or save (a suggested file name; warn before
                                   overwriting); a title; a file pattern such as "*.fcmppreset"; a callback that
                                   receives the chosen paths as UTF-8 (empty: cancelled).
             copyText(utf8)        to the system clipboard; returns whether it was written.
             commandKeyIsMeta()    whether the platform's command key is Meta (Cmd) rather than Ctrl. Default: the
                                   platform the library was built for (true on Apple).
           Rules every implementation keeps, stated in the header: a callback runs on the message thread, at most
           once, never from inside the call that took it, and never after dismissMenus(), after the Panel is
           detached or after the host is destroyed; a second showMenu or chooseFiles replaces the first (whose
           callback does not run). The callback type is a std::function; the request types are plain structs.
        2. EditorHost serves all three, behaving exactly as the four FCompressor views do today, so that Sprint C can
           swap the views' own JUCE code for these calls with nothing visible changing: juce::PopupMenu with a
           funkgui::MenuLook built from the request's Theme and anchored to the rectangle under the UI zoom
           (owner->localAreaToGlobal of the rectangle scaled by width()/logical width, as the views compute it);
           juce::FileChooser parented on the editor, starting in the user's Documents, with the modes and flags the
           views pass; juce::SystemClipboard. The destructor and detach dismiss menus and drop pending callbacks.
           ownerComponent() stays (HardwareReverb uses it).
        3. HeadlessHost reports every service, logs each call in `log` (counts, and the last request: the items, the
           anchor, the chooser's mode, title, pattern and suggested name, the copied text) and takes scripted
           replies: a pending menu or chooser is answered by a test (choose an id or dismiss; return paths or
           cancel), which is when the callback runs. commandKeyIsMeta() is settable. Nothing else about HeadlessHost
           changes: a Panel that never calls a service draws, ticks and logs exactly as before.
        4. Tests (test/unit/host_services*.cpp, `// FUNKGUI_TEST name=fg.host.services ... gpu=0`): the defaults
           refuse; HeadlessHost's log and scripted replies; every rule of deliverable 1 (at most once, not
           re-entrant, not after dismissMenus, not after detach or destruction, replacement). They are JUCE-free and
           must pass in the agent, nojuce and web presets. EditorHost's side cannot be driven headlessly: add a
           gallery view of its own to the gallery app (a menu with a checked item and a separator, an open chooser,
           a save chooser, a copy cell, and a line that shows what came back), so the lead can try it by hand, and
           say in the handoff exactly what to click. The existing gallery views and their fg.gallery.* rows must not
           move; the new view's rows arrive as candidates.
        ACCEPTANCE (part of DONE)
        cmake --workflow --preset agent-verify && tools/verify.sh "$FWT/build-agent", and the same for agent-gui and
        nojuce: 0 blocking, 0 drift, 0 missing except the new test's own rows (candidates, with a reason).
        cmake --workflow --preset web-verify: the JUCE-free tests, the new one included, pass as wasm32 under node.
        [XV] FCompressor's gate against your worktree, in the lead-made read-only worktree
        /Users/seanfunk/audio/plugins/FCompressor/.claude/worktrees/ro-web-b-gb:
          cd <that worktree> && cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<your worktree> &&
          cmake --build --preset agent && Scripts/verify.sh "<that worktree>/build-agent": 0 blocking, 0 drift.
        Build there, never edit it.
NOTES   The lead writes CHANGELOG.md and tags: put the entry's text and the list of API additions (each new
        declaration, verbatim) in your handoff. Do not design for the browser here: the web host's DOM menu and
        navigator.clipboard are Sprint C. Keep the API small: no submenus, no icons, no shortcuts column; the four
        views need none.
```

## G-C

```text
TASK    G-C   card: WEB-B.2   repo: FunkGui (lead-made worktree)   size: M
        worktree: /Users/seanfunk/audio/libraries/FunkGui.wt/web-b-gc   branch: web-b/g-c
GOAL    FunkGui::web: a WebGL2 sink that draws a recorded frame in a browser, from the same shader source
OWNS    include/funkgui/web/** src/web/** cmake/** CMakeLists.txt CMakePresets.json test/CMakeLists.txt
        test/smoke/shader_web.cpp test/web/** tools/web/**
        (not: the VERSION in CMakeLists.txt, which stays 0.11.1; CHANGELOG.md; test/golden/**; shaders/**)
FROZEN  shaders/*.sc are the one shader source: never edit them. Nothing under include/funkgui/gpu or src/gpu
        changes. With FUNKGUI_WITH_JUCE=ON every existing target, flag, test and golden is exactly as it is now.
        G-B owns HostServices.h, HeadlessHost.{h,cpp}, EditorHost.{h,cpp}, test/unit/host_services*.cpp,
        test/gallery/** and tools/GalleryApp/**: never edit them. Never bless.
FUNKGUI base main = 35c5ac9 (your branch starts there).
DONE    FunkGui's CLAUDE.md "Done means" plus the Acceptance below.
READS   /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/map-render.md (the whole report: the frame as
        one draw call, the vertex layout, the shader transformation that compiled and matched in Chromium, the
        context attributes, context loss) /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/plan.md
        (Sprint B, card G-C) src/gpu/BgfxSink.cpp src/gpu/BgfxContext.cpp (the contract to mirror)
        include/funkgui/canvas/Expand.h shaders/{vs_ui,fs_ui,varying.def}.sc include/funkgui/canvas/SoftRaster.h
        cmake/FunkGuiTargets.cmake (FUNKGUI_WITH_JUCE, the Emscripten branches, how FunkGuiShaders is built)
        test/smoke/shader_hash.cpp FunkGui's CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. The shader text. A CMake script generates GLSL ES 3.00 vertex and fragment sources from shaders/vs_ui.sc
           and fs_ui.sc (the scout's transformation: strip the $input/$output and bgfx include lines; an all-highp
           preamble with `#version 300 es`, the in/out declarations taken from varying.def.sc, vec2_splat,
           texture2DLod -> textureLod, SAMPLER2D; gl_FragColor renamed) and embeds them as a generated header. It
           runs on EVERY host and in every configuration (it needs no shaderc and no bgfx), so one test can pin
           the text everywhere: test/smoke/shader_web.cpp, `// FUNKGUI_TEST name=fg.shader.web ... gpu=0
           links=harness`, with rows for each text's hash and byte count (new rows: they arrive as candidates, the
           lead blesses) and spec rows that the text starts with `#version 300 es` and names no bgfx macro.
        2. FunkGui::web, defined only under Emscripten (FUNKGUI_WEB): include/funkgui/web/WebGlSink.h and
           src/web/WebGlSink.cpp over FunkGui::core. It mirrors BgfxSink's contract: one draw call per frame from
           funkgui::expand (six 64-byte vertices per Prim, attributes at offsets 0, 8, 12, 16, 32, 48), one program,
           u_viewSize = (logical width, logical height, 1 / dpi, seconds), one R8 1024x512 atlas texture from
           FontService's atlas (linear, clamp to edge, no mips), blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA, no depth,
           no culling, the viewport the physical size, cleared to the frame's clear colour with alpha 1.
           It creates its own WebGL2 context on a canvas named by a CSS selector, with alpha: false, antialias:
           false, depth: false, stencil: false, premultipliedAlpha irrelevant; WebGL2 only
           (-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2, Emscripten's GLES3 bindings, no get-proc-address path).
           It reports failure instead of aborting when WebGL2 is missing, and on webglcontextlost /
           webglcontextrestored it rebuilds the program, the buffer and the texture, so the next frame after a
           restore draws. Public API: construction on a selector, ok(), submit(const PrimList&, physical size),
           a frame counter and a lost flag for a host's diagnostics, readPixels for tests.
        3. A browser check the lead can run and you should try to run: test/web/ builds, in the web preset only, a
           page (html, js, wasm) that draws one frame holding a prim of every kind and real text from the embedded
           atlas through WebGlSink, reads the pixels back, rasterises the same PrimList with SoftRaster, and
           reports the largest channel difference and the count over a small tolerance; then forces a context
           loss and restore (WEBGL_lose_context), draws again and compares again. The verdict is machine-readable:
           document.title becomes "PASS" or "FAIL: <why>", and every line also goes to console.log and a <pre>.
           Say in the handoff how to serve it (python3 -m http.server from which directory, which URL).
           Google Chrome is installed (/Applications/Google Chrome.app): try it headless against a local server
           (--headless=new; WebGL2 may need --use-angle=metal or --enable-unsafe-swiftshader) and report what you
           saw. If you cannot get a browser to run, say so plainly: the lead runs the page before merging.
        ACCEPTANCE (part of DONE)
        cmake --workflow --preset agent-verify && tools/verify.sh "$FWT/build-agent", and the same for agent-gui and
        nojuce: 0 blocking, 0 drift, 0 missing except fg.shader.web's own rows (candidates, with a reason).
        cmake --workflow --preset web-verify: the core, FunkGui::web and the page build warning-free under
        Emscripten 6.0.3, and every JUCE-free test, fg.shader.web included, passes as wasm32 under node.
        The generated text is byte-identical on this Mac in the agent, agent-gui, nojuce and web build trees (the
        same hash rows in all four).
        [XV] FCompressor's gate against your worktree, in the lead-made read-only worktree
        /Users/seanfunk/audio/plugins/FCompressor/.claude/worktrees/ro-web-b-gc:
          cd <that worktree> && cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<your worktree> &&
          cmake --build --preset agent && Scripts/verify.sh "<that worktree>/build-agent": 0 blocking, 0 drift.
        Build there, never edit it.
NOTES   No requestAnimationFrame loop, no input, no canvas sizing policy, no DOM menu here: the web host is
        Sprint C. The sink takes a physical size and a dpi from its caller. node has no WebGL, so nothing under
        ctest can draw: the page is the only drawing check, which is why its verdict must be exact about what ran.
        GPU output is not bit-equal to SoftRaster: pick the tolerance from what you measure and report the
        measurement. The lead writes CHANGELOG.md and tags: put the entry's text and the API additions in the
        handoff.
```

## E-1

```text
TASK    E-1   card: WEB-B.3   repo: FCompressor (isolation: worktree)   size: M
        branch: web-b/e-1 (rename the harness-made branch: git branch -m web-b/e-1)
GOAL    The model code the browser demo will share leaves JUCE, with no change in what the plugin does
OWNS    Source/editor/PreviewWorker.cpp Source/editor/PreviewWorker.h Source/editor/PreviewCompute.h
        Source/editor/PreviewCompute.cpp Source/plugin/portable/** Source/plugin/EditHistory.h
        Source/plugin/EditHistory.cpp Source/plugin/factory/FactoryBank.cpp Source/plugin/factory/FactoryBank.h
        Source/plugin/Processor.h Source/plugin/Processor.cpp Source/plugin/Presets.cpp
        Tools/probes/plugin/FakeFacade.h Tools/probes/plugin/FakeFacade.cpp Tools/probes/plugin/history.cpp
        Tools/probes/plugin/presets.cpp Tools/probes/plugin/ui_dump.cpp Source/editor/SubView.h
        Source/editor/views/Settings.cpp Source/editor/gpu/Editor.cpp
FROZEN  FZ0–FZ5. Approved here: RenderInfo (SubView.h) gains one member, `renderer`; FakeFacade.h changes its
        includes and private members only (its public surface, what every ui probe uses, stays). In Processor.h,
        Processor.cpp, Presets.cpp, history.cpp, presets.cpp and ui_dump.cpp: include paths and what the moved
        code forces, nothing else. PreviewWorker.h's public interface stays. No golden row may move; never bless.
        Every path outside OWNS: never edit.
FUNKGUI pin v0.11.1 = 923ec44b8c5fb6f5d46f855555eca815ab31f939. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/plan.md (Sprint B, card E-1) docs/sprints/web/map-editor.md (PreviewWorker, FactoryBank,
        FakeFacade, RenderInfo: what uses JUCE and why) Source/editor/PreviewWorker.{h,cpp}
        Source/plugin/factory/FactoryBank.{h,cpp} Source/plugin/EditHistory.{h,cpp}
        Tools/probes/plugin/FakeFacade.{h,cpp} cmake/FcmpSources.cmake (the plugin globs) cmake/LintDeps.cmake
        (rule plugin.portable) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. PreviewWorker.cpp without JUCE: std::thread, a condition variable and an atomic stop flag in place of
           juce::Thread, with the same contract (the synchronous path creates no thread; requests coalesce; results
           are double-buffered; stop() joins within one run; stop-before-teardown). The pure computation moves to
           Source/editor/PreviewCompute.{h,cpp} (JUCE-free, no thread type in its interface: it takes a
           cancellation flag it polls where the old code polled threadShouldExit), so a later host can run it
           somewhere else. skeleton.preview.{sync,async} and skeleton.teardown.worker must keep passing unchanged.
        2. Source/plugin/portable/FactoryData.{h,cpp}, JUCE-free: the factory bank's entry table (the same
           kEntries, the same FactoryIncludes.h expansion, the bank revision) and the fcdsp-only default filling,
           exposed as plain rows (uuid, name, category, notes, the Mode's key and revision, the 22 plain values).
           FactoryBank.cpp keeps only the conversion to funkgui::presets::Preset and whatever needs FunkPresets;
           the bank it produces is byte-identical (proc.presets and the factory revision are the proof).
        3. Source/plugin/EditHistory.{h,cpp} move to Source/plugin/portable/ with `git mv` (history kept), the
           include paths that name them follow, and nothing in them changes but paths.
        4. FakeFacade.{h,cpp} read FactoryData and include no JUCE and no funkgui/presets header, directly or
           through anything they include; the rows FakePresets lists and what it applies are unchanged (every ui.*
           golden is the proof). If a probe needs the Preset form, it gets it from FactoryBank, not from FakeFacade.
        5. RenderInfo::renderer: the name of the API the frame is drawn with, upper case ("METAL" on macOS,
           "VULKAN" on Linux: FunkGui fixes the renderer per platform), set by Source/editor/gpu/Editor.cpp;
           Settings.cpp prints it where it prints the hard-coded "METAL" today, and prints what it prints today
           when there is no GPU editor. On macOS nothing visible changes; on Linux the row stops saying METAL.
        ACCEPTANCE (part of DONE)
        [AGENT] cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0 blocking, 0 drift,
                0 missing. lint.deps passes with plugin.portable in force (run
                `cmake -DFCMP_SOURCE_DIR="$WT" -P cmake/LintDeps.cmake` too and quote its last line).
        [GPU]   cmake --workflow --preset agent-gui-verify: builds warning-free and passes.
        [DSP]   cmake --workflow --preset dsp-verify: unaffected, still green.
        Proofs in the handoff: `grep -n juce` over PreviewWorker.cpp, PreviewCompute.{h,cpp},
        Source/plugin/portable/** and FakeFacade.{h,cpp} returns nothing but comments; proc.presets' rows and the
        factory revision before and after; the names of the tests that cover the thread swap, with their results.
NOTES   The UI probes run the Panel with the synchronous preview, so the thread path is covered only by the
        skeleton tests and proc.* probes that name it: say which, and whether you ran the async path under
        tsan-agent (lead-only preset: do not; say it is untested there). std::thread has no name: set one where
        the platform has a call for it, or leave it and say so. Emscripten is not part of this card.
```
