# Scout report: the plan's hand checks, automated in headless Chrome

## Headline
- All 11 items pass in headless Chrome 154 (ANGLE Metal, muted) with real CDP input: 0 product failures over about 120 rows, no console line on the demo page, 0 refused records.
- Two gate findings: under SwiftShader the shipped self-test fails `editor.pixels` every time, and the pixel row is exact only on a still frame.
- A no-hook scenario (11 rows, about 15 s) is proven on the shipped site; a 1.2 KB `Module.fcmpA11y()` hook would make the remaining rows robust.

`H` below is `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-l/hand`.

## Facts

### Per item (logs in `H/logs/s*.log`)
| # | Item | Result | Evidence |
|---|---|---|---|
| 1 | START, telemetry | PASS | Running 246–264 ms after the click; 48 kHz; 188 quanta per 0.5 s; flags 0x34; refused 0; console empty. |
| 2 | Every screen | PASS | 13 click/Escape rows judged by Panel `screen`/`overlay`/`scTab`. Six `?view=` ids at scale 1 (buffer 960×640) and scale 2 (1920×1280); still-frame pixel row 0 over 2, largest ≤ 2. |
| 3 | Drag, double-click, wheel, typed value, focus | PASS | Drag −60 px: THRESHOLD −18 → −33, 30 records, engine `thrDb` −33, GR max 8.0 → 17.3 dB. Double-click → −18. Wheel raises/lowers; the page does not scroll over a slot and does over HISTORY. Typed `-30` + Return → −30; Escape cancels; `9z` refused, field stays open; `-24 db` accepted. Tab / Shift-Tab move the ring, the canvas keeps DOM focus, arrows move 0.6 dB. |
| 4 | Presets | PASS | Next/previous wrap, records carry snap 1. Browser: click loads and stays open (Mode → BUS G), double-click closes, arrows + Return, wheel scrolls the list. MODIFIED appears after a drag. Rename and delete (armed, then confirmed) work. |
| 4 | SAVE AS name entry | PASS | The name is typed with plain keydowns into the LineEdit ("Scout One" → user preset; settings shows `76 FACTORY · 1 USER`); Backspace works; `TAKEN: IT WILL BE SAVED AS 'SCOUT ONE 2'`. |
| 4 | Menus, IMPORT/EXPORT | PASS | SAVE menu `Save | Save As...` opens and acts. Row menu lists `Load, Save As..., Rename..., Export... (disabled), Import... (disabled), Delete`. IMPORT/EXPORT cells are disabled; footer says `IMPORTING PRESET FILES IS NOT AVAILABLE HERE` / `EXPORTING …`. |
| 5 | Undo/redo, A/B, COPY REPORT | PASS | Host reports the command key as Meta (footer `UNDO THRESHOLD   CMD-Z`, platform `macOS`); Ctrl-Z does nothing; Cmd-Z and Shift-Cmd-Z undo and redo in order. A|B switches. Menu `Copy B to A` opens on a letter, is chosen by click or ArrowDown + Return, Escape and an outside press dismiss it, and that press goes no further. |
| 5 | Clipboard | PASS | Permission `granted`, `writeText` resolved, 469-character report; the editor says `Copied` for 2 s. The system pasteboard's change count did not move: headless Chrome's clipboard is its own. |
| 6 | Mode change and PNGs | PASS | Arrows, wheel and browser change the Mode; the engine's `modeSlot` follows. 7 PNGs each for BUS G (stepped) and CLEAN (continuous): six views in GRAPHITE plus the panel in PAPER. |
| 7 | QUALITY / LOOKAHEAD | PASS | Latency STD 4 → HQ 61 → +5 MS 301 → +20 MS 1021 → ECO 960 → STD 964 → OFF 4, equal in status, worklet and reply. Audio continues at 375 quanta/s, refused 0. The page says "MAY CLICK". |
| 8 | Dropped file | PASS | `Input.dispatchDragEvent` with a 1 kHz −12 dBFS WAV: `SOURCE: SCOUT-TONE.WAV`, engine input −12.0 dBFS, output peak at 1002 Hz. Undecodable and too-short files leave the source with the notice. BUILT-IN LOOP returns. File input with a mono 44.1 kHz file plays on both channels. A drop before START says `PRESS START FIRST…`. |
| 9 | Context loss | PASS | Lost: `ok` 0, `error` empty, 60 frames/s counted as `lost`, audio and replies continue, page still says PLAYING. Restored: `ok` 1, 60 fps, same atlas hash, still-frame pixel row largest 1, 0 over 2. A second loss, an edit while lost, and a loss with settings + PAPER open all recover. |
| 10 | Hidden/shown, reload | PASS | Another tab in front or the window minimised: frames 0, pulls 0, audio 375 quanta/s; shown: 60/60. Reload: `pagehide` (not persisted) shuts the editor down, the page is idle and START works again. RESUME by a real click works after `context.suspend()`. |
| 11 | Zoom, reload, second tab | PASS | 1280×800: only 100 fits (`125 % NEEDS A LARGER DISPLAY`). 1920×1080: default 125; 100/125/150 selectable, 175 off. `FCompressor.uiZoom` survives a reload. A smaller window refits to 100 and keeps the stored 150. Arrows on the ZOOM group work. A second tab plays too, and its theme/zoom reach the first when it is shown again. |

