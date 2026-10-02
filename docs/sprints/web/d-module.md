# Scout report: W-U part 1, the editor module (`fcmp-ui`) and its link to the audio thread

`FC` = /Users/seanfunk/audio/plugins/FCompressor (web/sprint-c, 9746e6d). `FG` = /Users/seanfunk/audio/libraries/FunkGui (web/sprint-c, bc072e4; **no `v0.13.0` tag exists yet**, `git tag` shows only v0.12.0). `S` = /private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-d/ui.

**What I ran, all under `S`** (both repositories still show an empty `git status --short`):
- Prototypes `S/src/web/ui/{PortLink.h,PortLink.cpp,WebMain.cpp}`, compiled with FC's warning list and `-Werror`.
- A node test: `S/porttest.cpp` + `S/run-porttest.mjs`, WebFacade + PortLink (wasm) over a real `MessageChannel` to a copy of `fcmp-engine.wasm`.
- `S/heap.mjs`: the engine's fixed memory across 360 reconfigures at four sample rates.
- A hand link of the whole browser module from copies of build-web's archives plus FunkGui's `src/web/*.cpp`, then five recompiles of all 104 translation units with other flags (`S/variant.sh`, `-j3`, outputs in `S`).
- The linked module with a real AudioWorklet in headless Chrome 154 (`--mute-audio`, gain 0), driven by FunkGui's `check-page.mjs` from `FC/build-web/_deps/funkgui-src/tools/web`; one screenshot (`S/settings.png`).
- A patched lint (`S/lint/LintDeps.cmake`, diff in `S/lint/lint.diff`) on the real tree and on a fake tree.
- Side effects outside `S`: small `js_output/` and `symbol_lists/` entries in Emscripten's shared cache (no system library was generated). One headless Chrome started with `--screenshot` did not exit on its own; I killed it and its server.

## 1. Facts

**(a) What WebMain must do**
1. The native editor's configuration (FC/Source/editor/gpu/Editor.cpp:52-64):
   - Size 960 × 640.
   - `setUiAttached` forwards to the facade (:58).
   - Zoom steps are `layout::footer::kZoomSteps` = 100/125/150/175, default 125 (FC/Source/editor/Layout.h:573-574); pref key `"uiZoom"` (Editor.cpp:17).
   - `beginBatch`/`endBatch` stay **empty**: the Panel's HostProxy already brackets the facade (FC/Source/editor/Panel.cpp:194-203; Editor.h:24-25).
