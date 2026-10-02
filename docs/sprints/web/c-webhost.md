# Scout report: card G-D, FunkGui `WebHost`

Paths: `FG` = `/Users/seanfunk/audio/libraries/FunkGui` (branch `web/sprint-b`, f2641a9), `E:` = `FG/src/gpu/EditorHost.cpp`, `P:` = `FG/src/gpu/FramePump.cpp`, `EM` = `/opt/homebrew/Cellar/emscripten/6.0.3/libexec`, `JUCE` = `~/audio/.deps/JUCE-8.0.4/modules/juce_gui_basics`.

Scratch run: `<scratchpad>/scout-c/webhost/{probe.cpp,index.html,run.mjs}`. I built it with `em++ -std=c++20 -O1 probe.cpp -o probe.js -sENVIRONMENT=web -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2` and ran `node run.mjs` (headless Chrome over DevTools, at device scale 1 and 1.25). Facts marked **[S]** come from that run. Side effect: the em++ link generated `libGL-webgl2-getprocaddr.a` in Emscripten's own cache under `EM/cache`. No repository was touched.

## 1. Facts

### (a) Frame order (`EditorHost::submitFrame`, E:449-581)
1. **Prefs and zoom (E:457-468).** If `UiPreferences::revision()` moved: `applyTheme(prefs.theme())` unless `capture_.uiTheme >= 0`, then `followZoomPreference()`. Then `if (zoomTarget_ != zoomApplied_) applyZoom()`. This runs even when hidden.
2. **Hidden gate (E:472).** `if (!isShowing()) return` with nothing submitted and `wantsFullRate=false`. The Panel is not ticked while hidden.
3. **Surface attach, retry, fallback (E:476-505).** Native only. The web analogue is `WebGlSink::ok()/lost()`.
4. **Backing scale (E:508).** `refreshDrawableScale()` runs every frame.
5. **First frame only (E:513-520).** `UI_KEYS` replay, then the a11y sync.
6. **Tick (E:523-526).** `tickDt = fixedDt > 0 ? fixedDt : dt`; `panel_->tick(tickDt)`; `seconds_ += tickDt`; `++ticks_`.
7. **Record (E:529-552).** FrameInfo carries:
   - `logicalW/H` = the Panel's size; `dpi = physH_ / logicalH_`, guarded so that `!(dpi > 0.05)` becomes 1 (E:535-538);
   - `clear` = `theme_.ground`, plus `textGamma` and `theme`;
   - `seconds` = `seconds_` (after the tick), `frame` = `ticks_`, `dt` = `tickDt`;
   - `fixedClock` = `fixedDt > 0`; `displayLinked` and `fps` from the pump (fps 0 when fixed); `fullRate` = `pump.wantedFullRate()`, which is the previous request, not this frame's; `overflows`.
   - Then `canvas_.begin(info)`, `panel_->draw(canvas_, theme_)`, `canvas_.end()`.
8. **Capture dump (E:555-561).** Once.
9. **Submit (E:565-569).** `++diag_.frames`.
10. **A11y (E:573-576).** On first frame, on a revision change, or every ≥ 0.1 s.
11. **Rate (E:579).** `result.wantsFullRate = panel_->wantsFullRate()`.

Clock (FramePump):
- The first dt is 1/60 (no stamp yet); otherwise it is the stamp difference, clamped to [1 ms, 100 ms] (P:152-159).
- fps is measured over windows of at least 1 s (P:161-167).
- After the frame, `requestRate(fullRate)` (P:193).
- The stamp is dropped only when the clock source changes or the clients empty (P:64, P:85).

HeadlessHost's order, for parity (`FG/src/panel/HeadlessHost.cpp`):
- `tick` = `Panel::tick(dt)`, `now += dt`, `++frame`, `Panel::idle(now)` (:210-220).
- `draw` uses `seconds = now_`, `fixedClock = true`, and `fullRate = panel.wantsFullRate()` at draw time (:235-252).

### (b) Cadence
- Constants: `kIdleHz = 12`, `kFullHz = 60` (`FG/include/funkgui/gpu/FramePump.h:102-103`).
- Display link: full = min 30 / max 120 / preferred 60; idle = 8 / 12 / 12 (P:108-109). Fallback timer: 60 or 12 Hz (P:93-95, P:113).
- `nudgeFullRate()` = `requestRate(true)` at once (P:127-130). It lasts until the next tick's `requestRate(result)`.
- EditorHost also runs a separate 10 Hz `juce::Timer` (E:93, E:210) that calls `panel_->idle(nowSeconds())` (E:624-627). It runs whether or not frames do.
- `nowSeconds()` is the wall clock, or `seconds_` under `fixedDt` (E:897-900).
- **[S]** `emscripten_request_animation_frame_loop` kept firing at about 60 Hz in headless Chrome after `main()` returned (no `EXIT_RUNTIME`), and after the GL context was destroyed.

