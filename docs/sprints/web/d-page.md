# Scout report: Web Sprint D, card W-U part 2 — the page and the site

`FC` = /Users/seanfunk/audio/plugins/FCompressor (web/sprint-c, 9746e6d). `FG` = /Users/seanfunk/audio/libraries/FunkGui (web/sprint-c, bc072e4). `S` = /private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-d/page. `U` = the part-1 scout's scratch, `S/../ui`, which I only read.

**Headline.** A scratch site runs end to end in headless Chrome 154: Start click, 48 kHz context, worklet over the real `fcmp-engine.wasm`, a synthesised loop, the real Panel drawing live telemetry (GR about 7 dB at defaults), file drop, BYPASS, suspend/resume, and `?selftest=1` read by FunkGui's pinned runner. Only Chrome on this arm64 Mac was run, always muted.

**What I ran, all under `S`** (both repositories still show an empty `git status --short`; nothing was built in a repository):
- `site/`: candidate `fcmp-worklet.js`, `loop.js`, `main.js`, `index.html`, `demo.css`, and `audio.js` (Web Audio facts page).
- `tests/*.mjs` under node 24.15: fake AudioWorkletGlobalScope, loop levels, sample rates, reconfigure timing, size.
- `size/main.cpp`: a 100-line stand-in for WebMain/PortLink, compiled with em++ and linked against FC's existing `build-web` archives (read-only) and FG's built `src/web` objects.
- `site2/`: my page with the part-1 scout's built editor module (`U/site/fcmp-ui.{js,wasm}`) and its seam.
- `cmake/FcmpWebSite.cmake` run with `cmake -P`; `FC/cmake/FcmpBuiltFrom.cmake` run with its output in `S`.
- Headless Chrome through FG's `check-page.mjs` (unchanged) and through my own CDP runners (`run-page.mjs`, `scenario.mjs`: trusted clicks, screenshots).
- **One side effect outside `S`:** the two em++ links added entries to Emscripten's shared `cache/js_output/` (no system library was generated).

## 1. Facts

**1.1 Audio graph and lifecycle** (Chrome 154 headless, ANGLE Metal, `--mute-audio`; [S] = `site/audio.js`, `scenario.mjs`)
1. **Autoplay.** Without a gesture, `new AudioContext()` is `suspended` and `resume()` stays pending forever. `addModule`, node construction and port messages all work while suspended: the worklet answered a self-check with zero quanta rendered. A trusted click, then `resume()`, gives `running`. With `--autoplay-policy=no-user-gesture-required` it runs at once. [S]
2. **Sample rate.** `{sampleRate: 48000}` is honoured; so is every rate from 3000 to 768000 (1 MHz throws NotSupportedError). The worklet global `sampleRate` is the context's. `fcmp_web_configure` accepts any rate in (0, 1e6) (FC/Source/web/engine/WebEngine.cpp:455); under node the default Mode ran finite from 3 kHz to 768 kHz, at 82× real time at 48 kHz and 21× at 192 kHz. [S]
3. **addModule** takes a relative same-origin URL (5 ms). A worklet script is a module script, so `export` is legal in it (the part-1 prototype uses that).
4. **processorOptions.** An ArrayBuffer is structured-cloned: the main thread still holds all 545,704 bytes afterwards. `new WebAssembly.Module(bytes)` plus the instance take 1–2 ms inside the processor constructor. [S]
5. **Worklet scope.** `performance` is undefined; `Date.now()` exists; the globals include `currentFrame`, `currentTime`, `sampleRate` and `renderQuantumSize`. [S]
6. **process() arrays.** 128 frames. With `outputChannelCount: [2]`, the output has 2 channels and an unconnected or stopped input is an empty array (`inputs[0].length === 0`). With default options the output has **1** channel when nothing is connected, and a mono source gives 1 in and 1 out. With `channelCount: 2, channelCountMode: 'explicit'`, a mono source arrives as 2 channels. [S]
7. **Looping source.** An `AudioBufferSourceNode` with `loop = true` plays. In an `OfflineAudioContext`, source to destination copies samples exactly (0 of 1,920,000 differ). [S]
8. **decodeAudioData.** It resamples to the context rate (44.1, 22.05 and 96 kHz WAV all came back at 48000), keeps mono as 1 channel, and detaches its argument. Garbage and empty input reject with `EncodingError: Unable to decode audio data`. Only WAV was decoded. [S]
9. **Suspend and visibility.** `suspend()` stops quanta (0 in 300 ms) while messages are still answered. With the window minimised (`visibilityState` hidden) for 2 s, audio kept running (754 quanta) and the editor drew 1 frame and pulled once; it resumed when shown. [S]
10. **Latency.** `baseLatency` 5.33 ms; `outputLatency` 0 until running, then 24 ms. Engine latency: 4 samples at STD, 61 at HQ, 1021 at HQ with 20 ms lookahead. [S]
11. **Secure context.** On a plain-http non-localhost origin, `isSecureContext` is false and `AudioContext.prototype.audioWorklet` is absent, although `typeof AudioWorkletNode` is still `"function"`. [S: `--host-resolver-rules`]
12. **OfflineAudioContext needs no gesture** and runs the real worklet: self-check hash `5a96ce217d29ca6f`, 3750 quanta of 128 for 10 s. [S]

