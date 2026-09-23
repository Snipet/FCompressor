# A — HardwareReverb GUI stack map (for FCompressor)

Root of all paths below: `/Users/seanfunk/audio/plugins/HardwareReverb/` (read-only).
Snapshot taken 2026-09-22 19:40–19:52 EDT.

> **Warning: HardwareReverb was being edited while this was read.** `Source/BgfxEditor.cpp` went from
> 1944 to 1968 lines and `Source/PresetPanel.cpp` from 1278 to 1316 lines between 19:45 and 19:49
> (the brief expected 1847 lines). The changes were `replayKeys` (now takes `cmd+`/`alt+` and single characters,
> `BgfxEditor.cpp:1914-1956`) and `dumpAccessibilityTo` (now skips hidden items, `:483`). Line numbers below
> are for the **post-change** files. Treat them as approximate: someone is working on HR right now. That matters
> for the sharing decision in §6.

---

## 0. The stack in one paragraph

JUCE 8.0.4 (FetchContent, `CMakeLists.txt:112-117`) supplies the plugin wrapper, the window, and mouse and key events.
The editor (`HardwareReverbBgfxEditor`) has **no painted child Components**. `paint()` draws only the no-GPU
fallback screen. A click-through, layer-backed child `NSView` (`NativeSurface.mm`) carries a CAMetalLayer that bgfx
renders into. bgfx comes from `bkaradzic/bgfx.cmake` tag `v1.153.9385-561` (`CMakeLists.txt:236-241`) and runs in
single-threaded mode on the message thread, Metal only. **One vertex shader and one fragment shader** draw everything:
rounded rects, SDF text and capsule segments, with the kind chosen by a vertex field. Each editor submits **one
draw call per frame**. One process-wide `FramePump` driven by a `CADisplayLink` calls `bgfx::frame()` exactly once per
vsync for all open editors, at 12 Hz when idle and 60 Hz (up to 120 Hz) when active. The font is a bundled
JetBrains Mono subset (103 glyphs). A 1024×512 R8 SDF atlas is baked from it at runtime (11–23 ms). Themes are
two token tables, stored as a machine-wide preference. The audio thread publishes live DSP state through a
**seqlock** of atomic words. Offscreen harnesses turn a text dump of the frame's primitives back into pixels
(`Tools/FrameRender.cpp`) and fingerprint that geometry against golden files.

---

## 1. File-by-file inventory

Classification: **(a)** reusable verbatim (a namespace rename at most); **(b)** reusable after renaming or
parameterising (the exact change is listed); **(c)** reverb-specific, re-implement the pattern.

### 1.1 `Source/gui/` (2,158 LOC: 2,008 C++/ObjC in 19 files + 150 shader in 3 files)

| File | LOC | Purpose | Includes / deps | HR couplings | Class |
|---|---|---|---|---|---|
| `BgfxContext.h` | 111 | Process-wide bgfx singleton. Registers windows (the primary uses the default swapchain; others get `createFrameBuffer(nwh)`). Allocates view ids from a free list. Owns the program, uniforms and font texture. | `<bgfx/bgfx.h>`, `FontAtlasSdf.h` | `namespace hrvbgui`. `kMaxWindows = 16` (`:78`) | (b) namespace |
| `BgfxContext.cpp` | 295 | `initBackend` (renderFrame before init = single-threaded; `RendererType::Metal`; `BGFX_RESET_NONE`, no vsync) `:20-42`. `createResources` (vertex layout, shaders, uniforms, atlas texture) `:44-115`. acquire/release, including primary-window rebuild `:146-255`. resize `:271-294`. | `BundledFont.h`, generated `<shaders/vs_ui.mtl.h>`, `<shaders/fs_ui.mtl.h>` (`:9-10`) | namespace. Generated-header include path comes from CMake `${CMAKE_CURRENT_BINARY_DIR}/generated` (`CMakeLists.txt:290-291`) | (b) namespace. Optionally raise `init.limits.maxTransientVbSize` (§2.6) |
| `BundledFont.h` | 21 | The single place that names the shipping face | `<HardwareReverbFonts.h>` (`:3`) | `hrvbfonts::JetBrainsMonoRegularsubset_ttf` / `…ttfSize` (`:17-18`) come from `juce_add_binary_data(HardwareReverbFonts HEADER_NAME HardwareReverbFonts.h NAMESPACE hrvbfonts)` (`CMakeLists.txt:156-161`) | (b) header name + namespace of the binary-data target |
| `Col.h` | 37 | 8-bit RGBA, `withAlpha`, `mix` (gamma-space lerp) | `<cstdint>` | namespace | (a) |
| `DisplayLink.h` | 28 | C interface to CADisplayLink | – | namespace | (a) |
| `DisplayLink.mm` | 76 | Per-view `CADisplayLink` in `NSRunLoopCommonModes`. `preferredFrameRateRange` | Cocoa, QuartzCore | **ObjC class `HrvbDisplayLinkTarget` (`:9-26`)** | (b) **must rename ObjC class** (see §6.4) |
| `FontAtlasSdf.h` | 108 | Atlas constants, `kExtraChars`, glyph lookup, metrics | `<vector>` | namespace | (a) apart from extending `kExtraChars` |
| `FontAtlasSdf.cpp` | 280 | JUCE rasterises each glyph. Exact Felzenszwalb EDT, with coverage used on edge texels. Shelf packing. Overflow counts as failure. | `juce_gui_basics` | namespace | (a) |
| `FramePump.h` | 105 | `FrameClient { submitFrame(dt) -> {submitted, wantsFullRate}; frameClockView() }`. One `FramePump` for all editors. `kIdleHz = 12`, `kFullHz = 60` (`:102-103`) | `juce_events` | namespace | (a) |
| `FramePump.cpp` | 195 | Display link or fallback juce::Timer. `tick` clamps dt to [1 ms, 100 ms] and calls `bgfx::frame()` once. Rate requests (`:98-115`) | `BgfxContext.h`, `DisplayLink.h` | namespace | (a) |
| `NativeSurface.h` | 13 | C interface to the render view | – | namespace | (a) |
| `NativeSurface.mm` | 57 | Click-through (`hitTest → nil`), flipped, layer-backed child view. `getBackingScale` | Cocoa | **ObjC class `HrvbRenderView` (`:8-15`)** | (b) **must rename ObjC class** |
| `SdfCanvas.h` | 111 | Immediate-mode primitive API (§2) | bgfx, `Col.h` | namespace. The `HRVB_CANVAS_DUMP` name appears in a comment (`:84`) | (a) |
| `SdfCanvas.cpp` | 344 | Vertex emission, UTF-8 decode, text layout, dump writer, submit | `BgfxContext.h`, `FramePump.h` | namespace | (a) |
| `TagPalette.h` | 27 | 7 preset-tag colours per theme | `Col.h` | Preset-system concept | (a) only if the preset system is shared |
| `Theme.h` | 58 | The token table: `graphite()`, `paper()`, `byIndex`, `kCount = 2`, `name()` | `Col.h` | **`ice` is "reserved for exactly one thing: Freeze"** (`:21`) | (b) rename `ice` to a neutral name (for example `signal`) and redefine its one job per product |
| `TypeScale.h` | 23 | 9 text styles (§2.5) | `SdfCanvas.h` | namespace `hrvbgui::type` | (a) |
| `UiPreferences.h` | 45 | Machine-wide theme preference, `revision()` counter | `juce_data_structures` | Path in comment | (b) |
| `UiPreferences.cpp` | 74 | PropertiesFile. `folderName = "HardwareReverb"` (`:29`). **`HRVB_PREFS_DIR`** override (`:38`). Writes through immediately. Singleton is never destroyed. | `Theme.h` | **app folder name, env var** | (b) folder name + env prefix |
| `shaders/fs_ui.sc` | 115 | The single fragment shader: rrect / text / segment branches | `bgfx_shader.sh` | none | (a). Extend for new kinds (§2.4) |
| `shaders/vs_ui.sc` | 23 | Logical px → NDC. Passes data through. | – | none | (a) |
| `shaders/varying.def.sc` | 12 | 2 colours + 3 vec4 data varyings | – | none | (a) |

Stale artefacts: `build/generated/shaders/` still holds `fs_ring.mtl.h`, `fs_rrect.mtl.h` and `fs_text.mtl.h` from the old
three-program design. Nothing includes them.

### 1.2 Editor files

