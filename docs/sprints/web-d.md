# Web Sprint D — the editor in wasm under node, and the page: task manifests

Status: **sprint base**, written by the lead from the judged plan (`docs/sprints/web/plan.md`, "Sprint D") and three
scout reports made on the Sprint C result (`docs/sprints/web/d-nodeprobes.md`, `d-module.md`, `d-page.md`). Manifest
format as in `docs/sprints/web-c.md` (read by `Scripts/sprint/ownership.py docs/sprints/web-d.md#<ID> <worktree>`).
The user authorised this sprint and no further: **the plan's "Lead phase" is not authorised.** No browser gate in CI,
no upload of the site, nothing published; the site is a build output on this machine.

Goal. The editor runs as WebAssembly: the real UI probes pass under node against the same goldens, and a page on
127.0.0.1 plays a loop through the real engine in an AudioWorklet with the real Panel drawn on a canvas. **The plugin
does what it did:** every existing golden row holds natively.

**Base.** FCompressor: branch `web/sprint-d` at the commit that adds this file (on Sprint C's result): FZ0–FZ5
frozen. FunkGui: branch `web/sprint-d` = v0.13.0 plus `WebHostConfig::beforeTick`; FCompressor pins that commit for
the sprint (the lead tags v0.14.0 at the end).

**The lead has already made** (FCompressor; read them first):
- The probes under node: `cmake/FcmpProbes.cmake` builds `fcmp_probe_web` in the web configuration (the editor outside
  `gpu/`, the portable model code, the facade, the engine archive, the probe commons and every `layer=ui` probe) and
  registers its probes under node with `--arch wasm32`; `platform=` takes a comma list over `apple|linux|web`;
  `lint.deps` runs in the web tree; `Scripts/verify.sh build-web` judges them as probes. At the base all 204 `ui.*`
  tests pass under node with no overlay.
- The editor module: `cmake/FcmpSources.cmake` globs `Source/web/ui/*.cpp`; `cmake/FcmpWeb.cmake` builds
  `fcmp_web_ui` (`fcmp-ui.js`, `fcmp-ui.wasm`: the editor, the model code, the facade, `Source/web/ui`, FunkGui's core
  and `FunkGui::web`), `fcmp_web_port_check` (a node module of `Tools/web/port/*.cpp` with `PortLink` and the facade,
  for `web/tests/port.mjs`) and `fcmp_web_site` (`build-web/site`, assembled by `cmake/FcmpWebSite.cmake`: the page's
  files, the two modules, the licences, `built-from.txt`, and `fcmp-ui.html`, a copy of `index.html` that FunkGui's
  page runner can open). All are part of the `web` build.
- `Source/web/ui/{PortLink.h,PortLink.cpp,WebMain.cpp}` and `web/{index.html,main.js,fcmp-worklet.js,loop.js,
  demo.css}`: the scouts' working prototypes, committed as the base so that both halves build and run from the first
  minute. They are U-1's and U-2's from here: bring them to the manifest, do not treat them as finished.
- Lint rules `web.emscripten` (an Emscripten header only under `Source/web/ui`) and `web.ui`.

**Read before writing anything:** your card's scout report in `docs/sprints/web/` (facts with file:line or a scratch
run, the change list, the traps, the tests worth adding), `docs/sprints/web/plan.md` ("Sprint D"), ADR-93 in
`docs/DECISIONS.md`. Where a scout's "open question" is answered here, this manifest binds. The reports cite scratch
files as `S/...` (a session directory that may be gone): the ones worth reading are kept, as text, in
`docs/sprints/web/scratch-d/` (`wn-*`: the preview's cost programs; `ui-*`: the port test and the heap-view trap;
`page-*`: the worklet, loop, size and reconfigure tests, the Chrome scenario and its runner). They are references,
never built: write your own files in your OWNS paths.

## The seam between the module and the page (frozen: both U-1 and U-2 write against it)

- **Page to module, before `fcmp-ui.js` loads:** `var Module = { fcmpReady() {…}, onAbort(what) {…} }`. The DOM holds
  `canvas#fcmp-canvas`. `Module.onRuntimeInitialized` fires before `main()`: the page waits for `fcmpReady`, which
  `main()` calls last.