2. Initial view: the Panel restores `facade.uiState()` (Panel.cpp:229-232; WebFacade's default is PANEL). An optional view is set with `setView(*v, true)` before the first tick (Editor.cpp:69-70).
3. `setRenderInfo` (Editor.cpp:80-92) feeds only the DISPLAY row (FC/Source/editor/views/Settings.cpp:481-504):
   - `gpu` false prints `HEADLESS`.
   - Otherwise it prints `<renderer> · <zoom> % ZOOM · <scale>× BACKING · <fps> FPS`, plus `· n DROPPED` when `overflows > 0`.
   - `displayLinked` and `frames` are not printed.
   - `renderer` is a literal (FC/Source/editor/SubView.h:176-178).
4. Teardown order (Editor.cpp:104-110): `setRenderInfo({})`, `Panel::shutdown()`, then the host. After `shutdown()` `Panel::tick` returns at once (Panel.cpp:456-457). `PanelOptions` are at FC/Source/editor/Panel.h:84-89; `Panel` is `final` (:91).
5. `funkgui::WebHost` (FG/include/funkgui/web/WebHost.h, FG/src/web/WebHost.cpp):
   - Config (:89-107): `canvasSelector`, `zoomSteps`, `defaultZoomPercent`, `zoomPrefKey`, `fitMarginX/Y`, `setUiAttached`, `beginBatch/endBatch`, `capture` pins as values (fixedDt, uiTheme, uiZoom, uiScale).
   - The constructor never throws, attaches the Panel and calls `setUiAttached(true)` (WebHost.cpp:210-240). `ok()`/`error()` report a missing canvas or WebGL2.
   - `start()`/`stop()` (:284-299); `frame(nowMs)` is public for tests (:362).
   - `Diagnostics{frames, lost, restores, fps, scale, zoomPercent, physW, physH}` (WebHost.h:147-157).
   - The destructor stops the clock, removes listeners, closes gestures and calls `setUiAttached(false)` (:242-253).
   - `services()` = menus | clipboard, no chooser (:695-698). No file drops (WebHost.h:42-44).
6. **WebHost has no per-frame hook.** `frame()` goes straight from the hidden check (WebHost.cpp:387-392) to `panel_.tick` (:398-399). The clock, its cadence and `nudgeFullRate` (:661-670) are private. The options are in Q1.
7. WebFacade's own calls (FC/Source/web/facade/WebFacade.h:92-103):
   - `pull()`: one outstanding Pull; re-posted after `kPullPatience` = 30 calls (:115; WebFacade.cpp:187-197).
   - `resync()`: clears the open Pull, posts Params with snap, and Attach when attached (WebFacade.cpp:248-257).
   - `resetEngine()`; `setEngineSetup(rate, maxBlock)`.
   - `setEnvironment(format, host)`: `format` must be a static string, `host` is copied (48 chars).
   - The constructor posts nothing (WebFacade.cpp:74-80).
8. `funkgui::installLocalStoragePrefs(nullptr)` must run before the Panel is built (FG/include/funkgui/web/WebPrefs.h:5-6; Panel.cpp:220-221 reads preferences in its constructor). The key prefix is `FUNKGUI_PREFS_FOLDER` + "." = `"FCompressor."` (FG/src/web/WebPrefs.cpp:106-112; FC/cmake/FcmpSources.cmake:41).
9. Emscripten calls `Module.onRuntimeInitialized` **before** `main()` (emscripten 6.0.3 `src/postamble.js`:165 against :174-176).
10. [S] The prototype WebMain does all of the above. In Chrome 154: WebGL2 ok, 60 fps, all six views (`?view=` panel, chars.sidechain, chars.colour, modebrowser, presetbrowser, settings) run with `-sSTACK_OVERFLOW_CHECK=2` at the default 64 KB stack with no abort.

**(b) PortLink**
11. EngineLink contract (FC/Source/web/facade/EngineLink.h:7-15):
   - `post` never blocks, has no result, and reads the bytes before returning.
   - A link that is not connected looks like a refused record.
   - Replies come on the facade's thread, in order, at most one per Pull, valid only during the call.
12. Record sizes (FC/Source/web/engine/WebProtocol.h): Header 16 (:58-65), Params 140 (:69-74), Attach 20, Reset 16, Pull 16. A reply is 320 + 32 × columns, at most 512 columns = 16704 = `sizeof(Reply)` (:102-118). `kMaxMessageBytes` = 140 (:121).
13. The protocol already says the reply returns in the Pull's buffer (:18) and that a reply is copied as its first `h.bytes` bytes (:22-23). The facade checks `h.bytes == replyBytes(count) && h.bytes <= record.size()` (WebFacade.cpp:217), so a link may hand it the whole 16704-byte buffer.
14. [S] A transferable round trip works with the protocol as it is:
   - The page keeps one 16704-byte `ArrayBuffer` (the "carrier"). A Pull is written into its first 16 bytes and transferred; the worklet copies the reply into the same buffer and transfers it back.
   - 60 pulls made exactly 1 carrier, under node and in Chrome (`carriersMade 1`, 88 replies in 1.5 s, 0 refused).
   - Per message there are still small wrapper objects (the event, the ArrayBuffer object, one or two typed-array views); MessagePort offers no way around that. No backing store is allocated per frame.
15. [S] Posting a view on the wasm heap without a slice clones the **whole memory**: a 16-byte `subarray` arrived over a 16,777,216-byte buffer.
16. Re-entrancy: replies arrive in a `message` task, never inside a facade call (no Asyncify). `WebFacade::reply` posts nothing and copies everything out (WebFacade.cpp:199-236).
17. [S] Before a port exists, Attach, Pull and Params are dropped (3 dropped, nothing thrown). `connect` then calls `resync()`: Params with snap plus Attach, both accepted by the engine.
18. [S] Reconnect: a late reply from the old port is ignored (port identity check); the new engine has the facade's setup after `resync` (latency 1021 on both sides).
19. [S] A worklet that stops answering: 61 `pull()` calls made 2 new carriers (one per 30 calls). Extras that come back later are dropped.
20. [S] Chrome services the worklet's port while the AudioContext is suspended (replies 88 → 130 in 0.7 s suspended). The frames stop moving, so the Panel goes stale after 0.5 s (Panel.cpp:411-424).

**(c) The engine in the worklet**
21. Instantiation: `new WebAssembly.Module(bytes)`, `new WebAssembly.Instance(module, {})`, `_initialize()` (FC/web/tests/engine.mjs:77, :98-99). Exports are the 10 `fcmp_web_*` functions plus memory, malloc, free.
22. [S] In Chrome's AudioWorkletGlobalScope, with the bytes in `processorOptions`:
   - Synchronous compile, instantiate, create, configure and self-check took 46 ms in the processor's constructor.
   - The self-check hash was `5a96ce217d29ca6f`, the native constant (engine.mjs:23).
   - The scope has `WebAssembly`, `console`, `sampleRate`, `currentFrame`. It lacks `performance`, `fetch`, `TextDecoder`, `setTimeout`, `crypto`.
   - `export` in the worklet module file is accepted, so node can import the same file.
   - `AudioContext({sampleRate: 48000})` gave 48000.
23. Per quantum: `fcmp_web_process(e, inL, inR, outL, outR, frames)` with pointers into the module's memory.
   - `inR == 0` is mono; `inL == 0` is silence (FC/Source/web/engine/WebEngine.cpp:497-514).
   - Any frame count is accepted and split by the configured cap (:501-503).
   - Before configure the output is silence (:490-496). Denormal inputs are zeroed (:142-155).
24. Memory: `-sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=16777216` (FC/cmake/FcmpWeb.cmake:93-96), so typed-array views made once stay valid. [S] About 15.4 of 16 MiB are free after configure; over 360 Params records cycling quality × budget × Mode at 44.1/48/96/192 kHz, 0 were refused and the least free was 14.94 MiB.
25. [S] A mono oscillator arrives as `inputs[0].length == 1`; after `disconnect()` it is 0. Frames were always 128. With no input the gate closes (reply flags 0x3c, `kReplyGated`; WebEngine.cpp:526-535) and frames stop publishing.
26. [S] Cost in the message handler under node on this Mac:
   - A Pull: median 0.004 ms, max 0.05 ms.
   - A Params record that reconfigures (STD → HQ, lookahead budget on): 0.12 ms cold, at most 0.34 ms warm.
   - One quantum is 2.67 ms. The engine restarts snapped and cleared (WebEngine.cpp:180-191), so a discontinuity is inherent.

**(d) Build**
27. `FUNKGUI_BROWSER` matters only to FunkGui's own executables (FG/cmake/FunkGuiTargets.cmake:214-226). A consumer never passes through it.
28. FunkGui's pages link with `FunkGui::core FunkGui::web`, `-sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1`, `funkgui_configure_product`, no MODULARIZE and no export list (FG/test/web/CMakeLists.txt:19-25; FG/tools/GalleryWeb/CMakeLists.txt:18-23). `FunkGui::web` adds the WebGL2 link options and depends on the shader text (FunkGuiTargets.cmake:569-577).
29. The atlas (528,908 bytes), the TTF (11,004) and the licence (4,399) are embedded by `FunkGuiFonts`; nothing is fetched at run time.
30. [S] Sizes of the hand-linked module (prototype WebMain; all 104 translation units recompiled per row except the first, which uses build-web's own objects):

| flags | fcmp-ui.wasm | gzip -9 | fcmp-ui.js |
|---|---|---|---|
| -O3 (what build-web compiles today) | 1,318,250 | 440,729 | 52,833 (gzip 15,109) |
| -O2 | 1,194,173 | 431,732 | 54,239 |
| -Os | 909,348 | 358,248 | 52,722 |
| -O2 -flto | 1,268,339 | 452,031 | 54,895 |
| -Os -flto | 891,185 | 350,400 | 53,310 |
| -O3 -flto | 1,218,372 | 442,568 | 53,421 |

   - About 544 KB of each is the font data. The `-Os -flto` module also ran in Chrome at 60 fps.
   - The engine is 545,704 bytes (gzip 134,692).
31. [S] A target-level `-Os` does **not** win: CMake puts it before the linked interface's `-O3` (`-O3 -DNDEBUG -Os -O3 …` in a scratch project shaped like `fcdsp → fcmp_flags`). Per-source `COMPILE_OPTIONS` come last.

**(e) Tests**
32. [S] FunkGui's runner works unchanged from the fetched sources: `node build-web/_deps/funkgui-src/tools/web/check-page.mjs <dir> --page fcmp-ui --chrome-flag …`. Its rules:
   - `<stem>.html` beside `<stem>.js` and `.wasm` (check-page.mjs:68-73).
   - The verdict is `document.title` (`PASS` / `FAIL…`), the log is element `funkgui-log` (:215-216).
   - No query string can be passed (:153).
   - Any `--chrome-flag` replaces the default GPU flags (:82-84).

**(f) Settings in the browser**
33. [S] `S/settings.png`, taken before audio starts, shows:
   - VERSION `FCOMPRESSOR 1.1.0`; LIBRARIES `FUNKGUI 0.13.0 · JUCE –`; FORMAT `WEB · GOOGLE CHROME 154`.
   - SAMPLE RATE `THE HOST HAS NOT STARTED THE AUDIO`; BLOCK SIZE and CHANNELS `–`.
   - DSP LOAD `–`; OVERRUNS `0 OF 0 BLOCKS`; PRESETS `76 FACTORY · 0 USER`.
   - DISPLAY `WEBGL2 · 100 % ZOOM · 1× BACKING · 0 FPS` (an early frame).
   - AUDIO reads `RUNNING` for the first 0.5 s (the zero frame counts as fresh, as in the plugin), then `STOPPED`.
34. Once a reply carries `kReplyConfigured` (WebFacade.cpp:316-321), the code prints `48000 HZ`, `UP TO 128 SAMPLES`, `2 IN · 2 OUT · NO KEY INPUT` (Settings.cpp:349-389; read from code, not captured).
35. IMPORT and EXPORT are disabled: `hasChooser` is false (FC/Source/editor/views/PresetBrowser.cpp:218-221) and WebPresets refuses files (FC/Source/web/facade/WebPresets.h:34-35).
36. The new-instance QUALITY and LOOKAHEAD rows write `newQuality`/`newLookahead` through UiPreferences (Settings.cpp:257-259), so they land in localStorage as `FCompressor.newQuality` and `FCompressor.newLookahead`. Nothing reads them: the processor's reader is JUCE code (FC/Source/plugin/Processor.cpp:149-158), and the facade may not include UiPreferences (lint `web.facade`). See Q3.

## 2. What the card must do

All three files exist as working prototypes in `S/src/web/ui/`; the order below keeps every configuration green (native trees only lint these files).

1. **`Source/web/ui/PortLink.h`** (the seam; lead-frozen if the card is split):
   ```cpp
   class PortLink final : public fcmp::web::EngineLink {
   public:
       struct Events { virtual void connected(double sampleRate, int maxBlock) = 0;
                       virtual void disconnected() = 0; protected: ~Events() = default; };
       struct Counters { std::uint32_t posted = 0, dropped = 0, delivered = 0; };
       explicit PortLink(Events* = nullptr);            // installs Module.fcmpPort
       ~PortLink() override;
       void post(std::span<const std::uint8_t>) override;
       void setSink(ReplySink*) override;
       bool connected() const noexcept;
       const Counters& counters() const noexcept;
   };
   ```
2. **`Source/web/ui/PortLink.cpp`**: three EM_JS functions, no `EMSCRIPTEN_KEEPALIVE`, no `EXPORTED_FUNCTIONS`. JavaScript calls C++ through function pointers and `getWasmTableEntry`, as WebHost.cpp:26-28 does.
   - `void fcmp_port_install(void* link, unsigned char* inbox, int capacity, void (*onRecord)(void*, int), void (*onConnected)(void*, double, int), void (*onDisconnected)(void*))`
     - Creates `Module.fcmpPort = { port, carrier, capacity, carriersMade, connect, disconnect }`.
     - `connect(port, sampleRate, maxBlock)` disconnects any old port, sets `port.onmessage`, then calls `onConnected`.
     - The handler ignores a message unless `state.port === port` and `event.data instanceof ArrayBuffer`. It copies `min(byteLength, capacity)` bytes to `inbox` with `HEAPU8.set(new Uint8Array(data, 0, n), inbox)`, keeps a buffer of exactly `capacity` bytes as the next carrier when it has none, and calls `onRecord(link, n)`.
   - `void fcmp_port_remove(void)`.
   - `int fcmp_port_post(const unsigned char* bytes, int n, int carrier)`: returns 0 with no port.
     - `carrier != 0`: take `state.carrier` (or `new ArrayBuffer(capacity)`), write the record at its start, transfer it.
     - Otherwise: `const b = HEAPU8.slice(bytes, bytes + n).buffer; port.postMessage(b, [b])`.
   - C++ side: `inbox_` is `sizeof(Reply)` bytes at a stable address. `post()` reads only `Header::kind` to route a Pull through the carrier (`web/engine/WebProtocol.h`, never `WebEngine.h`). `onRecord` gives the sink `{inbox, n}`.
3. **Node test of PortLink** (web only): a small module built from PortLink.cpp + facade + portable over fcdsp with `-sENVIRONMENT=node -sMODULARIZE=1 -sEXPORT_ES6=1`, and a `web/tests/port.mjs` in the shape of `S/run-porttest.mjs`. It needs prototypes for its exported test hooks (trap 3).
4. **`Source/web/ui/WebMain.cpp`**, `main()` in this order:
   1. `funkgui::installLocalStoragePrefs(nullptr)`.
   2. `PortLink link{events}; WebFacade facade{link};`. `Events::connected` calls `facade.setEngineSetup(rate, maxBlock); facade.resync();`.
   3. `facade.setEnvironment("WEB", <browser name from an EM_JS>)`.
   4. If Q3 is yes: for `kPrefNewQuality`/`kPrefNewLookahead`, `v = UiPreferences::get().getInt(key, -1, -1, 2)`; when `v >= 0`, `facade.port(pid).setValue01(v / 2.0f)`.
   5. `Panel(facade, {.syncPreview = true})`; optional `?view=<id>` through `findView` and `setView(*v, true)`.
   6. `WebHostConfig`: `canvasSelector` (agree the id with the page; the prototype uses `#fcmp-canvas`), the zoom steps, default and key of fact 1, `fitMarginX/Y` measured from the canvas's position (as FG/tools/GalleryWeb/GalleryWeb.cpp:131-141), `setUiAttached = facade.setUiAttached`, batches empty, capture pins from `?theme`, `?zoom`, `?dt`, `?scale`.
   7. `WebHost host(panel, config)`; the pull per Q1.
   8. `panel.setRenderInfo(...)`: `gpu = host.ok()`, `displayLinked = host.running()`, `fps`, `scale`, `zoomPercent`, `frames` from `host.diagnostics()`, `overflows = 0` (or `lost`), `renderer = "WEBGL2"`.
   9. `host.start()`.
   10. Last, an EM_JS sets `Module.fcmpUi = { resetEngine(), shutdown(), status() }` and calls `Module.fcmpReady?.()`.
   - Shutdown: `setRenderInfo({})`, `panel.shutdown()`, destroy the host, then the Panel, the facade, the link.
5. **The port's wire rule** (the worklet agent writes against this; `S/site/fcmp-worklet.js` is a working 70-line version):
   - A message whose `data` is an `ArrayBuffer` is one record. Anything else is private to the page and the worklet; the page listens with `addEventListener`, never `onmessage`.
   - Page → worklet: a Pull travels in the 16704-byte carrier; every other record in a buffer of exactly its size. Both are transferred.
   - Worklet: copy `min(byteLength, 140)` bytes to a malloc'd inbox; `n = min(u32 at offset 8, copied)` (`Header::bytes`, the one field a script reads); `r = fcmp_web_post(e, inbox, n)`. When `r > 0`, copy `r` bytes from `fcmp_web_reply(e)` into the same buffer (a new one only if it is smaller) and transfer it back. When `r <= 0`, send nothing.
   - Worklet set-up: `create`, `configure(sampleRate, 128)`, four persistent 128-frame `Float32Array` views, `process` with `set()` only (no allocation per quantum), the frame count from `outputs[0][0].length`.

**What the lead must land first**
1. `cmake/FcmpSources.cmake`: `file(GLOB FCMP_WEB_UI_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/web/ui/*.cpp)` (flat).
2. `cmake/FcmpWeb.cmake`, web only (I hand-linked this set; I did not run it as a CMake target):
   ```cmake
   if(FCOMPRESSOR_WEB AND FCMP_WEB_UI_SOURCES)
     set(_fcmp_web_ui_own ${FCMP_EDITOR_SOURCES} ${FCMP_PLUGIN_PORTABLE_SOURCES} ${FCMP_WEB_FACADE_SOURCES} ${FCMP_WEB_UI_SOURCES})
     add_executable(fcmp_web_ui ${_fcmp_web_ui_own})
     set_target_properties(fcmp_web_ui PROPERTIES OUTPUT_NAME fcmp-ui)
     fcmp_warn_sources(${_fcmp_web_ui_own})
     target_include_directories(fcmp_web_ui PRIVATE ${FCMP_SOURCE_ROOT} ${FCMP_GENERATED_DIR})
     target_link_libraries(fcmp_web_ui PRIVATE fcdsp FunkGui::core FunkGui::web)
     funkgui_configure_product(fcmp_web_ui PRODUCT ${FCMP_PRODUCT_NAME} OBJC_PREFIX ${FCMP_OBJC_PREFIX}
                               ENV_PREFIX ${FCMP_ENV_PREFIX} PREFS_FOLDER ${FCMP_PREFS_FOLDER})
     target_link_options(fcmp_web_ui PRIVATE -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
   endif()
   ```
   - Add `fcmp_web_ui` to the `fcmp_web` list (:131).
   - Add the node target for step 3 above, with its C++ outside the flat `Tools/web/*.cpp` glob (that glob also builds natively, where PortLink cannot compile).
3. `cmake/LintDeps.cmake`, tested: 0 violations on the real tree; every clause fires on a fake tree; the prototypes and `PreviewWorker.cpp:101`'s `__EMSCRIPTEN__` pass.
   - Flags: `set(_in_web_ui FALSE)` and `elseif(_rel MATCHES "^web/ui/") set(_in_web_ui TRUE)`.
   - After the `web.juce` check:
   ```cmake
   if(NOT _in_web_ui AND (_inc MATCHES "(^|/)emscripten(/|\\.h$)" OR _inc MATCHES "^emscripten"))
     _lint_fail("${_f}" ${_n} web.emscripten "an Emscripten header is included only under Source/web/ui: ${_code}")
   endif()
   if(_in_web_ui)
     if(_inc MATCHES "(^|/)web/engine/" AND NOT _inc MATCHES "(^|/)web/engine/WebProtocol\\.h$")
       _lint_fail("${_f}" ${_n} web.ui "Source/web/ui shares only web/engine/WebProtocol.h with the engine (WebEngine.h's functions carry export_name): ${_code}")
     endif()
     if(_inc MATCHES "(^|/)plugin/" AND NOT _inc MATCHES "(^|/)plugin/(portable/[^/]+|ProcessorFacade\\.h)$")
       _lint_fail("${_f}" ${_n} web.ui "Source/web/ui includes only plugin/portable/* and plugin/ProcessorFacade.h from plugin/: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)editor/gpu/" OR _inc MATCHES "(^|/)funkgui/(gpu|juce|presets)/")
       _lint_fail("${_f}" ${_n} web.ui "Source/web/ui includes nothing of the GPU editor or of FunkGui's JUCE parts: ${_code}")
     endif()
     if(_inc MATCHES "(^|/)fcdsp/engine/EngineHost\\.h$" OR _code MATCHES "(^|[^A-Za-z0-9_])EngineHost([^A-Za-z0-9_]|$)")
       _lint_fail("${_f}" ${_n} web.ui "Source/web/ui reaches the engine only through the port, never fcdsp::EngineHost: ${_code}")
     endif()
   endif()
   ```
   - Add both rules to the header comment (:36-48).
4. If Q1 is answered with the FunkGui hook: add it before tagging v0.13.0 and bump the pin (FC/cmake/FcmpDeps.cmake:31-33).
5. The seam, frozen in the manifest: `PortLink.h` above, the wire rule of step 5, and the page-facing names (`Module.fcmpReady`, `Module.fcmpPort.connect/disconnect`, `Module.fcmpUi`, the canvas id, the query parameters).

## 3. Traps

1. **`EM_JS(...);` fails under FC's warning list**: `-Wextra-semi` rejects the trailing semicolon [S]. FunkGui's sources write it; ours must not (same for `EM_JS_DEPS`).
2. **An EM_JS body is a C string**: a regex literal with backslashes is an "unknown escape sequence" error [S]; a lone apostrophe starts a character constant (WebHost.cpp:50-51). Use `new RegExp("…")` and no comments.
3. **`EMSCRIPTEN_KEEPALIVE` functions need a prior declaration** (`-Wmissing-prototypes`) [S]. Function pointers avoid both that and an export list.
4. **`onRuntimeInitialized` fires before `main()`** (fact 9): the page must wait for `Module.fcmpReady`, which `main()` calls last.
5. **Never post a heap view** (fact 15); slice first. A `WebAssembly.Memory` buffer cannot be transferred either.
6. **`port.onmessage` belongs to PortLink.** A page that assigns it replaces the link.
7. **Including `web/engine/WebEngine.h` in the editor module** exports the engine's ABI from it and keeps the engine alive (WebEngine.h:26-30). Hence lint `web.ui`.
8. **`Panel` is `final` and WebHost has no hook** (facts 4, 6). Driving `frame()` from WebMain's own requestAnimationFrame loses the 60/12 Hz cadence, the 10 Hz idle and the nudge on input.
9. **Do not fill `WebHostConfig::beginBatch/endBatch`**: the Panel already brackets the facade, so a second bracket only nests (fact 1).
10. **A late reply from a replaced worklet** would push another engine's columns into the mirror; the handler must check the port's identity (fact 18).
11. **`pagehide` with `persisted == true`** (back/forward cache) followed by a return shows a shut-down Panel that no longer ticks. Shut down only when `!event.persisted`, or not at all on unload.
12. **Worklet views stay valid only while the engine's memory cannot grow** (fact 24). If the link option changes, every view must be remade after each call.
13. **`inputs[0]` has 0 or 1 channels at times** (fact 25); pass null pointers rather than reading `inputs[0][1]`.
14. **A refused or failed post is invisible to the facade** (`post` has no result; `kPostFailed`, WebEngine.h:100). The worklet should count refusals for the self-test.
15. **`FunkGui::core` is INTERFACE sources**: use `fcmp_warn_sources`, never `fcmp_warnings` target-wide, and always call `funkgui_configure_product` (FG/include/funkgui/core/Config.h:18-21).
16. **A target-level `-Os` is silently overridden** (fact 31).
17. **Headless Chrome with `--screenshot` does not exit** on a page that holds an AudioContext [S]. Use the DevTools runner.
18. **`check-page.mjs` takes no query string**: a test page must set its own mode. The prototype's view pages call `history.replaceState(null, '', '?view=…')` before the module loads.
19. **`-sENVIRONMENT=web` output cannot run under node**: the node test of PortLink is its own small module.
20. **With no input the editor looks stopped** (fact 25): the gate stops the frames. This belongs on the page's "what differs" list.

## 4. How to verify

**Existing coverage**
- `proc.webnull`, `proc.webpresets`, `ui.web` hold the facade natively; `ui.web` already runs "quanta, `pull()`, tick" per frame (FC/Tools/probes/plugin/ui_web.cpp:6, :117).
- `web.engine.abi` instantiates the standalone module as the worklet does.
- `fcmp_web_editor_check` compiles the editor as wasm32.

**New, under node** (label `verify;web`, judged by exit code)
- `web.ui.port`: the rows of `S/run-porttest.mjs`. Dropped before connect; resync on connect; one carrier for 60 pulls; flags and telemetry; a reconfigure through the port; a late reply ignored; the patience rule; the clone trap. It runs in 0.24 s.
- If the worklet file exports its engine driver (guarded `registerProcessor`), the same test imports the shipped `web/fcmp-worklet.js` [S: done].
- `lint.deps` with `web.emscripten` and `web.ui`, in every native tree.

**In headless Chrome** (lead phase, or by hand in this sprint)
- A smoke page run by FunkGui's runner: `--page fcmp-ui --chrome-flag --use-angle=metal --chrome-flag --autoplay-policy=no-user-gesture-required --chrome-flag --mute-audio`.
- It proves: WebGL2 ok, frames drawn, replies ≥ frames − 2, 0 refused, 1 carrier, the worklet's self-check hash, the gated flag after the source stops.
- `S/site/fcmp-ui.html` is such a page and passed six times.

**Cannot be automated in this sprint**
- Real pointer, wheel and key input on the canvas; the DOM menus; the clipboard.
- Safari and Firefox.
- An audible click on a QUALITY change.
- Whether 12 Hz idle pulls feel right.

## 5. Open questions for the lead

**Q1. Where does the per-frame pull go?**
- (a) FunkGui: `std::function<void()> beforeTick` in `WebHostConfig`, called before `panel_.tick` (WebHost.cpp:398). Exact, three lines plus a row in `test/web/host.cpp`; possible inside 0.13.0 because the tag is not made.
- (b) FCompressor only: a forwarding `funkgui::Panel` in `Source/web/ui` whose `tick` calls `facade.pull()` first. The prototype does this [S: 88 replies for about 90 frames]. A future defaulted virtual of `funkgui::Panel` would not be forwarded.
- (c) The page's own requestAnimationFrame calls `pull()`: more pulls than drawn frames, and it defeats the idle cadence.
- I recommend (a) if you touch FunkGui before the tag anyway, else (b). WebMain differs by a few lines either way.

**Q2. Does `PortLink::post` read `Header::kind`?** EngineLink.h:3 says a link "reads no field", but routing the Pull through the carrier needs it (LoopbackLink already reads the header for its log, FC/Tools/probes/plugin/LoopbackLink.cpp:44-71). The alternative is a size rule, which Reset (also 16 bytes) breaks. I recommend allowing `kind` in C++ and `bytes` in the worklet's script, and saying so in EngineLink.h and WebProtocol.h:5.

**Q3. Do the new-instance QUALITY and LOOKAHEAD preferences apply on page load?** Yes costs four lines in WebMain (step 4.4) and makes the settings rows true across reloads. No leaves two rows that do nothing. I recommend yes. The captions ("IN EVERY HOST", "THE HOST HAS NOT STARTED THE AUDIO", "WHAT THE HOST COMPENSATES") stay as they are either way, since changing them moves `ui.settings` rows.

**Q4. Optimisation level and the `web.size` budget.** -O3 gives 1.32 MB (gzip 441 KB); -Os gives 0.91 MB (gzip 358 KB) but needs per-source options (fact 31). LTO gains about 2 %. I recommend -O3 with no special flags and a budget of about 1.45 MB for the wasm.

**Q5. Stack size.** 64 KB passed all six views idle; I recommend `-sSTACK_SIZE=1048576`, because drags, menus and preview requests were not exercised.

**Q6. `RenderInfo::overflows`.** 0, or WebHost's `lost` (it would print "n DROPPED" after a context loss). I recommend 0.

**Q7. Who configures the engine first?** The worklet's constructor at the defaults, as in the prototype (a visitor who chose HQ before Start pays one reconfigure of about 0.1 ms), or the first Params record. I recommend the constructor.

## 6. Can it be split?

Yes, into two file-disjoint agents behind the seam of section 2 (lead step 5):
- **U-1** owns `Source/web/ui/*` and the node port test. It needs only a 20-line fake worklet of its own, or the frozen driver.
- **U-2** owns `web/{index.html,main.js,fcmp-worklet.js,demo.css}` and its tests. It needs only `Module.fcmpReady`, `Module.fcmpPort.connect/disconnect`, `Module.fcmpUi` and the wire rule.

The lead writes and freezes `PortLink.h`, the wire rule and the names first. With W-N that makes three agents. As one card it is also feasible (about 350 lines of C++ and 600 of JavaScript); the split buys time, not safety.

## Not checked

- Safari and Firefox entirely: the worklet's scope, messages while suspended, `export` in a worklet module, a `WebAssembly.Module` in `processorOptions`.
- Real input in the browser; only `S/settings.png` was looked at, so the other five views ran without anyone seeing their pixels.
- The CMake target itself (the module was linked by hand) and FunkGui's link options on a node executable.
- Audible output (Chrome was muted) and HQ load in a real worklet.
- The cost of a synchronous preview request (W-N's measurement); CHARACTERISTICS only idled at 60 fps.
- Context loss, a return from a hidden tab, a render quantum other than 128, a device pixel ratio other than 1.
- The Linux CI runner (SwiftShader) for the Chrome page.
- Whether -Os moves any `ui.*` row under node.