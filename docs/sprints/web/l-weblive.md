# Scout report: `Scripts/web-live.sh`, the browser gate

## Headline
- All six views, in both themes, give the same fingerprint in real headless Chrome 154 (ANGLE Metal) as a node run of the same editor code. The prototype run of 12 capture pages plus the page's self-test took 5.7 s; the pixel row held on every page (largest 1 or 2 of 255, 0 over 2).
- It costs one module export (`Module.fcmpFrame()`, +6,144 B wasm, +2,928 B gzip), three query pins plus sync preview under `dt`, a `--facade web` mode in `ui.dump`, and its own node runner. FunkGui's `check-page.mjs` cannot do it.
- No golden is added. It is a live comparison like gui-live's, and an expectation directory written by the script lets a bare site artifact be tested.

## Facts
Paths: `R` = `/Users/seanfunk/audio/plugins/FCompressor`, `S` = `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-l/weblive`.

**What exists today**
1. The self-test triggers on `?selftest=1` or the path `fcmp-ui.html` (`R/web/main.js:189-190`). Its rows are `browser`, `engine.selfcheck.main`, `engine.selfcheck.worklet`, `worklet.render`, `engine.silence` (:608), `page.start`, `page.status`, `editor.atlas` (:652), `editor.pixels` (:653), `editor.draws`, `editor.link`, `page.audio`.
2. Its NOTE lines are the user agent, `worklet load: 10 s rendered in N ms, Nx real time` (:553), and the editor and worklet status JSON. The verdict is `document.title` (RUNNING, then PASS or `FAIL: <row>`), the log is `#funkgui-log`, and a watchdog fires at 75 s (:747).
3. `Module.fcmpSelftest()` returns `{atlasHash, pixels{frames,largest,over2,samples}}` for the current view only (`R/Source/web/ui/WebMain.cpp:306-318`).
4. The module reads the pins `view` (:361-364), `theme`, `zoom`, `dt`, `scale` (:375-378). It always sets `syncPreview = false` (:359). It has no `nohint` and no `nolive` pin, and no export that returns a frame's list or fingerprint.
5. The native editor sets `syncPreview` whenever `FCMP_UI_FIXED_DT > 0`, and reads `UI_NO_HINT`, `UI_NO_LIVE`, `UI_VIEW` (`R/Source/editor/gpu/Editor.cpp:38-43`; `PanelOptions` at `Panel.h:86-88`).
6. gui-live's shape:
   - Five views × clean, DT `0.0166666675` (`R/Scripts/gui-live.sh:52-54`). Settings is left out because its DIAGNOSTICS show the real processor.
   - Headless side: `fcmp_probe_plugin ui.dump … -- --view V --dpi 2 --theme 0` (:212). Both dumps go through `funkgui_framerender --fingerprint` (:270).
   - Every line but `live` is compared (:286). The live dump must say `dpi 2 clock fixed` (:280) and `rate idle`.
   - Results go to `<build>/gui-live/`; the last line is `gui-live: N/5 equal`; exit 0, 1 or 2 (:49, :300-301). It takes the lock `/tmp/fcmp-gui.lock`.
   - It is a custom target `verify-gui-live` (`R/cmake/FcmpProbes.cmake:295-304`) and a milestone gate (03 step 8, `docs/design/03-build-verify-process.md:1413`). 03 has no web text yet.
7. `check-page.mjs` (`R/build-web/_deps/funkgui-src/tools/web/check-page.mjs`):
   - Serves with `python3 -m http.server 0` (:148).
   - Opens only `<stem>.html` with no query string (:153), and needs `<stem>.js` and `.wasm` beside it (:68-73), which is why `fcmp-ui.html` exists.
   - Reads only the title and `#funkgui-log` (:215). No evaluate hook, no screenshot.
   - Any `--chrome-flag` replaces its default GPU flags (:82-84). Exit 0 PASS, 1 FAIL, 2 could not run (:20-21).
   - It still passes on the prototype site: `check-page: PASS`, exit 0, 2.1 s.
8. The fingerprint code is FunkGui core (`src/canvas/Fingerprint.cpp`) and is already an object of `fcmp_web_ui` (`R/build-web/build.ninja:2304`). `addMetrics` in the same file needs only the header-only harness.
9. `ui.dump` is in `fcmp_probe_web` (`R/cmake/FcmpProbes.cmake:103-113`: files with no `FCMP_PROBE` line are included). It runs under node in 0.05 s for `panel`, but only over a `FakeFacade` with `Panel{true,true,false}` (`R/Tools/probes/plugin/ui_dump.cpp:232`).
10. `ui.geometry.clean` passes under node against the shared goldens (`R/build-web/probe-results/ui.geometry.clean.json`: wasm32, pass, 560 golden rows, 0 fail).

