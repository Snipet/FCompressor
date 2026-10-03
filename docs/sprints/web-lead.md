# Web lead phase — the browser gate, CI and the final documents: task manifests

Status: **phase base**, written by the lead from the judged plan (`docs/sprints/web/plan.md`, "Lead phase: browser
gate, CI, documents, publish") and five scout reports made on Sprint D's result (`docs/sprints/web/l-weblive.md`,
`l-print.md`, `l-ci.md`, `l-hand.md`, `l-docs.md`). Manifest format as in `docs/sprints/web-d.md` (read by
`Scripts/sprint/ownership.py docs/sprints/web-lead.md#<ID> <worktree>`). The user authorised the lead phase.
**Hosting** (decided by the user while the cards ran, 2026-10-02): GitHub Pages, published by CI on every push to `main`
once every job is green, from the site artifact the gate tested (the lead's `publish` job in `.github/workflows/ci.yml`;
no card publishes anything).

Goal. `Scripts/web-live.sh build-web` is the browser gate: in headless Chrome the editor's six views equal the node
values, the pixels equal SoftRaster's, the engine's 112 print rows hold through the real worklet, and a scripted user
(start, drag, type, undo, presets, Modes, quality, a dropped file, a lost context) finds nothing wrong. CI builds the
site, keeps it, and runs the gate on the downloaded copy. The documents say what exists. **The plugin does what it
did:** every existing golden row holds natively and under node.

**Base.** FCompressor: branch `web/lead` at the commit that adds this file (on `main` 58f13e9 = Sprint D). FunkGui:
the pin, v0.14.0; no FunkGui change is planned in this phase (a need there is a request in the handoff).

**The lead has already made** (read them first; they work as they stand, and the cards bring them to this manifest):
- `Source/web/ui/WebMain.cpp`: the scouts' prototypes behind two switches (`PROTO_FP`, `PROTO_DUMP`): the pins
  `nohint`, `nolive` and the synchronous preview under a pinned `dt`; `Module.fcmpFrame()`; `Module.fcmpA11y()`.
  `Tools/probes/plugin/ui_dump.cpp`: `--facade web`, `--host`, `--nolive`, `--fp`. At the base the five gui-live views
  give the same fingerprint in headless Chrome as under node (and as the native editor in `gui-live`).
- `cmake/FcmpSources.cmake`, `cmake/FcmpWeb.cmake`, `cmake/FcmpWebLive.cmake`: `fcmp_web_print`
  (`build-web/live-obj/fcmp-print.wasm` from `Tools/web/live/*.cpp`), `fcmp_web_live` (`build-web/live`: `web/live`'s
  pages, the test modules, `golden/<key>.dsp.print.txt`), `verify-web-live` (once `Scripts/web-live.sh` exists). All
  part of the `web` build and of what the gate's stamp covers. Nothing of it is in the site.