- **Module to page:**
  - `Module.fcmpPort.connect(port, sampleRate, maxBlock)` and `Module.fcmpPort.disconnect()`. After `connect` the
    port's `onmessage` is the module's: the page listens with `addEventListener('message', …)` and `port.start()`.
  - `Module.fcmpStatus()`: a JSON text with `ok, error, frames, fps, zoom, replies, refused, flags, latency,
    prepared, rate, posted, dropped, host`.
  - `Module.fcmpSelftest()`: a JSON text with `atlasHash` (16 hex digits) and `pixels` (`{frames, largest, over2,
    samples}`: one frame drawn through the sink and read back in the same call, against SoftRaster).
  - `Module.fcmpResetEngine()`, `Module.fcmpShutdown()`.
  - Query parameters the module reads: `view`, `theme`, `zoom`, `dt`, `scale`.
- **The port's wire rule.** A message whose `data` is an `ArrayBuffer` is one `WebProtocol` record; anything else is
  private to the page and the worklet (objects with an `fcmp` member).
  - Page to worklet: a Pull travels in a 16,704-byte carrier (`sizeof(Reply)`), with the record in its first 16
    bytes; every other record in a buffer of exactly its size. Both are transferred, never a view on the wasm heap.
  - Worklet: copies at most 140 bytes to its inbox, posts `n = Header::bytes` (the u32 at offset 8: the one field a
    script reads), and when `fcmp_web_post` returns `r > 0` copies `r` bytes of the reply into the same buffer and
    transfers it back. When `r <= 0` it sends nothing and counts a refusal.
  - Page-private messages: to the worklet `{fcmp: 'stats'}`, `{fcmp: 'selfcheck'}`; from it `{fcmp: 'ready', abi,
    latency, sampleRate}`, `{fcmp: 'error', error}`, `{fcmp: 'stats', …}`, `{fcmp: 'selfcheck', rc, hash, ms}`.
- **Who pulls:** the module, from `WebHostConfig::beforeTick`. The page never pulls.
- **Added in the web lead phase** (`docs/sprints/web-lead.md`, "The gate's contract"): `Module.fcmpFrame()` and
  `Module.fcmpA11y()`, and the query pins `nohint`, `nolive` and `host`.
- A change to this section is an interface-change request in a handoff, never an edit: the other card depends on it.