### Click measurement (item 7; recorder worklet on the engine's output)
- The programme's own largest sample step is 0.108 (P99.99 0.090).
- With a lookahead budget on, the change leaves a hole of exact zeros as long as the budget (240 samples at 5 MS, 960 at 20 MS) with hard edges: steps of 0.15–0.18 (1.6–2.0× the programme).
- STD → HQ with no budget: step 0.094 (1.1×), no hole. A THRESHOLD edit: 0.062.
- The plugin does the same kind of thing: `suspendProcessing(true)` around `configureEngine()` (`Source/plugin/Processor.cpp:720-746`). "May click" is accurate.

### Findings
- **F1 (gate, another scout's area but it blocks CI).** With `--use-angle=swiftshader --enable-unsafe-swiftshader` (the runner's default off macOS) the shipped self-test fails every time.
  - `check-page.mjs H/site --page fcmp-ui --chrome-flag --mute-audio` plus those flags prints `FAIL: editor.pixels … largest difference 7 of 255, 1588 over 2`; 8 of 8 runs.
  - Metal passes 62 of 62.
  - The offenders are glyph edges everywhere (first at (45,20), the wordmark). PAPER is worse: 7,314 on the panel, 12,200 on settings (0.5 % of samples).
  - Measured on this Mac's SwiftShader, not on Linux.
  - Rule at `Source/web/ui/WebMain.cpp:147` (`kPixelTolerance`), judged at `web/main.js:653`.
- **F2 (gate).** The pixel row is exact only on a still frame. On live frames WebGL and SoftRaster differ by one pixel in a state-dependent share:

  | State | Frames over tolerance | Largest | Where |
  |---|---|---|---|
  | Default panel | 5 of 250 | 3 | TRANSFER's moving dot, x 671–684 |
  | CHARACTERISTICS | 115 of 250 | 61 | Row y 461, CONTROL PATH's GR lane |
  | THRESHOLD −30.5 | 100 of 150 | 80 | Row y 200 in HISTORY, the GR trace at 15.0 dB |

  - Still frames (before START, or context suspended and meters at rest) are exact at every threshold tried.
  - The shipped self-test passes with a running context only because it reads the frame about 0.3 s after START (`web/main.js:618` then `:641-657`).
  - Not visible to the eye. Suspect: sink vs SoftRaster on an edge that lands on a pixel boundary (FunkGui `WebGlSink`), which ADR-93 says only clips produce.