| File | LOC | Purpose | Couplings | Class |
|---|---|---|---|---|
| `Source/BgfxEditor.h` | 249 | `HardwareReverbBgfxEditor : AudioProcessorEditor, FileDragAndDropTarget, private FrameClient, private juce::Timer`. Fixed `kWidth 880`, `kHeight 520` (`:51-52`) | `HardwareReverbProcessor&`, `PresetPanel`, `hrvb::FDNReverb::UiFrame live_` (`:167`), `RankModel` (`:151-160`), reverb state (`capAmt_[32]`, `freezeAmt_`, `smearAmt_`) | mixed: (c) for the class, (b) for the infrastructure inside it (§6.5) |
| `Source/BgfxEditor.cpp` | 1968 | The whole panel: layout, a11y, formatting, surface lifecycle, frame loop, drawing, input | See the table below | mixed |
| `Source/PluginEditor.h/.cpp` | 53 / 183 | **Legacy** JUCE-Component editor (rotary sliders, 760×220/396, "Advanced" toggle). Built only with `-DHARDWAREREVERB_CLASSIC_GUI=ON` (`CMakeLists.txt:99-101, 219-220`; `PluginProcessor.cpp:430-436`) | reverb params | not needed. FCompressor does not have to carry a classic editor |
| `Source/PresetPanel.h` | 167 | Preset strip and browser, all drawn on the canvas, including text entry. Input-refusal protocol (each handler returns `bool`) | `HardwareReverbProcessor&` (`:32`), `hrvb::presets::PresetStore` via `SharedResourcePointer` (`:125`), geometry fixed to the 880×520 header (`:14-18`), `HRVB_UI_BROWSER` hook (`:71`), `.hrvbpreset` (`presets/PresetFile.h:12`), `HRVB_PRESETS_DB` and `~/Library/Application Support/HardwareReverb/Presets.db` (`presets/PresetStore.cpp:21-23`) | (b)/(c). Reusable only with a `PresetHost` interface plus parameters for name/extension/path/geometry. Generic pieces to lift: `editKey` line editor (`PresetPanel.cpp:~385-421`), `fit()`, `printable()`, `MenuLook` (`:54-150`) |

`BgfxEditor.cpp` by region (current line numbers):

| Lines | Region | Generic? |
|---|---|---|
| 18-128 | Reverb constants (`kSeed48k` primes, axis maps `axisX`/`freqX`, `bitReverse5`, `gainToHeight`), plus `fade()`, `fmt()`, `sameBits()` | `fade/fmt/sameBits` generic; the rest (c) |
| 131-175 | ctor/dtor: env hooks, prefs reload, theme, layout, a11y, `setUiAttached`, pump add. The dtor closes open gestures before detaching. | generic pattern |
| 178-240 | `buildLayout`: 6 primary + 8 secondary controls, order cells, freeze rect, theme cells | (c) |
| 249-424 | `AccessibleItem` (Kinds: control, orderCell, freeze, themeCell, preset*) | generic pattern, (b) |
| 426-495 | build/refresh/dump accessibility | mixed |
| 497-510 | `applyTheme` | generic |
| 513-559 | `recomputeRank` (prime snap mirroring the DSP) | (c) |
| 562-647 | `refreshCache`: value formatting as an `if` chain on param-ID strings | (c). Needs a per-control formatter |
| 650-787 | Surface attach/detach/re-parent/move/resize/backing scale | **generic, verbatim** |
| 789-812 | Fallback `paint()` ("HARDWARE REVERB" / "GPU RENDERER UNAVAILABLE") | generic apart from strings |
| 815-1066 | `submitFrame`: prefs revision, `isShowing` gate, retry, `owns()` recovery, scale, capture hooks, easing, UiFrame read, view choice, canvas, full-rate decision | ~40% generic |
| 1068-1480 | `drawFrame`, header, display, views (filter/mod), Rank | (c). The view mechanism is generic. |
| 1483-1553 | `drawControls` (rule-slider rendering) | **generic widget** |
| 1556-1586 | Footer: first-run hint, spec line | generic pattern |
| 1589-1908 | Hit tests, gestures, drag, wheel, double-click, keys | **generic widget and gesture logic** |
| 1914-1956 | `replayKeys` | generic |
| 1959-1968 | File drag → preset panel | generic |

### 1.3 Every HardwareReverb-specific coupling in the GUI layer

- **Namespaces:** `hrvbgui` (all gui files), `hrvbgui::type` (TypeScale), `hrvbfonts` (binary data), `hrvb` (DSP/presets/`Tools/Harness.h`).
- **Env vars** (all read with `std::getenv`):
  `HRVB_CANVAS_DUMP`, `HRVB_CANVAS_DUMP_AFTER` (`BgfxEditor.cpp:136-141`); `HRVB_UI_THEME` (`:152`, and **every frame** at `:826`);
  `HRVB_UI_SCALE`, `HRVB_UI_SCALE_AFTER` (`:718-722`); `HRVB_UI_BROWSER`, `HRVB_UI_KEYS`, `HRVB_A11Y_DUMP` (`:885-894`);
  `HRVB_UI_VIEW` (**every frame**, `:998`); `HRVB_PREFS_DIR` (`gui/UiPreferences.cpp:38`, `Tools/PrefsCheck.cpp:33-41`);
  `HRVB_PRESETS_DB` (`presets/PresetStore.cpp:21`, `Scripts/capture-frame.sh:22`).