### (c) Sizing
- **Logical size.** Fixed, from the Panel (E:188-192).
- **Zoom steps.** Kept only in 25..400, sorted, unique; the default becomes the nearest listed step (E:992-1010).
- **Zoom state.**
  - `zoomPin_ = uiZoom`, or 100 under `CANVAS_DUMP` (E:1012).
  - The preference is read with `getInt` and taken only if it is a listed step (E:1017-1026).
  - The target is the pin, else 100 with no steps, else `fitZoom(chosen)` (E:1039-1047).
- **Fit (E:1066-1080).** The largest step ≤ chosen with `zoomedSize(W,s) <= area.w && zoomedSize(H,s) <= area.h`; none fits gives the smallest step; an empty area means no fit.
- **Zoomed size.** `lround(logical * percent / 100)` (E:1050-1053).
- **When the fit is evaluated.** Only when chosen, read, or on a new peer (E:52-63). It is not continuous.
- **`setZoomPercent` (E:969-978).** Ignores unlisted values; writes the preference when `zoomPrefKey` is set; re-targets; nudges.
- **`zoomFits` (E:980-985).** A listed step and (pinned or `fitZoom(p) == p`).
- **`applyZoom` (E:1091-1106).** Sets `zoomScale_ = applied / 100` and calls `setSize`.
- **Physical size.** `roundToInt(area * scale)` (E:288-289, 327-328, 410-411). A scale change is detected per frame with a 1e-6 threshold (E:318-333).
- **What CaptureConfig pins** (`FG/include/funkgui/panel/CaptureConfig.h:23-38`): `canvasDump` (fallback clock, no mouse, zoom 100: E:175-183), `uiTheme` (E:200, 462, 918), `uiScale`/`uiScaleAfter` (E:337-342), `uiKeys` (E:517), `fixedDt` (E:523, 545-547, 899), `a11yDump`, `gpuLog`, `uiZoom`.
- **`fromEnv()`** reads `funkgui::env` (`getenv`), which is empty in a browser.

### (d) Input
- **Pointer (E:635-644).** `x,y = e.position / zoomScale_` (float); `mods`; `clicks = getNumberOfClicks()`; `popup = mods.isPopupMenu()`. There is no button filter: a middle click arrives as a plain down.
- **Mods (E:100-108).** shift, cmd, alt, ctrl from JUCE. On macOS cmd = Command and ctrl = Control. Elsewhere `commandModifier == ctrlModifier`, so Ctrl sets both and Meta is unmapped (`JUCE/keyboard/juce_ModifierKeys.h:149-160`).
- **Popup.** macOS: right button or Ctrl (`testFlags` on `rightButton|ctrl`, ModifierKeys.h:153). Elsewhere: right button only (:160). HeadlessHost uses `popup = m.ctrl` on every platform (HeadlessHost.cpp:66).
- **Calls (E:659-703).**
  - move: nudge, `pointerMove`, cursor.
  - exit: `pointerExit`, cursor (no nudge).
  - down: nudge, `pointerDown`.
  - drag: nudge, `pointerDrag`.
  - up: `pointerUp`, then unbounded drag off (no nudge).
  - double click: nudge, `doubleClick`.
- **Double-click order differs between hosts.** Live JUCE sends `mouseUp` and then `mouseDoubleClick` for every release with clicks ≥ 2 (`JUCE/components/juce_Component.cpp:2267-2270`). HeadlessHost sends down(2), doubleClick(2), up(2) (HeadlessHost.cpp:269-278).
- **Click count** (`JUCE/detail/juce_MouseInputSourceImpl.h:405-427, 553-562`): previous downs within 400 ms (800 ms for the 3rd and 4th), under 8 px in x and y, same buttons, at most 4. The count is 1 once the press moved or was held more than 300 ms.
- **Wheel (E:705-723).** `x,y / zoom`; `dx,dy = wheel.deltaX/Y`; smooth, reversed, inertial; nudge; `used = panel_->wheel(w)`; unused events go to the parent.
- **Wheel units.**
  - macOS precise: `0.5/256 * scrollingDelta` (points), smooth (`JUCE/native/juce_NSViewComponentPeer_mac.mm:806-812`).
  - macOS coarse: `10/256 * delta` (:823-828).
  - X11: ±`50/256` per notch, not smooth (`JUCE/native/juce_XWindowSystem_linux.cpp:3618-3619`).
  - Positive dy = wheel up. DOM `deltaY` has the opposite sign.
