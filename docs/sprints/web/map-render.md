# Web demo scout report: Drawing the frame in a browser

Written by a read-only scout before Sprint A (ADR-93 design pass, 2026-10-01). Line numbers are as of main 3731e07.
Statements about WebAssembly behaviour were not built unless the report says a scratch test ran.

## Summary

Recommend option B: a small dedicated WebGL2 sink in FunkGui, fed by the existing `funkgui::expand` vertex stream and a fragment shader generated from the same `shaders/fs_ui.sc` text. Option A (bgfx on WebGL2) is feasible, but it does not actually keep the GPU layer unchanged, adds roughly 0.5-1 MB of wasm (my estimate, not built), and has two web-specific hazards (canvas alpha, no context-loss handling).

**How a frame is drawn today.** A frame is one draw call: `expand` turns each 84-byte Prim into six 64-byte vertices, copied into one bgfx transient buffer, drawn with one program, one vec4 uniform, one R8 1024x512 atlas texture and plain alpha blending. The fragment shader is 153 lines with four branches and uses no derivatives, no instancing, no index buffer and no MRT. It needs only `textureLod` and an R8 texture, both core in WebGL2.

**What I tested.** I ran two checks outside the repositories (scratchpad only; both repos are still clean):
- The pinned prebuilt shaderc compiles both shaders with `--platform asm.js -p 300_es` (fs 6315 B, vs 785 B).
- In Chromium WebGL2 (ANGLE Metal, Apple M5), both the shaderc ES output and a 9-line preamble plus the verbatim body of `fs_ui.sc` compile and link. Drawing one prim of each kind through the 64-byte layout gave identical pixels for the two variants (0 of 16384 samples differ).

**Option A, bgfx to WASM.**
- **Supported upstream, but experimental.** bgfx.cmake lists Emscripten as "experimental", turns multithreading off and adds `-sMAX_WEBGL_VERSION=2`; bx recognises Emscripten 6.
- **Not unchanged.** `BgfxSink.cpp` compiles as is, but `BgfxContext.cpp`, the shader profile list, the `fg.shader.hash` goldens, the platform guard in `FunkGuiDeps.cmake` and the `FunkGuiGpu` target (which globs the JUCE-based EditorHost, FramePump and A11yBridge) all need a web branch or a new target.
- **Alpha hazard.** bgfx creates the canvas with `alpha: true, premultipliedAlpha: false`, and the sink writes alpha with SRC_ALPHA blending, so antialiased edges would let the page show through unless a pre-made `alpha: false` context is passed in. This is derived from the code, not observed.
- **Other costs.** No WebGL context-loss handling in bgfx; needs `GL_ENABLE_GET_PROC_ADDRESS`; the 32 MiB transient buffer default must be lowered.

**Option B, dedicated sink.**
- About 150-200 lines: one program, one stream-draw buffer, six attribute pointers at the existing offsets, one R8 texture, `blendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)`, and a context created with `alpha: false, antialias: false`.
- The shader is generated at build time from `fs_ui.sc` with all-highp precision, so it cannot drift from the native one and shaderc is not needed for the web build.
- Context loss, the no-WebGL2 fallback and canvas sizing stay in our hands.

**Parity.** The recorded PrimList and vertex stream are the same code in both options, so parity is decided by the atlas pixels and GPU float differences, not by the choice of A or B. Bit-exact GPU output against Metal is not attainable either way; the PrimList dump and SoftRaster remain the oracles.

**HiDPI and zoom (identical for both).** Canvas CSS size is 960x640 times zoom; the backing store is that times `devicePixelRatio`; `info.dpi = physH / 640`, exactly as EditorHost computes it. Fractional dpi is already exercised natively by the 125/150/175 % zoom steps.

Scratch evidence: `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/glsl_check.html`, `shaders.json`, `{vs,fs}_ui.{100,300}_es.bin`.

## Facts