**1.2 The worklet script**
13. **ABI.** `fcmp_web_process` takes any frame count; `inR == 0` is mono, `inL == 0` is silence, `outR` may be 0 (FC/Source/web/engine/WebEngine.h:51-58). The engine splits calls at its own cap (WebEngine.cpp:63, :502). Memory never grows (FC/cmake/FcmpWeb.cmake:96; `memory.grow(1)` is refused [S]), so typed-array views made once stay valid.
14. **Records.** Only Pull returns a byte count above 0; Params, Attach and Reset return 0; a refusal is negative (WebEngine.h:60-66, :93-101). Each kind requires `Header::bytes == n` exactly (WebEngine.cpp:568-590), so a Pull carried in a reply-sized buffer must be posted with n = 16. The largest record is 140 bytes and the largest reply 16,704 (FC/Source/web/engine/WebProtocol.h:102, :121).
15. **Node run** (`tests/worklet2.mjs`, 14 rows pass): ready; self-check hash through the port; Attach has no reply; Pull's reply comes back in its carrier; refusals are counted; 3000 quanta are bit-equal to the module driven directly; a 256-frame quantum equals two of 128; empty, mono and one-channel shapes do not throw; process() posted 0 messages.
16. **Cost.** Under node: 25.7 µs per quantum through the script against 23.8 µs for the module alone (a second run: 37.8 against 37.7), so the script adds nothing measurable. In Chrome, offline through the real worklet: STD 23.7 µs per quantum (113× real time, 0.9 % of a 2667 µs quantum); HQ 51.8 µs; HQ with 20 ms lookahead 48.9 µs. Default Mode only. [S]
17. **Garbage.** Heap growth over 50,000 `process()` calls is 3–9 KB, which is the measurement's noise. `gc` is reachable without a node flag (`v8.setFlagsFromString('--expose-gc')`, then `vm.runInNewContext('gc')`). [S]
18. **Port round trip** (main to worklet to main, running): median below 0.1 ms, max 0.7 ms. `fcmp_web_selfcheck` takes 30–60 ms on the worklet thread. [S]
19. **Reconfigure.** A QUALITY or LOOKAHEAD record costs 0.02–0.15 ms inside `fcmp_web_post` under node (records captured from the real editor). The audio thread does not stall; any click is the cleared engine and the latency jump. [S: `tests/reconf.mjs`]