- **F3 (low, FunkGui).** A menu opened by the mouse does not hold DOM focus: `document.activeElement` is the canvas 400 ms after the right-click. `root.focus()` at `src/web/WebServices.cpp:397` is undone by the press's own default focus. Keys still work through the window capture listener.
- **F4 (low).** A licence text page logs `404 /favicon.ico` (`index.html:7` has `rel=icon`; the `.txt` files cannot). A "console is empty" rule must be about the demo page only.
- **F5 (note).** Settings and COPY REPORT say `FCOMPRESSOR 1.1.0` (`CMakeLists.txt:54`), while the demo carries v1.2 features.
- **F6 (note).** Under DevTools device emulation at ratio 2 the editor draws a 1× buffer: the ResizeObserver's device-pixel box stays 960×640 and WebHost trusts it (`src/web/WebHost.cpp:137-141,175`). A real ratio 2 gives 1920×1280. Only DevTools users see it.
- **F7 (note).** Two tabs play two loops at once, by design (audio continues while hidden).

## Design and change list

### `Scripts/web-live.sh <build-web> [--serve [port]] [--png]` (lead only)
1. Refuse unless `<build-web>/site/built-from.txt` exists.
2. `--serve`: `python3 -m http.server <port, default 8137> --bind 127.0.0.1 --directory <site>` in the foreground, printing both URLs. This is the hand-check entry.
3. Self-test: `node <funkgui-src>/tools/web/check-page.mjs <site> --page fcmp-ui --chrome-flag --mute-audio --chrome-flag <gpu>`. Run it once as is (context suspended) and once with `--chrome-flag --autoplay-policy=no-user-gesture-required` (judges `page.audio`).
4. Scenario: `node web/tests/live/scenario.mjs <site> [--png <dir>]`, exit 0/1/2 as check-page. Results go to `<build-web>/web-live/{selftest.log,scenario.log,png/}`; last line `web-live: N/M rows`.

### New files
- `web/tests/live/cdp.mjs`, from `H/lib.mjs`: serve, launch, real mouse/key/wheel/drag-file input, the port tap (Params records and reply head).
- `web/tests/live/scenario.mjs`, from `H/scenario-nohook.mjs`.
- Neither may carry a `// FCMP_WEB_TEST` line (that registers into `verify`). If CTest should know them, add `web.live.selftest` / `web.live.scenario` in `cmake/FcmpWeb.cmake` with `LABELS live`, as `fg.web.page` does.

### Scenario rows, no hook
Controls come from `Source/editor/Layout.h` (frozen FZ4); each row is judged by what crossed the port or by the DOM.
- start
- drag.threshold
- doubleclick.resets
- typed-value
- undo.chord
- preset.next-previous
- mode.next-previous
- quality-and-lookahead (latency 4 / 61 / 301 / 4)
- dropped-file
- context-loss
- no-error

Proven: 3 of 3 on the shipped site, and 10 of 11 under SwiftShader with `--disable-audio-output`; the eleventh, `no-error`, was my console rule tripping on SwiftShader's warning.

### Recommended hook (decision is yours)
`Module.fcmpA11y()`: the Panel's own `accessibility()` list as JSON (id, parent, role, bounds, title, value, enabled, checked) plus `screen`, `overlay`, `scTab`, `focus`, `focusVisible`, `textEntry`.
- Prototype at `H/hook/WebMain.cpp` (about 45 lines beside `statusText()`), built by `H/hook/build.sh` from build-web's objects.
- Cost: wasm 1,323,756 → 1,325,075 bytes, js +133.
- It makes these robust: every screen, preset names and MODIFIED, undo enabled, zoom cells, footer text, settings rows, menu anchors.
- Changes needed: `Source/web/ui/WebMain.cpp`; one line in the seam (`docs/sprints/web-d.md`, "beyond the seam"); `web/tests/size.mjs` budget re-measured.
- Do not copy my hook's `mode` field (wrong, derived from `value01`); read the Mode from the `Mode` item.
- The cheaper alternative is three ints in `fcmpStatus()` (`screen`, `overlay`, `mode`).

### Self-test changes
- F2: move the `editor.atlas` / `editor.pixels` block before `start()`, or state that it must stay within the first frames (`web/main.js:641-657` → before `:618`).
- F1: a renderer-aware rule in `web/main.js:653`: `over2 === 0` on hardware; on SwiftShader `largest <= 8` and `over2 <= 1 %` of samples (measured 7 and 0.50 %).