- **A recorded primitive is 84 bytes (quad corners, two RGBA8 colours, d0/e0/d1/d2 floats, CPU-only tag) with four kinds: rrect, text, segment, area.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/include/funkgui/canvas/Prim.h:16, :29-40
- **The GPU vertex stream is produced by a JUCE-free, bgfx-free function: six non-indexed 64-byte vertices per Prim (offsets 0, 8, 12, 16, 32, 48).**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/include/funkgui/canvas/Expand.h:20-36; /Users/seanfunk/audio/libraries/FunkGui/src/canvas/Expand.cpp:3 (only <cstddef>), :11-13, :15-53
- **BgfxSink::submit is one draw call per frame: view rect/clear/sequential/touch, expand, one transient vertex buffer, u_viewSize = (logicalW, logicalH, 1/dpi, seconds), one texture, state WRITE_RGB | WRITE_A | BLEND_ALPHA, one submit. It has no platform-specific code.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxSink.cpp:35-39, :45-57, :59-66
- **The vertex layout is Position 2 float, Color0 and Color1 4 x uint8 normalised, TexCoord0..2 4 float each. There is a single program (vs_ui + fs_ui), a vec4 uniform u_viewSize and a sampler s_texColor.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxContext.cpp:126-133, :139-144
- **The font atlas is an R8 1024x512 texture with U/V clamp and no mips, uploaded from FontService's CPU bake. bgfx's default filter with no flags is GL_LINEAR.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxContext.cpp:150-160; /Users/seanfunk/audio/libraries/FunkGui/include/funkgui/text/FontAtlasSdf.h:16-17; /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/renderer_gl.cpp:193-205
- **fs_ui.sc is 153 lines, one program branching on v_data2.z. It takes no derivatives (AA width comes from u_viewSize.z) and uses one texture2DLod fetch, pow, inversesqrt, mod/floor, smoothstep. vs_ui.sc is a 10-line pass-through that maps logical px to NDC.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/shaders/fs_ui.sc:12-17, :24, :28, :46, :92, :103, :137-142, :152; /Users/seanfunk/audio/libraries/FunkGui/shaders/vs_ui.sc:13-22
- **FunkGuiShaders compiles only two profiles, Metal and SPIR-V, on every host, with --bin2c. BgfxContext.cpp includes the Metal pair on Apple and the SPIR-V pair otherwise, with the renderer fixed to Metal or Vulkan.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiTargets.cmake:329, :340-347; /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxContext.cpp:13-19, :33-42
- **Adding an ES profile for option A changes the fg.shader.hash goldens: the test includes every profile header and the golden has exact rows per profile (Metal fs 4532 B, SPIR-V fs 6248 B).**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/test/smoke/shader_hash.cpp:1-23; /Users/seanfunk/audio/libraries/FunkGui/test/golden/base/global/fg.shader.hash.txt
- **The pinned shaderc has ESSL profiles 100_es/300_es/310_es/320_es and an 'asm.js' platform. The prebuilt stamped host shaderc compiled both shaders for both 100_es and 300_es with exit code 0 (300_es: fs 6315 B containing 6254 B of GLSL text, vs 785 B).**
  Evidence: /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/tools/shaderc/shaderc.cpp:120-125, :1393-1398; I ran /Users/seanfunk/audio/.deps/tools/shaderc-v1.153.9385-561/shaderc with --platform asm.js -p 300_es, output written to the session scratchpad
- **shaderc's 300_es fragment output declares 'precision highp float' but glsl-optimizer marks the colour temporaries lowp and the output mediump (e.g. 'lowp vec4 col_1', 'out mediump vec4 bgfx_FragColor', 'max(tmpvar_18, 1e-05)' in a lowp expression). bgfx prepends '#version 300 es' and 'precision mediump float' when the code has no #version.**
  Evidence: strings of scratchpad fs_ui.300_es.bin; /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/renderer_gl.cpp:6616, :7015-7022