- Prototypes in place, not wired to anything: `Tools/web/live/PrintModule.cpp`, `web/live/fcmp-print.{html,js}`,
  `Scripts/web/cdp.mjs` (the hand scout's library), `Scripts/web/scenario.mjs`, `Scripts/web/live.mjs`,
  `Scripts/web/page-check.mjs`. Further prototypes are kept as text in `docs/sprints/web/scratch-l/` (the reports
  cite scratch paths that may be gone: these are the files worth reading; never built from there).

**Read before writing anything:** your card's scout report (the whole of it: facts, design, traps, tests), the plan's
lead-phase section, ADR-93 in `docs/DECISIONS.md`, and `docs/sprints/web-d.md`'s "seam between the module and the
page". Where a scout's open question is answered here, this manifest binds.

## The gate's contract (frozen: every card writes against it)

- **Module pins** (query parameters, beside Sprint D's `view`, `theme`, `zoom`, `dt`, `scale`): `nohint=1` (no first-use
  hint: `PanelOptions::skipHint`), `nolive=1` (`PanelOptions::ignoreLive`), `host=<text>` (at most 31 characters of
  `[A-Za-z0-9 ._-]`: replaces the browser's name in `setEnvironment("WEB", …)`, so an expectation is the same for
  every browser). A pinned `dt` makes the preview synchronous, as the native editor does under `FCMP_UI_FIXED_DT`.
- **`Module.fcmpFrame()`**: settles the host (frames until the Panel no longer asks for the full rate, at most 600;
  it stops at a frame that was not drawn, and on a page whose engine is publishing it draws one frame and returns it as
  it is: a frame of live audio is never reported as settled) and returns a text: the line `hooks dpi <g> clock <fixed|free> theme <n> dt
  <g> settle <n> drawn <0|1> idle <0|1>`, then `geometry <16 hex>`, `text <16 hex>`, `statics`, `live`, `texts`,
  `rrects`, `segments`, `areas`, `max_x`, `max_y`, one `tag.<name> <n>` per tag, `view_w`, `view_h`,
  `glyphs_missing` (one `name value` per line). Empty after shutdown.
- **`Module.fcmpA11y()`**: a JSON text `{screen, overlay, scTab, revision, fullRate, focus, focusVisible, textEntry,
  items: [{id, parent, role, x, y, w, h, enabled, checked, v, title, value, description}]}`: the Panel's own
  accessibility list (visible items; logical px) and its state. No `mode` member: the Mode is the `Mode` item's value.
- **A capture page**: `index.html?view=<id>&theme=<0|1>&zoom=100&scale=2&dt=0.0166666675&nohint=1&nolive=1&host=WEB-LIVE`,
  START never pressed (no AudioContext, no sound). The six ids: `panel`, `chars.sidechain`, `chars.colour`,
  `modebrowser`, `presetbrowser`, `settings`.
- **The node value of a capture**: `<node> <build>/fcmp_probe_web.js ui.dump --mode clean --golden-dir
  <source>/tests/golden --out <tmp>.json -- --view <id> --dpi 2 --theme <t> --facade web --host WEB-LIVE --nolive 1
  --fp <expect>/<id>.theme<t>.node.fp`, with `FCMP_PREFS_DIR` and `FCMP_PRESETS_DB` set to scratch paths (`ui.dump`
  prints `HARNESS ERROR unknown flag` lines by design: judge its exit code and the file). The comparison is every line
  but `live` and `hooks`; the hooks line must say `dpi 2 clock fixed … drawn 1 idle 1`.
- **What the gate's server serves** (its own node server on 127.0.0.1, a free port, `application/wasm`, no caching,
  nothing above its roots): `/` = the site (exactly the shipped files), `/live/` = the live directory
  (`<build>/live`), `/expect/` = the expectation directory. A live page reaches the shipped files as `../<name>`.
- **A live page** (`web/live/<name>.html`) speaks the self-test's protocol: `document.title` is `RUNNING`, then `PASS`
  or `FAIL: <the first failing row>`; rows are `PASS|FAIL|NOTE     <test> <row>: <detail>` lines in `#funkgui-log`; an
  uncaught error is a FAIL that no PASS replaces. The runner opens every `*.html` of the live directory in turn.
- **The expectation tool**: `node Tools/web/live/expect.mjs --site <site> --live <live dir> --out <expect dir>` writes
  what a live page compares with and node can compute (the shipped engine under node: the rows that have no golden),
  as JSON files the pages fetch from `/expect/`. Exit 0, or 2 with a reason.
- **The scenario**: `node Scripts/web/scenario.mjs --dir <site> --out <dir> [--png] [--chrome <path>]` prints
  `PASS|FAIL|NOTE` rows and ends with `scenario: N/M passed`; exit 0, 1 (a row failed), 2 (it could not run).
- **The gate**: `Scripts/web-live.sh <build-web>` (node values from the build, `<build>/site` in Chrome);
  `Scripts/web-live.sh --dir <site> --live <dir> --expect <dir>` (a downloaded artifact: nothing is built or
  computed); `--serve` (serves only and prints the URLs, for a human with another browser; Ctrl-C stops);
  `--out <dir>` (default `<build>/web-live`), `--chrome <path>` or `$CHROME`, `--timeout <s>`. Lines: `EQUAL|DIFFERS|FAIL
  <view>.theme<t>: …`, the pages' `PASS|FAIL|NOTE` lines, and last `web-live: N/M passed (results in <dir>)`. Exit 0,
  1 (a view differs or a row failed), 2 (usage, no Chrome, no node 22 or later, no verdict). Results:
  `<out>/expect/`, `<out>/frames/<view>.theme<t>.live.fp`, `<out>/png/`, `<out>/selftest.log`, `<out>/<page>.log`,
  `<out>/scenario.log`, `<out>/summary.txt`.