**Measured in the prototype (headless Chrome, `--mute-audio --use-angle=metal`)**
11. Relinking build-web's own objects in scratch reproduces `fcmp-ui.wasm` and `fcmp-ui.js` byte for byte (`cmp`), so the size deltas below are exact.

| Variant | wasm bytes | wasm gzip -9 | js bytes |
|---|---|---|---|
| shipped | 1,323,756 | 444,743 | 55,707 |
| pins only | +213 | +12 | +208 |
| pins + `fcmpFrame` (fingerprint in the module) | +6,144 | +2,928 | +208 |
| pins + dump text export (`open_memstream`) | +6,070 | +2,996 | +655 |

`web.size`'s 20 % budgets hold for all of them.

12. With `?view=V&theme=T&zoom=100&scale=2&dt=0.0166666675&nohint=1&nolive=1`, before START, the browser's fingerprint equals:
    - the `<view>.dpi2.*` golden rows of `R/tests/golden/base/modes/clean/ui.geometry.txt` for the five gui-live views (41 to 49 rows each, tags included), e.g. panel `geometry 738cee5019a4f33c text 3e535f56470579c4`;
    - a node `ui.dump --facade web --host "Google Chrome 154" --nolive 1` (prototype) for all six, settings included (`geometry a337e3b270dc06e4 text 306354ce3a105d98`).
13. Settings against the FakeFacade golden differs in `text`, `statics`, `texts`, `tag.settings_text` and `tag.settings_value`; its `geometry` is equal. The static rows show `FCOMPRESSOR 1.1.0`, `FUNKGUI 0.14.0`, `WEB · GOOGLE CHROME 154` and `THE HOST HAS NOT STARTED THE AUDIO`. The `live` count also differs (29 node, 51 browser), which is why `live` is excluded.
14. What each pin does:
    - Without `nolive`: all six differ (`display_sub` missing, statics −9). A WebFacade before START has no rate.
    - Without `nohint`: equal, but 359 frames to settle instead of 1 to 30.
    - Without `dt` (free clock, the shipped async preview): equal.
    - With `dt` and the async preview forced: equal, settle 29 to 30.
    - Without `scale`: dpi 1, differs.
    - Without `zoom`: the fit picks 125 %, dpi 2.5, differs.
15. A node side run with the wrong host (`Firefox 140`) gives `DIFFERS settings`, exit 1. The row can fail.
16. Cost: 190 to 390 ms per capture page (load, settle, fingerprint, pixel row at 1920×1280, screenshot). Six views × two themes took about 3.3 s; the self-test page 1.9 s.
17. `funkgui_framerender --fingerprint` on a product dump drops the product's tag rows (24 of panel's 35 `tag.*` lines); the other lines equal the module's.

## Design and change list

**Method (a).** One capture page per view × theme, never pressing START (no AudioContext, no sound). The runner evaluates `Module.fcmpFrame()` and compares every line but `live` with the node value, then requires the hooks line `dpi 2 clock fixed … drawn 1 idle 1`.

**Node value.** `fcmp_probe_web ui.dump --mode clean … -- --view V --dpi 2 --theme 0 --facade web --host WEB-LIVE --nolive 1 --fp <expect>/V.node.fp`, run by the script with scratch `FCMP_PREFS_DIR` and `FCMP_PRESETS_DB`. Six runs take under 1 s.

**Why not goldens.** Settings holds the version strings, so a golden would drift on every version or pin bump.