- **Wheel consumers.**
  - RuleSlider: `v = (reversed ? -1 : 1) * (dy != 0 ? dy : dx)`. Not smooth: one detent per event and ×4. Smooth: accumulates per `kWheelNotch = 0.10` (`FG/src/widgets/RuleSlider.cpp:689-759`).
  - FCompressor lists: `px = -d * 512 / zoom` for smooth (`Source/editor/views/PresetBrowser.cpp:101, 1967`).
  - `inertial` is used at `Source/editor/Panel.cpp:649`.
- **Keys (E:725-770).** Table: tab, up, down, left, right, pageUp, pageDown, home, end, escape, return→enter, backspace, delete→del, space (`ch = ' '`). Otherwise `Key::character` with `ch = text` if ≥ 0x20, else the key code if in 0x20..0x7e, else return false. Then nudge and `return panel_->key(ev)`.
- **Other key facts.** With cmd held JUCE sends no text and the key code (E:751-754). HeadlessHost lower-cases A-Z only under cmd (HeadlessHost.cpp:187). There is no key-up, no focus handler, and repeats arrive as ordinary presses.
- **Cursor (E:646-657).** normal, leftRight, upDown, pointingHand, crosshair.
- **Files (E:827-855).** `filesInterest/DragEnter/DragMove/DragExit/Dropped` take absolute paths; positions are divided by the zoom; each is followed by a nudge.
- **`setUnboundedDrag` (E:861-877).** `enableUnboundedMouseMovement(true, true)`. It is called for every parameter drag (`FG/src/params/GestureController.cpp:39, 58`).

### (e) HostServices on EditorHost
- `showParamMenu`: nothing without a host context (E:879-890).
- `themeIndex` = the `uiTheme` pin, else `UiPreferences::theme()` (E:916-919).
- `services()` = menus | fileChooser | clipboard (E:928-931).
- `showMenu` passes `scale = getWidth() / logicalW_` (E:933-938).
- Callback rules: `FG/include/funkgui/panel/HostServices.h:144-157`.
- The menu-validity rule and the Pending/serial pattern to copy: `FG/src/gpu/HostServicesJuce.cpp:51-64, 72-116`.
- Menu look: ground background, ink100 text, ink16 highlight, bundled face at 14 px (`FG/src/juce/MenuLook.cpp:23-36`).
- `commandKeyIsMeta` default = `__APPLE__` (HostServices.h:198-205). In a wasm build that is false.
- Prefs backend API: `read/write/reload` (`FG/include/funkgui/prefs/UiPreferences.h:52-68`), `setBackend` (:70-74). Without JUCE the default is in memory (`FG/src/nojuce/PrefsDefaultBackend.cpp:9-12`). The theme is read leniently (`FG/src/prefs/UiPreferences.cpp:116-121`); `setInt` writes decimal text (:170-187).

### (f) Lifetime
- **Constructor (E:165-213).** Services, capture, logical size, `prefs.reload()`, theme, `initZoom` + `applyZoom`, `panel_->attach(*this)`, `setUiAttached(true)`, pump add, 10 Hz timer.
- **Destructor (E:215-231).** `services_->letGo()`, `stopTimer`, `panel_->closeGestures()`, `setUiAttached(false)`, pump remove, detach surface, a11y clear. `panel_` is declared first and destroyed last (EditorHost.h:189-190).
- **HeadlessHost.** Takes `Panel&`; its destructor drops pending callbacks and then calls `closeGestures()` (HeadlessHost.cpp:199-206).

### (g) WebGlSink
- One sink per canvas; a second is refused; a replacement only after destruction (`FG/include/funkgui/web/WebGlSink.h:17-24`).
- `submit(list, physW, physH)` sets the canvas `width`/`height` attributes itself when they differ (`FG/src/web/WebGlSink.cpp:324-327`). It never sets the CSS size.
- `readPixels` only in the submitting task (WebGlSink.h:102-106).
- A clip edge on a device pixel centre in y fills one row lower (WebGlSink.h:26-36).
- The destructor calls `emscripten_webgl_destroy_context` (WebGlSink.cpp:155), which runs `JSEvents.removeAllHandlersOnTarget(canvas)` (`EM/src/lib/libwebgl.js:1178`).
- **[S]** After destroy, an html5.h mousemove callback on the canvas stopped firing, while a listener added with plain `addEventListener` from EM_JS kept firing.