- **App-support paths:** `~/Library/Application Support/HardwareReverb/preferences.settings` (`UiPreferences.cpp:26-32`; hard-coded in `Tools/PrefsCheck.cpp:44`), `…/HardwareReverb/Presets.db`.
- **ObjC runtime class names:** `HrvbDisplayLinkTarget`, `HrvbRenderView`.
- **Fixed geometry:** 880×520 (`BgfxEditor.h:51-52`; `FrameRender.cpp:75` defaults; PresetPanel's constants). The Rank exclusion band `y 165..295` in the FrameRender fingerprint (`FrameRender.cpp:122-127`).
- **Reverb constants in the editor:** prime seed table, RT60 map `0.2..30 s`, size scale `0.25..6`, axis `4..640 ms`, 32 lines, `fs = 48000` assumption (`BgfxEditor.cpp:48-54, 535`).
- **Strings:** "HARDWARE"/"REVERB" wordmark (`:1095-1098`), "FEEDBACK DELAY NETWORK", "FDN / HADAMARD / SIMD / SDF" footer (`:1584`), fallback text (`:799-811`), a11y names ("Matrix order 4" etc., `:440`).
- **Theme token `ice`** = Freeze.
- **Processor API used by the editor:** `proc.apvts`, `proc.readUiFrame(UiFrame&)`, `proc.setUiAttached(bool)`, `proc.presets()`, `proc.getSampleRate()`.
- **CMake:** `hrvb_compile_shader()` (`CMakeLists.txt:250-266`), targets `HardwareReverbShaders`, `HardwareReverbFonts`, the definition `HARDWAREREVERB_BGFX_GUI=1`, tool targets `HardwareReverb{AtlasDump,FrameRender,PrefsCheck,FontProbe}` (`:400-418`), `verify-gui` (`:582-602`).
- **Scripts:** `Scripts/capture-frame.sh` finds `HardwareReverb_artefacts/*Standalone*/MacOS/HardwareReverb` (`:13`) and sets `HRVB_*`.

---

## 2. SdfCanvas API reference

### 2.1 Public API (exact signatures, `Source/gui/SdfCanvas.h`)

```cpp
explicit SdfCanvas(BgfxContext& ctx);
void  begin(bgfx::ViewId view, bgfx::FrameBufferHandle fb,
            int logicalW, int logicalH, int physW, int physH, Col clear, float seconds);
void  rrect (float x, float y, float w, float h, float radius,
             Col fill, float borderW = 0.0f, Col border = {}, float softness = 0.0f);
void  rrect4(float x, float y, float w, float h, float rTL, float rTR, float rBR, float rBL,
             Col fill, float borderW = 0.0f, Col border = {}, float softness = 0.0f);   // unused in HR
void  hairlineH(float x, float y, float w, Col c);   // 1 device px tall, y floored to device px
void  hairlineV(float x, float y, float h, Col c);   // 1 device px wide, x floored
void  segment(float x0, float y0, float x1, float y1, float width, Col c, float softness = 0.0f);
float snapY(float y) const;                          // round to device px (note: hairlines FLOOR)
struct TextStyle { float px = 13; float tracking = 0; float weight = 0; bool tabular = false; };
enum class Align { left, centre, right };
void  text(const char* s, float x, float y /*TOP of em box*/, const TextStyle&, Col, Align = Align::left);
float textWidth(const char* s, const TextStyle&) const;
float capCentreTop(float centreY, const TextStyle&) const;
float sharedBaselineTop(float otherTop, const TextStyle& other, const TextStyle& mine) const;
void  end();
void  setTextGamma(float g);
static void dumpNextFrameTo(const char* path, int skipFrames = 0);
```

Other notes:
- Circles and dots: `rrect(cx-r, cy-r, 2r, 2r, r, …)`. The radius is clamped to `min(hw,hh)` in the shader (`fs_ui.sc:53`), so `radius 9999` gives a pill. Rings: `fill.a = 0` plus `borderW` (`PresetPanel.cpp:115-120`).
- `softness` (glow) is **never used** in HR. That fits the no-shadows rule.
- There is **no clip, scissor or transform stack.** Everything is drawn in absolute logical px, and callers clip on the CPU (for example `jlimit(top, datum, …)` at `BgfxEditor.cpp:1298-1299`).
- The canvas is **constructed per frame on the stack** (`BgfxEditor.cpp:1031`), so `verts_` regrows from empty every frame. A lifted shell should keep it as a member (`clear()` keeps capacity).

### 2.2 Vertex format

C++ `Vtx` (`SdfCanvas.h:91-98`) and bgfx layout (`BgfxContext.cpp:48-55`) are 64 bytes per vertex:

| Offset | Attrib | Type | C++ field | Varying |
|---|---|---|---|---|
| 0 | Position | 2×float | `x, y` (logical px) | – |
| 8 | Color0 | 4×uint8 normalised | `c0` (fill/text/stroke colour, RGBA packed `r | g<<8 | b<<16 | a<<24`) | `v_color0` |
| 12 | Color1 | 4×uint8 normalised | `c1` (border colour; segment: = c0; text: = c0) | `v_color1` |
| 16 | TexCoord0 | 4×float | `d0` | `v_data0` |
| 32 | TexCoord1 | 4×float | `d1` | `v_data1` |
| 48 | TexCoord2 | 4×float | `d2` | `v_data2` |

Per-kind payload (`SdfCanvas.cpp`). **Free slots marked ∅**:

| Kind (`d2.z`) | d0 | d1 | d2 | c1 |
|---|---|---|---|---|
| 0 RRECT | local x,y (centre-relative, +y down), hw, hh | rTL, rTR, rBR, rBL | borderW, softness, 0, **∅ (=0)** | border |
| 1 TEXT | atlas u,v, **∅, ∅** | weight, aa (field units), gamma, **∅** | **∅, ∅**, 1, **∅** | = c0 (∅) |
| 2 SEGMENT | local x,y, hw, hh (bbox) | ax, ay, bx, by (centre-relative) | radius (=width/2), softness, 2, **∅** | = c0 (∅) |

Geometry: every primitive is **one quad = 6 non-indexed vertices** (`quad()` pushes a,b,c,a,c,d, `SdfCanvas.cpp:85-89`). There is no index buffer. The AA apron is `pad = 1.5 + softness` logical px (`:105, 130`). The dump writes vertex 0 (TL) and vertex 2 (BR) only (`:301-316`), so **every quad must be axis-aligned** for FrameRender to rebuild it.

Submit (`:321-342`): one transient VB, uniform `u_viewSize = {viewW, viewH, 1/dpiScale, seconds}`, texture stage 0 = font atlas, state `WRITE_RGB | WRITE_A | BLEND_ALPHA` (straight alpha), `ViewMode::Sequential`, one `bgfx::submit`. There is no depth test and no MSAA (`BGFX_RESET_NONE`).

### 2.3 Fragment shader kinds (`Source/gui/shaders/fs_ui.sc`)

```glsl
#define KIND_RRECT 0.0   #define KIND_TEXT 1.0   #define KIND_SEGMENT 2.0     // :30-32
if (v_data2.z < 0.5)      { /* rrect: per-corner radius by quadrant, r=min(r,min(b.x,b.y)),
                               d = length(max(q,0)) + min(max(q.x,q.y),0) - r;
                               fa = (1 - smoothstep(-fw, fw, d)) * a,  fw = max(aa, soft);
                               border band |d + bw/2| - bw/2, composited source-over */ }   // :39-79
else if (v_data2.z > 1.5) { /* segment: capsule, t = clamp(dot(p-a,ba)/dot(ba,ba),0,1),
                               d = length(p - a - ba*t) - r */ }                            // :80-94
else                      { /* text: d = texture2DLod(s_texColor, uv, 0).x;
                               a = smoothstep(0.5-weight-taa, 0.5-weight+taa, d);
                               col.a = c.a * pow(a, gamma) */ }                             // :95-112
```

- AA half-width = one physical pixel in logical px, `u_viewSize.z`. **No `fwidth()` anywhere.** Text AA is computed on the CPU as `aa = 1/(dpi·2·kSpread·scale)` (`SdfCanvas.cpp:224-225`).
- The branch order matters: **a new kind 3 would fall into the `> 1.5` segment branch.** Any new kind means rewriting the chain as `<0.5 / <1.5 / <2.5 / <3.5 …`.

### 2.4 Per-frame limits

- **Transient vertex buffer: 6 MiB** (`BGFX_CONFIG_MAX_TRANSIENT_VERTEX_BUFFER_SIZE (6<<20)`, `build/_deps/bgfx-src/bgfx/src/config.h:427`; not overridden in `initBackend`). It is **shared by every view in the process for one `bgfx::frame()`**, so by all open editors together.
  At 64 B × 6 = **384 B per primitive**, that is **16,384 primitives per frame, process-wide**.
- Measured load (goldens under `Tools/golden/`): default frame 450 static primitives (368 text) plus ≤32 live caps, about 185 KB (2.9%). Filter view 752 (320 segments). Mod view 812 (384 segments).
- Overflow: `end()` checks `getAvailTransientVertexBuffer(n) < n` and **drops the whole frame silently** (`SdfCanvas.cpp:322-329`). With several editors open, the ones that submit last blank out.
- Transient index buffer: 2 MiB, unused. Max views 256 (HR uses 0..16). Max frame buffers 128. **Max shaders 512**, which is why `BgfxContext` holds shader handles explicitly (`BgfxContext.h:101-106`). Draw calls: 1 per editor.
- `kMaxWindows = 16` editors per process (`BgfxContext.h:78`). The 17th waits for a slot and does not fail (`BgfxEditor.cpp:656-660`).

### 2.5 Text layout

- `px` is **JUCE font height (ascent + descent), not em.** Baked at `kBasePx = 48`, so `scale = px/48`. JetBrains Mono has UPM 1000, ascender 1020 and descender 300, so em = px × 0.758. Metrics at 48 (`Tools/golden/fontprobe.txt`): ascent 37.0909, cap 26.5455, x-height 20, max digit advance 21.8182.
- Pen: `x += advance·scale + tracking`. Space: `spaceAdvance·scale + tracking`. `textWidth` drops the trailing tracking so centred and right-aligned runs line up optically (`SdfCanvas.cpp:173-189`).
- **Digit slotting:** with `tabular`, each digit takes `maxDigitAdvance·scale` and is centred in that slot, `slot = (digit - adv)/2` (`:240-244`). JetBrains Mono is monospace, so this does nothing, which is exactly why the face was chosen (README 226-238).
- Vertical placement: `y` is the top of the em box, baseline = `y + ascent·scale`. `capCentreTop(c) = c - (ascent - cap/2)·scale`. `sharedBaselineTop(top, other, mine) = top + (other.px - mine.px)·ascent/48`.
- UTF-8 is decoded (`:31-51`). **An unknown codepoint is skipped with no advance** (`:237-238`), so it vanishes without any error. Missing extras must be caught when the subset is generated.
- Weight is an SDF threshold shift (≈0.05 reads as semibold). Text gamma is per theme (1/1.4 on dark, 1.4 on light).
- **TypeScale** (`TypeScale.h:14-22`) `{px, tracking, weight, tabular}`:
  `kDisplay{44,0,.03,T}` `kValueP{24,0,0,T}` `kValueS{18,0,0,T}` `kNumeral{18,0,0,T}` `kWordmark{13,2.6,.05,F}` `kLatch{13,2.0,.05,F}` `kLabel{11,1.6,.03,F}` `kCaption{10,2.0,.03,F}` `kMicro{10,1.4,.03,F}`. Ad hoc: the unit style `kUnit{14,1,.03,F}` (`BgfxEditor.cpp:1173`). The hard floor is **10 px**.

### 2.6 CRUCIAL: how curves are drawn today, and how to draw a compressor's visuals

**The README's "rectangle and text only" line (README:337) is out of date.** The canvas has a third primitive,
**`segment()`: an antialiased capsule SDF** (kind 2). Every curve in HR is a polyline of capsules. Consecutive
capsules overlap by one radius at the joins, and the SDF feathering hides the seam when the colour is opaque:

- Filter view (`BgfxEditor.cpp:1219-1307`): 161 log-spaced samples 20 Hz–20 kHz, complex one-pole evaluation, and
  `curve = [&](ys,col,w){ for k in 1..160: cv.segment(xs[k-1],ys[k-1],xs[k],ys[k],w,col); }`. Two curves give 320 segments. Active curve 1.5 px in accent, context curve 1.0 px in ink32. Grids use `hairlineH/V`. Axis ticks are 5 px `hairlineV` with kMicro labels.
- Mod view (`:1313-1375`): 8 sines × 48 segments. Chevrons in the preset strip are 2 segments each (`PresetPanel.cpp:502-508`).
- FrameRender evaluates the capsule exactly as the shader does (`Tools/FrameRender.cpp:213-230`). The view goldens exist (`framerender-filter.txt`, `framerender-mod.txt`).

**Known defect of the segment approach:** with a colour whose alpha is below 1 (for example `fade(c, alpha)` during the 180 ms
view crossfade), the overlapping capsule ends double-blend and show beads at every join. The fix needs no shader
change. On the flat ground, draw curves with an **opaque pre-mixed colour** (`mix(th.ground, c, a)`) instead of alpha.
This is exact wherever the curve sits on bare ground, which is all of this design, because there are no gradients.

**Recommended approach for each FCompressor visual:**

| Visual | Primitive(s) | Cost |
|---|---|---|
| Grid, axes, threshold marker | `hairlineH/V` (device-snapped), tick labels in `kMicro`. Dashed lines = runs of 1 px `rrect`s | ~20–40 prims |
| Unity diagonal | one `segment`, ink16 | 1 |
| Transfer curve with soft knee | polyline of `segment`s, **non-uniform sampling**: ~64 uniform across the dB range plus ~32 inside `[T-W/2, T+W/2]` where `y = x + (1/R-1)(x-T+W/2)²/(2W)`. Width 1.5 in accent while a curve-shaping control is under the hand (HR's filter-view convention), 1.0 in ink100/70 otherwise. Opaque pre-mixed colour. | ~96 |
| Fixed-ratio Modes (2-4-10, ∞ "all buttons") | Draw the **other detents as ghost curves in ink16**, as the Rank ghosts lines above the current Order (`BgfxEditor.cpp:1438-1439`) | +96 per ghost |
| Filled GR region (between curve and unity) | **New kind AREA** (below) or rrect columns (fallback) | ~1 per px column |
| Operating-point dot | `rrect` circle (r = 3–4) in ink100. Optional ring = `rrect` with `borderW` (not `softness`, which is the no-glow rule). The trail is a few segments with decreasing ink. | 1–5 |
| Scrolling GR history (hundreds of samples) | **KIND_AREA column per logical px**, filled from the 0 dB line down to the GR value, with the top edge stroked in c1. 400 px wide = 400 prims ≈ 154 KB/frame. Re-emitted every frame; "scrolling" is just shifted x. | 400 |
| Meters (in/out/GR bars, peak hold) | `rrect`s | ~10 |

**Zero-shader-change fallback for fills:** one `rrect` per column, **overlapping the next column by 1 logical px, in an opaque
pre-mixed colour**. Abutting translucent or AA'd columns leave light seams: at 2×, the two AA aprons at a shared edge give about
0.84 and 0.16 coverage, so ≈13% lighter lines at every boundary. Overlap plus opacity removes them. The column tops are then flat steps,
one logical px wide. Draw the line itself as a separate segment polyline.

**Proposed new primitive `KIND_AREA = 3` (recommended, ~25 shader lines):** a vertical span between two straight edges
across one column, with an optional stroke on the top edge. **The quad stays axis-aligned**, so the dump format and
FrameRender's rectangle rebuild keep working. The quad's x-extent is exactly `[x0,x1]` with **no x-apron**, so neighbouring
columns tile with no gap and no overlap. The y-extent is `[min(top)-pad, max(bottom)+pad]`.

Payload: `d0 = (lx, ly, hw, hh)` as for rrect. `d1 = (yTopL, yTopR, yBotL, yBotR)`, centre-relative. `d2 = (strokeHalfW, softness, 3, flags)`.
`c0` = fill, `c1` = top stroke.

```glsl
else if (v_data2.z < 3.5) {                       // KIND_AREA
    vec2  p  = v_data0.xy;  float hw = v_data0.z;
    float t  = clamp(p.x / (2.0 * hw) + 0.5, 0.0, 1.0);
    float yT = mix(v_data1.x, v_data1.y, t), yB = mix(v_data1.z, v_data1.w, t);
    float sT = (v_data1.y - v_data1.x) / (2.0 * hw), sB = (v_data1.w - v_data1.z) / (2.0 * hw);
    float dT = (yT - p.y) * inversesqrt(1.0 + sT * sT);   // >0 above the top edge
    float dB = (p.y - yB) * inversesqrt(1.0 + sB * sB);   // >0 below the bottom edge
    float fa = (1.0 - smoothstep(-aa, aa, max(dT, dB))) * v_color0.a;
    float sa = v_data2.x > 0.0 ? (1.0 - smoothstep(v_data2.x - aa, v_data2.x + aa, abs(dT))) * v_color1.a : 0.0;
    float oA = sa + fa * (1.0 - sa);                         // same source-over as the rrect border
    col = vec4((v_color1.rgb * sa + v_color0.rgb * fa * (1.0 - sa)) / max(oA, 1e-5), oA);
}
```

Where the pieces go:
1. `fs_ui.sc:30-32`: add `#define KIND_AREA 3.0`. Rewrite the branch chain (`:39, 80, 95`) as `<0.5 rrect`, `<1.5 text`, `<2.5 segment`, `else area`.
2. `SdfCanvas.cpp:13-15`: add `constexpr float kKindArea = 3.0f;`.
3. `SdfCanvas.h`: add
   `void area(float x0, float x1, float yTop0, float yTop1, float yBot0, float yBot1, Col fill, float strokeW = 0.0f, Col stroke = {});`
   and the convenience wrappers
   `void areaStrip(const float* xs, const float* yTop, const float* yBot /*nullable = flat base*/, int n, float yBase, Col fill, float strokeW = 0.0f, Col stroke = {});`
   `void polyline(const float* xs, const float* ys, int n, float width, Col c);` (segments; no shader change)
   `void disc(float cx, float cy, float r, Col fill, float ringW = 0.0f, Col ring = {});` (rrect wrapper).
4. `Tools/FrameRender.cpp:119-121, 194-195`: classify kinds by range (`isSeg = d2[2] > 1.5f && d2[2] < 2.5f`) and add a CPU mirror of the area branch (~20 lines).
   **This must land with the shader change, or the fingerprint will call areas "segments".**
5. **Live-geometry flag:** `d2.w` is 0 for every current kind. Define `d2.w = 1` as "live/animated" and add `SdfCanvas::setLive(bool)` to stamp it.
   The FrameRender fingerprint can then skip flagged primitives. Today it excludes the Rank caps with a hard-coded y-band and a
   size heuristic (`FrameRender.cpp:122-127`), which will not carry over to meters, GR history or the operating dot.
6. **Budget:** a full FCompressor frame (base panel ~500 + transfer ~250 + history 400–800 + meters) is about 1.2–1.8K prims, 0.45–0.7 MB.
   That allows ~9–13 editors before frames drop. Fix either way:
   (i) set `init.limits.maxTransientVbSize = 32 << 20;` in `BgfxContext::initBackend` (1 line), or
   (ii) switch to indexed quads: 4 vertices + 6×uint16 indices = 268 B/prim, −30%.
   Also make the drop visible (assert, or count it in the dump) rather than silent.
7. Texture-driven plot (optional, later): one quad samples an R32F history texture with a second sampler (`s_plotData`,
   stage 1, updated with `bgfx::updateTexture2D`). This makes a history 1 primitive regardless of width, but it needs per-editor
   texture lifetime and a dump extension so FrameRender can reproduce it. Not worth it at ≤1K columns.

**The data path for history is not the seqlock.** The UiFrame seqlock is *latest value wins*. The editor reads at 12–60 Hz,
while blocks arrive at roughly 375–1500 Hz, so peak GR between frames would be lost. For history, use an SPSC ring:
the audio thread writes `{grDbMax, inPeak, outPeak}` per block or per fixed ~2–5 ms chunk into a power-of-two array and publishes
an `atomic<uint64_t> writeCount` with release ordering. The editor reads `[lastRead, writeCount)` each frame (bounded to capacity)
and aggregates the max per column (fixed time per column, for example 5 s / 400 px = 12.5 ms). Keep the seqlock for instantaneous
state: detector level, current GR, operating point, sample rate.

---

## 3. Editor architecture patterns in BgfxEditor

### 3.1 Control model (`BgfxEditor.h:56-78`)

```cpp
struct Control {
    juce::RangedAudioParameter* param; const char* label;
    juce::Rectangle<float> track;   // the drawn 1px rule
    juce::Rectangle<float> hit;     // generous target (primary 136x88, secondary 98x70)
    Slot slot; bool bipolar;        // bipolar fills from the default notch
    float hoverAmt, shown; bool shownInit;             // animation state, survives relayout
    juce::String valueText, unitText, subText;         // cached formatted value
    float cachedNorm = -1; int cachedAux = -1;          // cache key (norm bits + derived-model key)
};
```

- **Layout** (`buildLayout`, `:178-240`) is **absolute constants**, built once. The window never resizes (`setSize` once, `:146`).
  Primary: x `{40,176,312,448,584,720}`, top 316, width 120, track at top+72. Secondary: 8 on a 102 pitch, top 416, width 86, track at top+54.
  Hit rectangles keep a ≥4 px dead gutter between neighbours (`:218-220`). Order cells: `{640+52i, 20, 48, 32}`. Freeze `{700,96,140,44}`.
  Theme cells `{710,52,72,16}` and `{792,52,48,16}`. These are fixed widths because layout runs before the font exists (`:230-237`).
- **Formatting** (`refreshCache`, `:562-647`) only reformats when `sameBits(norm)` or the aux key changes. Before that change it cost about
  780 String allocations per second (`:566-568`). Formatting is a hard-coded `if` chain on param ID.
  For FCompressor this must become a **per-control, per-Mode `std::function<void(float norm, Texts&)>`**.

### 3.2 Hit testing
A linear scan of rectangles: `hitControl`, `hitOrderCell`, `hitThemeCell`, `hitFreeze` (`:1589-1613`). The preset panel gets first
refusal on every event (`mouseDown`, `:1687`). `updateHover` (`:1641-1660`) sets hover indices, drives the display band and sets the cursor:
`LeftRightResizeCursor` over a control, `PointingHandCursor` over cells or Freeze. It also nudges the pump to full rate.

### 3.3 Drag (fine/ultra, re-anchoring, unbounded)

```cpp
// mouseDown (:1732-1744)
dragStartNorm_ = c.param->getValue(); dragStartPos_ = e.position;
dragFine_ = e.mods.isShiftDown(); dragUltra_ = e.mods.isCommandDown();
c.param->beginChangeGesture();
e.source.enableUnboundedMouseMovement(true, true);   // pointer hidden, infinite travel
// mouseDrag (:1746-1772)
if (fine != dragFine_ || ultra != dragUltra_) { /* re-anchor: no jump on modifier toggle */
    dragFine_ = fine; dragUltra_ = ultra; dragStartNorm_ = c.param->getValue(); dragStartPos_ = e.position; }
const float span = ultra ? 6000.0f : (fine ? 1200.0f : 240.0f);          // px per full range
const float d = (e.position.x - dragStartPos_.x) + (dragStartPos_.y - e.position.y);  // right OR up
applyNorm(c, dragStartNorm_ + d / span, true);   // setValueNotifyingHost(jlimit(0,1,…))
```

`mouseUp` → `endDragGesture()` (`:1621-1627`) and `touchView(dragging_)`. The destructor calls `endDragGesture()` and `endWheelGesture()`
(`:163-175`) because a host can close the editor mid-drag.

### 3.4 Wheel automation gesture and timer

```cpp
// (:1805-1848)
if (idx == kNone) return;                         // hit-test required: host scrolling must not write automation
const int   n    = c.param->getNumSteps();
const float step = (n > 1 && n < 64) ? 1.0f / (n - 1) : (e.mods.isShiftDown() ? 0.005f : 0.025f);
const float delta = wh.deltaY != 0.0f ? wh.deltaY : wh.deltaX;   // shift+scroll arrives as X on macOS
const float dv = dir * delta * (wh.isSmooth ? 1.0f : 4.0f) * step;
if (wheelParam_ != c.param) { endWheelGesture(); wheelParam_ = c.param; wheelParam_->beginChangeGesture(); }
startTimer(kWheelIdleMs);                          // 500 ms, restarted per notch; timerCallback → endWheelGesture
```

The timer is a `juce::Timer` on the message thread, **not the frame clock**, because the display link stops while the window is occluded
and the gesture would stay open (`BgfxEditor.h:119-124`).

### 3.5 begin/endChangeGesture discipline
- Discrete clicks (order cell, freeze, a11y press, double-click reset, keys) use `begin` + `setValueNotifyingHost` + `end` together.
  An order cell skips the write when the value is unchanged (`:1712-1720`).
- Drag: begin on down, end on up, and in the destructor. Wheel: one gesture per scroll burst.
- Right-click and ctrl-click never move a value. They open the **host's** parameter menu:
  `getHostContext()->getContextMenuForParameter(param)->showNativeMenu(...)` (`:1692-1700`).

### 3.6 Double-click reset
`mouseDoubleClick` (`:1792-1803`) does `begin`, `setValueNotifyingHost(getDefaultValue())`, `end`. It ignores popup-menu clicks and gives the preset panel first refusal.

### 3.7 Keyboard focus (`:1850-1908`)
- Tab and Shift-Tab cycle `focused_` and show the ring (`focusRing_`). Escape hides the ring. A mouse click clears the ring (`:1683`).
- The arrows adjust the focused control first, then the hovered one, then the last touched one, else control 0: ±0.01 normalised (Shift ±0.001), as a begin/set/end triple.
  **Gap:** on a stepped parameter, ±0.01 normalised snaps back to the same step, so **arrows cannot move a choice or detented
  parameter**. The wheel handles steps; the keys do not. Fix this in the lifted widget: step to the next detent.
- Returns `true` so Logic and Live don't swallow the key. The ring is four accent hairlines on `hit.reduced(1)` (`:1542-1551`).

### 3.8 Views, crossfade and dwell (the band under the display)
- `enum class View { rank, filter, modulation }`. `viewFor(control)` maps param IDs to views (`:1190-1201`).
- `viewTarget_` is the dragged control's view, else `viewSource_`'s view. `viewSource_` is set by wheel, key or drag release (`touchView`, `:1203-1208`) and expires after **0.9 s** (`:993-997`).
  **Hover never switches views** (`:989-992`).
- `viewShown_` latches the last non-default view so it fades out correctly. `viewAmt_` eases with τ = **0.18 s** and snaps within 1e-3 (`:1014-1019`).
- Draw: `drawRank(cv, 1 - viewAmt_)` then the view at `viewAmt_` (`:1076-1083`). The default view dims but never disappears. Every colour goes through `fade(c, alpha)`.
- **Direct fit for FCompressor's "characteristics screen":** touching Threshold/Ratio/Knee shows the transfer curve, Attack/Release show the GR/time view, the sidechain filter shows a filter view (HR's `drawFilterView` complex one-pole math carries straight over).
  The default view could be GR history.

### 3.9 Display band
Shows RT60 by default, or the dragged control, else the hovered one, else `displaySource_`. That source is held by a **0.9 s dwell** after hover/drag ends (`:983-987, 1146-1164`).
Freeze overrides it: "HOLD" in `ice`. The value is in `kDisplay` (44 px), and the unit is drawn on a shared baseline with `kUnit` (`:1166-1176`).

### 3.10 Accessibility children (`:249-495`)
One invisible `juce::Component` per operable thing (`setInterceptsMouseClicks(false,false)`, never paints), bounds = its hit rectangle.
Roles: control → `slider` with `AccessibilityValueInterface` (range 0–1, step 0.01; `setValueAsString` uses `param->getValueForText`).
Order and theme cells → `radioButton` with press. Freeze and the preset browser → `toggleButton` (press + toggle). Preset prev/next/save → `button`. Rows → `listItem`.
Checked state comes live from the params (`isOn`). Spoken values use the same cached text the panel prints. `refreshAccessibility()` runs **every frame**
(titles follow the preset, items hidden under the browser) (`:455-476`). `HRVB_A11Y_DUMP` writes role, title, bounds and value/state after the first frame,
because JUCE only creates handlers once the component is in a window (`:888-894`).

### 3.11 Theme switching via the UiPreferences revision
`applyTheme(idx, persist)` (`:497-510`) swaps `th_ = Theme::byIndex(idx)` and persists through `UiPreferences::setTheme`, which bumps `revision_` and writes through immediately.
Every frame `submitFrame` compares `prefs.revision()` with `prefsRevision_` (`:826-834`), so other editors **in the same process** follow on the next frame.
Other processes see the change when an editor opens (`reload()` in the constructor, `:149-151`). `HRVB_UI_THEME` forces a theme without persisting it.

### 3.12 First-run hint
`firstRunHint_ = 6.0f` seconds counts down on wall clock (`:1022-1024`), fades over the last 0.4 s, and is cut to 0.4 s on the first mouse move (`:1668`).
It keeps the panel at full rate while it shows. Text: "DRAG A VALUE. DOUBLE-CLICK TO RESET." (`:1558-1564`).
Afterwards the footer shows a **persistent spec line** in place of tooltips: `LABEL   DEFAULT x   DRAG   SHIFT FINE   DBL-CLICK RESET   RIGHT-CLICK MENU` (`:1567-1582`).
There is also an "always chrome" fallback: if a mouseDown ever arrives with no mouseMove, the tracks and carets draw permanently (`:1488`).

### 3.13 Fallback screen and retry (`:650-690, 789-812, 838-872`)
- `attachSurfaceIfPossible`: needs a peer and a free slot (a full slot is a wait, not a failure). It creates the render view at `peer->getAreaCoveredBy(*this)`,
  sizes physical px as logical × scale, and calls `BgfxContext::acquire`. **Five consecutive failures set `bgfxFailed_`** and the fallback shows. A success clears everything.
- `submitFrame` retries every **2 s** while hopeful and every **10 s** once failed. The fallback `repaint()` happens once per state change, not every tick.
- If `!BgfxContext::get().owns(renderView_)`, a primary-window rebuild failed. Detach and retry on the next tick (`:866-872`).
- `paint()` is JUCE text on the ground colour: wordmark, "GPU RENDERER UNAVAILABLE", and a pointer to the host's generic editor.
- `parentHierarchyChanged` re-attaches when the host re-parents the editor to a new peer (`:745-757`).
  `detachSurface` releases the display link **before** destroying the view (`:727-743`).
- `if (!isShowing()) return {}`: hidden windows cost nothing (`:838`).

### 3.14 Multi-instance view-id allocation (`BgfxContext.cpp:133-255`)
The primary window uses view 0 and the default backbuffer. Others get ids 1..16 from `viewUsed_[]` and a per-window `createFrameBuffer(nwh, w, h)`.
Closing the primary rebuilds bgfx on survivor 0 and re-acquires the rest. If that fails, every survivor sees `owns()==false` and reattaches.
Shaders are held so each open/close cycle does not leak handles against the 512 ceiling.

### 3.15 Backing scale (`:697-724, 759-787`)
`refreshDrawableScale()` runs every frame and on `moved()`. If `getBackingScale(view)` differs from `attachedScale_`, it recomputes `physW_/H_` and calls `resizeWindow`
(which is `bgfx::reset` for the primary and a framebuffer rebuild for the others). `HRVB_UI_SCALE[_AFTER]` fakes a display change for capture. The canvas takes `dpi = physH/logicalH`.

### 3.16 Canvas dump / key replay
- `HRVB_CANVAS_DUMP=path` with `_AFTER=n` → `SdfCanvas::dumpNextFrameTo` plus `FramePump::forceFallbackClock()`, so the capture doesn't depend on the window being in front (`:136-142`).
  Dump format: `clear rrggbb`, `view W H dpi s clock … fps … rate …`, then one line per primitive
  `p x0 y0 x1 y1  c0 c1  d0[4]  e0 (v2's d0.xy)  d1[4]  d2[4]` (`SdfCanvas.cpp:290-319`). The dump is a process-global, one-shot static.
- `HRVB_UI_KEYS="tab,shift+tab,up,cmd+a,x,return,…"` drives the real `keyPressed` once before the first frame (`:880-887, 1914-1956`). `HRVB_UI_BROWSER=1` opens the preset browser first.
- `Scripts/capture-frame.sh` launches the Standalone with `HRVB_CANVAS_DUMP_AFTER=8 HRVB_UI_THEME=0` and a scratch preset DB, then waits up to 20 s.
  `verify-gui` = FontProbe + PrefsCheck + three FrameRender fingerprints (default / `HRVB_UI_VIEW=filter` / `=mod`) checked against `Tools/golden/` (`CMakeLists.txt:582-602`).
  The golden machinery is `Tools/Harness.h` (`Metric`, `addNum`, `addHash`, `checkGolden`, `finish`, `--bless`).

### 3.17 Audio → UI: seqlock UiFrame (`Source/dsp/FDNReverb.h:20-44, 297-318`; `FDNReverb.cpp:1190-1272`)

```cpp
// Published once per processBlock, only while an editor exists (uiAttached_, relaxed load hoisted per block).
struct UiFrame { uint32_t publishCount; int32_t order, liveOrder, warmupPushed; float sampleRate;
                 float currentDelay[32], feedback[32], lineEnergy[32]; float freeze, preDelaySamples, dryRms, wetRms; };
std::array<std::atomic<uint32_t>, sizeof(UiFrame)/4> uiWords_;  std::atomic<uint32_t> uiSeq_;   // slot = atomic WORDS (TSan-clean)
// writer (audio thread)
memcpy(words, &stage, sizeof words);
uiSeq_.fetch_add(1, release); atomic_thread_fence(release);            // odd = writing
for (k) uiWords_[k].store(words[k], relaxed);
atomic_thread_fence(release); uiSeq_.fetch_add(1, release);            // even = stable
// reader (message thread), up to 8 attempts, else reuse last frame
s0 = uiSeq_.load(acquire); if (s0 & 1) continue;
for (k) words[k] = uiWords_[k].load(relaxed);
atomic_thread_fence(acquire); if (uiSeq_.load(relaxed) != s0) continue;
memcpy(&dest, words, sizeof words); return true;
```

The processor passes it through (`PluginProcessor.h:67-74`: `readUiFrame`, `setUiAttached` keyed to editor lifetime).
Editor consumption (`BgfxEditor.cpp:934-952`): read every frame. A **new `publishCount`** resets `liveStaleSec_`, otherwise it accumulates dt.
`isLive() = liveValid_ && liveStaleSec_ < 0.5f` (`BgfxEditor.h:175`). When the stream isn't live, **the display falls back to a model
computed from the parameters** with the same expressions the DSP uses, so the panel is correct with no audio running.
Envelopes on the DSP side are instant attack with a 40 ms release per block (`FDNReverb.cpp:1197-1210`). The UI eases the caps with τ = 50 ms.
Full rate is requested while `wetRms > 3e-4` (about −70 dBFS) (`BgfxEditor.cpp:1059-1064`).
**For FCompressor:** reuse this for the instantaneous state (detector dB, GR dB, in/out RMS and peak, operating point). Add the SPSC ring from §2.6 for history.

### 3.18 Frame loop and rate policy
`FramePump::tick` (`FramePump.cpp:147-194`): clamp dt, call each client's `submitFrame(dt)` over a snapshot of the client list, **one `bgfx::frame()` if anything was submitted**, then `requestRate(anyFullRate)`.
Full rate: link range 30–120 Hz, preferred 60. Idle: 8–12 Hz. HR measured 12.3 fps idle against 62.5 fps active, which was 7.8% against 14.4% process CPU (README 431-437).
Easing constants in the editor: hover 90 ms in and 160 ms out, "No overshoot, ever." (`:917-919`). Displayed value τ 90 ms, **snapped for jumps > 0.15** so a preset recall doesn't sweep (`:925-928`).
Freeze τ 350 ms, smear 200 ms, caps 50 ms. All are wall-clock seconds, never frame counts.

---

## 4. Font atlas

- Constants (`FontAtlasSdf.h:15-20`): `kAtlasW 1024`, `kAtlasH 512`, R8, `kBasePx 48`, `kSpread 6` texels, ASCII 33..126 (94 glyphs; the space is an advance only).
- **`kExtraChars` (`FontAtlasSdf.h:25-35`), 9 entries:** U+00B0 °, U+00B1 ±, U+00D7 ×, U+2013 –, U+2192 →, U+221E ∞, U+00B7 ·, U+2191 ↑, U+2193 ↓.
  That makes **103 baked glyphs**. The subset TTF has 104 cmap entries (103 plus space) and is 10,288 bytes (upstream 114,904).
- Occupancy: **43% of the atlas for 103 glyphs, 2.3× headroom** (README 256-257). Shelf packing puts about 27 glyphs of ~37 px width in a 1024-wide row, and a row is about 55 px tall at a 48 px bake.
  So each extra row of ~27 glyphs costs about 11%. **Adding 20–30 glyphs is safe.** Overflow makes `bake()` return false and **all text disappears** (`FontAtlasSdf.cpp:262-269`).
- Extras are looked up with a linear search (`glyph()`, `:67-74`). Keep the list short, though a 30-entry scan per glyph is still trivial.

**Adding a character requires all four of these:**
1. Append the codepoint to `kExtraChars`.
2. Regenerate the committed subset (README 277-284), adding `U+XXXX` to `--unicodes`:
   ```sh
   python3 -m venv /tmp/fontenv && /tmp/fontenv/bin/pip install fonttools
   /tmp/fontenv/bin/pyftsubset Resources/fonts/upstream/JetBrainsMono-Regular.ttf \
     --output-file=Resources/fonts/JetBrainsMono-Regular-subset.ttf \
     --unicodes="U+0020-007E,U+00B0,U+00B1,U+00B7,U+00D7,U+2013,U+2191,U+2192,U+2193,U+221E,<NEW>" \
     --layout-features='' --no-hinting --name-IDs='0,13,14' --drop-tables+=DSIG --notdef-outline
   ```
3. `FontProbe <upstream.ttf>` proves the subset bakes bit-identically to upstream (README 294-303).
   Then **re-bless** `fontprobe.txt` (the atlas hash changes because the packing moves) and the FrameRender goldens (glyph UVs are part of the geometry hash).
4. Remember that `PresetPanel::printable()` maps anything outside ASCII 32..126 to `?` (`PresetPanel.cpp:~64-73`), so typed names stay ASCII.

fontTools is not installed on this machine. I checked cmap coverage in the upstream JetBrains Mono file with a small parser.
**Codepoints a compressor UI will likely want, all present in upstream JetBrains Mono but not in the shipped subset:**

| CP | Glyph | Use |
|---|---|---|
| U+00B5 | µ | **attack in µs** (1176-style 20–800 µs) |
| U+2212 | − | real minus in "−12 DB" (HR uses ASCII `-` today) |
| U+2026 | … | ellipsis (PresetPanel fakes it with `..`) |
| U+2264 / U+2265 | ≤ ≥ | "≥ 20:1", limits |
| U+2248 | ≈ | approximate readouts (e.g. "≈ 3 DB GR") |
| U+2190 / U+2194 | ← ↔ | stereo link, direction |
| U+25B2 / U+25BC | ▲ ▼ | over/under indicators |
| U+25CF | ● | operating-point legend / LED |
| U+0394 | Δ | delta (makeup delta, ΔGR) |
| U+2014 | — | em dash |
| U+2022 | • | bullet |
| U+00BD | ½ | rarely (half-dB labels) |

Already covered: ∞ (∞:1 / "ALL"), × (multipliers), ± (knee ±), · (separators), → (in→out), ↓ (GR), ↑, °, –. "dB" is written "DB" by house style (all caps).
Not in upstream (do not add): U+2206 ∆ increment, U+25CB ○, U+21BA/21BB ↺↻, U+2009 thin space, U+2217.

---

## 5. Theme tokens and design rules

### 5.1 Tokens (`Source/gui/Theme.h:11-57`)

| Token | Graphite (dark, idx 0) | Paper (light, idx 1) | Job |
|---|---|---|---|
| `ground` | `#16171A` | `#EDEBE6` | window ground **and** clear colour |
| `ink100` | `#ECEAE4` | `#17181A` | values, live caps, active state |
| `ink70` | `#B4B8BE` | `#5B5E62` | rank shell strokes, hovered labels, active theme cell |
| `ink52` | `#8A8E95` | `#777776` | labels, units, section captions |
| `ink32` | `#575C63` | `#B0AFAB` | at-default values, inactive numerals, sub-readouts |
| `ink16` | `#33373C` | `#D3D2CE` | datum rule, tracks, ghost strokes (scenery only) |
| `accent` | `#FF5A1F` | `#E8451A` | "the control under the hand. Nothing else." |
| `accentDim` | `#7A3A22` | `#F2BBA6` | drag/hover track fill |
| `ice` | `#7FD4E8` | `#1E7E96` | "reserved for exactly one thing: Freeze" |
| `textGamma` | `1/1.4` | `1.4` | per-polarity text coverage correction |

`kCount = 2`. Names "GRAPHITE" and "PAPER". The comment says "nine ink levels plus two accents" (`:7`), but the struct actually has **9 colour tokens**: ground, 5 inks, accent, accentDim and ice.
Tag colours (`TagPalette.h`) are deliberately **not** theme tokens: they keep the hue and shift the lightness, and none equals the accent.

### 5.2 Design rules stated in code and README
1. **Depth is ink percentage only. No gradients, shadows or bevels** (`Theme.h:7-10`; README 337-338). `softness` exists but is never used.
2. **The vocabulary is rectangle and text** (README 337), plus the **capsule segment for curves** (README 630-632; `SdfCanvas.h:43-48`).
3. **The accent has exactly three jobs:** the control under the hand, the live Order, and the warmup overlay. "A fourth would dilute all of them" (`BgfxEditor.cpp:1115-1118`; README 385-388).
   In practice the accent is also used for the keyboard focus ring, the curve the touched control shapes in a view, and preset-panel editing affordances (caret, rule, selected row, armed delete, drop border).
4. **`ice` has exactly one job (Freeze).** FCompressor should keep a single-job special token and define what that job is. Candidates are GR or "limiting".
5. **A theme is a pure token swap. No conditionals in drawing code** (`Theme.h:33-37`; `BgfxEditor.h:195-196`). The ink ladder always runs from most contrast (100) to least (16) on either ground.
6. The clear colour is derived from `ground` (`SdfCanvas.cpp:74-80`). Text gamma flips with polarity.
7. The theme selector is not accented: active ink70, hovered ink100, inactive ink32. ink16 is for scenery, never for a control someone has to find (`BgfxEditor.cpp:1119-1127`).
8. **Type:** hierarchy comes from size, case, tracking, colour and SDF weight, never from a second face. **10 px is the floor** (`TypeScale.h:3-6`). All-caps labels and units. Tabular digits.
9. Hairlines are snapped to device pixels (`SdfCanvas.h:37-41`).
10. **One fixed window. All parameters permanently visible. No disclosure. No resize after construction** (`BgfxEditor.h:17-18`; `:143-146`).
11. **Fixed axes that never auto-scale** (`BgfxEditor.cpp:29-31`; README 347-348). This applies directly to dB axes and GR-history scales.
12. **Displays are truthful:** computed from the DSP's own expressions, falling back to a parameter-derived model without audio (README 350-362; `BgfxEditor.cpp:1209-1217`).
    Values are **absent rather than fake**: pre-delay at 0 draws nothing (`:1460-1461`), and LO CUT prints "OFF" (`:633-638`).
13. Motion: 90 ms in, 160 ms out, **no overshoot**. Big jumps snap. Time constants are in seconds, never frames.
14. No tooltips. A persistent footer spec line replaces them (`:1574-1577`). The cursor shape is the affordance.
15. Bipolar controls fill from the default notch. At-default values print in ink32.
16. Popup menus borrow the theme and the bundled face (`PresetPanel.cpp` `MenuLook`), because they are the one place that leaves the canvas.

---

## 6. Recommendation: sharing the code with FCompressor

### 6.1 Constraints that decide it
- HR is **read-only** for this effort, and it was **being edited while this report was written**. Anything that edits HR now (includes, CMake, namespace) is off the table and would conflict with the work in progress.
- Every symbol in the plugin is compiled with `-fvisibility=hidden` (`JUCEUtils.cmake:1439-1441, 1588-1590`), and macOS bundles use two-level namespaces. **C++ names cannot collide between two plugins loaded in one host**, so sharing the namespace is safe.
  bgfx is statically linked into each plugin, which gives each binary its own independent bgfx singleton, `FramePump` and `BgfxContext`. bgfx's own Metal renderer defines no ObjC classes (checked: only its `examples/` do).
- **ObjC class names are process-global.** `HrvbDisplayLinkTarget` and `HrvbRenderView` would be defined by both binaries. The runtime then warns "implemented in both" and picks one arbitrarily, so two diverging copies become a real bug.
  **This is the one mandatory rename, whatever the sharing model.**

### 6.2 Options compared

| | (i) Fork into `FCompressor/Source/gui` | (ii) Shared sibling lib consumed by **both** now | (iii) **Shared sibling lib, FCompressor first; HR adopts later** |
|---|---|---|---|
| What | Copy 19 gui C++/ObjC files + 3 shaders + font + 4 GUI tools + `Harness.h` + `capture-frame.sh`, then rename | New `/Users/seanfunk/audio/plugins/FunkSdfUi/` (name TBD), `add_subdirectory` from both plugins, HR's `Source/gui` deleted | Same library as (ii), seeded by copying HR's files and adding the seams. Only FCompressor consumes it. HR switches later behind its own goldens |
| HR risk | none | **high**: edits a polished product that is in active development. Needs the `verify-gui` goldens to prove zero geometry change | none now. Later migration is gated by `layout.geometry`/`font.atlas` hash equality |
| Drift | Fixes must be ported by hand. HR's comments record ~15 subtle lifecycle bugs fixed (re-parenting, primary rebuild, leaks, occlusion); a future one would be fixed twice | none | FCompressor-side improvements (KIND_AREA, live flag, indexed quads) happen in the lib. HR converges when it migrates |
| Effort | ~0.5 day mechanical, plus widget extraction in FCompressor (1–2 days) | (iii) + ~1 day of HR edits and re-blessing | ~1 day for the lib and seams + 1–2 days lifting widgets (§6.5) |
| Second-plugin reuse | copy again | yes | yes |

**Recommendation: (iii).** Build the library as a sibling directory now, consumed only by FCompressor, and seed it from HR's current `Source/gui`.
Keep HR byte-for-byte untouched. Its later adoption is a separate, reversible task gated on `verify-gui` producing identical `layout.geometry` and `font.atlas` hashes.
One possible downside: the user asked for "the same libraries/backend code". Right after the fork (iii) *is* the same code, and it becomes literally shared once HR migrates.

### 6.3 Packaging the library
Ship it as a **JUCE module** (`juce_add_module(<path>/sdfui)`) or an **INTERFACE library** (`add_library(sdfui INTERFACE)` + `target_sources(sdfui INTERFACE …)`).
Do **not** use a STATIC library: the sources include `juce_gui_basics`, `juce_events` and `juce_data_structures`, and must compile with the consuming plugin's JUCE config flags to avoid ODR mismatches. JUCE's own modules work the same way.
The library's CMake should *expect* the targets `bgfx bx bimg shaderc juce::…` (guard with `if(NOT TARGET bgfx)`) rather than FetchContent them itself. It should provide:
- `sdfui_compile_shaders(<target>)`: `hrvb_compile_shader` generalised, output to `${CMAKE_CURRENT_BINARY_DIR}/generated/shaders`, plus include dir and dependency.
- `sdfui_add_font(<target>)`: `juce_add_binary_data(SdfUiFonts HEADER_NAME SdfUiFonts.h NAMESPACE sdfuifonts SOURCES <subset.ttf> <LICENSE.txt>)`, plus the licence as a bundle resource (README 636-653 explains why a resource and not a post-build copy).
- `sdfui_add_gui_tools(<prefix>)`: AtlasDump, FrameRender, PrefsCheck, FontProbe and a `verify-gui` template.
- Out-of-tree `add_subdirectory` needs an explicit binary dir: `add_subdirectory(../FunkSdfUi ${CMAKE_BINARY_DIR}/sdfui)`.

### 6.4 Exact seams (API parameters the library must expose)
1. **ObjC class prefix** (mandatory): a compile definition `SDFUI_OBJC_PREFIX=Fcmp`. In the `.mm` files:
   `#define SDFUI_CAT2(a,b) a##b` / `#define SDFUI_CAT(a,b) SDFUI_CAT2(a,b)` / `#define SdfRenderView SDFUI_CAT(SDFUI_OBJC_PREFIX, RenderView)`, and the same for `DisplayLinkTarget`.
2. **App folder for preferences:** `UiPreferences::configure(const char* folderName)` must be called before the first `get()`, or use a compile definition.
   **Decision needed:** per plugin (`FCompressor`), or a shared `Funk` folder so one theme choice applies to every Funk plugin. Shared is attractive, but HR currently uses `HardwareReverb`.
3. **Env-var prefix:** `sdfui::env("CANVAS_DUMP")` reads `<prefix>CANVAS_DUMP`, with the prefix set by `SDFUI_ENV_PREFIX="FCMP_"`. Then HR can keep `HRVB_` when it migrates.
   Cache the lookups: HR calls `getenv` every frame at `:826` and `:998`.
4. **Namespace:** `sdfui` (neutral, one for all consumers; hidden visibility makes that safe). `hrvbgui::type` becomes `sdfui::type`.
5. **Font:** `BundledFont` reads `sdfuifonts::JetBrainsMonoRegularsubset_ttf`. Use the extended `kExtraChars` (§4) and the regenerated subset.
6. **Theme:** rename `ice` → `signal` (single-job token, meaning defined per product). Keep `graphite()`/`paper()`. Optionally allow a product accent override.
7. **Transient budget:** `BgfxContext::configure({ .transientVbBytes = 32<<20, .maxWindows = 16 })`.
8. **FrameRender:** read the view size from the dump (it already parses `view`). Replace the Rank y-band exclusion with the `d2.w` live flag. Add KIND_AREA.
   `capture-frame.sh` takes the app name and env prefix as arguments.

### 6.5 Generic logic to lift out of `BgfxEditor.cpp` (a multi-plugin toolkit)

| Component | Lift from | API sketch | Notes for FCompressor |
|---|---|---|---|
| **EditorShell** (base class) | ctor/dtor `:131-175`; surface `:650-787`; fallback `paint :789-812`; `submitFrame` gating `:815-900`; `replayKeys`; a11y dump; canvas dump; theme revision poll | `class EditorShell : AudioProcessorEditor, FrameClient, Timer { virtual void layout(); virtual void tick(float dt); virtual void draw(SdfCanvas&); virtual bool wantsFullRate() const; virtual const char* productName() const; }` | Keeps all the hard-won lifecycle fixes in one place. Holds a member `SdfCanvas` (no per-frame realloc) |
| **RuleSlider** (Control) | `Control` struct; `drawControls :1483-1553`; drag `:1678-1790`; wheel `:1805-1848`; keys `:1850-1908`; double-click `:1792-1803`; right-click host menu `:1692-1700` | `struct RuleSlider { param; label; Rect track, hit; bool bipolar; Formatter fmt; ValueModel* vm; float hoverAmt, shown; Texts cache; }` | Add a per-Mode **`ValueModel`**: continuous, or **detents** (list of normalised values plus labels) for locked-increment Modes. Drag snaps to the nearest detent with hysteresis; wheel and keys step between detents (fixing HR's key gap). The formatter comes from the Mode |
| **SegmentedSelector** | Order cells: layout `:224-226`, draw `:1100-1113`, hit `:1596-1601`, click `:1709-1722`, a11y `radioButton` | `SegmentedSelector{ param or ValueModel; std::vector<const char*> labels; Rect cells[] }` | **This is the 2-4-10 ratio button array, the Mode picker, and the 1176 "ALL"/∞ button.** Style: active in accent, hover ink100, rest ink32 |
| **LatchToggle** | Freeze: arm on mouseDown, commit on mouseUp inside, drag-off cancels (`:1724, 1774-1788`); draw `:1177-1185`; a11y `toggleButton` | `LatchToggle{ param; Rect; labels on/off; Col onFill }` | Bypass, SC listen, auto-release, stereo link |
| **ThemeCells** | `:236-237, 1115-1131, 1703-1707` | `ThemeCells{ Rect[kCount] }` | verbatim |
| **AccessibleItem** | `:249-424`, `build/refresh/dump :426-495` | Generic kinds `{slider, radio, toggle, button, listItem}` bound to widget callbacks (`isOn`, `press`, `value`) rather than to editor fields | Every widget registers its own item |
| **GestureController** | `endDragGesture`, `endWheelGesture`, wheel `Timer` (500 ms), gesture-in-dtor | `beginDrag(param)`, `wheel(param, dv)`, `tap(param, v)`, `closeAll()` | One per editor |
| **DwellSelector / BandView** | view target/shown/amt/source/dwell `:983-1019`; display source dwell | `DwellSelector<View>{ tau = 0.18, dwell = 0.9 }` | Drives the characteristics screen views |
| **Ease helpers** | the `x += (t-x)*jmin(1, dt/tau)` idiom repeated ~8×, `sameBits`, `fade` (duplicated `BgfxEditor.cpp:83` and `PresetPanel.cpp:56`) | `sdfui::ease(x, t, dt, tau)`; move `fade` into `Col.h` | |
| **LiveFeed** | UiFrame staleness logic `:934-952` | `template<class F> struct LiveFeed { bool read(fnRead, dt); bool live() const; }` + SPSC `HistoryRing` | |
| **Text utils / LineEdit / MenuLook** | `PresetPanel.cpp` `printable`, `fit`, `editKey`, `MenuLook` | `sdfui::text::fit()`, `LineEdit`, `ThemedMenuLook` | Needed if FCompressor adds preset naming or typed value entry |
| **Footer spec line / first-run hint / always-chrome** | `:1488, 1556-1586, 1022-1024, 1668` | `HintLine` | Cheap wins, carry over as they are |

Keep product-specific: layout constants, formatters, `recomputeRank`, views, wordmark and footer strings, `PresetPanel` geometry.
The preset system (`PresetPanel` + `presets/`) is its own sharing question. It is coupled to `HardwareReverbProcessor`, `.hrvbpreset`, `HRVB_PRESETS_DB` and the 880×520 header geometry, and would need a `PresetHost` interface.

### 6.6 Things FCompressor must decide early (surfaced by this map)
- **Window model:** HR's rule is one fixed window with everything visible. A "characteristics screen" fits that rule as a **band with views** (§3.8), not a separate page. A page switch would break the rule.
- **Special token:** what gets the single-job `signal` colour. GR is the obvious candidate. Accent stays on "the control under the hand".
- **Primitive additions:** KIND_AREA plus the live flag plus a raised transient budget. Land all three in the library together with the FrameRender changes.
- **Preferences folder:** per plugin or shared.
- Sibling plugins (`GlueCompressor`, `FunkPluginTemplate`, `SampleAtlas`, …) use **visage**, not this stack. No existing shared library conflicts with the proposal.