- **Chrome is always headless, with `--mute-audio` and a throwaway profile** (never the user's; never killed by
  name): ANGLE Metal on macOS, SwiftShader elsewhere. `--autoplay-policy=no-user-gesture-required` only where a row
  needs a running context.
- A change to this section is an interface-change request in a handoff, never an edit: other cards depend on it.

**Seven agents share the machine at most six at a time** (six cards build): builds at `-j6`, at most two build
directories per agent, delete a build directory as soon as you no longer need it. Run gates with `FCMP_TIMING_SCALE=3`;
rerun a timing row alone before concluding anything from it. **Emscripten** 6.0.3: the system-library cache is shared;
on a cache lock or a half-written library, wait and retry, never clear the cache. **Headless Chrome plays sound through
the user's speakers: every run passes `--mute-audio`**; never a visible window; give every Chrome its own
`--user-data-dir` under your scratch; kill only your own processes, by that path; check with `pgrep` before the
handoff. Never pass `nativeVirtualKeyCode` to `Input.dispatchKeyEvent` (on macOS the key repeats without end). Safari
is never launched or automated here; Firefox is not installed. No network.

**Frozen set.** FZ0–FZ5 as in `docs/sprints/s13.md`. `Source/web/engine/**`, `Source/web/facade/**`,
`Source/web/ui/PortLink.{h,cpp}`, `web/fcmp-worklet.js` and `web/loop.js` are frozen for every card: what ships does
not change in this phase except where a card's manifest says so. A need there is an interface-change request.