### Emscripten 6.0.3 (`EM/system/include/emscripten/html5.h`)
- **Mouse.** `EmscriptenMouseEvent` coordinates are `int` (:113-133). `targetX = e.clientX - (rect.left|0)` (`EM/src/lib/libhtml5.js:500`). **[S]** A synthetic move at 50.75/60.5 on a canvas at left 10.5 arrived as client 50,60 and target 40,40; a PointerEvent listener saw 50.75 and offsetX 40.25.
- **Missing from html5.h.** Pointer events, capture, drop, paste, ResizeObserver, CSS cursor.
- **Wheel (:150-160).** `deltaX/Y/Z` doubles and `deltaMode` (PIXEL 0, LINE 1, PAGE 2). No momentum flag and no direction-inverted flag. **[S]** Returning true from the callback on a canvas listener sets `defaultPrevented`. A trusted DevTools wheel arrives as `deltaY = 120`, mode 0.
- **Key (:90-105).** `key[32]`, `code[32]`, `repeat`, `location`, four modifier booleans. No `isComposing`.
- **[S] Key focus.** A trusted key reaches a canvas listener only when the canvas has `tabindex` and focus; otherwise the target is BODY.
- **Other callbacks.** blur/focus (:183-192); resize, with the listener ignoring bubbled events (libhtml5.js:672-678); visibilitychange (:321-334); pointer lock (:302-319), gated on user activation (libhtml5.js:113-125); beforeunload (:412).
- **Sizes.** `emscripten_get/set_element_css_size` (:420-421, a `getBoundingClientRect`); `emscripten_get_device_pixel_ratio` (`emscripten.h:85`).
- **Frame clock.** `emscripten_request_animation_frame`, `emscripten_cancel_animation_frame`, `emscripten_request_animation_frame_loop` (:470-472; the loop stops only when the callback returns false). `emscripten_set_main_loop` allows one loop per module (`EM/src/lib/libeventloop.js:414`).
- **[S] Pointer events.**
  - `pointerdown.detail` is always 0; `mousedown.detail` is the click count (1, 1, 2).
  - `setPointerCapture` succeeds on trusted input, and a move at (400,500) outside the canvas was still delivered.
  - On a synthetic PointerEvent `setPointerCapture` throws `NotFoundError`.
  - Ctrl+left click on macOS Chrome fires `contextmenu` with `button=0 ctrl=true`.
- **[S] Device size.** ResizeObserver `device-pixel-content-box` exists in Chrome. A CSS box of 300.5×200 gave device 300×200 at dpr 1 and 376×250 at dpr 1.25, so `round(css*dpr)` can be off by one.
- **[S] Services.** After trusted input `navigator.clipboard.writeText` resolved; `document.execCommand('copy')` returned true; `localStorage` works on 127.0.0.1. `navigator.platform = MacIntel`, `userAgentData.platform = macOS`.

### Wiring today
- `src/web/**` is globbed only under `EMSCRIPTEN` into INTERFACE `FunkGuiWeb` (`FG/cmake/FunkGuiTargets.cmake:321-323, 569-577`), `*.cpp` only. A new `.cpp` needs no CMake edit; a `--js-library` file would.
- `FunkGuiCoreCheck` compiles the web sources under `-Werror` in `all` (:593-601).
- Test executables link harness, core, gpu or presets only (`FG/test/CMakeLists.txt:7-13`). Under the `web` preset node runs them with `-sNODERAWFS` (FunkGuiTargets.cmake:220-226). None links `FunkGui::web`.
- `fg.headers` compiles every header under `include/funkgui/web/` standalone on native hosts with `-Wall -Wextra -Wshadow -Wpedantic -Werror` (`FG/tools/check-headers.sh:16-19`).
- The page: `FG/test/web/CMakeLists.txt:12-31`. It sets `FUNKGUI_BROWSER ON` before `_funkgui_internal_target`, links `-sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1`, and registers `fg.web.page` with labels `fg;live`.
- The runner hard-codes the file names `funkgui_web_page.js/.wasm` (`FG/tools/web/check-page.mjs:56`) and the log id `funkgui-log` (:200). It already speaks DevTools (:179-194).
- `FG/tools/GalleryApp/CMakeLists.txt:17-21` returns early without bgfx, which is the case on web.
- Gallery sections and `GalleryPanel` are JUCE-free and already build to wasm (`FG/build-web/FunkGuiGalleryProbe.wasm`). The native app's settings to mirror: `zoomSteps {100,125,150,175}`, `zoomPrefKey "uiZoom"` (`FG/tools/GalleryApp/GalleryApp.cpp:72-76`).