**How expected values travel.** The script writes `<build>/web-live/expect/*.node.fp`. CI uploads `build-web/web-live/` beside `build-web/site/`, never inside it (`web.size` requires exactly the site's files). The lead then runs `--dir <site> --expect <dir>`.

**Module: `Source/web/ui/WebMain.cpp`** (prototype diff `S/WebMain.patch`, 107 added lines, compiled under the project's full `-Werror` list)
1. Pins: `nohint=1` sets `PanelOptions::skipHint`; `nolive=1` sets `ignoreLive`; `syncPreview = (dt pinned)`, as `Editor.cpp:42`.
2. New pin `host=<text>` (at most 31 characters) replaces the browser's name in `setEnvironment("WEB", …)`. Not prototyped; about 6 lines.
3. `Module.fcmpFrame()` through a fifth function pointer in `fcmp_ui_ready`:
   - Loop `host.frame(now)` until `!wantsFullRate`, at most 600 frames; stop when a frame is not submitted (hidden document, lost context).
   - Return text: one hooks line, then framerender's line format (`geometry`, `text`, `statics`, `live`, `texts`, `rrects`, `segments`, `areas`, `max_x`, `max_y`, `tag.<name>`, `view_w`, `view_h`, `glyphs_missing`).
   - After shutdown it returns an empty text.
4. Update the header comment, and the seam text in `docs/sprints/web-d.md` (the lead's revision of a frozen section).

**Probe: `Tools/probes/plugin/ui_dump.cpp`** (prototype `S/ui_dump.patch`, 65 added lines)
- `--facade web`: a `WebFacade` over a link that drops everything; `--host <name>`; `--nolive 1`; `--fp <path>` writing the same lines in-process.
- Do not use framerender for the web gate (fact 17).

**Runner: new `Scripts/web/live.mjs`** (prototype `S/live.mjs`, 224 lines; node ≥ 22 for the global `WebSocket`)
- A node `http` server on 127.0.0.1 port 0, with `application/wasm` and `Cache-Control: no-store`. No python.
- One headless Chrome with a throwaway profile, always `--mute-audio`; ANGLE Metal on macOS, SwiftShader elsewhere; `--autoplay-policy=no-user-gesture-required`.
- One tab at a time through DevTools: navigate, `Runtime.evaluate`, `Page.captureScreenshot`.
- Capture pages, then `?selftest=1` on `index.html` with every log line copied into the summary. That is where the page's rows land, including the other scout's (c) rows. If (c) needs data from the script, `Page.addScriptToEvaluateOnNewDocument` can inject it.
- NOTE lines: `Browser.getVersion`, and `SystemInfo.getInfo` (it printed `ANGLE Metal Renderer: Apple M5`).

**Script: new `Scripts/web-live.sh`** (sh, gui-live's shape)
```
Scripts/web-live.sh <build-web>                     the gate: node values from <build>, <build>/site in Chrome
Scripts/web-live.sh --dir <site> [--expect <dir>]   a downloaded artifact (default expect: <site>/../web-live/expect)
Scripts/web-live.sh --serve <build-web>|--dir <site> [--port N]   serve only; prints the URLs; Ctrl-C stops
  --out <dir> (default <build>/web-live, or ./web-live with --dir), --chrome <path> or $CHROME, --timeout <s> (default 120)
```
- Reads `CMakeCache.txt` as gui-live does: `CMAKE_HOME_DIRECTORY`, `FCOMPRESSOR_WEB`, and node from `CMAKE_CROSSCOMPILING_EMULATOR`.
- Output lines: `EQUAL|DIFFERS|FAIL <view>.theme<t>: geometry … text … (rows; hooks; pixels …)`, the page's `PASS|FAIL|NOTE` lines, and last `web-live: N/M passed (results in <dir>)`.
- Exit: 0 all passed; 1 a view differs or a row failed; 2 usage, no Chrome, no node ≥ 22, no verdict.
- Results in `build-web/web-live/`: `expect/<view>.node.{fp,dump}`, `<view>.theme<t>.live.fp`, `<view>.theme<t>.png` (the canvas clipped out of Chrome's own screenshot, 1920×1280, overlay hidden by the runner), `selftest.log`, `selftest.png`, `summary.txt`.
- No lock: headless Chrome takes no window.

**Build and documents**
- `cmake/FcmpWeb.cmake`: a custom target `verify-web-live` (depends on `fcmp_web_site` and `fcmp_probe_web`), never labelled `verify`.
- 03 §3.6 gets a web paragraph, and step 8 gets `Scripts/web-live.sh build-web`.
- CI web job: run it after Verify, then upload `build-web/site` and `build-web/web-live`.

**(b)** Run the pixel row on all six views in both themes at the pinned 2× (12 rows, about 3.3 s), plus the self-test's unpinned default view.

**(d)** In a browser today, "the flush-to-zero self-test result" is two things: the self-check hash in the worklet and on the main thread, and `engine.silence` (gate off, limit ×3). Those two rows and the `worklet load` NOTE are kept verbatim in `summary.txt`. The explicit flush-boundary test (`web.simd`) exists only in `fcmp_web_check` under node and is not in the site. The meaningful `engine.silence` number is the x86 CI runner's Chrome, not this Mac's.

## Traps
- `Emulation.setDeviceMetricsOverride` makes `devicePixelRatio` read 2 while the canvas buffer stays 960×640. Pin `scale=2` and `zoom=100`.
- The node side must set scratch `FCMP_PREFS_DIR` and `FCMP_PRESETS_DB`, or the lead's preferences leak into the settings frame (the animation step, for one).
- With `--serve` on a fixed port, a human's localStorage (theme, zoom, animation, new-instance QUALITY and LOOKAHEAD) persists and changes the frame. Default to a random port (a fresh origin) and say "use a private window".
- `ui.dump` prints `HARNESS ERROR unknown flag` lines by design. Judge it by its exit code and the output file.
- A hidden tab is neither ticked nor drawn, so `fcmpFrame` must stop on `drawn 0` rather than loop. Keep tabs sequential.
- Without the `host` pin, an expectation made with one Chrome version fails settings on another.
- The chars views sit at largest 2 of 255, exactly the tolerance, on Metal. SwiftShader on the Linux runner is unmeasured.
- The runner needs node ≥ 22 for the global `WebSocket`. Check what `$EMSDK_NODE` is in CI.
- Never pass `--chrome-flag` semantics through to this runner: its flags are fixed, and `--mute-audio` is unconditional.
- `web.size`: the module grows 0.46 %, inside the budget. Nothing may be added to `site/`.

## Tests (each can fail)
- `frame.<view>.theme<0|1>` (12 rows): every fingerprint line but `live` equals the node value, and the hooks line is right. Shown failing with a wrong host, without `nolive`, and without the scale or zoom pin.
- `pixels.<view>.theme<t>` (12 rows): `frames 1, samples > 0, over2 0`. It fails with no context (frames 0) or a sink error.
- `expect.golden.<view>` (5 rows, only when the source tree is at hand): the node WebFacade value equals `ui.geometry`'s `<view>.dpi2.*` golden rows. This ties the browser to the blessed goldens, and held for all five in the prototype.
- `frame.chars.sidechain.async` (optional): the same view with no `dt` pin, so the shipped rest-then-compute preview reaches the same frame in a browser (measured equal).
- `selftest`: the page's verdict and every row of it, with a FAIL for no verdict inside the timeout.
- Under node (`web/tests/page.mjs` or a new test): the pins the runner sends are the ones `WebMain.cpp` reads.

## Open questions, with recommendations
1. **Fingerprint in the module, or dump text out and fingerprint elsewhere?** In the module. The size is the same, a bare artifact then needs no tool, and framerender loses the product tags. Add the dump export (+6 KB more) only if you want `<view>.live.dump` for diagnosis; the PNG and the `tag.*` lines localise a difference already.
2. **`host` pin, or pass the real browser name to the node side?** The pin. Expectations become browser-independent and travel with the artifact. Add a NOTE with the real `status.host`.
3. **Sync preview under a pinned `dt`?** Yes, as the native editor does (02 §3.7 rule 7). Both paths were equal; the optional async row covers the shipped path.
4. **Settings as the sixth view?** Yes. It is only possible with the WebFacade node side, and it is the one view where the browser shows its own facts.
5. **Own runner or `check-page.mjs`?** Own. Keep `fcmp-ui.html` and check-page for Sprint D's by-hand [PAGE] command.
6. **Other Modes (PNGs "in one stepped and one continuous Mode")?** Not in this change. It needs a `mode` pin and a WebFacade Mode write on both sides; a small follow-up if you want those PNGs automatic.
7. **Safari and Firefox by hand:** `--serve` prints the six capture URLs and the expected `geometry` and `text`. Optionally a `want=<geometry>.<text>` query makes the page's self-test judge it in the title (about 5 lines in `main.js`).
8. **Run it in CI's web job, or only upload?** Run it there (Chrome is on the runner; x86 gives the real silence number), then upload both directories. Expect the first run to settle the SwiftShader pixel question.

## What I ran
- Read: ADR-93, the plan's lead phase, `web-d.md`, `gui-live.sh`, `main.js`, `WebMain.cpp`, `check-page.mjs`, `WebHost.h/.cpp`, `Fingerprint.cpp`, `FrameRender.cpp`, `ui_dump.cpp`, `ui_web.cpp`, `ui_geometry.cpp`, `Editor.cpp`, `Settings.cpp`, `FcmpProbes.cmake`, `FcmpWeb.cmake`, `ci.yml`.
- In `S` only:
  - `build.sh`: em++ compile of a scratch `WebMain.cpp` with build-web's flags, linked with build-web's objects. Five variants.
  - A scratch `fcmp_probe_web` with the patched `ui_dump.cpp`.
  - `node live.mjs --dir site --out out3 --expect expect --themes 0,1 --selftest`: 13/13, exit 0, 5.7 s.
  - The pin variations, the wrong-host run, and `check-page.mjs` on the scratch site.
  - `build-web/fcmp_probe_web.js ui.dump` and `build-lead`'s `funkgui_framerender --fingerprint`, with outputs in scratch.
- Every Chrome ran headless with `--mute-audio`. `pgrep` shows none of my Chromes or servers left; one `http.server` under `scout-l/hand` belongs to another scout. `git status` in FCompressor is clean at 58f13e9.
- Kept for the implementers: `S/WebMain.patch`, `S/ui_dump.patch`, `S/live.mjs`, `S/build.sh`, `S/out3/summary.txt`, `S/out6/*.png`.