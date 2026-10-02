# Web Sprint C — the views leave JUCE; the web facade proven natively; FunkGui's web host (v0.13.0): task manifests

Status: **sprint base**, written by the lead from the judged plan (`docs/sprints/web/plan.md`, "Sprint C") and four
scout reports made on the Sprint B result (`docs/sprints/web/c-views.md`, `c-facade.md`, `c-webhost.md`,
`c-build.md`). Manifest format as in `docs/sprints/web-b.md` (read by
`Scripts/sprint/ownership.py docs/sprints/web-c.md#<ID> <worktree>`). The user authorised this sprint ("continue to the
next sprint") and raised the agent limit to six; Sprint D needs a new authorisation.

Goal. After this sprint nothing under `Source/editor` outside `gpu/` names JUCE, a `WebFacade` stands in for the
processor and is proven bit-equal to it in native probes, and FunkGui has a browser host. The editor is still not
built for the browser: that is Sprint D. **The plugin does what it did:** every existing golden row holds, with two
spec rows of `ui.settings` replaced by name (E-2's manifest says which and why).

**Base.** FCompressor: branch `web/sprint-c` at the commit that adds this file (on `main` after Sprint B): FZ0–FZ5
frozen; FunkGui pin v0.12.0 = `f1d3b3c74c6c0bebe62d3ca9c7b134656c334c31`. FunkGui: branch `web/sprint-c` = v0.12.0
plus the lead's seam (`src/web/WebServices.h`, frozen, and a refusing stub `WebServices.cpp`).

**The lead has already made** (FCompressor; read them first):
- `cmake/FcmpSources.cmake`: the globs `Source/web/facade/*.cpp` (flat) and `Source/plugin/portable/*.cpp` as lists.
- `cmake/FcmpWeb.cmake`: natively, `fcmp_probe_plugin` links the engine archive (`fcmp_web_engine_lib`) and compiles
  `Source/web/facade/*.cpp`; in the web configuration, two compile-only wasm32 libraries: `fcmp_web_facade_check`
  (portable + facade; part of the `web` build) and `fcmp_web_editor_check` (the editor outside `gpu/` + portable +
  facade over FunkGui's JUCE-free core; not in `all` yet: it is red on exactly the four views until E-2 lands).
- `cmake/FcmpDeps.cmake`: the web configuration fetches FunkGui (JUCE-free core and `FunkGui::web`).
- `cmake/LintDeps.cmake`: rule `web.facade` (active) and rule `editor.juce` (present, switched on by
  `-DFCMP_LINT_EDITOR_JUCE=ON`; the lead makes it permanent when E-2 merges).

**Read before writing anything:** your card's scout report in `docs/sprints/web/` (it is your map: facts with
file:line, the change list, the traps, the rows worth adding), `docs/sprints/web/plan.md` ("Sprint C"), and ADR-93 in
`docs/DECISIONS.md`. Where a scout's "open question" is answered in your manifest, the manifest's answer binds.

**Six agents share the machine** (five cards build): builds at `-j6`, at most two build directories per agent, delete
a build directory as soon as you no longer need it (about 20 GB are free). Wall-clock test rows read
`FCMP_TIMING_SCALE`: run gates with `FCMP_TIMING_SCALE=3` in the environment, and rerun a timing row alone before
concluding anything from it. **Emscripten** 6.0.3 is installed; its system-library cache is shared: on a cache lock or
a half-written library, wait and retry, never clear the cache.

**Frozen set.** FZ0–FZ5 as in `docs/sprints/s13.md`. This sprint's approved revisions are listed per card. FunkGui's
public headers are API: additive changes only (for the MINOR tag v0.13.0, which the lead makes).

**Lead-owned, as always:** FCompressor's `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `docs/**`,
`tests/golden/**`, `.github/**`, `README.md`, `CLAUDE.md`; FunkGui's `CHANGELOG.md`, `test/golden/**`, `SEED.tsv`,
`docs/PROVENANCE.md`, the VERSION in its `CMakeLists.txt` (0.12.0 until the lead tags) and `src/web/WebServices.h`.
If a lead-owned file has a bug that blocks you, make the minimal edit, keep its meaning, and list it under "ownership
deviations needing approval" in the handoff.

**New rows are spec rows.** No card adds a golden row to FCompressor: CI runs `--strict` and Linux has no overlay, so
a new golden would need blessing on two platforms. Mismatch counts and equalities (`P.eq`, `P.near`, `P.ge`) need none.

---

## E-2

```text
TASK    E-2   card: WEB-C.1   repo: FCompressor (isolation: worktree)   size: L
        branch: web-c/e-2 (rename the harness-made branch: git branch -m web-c/e-2)
GOAL    The four views ask their host for menus, the file chooser and the clipboard; no JUCE under Source/editor
        outside gpu/
OWNS    Source/editor/Panel.cpp Source/editor/Panel.h Source/editor/views/EditControls.h
        Source/editor/views/EditControls.cpp Source/editor/views/PresetStrip.h Source/editor/views/PresetStrip.cpp
        Source/editor/views/PresetBrowser.h Source/editor/views/PresetBrowser.cpp Source/editor/views/Settings.h
        Source/editor/views/Settings.cpp Tools/probes/plugin/ui_edits.cpp Tools/probes/plugin/ui_presets.cpp
        Tools/probes/plugin/ui_settings.cpp
FROZEN  FZ0–FZ5. Approved here (FZ4 view headers): the removal of the JUCE forward declarations and of the members
        menuLook_ and chooser_; EditControls::commandOnly and commandKeyName take the host's answer as a bool
        (commandOnly(const funkgui::Mods&, bool commandKeyIsMeta), commandKeyName(bool commandKeyIsMeta)); nothing
        else in a public declaration changes. Every other ui_*.cpp probe is E-3's, ui_web.cpp is W-F's: never edit
        them. Every path outside OWNS: never edit. No golden row may move; never bless.
FUNKGUI pin v0.12.0 = f1d3b3c74c6c0bebe62d3ca9c7b134656c334c31. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/c-views.md (the whole report: sites, the replacing calls, traps, the new rows)
        docs/sprints/web/plan.md (Sprint C, card E-2) the FunkGui headers of the pin, in your build tree's
        _deps/funkgui-src: include/funkgui/panel/HostServices.h (the services and their rules) and HeadlessHost.h
        (pendingMenu, chooseMenuItem, cancelMenu, pendingFiles, returnFiles, cancelFiles, log.lastCopy,
        setCommandKeyIsMeta) cmake/LintDeps.cmake (rule editor.juce) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Panel's HostProxy forwards the six calls (services, showMenu, dismissMenus, chooseFiles, copyText,
           commandKeyIsMeta) FIRST, before any view reads them (without services() the IMPORT and EXPORT cells
           would disable and goldens would move); when everything below is done its ownerComponent() override goes.
        2. EditControls, PresetStrip and PresetBrowser show their menus through HostServices::showMenu: the same
           items, ids, ticks and separators, the anchor in the Panel's own px (no zoom factor: the host scales),
           the theme productTheme(themeIndex()), the same callback bodies behind the same weak `alive` guard.
           PresetBrowser's import and export go through chooseFiles (openMany; save with the suggested file name);
           the view's own extension and legal-name code goes (the host does both). Settings copies through
           copyText, and shows COPIED only when it returned true. No view destructor calls a service (the host has
           dropped the callbacks; it may be gone): they keep `alive_.reset()` only.
        3. IMPORT and EXPORT (the action cells and the menu items "Import..." and "Export...") are enabled only when
           the host reports hostservice::fileChooser. Menus and the clipboard are not gated.
        4. The command key comes from the host: commandOnly and commandKeyName as approved above, the footer and
           Panel's undo chord pass host->commandKeyIsMeta(). No `#if JUCE_MAC` (it would turn silently false).
        5. PresetBrowser's list clip is pushed with its y edges on device pixels:
           top = c.snapY(kList.y), height = c.snapY(kList.bottom()) - top (ADR-93: WebGL fills a tie row
           differently; where the dpi is a multiple of 0.25, i.e. every macOS window and every integer Linux
           scale, the snap changes nothing, which the goldens prove; the review noted that at a fractional
           Linux desktop scale an edge can move by one device row, which the lead accepts).
        6. The four views and their headers include and name nothing of JUCE's and no funkgui/juce/MenuLook.h.
           `cmake -DFCMP_SOURCE_DIR="$WT" -DFCMP_LINT_EDITOR_JUCE=ON -P cmake/LintDeps.cmake` reports 0 violations.
        7. Probes ui_edits.cpp, ui_presets.cpp, ui_settings.cpp: funkgui::HeadlessGuiScope in place of JUCE's GUI
           initialiser and include; `#if JUCE_MAC` becomes the host's commandKeyIsMeta(); the rows the scout lists
           under "New rows" (the menu requests, choices, cancels and stale cases; the chooser requests, results
           and cancels; the clipboard; both command-key answers on one platform through setCommandKeyIsMeta; a host
           without the chooser and one without the clipboard, made by a probe-local HostServices wrapper that masks
           services() and refuses). APPROVED REPLACEMENT: ui.settings' rows copy.headless_copies_nothing and
           copy.title_kept assert that a headless host copies nothing, which is no longer true; they are replaced
           by copy.report_copied, copy.title_copied, copy.title_back and copy.no_clipboard. Every other existing
           row of the three probes keeps its key and its value.
        ACCEPTANCE (part of DONE)
        [AGENT] cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0 blocking, 0 drift,
                0 missing.
        [GPU]   cmake --workflow --preset agent-gui-verify: builds warning-free and passes.
        [LINT]  the command of deliverable 6: 0 violations (quote its last line).
        [WEB]   cmake --preset web && cmake --build "$WT/build-web" --target fcmp_web_editor_check: the editor outside
                gpu/ compiles as wasm32 under -Werror (with E-3's and W-F's files absent this is the whole proof
                that the views are JUCE-free; wasm32 has a 32-bit size_t, so new warnings can appear only here).
        Proofs in the handoff: for one menu, one chooser and the clipboard, the row that fails when the request is
        built wrongly (say how you checked it can fail: a temporary wrong anchor, theme or id).
NOTES   PresetStrip::MenuItem and PresetBrowser::MenuItem are public nested types the probes use: inside members
        write funkgui::MenuItem and funkgui::MenuRequest. A MenuItem's id must be > 0 and MenuRequest::theme
        defaults to graphite: set it. The lead tries the real menus, choosers and clipboard by hand in the
        Standalone afterwards (position and colours at each zoom and theme): say what to look at.
```

## E-3

```text
TASK    E-3   card: WEB-C.2   repo: FCompressor (isolation: worktree)   size: S
        branch: web-c/e-3 (rename the harness-made branch: git branch -m web-c/e-3)
GOAL    The UI probes hold no JUCE of their own, so Sprint D can run them under node
OWNS    Tools/probes/plugin/ui_*.cpp
        (not: ui_edits.cpp ui_presets.cpp ui_settings.cpp, which are E-2's, and ui_web.cpp, which is W-F's)
FROZEN  FZ0–FZ5. No row of any probe may change its key or its value, and no golden row may move: this card changes
        how the probes are set up, never what they measure. Every path outside OWNS: never edit.
FUNKGUI pin v0.12.0 = f1d3b3c74c6c0bebe62d3ca9c7b134656c334c31. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 40-line handoff) plus the Acceptance below.
READS   docs/sprints/web/c-views.md (section H "Probes" and the traps about them) docs/sprints/web/c-build.md
        (facts 18 to 21: what compiles for wasm32 today and what does not) the pin's
        include/funkgui/panel/HeadlessGuiScope.h Source/plugin/portable/FactoryData.h CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. In every ui_*.cpp you own: <funkgui/panel/HeadlessGuiScope.h> and `const funkgui::HeadlessGuiScope gui;`
           in place of the JUCE include and juce::ScopedJuceInitialiser_GUI. juce::Thread::sleep becomes
           std::this_thread::sleep_for (ui_chars.cpp, ui_geometry.cpp). ui_truth.cpp's 64-to-32-bit index warning
           (wasm32's size_t is 32 bits) is fixed without changing a value.
        2. ui_dump.cpp applies a preset from Source/plugin/portable/FactoryData rows instead of
           plugin/factory/FactoryBank.h and funkgui::presets::Preset, so it holds no JUCE through FunkPresets: the
           frames it dumps for a preset are unchanged (compare its output for three presets before and after).
        3. After the card, `grep -n "juce\|JUCE_"` over your files matches comments only, and each file passes
           `em++ -std=c++20 -fsyntax-only` with the project's warning list and -Werror (cmake/FcmpArch.cmake:
           FCMP_WARNING_FLAGS) and the include paths of a web build tree (cmake --preset web gives you
           build-web/_deps/funkgui-src; add Source, Tools/probes/common, the generated directory): the list of
           files and the exit code go in the handoff. Linking and running them under node is Sprint D.
        ACCEPTANCE (part of DONE)
        [AGENT] cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0 blocking, 0 drift,
                0 missing; every ui.* probe reports the same number of rows as at the base (give both counts).
NOTES   Probes self-register from their first line: do not touch those lines. gui-live and the GPU build do not
        use these files. Keep each diff minimal: reviewers will read every hunk as "setup only".
```

## W-F

```text
TASK    W-F   card: WEB-C.3   repo: FCompressor (isolation: worktree)   size: L
        branch: web-c/w-f (rename the harness-made branch: git branch -m web-c/w-f)
GOAL    The web facade: a ProcessorFacade over the engine module's byte protocol, proven bit-equal to the Processor
OWNS    Source/web/facade/*.h Source/web/facade/*.cpp Tools/probes/plugin/webnull.cpp
        Tools/probes/plugin/webpresets.cpp Tools/probes/plugin/ui_web.cpp Tools/probes/plugin/LoopbackLink.h
        Tools/probes/plugin/LoopbackLink.cpp
FROZEN  FZ0–FZ5, and in practice everything the facade stands on: Source/plugin/ProcessorFacade.h,
        Source/plugin/portable/**, Source/web/engine/** (the protocol and the engine are Sprint A's: a need there,
        such as load figures or an acknowledgement of a refused record, is an interface-change request, not an
        edit), Source/fcdsp/**, Tools/probes/plugin/FakeFacade.*, EngineFacade.*. Source/editor/** is E-2's.
        Every path outside OWNS: never edit. Never bless.
FUNKGUI pin v0.12.0 = f1d3b3c74c6c0bebe62d3ca9c7b134656c334c31. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/c-facade.md (the whole report: what Processor does for every member of ProcessorFacade,
        the batch and snap scheme, how a write becomes a plain value under JUCE, the telemetry mirror, presets,
        the proposed interface, the probe programme, the traps) docs/sprints/web/c-build.md (sections 1.3 and 2:
        the probe grammar, what the lead's CMake already does for you) docs/sprints/web/plan.md (Sprint C, card
        W-F; Decisions 6) Source/web/engine/{WebProtocol.h,WebEngine.h} Source/plugin/ProcessorFacade.h
        Source/plugin/Processor.cpp Source/plugin/Presets.cpp Source/plugin/portable/{EditHistory,FactoryData}.h
        Tools/probes/plugin/{null,history,ui_edits}.cpp cmake/LintDeps.cmake (rule web.facade) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Source/web/facade/EngineLink.h: the link a facade posts WebProtocol records through and receives replies
           from (the scout's interface: post(span of bytes), setSink; replies arrive on the facade's thread, in
           order, at most one per Pull, possibly inside post(), possibly later, possibly never; the bytes are valid
           only during the call). JavaScript will only move bytes: nothing here knows a MessagePort.
        2. Source/web/facade/WebFacade.{h,cpp}: `final : public ProcessorFacade`, over an EngineLink&.
           - Values: THE LEAD'S DECISION (scout Q1): mirror JUCE's two-value model per parameter (the parameter
             value and the raw value, with the APVTS adapter's approximatelyEqual rule and the preset write's skip
             rule), so that the raw values are bit-equal to the Processor's in every case, including a one-ulp drag
             step and Init after an undo. Say in a comment that this restates JUCE on purpose, and where.
           - Ports as the Processor's HistoryPort (gestures reported to EditHistory); currentRaw() as the
             Processor's (resolveSlot; the budget from labudget).
           - Batches: nothing is posted while a batch is open; the outermost end posts exactly one Params record
             with all 30 values and snap = whether any bracket of the nest asked for it, even when no value
             changed; outside a batch every write that changes a raw value posts one record with snap 0 (scout
             Q6). EditHistory's own batches end without the snap, as the Processor's do.
           - Telemetry: setUiAttached is a count, Attach is posted on 0 <-> 1 only; pull() is an explicit call for
             the frame loop (scout Q2), at most one outstanding; a reply is parsed in place with memcpy (never cast,
             never on the stack: a full Reply is 16 KB), checked (magic, version, kind, sizes) and copied before
             anything else is posted; a mirror HistoryRing at a stable address receives the columns in order, with
             one marker column where the reply flags a gap; readUiFrame returns the last frame as the Processor
             does (true with zeros before any reply).
           - resync() for a link that has just connected (Params with snap, Attach if attached) and resetEngine().
           - uiState, stateNotice (none), diagnostics (what the web can fill; the four load figures stay 0: scout
             Q4), edits() an EditHistory over a host of its own.
        3. Source/web/facade/WebPresets.{h,cpp}: `final : public PresetAccess`: the factory rows of FactoryData in
           bank order, then the session's user rows by folded name; apply inside one batch with the Mode first and
           the identity set before the batch ends; modified(), step() with its wrap, currentUuid, restoreCurrent,
           saveAs with unique names, rename, remove, overwrite, revision(), all as the Processor's PresetAccess
           behaves (the scout's facts 37 to 44); importFile and exportFile keep the refusing defaults.
        4. Tools/probes/plugin/LoopbackLink.{h,cpp} (THE LEAD'S DECISION, scout Q3: a probe helper, not facade code,
           so that Source/web/facade is exactly what ships and never includes the engine): an EngineLink over an
           FcmpWebEngine in the same process; replies are delivered inside post(); the probe drives
           fcmp_web_configure and fcmp_web_process as the worklet would.
        5. Probes, spec rows only (scout section 4 gives the programme and the rows):
           webnull.cpp     `// FCMP_PROBE layer=proc name=webnull scope=mode timeout=120`: per Mode, a Processor
                           (128-frame blocks) against a WebFacade over a LoopbackLink (gate off, 48 kHz, 128-frame
                           quanta), the same never-silent input, through a script of a gesture, a batch with audio
                           while it is open, an empty batch during a ramp, a preset of another Mode, step, undo and
                           redo, A/B, OUTPUT and BYPASS, quality and lookahead changes (the Processor's
                           SetupWatcher polled after each), a reset, and the two corner cases the two-value model
                           exists for. Rows per step: output mismatches 0 (both channels, bit for bit), raw-value
                           mismatches 0 (all 30), UiFrame mismatches 0, the records posted and their snap flags;
                           once: column mismatches 0, latency equal, nothing refused, and a row proving the steps
                           are not vacuous (consecutive outputs differ).
           webpresets.cpp  `// FCMP_PROBE layer=proc name=webpresets scope=global timeout=120`: WebPresets against
                           a Processor's presets() for every row and operation of deliverable 3.
           ui_web.cpp      `// FCMP_PROBE layer=ui name=web scope=global timeout=300`: a Panel over WebFacade and
                           a LoopbackLink through funkgui::HeadlessHost: a preset click, undo and redo by click
                           and by chord, A/B, a typed value, with the records each posts; no JUCE in the file
                           (funkgui::HeadlessGuiScope), no Processor, so Sprint D runs it under node unchanged.
                           The command key's name and chord are read from the host's commandKeyIsMeta(), never
                           from a platform macro and never set by the probe (E-2 changes how the views read it).
        ACCEPTANCE (part of DONE)
        [AGENT] cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0 blocking, 0 drift,
                0 missing; proc.webnull passes for all 14 Modes.
        [LINT]  cmake -DFCMP_SOURCE_DIR="$WT" -P cmake/LintDeps.cmake: 0 violations, web.facade in force (no
                Emscripten header, no EngineHost token, nothing from editor/, from plugin/ only portable/* and
                ProcessorFacade.h, from funkgui only params/ParamPort.h, from web/engine only WebProtocol.h).
        [WEB]   cmake --workflow --preset web-verify: fcmp_web_facade_check compiles the facade as wasm32 under
                -Werror, and Sprint A's six web tests still pass.
        Proofs in the handoff: for the batch rule, the snap rule and the two-value model, the proc.webnull row that
        fails when the rule is broken (say how you checked: a temporary change, then restored).
NOTES   The silence gate is on by default in the engine module and the Processor has none: the probes turn it off.
        The reply buffer is valid until the next post. A facade's attach is a count, the engine's a boolean. The
        mirror ring's written() is its own count: never compare it with UiFrame::historyWritten after a gap. The
        project's -Wconversion, -Wshadow-all, -Wswitch-enum and -Wcast-align are errors, and wasm32's size_t is 32
        bits. The glob is flat: no sub-directory under Source/web/facade.
```

## G-D

```text
TASK    G-D   card: WEB-C.4   repo: FunkGui (lead-made worktree)   size: L
        worktree: /Users/seanfunk/audio/libraries/FunkGui.wt/web-c-gd   branch: web-c/g-d
GOAL    WebHost: the browser's counterpart of EditorHost (the frame clock, sizing and zoom, input), and the gallery
        as a web page
OWNS    include/funkgui/web/WebHost.h include/funkgui/web/WebInput.h include/funkgui/web/WebClock.h
        src/web/WebHost.cpp test/unit/web_input.cpp test/unit/web_clock.cpp test/web/host.cpp test/web/host.html
        test/web/CMakeLists.txt tools/web/check-page.mjs tools/GalleryWeb/** cmake/** CMakeLists.txt
        CMakePresets.json test/CMakeLists.txt
        (not: the VERSION in CMakeLists.txt; CHANGELOG.md; test/golden/**; shaders/**)
FROZEN  src/web/WebServices.h is the lead's seam with card G-E: call it as declared, never edit it; G-E owns
        src/web/WebServices.cpp, src/web/WebPrefs.cpp, include/funkgui/web/WebPrefs.h, test/web/services.* and
        test/unit/web_prefs.cpp: never edit them (the base's WebServices.cpp is a stub that refuses, so your menu
        and clipboard calls return false until the lead merges both cards). WebGlSink.{h,cpp} and everything under
        include/funkgui/gpu, src/gpu, include/funkgui/panel and src/panel stay as they are. With
        FUNKGUI_WITH_JUCE=ON every existing target, flag, test and golden is exactly as now. Never bless.
FUNKGUI base = branch web/sprint-c (v0.12.0 f1d3b3c plus the lead's seam commit; your branch starts there).
DONE    FunkGui's CLAUDE.md "Done means" plus the Acceptance below.
READS   /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/c-webhost.md (the whole report: EditorHost's
        frame order, cadence, sizing and input as the specification; what Emscripten 6.0.3 offers and what was
        measured in Chrome; the proposed API; the traps) /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/
        web/plan.md (Sprint C, card G-D) src/gpu/EditorHost.cpp src/gpu/FramePump.cpp src/panel/HeadlessHost.cpp
        include/funkgui/web/WebGlSink.h src/web/WebServices.h test/web/ tools/web/check-page.mjs
        tools/GalleryApp/ test/gallery/ FunkGui's CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. include/funkgui/web/WebInput.h and WebClock.h: the pure parts, header-only plain C++ with no Emscripten
           include, so they compile natively and their tests run in every preset. WebInput: DOM modifiers to Mods
           (off Apple platforms Ctrl sets cmd AND ctrl, as JUCE does: FCompressor's undo chord depends on it);
           the popup rule (right button; Ctrl-click on a Mac); client coordinates to the Panel's logical px; the
           key table of EditorHost (space's character, lower-casing under cmd, dead and modifier keys refused);
           the wheel (DOM sign and units to FunkGui's: pixel mode smooth, line and page mode notched; no reversed
           or inertial flag exists in Chrome); a click counter with JUCE's rule; Cursor to a CSS cursor name.
           WebClock: the dt rule (first 1/60, clamped to 1..100 ms), fps over one-second windows, full rate capped
           near 60 Hz on faster displays, 12 Hz idle, a nudge renders at the next vsync, nothing while hidden; the
           zoom helpers (clean steps, nearest step, zoomed size, the fit) as EditorHost computes them.
        2. include/funkgui/web/WebHost.h (plain C++) and src/web/WebHost.cpp: a HostServices over a Panel& (the
           Panel outlives the host: HeadlessHost's rule) and one WebGlSink constructed once and never recreated.
           THE LEAD'S DECISIONS on the scout's questions: own DOM listeners installed from EM_JS under one
           AbortController (pointer events with capture and fractional coordinates; html5.h's canvas callbacks die
           with a sink and have neither), events delivered to the Panel synchronously inside the DOM handler in
           JUCE's order (up, then doubleClick), preventDefault on wheel and keydown only when the Panel consumed
           the event and always on contextmenu; no pointer lock (setUnboundedDrag is a flag); the middle button
           ignored; text from keydown's key only (no IME); no file drops, no accessibility mirror; the zoom fitted
           to the window's inner size minus the config's margins and refitted on resize; the canvas's CSS size is
           the zoomed logical size, its drawing buffer that times the device pixel ratio (the ResizeObserver's
           device-pixel box where the browser has one), info.dpi = physical height / logical height.
           Frame order as EditorHost::submitFrame (preferences and zoom, hidden gate, tick, record, submit,
           wantsFullRate), a 10 Hz Panel::idle, single-shot requestAnimationFrame requests that can be cancelled
           (never emscripten_set_main_loop). The capture pins (fixed dt, theme, zoom, scale) come from the config
           as a value, never from the environment. HostServices: themeIndex and the zoom calls as EditorHost;
           services() = menus | clipboard, forwarded to WebServices with the Panel's logical width; chooseFiles
           refuses; commandKeyIsMeta() from the browser's platform; nowSeconds from the performance clock (the
           fixed clock under a pin). On visibilitychange: hidden closes the Panel's gestures, visible reloads the
           preferences and nudges. The destructor stops the clock, removes the listeners, lets the services go,
           closes gestures, detaches, and only then destroys the sink.
        3. tools/GalleryWeb/: the gallery as a page (the same test/gallery sections over WebHost; ?section=,
           ?theme=, ?zoom=, ?dt=, ?scale= fill the config; one link per section, a reload each, so a host is never
           rebuilt), built by the web preset. It is what the lead opens in Chrome and Safari.
        4. Tests. test/unit/web_input.cpp and web_clock.cpp (`// FUNKGUI_TEST name=fg.web.input ... gpu=0`,
           `fg.web.clock`): spec rows for every rule of deliverable 1 (the scout lists them), passing in the agent,
           agent-gui, nojuce and web presets. test/web/host.cpp and host.html, CTest fg.web.host (labels fg;live;
           tools/web/check-page.mjs gains --page <stem>): driven by host.frame() under a pinned clock, per gallery
           case the frame WebHost recorded against a HeadlessHost's for the same script (fingerprint and static
           primitives equal, dpi exact), readPixels against SoftRaster within the sink page's bounds; synthetic DOM
           events at zoom 150 arriving at a recording Panel at exact logical coordinates, with the modifiers, the
           click count and the JUCE order; the CSS cursor; a zoom change resizing the canvas and its buffer; a
           forced context loss and recovery through the host. The verdict is machine-readable as the sink page's.
        ACCEPTANCE (part of DONE)
        cmake --workflow --preset agent-verify && tools/verify.sh "$FWT/build-agent", and the same for agent-gui and
        nojuce: 0 blocking, 0 drift, 0 missing (your tests are spec rows).
        cmake --workflow --preset web-verify && tools/verify.sh "$FWT/build-web": the core, FunkGui::web, both
        pages and the gallery page build warning-free; every JUCE-free test passes as wasm32 under node.
        The pages in headless Chrome: tools/web/check-page.mjs on the sink's page (still PASS) and on the host's
        page (PASS), on ANGLE Metal; say what SwiftShader gives.
        [XV] FCompressor's gate against your worktree, in the lead-made read-only worktree
        /Users/seanfunk/audio/plugins/FCompressor/.claude/worktrees/ro-web-c-gd:
          cd <that worktree> && cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<your worktree> &&
          cmake --build --preset agent && Scripts/verify.sh "<that worktree>/build-agent": 0 blocking, 0 drift.
        Build there, never edit it.
NOTES   EM_JS bodies pass through the C preprocessor: an apostrophe in a JavaScript comment breaks the build. A
        synthetic pointer event cannot be captured (setPointerCapture throws): wrap it. A canvas gets keys only
        with a tabindex and the focus. The host must override commandKeyIsMeta(): the default is the compile
        platform, which is never Apple in a wasm build. Natural scrolling: Chrome has no direction flag, so a
        value control turns the other way than in the native plugin on a Mac; say so in the handoff, do not guess.
        The lead writes CHANGELOG.md and tags: put the entry's text and the API additions in the handoff.
```

## G-E

```text
TASK    G-E   card: WEB-C.5   repo: FunkGui (lead-made worktree)   size: M
        worktree: /Users/seanfunk/audio/libraries/FunkGui.wt/web-c-ge   branch: web-c/g-e
GOAL    The browser's services for a web host: a popup menu in the DOM, the clipboard, preferences in localStorage
OWNS    src/web/WebServices.cpp src/web/WebPrefs.cpp include/funkgui/web/WebPrefs.h test/web/services.cpp
        test/web/services.html test/web/services.cmake test/unit/web_prefs.cpp
FROZEN  src/web/WebServices.h is the lead's seam with card G-D: implement exactly what it declares, never edit it
        (what you need behind it goes in `Impl`). G-D owns WebHost.*, WebInput.h, WebClock.h, test/web/host.*,
        test/web/CMakeLists.txt, tools/web/check-page.mjs, tools/GalleryWeb/**, cmake/**, CMakeLists.txt,
        CMakePresets.json and test/CMakeLists.txt: never edit them (see NOTES for how your page is built).
        include/funkgui/panel/HostServices.h and include/funkgui/prefs/UiPreferences.h stay as they are. With
        FUNKGUI_WITH_JUCE=ON every existing target, flag, test and golden is exactly as now. Never bless.
FUNKGUI base = branch web/sprint-c (v0.12.0 f1d3b3c plus the lead's seam commit; your branch starts there).
DONE    FunkGui's CLAUDE.md "Done means" plus the Acceptance below.
READS   /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/c-webhost.md (facts (e) and "Emscripten", step 5
        of the change list, the traps about the clipboard, EM_JS and the preferences backend)
        src/web/WebServices.h include/funkgui/panel/HostServices.h (the rules every host keeps)
        src/gpu/HostServicesJuce.cpp (the same rules over JUCE: the pending request and its serial are the model)
        src/juce/MenuLook.cpp (what a menu looks like: ground, ink100, ink16 highlight, the bundled face at 14 px)
        include/funkgui/prefs/UiPreferences.h and src/prefs (the Backend interface) src/web/WebGlSink.cpp (how
        this module uses EM_JS) test/web/ tools/web/check-page.mjs FunkGui's CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. src/web/WebServices.cpp: the menu as DOM elements (role=menu, one element per item; a separator; a tick;
           a disabled item shown and not choosable), in the request's Theme and the bundled face at 14 CSS px,
           placed beside the anchor (the request's rectangle times the canvas's CSS width over the logical width,
           from the canvas's box) and kept inside the window; chosen by click or by Up/Down and Return, dismissed
           by a press outside it, Escape, the window's blur or resize; the keyboard focus returns to the canvas.
           Every rule of HostServices holds: at most once, never inside showMenu, never after dismissMenus, letGo
           or destruction, a second menu replaces the first even when refused, the three refusals. copyText:
           navigator.clipboard.writeText with a fallback where it is missing; true means the write was issued.
           Nothing is left in the document after a menu closes or the object is destroyed.
        2. include/funkgui/web/WebPrefs.h and src/web/WebPrefs.cpp: installLocalStoragePrefs(keyPrefix), a
           UiPreferences::Backend over localStorage with an in-memory mirror (read from the mirror; write to the
           mirror and to storage inside try/catch: a private window may refuse; reload re-reads the prefixed keys
           and reports whether anything changed). A product calls it before it constructs its Panel. The backend's
           pure part (the mirror, the key prefix, change detection) is written so that test/unit/web_prefs.cpp
           (`// FUNKGUI_TEST name=fg.web.prefs ... gpu=0`) can test it natively and under node with a fake storage.
        3. test/web/services.cpp and services.html, CTest fg.web.services (labels fg;live), a page that needs no
           WebHost: a canvas, a WebServices, and rows that the menu's elements exist with the request's items,
           texts, tick, separator and disabled state and computed colours from the Theme; that it sits beside the
           anchor at scale 1 and 1.5; that a click on an item runs the callback once with its id, later and not
           inside showMenu; Escape and an outside press give 0; dismissMenus, letGo and destruction give nothing;
           a second menu replaces the first; the three refusals; the document is clean afterwards; copyText
           returns true and (where the page may read the clipboard back) the text round-trips; the localStorage
           backend round-trips, survives a refused write and reports a reload's change. Machine-readable verdict as
           the sink's page (document.title PASS or FAIL: why; every line to console.log and a <pre>).
        ACCEPTANCE (part of DONE)
        cmake --workflow --preset agent-verify && tools/verify.sh "$FWT/build-agent" and the same for nojuce: 0
        blocking, 0 drift, 0 missing (fg.web.prefs is spec rows).
        cmake --workflow --preset web-verify && tools/verify.sh "$FWT/build-web": everything builds warning-free
        under Emscripten 6.0.3 and the JUCE-free tests pass under node.
        Your page in headless Chrome: PASS (run it with a local copy of the runner if tools/web/check-page.mjs
        cannot take another page name yet: G-D adds --page; say what you ran).
        [XV] is not needed: nothing a native consumer builds changes (say so with `git diff --stat` in the handoff).
NOTES   test/web/CMakeLists.txt is G-D's. Put your page's target in test/web/services.cmake and say in the handoff
        the one line the lead adds to include it; until then build the page yourself in the web tree with an
        explicit em++ command or a scratch CMake file outside the repository, and give the command. Synthetic
        clicks carry no user activation, so a clipboard write may be refused in the page test: assert what the
        browser allows and say what a real gesture is needed for (the lead tries it by hand). EM_JS bodies pass
        through the C preprocessor: no apostrophes in JavaScript comments. The lead writes CHANGELOG.md: put the
        entry's text and the API additions in the handoff.
```