- **Measured in Chromium WebGL2 (ANGLE Metal, Apple M5): the shaderc 300_es pair, and a 9-line ES 3.00 preamble plus the verbatim fs_ui.sc/vs_ui.sc bodies (directive lines stripped, gl_FragColor renamed), both compile and link with empty logs. Drawing four prims (one per kind) through the 64-byte layout with an R8 linear clamp texture gave zero differing samples out of 16384 between the two. Low, medium and high float precision are all 127/127/23 on this GPU.**
  Evidence: Scratchpad glsl_check.html served once from 127.0.0.1 (server stopped): 'B vs A: differing channel samples=0 of 16384'; 'fragment precision low=127/127/23 medium=127/127/23 high=127/127/23'
- **bgfx.cmake supports Emscripten but calls it experimental: multithreading is forced off, '-sMAX_WEBGL_VERSION=2' is a PUBLIC link option, and the examples still pass legacy flags (PRECISE_F32, --memory-init-file) that Emscripten 6.0.3 lists as legacy.**
  Evidence: /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/CMakeLists.txt:48; cmake/bgfx/bgfx.cmake:69-71; README.md:27; cmake/bgfx/examples.cmake:263-270; /opt/homebrew/Cellar/emscripten/6.0.3/libexec/tools/cmdline.py:210, tools/settings.py:223
- **On Emscripten bgfx enables only its OpenGL ES renderer (WebGPU is commented out for that platform), is single-threaded by default, and decides GLES3 at run time from the GL version string. bx detects Emscripten through emscripten/version.h's __EMSCRIPTEN_MAJOR__, which 6.0.3 defines.**
  Evidence: /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/config.h:88-100, :113-119, :272-274; bgfx/src/renderer_gl.cpp:2356-2373; bx/include/bx/platform.h:208-211; /opt/homebrew/Cellar/emscripten/6.0.3/libexec/cache/sysroot/include/emscripten/version.h
- **bgfx's HTML5 context treats platformData.nwh as a canvas CSS selector string and platformData.context as an optional pre-made WebGL context. It tries WebGL2 then WebGL1, and sets the canvas element size from the resolution at init and on reset.**
  Evidence: /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/glcontext_html5.cpp:85-102, :107-111, :138-142, :152-169
- **Option A alpha hazard (derived from code, not run): bgfx creates the canvas with alpha = (colour format has alpha bits) and premultipliedAlpha = false. The default backbuffer format is BGRA8, and FunkGui's state writes alpha with SRC_ALPHA/INV_SRC_ALPHA, so destination alpha falls below 1 under translucent or antialiased pixels and the page would show through.**
  Evidence: /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/glcontext_html5.cpp:76-83; bgfx/src/bgfx.cpp:4000-4002; bgfx/include/bgfx/defines.h:597, :610-611; /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxSink.cpp:65
- **bgfx imports GL through emscripten_webgl1/2_get_proc_address. That needs GL_ENABLE_GET_PROC_ADDRESS, which Emscripten documents as a 'substantial code size and performance impact' (it defaults to true in 6.0.3).**
  Evidence: /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/glcontext_html5.cpp:17-19, :211-221; /opt/homebrew/Cellar/emscripten/6.0.3/libexec/src/settings.js:637-643
- **bgfx has no WebGL context-loss handling.**
  Evidence: grep -ri 'contextlost|context_lost|webglcontextlost' over /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src returned nothing
- **Option A cannot reuse the GPU layer as is. FunkGuiDeps FATALs unless the system is macOS or Linux. BgfxContext::initBackend returns false on non-Apple when nativeDisplay() is null. FunkGuiGpu is every file under src/gpu, including EditorHost, FramePump and A11yBridge, which include JUCE.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiDeps.cmake:193-196; /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxContext.cpp:83-95; /Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiTargets.cmake:380-388; include/funkgui/gpu/EditorHost.h:53-54; include/funkgui/gpu/FramePump.h:3
- **The host-tool path for shaderc already exists (FUNKGUI_SHADERC prebuilt with a stamp, BGFX_BUILD_TOOLS OFF), so an Emscripten build would not need to cross-compile shaderc.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiDeps.cmake:100-120, :201-202; /Users/seanfunk/audio/plugins/FCompressor/cmake/FcmpDeps.cmake:171-180; /Users/seanfunk/audio/.deps/tools/shaderc-v1.153.9385-561/shaderc.stamp
- **Size indicator for option A (native arm64 object code, not wasm): bgfx.cpp.o is 358 KB of text, the Metal renderer 196 KB, bx 163 KB and bimg 392 KB. The GL renderer source is 9157 lines. A wasm figure of roughly 0.5-1 MB is my estimate; nothing was built.**
  Evidence: `size` on /Users/seanfunk/audio/plugins/FCompressor/build-release/_deps/bgfx-build/cmake/{bgfx/libbgfx.a,bx/libbx.a,bimg/libbimg.a}; wc -l /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/renderer_gl.cpp