**Five agents share the machine** (four cards build): builds at `-j6`, at most two build directories per agent,
delete a build directory as soon as you no longer need it. Run gates with `FCMP_TIMING_SCALE=3`; rerun a timing row
alone before concluding anything from it. **Emscripten** 6.0.3: the system-library cache is shared; on a cache lock or
a half-written library, wait and retry, never clear the cache. Headless Chrome plays sound: always pass
`--chrome-flag --mute-audio` (and, since any flag replaces the runner's defaults, `--chrome-flag --use-angle=metal`).

**Frozen set.** FZ0–FZ5 as in `docs/sprints/s13.md`; approved revisions are listed per card. `Source/web/engine/**`
(the protocol and the engine) and `Source/web/facade/**` are frozen for every card: a need there is an
interface-change request.

**Lead-owned, as always:** `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `docs/**`,
`tests/golden/**`, `.github/**`, `README.md`, `CLAUDE.md`, `LICENSE`; FunkGui's `CHANGELOG.md`, `test/golden/**`, its
VERSION. If a lead-owned file has a bug that blocks you, make the minimal edit, keep its meaning, and list it under
"ownership deviations needing approval" in the handoff.

**New rows are spec rows.** No card adds a golden row.

---

## W-N

```text
TASK    W-N   card: WEB-D.1   repo: FCompressor (isolation: worktree)   size: M
        branch: web-d/w-n (rename the harness-made branch: git branch -m web-d/w-n)
GOAL    The UI probes under node are complete (ui.font, the preview's cost), and the editor stays smooth where no
        preview thread can exist
OWNS    Tools/probes/plugin/ui_font.cpp Tools/probes/plugin/ui_previewcost.cpp Source/editor/PreviewWorker.cpp
        Source/editor/PreviewWorker.h Tools/probes/plugin/ui_geometry.cpp Tools/probes/plugin/ui_chars.cpp
FROZEN  FZ0–FZ5. Approved here: PreviewWorker.h's comments (the contract's new sentence); its declarations stay.
        ui_geometry.cpp and ui_chars.cpp only where the asynchronous-preview rows need more simulated time, with
        every row's key and value kept. No golden row may move; never bless. Every path outside OWNS: never edit.
FUNKGUI the base's pin. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/d-nodeprobes.md (the whole report: the node run, ui.font, the preview's measured cost and
        why it matters, the traps) Source/editor/PreviewWorker.{h,cpp} Source/editor/PreviewCompute.h
        Source/editor/views/StepPlot.cpp (who requests, how often) Tools/probes/plugin/ui_geometry.cpp
        (skeleton.preview.*) Tools/probes/plugin/ui_chars.cpp (worker.async_equal) cmake/FcmpProbes.cmake CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. ui_font.cpp registers on web too (`platform=apple,web`): without JUCE the atlas is FunkGui's committed
           macOS bake, so the row is macOS's. The comment says so.
        2. THE LEAD'S DECISION on the preview (scout Q1): one synchronous request costs 85 to 420 ms under node, so
           computing it on every change would drop the page to 2 to 10 frames a second while a control moves. Where
           an asynchronous PreviewWorker has no thread (Emscripten without pthreads; a system that refuses one), a
           request is computed inline only once no newer request has replaced it for 0.15 s of tick time: the Panel
           stays smooth while a control moves and the plots follow when it rests. The synchronous option
           (PanelOptions::syncPreview, what the probes use) computes at once, as now. With a thread nothing
           changes. PreviewWorker.h's contract says it in one sentence.
        3. Tools/probes/plugin/ui_previewcost.cpp, `// FCMP_PROBE layer=ui name=previewcost scope=mode timeout=120`:
           per Mode, one request through a synchronous worker: a spec row that it completed and is the result the
           asynchronous path gives, and the milliseconds as a note (never a limit: it runs beside other tests). On
           the no-thread path: rows that a request replaced within the rest is never computed, that the last one is
           computed once the rest has passed, and that ticks during the rest cost no computation (count the jobs,
           not the time).
        ACCEPTANCE (part of DONE)
        [AGENT] FCMP_TIMING_SCALE=3 cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0
                blocking, 0 drift, 0 missing.
        [WEB]   cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web": every ui.* probe
                passes as wasm32 under node, ui.font and ui.previewcost included, 0 drift, 0 missing.
        Numbers in the handoff: the preview's cost per Mode under node and natively (the scout's table, re-measured
        through your probe); the row counts of ui.geometry and ui.chars before and after (unchanged).
NOTES   Under node the asynchronous rows of ui.geometry and ui.chars run on the no-thread path: with the rest they
        need 0.15 s of simulated ticks before the result exists. Natively they run on the thread and must not
        change at all. A catch never runs in the web build (no exception model): nothing on the no-thread path may
        rely on one. The real fix for a live plot while dragging is a Worker (the plan's follow-up), which needs
        the user's go: say in the handoff what your probe's numbers mean for it.
```

## U-1

```text
TASK    U-1   card: WEB-D.2   repo: FCompressor (isolation: worktree)   size: L
        branch: web-d/u-1 (rename the harness-made branch: git branch -m web-d/u-1)
GOAL    The editor module: the Panel over the web facade on a canvas, and the byte link to the worklet
OWNS    Source/web/ui/*.h Source/web/ui/*.cpp Tools/web/port/*.cpp web/tests/port.mjs
FROZEN  FZ0–FZ5; Source/web/engine/** and Source/web/facade/**; "The seam between the module and the page" in this
        file. web/** except web/tests/port.mjs is U-2's: never edit it. Source/editor/** and Tools/probes/** are not
        yours. Every path outside OWNS: never edit. Never bless.
FUNKGUI the base's pin (v0.13.0 plus WebHostConfig::beforeTick). No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/d-module.md (the whole report: what WebMain must do, PortLink, the worklet's side, the
        build, the traps) Source/web/ui/* (the prototype you own now) Source/editor/gpu/Editor.{h,cpp} (the native
        counterpart) Source/web/facade/{WebFacade.h,EngineLink.h} Source/web/engine/WebProtocol.h the pin's
        include/funkgui/web/{WebHost.h,WebPrefs.h} cmake/FcmpWeb.cmake (fcmp_web_ui, fcmp_web_port_check)
        cmake/LintDeps.cmake (rules web.emscripten, web.ui) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. PortLink (the EngineLink over a MessagePort): post() hands a record to JavaScript and never blocks; a
           Pull travels in the recycled 16,704-byte carrier, every other record in a buffer of its own size, both
           transferred (never a view on the wasm heap: that clones the whole memory); replies arrive from a message
           task, are copied into a fixed inbox and given to the sink; before a port exists and after disconnect a
           record is dropped and counted; connect() calls the events (WebMain then calls setEngineSetup and
           resync()); a late message from a replaced port is ignored. It reads Header::kind and nothing else of a
           record. JavaScript reaches C++ through function pointers (no export list, no EMSCRIPTEN_KEEPALIVE).
        2. WebMain: installLocalStoragePrefs before the Panel exists; PortLink, WebFacade (setEnvironment "WEB" and
           the browser's name); THE LEAD'S DECISIONS on the scout's questions: the new-instance QUALITY and
           LOOKAHEAD preferences are applied on page load (Q3); the Panel is built with the asynchronous preview
           (`.syncPreview = false`: W-N makes the no-thread path rest before it computes); a funkgui::WebHost on
           #fcmp-canvas with the native editor's zoom steps, default and preference key, fit margins measured from
           the canvas, setUiAttached forwarded, the batch hooks left empty (the Panel brackets the facade itself),
           the capture pins from the query; `beforeTick` calls facade.pull() (Q1: the FunkGui hook, so a pull
           follows the host's cadence); setRenderInfo with renderer "WEBGL2", overflows 0 (Q6); the optional
           ?view=; the page-facing names of the seam, fcmpReady called last. Shutdown: setRenderInfo({}),
           Panel::shutdown(), then the host, the Panel, the facade, the link; not on a pagehide that is persisted.
        3. Module.fcmpSelftest(): the atlas hash, and one frame drawn through the host's sink and read back in the
           same call against SoftRaster (the page cannot read the canvas afterwards).
        4. Tests. Tools/web/port/*.cpp and web/tests/port.mjs (`// FCMP_WEB_TEST name=web.ui.port timeout=120
           args={build},{engine}`): PortLink and a WebFacade as wasm under node over a real MessageChannel to the
           shipped fcmp-engine.wasm driven by a small worklet stand-in of your own that follows the wire rule (do
           not import U-2's file): dropped before connect; resync on connect; one carrier for 60 pulls; flags and
           telemetry arrive; a reconfigure through the port; a late reply from a replaced port ignored; the
           patience rule; the heap-view trap (a row that fails if a view is posted without a slice).
        ACCEPTANCE (part of DONE)
        [WEB]   cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web": fcmp_web_ui
                builds warning-free under -Werror; web.ui.port passes; every other web test and every ui.* probe
                under node still passes.
        [LINT]  cmake -DFCMP_SOURCE_DIR="$WT" -P cmake/LintDeps.cmake: 0 violations (web.emscripten, web.ui).
        [AGENT] FCMP_TIMING_SCALE=3 cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0
                blocking, 0 drift (nothing native compiles Source/web/ui; lint.deps reads it).
        [PAGE]  with the base's page (U-2's prototype) in build-web/site:
                node build-web/_deps/funkgui-src/tools/web/check-page.mjs build-web/site --page fcmp-ui
                --chrome-flag --use-angle=metal --chrome-flag --autoplay-policy=no-user-gesture-required
                --chrome-flag --mute-audio prints `check-page: PASS`. Give the module's status JSON from that run
                (frames, replies, refused, carriers) and the sizes of fcmp-ui.wasm and fcmp-ui.js.
NOTES   `EM_JS(...);` with a trailing semicolon fails the project's -Wextra-semi; an EM_JS body is a C string (no
        regex literals with backslashes, no apostrophes in comments). A target-level -Os is silently overridden:
        leave the optimisation level to the lead. The stack is 1 MiB (the lead's link option). If the base's page
        cannot run your module because the seam is short of something, that is an interface-change request: say
        exactly what, and keep your side working against the seam as written.
```

## U-2

```text
TASK    U-2   card: WEB-D.3   repo: FCompressor (isolation: worktree)   size: L
        branch: web-d/u-2 (rename the harness-made branch: git branch -m web-d/u-2)
GOAL    The page: a static site that plays a loop through the engine in an AudioWorklet and shows the editor
OWNS    web/index.html web/main.js web/fcmp-worklet.js web/loop.js web/demo.css web/package.json
        web/tests/worklet.mjs web/tests/loop.mjs web/tests/size.mjs web/tests/page.mjs
FROZEN  web/tests/engine.mjs (Sprint A's) and web/tests/port.mjs (U-1's); Source/** entirely (U-1 owns
        Source/web/ui: its prototype in the base is what your page loads); "The seam between the module and the
        page" in this file. Every path outside OWNS: never edit. Never bless.
FUNKGUI the base's pin. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/d-page.md (the whole report: the audio graph's facts measured in Chrome, the worklet, the
        loop, the page's states and wording, the differences list, the site, the tests, the traps)
        web/* (the prototype you own now) Source/web/engine/{WebEngine.h,WebProtocol.h} web/tests/engine.mjs
        cmake/FcmpWeb.cmake and cmake/FcmpWebSite.cmake (what the site holds; how web tests register from
        `// FCMP_WEB_TEST` lines) docs/DECISIONS.md ADR-93 CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. web/fcmp-worklet.js: plain JavaScript, an ES module (THE LEAD'S DECISION, scout Q3: web/package.json says
           "type": "module", so node imports the shipped files): the processor compiles the engine's wasm from
           processorOptions, instantiates it with an empty import object, creates and configures the engine at the
           context's rate (scout Q7: in the constructor, at the defaults), makes its memory views once, and per
           quantum copies in, calls fcmp_web_process and copies out with no allocation, no message and no console
           call; any frame count; an empty, mono or one-channel input; never a throw. The port follows the wire
           rule of the seam (bare ArrayBuffers; Header::bytes is the one field read). The self-check runs only on
           request, never while a source plays.
        2. web/loop.js: `synthLoop(sampleRate)`, a deterministic loop made in the page (no audio file enters the
           repository): drums with real transients, a sustained bass and a pad, a whole number of beats, peaks at
           -3 dBFS, RMS between -16 and -13 dBFS (scout Q5), no click at the seam, no DC.
        3. web/index.html, web/demo.css, web/main.js: the refusals before Start (no WebAssembly, an insecure
           context, no AudioWorklet, no WebGL2 probed on a throwaway canvas, file:); Start creates the AudioContext
           synchronously in the click (48 kHz asked for, the actual rate used), loads the worklet and the engine's
           bytes, connects the module's port, plays the loop through a gain node; a dropped or chosen audio file
           replaces the loop (size and length limits, fades, a clear notice when it cannot be decoded, the source
           unchanged on failure); the context's state changes show RESUME; the wording of the scout's section 2.6,
           upper case as the Panel speaks. THE LEAD'S DECISIONS: no page-level A/B button, the text names the
           Panel's own BYPASS (Q6); the list of what differs from the plugin (the scout's section 2.7, checked
           against ADR-93, plus: with no input the engine idles and the meters stop); the licences linked from the
           footer (the site's licences/ directory, made by the lead's CMake) and the built commit from
           built-from.txt (no dead link for `none` or a dirty tree); all URLs relative, so the site works from any
           path; nothing loaded from another origin.
        4. ?selftest=1 (and the path fcmp-ui.html, which the unchanged FunkGui runner opens): rows for the worklet's
           self-check hash in a real AudioWorklet and on the main thread against the constant, a 10 s offline render
           through the real worklet, the silence-cost row (scout Q7 (a): timed through the existing ABI, judged at
           x3 over best-of-three windows), the atlas hash and the pixel row from Module.fcmpSelftest(), frames
           drawn and replies arriving. document.title is RUNNING, then PASS or FAIL: <the first failing row>; rows
           in #funkgui-log; an uncaught error or rejection is a FAIL that PASS can never replace.
        5. Tests under node, one `// FCMP_WEB_TEST` line each (the scout's section 2.8 and 4): web.worklet (the
           shipped script over the shipped engine: output bit-equal to the module driven directly, any frame count,
           every input shape, Pull replies in its carrier, refusals counted, no message from process, no heap
           growth), web.loop (the properties of deliverable 2 at 44.1, 48 and 96 kHz: never a hash, Math differs by
           engine), web.size (the site is exactly the expected files, each within a raw and a gzip budget of the
           measured size plus 20 %, nothing absolute or cross-origin), web.page (the page's constants equal the
           repository's; every file index.html names is in the site; the licence files are byte-equal to their
           sources).
        ACCEPTANCE (part of DONE)
        [WEB]   cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web": the four new
                tests pass with every other web test and every ui.* probe under node.
        [PAGE]  node build-web/_deps/funkgui-src/tools/web/check-page.mjs build-web/site --page fcmp-ui prints
                `check-page: PASS` with no flag (the context suspended) and with --chrome-flag --use-angle=metal
                --chrome-flag --autoplay-policy=no-user-gesture-required --chrome-flag --mute-audio (running);
                with --chrome-flag --disable-webgl2 it prints a FAIL that names the browser. Give the rows.
        Proofs in the handoff: for the worklet's no-allocation rule, the wire rule (a Pull posted with the carrier's
        byte length is refused) and the sticky FAIL, the row that fails when the rule is broken.
NOTES   Never unmuted: headless Chrome plays through the lead's speakers. Stop every server you start. The module
        in the base is U-1's prototype: if it lacks something the seam promises (Module.fcmpSelftest may be a
        stub), write your row so that it fails clearly with "the module does not provide …" and say so; the lead
        runs the page with both cards merged. Safari and Firefox are not run in this sprint: write to the
        specifications, and list in the handoff what you could not check.
```

## G-F

```text
TASK    G-F   card: WEB-D.4   repo: FunkGui (lead-made worktree)   size: S
        worktree: /Users/seanfunk/audio/libraries/FunkGui.wt/web-d-gf   branch: web-d/g-f
GOAL    WebHost's per-frame hook is proven
OWNS    test/web/host.cpp test/web/host.html
FROZEN  include/funkgui/web/WebHost.h and src/web/WebHost.cpp carry the lead's hook (WebHostConfig::beforeTick,
        called before Panel::tick in every frame that ticks): if a row shows it wrong, say so in the handoff with
        the evidence; do not edit them. Everything else stays as it is. Never bless.
FUNKGUI base = branch web/sprint-d (v0.13.0 e348524 plus the lead's hook commit; your branch starts there).
DONE    FunkGui's CLAUDE.md "Done means" plus the Acceptance below.
READS   include/funkgui/web/WebHost.h (the hook's comment is its contract) src/web/WebHost.cpp (frame())
        test/web/host.cpp (how the page drives host.frame() under a pinned clock and counts what the Panel sees)
        tools/web/check-page.mjs FunkGui's CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Claims on the host page for the hook: it runs exactly once per frame that ticks the Panel, before that
           tick (the Panel sees what the hook brought in the same frame); it does not run while the document is
           hidden (no tick, no hook); it does not run after stop() or after the host is destroyed; an empty hook
           is nothing. Each claim fails when its rule is broken (show it: move the call after the tick, call it
           before the hidden gate, call it twice; then restore).
        ACCEPTANCE (part of DONE)
        cmake --workflow --preset web-verify && tools/verify.sh "$FWT/build-web": 0 blocking, 0 drift; the sink's
        page and the services page still PASS and the host page PASSes with the new claims
        (tools/web/check-page.mjs build-web/test/web [--page host | --page services]).
        cmake --workflow --preset agent-verify && tools/verify.sh "$FWT/build-agent": 0 blocking, 0 drift (nothing
        native changes; fg.headers compiles WebHost.h).
NOTES   The lead writes CHANGELOG.md and tags v0.14.0: give the entry's text in the handoff.
```