## Traps (driver)
1. Never pass `nativeVirtualKeyCode` to `Input.dispatchKeyEvent`. On macOS headless the key repeats without end (8,503 keydowns in 1 s on a plain page). The page's timers and port messages starve while rAF stays at 60: replies fall to 0–5/s and it looks like a product bug. `windowsVirtualKeyCode` alone is correct (`H/logs/t-keys.log`).
2. `Emulation.setDeviceMetricsOverride` with `deviceScaleFactor` 2 is not a real ratio (F6). Use `--force-device-scale-factor=2`; use the override at factor 1 only for window sizes.
3. `Page.setWebLifecycleState('frozen')` leaves the page hidden after `'active'`. Hide with `Target.activateTarget` on another tab or `Browser.setWindowBounds` minimised; show with `activateTarget` + `Page.bringToFront`.
4. Judge the pixel row only on a still frame (F2): before START, or `context.suspend()` then about 3 s for the meters to fall.
5. SwiftShader logs a warning (`GPU stall due to ReadPixels`) on the normal page. Fail on exceptions and `error` level only.
6. `Target.createTarget` takes `width`/`height` only with `newWindow`.
7. A|B: aim at a letter; x 581, the 2 px between them, hits nothing.
8. Double-click needs two presses under 400 ms and 8 px apart: WebHost counts clicks itself from the events' timestamps.
9. A letter does not open a typed field; a digit, `.`, `-`, `+` or Return does.
10. The Mode browser and the preset browser stay open after a click on a row.
11. Two tabs in one headless Chrome: 1 run of 5 hung on an unanswered `Runtime.evaluate`. Keep that check by hand.
12. The user's own Chrome is running (pid 66988). Kill only by your profile path, never `pkill Chrome`. Use `--user-data-dir` under scratch.
13. `Input.dispatchDragEvent` with `data.files` works for real drops; `DOM.setFileInputFiles` fires `change`.

## Tests (each can fail)
- **Scenario rows above.** A worklet that drops Params records (`H/site-mut`) fails 5 of 11: drag.threshold, typed-value, mode.next-previous, quality-and-lookahead, no-error.
- **Tighten two rows before committing.** `doubleclick.resets` and `preset.next-previous` passed under that mutation. Judge the reply's `thrDb` after a preceding change (the double-click must move the engine back from −33), and judge the preset by the engine's value, not only the record.
- **With the hook:** screens (overlay/screen ids), `Preset` value and `, modified`, `Undo` enabled 0 → 1 → 0, ZOOM cells' enabled set per window size, footer text over IMPORT/EXPORT, menu items by DOM `[data-funkgui-menu]`.
- **Still-frame pixel row after a context restore:** exact on Metal, 8 of 8 frames. It fails if the atlas is not re-uploaded.
- **Not for the gate:** the click figures (report as a NOTE), two tabs, the lifecycle freeze.

## Open questions and recommendations
1. **Hook or coordinates?** Recommend the hook, shipped: 1.3 KB, and it is the Panel's own a11y list, the first step of a later accessibility mirror. Without it, keep the 11-row scenario and leave screens, presets and zoom to the hand list.
2. **F1, the pixel rule under SwiftShader.** Recommend the renderer-aware rule above, confirmed by one run on the Linux runner before the CI step is made required. Otherwise the site-artifact self-test in CI is red.
3. **Autoplay flag in `web-live.sh`?** Yes for the scenario (it needs a running context). Run the self-test both ways.
4. **Scenario in CI?** It passed here on SwiftShader with no audio device (`--disable-audio-output`). Recommend adding it after one green run on the runner; it then stands in for part of the "Intel or AMD machine" check.
5. **PNGs.** Write to `<build-web>/web-live/png` and never commit. The 14 Mode PNGs and 12 view PNGs from this run are in `H/png`.
6. **Version text 1.1.0 in the demo (F5).** Recommend leaving it until the v1.2.0 bump and saying so in the README.
7. **F3 (menu focus).** A FunkGui follow-up, not this phase.