**1.3 The loop and the file drop**
20. **Draft `loop.js`**: 16 beats at 100 BPM (9.6 s, stereo) of kick, snare, hats, a sustained bass note per bar and a detuned pad. Every voice wraps around the end and sustained voices have whole cycles, so the seam step is at most 7e-4 against in-loop steps of 0.17–0.25. Synthesis takes 80–150 ms once.
21. **Levels.** Peak is exactly −3.00 dBFS (normalised). RMS is −17.2 dBFS as drafted, −14.1 with the bass and pad raised (crest 11–14 dB); the 50 ms RMS ranges −22 to −9.6 dBFS. −12 dBFS RMS with −3 peaks needs crest 9 dB, which honest drum transients do not give.
22. **At the plugin's defaults** (threshold −18 dB, FC/Source/fcdsp/params/HostParams.cpp:28) the loop shows about 7 dB of gain reduction (`S/shot-running.png`).
23. **Drop path.** A synthetic `drop` with a mono 44.1 kHz WAV swapped the source; a text file gave the notice and left the source alone. WebHost takes no file drops (FG/include/funkgui/web/WebHost.h:42-43), so a window-level listener sees them. [S]

**1.4 Page, site, runner**
24. **BYPASS** is already a latch in the Panel's display row (FC/Source/editor/views/DisplayRow.cpp:69, :288). A trusted click on it set `Pid::bypass` to 1. [S]
25. **Editor module size**, stand-in at -O3 without LTO: `fcmp-ui.wasm` 1,301,431 bytes (gzip 438,064), `fcmp-ui.js` 51,219 (gzip 14,772). The part-1 scout's files are 1,318,250 at -O3 and 891,185 for `-Os` with LTO. FG's gallery page: 462,452 and 97,043. Engine: 545,704 (gzip 136,515).
26. **Whole scratch site**: 11 files, 1,968,346 bytes, gzip 614,975. It also passes from a nested path (`/deep/er/site/`). [S]
27. **MIME.** `python3 -m http.server` serves `.wasm` as `application/wasm`. The Emscripten glue resolves the wasm beside its own script and falls back to ArrayBuffer instantiation on a wrong type. [S]
28. **Built commit.** `FcmpBuiltFrom.cmake` works as it is for a site file: `-DFCMP_WHAT=site -DFCMP_OUT=<site>/built-from.txt` wrote `site 9746e6d… clean 2026-10-02T03:38:44Z` (line format: FC/cmake/FcmpBuiltFrom.cmake:8). Outside a git checkout it writes `site none dirty …`.
29. **FG's runner.** `check-page.mjs --page <stem>` requires `<stem>.html`, `<stem>.js` and `<stem>.wasm` in the directory and cannot pass a query string (FG/tools/web/check-page.mjs:64-73). Any `--chrome-flag` replaces the default `--use-angle=metal` (:82-84). It reads `document.title` and the element `funkgui-log` (:215-216). It never mutes audio.
30. **The pinned runner passes on the scratch site.** `node FC/build-web/_deps/funkgui-src/tools/web/check-page.mjs S/site2 --page fcmp-ui` gives PASS, exit 0, with `fcmp-ui.html` a copy of `index.html` and `main.js` treating that path as selftest. It passes without flags (context suspended) and with `--use-angle=metal --autoplay-policy=no-user-gesture-required --mute-audio` (context running). [S]
31. **Selftest rows that pass in Chrome**: both self-check hashes (main thread and worklet); 10 s offline render; silence costing 1.01–1.05× the signal with the gate off; atlas hash `b744c79b9bb755d0` from `FontService::atlasHash()`, equal to FC/tests/golden/base/global/ui.font.txt:3; frames drawn; replies with flags 0x34. With `--disable-webgl2` the page refuses and selftest says `FAIL: browser`. [S]
32. **Test registration.** `web/tests/*.mjs` register from `// FCMP_WEB_TEST` lines, web-only, labels `verify;web;global`, run as `node <file> <args>` (FcmpWeb.cmake:140-193). `{source}/web/x.js` and `{build}/site` already expand (:167-169). A `.mjs` without the line registers nothing, so helpers are allowed. CI's web job runs them under `verify.sh --strict` (FC/.github/workflows/ci.yml:195-199).
33. **The two halves fit.** `S/site2` is my page and worklet with the part-1 scout's module and seam (`Module.fcmpReady`, `Module.fcmpPort.connect(port, rate, 128)`, `Module.fcmpStatus()`, bare ArrayBuffer records): replies 53, refused 0, `prepared` 1, rate 48000, host "Google Chrome 154".