- **FunkGui defaults the transient vertex buffer to 32 MiB. A frame is typically 2.0K prims (768 KB of vertices), 6.9K at worst (2.65 MB).**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/CMakeLists.txt:45; /Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxContext.cpp:104; /Users/seanfunk/audio/plugins/FCompressor/docs/design/02-funkgui-and-ui.md:953-966
- **dpi is physical height / logical height (backing scale times zoom). The Canvas snaps hairlines and text AA to device px from info.dpi, and the sink only needs the physical size and 1/dpi. Zoom keeps the Panel at 960x640 logical with steps 100/125/150/175 (default 125), so fractional dpi is already a native case.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/src/gpu/EditorHost.cpp:513-535, :548; include/funkgui/gpu/EditorHost.h:16-31; src/canvas/Canvas.cpp:202-211, :250-257, :293; /Users/seanfunk/audio/plugins/FCompressor/Source/editor/Layout.h:43-44, :573-574; Source/editor/gpu/Editor.cpp:52-54
- **The font atlas bake uses JUCE (Typeface, Font, GlyphArrangement), so neither option has an atlas in a JUCE-free build. The atlas pixels already differ per native platform (different golden hashes on macOS and Linux).**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/src/text/FontAtlasSdf.cpp:3, :83-125; src/text/FontService.cpp:38-50; /Users/seanfunk/audio/plugins/FCompressor/tests/golden/base/global/ui.font.txt (b744c79b9bb755d0) vs ui.font_linux.txt (8d0078b1bae2d442)
- **A CPU oracle for a browser parity check exists: SoftRaster mirrors fs_ui.sc's four kinds and uses JUCE only in writePng. The PrimList dump writer is std-only.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/include/funkgui/canvas/SoftRaster.h:3-5, :24; src/canvas/SoftRaster.cpp:3, :264-283; src/canvas/PrimList.cpp includes (std only)
- **FunkGui rules that bear on where a sink lives: bgfx and Objective-C code stay under gpu/, and a public header is API that needs the lead's approval and a MINOR bump.**
  Evidence: /Users/seanfunk/audio/libraries/FunkGui/CLAUDE.md:45-48

## Blockers and options

- **The font atlas cannot be baked without JUCE. FontAtlasSdf::bake rasterises glyphs through juce::Typeface, Font and GlyphArrangement, and there is no other way to fill the private glyph tables and pixels. Both sink options need the R8 pixels and the glyph metrics.** (/Users/seanfunk/audio/libraries/FunkGui/src/text/FontAtlasSdf.cpp:83-125; include/funkgui/text/FontAtlasSdf.h:54, :97-109; src/text/FontService.cpp:38-50)
  Options: (1) Pre-bake on macOS with a host tool and embed the blob (1024x512 R8 = 524,288 B raw, plus glyph tables), loaded through a new FontAtlasSdf entry point; the web build can assert hash b744c79b9bb755d0 for text parity with the macOS editor. (2) Bake in wasm with stb_truetype or FreeType: no blob, but different pixels and a third golden hash. I recommend (1).