## 2. What the card must do (in an order that keeps every preset green)

1. **`include/funkgui/web/WebInput.h`** (new; plain C++, all `inline`/`constexpr`, no Emscripten include). The pure conversion layer:
   - `Mods modsFromDom(shift, ctrl, alt, meta, bool mac)`: cmd = mac ? meta : ctrl; ctrl = ctrl.
   - `bool popupFromDom(rightButton, ctrl, mac)`.
   - `float toLogical(client, rectOrigin, rectSize, logicalSize)` = `(client - origin) * logical / rectSize`. This equals dividing by the zoom when WebHost sized the canvas.
   - `bool keyFromDom(std::string_view key, Mods, KeyEvent&)`: the table of fact (d). `" "` is space with `ch = ' '`. One UTF-8 code point ≥ 0x20 is a character, lower-casing A-Z under cmd. Anything else ("Shift", "Dead", "F5", "Process") is false.
   - `WheelEvent wheelFromDom(dx, dy, deltaMode, invertedFromDevice)`: PIXEL gives `-delta * 0.5/256`, smooth. LINE and PAGE give not smooth, with a named per-line constant (my proposal: `50/256` per 3 lines). `inertial = false`.
   - `ClickCounter`: JUCE's rule (400/800 ms, 8 px, same button, at most 4, 1 after a move or a hold over 300 ms).
   - `const char* cssCursor(Cursor)`: default, ew-resize, ns-resize, pointer, crosshair.
2. **`include/funkgui/web/WebClock.h`** (new, pure). `FrameCadence`:
   - dt rule: first 1/60, clamp [0.001, 0.1];
   - fps over ≥ 1 s windows;
   - full rate capped near 60 Hz on faster displays;
   - idle: one frame per 1/12 s;
   - nudge: the next vsync;
   - hidden: none.
   - It answers "render this callback?" and "delay before the next rAF request".
   - Also the zoom helpers: `cleanSteps`, `nearestStep`, `zoomedSize`, `fitZoom`, as E:992-1010, 1050-1080.
3. **`test/unit/web_input.cpp`, `test/unit/web_clock.cpp`** (new; `// FUNKGUI_TEST name=fg.web.input|fg.web.clock timeout=120 gpu=0`). Spec rows only. They run in agent, agent-gui, nojuce and web (node).
4. **`include/funkgui/web/WebHost.h`** (plain C++) and **`src/web/WebHost.cpp`**. Proposed API:
   ```cpp
   struct WebHostConfig {
       const char* canvasSelector = "#canvas";
       std::vector<int> zoomSteps; int defaultZoomPercent = 100; const char* zoomPrefKey = nullptr;
       int fitMarginX = 0, fitMarginY = 0;            // CSS px of page chrome around the canvas (fit = window inner size minus these)
       std::function<void(bool)> setUiAttached; std::function<void()> beginBatch, endBatch;
       CaptureConfig capture{};                       // a value, NOT fromEnv(): honoured fields fixedDt, uiTheme, uiZoom, uiScale
   };
   class WebHost final : private HostServices {
   public:
       WebHost(Panel&, WebHostConfig);                // Panel outlives the host (HeadlessHost's rule); attaches; does not start
       ~WebHost();
       bool ok() const; const char* error() const;    // the sink's
       void start(); void stop();                     // the rAF clock and the 10 Hz idle; stop() also closes the Panel's gestures
       void frame(double nowMs);                      // the clock's entry point; public for tests (FramePump::tick's reason)
       const PrimList& lastFrame() const;             // the last recorded frame (valid until the next)
       WebGlSink& sink();
       struct Diagnostics { uint32_t frames, lost, restores; float fps; double scale; int zoomPercent; int physW, physH; };
       Diagnostics diagnostics() const;
   };
   ```
   - Member order: sink before the listeners' state; the sink is constructed once and never recreated.
   - Frame = steps 1, 2 (`document.hidden`), 4, 6, 7, 9, 11 of fact (a).
     - `Result::lost` or `unavailable` counts as "not submitted".
     - Each frame reads the dpr (or the ResizeObserver device box when it is fresh). `physW/H = round(cssW/H * scale)`; the CSS size is `zoomedSize(W/H)` through `emscripten_set_element_css_size`; `info.dpi = physH / logicalH`.
     - Refit the zoom on window `resize` as well.
   - Input: one EM_JS installer that adds its own listeners on the canvas under an `AbortController`.
     - Listeners: pointerdown/move/up/cancel/leave, dblclick, wheel `{passive:false}`, keydown, contextmenu; plus window resize and document visibilitychange.
     - They call back through function pointers (`$getWasmTableEntry`) with a `void*` host.
     - It sets `tabindex=0`, `touch-action:none`, `outline:none`.
     - It calls `setPointerCapture` inside try/catch and `canvas.focus({preventScroll:true})` on down.
     - Events are delivered to the Panel synchronously inside the DOM handler, in JUCE's order (up, then doubleClick), with `ClickCounter` supplying the count.
     - `preventDefault` on wheel and keydown only when consumed; always on contextmenu.
     - Each event calls `nudgeFullRate()` as in E:659-770, then a cursor update (write `style.cursor` only on change).
   - HostServices:
     - `setUnboundedDrag`: a flag only (capture already gives positions outside the canvas);
     - `showParamMenu`: nothing;
     - `nowSeconds`: `emscripten_performance_now() / 1000`, or `seconds_` under fixedDt;
     - `themeIndex` and the zoom calls as EditorHost;
     - `services()` = menus | clipboard;
     - `chooseFiles`: refuse;
     - `commandKeyIsMeta()` = platform matches `Mac|iPhone|iPad`.
   - visibilitychange hidden: `closeGestures()`. Visible: `UiPreferences::reload()` and a nudge.
   - Destructor: stop, abort the listeners, services letGo, `closeGestures()`, `setUiAttached(false)`, then the members (the sink goes after the listeners).