## 2. What the card must do

**Card W-U part 2** (new files only; each step leaves `web-verify` green once the lead's site target exists)
1. **`web/package.json`**: `{"type":"module"}`, so node treats `web/*.js` as modules whatever its version. It is not copied to the site. (See Q3; my scratch used classic scripts.)
2. **`web/loop.js`**: `export function synthLoop(sampleRate)` returning `{frames, left, right}`. Start from `S/site/loop.js`, raise the sustained voices toward the level the lead picks (Q5), and keep the wrap-around and whole-cycle rules.
3. **`web/fcmp-worklet.js`**: start from `S/site2/fcmp-worklet.js`.
   - Constructor: compile `processorOptions.wasm`, instantiate with `{}`, call `_initialize`, `fcmp_web_create`, malloc four audio blocks plus 140 message bytes plus 8 hash bytes, make the views once, `fcmp_web_configure(engine, sampleRate, cap)` with `cap = globalThis.renderQuantumSize ?? 128`, post `{fcmp:'ready', abi, latency, sampleRate}` or `{fcmp:'error', error}`. Catch everything.
   - `process`: read `outputs[0][0].length`; copy in with `view.set`, call, copy out; another frame count goes through an index-copy loop in chunks; empty input passes pointer 0; return true.
   - `port.onmessage`: an ArrayBuffer is a record. Copy at most 140 bytes, post with n = `Header::bytes` (u32 at offset 8), copy a reply back into the same buffer and transfer it; otherwise count the refusal. `{fcmp:'stats'}` and `{fcmp:'selfcheck'}` answer on request only.
   - **Never on the audio thread:** an allocation in `process` (no `subarray`, no closure, no array literal), a `postMessage` from `process`, a `console` call, the self-check while a source plays, a throw.
4. **`web/index.html`, `web/demo.css`**: start from `S/site2/index.html`.
   - `<link rel="icon" href="data:,">`; `#fcmp-stage` with `min-width: 960px; min-height: 640px`; `canvas#fcmp-canvas` with no border or padding.
   - The overlay (`#fcmp-start`, `#fcmp-status`), the source row (loop button, file input with `accept="audio/*"`, `#fcmp-notice` with `role="status"`), the differences list, the footer (two licence links, `#fcmp-built`), `<pre id="funkgui-log" hidden>`.
   - `var Module = { fcmpReady, onAbort }` before `<script src="fcmp-ui.js">`. All URLs relative.
5. **`web/main.js`**: start from `S/site2/main.js`.
   - **Refusals before Start:** no WebAssembly; `!isSecureContext`; no `audioWorklet` on `AudioContext.prototype`; no WebGL2 (probe a throwaway canvas); `file:` protocol.
   - **Start, synchronously in the click:** `new AudioContext({sampleRate: 48000, latencyHint: 'interactive'})` in a try, falling back to `new AudioContext()`; call `resume()` and never await it unguarded. Then `addModule`, fetch the engine bytes, make the node with the options of fact 6, synthesise the loop at `ctx.sampleRate`, `Module.fcmpPort.connect(node.port, ctx.sampleRate, 128)`, play, hide the overlay.
   - **Sources:** source, then a GainNode, then the node; a 30 ms ramp out and in on a swap; `loop = true`. Dropped file: size cap (32 MB), `file.arrayBuffer()`, `decodeAudioData`, refuse under 0.1 s or over 10 min, 5 ms fades at both ends, play.
   - **`ctx.onstatechange`:** anything but `running` shows the overlay with RESUME.
   - **`built-from.txt`:** fetched into `#fcmp-built` as a link to `https://github.com/Snipet/FCompressor/tree/<sha>`; say so when dirty; no link for `none`.
   - **`?selftest=1`** (or the path `fcmp-ui.html`): the rows of fact 31 plus `Module.fcmpSelftest()` (seam, §6). Title `RUNNING`, then `PASS` or `FAIL: <first failing row>`; rows in `#funkgui-log` as `PASS     web.page <row>: <detail>`. Uncaught errors and rejections also set FAIL.
6. **Wording of the states** (upper case, as the Panel speaks):
   - Idle: `START` / `A DRUM LOOP PLAYS THROUGH THE COMPRESSOR. SOUND STARTS WHEN YOU CLICK.`
   - Loading: `LOADING`.
   - No WebGL2: `THIS BROWSER GIVES THE PAGE NO WEBGL2, WHICH THE EDITOR IS DRAWN WITH. TRY A CURRENT DESKTOP BROWSER WITH HARDWARE ACCELERATION ON.`
   - No worklet: `THIS BROWSER HAS NO AUDIO WORKLET. …`; insecure: `THE DEMO NEEDS HTTPS (OR LOCALHOST) …`.
   - Decode error: `THIS BROWSER COULD NOT READ <NAME> AS AUDIO (WAV, MP3, FLAC AND M4A USUALLY WORK). THE SOURCE IS UNCHANGED.`
   - Paused or interrupted: `AUDIO IS PAUSED BY THE BROWSER. CLICK TO RESUME.` / `RESUME`.
   - Failure: `THE DEMO COULD NOT START: <WHY>`.
7. **The differences list** (sources: ADR-93 at FC/docs/DECISIONS.md:1071-1073, :1138-1139, :1153-1166; the handoffs `handoff-c-{W-F,G-D,G-E,E-2}.md`):
   - BYPASS in the display row compares with the dry signal at the same latency, as in the plugin.
   - No preset IMPORT or EXPORT; your own presets last until the page is closed.
   - No side-chain key input.
   - The DSP load in SETTINGS shows a dash.
   - With natural scrolling on a Mac the wheel turns a value the other way; a notched wheel moves a stepped control about two steps.
   - Typed values take plain keys only (no input method, no dead keys).
   - A QUALITY or LOOKAHEAD change rebuilds the engine on the audio thread and may click.
   - Desktop browsers with WebGL2; no screen-reader mirror; no host parameter menu on right-click.
8. **`web/tests/`** (node; one `FCMP_WEB_TEST` line each):
   - `worklet.mjs`: `name=web.worklet timeout=120 args={source}/web/fcmp-worklet.js,{engine},{source}/web/loop.js` (from `S/tests/worklet2.mjs`).
   - `loop.mjs`: `name=web.loop timeout=60 args={source}/web/loop.js` (from `S/tests/loop.mjs`).
   - `size.mjs`: `name=web.size timeout=60 args={build}/site` (from `S/tests/size.mjs`).
   - `page.mjs`: `name=web.page timeout=60 args={source},{build}/site`. Static checks: `main.js`'s two constants equal `Tools/web/enginecheck.cpp:70` and `ui.font.txt:3`; every file `index.html` names exists in the site; the licence files are byte-equal to their sources.

**What the lead must land first**
1. **`cmake/FcmpWebSite.cmake`** (new; `S/cmake/FcmpWebSite.cmake` ran in 0.03 s): rebuilds `<build>/site` from scratch through a `.tmp` rename; copies `web/*.html|*.js|*.css|*.svg` (never a plain glob: `.DS_Store`, `tests/` and `package.json` stay out), the engine wasm, `fcmp-ui.js` and `.wasm`, `LICENSE` as `licences/GPL-3.0.txt`, FunkGui's `fonts/JetBrainsMono-LICENSE.txt` as `licences/JetBrainsMono-OFL.txt`; runs FcmpBuiltFrom with `FCMP_WHAT=site`. Add one line: copy `index.html` again as `fcmp-ui.html` (Q2).
2. **`cmake/FcmpWeb.cmake`**, after the `fcmp_web_ui` target. This text was not run inside FC's CMake:
   ```cmake
   if(FCOMPRESSOR_WEB AND TARGET fcmp_web_engine AND TARGET fcmp_web_ui)
     add_custom_target(fcmp_web_site
         COMMAND ${CMAKE_COMMAND} -DFCMP_SOURCE_DIR=${PROJECT_SOURCE_DIR} -DFCMP_SITE_DIR=${CMAKE_BINARY_DIR}/site
                 -DFCMP_ENGINE_WASM=$<TARGET_FILE:fcmp_web_engine> -DFCMP_UI_JS=$<TARGET_FILE:fcmp_web_ui>
                 -DFCMP_FONT_LICENCE=${funkgui_SOURCE_DIR}/fonts/JetBrainsMono-LICENSE.txt
                 -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -P ${PROJECT_SOURCE_DIR}/cmake/FcmpWebSite.cmake VERBATIM)
     add_dependencies(fcmp_web_site fcmp_web_engine fcmp_web_ui)
   endif()
   ```
   Add `fcmp_web_ui fcmp_web_site` to the `fcmp_web` list (:131), so the `web` build preset (FC/CMakePresets.json:138) builds the site before CTest runs `web.size`.
3. **The contract of §6**, frozen, and a minimal `Source/web/ui` stub so part 2 has a real `fcmp-ui.js` from its first build (the pattern of Sprint C's refusing `WebServices.cpp`; `S/size/main.cpp` or the part-1 prototype would do).
4. **One line in `WebProtocol.h`:5**, if Q1 goes to bare buffers: "no script reads a field but `Header::bytes`".
5. **The manifest's OWNS** names the files (`web/index.html web/main.js web/fcmp-worklet.js web/loop.js web/demo.css web/package.json web/tests/{worklet,loop,size,page}.mjs`), with `web/tests/engine.mjs` FROZEN.
6. **Budgets** for `web.size` once the flags of `fcmp_web_ui` are fixed (Q4).

## 3. Traps

1. **Default node options give a mono output** when nothing is connected (fact 6). Without `outputChannelCount: [2]`, `outputs[0][1]` is undefined.
2. **`typeof AudioWorkletNode` is not the test** for worklet support (fact 11); test `isSecureContext` and `'audioWorklet' in AudioContext.prototype`. "Any static server" means https or localhost.
3. **`resume()` never settles without a gesture** (fact 1). Create the context and call `resume()` synchronously in the click, before any await (Safari is stricter than Chrome; not run).
4. **A Pull in a carrier must be posted with n = 16** (fact 14). Passing the buffer's `byteLength` is refused with −5.
5. **The self-check blocks the audio thread for 30–60 ms** (fact 18). The part-1 prototype runs it in every constructor; run it on request, in the offline context.
6. **`performance` does not exist in the worklet** (fact 5). A `Date.now()` estimate gave 12 and 89 µs for the same load in two runs; use the offline render for a load figure.
7. **Probe WebGL2 on a throwaway canvas.** A context taken on `#fcmp-canvas` by the page is the canvas's one context, and the sink's attributes would not apply.
8. **`port.onmessage` belongs to the module** after `connect` (part-1's PortLink sets it). The page must use `addEventListener('message')` plus `port.start()`.
9. **`Module.onRuntimeInitialized` fires before `main()`**; wait for the module's own `fcmpReady`.
10. **`decodeAudioData` detaches its argument** (fact 8); the engine bytes are not detached and can be reused for every node.
11. **A vm-based node test crosses realms**: `x instanceof ArrayBuffer` is false for a buffer made by the test, which would fail the worklet's record branch for the wrong reason. Import the worklet as a module, or test with `Object.prototype.toString`.
12. **An object literal in the test's own loop** (`process(inputs, outputs, {})`) showed 3.6–14 bytes per quantum that was the test's, not the script's.
13. **`Math.sin` and `Math.exp` differ by engine**, so the loop is the same per browser, not across browsers. Test its properties; never hash it.
14. **The runner plays sound.** With the autoplay flag and no `--mute-audio`, the loop comes out of the lead's speakers; and giving any flag drops `--use-angle=metal` (fact 29).
15. **A redirect page for the runner works by luck**: 3 of 3 passed, but a `Runtime.evaluate` that lands during the navigation ends the run with exit 2 (check-page.mjs:206-209, :244). Copy the page instead.
16. **`favicon.ico` 404** is a console error on every load without the `data:,` icon.
17. **`ctest -L web` is a regex**; use `-L '^web$'` or `-R '^web\.'` (c-build.md trap 8).
18. **A hidden tab stops pulls while audio runs** (fact 9). Hidden longer than the engine ring holds, the first reply carries `kReplyGap` (WebProtocol.h:94); the facade handles it. I only hid for 2 s.
19. **`built-from.txt` says `none` or `dirty`** in an export or an uncommitted tree; the page must not make a dead link.

## 4. How to verify

**Existing tests that still cover it**
- `web.engine.abi` (FC/web/tests/engine.mjs): the shipped module's exports and self-check.
- `web.engine.selfcheck`, `.print`, `.tail`, `.speed`: the engine the worklet wraps.

**New tests, under node, in `web-verify` and CI's web job**
- **`web.worklet`** proves the script moves bytes and samples unchanged: output bit-equal to the module driven directly; any frame count; all input and output shapes; Pull replies in its carrier; refusals counted; 0 messages from `process`; heap growth under 64 KB per 50,000 quanta.
- **`web.loop`** proves the material: finite; peak −3.0 ± 0.1 dBFS; RMS inside the lead's window; seam step below 1 % of the largest in-loop step; DC below 1e-3; whole-beat length at 44.1, 48 and 96 kHz.
- **`web.size`** proves the site is exactly the expected files, each within its raw and gzip budget, and that no HTML, JS or CSS loads an absolute path or another origin.
- **`web.page`** proves the page's constants equal the repository's and the licences are the real texts.

**In headless Chrome, run by the agent** (loopback only; not in `verify`)
- `node build-web/_deps/funkgui-src/tools/web/check-page.mjs build-web/site --page fcmp-ui`, then the same with `--chrome-flag --use-angle=metal --chrome-flag --autoplay-policy=no-user-gesture-required --chrome-flag --mute-audio`. Both must print `check-page: PASS`.
- It proves: the worklet's hash in a real AudioWorklet; 10 s through the real worklet; the editor drawing through WebGL2 with replies arriving; the atlas hash; the flush figure.
- Optional: `--chrome-flag --disable-webgl2` must give `FAIL: browser`.

**Cannot be automated in this sprint**
- That the loop is audible and a drag changes the sound (the lead's ears; plan.md:399-401).
- Safari, Firefox, an Intel or AMD machine.
- A real drag from the Finder; MP3, FLAC and M4A decoding.
- A device that refuses 48 kHz; the `interrupted` state.
- The gate itself (`Scripts/web-live.sh`, CI upload) is lead phase.

## 5. Open questions for the lead

**Q1. Port message shape.**
- (a) Bare ArrayBuffers; the worklet reads `Header::bytes` (part-1's prototype; I verified it in `site2`).
- (b) `{b, n}` objects, so no script reads a field (my first scratch; it also passed).
- I recommend (a): one object fewer per message and a clean `instanceof` dispatch, with the one-line comment change in `WebProtocol.h`.

**Q2. How the unchanged FunkGui runner opens selftest.**
- (a) The site also holds `fcmp-ui.html`, a copy of `index.html`, and `main.js` treats that path as selftest (verified).
- (b) A `--query` option in FunkGui's runner (a FunkGui change and a pin bump).
- (c) Sprint D has no browser run.
- I recommend (a) now and (b) in the lead phase.

**Q3. Classic scripts or ES modules for `web/*.js`?**
- Modules plus `web/package.json` let node import the shipped files directly.
- Classic scripts need a vm scope in every test (trap 11).
- I recommend modules. I did not run `main.js` as a module in a browser, and I did not check which node the CI's emsdk ships.

**Q4. `web.size` budgets.**
- Measured at -O3: engine 545,704; editor wasm about 1.31 MB; site 1.97 MB raw, 615 KB gzip.
- I recommend each file at measured + 20 % and the site at 2.4 MB raw, 760 KB gzip. If part 1 ships `-Os` with LTO (891 KB), set the editor budget from that build.

**Q5. Loop level.**
- (a) Hold −12 dBFS RMS: needs sustained voices about 6 dB under the kick, or a soft-clipped kick.
- (b) Accept RMS −16 to −13 dBFS with peaks at −3.
- I recommend (b): it already shows 7 dB of gain reduction at defaults.

**Q6. A page-level A/B button?**
- (a) Only the Panel's BYPASS, named in the page's text.
- (b) A page button writing `Pid::bypass` through a new export; two controls for one parameter, state to sync both ways.
- I recommend (a).

**Q7. The flush-to-zero row.**
- The engine exports no arithmetic flush check (WebEngine.h exports end at :87).
- (a) A timed row through the existing ABI on a main-thread instance (verified: 1.01–1.05×), judged at ×3 with best-of-three windows as `web.engine.tail` does.
- (b) A new export `fcmp_web_flushcheck`.
- I recommend (a) in Sprint D.

**Q8. Emscripten's own licences.** The wasm holds musl (MIT; `system/lib/libc/musl/COPYRIGHT` exists in the install) and libc++. Should the site carry a third-party notice beside GPL and OFL? I recommend one `licences/THIRD-PARTY.txt`, written by the lead.

**Q9. Cache-busting.** Nothing is published, so none is needed now. For the lead phase: `main.js` can append `?v=<sha12>` from `built-from.txt` to what it loads. Not tested.

**Q10. A CTest row for the browser run?** Three lines in `FcmpWeb.cmake` would register `web.page.live` with label `live` (as `fg.web.page` does, FG/test/web/CMakeLists.txt:34-37). It is not `web-live.sh`; your call whether it counts as the unauthorised gate.

## 6. Can it be split?

Yes: W-U splits into two agents with file-disjoint ownership, and fact 33 shows the halves fit.
- **U-1** (part 1): `Source/web/ui/*.cpp|*.h`.
- **U-2** (this report): `web/**` except `web/tests/engine.mjs`.
- Part 2 is one agent's work: about 700 lines of JS, HTML and CSS plus 400 of tests. Splitting the worklet and loop from the page costs more coordination than it saves.

**The seam the lead freezes first** (a header comment in `Source/web/ui`, or the manifest):
- **Page to module, before `fcmp-ui.js` loads:** `var Module = { fcmpReady(), onAbort(what) }`. DOM: `canvas#fcmp-canvas`.
- **Module to page:**
  - `Module.fcmpPort.connect(port, sampleRate, maxBlock)` and `.disconnect()`.
  - `Module.fcmpStatus()`: JSON text with `ok, error, frames, fps, zoom, replies, refused, flags, latency, prepared, rate, posted, dropped, host`.
  - `Module.fcmpResetEngine()`, `Module.fcmpShutdown()`.
  - **To add:** `Module.fcmpSelftest()`: text rows for the atlas hash and a `readPixels` of a frame against SoftRaster. It must draw and read in one call; the page cannot read the canvas afterwards.
- **Port, to the worklet:** a transferred ArrayBuffer (a record at exact size, or a 16,704-byte carrier holding a Pull); `{fcmp: 'stats' | 'selfcheck'}`.
- **Port, from the worklet:** the carrier back with the Reply; `{fcmp: 'ready', abi, latency, sampleRate}`; `{fcmp: 'error', error}`; `{fcmp: 'stats', …}`; `{fcmp: 'selfcheck', rc, hash, ms}`.
- **Who pulls** is part 1's (its prototype wraps the Panel's `tick`); the page never does.

## Not checked

- Safari and Firefox: nothing was run in either; every statement about them is from memory.
- x86; audible output (always muted); a real OS drag; any format but WAV; `file://`; the back-forward cache; mobile.
- A render quantum other than 128 in a real browser (only a 256-frame fake under node).
- A hidden tab longer than 2 s; hours of running; GC pauses on the worklet thread.
- HQ at high sample rates inside the fixed 16 MiB; any Mode but the default in Chrome.
- `main.js` as an ES module; the CMake target inside FC's tree (only the `-P` script ran); the CI runner's node and Chrome.
- The `readPixels` row: it needs part 1's export.
- The part-1 prototype may have changed since I copied its build (sha1 of its `fcmp-ui.wasm` starts 319bec42eb34).