- **There is no JUCE-free FunkGui target for the sink to sit on. FunkGuiCore links juce_gui_basics, juce_data_structures and juce_audio_processors and globs JUCE-including sources; the bundled font comes from juce_add_binary_data. This is shared by options A and B and is outside the drawing area proper.** (/Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiTargets.cmake:234-258, :273-277, :304-310; JUCE includes in src/text/FontAtlasSdf.cpp, src/text/LineEdit.cpp, src/canvas/SoftRaster.cpp, src/prefs/UiPreferences.cpp, src/params/JuceParamPort.cpp)
  Options: (1) A new Emscripten-only target (e.g. FunkGui::web) with an explicit JUCE-free source subset plus src/web/**, filtered the way src/<module>/linux/** is at FunkGuiTargets.cmake:243-249. (2) Split the JUCE-touching files of FunkGuiCore behind a platform filter so that Core itself becomes JUCE-free on Emscripten.
- **The sink is a FunkGui API addition (new public header, new target, and a shader-generation step for option B, or a new shader profile and goldens for option A). That needs the lead's approval, a FunkGui MINOR tag and a pin bump in FCompressor, which only the lead does.** (/Users/seanfunk/audio/libraries/FunkGui/CLAUDE.md:45-48; /Users/seanfunk/audio/plugins/FCompressor/CLAUDE.md (Lead: bumps the FunkGui pin; FunkGui is consumed only as a tag))
  Options: (1) Put WebGlSink in FunkGui as include/funkgui/web/WebGlSink.h and src/web/WebGlSink.cpp, tag e.g. v0.12.0, bump the pin. (2) Keep the sink in FCompressor's web target at first and upstream it later; faster, but HardwareReverb cannot reuse it and it sits outside FunkGui's tests.
- **The frame needs WebGL2 (R8 textures, textureLod and ES 3.00 are core there). On WebGL1 the atlas would be LUMINANCE and textureLod would need EXT_shader_texture_lod; no such path exists or was tested.** (/Users/seanfunk/audio/libraries/FunkGui/src/gpu/BgfxContext.cpp:155-159; shaders/fs_ui.sc:92; /Users/seanfunk/audio/.deps/bgfx.cmake-v1.153.9385-561/bgfx/src/renderer_gl.cpp:2758-2769)
  Options: (1) Require WebGL2 and show a static fallback (a screenshot or a message) when getContext('webgl2') is null or the context is lost. This is my recommendation. (2) Add a WebGL1 path: more shader variants, and parity is not covered.
- **Mobile GPU precision is unverified. I tested one desktop GPU where lowp, mediump and highp are identical. shaderc's ES output (the option A path) carries lowp/mediump colour temporaries and a 1e-5 guard in a lowp expression, which may underflow where mediump is 16-bit.** (Scratchpad fs_ui.300_es.bin ('lowp vec4 col_1', 'max (tmpvar_18, 1e-05)', 'out mediump vec4 bgfx_FragColor'); /Users/seanfunk/audio/libraries/FunkGui/shaders/fs_ui.sc:81-84, :148-149)
  Options: (1) Option B with an all-highp preamble over the fs_ui.sc text, which avoids the inferred qualifiers. (2) If option A is chosen, test on one Android and one iOS device before launch. (3) Declare the demo desktop-only.

## Recommended approach

Take option B: add a WebGL2 sink to FunkGui that mirrors BgfxSink's contract and reuses everything upstream of it.

1. **Sink.** `include/funkgui/web/WebGlSink.h` and `src/web/WebGlSink.cpp`, compiled only under Emscripten, with `submit(const PrimList&, int physW, int physH)`:
   - `funkgui::expand(list, scratch_)`, then `glBufferData(GL_ARRAY_BUFFER, n * 64, GL_STREAM_DRAW)`.
   - Six attribute pointers at stride 64: position 2f at 0; colour0 and colour1 4ub normalised at 8 and 12; texcoord0..2 4f at 16, 32, 48.
   - `u_viewSize = (logicalW, logicalH, 1 / dpi, seconds)`.
   - Viewport `physW x physH`, clear to `info.clear` with alpha 1, blend `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, no depth test, no culling, `glDrawArrays(GL_TRIANGLES, 0, n)`.
   - Use Emscripten's GLES3 bindings with `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2` and without the get-proc-address path.

2. **Context.** Create it with `alpha: false, antialias: false, depth: false, stencil: false`. This matches the opaque native layer and removes the page-bleed hazard. Handle `webglcontextlost` and `webglcontextrestored` by rebuilding the program, buffer and texture. Show a static fallback when WebGL2 is missing.

3. **Shaders.** Generate both at build time from `shaders/fs_ui.sc` and `shaders/vs_ui.sc`, so there is still one shader source:
   - Strip the `$input`/`$output` and `#include <bgfx_shader.sh>` lines.
   - Prepend an ES 3.00 preamble: `#version 300 es`; highp float, int and sampler2D; `#define vec2_splat(x) vec2(x)`; `#define texture2DLod textureLod`; `#define SAMPLER2D(n, s) uniform sampler2D n`; `in`/`out` declarations.
   - Rename `gl_FragColor`.
   - This exact transformation compiled, linked and matched shaderc's ES output pixel for pixel in my Chromium test.
   - Add a hash row for the generated text beside `fg.shader.hash` so a shader change shows up for the web too.

4. **Atlas.** `glTexImage2D(R8, 1024, 512, RED, UNSIGNED_BYTE)` with linear filtering and clamp-to-edge, from `FontService::atlas().pixels()`. The pixels should come from a macOS pre-baked blob (see blockers).

5. **HiDPI and zoom.** Reproduce EditorHost's rule in the web host:
   - Canvas CSS size is 960z x 640z; `canvas.width/height` is that times `devicePixelRatio`, rounded (use ResizeObserver's `devicePixelContentBoxSize` where available).
   - `info.dpi = physH / 640`.
   - Pointer coordinates are divided by z.
   - Re-evaluate on a `devicePixelRatio` change.
   - Implement the HostServices zoom calls with the existing 100/125/150/175 steps, fitted to the window.

6. **Pacing.** Use `requestAnimationFrame`. Draw only when the Panel wants full rate or an idle tick is due; the canvas keeps its last frame otherwise.

7. **Parity gate.**
   - Diff the wasm build's PrimList dump against native HeadlessHost dumps for the same states (CPU side, exact).
   - Compare a `readPixels` capture with SoftRaster within a small tolerance (GPU side).

Keep option A as the fallback only if a second GPU consumer of bgfx on the web appears. If it is ever taken, it needs: an ES profile in the shader list and goldens, a third BgfxContext branch using a pre-created `alpha: false` context passed through `platformData.context`, a JUCE-free GPU target, and `FUNKGUI_TRANSIENT_VB_MIB` lowered to about 4.

A later optimisation, not needed now: instanced drawing of the 84-byte Prim directly, about 4.6x less upload.

## Effort notes

**Option B, sink only:** small. About 150-200 lines of C++ for WebGlSink, about 30 lines of CMake for the shader text generation and the Emscripten-only source filter, a 9-line preamble, and a hash row. Call it one FunkGui card.

**Web host around the sink:** a second card of similar size, covering canvas sizing and dpi, the `requestAnimationFrame` pump, pointer/wheel/key translation into the Panel's input structs, zoom, context loss and the fallback. Part of it belongs to whoever owns the JUCE-free host.

**Parity harness:** half a card. It needs SoftRaster's `rasterise` built without JUCE (only `writePng` uses JUCE).

**Shared prerequisites, not sink work:** the pre-baked atlas and its loader, and a JUCE-free FunkGui core target. These gate both options.

**Option A for comparison:** similar or more engineering (bgfx under emcmake, a BgfxContext web branch, the alpha workaround, the ES profile and golden rows, a JUCE-free GPU target, memory limits), plus a larger download and no context-loss recovery. The wasm size is an estimate from native object sizes; I did not build anything with emcc.

**What I ran:**
- The prebuilt host shaderc, writing into the session scratchpad.
- One WebGL2 compile-and-draw page, served briefly from 127.0.0.1 and then stopped.
- Neither repository was edited, built or had its git state changed; `git status` is clean in both.

**Not verified:** mobile GPUs, Safari and Firefox, bit-level comparison against the Metal editor, and the real atlas (the browser test used a stand-in R8 texture).