5. **Web services** (the seam, see §5 Q1): `src/web/WebServices.{h,cpp}`.
   - A DOM menu: `role=menu`, items from `MenuRequest`, colours from `request.theme`, bundled face through `FontFace` from `BundledFont::data()`, 14 CSS px, anchored at the canvas rect plus `anchor * (rect.width / logicalW)`.
   - It closes on outside pointerdown, Escape, blur, resize, a second menu, `dismissMenus`, letGo.
   - It keeps the Pending/serial rules of `HostServicesJuce.cpp:96-115`.
   - `copyText`: `navigator.clipboard.writeText`, with the `execCommand('copy')` fallback.
   - **`include/funkgui/web/WebPrefs.h`**: `void installLocalStoragePrefs(const char* keyPrefix)`, a `UiPreferences::Backend` with an in-memory mirror. `read` from the mirror; `write` to the mirror and `setItem` in try/catch; `reload` re-reads the prefixed keys and reports a change. The product calls it before constructing its Panel.
6. **Gallery page.** A new `tools/GalleryWeb/CMakeLists.txt` (the `tools/*/CMakeLists.txt` glob picks it up, FunkGuiTargets.cmake:622) or an `if(EMSCRIPTEN)` branch above `tools/GalleryApp/CMakeLists.txt:17`.
   - Target `funkgui_gallery_web` = `GalleryWeb.cpp` + `test/gallery/*.cpp`, `FUNKGUI_BROWSER ON`, links `FunkGui::core FunkGui::web`, same link options as the test page, an `index.html`.
   - Section from `?section=` (default "primitives"); a DOM list of links, one per `G::sections()` entry (a reload per section, so the host is never rebuilt).
   - `?theme= ?zoom= ?dt= ?scale=` fill `capture`.
   - Zoom steps and prefs key as GalleryApp.cpp:74-75.