**Lead-owned, as always:** `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `docs/**`,
`tests/golden/**`, `.github/**`, `README.md`, `CLAUDE.md`, `LICENSE` — except where a card's OWNS names a path. If a
lead-owned file has a bug that blocks you, make the minimal edit, keep its meaning, and list it under "ownership
deviations needing approval" in the handoff.

**New rows are spec rows.** No card adds a golden row; never bless.

---

## L-M

```text
TASK    L-M   card: WEB-L.1   repo: FCompressor (isolation: worktree)   size: M
        branch: web-l/l-m (rename the harness-made branch: git branch -m web-l/l-m)
GOAL    The module reports a settled frame and its accessibility list; the node side makes the same frame
OWNS    Source/web/ui/WebMain.cpp Source/web/ui/FrameText.h Tools/probes/plugin/ui_dump.cpp
        Tools/probes/plugin/ui_webframe.cpp
FROZEN  FZ0–FZ5; Source/web/ui/PortLink.{h,cpp}; "The gate's contract" in this file. Source/editor/** and every other
        probe are not yours. Every path outside OWNS: never edit. Never bless.
FUNKGUI the pin (v0.14.0). No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/l-weblive.md (the whole report) docs/sprints/web/l-hand.md ("Recommended hook", traps)
        docs/sprints/web/scratch-l/weblive-*.txt Source/web/ui/WebMain.cpp (the prototype you own now)
        Source/editor/gpu/Editor.cpp (the native pins: FCMP_UI_FIXED_DT, UI_NO_HINT, UI_NO_LIVE)
        Tools/probes/plugin/ui_dump.cpp ui_geometry.cpp ui_web.cpp the pin's include/funkgui/canvas/Fingerprint.h
        Scripts/gui-live.sh (what the native gate compares) cmake/LintDeps.cmake (web.ui, web.emscripten) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. WebMain.cpp to the contract: the pins `nohint`, `nolive`, `host` (validated as the contract says; anything
           else is ignored and the browser's own name stays), the synchronous preview under a pinned `dt`;
           Module.fcmpFrame() and Module.fcmpA11y() as the contract gives them (no `mode` member; every string
           through the JSON escaper the status uses); both callable at any time and after shutdown; the two PROTO
           switches and every PROTOTYPE comment gone; the header comment and the seam's list of names updated.
           No dump export (THE LEAD'S DECISION: the fingerprint is made in the module; the PNG and the tag lines
           localise a difference).
        2. Source/web/ui/FrameText.h: the fingerprint's text (the lines after `hooks`) written ONCE, portable C++
           (no Emscripten header), used by WebMain.cpp and by ui_dump.cpp's --fp, so the two sides cannot drift.
        3. ui_dump.cpp to the contract: `--facade web` (a WebFacade over a link that drops everything: the page
           before START), `--host`, `--nolive 1`, `--fp <path>`; the existing flags and their output unchanged
           (gui-live uses them: prove it by running Scripts/gui-live.sh's headless half or by comparing dumps
           before and after for the five views).
        4. Tools/probes/plugin/ui_webframe.cpp, `// FCMP_PROBE layer=ui name=webframe scope=global timeout=120`
           (follow the first-line form of a global ui probe that exists): for the five gui-live views at dpi 2,
           theme 0, the WebFacade frame with nolive has the same geometry and text fingerprints, counts and tag rows
           as the blessed `ui.geometry` goldens' `<view>.dpi2.*` rows of Mode clean (read from the golden file at
           run time: no new golden); the settings view under two `--host` names differs in text and not in
           geometry; FrameText's lines parse back to the Fingerprint. Natively and under node.
        ACCEPTANCE (part of DONE)
        [WEB]   FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web".
        [AGENT] FCMP_TIMING_SCALE=3 cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0
                blocking, 0 drift (ui_dump and the new probe compile natively).
        [LINT]  cmake -DFCMP_SOURCE_DIR="$WT" -P cmake/LintDeps.cmake: 0 violations.
        [CHROME] headless, muted: for the six views and both themes, Module.fcmpFrame() on the capture page equals
                the node value of the contract (every line but `live`), with the hooks line right; show each pin
                failing the comparison when left out (nolive, scale, zoom, host on settings); fcmpA11y() parses as
                JSON on every screen and its items' bounds hit the controls (click the centre of the THRESHOLD
                item's bounds and see the edit); both exports after Module.fcmpShutdown(). Sizes of fcmp-ui.wasm
                and fcmp-ui.js raw and gzip, before and after.
NOTES   The base's Scripts/web/live.mjs is another card's file: use it read-only (or your own scratch driver) for
        the [CHROME] runs. web.size's budgets are L-J's file: if the module outgrows one, say by how much.
```

## L-R

```text
TASK    L-R   card: WEB-L.2   repo: FCompressor (isolation: worktree)   size: L
        branch: web-l/l-r (rename the harness-made branch: git branch -m web-l/l-r)
GOAL    Scripts/web-live.sh, the browser gate, and its runner
OWNS    Scripts/web-live.sh Scripts/web/live.mjs Scripts/web/cdp.mjs web/tests/weblive.mjs
FROZEN  "The gate's contract" in this file; the exported names of Scripts/web/cdp.mjs as the base has them (card L-S
        imports them: add, never rename or remove; say in the handoff what you added). Scripts/web/scenario.mjs is
        L-S's, Scripts/web/page-check.mjs is L-W's, web/live/** and Tools/web/live/** are L-P's, Source/** is L-M's:
        never edit them. Every path outside OWNS: never edit. Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   docs/sprints/web/l-weblive.md (the whole report) docs/sprints/web/l-ci.md (facts 5-7, traps; how CI will
        call the script) docs/sprints/web/l-print.md ("Scripts/web-live.sh", traps) Scripts/gui-live.sh (the shape to
        follow: options, output, exit codes, how it reads CMakeCache.txt) Scripts/web/live.mjs and Scripts/web/cdp.mjs
        (the prototypes) cmake/FcmpWeb.cmake (fcmp_web_live, verify-web-live) web/main.js (the self-test) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Scripts/web/cdp.mjs: the one library for driving headless Chrome (the static server of the contract with
           its three roots; launching Chrome with a throwaway profile, always muted, the GPU flag by platform, found
           from --chrome, $CHROME, the macOS path or PATH; tabs; evaluate; screenshot; the input and port-tap
           helpers the base has, kept as they are for L-S). No dependency; node 22 or later is checked with a clear
           message. Every Chrome it starts is killed on every exit path, also on SIGINT and SIGTERM.
        2. Scripts/web/live.mjs on that library: the capture pages (six views x two themes at the pinned 2x: the
           frame against the node value, the hooks line, the pixel row from Module.fcmpSelftest(), a PNG of the
           canvas); one row for the shipped asynchronous preview (chars.sidechain with no dt pin reaches the same
           frame); the page's own self-test (index.html?selftest=1) once with the context suspended and once with
           the autoplay flag, every log line copied; every page of the live directory by the live-page protocol
           (when the directory has none, a NOTE, not a failure); the scenario (when Scripts/web/scenario.mjs is
           there) as a child process, its rows copied; the summary and the results of the contract.
        3. Scripts/web-live.sh (sh, gui-live's shape): the three forms of the contract. With a build tree it makes
           the node values itself (the contract's ui.dump command, scratch preference paths) and runs the
           expectation tool when Tools/web/live/expect.mjs exists. It refuses a tree that is not a web build, a
           site without built-from.txt, and a missing node or Chrome, each with exit 2 and the reason. --serve
           serves on a random free port (a fresh origin: no stale localStorage), prints the page's URL, the
           self-test's URL and the six capture URLs with their expected geometry and text, and stays until
           interrupted.
        4. web/tests/weblive.mjs, `// FCMP_WEB_TEST name=web.live.runner timeout=120 on=web args={source},{build}`:
           without a browser: the server (wasm type, no caching, HEAD, 404, nothing above a root in any spelling,
           the three roots), the comparison (a differing line fails, `live` and `hooks` are excluded, a missing
           expectation is a FAIL and never a pass, a wrong hooks line fails), the usage errors and their exit
           codes, the summary's last line. Each row shown to fail.
        ACCEPTANCE (part of DONE)
        [WEB]   FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web".
        [LIVE]  Scripts/web-live.sh "$WT/build-web": `web-live: N/N passed`, exit 0, on the base's module and pages
                (the print page of the base may not follow the live-page protocol yet: if it does not, say what it
                did; do not edit it). Then the same on a copy of the three directories with --dir/--live/--expect.
                Show the gate failing (exit 1) for: a view's expectation made with another host; a site whose
                fcmp-ui.wasm is another build's; a live page that never gives a verdict (exit 1 with the timeout
                named, not a hang); and exit 2 for no Chrome. Time of a full run. `pgrep` clean afterwards.
NOTES   Emulation.setDeviceMetricsOverride does not give a real device pixel ratio: the pins scale=2 and zoom=100
        do. Keep tabs sequential (a hidden tab is neither ticked nor drawn). The runner's flags are fixed: there is
        no pass-through of Chrome flags. CI will run this script on an x86 Linux runner under SwiftShader with no
        audio device: write nothing that assumes Metal, a GPU or macOS paths, and print the renderer as a NOTE.
```

## L-S

```text
TASK    L-S   card: WEB-L.3   repo: FCompressor (isolation: worktree)   size: L
        branch: web-l/l-s (rename the harness-made branch: git branch -m web-l/l-s)
GOAL    The scripted user: the plan's hand checks as rows in headless Chrome
OWNS    Scripts/web/scenario.mjs Scripts/web/scenario/*.mjs
FROZEN  "The gate's contract" in this file. Scripts/web/cdp.mjs is L-R's: import it, never edit it (what you need
        and it lacks goes in a file of your own under Scripts/web/scenario/, and in the handoff as a request).
        Source/** and web/** are not yours. Every path outside OWNS: never edit. Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   docs/sprints/web/l-hand.md (the whole report: the eleven items, the findings, the traps, the rows worth
        keeping) docs/sprints/web/scratch-l/hand-*.txt (the scout's drivers) Scripts/web/cdp.mjs and
        Scripts/web/scenario.mjs (the prototypes) Source/editor/Layout.h and the views (where controls are; with
        Module.fcmpA11y() you find them by name instead) Source/web/ui/WebMain.cpp (fcmpStatus, fcmpA11y)
        web/main.js (globalThis.fcmpPage) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Scripts/web/scenario.mjs by the contract's command line: one headless Chrome (the autoplay flag, muted),
           real input events only (mouse, keys, wheel, a dropped file), controls found through Module.fcmpA11y()
           (THE LEAD'S DECISION: the hook ships; coordinates from Layout.h only where the list has no item), each
           row judged by what reached the engine (the reply's values, the worklet's stats, the status) or by the
           Panel's own state, never by the script's own intent. Rows, at least: start (running, telemetry, no
           refusal); every screen opens and closes (the panel, CHARACTERISTICS and its tabs, settings, the Mode
           browser, the preset browser); a drag on THRESHOLD moves the engine and a double-click moves it back; the
           wheel; a typed value (accepted, refused, cancelled); undo and redo by the platform's chord; presets
           (next, previous, a row from the browser, MODIFIED, SAVE AS with a typed name, rename, delete; IMPORT and
           EXPORT disabled and saying why); the menus (SAVE's, a preset row's, A|B's: open, choose, Escape, an
           outside press that goes no further); A|B; a Mode change to one stepped and one continuous Mode (which
           they are: from Source/fcdsp/modes); QUALITY and LOOKAHEAD (the latency in the status, the worklet and
           the reply; audio continues; no refusal); a dropped file plays, a bad file leaves the source with its
           notice, BUILT-IN LOOP returns; a lost WebGL context recovers (and the still-frame pixel row after it);
           hidden and shown again (pulls stop and resume); the zoom steps and the preference after a reload; no
           uncaught error and no error-level console line on the demo page throughout.
        2. --png: PNGs of each of the six views in one stepped and one continuous Mode in GRAPHITE, and the panel in
           PAPER, into <out>/png (never committed).
        3. Every row can fail: show it for each group with a mutated site (the scout's worklet that drops Params
           records; a module without the hook; a page whose START does nothing) and tighten the two rows the scout
           found too weak (the double-click and the preset rows must be judged by the engine's value).
        ACCEPTANCE (part of DONE)
        [SCENARIO] node Scripts/web/scenario.mjs --dir "$WT/build-web/site" --out <scratch> --png: `scenario: N/N
                passed`, exit 0, three runs in a row; the same with SwiftShader forced (the library's GPU flag
                overridden through its own option, not by editing it); the time of a run; the PNG list. The
                mutation runs with the rows each turned red. `pgrep` clean afterwards.
        [WEB]   FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify (the build you run the scenario on; nothing
                of yours is compiled or registered: the scenario is run by Scripts/web-live.sh, L-R's).
NOTES   Not in the gate (report them as notes if you measure them): the click figures around a QUALITY change, two
        tabs, the lifecycle freeze. The pixel row is exact only on a still frame (before START, or the context
        suspended and the meters at rest). The base's module has the hook's prototype with a `mode` member: do not
        use it (L-M removes it). CI will run the scenario under SwiftShader with no audio device.
```

## L-P

```text
TASK    L-P   card: WEB-L.4   repo: FCompressor (isolation: worktree)   size: L
        branch: web-l/l-p (rename the harness-made branch: git branch -m web-l/l-p)
GOAL    The engine's print rows through the real worklet in a browser, and what the denormal range does there
OWNS    Tools/web/live/*.cpp Tools/web/live/*.h Tools/web/live/*.mjs web/live/*.html web/live/*.js web/live/*.css
        web/tests/print.mjs
FROZEN  "The gate's contract" in this file; Source/web/engine/** (the shipped module does not grow: THE LEAD'S
        DECISION is the scout's option (ii), a test-only module beside the site); web/fcmp-worklet.js. Every path
        outside OWNS: never edit. Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   docs/sprints/web/l-print.md (the whole report) docs/sprints/web/scratch-l/print-*.txt
        Tools/web/live/PrintModule.cpp web/live/fcmp-print.{html,js} (the prototypes you own now)
        Tools/probes/common/PrintProgram.h Signals.h Tools/probes/dsp/print.cpp Tools/web/enginecheck.cpp
        Source/web/engine/{WebEngine.h,WebEngine.cpp,WebProtocol.h} web/fcmp-worklet.js web/tests/worklet.mjs
        cmake/FcmpWeb.cmake and cmake/FcmpWebLive.cmake (fcmp_web_print, fcmp_web_live) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Tools/web/live/PrintModule.{h,cpp}: the test-only module (dsp.print's program, the four parameter sets
           and a setup as WebProtocol Params records, the harness's hash), its exports declared in the header (no
           pragma), the project's comment style, warning-free under the probes' warning list.
        2. web/live/fcmp-print.{html,js} by the live-page protocol, reaching the shipped files as ../fcmp-worklet.js
           and ../fcmp-engine.wasm and the goldens as golden/<key>.dsp.print.txt (parsed in the page): all 112
           blessed rows through the SHIPPED worklet in an OfflineAudioContext, each row a fresh engine by the
           reconfigure method of the report (a plain snapped record is not a fresh engine), each asserting the
           quanta, the records, no refusal, the latency and the hash; `rows.count` and `program.hash` guard an
           empty or wrong run; the stats round trip before the render resumes (the record race).
        3. The rows that have no golden, compared with the shipped engine under node (never with native):
           Tools/web/live/expect.mjs by the contract writes them (ECO, HQ, HQ with a lookahead budget, 44.1 kHz;
           the tail and the floor values of the denormal range for all 14 Modes); a live page compares. THE LEAD'S
           DECISION: the tail and floor value rows are NOTE-level where the browser's values differ from node's
           (a browser that flushes on its audio thread is a fact to record, not a defect) and PASS where equal;
           everything else is a row.
        4. What the denormal range costs, on a live page: the floating-point environment in process(), in the
           worklet's message handler and on the main thread (a NOTE: flush-to-zero and denormals-are-zero, on or
           off, from JavaScript doubles in a test-only processor of your own); per Mode, silence against signal on
           the main thread with the gate off (a row at x2, as node's web.engine.tail) and through the worklet (a
           NOTE); the load, per Mode at STD and HQ, as real-time factors (a NOTE). One row at a render quantum
           other than 128 where the browser allows it (`renderSizeHint`), a NOTE where it does not.
        5. web/tests/print.mjs, `// FCMP_WEB_TEST name=web.worklet.print timeout=180 on=web args={source},{build}`:
           the node twin: the shipped worklet script in a stand-in scope over the shipped engine, the test module's
           records, all 112 rows against the goldens; and the expectation tool's output well-formed. Shown failing
           with a plain record (108 rows) and with one golden value changed.
        ACCEPTANCE (part of DONE)
        [WEB]   FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web":
                web.worklet.print passes with every other test.
        [CHROME] each live page, headless and muted, served by the contract's map (your own scratch server, or the
                base's Scripts/web/live.mjs read-only if it already serves /live/ and /expect/): the title PASS and
                the rows; the time of each page; the pages failing by name when the worklet drops a record, when a
                golden value is changed, and when an uncaught error is thrown. Keep the instance count per page load
                under the limit the scout found (the 125th engine instance throws).
NOTES   Buffers at the context's rate, or the browser resamples. The Emscripten cache is shared: a non-LTO link
        writes system libraries into it once; that is expected, never clear it. Safari and Firefox are not run
        here: write to the specifications (Firefox may have no OfflineAudioContext.suspend: one context per row
        must work as a fallback), and list in the handoff what you could not check.
```

## L-W

```text
TASK    L-W   card: WEB-L.5   repo: FCompressor (isolation: worktree)   size: M
        branch: web-l/l-w (rename the harness-made branch: git branch -m web-l/l-w)
GOAL    The page's self-test in any browser a WebDriver can drive (for CI's Firefox and Safari)
OWNS    Scripts/web/page-check.mjs web/tests/pagecheck.mjs web/tests/support/*.mjs
FROZEN  "The gate's contract" in this file. Scripts/web/cdp.mjs, live.mjs and scenario.mjs are other cards'.
        Every path outside OWNS: never edit. Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   docs/sprints/web/l-ci.md (the whole report: design D and E, the traps, what is unverified)
        docs/sprints/web/scratch-l/ci-*.txt Scripts/web/page-check.mjs (the prototype you own now) web/main.js (the
        verdict protocol) cmake/FcmpWeb.cmake (how web/tests/*.mjs register; the glob is flat, so web/tests/support
        registers nothing) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Scripts/web/page-check.mjs: WebDriver classic over HTTP with no dependency: its own static server by the
           contract's map (`<site>`, `--live <dir>`, `--expect <dir>`); the driver by browser (chromedriver from
           $CHROMEWEBDRIVER or PATH, geckodriver from $GECKOWEBDRIVER or PATH, safaridriver) on a free port;
           New Session with the capabilities the report gives (Chrome headless, muted, the GPU flag by platform;
           Firefox headless, volume 0, WebGL forced; Safari as it is and refused unless GITHUB_ACTIONS=true or
           --safari-here is given); the self-test (index.html?selftest=1): the verdict from the title, the log,
           the WebGL renderer, a screenshot; with --frames, the six capture pages of the contract, each compared
           with <expect>/<view>.theme0.node.fp as the gate compares (every line but `live` and `hooks`); with
           --pages, every live page by the live-page protocol; --autoplay; --log, --screenshot, --timeout. Exit 0
           PASS, 1 FAIL, 2 could not run. The session is deleted and the driver's process group killed on every
           exit path.
        2. web/tests/pagecheck.mjs, `// FCMP_WEB_TEST name=web.pagecheck timeout=180 args={source}`, with its fake
           driver under web/tests/support/: the protocol logic against a fake WebDriver server: the three verdicts
           and exit codes, the frames comparison, cleanup on every path, a hung page, a crashed or silent driver,
           the capabilities per browser, the Safari refusal, the server. Each rule shown to fail by a mutation.
        ACCEPTANCE (part of DONE)
        [WEB]   FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web":
                web.pagecheck passes with every other test (it registers in the web tree only: every web/tests/*.mjs
                is web-only in cmake/FcmpWeb.cmake, whatever its on= says).
        Say plainly in the handoff that no real driver has been met (none is installed here; Safari must not be
        automated on this machine): the lead verifies on CI's runners. List what you expect to go wrong first.
NOTES   Keep it small: this is a CI tool. A snap Firefox breaks geckodriver's profile directory; a driver that dies
        must not race a failed fetch (the scout's first prototype did).
```

## L-J

```text
TASK    L-J   card: WEB-L.6   repo: FCompressor (isolation: worktree)   size: S
        branch: web-l/l-j (rename the harness-made branch: git branch -m web-l/l-j)
GOAL    The page's self-test holds on a software renderer and on a live frame
OWNS    web/main.js web/index.html web/demo.css web/tests/page.mjs web/tests/size.mjs
FROZEN  web/fcmp-worklet.js, web/loop.js, the other tests of web/tests; Source/**; "The gate's contract" and
        Sprint D's seam. Every path outside OWNS: never edit. Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   docs/sprints/web/l-ci.md (facts 7-8, design A, trap 11) docs/sprints/web/l-hand.md (findings F1, F2, F4)
        docs/sprints/web/scratch-l/ci-main.js-pixel-class.diff.txt web/main.js web/tests/page.mjs web/tests/size.mjs
        the pin's test/web/page.cpp (FunkGui's software bound and why) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. `editor.pixels` judged by renderer class (THE LEAD'S DECISION): a GPU keeps "none over 2 of 255"; a
           software renderer (the WebGL renderer's name matching SwiftShader, llvmpipe, softpipe or a software
           rasteriser; read from a throwaway canvas and logged as a NOTE) gets FunkGui's bound: no sample over 16
           and at most 10 per mille over 2. The rule is a function web/tests/page.mjs holds with rows on both
           sides of each number and for an unknown renderer name (which is judged as a GPU).
        2. The pixel and atlas rows judged on a STILL frame: before START (the scout measured that live frames
           differ from SoftRaster by a pixel on a moving trace in a state-dependent share: only the first 0.3 s
           made today's row pass). The row's text says it is a still frame.
        3. web.size's budgets re-measured in the handoff's table if main.js outgrows its entry (the lead re-measures
           the modules after the merge); `rel=icon` so a licence text page does not ask for /favicon.ico (F4), if
           that needs no new file.
        ACCEPTANCE (part of DONE)
        [WEB]   FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web".
        [PAGE]  node build-web/_deps/funkgui-src/tools/web/check-page.mjs build-web/site --page fcmp-ui with
                (a) --chrome-flag --use-angle=metal --chrome-flag --mute-audio, (b) the same plus --chrome-flag
                --autoplay-policy=no-user-gesture-required, (c) --chrome-flag --use-angle=swiftshader --chrome-flag
                --enable-unsafe-swiftshader --chrome-flag --mute-audio: `check-page: PASS` three times, ten runs of
                (b) in a row; the rows of each. Show the row failing for a GPU frame with samples over 2 and for a
                software frame over the bound (a mutated rule or injected numbers).
NOTES   Every flag is its own --chrome-flag argument (a group passed as one word gives FAIL: browser). The count of
        samples over 2 under SwiftShader moves from run to run: never pin a count.
```