## Hand checklist for Safari and Firefox (and one Intel/AMD machine)
Serve with `Scripts/web-live.sh build-web --serve`, or `python3 -m http.server 8137 --bind 127.0.0.1 --directory build-web/site`.
1. `http://127.0.0.1:8137/?selftest=1`: the tab title becomes PASS. Note any FAIL row and the `worklet load` figure.
2. `http://127.0.0.1:8137/`: press START. Sound within a second, meters move, the status reads PLAYING. Listen for 30 s: no crackle.
3. Drag THRESHOLD left: gain reduction grows and the sound ducks. Double-click resets it. Click the value, type `-30`, Return.
4. Wheel or trackpad over a value: note the direction (the page lists it as a known difference). The page must not scroll while over a slot.
5. Cmd-Z, then Shift-Cmd-Z (Ctrl on Windows/Linux): the value goes back and forth. Check the browser's own undo does not steal the key.
6. Preset: `›` twice, click the name, click a row (sound changes), double-click a row (closes). SAVE: type a name, Return. Right-click SAVE: a menu appears beside it; choose Save As...; Escape.
7. Right-click the A letter: `Copy A to B`. Click outside: it goes and nothing else happens.
8. Mode: click the Mode name, choose BUS G, then CLEAN. Open CHARACTERISTICS and the COLOUR tab; Escape returns.
9. Gear: HQ, then 20 MS. LATENCY reads 1021 samples and sound continues (a click is expected). COPY REPORT: paste somewhere, text arrives, the cell says COPIED.
10. Drop an audio file from Finder on the page: the SOURCE line shows its name and it plays. Drop a text file: the notice appears and the source is unchanged. BUILT-IN LOOP returns.
11. ZOOM cells and PAPER; reload: both are kept.
12. Switch to another tab for 10 s and back: meters resume, no jump or error.
13. Context loss: in the browser's console run `l = document.getElementById('fcmp-canvas').getContext('webgl2').getExtension('WEBGL_lose_context'); l.loseContext()`. The picture freezes and sound goes on. Then `l.restoreContext()`: the picture returns with text intact.
14. The browser's console shows no error throughout.

## What I ran
- Read ADR-93, the plan's lead phase, `web-d.md`'s seam, `web/main.js`, `WebMain.cpp`, `Layout.h`, the views' headers, and FunkGui's `WebHost` / `WebInput` / `WebServices` / `check-page.mjs`.
- All work is in `H`; nothing was built, edited or run inside either checkout, and `git status` is clean in both.
- `H/site` is a copy of build-web's site. `H/site-hook` is the same with the hooked module, linked in scratch by em++ 6.0.3 in about 3 s from build-web's object files (read only).
- Drivers (node 24, no dependency): `H/lib.mjs`, `H/s1-start-views.mjs`, `s3-values`, `s4-presets`, `s5-edits`, `s6-modes`, `s7-quality`, `s8-file`, `s9-context-life`, `s11-zoom`, `scenario-nohook.mjs`.
- Probes: `t-keys`, `t-plain`, `t-rate2`, `t-dpr`, `t-pixels` 1–4, `t-selftest`, `t-swift`, `t-console`.
- Final results: s1 30/30, s3 12/12, s4 19/19, s5 11/11, s6 8/8, s7 9/9, s8 9/9, s9+10 11/11, s11 8/8 (three clean runs; one earlier run hung in the two-tab phase).
- 75 PNGs (7.6 MB) in `H/png`:
  - `view-<id>-dsf{1,2}.png` (12)
  - `mode-{bus-g,clean}-<view>-graphite.png` and `-panel-paper.png` (14)
  - `click-*.png` (13)
  - `3-*`, `4-*`, `5-*`, `7-*`, `8-*`, `9-*`, `11-*`
  - `swiftshader-panel.png`
- Every Chrome was headless with `--mute-audio` and its own profile. `pgrep` at the end shows none of my servers or browsers; Safari was never launched. The pasteboard was only probed by change count and was not modified.