7. **Host page test.** `test/web/host.cpp` + `host.html`, CTest `fg.web.host` (labels `fg;live`). `tools/web/check-page.mjs` gains `--page <stem>` (default `funkgui_web_page`).
8. No CMakePresets change is needed if the page targets hang on `funkgui_tools`/`funkgui_tests`. The CHANGELOG text goes in the handoff (FunkGui's CLAUDE.md gives the file to the lead).

## 3. Traps

- **html5.h handlers on the canvas die with the sink.** Destroying it strips every JSEvents handler on that canvas (libwebgl.js:1178, **[S]**). If any html5.h callback is used on the canvas, register it after the sink exists and never destroy the sink early. Own `addEventListener` listeners are immune, and they are also the only way to get capture and fractional coordinates.
- **Headers must stay plain C++.** `fg.headers` compiles `include/funkgui/web/*.h` natively: no `<emscripten/...>`, no non-inline definitions, `-Wshadow`.
- **Pure logic must be header-only.** Tests cannot link `FunkGui::web`, and `src/web/**` is not compiled natively.
- **Do not use `emscripten_set_main_loop`** (one per module, libeventloop.js:414) or `..._frame_loop` (cannot be cancelled). Use the single-shot request plus cancel.
- **`CaptureConfig::fromEnv()` in a browser** would read Emscripten's fake environment. Take the value from the config.
- **`commandKeyIsMeta()`'s default is compile-time `__APPLE__`.** WebHost must override it or every Mac browser shows CTRL hints.
- **Keys need focus.** Without `tabindex` and focus the canvas gets no trusted keys (**[S]**).
- **Never `preventDefault` an unconsumed key or wheel** (browser shortcuts, page scroll). Ctrl+wheel is also trackpad pinch-zoom.
- **Click count.** `pointerdown.detail` is 0 (**[S]**); count clicks in C++.
- **Synthetic pointers cannot be captured** (`NotFoundError`, **[S]**); wrap it, or the page test's first down throws.
- **Clipboard needs activation.** `writeText` is async and gated on user activation; the bool can only mean "issued". Input must reach the Panel synchronously inside the DOM handler, not be queued to the next frame. A menu callback should run inside the item's click handler; that is allowed, since it is not inside `showMenu`.
- **The Panel is built before the host,** so a prefs backend installed in the WebHost constructor is too late for a Panel that reads preferences when constructed. `setBackend` also bumps the revision and does not copy values.
- **Timers are throttled in hidden tabs** to about 1 Hz, so the 10 Hz idle cannot close a wheel burst there. Close gestures on `visibilitychange`.
- **No direction or momentum flags in Chrome.** `reversed` and `inertial` do not exist there. With natural scrolling, value controls (RuleSlider) will move opposite to the native plugin on macOS. Lists are unaffected, since they ignore `reversed` (PresetBrowser.cpp:1954-1956).
- **Notched mouse wheels in Chrome arrive in pixel mode** (±100/120). Treated as smooth, one notch is about 0.2, which is about two detents on a stepped slider (native: one).
- **Fractional dpi** (browser zoom, dpr 1.25) puts whole-logical-px clip edges on pixel centres, where WebGL fills one row off (WebGlSink.h:26-36). FCompressor's list clip uses unsnapped `kList.y` (PresetBrowser.cpp:1625). This is a Sprint D visual risk, not G-D's to fix.
- **`round(css*dpr)` can miss the true device box by 1 px** (**[S]** 300.5 → 300).
- **Parity is not automatic.**
  - `info.fullRate`: EditorHost reports the previous request, HeadlessHost the current want. Compare fingerprints and static primitives as `FG/tools/GalleryApp/GalleryLive.cpp:17-28` does, not the view line.
  - HeadlessHost's double-click order and ctrl-popup rule differ from the live host's.
- **EM_JS bodies go through the C preprocessor.** An apostrophe in a JS comment or an unbalanced quote breaks the build under `-Werror`.
- **The plan's "JS library" would need a CMake edit** (`--js-library`); EM_JS does not.
- **`PrimList::writeText` takes a `FILE*`.** A browser page has only MEMFS; compare in C++ inside the page instead of dumping.

## 4. How to verify

- **Existing gates.** `agent-verify`, `agent-gui-verify`, `nojuce-verify` with `tools/verify.sh`: zero drift. Only new spec rows appear; no golden should move, because nothing in core or gpu changes. `web-verify` compiles `WebHost.cpp` under `-Werror` through `FunkGuiCoreCheck` and runs the two pure tests under node. `fg.headers` covers the new headers on three presets.
- **New native/node rows, `fg.web.input`:**
  - the key table, entry by entry;
  - space's `ch`; the cmd lower-casing; "Dead", "Shift", "F5" refused;
  - mods on a Mac and elsewhere;
  - popup: right button, Ctrl on a Mac, Ctrl elsewhere;
  - `toLogical` at 100, 125, 150 and 175 % returning the logical point exactly;
  - wheel sign and units: pixel 512 gives dy −1.0 smooth; line mode gives not smooth;
  - `ClickCounter` at 399 ms, 401 ms, 8 px, the fourth click, after a move;
  - the cursor strings.
- **New native/node rows, `fg.web.clock`:**
  - first dt 1/60; clamps at 1 ms and 100 ms;
  - 60, 120 and 144 Hz streams give the capped rate;
  - idle gives 12 Hz; a nudge renders on the next vsync; hidden renders nothing;
  - `fitZoom` and `cleanSteps` against the cases of `FG/test/unit/editorhost_zoom.cpp` (`config.*`, `fit.*`).
- **Page rows, `fg.web.host`** (driven by `host.frame()` with `capture{fixedDt 1/60, uiScale 2, uiTheme 0, uiZoom 100}`, no rAF):
  - Per GalleryLive case, WebHost's `lastFrame()` against a HeadlessHost(dpi 2, theme 0) of a second section instance after the same script: fingerprint equal, static primitives bit-equal, dpi exactly 2, `readPixels` against SoftRaster within `page.cpp`'s bounds.
  - Synthetic DOM events at zoom 150 reach a recording Panel at exact logical coordinates.
  - `canvas.style.cursor`.
  - A `setZoomPercent` click changes the CSS size and the drawing buffer on the next frame.
  - Forced context loss: `frame()` reports lost, then recovers.
  - Menu: the DOM node exists with N items; a click gives the callback id; Escape gives 0; `dismissMenus` gives no callback; a second menu replaces the first.
  - A localStorage round trip and `reload()`.
- **Not automatable (lead, by hand in Chrome and Safari on the gallery page):** real trackpad scroll direction and feel; pointer capture leaving the window; clipboard under a real gesture (Safari is stricter); key focus and Tab behaviour; DPR change by browser zoom and by dragging between displays; a hidden tab; touch.
- **Trusted input** could be automated later through DevTools `Input.dispatchMouseEvent`. **[S]** shows it produces trusted events with working capture.

## 5. Open questions for the lead

1. **One agent or two?** It splits cleanly. I recommend two.
   - **G-D (host core):** `WebInput.h`, `WebClock.h`, `WebHost.{h,cpp}`, the two unit tests, the gallery page, `fg.web.host`, `check-page.mjs`.
   - **G-E (web services):** `src/web/WebServices.{h,cpp}`, `WebPrefs.h` + `src/web/WebPrefs.cpp`, its own page test of the menu, clipboard and storage, built without WebHost.
   - The seam is `src/web/WebServices.h` (showMenu with a logical-to-CSS scale and the canvas selector, dismissMenus, copyText, letGo), written by the lead and frozen.
   - The base commit carries a refusing stub `WebServices.cpp`, owned by G-E, so G-D links.
   - One agent is possible but large: about 900 lines of C++ and JS plus two pages.
   - The user's request allows 6 agents; FCompressor's CLAUDE.md still says 3.
2. **Panel ownership.** `Panel&` (HeadlessHost's rule, simplest for WebMain and the tests; my recommendation) or `std::unique_ptr<Panel>` (EditorHost's).
3. **Input mechanism.** Own EM_JS listeners (recommended: capture, fractional coordinates, independent of the sink) or html5.h callbacks (integer coordinates, no capture, removed with the sink).
4. **Unbounded drag.** No pointer lock (recommended): every parameter drag would otherwise raise the browser's pointer-lock notice. Or pointer lock behind a config flag.
5. **Wheel policy.**
   - `reversed`: false except where Safari's `webkitDirectionInvertedFromDevice` is defined (recommended), or assume natural scrolling on Macs.
   - Notched wheels in pixel mode: accept as smooth (recommended for v0.13.0), or add a `wheelDelta % 120` heuristic.
   - Confirm the line-mode constant.
6. **Full rate on fast displays.** Cap near 60 Hz (recommended, matches the native preferred rate), or draw every vsync.
7. **Zoom fit area.** Window inner size minus config margins, refitted on resize (recommended), or a container element's box.
8. **Middle button.** Ignore it (recommended), or mirror JUCE's plain down.
9. **Text input.** `keydown`'s `key` only: no IME, no dead keys, no mobile keyboard (recommended for the demo), or a hidden input element later.
10. **File drops and accessibility.** Neither in v0.13.0 (recommended): Panel paths have no browser meaning, and an ARIA mirror of A11yItems is a card of its own.
11. **Page location and prefs prefix.** `tools/GalleryWeb/` (new directory, no edit to the bgfx-guarded file) or `tools/GalleryApp/` as the plan says. Prefix default `FUNKGUI_PREFS_FOLDER + "."`.

## Not checked

- Safari and Firefox behaviour: everything marked **[S]** is headless Chrome on this Mac only.
- Whether Safari supports `device-pixel-content-box`.
- Real trackpad and mouse-wheel deltas in any browser; real key repeat; IME; touch.
- JUCE's exact key code for Shift+Cmd+letter.
- Windows JUCE wheel units.
- Whether a DOM `FontFace` built from `BundledFont` renders like the native menu.
- Closure and `MODULARIZE` builds with EM_JS callbacks.
- FCompressor's `Source/editor/gpu/Editor.cpp` (how the product derives from EditorHost).
- Nothing was built or run inside either repository.