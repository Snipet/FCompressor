# Web demo scout report: Build, test, CI and hosting

Written by a read-only scout before Sprint A (ADR-93 design pass, 2026-10-01). Line numbers are as of main 3731e07.
Statements about WebAssembly behaviour were not built unless the report says a scratch test ran.

## Summary

A `web` preset is feasible, but nothing JUCE-free beyond `fcdsp` and the DSP probes exists today, and even those do not compile for wasm32. The work splits into a build/CI/hosting part (mine, sketched below) and four prerequisites outside it.

**What stands in the way**
- **fcdsp has no wasm backend.** `Simd.h`, `ScopedFtz.h` and `FastMath.h` hard-error or use raw SSE/NEON on anything but arm64/x86-64.
- **Wasm has no fused multiply-add and no flush-to-zero.** That changes the arithmetic and removes denormal protection.
- **FunkGui core is not JUCE-free.** It links three JUCE modules, embeds the font through `juce_add_binary_data`, and refuses bgfx off macOS/Linux. A new FunkGui release and pin bump are needed.
- **The harness only knows `arm64|x86_64`.** `Harness.h` and `golden.py` both need a `wasm32` arch before any probe can run under node.

**Build**
- Add `FCOMPRESSOR_WEB` as a fourth configuration. Resolve the toolchain inside CMake with `em-config EMSCRIPTEN_ROOT`, so `cmake --workflow --preset web-verify` works with Homebrew and emsdk alike.
- Branch `FcmpPlatform`, `FcmpArch`, `FcmpDeps` and `FcmpProbes` on Emscripten; put the new targets in a new `cmake/FcmpWeb.cmake`.
- Sources: `Source/web/engine/*.cpp` and `Source/web/ui/*.cpp` as new globs; static page files in a top-level `web/`.

**Verify under node**
- Yes, `fcmp_probe_dsp` can run under node once the prerequisites land. It needs `NODERAWFS`, wasm exceptions, a larger stack, and the node emulator in the CTest command.
- Expect every exact hash to differ (8 `xarch.simd.*`, 8 `print.*` per Mode × 14 Modes). The roughly 2,000 abs/rel rows should hold.
- Thread probes (`dsp.telemetry`, `dsp.selftest`) and FP-control rows (`dsp.units`, `dsp.hostile`, `dsp.analysis`) must be excluded or reshaped.

**Page and hosting**
- Recommended: two wasm binaries with message passing. A glue-free standalone `fcmp-engine.wasm` runs in a hand-written AudioWorkletProcessor (bytes passed from the main thread); `fcmp-ui.wasm` runs the editor on the main thread.
- This needs no SharedArrayBuffer and no COOP/COEP, so it deploys to GitHub Pages as is.
- CI: an ubuntu job with a pinned, cached emsdk, plus a separate Pages deploy workflow on `main`.

Nothing was built or edited. Web-platform statements (browser support, Pages behaviour) are from general knowledge, not checked in this session.

## Facts

- **The Emscripten toolchain file is at <EMSCRIPTEN_ROOT>/cmake/Modules/Platform/Emscripten.cmake, and `em-config EMSCRIPTEN_ROOT` prints that root on this machine. /opt/homebrew/bin/emcc is a wrapper script, so dirname(realpath(emcc)) does not lead to the toolchain.**
  Evidence: `emcc --version` -> 6.0.3-git; `em-config EMSCRIPTEN_ROOT` -> /opt/homebrew/Cellar/emscripten/6.0.3/libexec; file /opt/homebrew/Cellar/emscripten/6.0.3/libexec/cmake/Modules/Platform/Emscripten.cmake; /opt/homebrew/opt/emscripten -> ../Cellar/emscripten/6.0.3; /opt/homebrew/bin/emcc is a bash script exec'ing libexec/emcc.
- **The toolchain sets CMAKE_SYSTEM_NAME=Emscripten, UNIX=1 (APPLE unset), forces compiler ID Clang, makes executables `.js`, and sets node as CMAKE_CROSSCOMPILING_EMULATOR. It also exposes EMSCRIPTEN_VERSION for a pin assertion.**
  Evidence: Emscripten.cmake:17 (SYSTEM_NAME), :48 (UNIX), :88-96 (emcc/em++/emar/emranlib), :99-112 (EMSCRIPTEN_VERSION), :160 and :167 (compiler ID Clang), :283 (CMAKE_EXECUTABLE_SUFFIX .js), :373-377 (find_program nodejs/node -> emulator).
- **`emcmake` only appends -DCMAKE_TOOLCHAIN_FILE and -DCMAKE_CROSSCOMPILING_EMULATOR to a cmake command line. The repo's gate uses workflow presets, so resolving the toolchain inside CMake fits better than wrapping.**
  Evidence: /opt/homebrew/Cellar/emscripten/6.0.3/libexec/emcmake.py:33-39; CMakePresets.json:153-172 (workflow presets); CLAUDE.md '[DSP] = cmake --workflow --preset dsp-verify'.
- **FCompressor's platform file refuses Emscripten today; its Clang-only check would pass.**
  Evidence: cmake/FcmpPlatform.cmake:29-37 (APPLE / Linux / else FATAL_ERROR 'builds on macOS and Linux'); :39-43 (compiler ID must match ^(Apple)?Clang$).
- **FcmpArch derives the architecture and ISA flags from the host processor, so on this Mac an Emscripten configure would pass -march=armv8-a to emcc (APPLE is unset under the toolchain) and accept only arm64|x86_64.**
  Evidence: cmake/FcmpArch.cmake:33-47 (FCMP_ARCHS from CMAKE_HOST_SYSTEM_PROCESSOR; regex ^(arm64|x86_64)$), :63-68 (ISA flags), :82-85 (fcmp_flags: -O3 -fno-math-errno -fno-trapping-math -ffp-contract=off), :185-200 (try_run for FCMP_CAN_RUN_PROBES).
- **There are three configurations today; only DSP-only is JUCE-free, and it builds just fcdsp, fcmp_probe_dsp and fcmp_bench with FunkGui reduced to its header-only harness.**
  Evidence: CMakeLists.txt:8-11, :48-49, :69-78, :97-99; cmake/FcmpDeps.cmake:137 (JUCE skipped), :245-251 (FUNKGUI_HARNESS_ONLY ON, FUNKGUI_WITH_PRESETS OFF); cmake/FcmpProbes.cmake:76-79, :82.
- **FunkGui has no JUCE-free core. FunkGuiCore links three JUCE modules, the font is embedded with juce_add_binary_data, the non-harness configure requires JUCE first, and bgfx is refused off macOS/Linux.**
  Evidence: FunkGui cmake/FunkGuiTargets.cmake:227-229 (harness-only return), :273-277 (juce_add_binary_data FunkGuiFonts), :304-310 (_funkgui_link_juce juce_gui_basics juce_data_structures juce_audio_processors); cmake/FunkGuiDeps.cmake:169-172 (FATAL without JUCE), :192-194 (FATAL: bgfx needs macOS or Linux). FunkGui HEAD 5f41b89 is the pinned SHA (FCompressor cmake/FcmpDeps.cmake:31-33).
- **JUCE use inside FunkGui's core modules is concentrated in a few files: software raster/PNG, the SDF font atlas, LineEdit, UiPreferences, JuceParamPort and MenuLook, plus type mentions in public headers.**
  Evidence: src/canvas/SoftRaster.cpp:3; src/text/FontAtlasSdf.cpp:3; src/text/LineEdit.cpp:5; include/funkgui/prefs/UiPreferences.h:3; src/params/JuceParamPort.cpp:3; src/juce/MenuLook.cpp (11 juce:: uses); include/funkgui/panel/HostServices.h:8,:44-50 (juce::Component*, PopupMenu, FileChooser); include/funkgui/core/Geometry.h (juce::Rectangle, 2 uses); include/funkgui/panel/Input.h (juce::MouseEvent, 1 use).
- **Outside gpu/, five editor .cpp files include JUCE directly (juce::Thread, PopupMenu, FileChooser, File, SystemClipboard); Panel.cpp mentions juce::Component twice. The rest of Source/editor uses FunkGui core headers only.**
  Evidence: Source/editor/PreviewWorker.cpp:25; Source/editor/views/PresetStrip.cpp:24; PresetBrowser.cpp:25; EditControls.cpp:21; Settings.cpp:26; Source/editor/gpu/Editor.h:45 (gpu, already excluded from headless). grep counts: PresetBrowser.cpp 32 juce:: uses, PreviewWorker.cpp 5, PresetStrip.cpp 7, EditControls.cpp 7, Settings.cpp 2, Panel.cpp 2.
- **ProcessorFacade.h, the editor's only route to the processor, includes no JUCE header; only fcdsp headers and funkgui/params/ParamPort.h, whose `native()` returns void*.**
  Evidence: Source/plugin/ProcessorFacade.h:7-14; FunkGui include/funkgui/params/ParamPort.h:25; lint rule editor.facade in cmake/LintDeps.cmake:27-28.
- **Every source list is a per-directory glob rooted at Source/ or Tools/, and target composition is chosen in CMake rather than by #if. A Source/web/ directory therefore needs new globs in lead-owned FcmpSources.cmake.**
  Evidence: cmake/FcmpSources.cmake:25-26, :85-98 (globs; editor gpu/ filtered at :91-93), :100 ('exactly one CreateEditor*.cpp per target, chosen here, never by #if'); CLAUDE.md lead-only list (CMakeLists.txt, cmake/**, CMakePresets.json, Scripts/**, docs/**, Resources/**, tests/golden/**).
- **fcdsp does not compile for wasm32 as written: Simd.h and ScopedFtz.h #error on any architecture other than arm64/x86-64, and FastMath.h's non-NEON branch is raw _mm_ intrinsics. Simd.h and ScopedFtz.h are frozen at FZ0 (names and signatures).**
  Evidence: Source/fcdsp/core/Simd.h:9, :25-36; Source/fcdsp/core/ScopedFtz.h:8, :15-19, :40-70; Source/fcdsp/core/FastMath.h:75-110 (e.g. :83-88 _mm_sub_epi32/_mm_srai_epi32).
- **fcdsp's contract requires a fused fma on every backend and runs under flush-to-zero; wasm provides neither. Emscripten's SSE-FMA emulation is mul+add unless -mrelaxed-simd, whose fusion is host-defined; its NEON emulation is also unfused and approximates vrsqrteq.**
  Evidence: Simd.h:11-12, :19 ('an unfused fallback would be a different arithmetic'), :93-97; Emscripten system/include/compat/fmaintrin.h:10-16, :40-48, :62-70; compat/arm_neon.h:56404-56425 (simde_vfmaq_f32 -> add(mul)), :128473-128503 (vrsqrteq approximation); site/source/docs/porting/simd.rst:83 ('does not have control over ... handling denormals'); tools/compile.py:107-109 (-msse*/-mfma/-mfpu=neon require -msimd128).
- **The portable FRSQRTE/FRSQRTS helpers in Simd.h are pure integer/ops code and would work on a wasm backend unchanged.**
  Evidence: Source/fcdsp/core/Simd.h:196-288 (detail::frsqrteBits, rsqrtsPortable; 'Both helpers compile on arm64 too').
- **The harness and blessing tool know only arm64 and x86_64, and the harness's own ScopedFtz #errors elsewhere. Both live in FunkGui, so running probes as wasm needs a FunkGui change.**
  Evidence: FunkGui include/funkgui/test/Harness.h:121-143 (#error at :141), :856-858 ('--arch must be arm64 or x86_64'), :1125-1126 (xarch. key not allowed in an overlay); tools/golden.py:55 (ARCHES = ('arm64','x86_64')); FCompressor Tools/probes/common/ProbeMain.cpp:4.
- **Goldens are a single base tree with no arch overlay. DSP golden rows are mostly toleranced: 1776 abs, 266 rel, 194 exact. Exact DSP rows are dsp.format (68), dsp.os (6), dsp.simd (8 xarch hashes) and dsp.print (8 per Mode).**
  Evidence: `ls tests/golden` -> base only; awk over tests/golden/base/{global,modes/*}/dsp.*.txt; tests/golden/base/global/dsp.simd.txt (8 xarch.simd.*.hash rows); grep 'exact$' counts per file; docs/design/03-build-verify-process.md:940-947, :993 (print hashes: overlay allowed with a stated cause).
- **Some DSP probes depend on threads or on reading/writing the FP control register and cannot run unchanged on single-threaded wasm.**
  Evidence: Tools/probes/dsp/telemetry.cpp:98, :169, :469 and selftest.cpp:234 (std::thread); units.cpp:37-84, hostile.cpp:79-115, analysis.cpp:142-234 (#if __aarch64__ / x86 FP-control code); docs/design/03-build-verify-process.md:989 ('Denormal tail cost < 2x', 'Silence gives exactly 0').
- **The CTest probe wrapper passes the probe as an argument to /bin/sh, so CMake will not prepend the cross-compiling emulator; the platform= key accepts only apple|linux; a timeout-scale mechanism already exists.**
  Evidence: cmake/FcmpProbes.cmake:121 (_fcmp_probe_sh), :145-149 (add_test COMMAND /bin/sh -c ... $<TARGET_FILE:${exe}>), :167-182 (regex 'platform=(apple|linux)'), :124-128 (FCMP_PROBE_TIMEOUT_SCALE).
- **lint.headers hands the build's compiler to check-headers.sh, which adds Darwin SDK handling; with em++ as CXX that test would not work as is. lint.deps is a pure CMake script over Source/** and Tools/** and already covers any new Source/web files for the product and test-tap rules.**
  Evidence: cmake/FcmpProbes.cmake:204-221; Scripts/check-headers.sh:30-33 (uname Darwin -> SDKROOT via xcrun); cmake/LintDeps.cmake:25-31, :155-156.
- **verify.sh works on any configured FCompressor build directory and has a --strict mode used by CI; it runs `ctest -L verify -j4` and classifies probe-results JSON.**
  Evidence: Scripts/verify.sh:63-66, :99-101, :281-287; .github/workflows/ci.yml:49, :81.
- **CI has four jobs (macOS and Ubuntu, dsp and plugin), read-only permissions, only first-party actions, a ~/audio/.deps cache keyed on deps.sh, and states that nothing is published.**
  Evidence: .github/workflows/ci.yml:14 ('Nothing is signed, installed or published here'), :23-24 (permissions: contents: read), :26-29 (FCMP_TIMING_SCALE: 3), :41, :52, :70 (actions/checkout@v5, upload-artifact@v4, cache@v4), :69-73 and :135-139 (deps cache), :93-119 (linux-dsp).
- **Emscripten's own Wasm Audio Worklet support (-sAUDIO_WORKLET) depends on WASM_WORKERS and SharedArrayBuffer. Separately, 'worklet' is a recognised ENVIRONMENT value, and standalone reactor modules are supported with --no-entry.**
  Evidence: Emscripten src/settings.js:1669-1677 (AUDIO_WORKLET 'depends on WASM_WORKERS and Wasm SharedArrayBuffer'), :673 ('worklet' environment), :1470-1472 (STANDALONE_WASM, --no-entry); tools/link.py:188-189, :200.
- **Emscripten defaults relevant to link flags: 64 KB stack, memory growth off, NODERAWFS off, legacy wasm exceptions when -fwasm-exceptions is used, environment web+webview+worker+node.**
  Evidence: src/settings.js:113 (STACK_SIZE = 64*1024), :226 (ALLOW_MEMORY_GROWTH false), :1070 (NODERAWFS false), :813 (WASM_LEGACY_EXCEPTIONS true), :697 (ENVIRONMENT).
- **The pinned bgfx.cmake has an Emscripten path (single-threaded, WebGL2) and the pinned shaderc has ESSL profiles, but FunkGui compiles only Metal and SPIR-V profiles, and FCompressor's shaderc fallback would try to build shaderc with the target toolchain.**
  Evidence: ~/audio/.deps/bgfx.cmake-v1.153.9385-561/CMakeLists.txt:48 (BGFX_CONFIG_MULTITHREADED off on Emscripten); cmake/bgfx/bgfx.cmake:69-71 (-sMAX_WEBGL_VERSION=2); bgfx/tools/shaderc/shaderc.cpp:122-123 (100_es, 300_es), :1393 (platform asm.js); FunkGui cmake/FunkGuiTargets.cmake:325-329 (profiles mtl, spv); FCompressor cmake/FcmpDeps.cmake:170-193 (prebuilt shaderc at ~/audio/.deps/tools, else BGFX_BUILD_TOOLS ON).
- **Telemetry records are fixed-size trivially copyable blobs (UiFrame 288 bytes, HistoryColumn 32 bytes), so they can be copied through a MessagePort unchanged on wasm32.**
  Evidence: Source/fcdsp/telemetry/UiFrame.h:55 (static_assert trivially copyable, sizeof == 72*4); Source/fcdsp/telemetry/HistoryRing.h:42 (sizeof(HistoryColumn) == 32).
- **EngineHost has one allocation point (configure) and a noexcept process(); fcdsp has no thread, mutex, chrono, file or getenv use, so a glue-free standalone wasm engine needs only malloc.**
  Evidence: Source/fcdsp/engine/EngineHost.h:8-9, :68, :75; grep over Source/fcdsp for thread|mutex|pthread|filesystem|fopen|getenv|chrono|__APPLE__|__linux__ returned only the Simd/FastMath/ScopedFtz arch code.
- **The SDF font atlas is rasterised with juce::Graphics and is already platform-dependent (CoreGraphics vs JUCE's software renderer), handled with a per-platform probe.**
  Evidence: docs/DECISIONS.md:993-997 (ADR-92 'Goldens'); FunkGui src/text/FontAtlasSdf.cpp:3; tests/golden/base/global/ui.font.txt and ui.font_linux.txt; cmake/FcmpProbes.cmake:9-12.
- **Size references: bundled font 11,004 bytes; native VST3 binary 6.97 MB (JUCE + bgfx + everything); fcmp_probe_dsp 2.2 MB; fcdsp about 18,000 lines, editor about 23,000 lines. Runtime reference: the full lead gate (493 tests) took 110 s here; the DSP-only configuration registers 210 tests.**
  Evidence: ls -la FunkGui/fonts/JetBrainsMono-Regular-subset.ttf; build-lead/FCompressor_artefacts/Release/VST3/.../FCompressor (6,968,880 bytes); build-lead/fcmp_probe_dsp (2,163,680); wc -l over Source/fcdsp and Source/editor; build-lead/verify-ctest.log:989, :1088; `ctest --test-dir build-dsp -N -L verify` -> Total Tests: 210.
- **The Linux-host default-compiler block runs before project(), but the Emscripten toolchain sets the compilers unconditionally afterwards, so it is harmless on a Linux CI host (a guard would still be cleaner).**
  Evidence: CMakeLists.txt:25-31; Emscripten.cmake:88-89.
- **The repository is GPL-3.0 with origin Snipet/FCompressor, and build-* and dist/ are git-ignored, so a build-web/ tree and its assembled site stay out of git.**
  Evidence: LICENSE:1-2; `git remote -v` -> git@github.com:Snipet/FCompressor.git; .gitignore ('/build*/', '/dist/').

## Blockers and options

- **fcdsp has no wasm32 SIMD backend. Simd.h and ScopedFtz.h hard-error and FastMath.h uses raw SSE intrinsics; Simd.h and ScopedFtz.h are frozen (bodies may be added, declarations not changed).** (Source/fcdsp/core/Simd.h:25-36, Source/fcdsp/core/ScopedFtz.h:15-19, Source/fcdsp/core/FastMath.h:75-110)
  Options: (a) Native third backend FCDSP_SIMD_WASM on <wasm_simd128.h> (f32x4 = v128_t), fma as mul+add, the portable rsqrte/rsqrts helpers reused, ScopedFtz a no-op; build with -msimd128 only. Cleanest and smallest. (b) Reuse the SSE branch through Emscripten's compat headers (-msimd128 -msse4.1 -mfma; widen the #if to __EMSCRIPTEN__): fewest source edits, but _mm_getcsr is a constant and the `__asm__("" : "+x"(mask))` fence in sel() will not compile. (c) -mfpu=neon via SIMDe: rejected, vrsqrteq is an approximation and fma is unfused. Never -mrelaxed-simd: fusion would vary per visitor's CPU.
- **Wasm has no fused multiply-add and no flush-to-zero/denormals-are-zero. Arithmetic differs in the last bit from native, and decaying recursive state enters denormals: slow on x86 hosts and possibly never reaching exact zero.** (Simd.h:11-19, :93-97; ScopedFtz.h; spec rows in docs/design/03-build-verify-process.md:989 (dsp.hostile: silence exactly 0, denormal tail cost < 2x); Emscripten simd.rst:83)
  Options: (a) Accept unfused fma on web, record it as a wasm32 golden overlay with a stated cause, and add explicit denormal protection in the wasm backend (snap tiny state to zero per block, or a tiny bias); keep dsp.hostile's tail-cost row as the gate for it. (b) Emulate exact fma in double: closer to native but slower, and float double-rounding is still not exactly fused. (c) Ship without denormal protection: risks audio glitches on Intel/AMD visitors; not recommended.
- **FunkGui has no JUCE-free core configuration, so the real editor Panel cannot link without JUCE. A FunkGui release and a pin bump are required before any web UI target exists.** (FunkGui cmake/FunkGuiTargets.cmake:273-277, :304-310; cmake/FunkGuiDeps.cmake:169-172; FCompressor cmake/FcmpDeps.cmake:290-301)
  Options: (a) New FunkGui option (e.g. FUNKGUI_WITH_JUCE=OFF) that builds core without src/juce, JuceParamPort, SoftRaster's PNG path, the JUCE atlas rasteriser and UiPreferences' PropertiesFile, with the font embedded by a plain generated array. (b) A separate FunkGui::core_web target assembled from a JUCE-free subset of modules. Either way the public headers that name juce:: types (HostServices.h, Geometry.h, Input.h, LineEdit.h, UiPreferences.h) need guards or JUCE-free signatures; that is a MINOR bump.
- **Five editor .cpp files outside gpu/ include JUCE (native popup menus, file chooser, clipboard, a worker thread).** (Source/editor/PreviewWorker.cpp:25; Source/editor/views/PresetStrip.cpp:24, PresetBrowser.cpp:25, EditControls.cpp:21, Settings.cpp:26)
  Options: (a) Route these through funkgui::HostServices so Source/editor minus gpu/ becomes JUCE-free and the existing FCMP_EDITOR_SOURCES glob serves web unchanged. (b) Move the JUCE-bound pieces to Source/editor/juce/ (filtered out on web exactly as gpu/ is filtered for headless) with web counterparts in Source/web/ui/. Both keep the 'chosen in CMake, never by #if' rule; (a) is less duplication.
- **The harness, golden.py and the xarch rule only admit arm64 and x86_64, so a wasm verify job cannot even parse its command line, and xarch.simd.* hashes can never be overlaid.** (FunkGui include/funkgui/test/Harness.h:121-143, :856-858, :1125-1126; FunkGui tools/golden.py:55; cmake/FcmpProbes.cmake:145-149, :167-182)
  Options: (a) Add arch `wasm32` to Harness.h and golden.py (with an adopt path for a wasm32 overlay), make the harness ScopedFtz a no-op there, and have dsp.simd emit wasm-specific keys instead of xarch.* on wasm. Full gate with tests/golden/wasm32/ overlays. (b) No golden comparison on web: run only spec rows plus a small node smoke test of the shipped engine wasm. Cheaper, but nothing proves the web arithmetic stays within tolerance of native. (c) Defer wasm verify until after the first demo ships.
- **The renderer for PrimList on the web is undecided, and it determines CI dependencies. bgfx works on Emscripten upstream but FunkGui refuses it off macOS/Linux and has no ESSL shader profile; cross-compiling also makes a prebuilt host shaderc mandatory.** (FunkGui cmake/FunkGuiDeps.cmake:192-194; cmake/FunkGuiTargets.cmake:329; FCompressor cmake/FcmpDeps.cmake:170-193)
  Options: (a) bgfx on WebGL2 with a third shader profile (shaderc --platform asm.js -p 300_es): one shader source and the same BgfxSink, but adds roughly 0.6-1 MB of wasm and requires Scripts/deps.sh (host shaderc, about 2,000 CPU-s uncached) in the web CI job. (b) A small dedicated WebGL2 sink in Source/web/ui with a hand-ported GLSL ES shader: no bgfx, no shaderc, far smaller, but a second shader to keep in parity. (c) CPU raster to a 2D canvas: simplest, slowest, and SoftRaster currently depends on JUCE.
- **The SDF font atlas is produced with juce::Graphics, which is unavailable on the web.** (FunkGui src/text/FontAtlasSdf.cpp:3; docs/DECISIONS.md:993-997)
  Options: (a) Bake the atlas at build time with a host tool and embed it (a second host-tool step, like shaderc). (b) Rasterise in wasm with stb_truetype or Emscripten's FreeType port. (c) Commit a pre-baked atlas blob to FunkGui. Any choice needs its own ui.font-style golden for the web platform.
- **Threading model: shared memory (one instance, real seqlock/SPSC telemetry) needs cross-origin isolation headers that GitHub Pages cannot set.** (Emscripten src/settings.js:1669-1677; hosting choice)
  Options: (a) Message passing between a worklet engine instance and a main-thread UI instance: no SharedArrayBuffer, no headers, works on GitHub Pages and inside iframes; cost is one message hop of telemetry latency and some garbage from postMessage. (b) SharedArrayBuffer with COOP/COEP on a host that supports headers (Cloudflare Pages, Netlify). (c) SharedArrayBuffer on GitHub Pages through a service-worker shim: one forced reload, breaks where service workers are disabled, and blocks plain iframe embedding. Recommend (a).
- **Publishing is a lead/user action: GitHub Pages must be enabled for Snipet/FCompressor, and deployment makes the demo public. ci.yml currently promises that nothing is published.** (.github/workflows/ci.yml:14, :23-24; CLAUDE.md (only the lead pushes and publishes))
  Options: (a) Separate .github/workflows/web.yml with pages: write and id-token: write, deploying only on push to main or on a tag; the user sets Settings > Pages > Source to GitHub Actions once. (b) Build-only job that uploads the site as an artifact until the user decides to publish. (c) Host elsewhere.
- **Bundled demo loops need audio the user owns or that is clearly redistributable alongside a public GPL-3.0 repository; none exists in the repo.** (new web/audio/ (no audio assets exist; Resources/ holds only FCompressor.entitlements))
  Options: (a) The user records or supplies 3-4 loops (drums, bass, vocal, mix) with a CREDITS file. (b) CC0 material with provenance recorded per file. (c) Ship v1 with file drop only and add loops later. Avoid CC-BY-NC and sample-pack content.
- **The Emscripten version must be pinned for reproducible goldens and CI. The local install reports 6.0.3-git from Homebrew; whether emsdk offers exactly 6.0.3 was not checked (no network use).** (emcc --version; proposed cmake/FcmpWeb.cmake pin; CI emsdk step)
  Options: (a) Pin FCMP_EMSCRIPTEN_VERSION in CMake and assert it against the toolchain's EMSCRIPTEN_VERSION, as FcmpDeps does for JUCE and bgfx; CI installs the same version from emsdk. (b) Accept a version range and re-bless the wasm32 overlay on upgrades.

## Recommended approach

**0. Order of work**
1. fcdsp wasm backend (Simd.h, FastMath.h, ScopedFtz.h bodies) plus denormal protection.
2. FunkGui release: `wasm32` in Harness.h and golden.py, a JUCE-free core option, an Emscripten renderer path, a font atlas without JUCE.
3. FCompressor build: `web` preset, CMake branches, `cmake/FcmpWeb.cmake`, probes under node, wasm32 overlay blessed by the lead.
4. Page shell, CI job, Pages deploy.
Steps 1 and 3 with probes only (no UI) already give a `web-dsp` milestone that proves the engine in wasm.

**1. Preset and toolchain**
- `CMakeLists.txt`, before `project()`: add `option(FCOMPRESSOR_WEB ...)`. When it is ON and `CMAKE_TOOLCHAIN_FILE` is unset, run `em-config EMSCRIPTEN_ROOT` and set `CMAKE_TOOLCHAIN_FILE` to `<root>/cmake/Modules/Platform/Emscripten.cmake`. This works for Homebrew here and for emsdk in CI with no environment variable.
- Guard the Linux default-compiler block (`CMakeLists.txt:27-31`) with `NOT FCOMPRESSOR_WEB`.
- `CMakePresets.json`:
  - configure `web`: inherits `cfg-release` and `base`, cache `FCOMPRESSOR_WEB=ON`;
  - build `web`: targets `fcmp_web_site` and `fcmp_probes`, `jobs: 6`;
  - test `web`: inherits `test-base`;
  - workflow `web-verify`.
- `FCOMPRESSOR_WEB` is a fourth configuration (`FCMP_CONFIGURATION "web"`): no JUCE, FunkGui JUCE-free core. Refuse it unless `CMAKE_SYSTEM_NAME` is Emscripten, and refuse Emscripten without it.
- `FcmpPlatform.cmake`: add an Emscripten branch (`FCMP_PLATFORM web`, no plugin formats); skip the JUCE 8.0.4 workaround as DSP-only does.
- `FcmpArch.cmake`: on Emscripten set `FCMP_ARCHS` and `FCMP_RUN_ARCH` to `wasm32`, widen the arch regex, set ISA flags to `-msimd128` (never `-mrelaxed-simd`). Add `-fwasm-exceptions` to `fcmp_flags` so every object shares one exception model. The existing `try_run` then runs through node.
- `FcmpDeps.cmake`: skip JUCE; set the new FunkGui options; if bgfx is the renderer, require the prebuilt host shaderc and fail with "run Scripts/deps.sh" instead of the build-from-source fallback. Assert `EMSCRIPTEN_VERSION` against a pin and write it to `fcmp-deps.txt`.

**2. Source selection**
- `FcmpSources.cmake` gains `Source/web/engine/*.cpp` (`FCMP_WEB_ENGINE_SOURCES`) and `Source/web/ui/*.cpp` (`FCMP_WEB_UI_SOURCES`).
- New `cmake/FcmpWeb.cmake` defines:
  - `fcmp_web_engine`: fcdsp plus the engine sources; `-sSTANDALONE_WASM --no-entry`, exported `fcmp_web_*` functions and `malloc/free`, fixed memory, `-sSTACK_SIZE=1MB`; output `fcmp-engine.wasm` with no JS glue.
  - `fcmp_web_ui`: `FCMP_EDITOR_SOURCES` (JUCE-free after the editor blocker is resolved), the UI sources, fcdsp (descriptors, params, analysis), FunkGui core; `-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB`.
  - `fcmp_web_site`: assembles `build-web/site/` from `web/` plus the wasm/js outputs and licences.
  - Tests `web.engine` (node instantiates the shipped `fcmp-engine.wasm` and processes a buffer) and `web.size` (budget check).
- `LintDeps.cmake`: add rules that `Source/web/**` includes no JUCE, `Source/web/engine/**` includes only fcdsp, and `Source/web/ui/**` never names `EngineHost`.

**3. Probes under node**
- `FcmpProbes.cmake`:
  - run `${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:exe>` inside the wrapper;
  - link `fcmp_probe_dsp` with `-sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sEXIT_RUNTIME=1`;
  - pass `--arch wasm32`;
  - extend the `platform=` key so thread probes can be excluded on web;
  - set a timeout scale of about 3;
  - drop `lint.headers` in this configuration (em++ plus the Darwin SDK logic do not mix; the native jobs already run it); keep `lint.deps`.
- Do not build the probes with `-pthread`: that would test different object code from the shipped single-threaded engine. Exclude `dsp.telemetry` and the thread part of `dsp.selftest` instead.
- Expected differences:
  - all exact hashes differ (8 `xarch.simd.*`, 112 `print.*`): the first needs wasm-specific keys, the second a `tests/golden/wasm32/` overlay with the cause "no fused multiply-add, no flush-to-zero in wasm";
  - `dsp.format` and `dsp.os` exact rows should match (Linux/glibc already matches with no overlay);
  - abs/rel rows should hold, since unfused fma moves results by about 1 ulp against tolerances of 0.01 dB and up;
  - FP-control spec rows in `dsp.units`, `dsp.hostile`, `dsp.analysis` need wasm branches;
  - self-consistency specs (block-size invariance, L/R swap, determinism) should hold because wasm float is deterministic without relaxed SIMD.
- Gate: `Scripts/verify.sh --strict build-web`, unchanged.

**4. Page shell (`web/`)**
- `index.html`: a 960x640 canvas (CSS-scaled, device-pixel-ratio aware), a Start overlay, a loop picker, a drop zone, a bypass toggle, a browser-support message.
- `main.js`:
  - fetch `fcmp-engine.wasm` as an ArrayBuffer;
  - on the first click create `new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' })` and `resume()` it (autoplay policy needs the gesture; 48 kHz keeps the engine inside the tested rate range);
  - `audioWorklet.addModule('fcmp-worklet.js')`;
  - create the `AudioWorkletNode` with stereo explicit channels and `processorOptions: { wasmBytes }`;
  - load the UI module and connect its facade to `node.port`.
- `fcmp-worklet.js`:
  - the worklet scope has no fetch, so compile the bytes with `new WebAssembly.Module` / `Instance` in the constructor, with a few stub imports, then call `_initialize` and `fcmp_web_create(sampleRate, 128)`;
  - `process()` copies input into wasm memory, calls `fcmp_web_process`, copies out, and returns true even with no input;
  - `port.onmessage` applies parameter changes (same thread as `process`, so no locking);
  - every few quanta it posts a UiFrame and new history columns in a recycled transferable buffer.
  - Passing bytes rather than a compiled Module avoids relying on Module cloning into worklets.
- Sources:
  - bundled loops as FLAC (lossless matters for a compressor; about 0.5-1 MB per 10 s stereo), lazily fetched, decoded with `decodeAudioData`, played by a looping `AudioBufferSourceNode`;
  - a dropped file through the same path, capped in length, with a note that nothing is uploaded;
  - microphone later and off by default (permission prompt, feedback risk, Bluetooth call-mode sample rates).

**5. CI and hosting**
- New job on `ubuntu-24.04`, either in `ci.yml` (build and verify only) or in a new `web.yml`:
  1. checkout; install `ninja-build`;
  2. install emsdk at the pinned version by cloning emsdk and running `install`/`activate` (keeps the repo's first-party-actions-only habit), cached with `actions/cache` on the emsdk directory keyed by version; the cache also keeps Emscripten's generated system libraries;
  3. if bgfx is used, `Scripts/deps.sh --no-pluginval` with the existing `~/audio/.deps` cache key;
  4. `cmake --preset web`, `cmake --build --preset web`, `Scripts/verify.sh --strict build-web`;
  5. upload `build-web/site` as an artifact.
- Deploy: a separate job with `actions/configure-pages`, `upload-pages-artifact`, `deploy-pages`, permissions `pages: write` and `id-token: write`, only on push to `main` or a tag. `ci.yml` keeps `contents: read`.
- Use relative URLs throughout (the site lives under `/FCompressor/`) and a version query or hashed filenames, since Pages' cache lifetime cannot be configured.
- Budget, enforced by `web.size`: initial load at most about 4 MB raw / 1.5 MB compressed; engine wasm at most about 1 MB; each loop at most 1 MB and lazy.

**6. Layout**
- `Source/web/engine/` — WebEngine.cpp, the C ABI over EngineHost.
- `Source/web/ui/` — WebMain, WebFacade (ProcessorFacade over the port), WebHost (HostServices), WebSink.
- `web/` — index.html, main.js, fcmp-worklet.js, demo.css, `audio/` with CREDITS, smoke.mjs.
- `cmake/FcmpWeb.cmake`, `tests/golden/wasm32/`, `.github/workflows/web.yml`.
- Add `web/**` and `.github/**` to the lead-only list in CLAUDE.md; `Source/web/**` is owned by the web card's manifest.
- Record the decision as a new ADR, since ARCHITECTURE and ADR-92 define the platform list.

## Effort notes

**Sizing (rough, in the repo's card terms)**
- Build plumbing (preset, four CMake branches, `FcmpWeb.cmake`, probes under node): one lead-owned card, comparable to the Linux port's CMake work. Every file touched is lead-only (`CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `.github/**`, `docs/**`).
- fcdsp wasm backend plus denormal protection: one DSP card; small in lines, but it touches frozen headers (bodies only) and decides the golden story.
- FunkGui release: the largest piece. A JUCE-free core, the harness `wasm32` arch, a renderer path and a font atlas probably mean two or three FunkGui cards and a MINOR version bump, then a pin bump here.
- Editor JUCE removal (five files): one UI card.
- Page shell, worklet, facade: one card. CI job and Pages deploy: half a card, plus a one-time repo setting by the user.
- Golden work for the lead: bless a `tests/golden/wasm32/` overlay (about 112 print hashes plus wasm-specific simd rows) and review any spec rows reshaped for wasm.

**Cheapest useful first milestone**
- `web` preset building only fcdsp, `fcmp_probe_dsp` and the standalone engine wasm, verified under node. It needs the fcdsp backend and the harness arch change, but no JUCE-free FunkGui core and no renderer.

**Runtime expectations**
- The DSP-only gate is 210 tests; the full native gate (493 tests) runs in 110 s on this Mac. Under node expect a few minutes locally and more on a shared runner. First CI run pays for the emsdk install and Emscripten's system-library build; later runs hit the cache.
- If bgfx is chosen, an uncached host shaderc build costs about 2,000 CPU-seconds; sharing the existing `~/audio/.deps` cache key with `linux-plugin` avoids that.

**Not verified in this session**
- Nothing was configured or built with Emscripten, so compile-level surprises (warnings under `-Werror` with a newer Clang, `RtInterposer.cpp` and `AllocCounter.cpp` under Emscripten's pthread stubs, `std::filesystem::equivalent` under NODERAWFS) are untested.
- Browser and hosting statements are from general knowledge: SIMD/AudioWorklet/WebGL2 baselines, Module cloning into worklets, GitHub Pages headers, compression and cache lifetime, FLAC decode support. Check them before committing to budgets.
- Whether emsdk offers exactly 6.0.3 was not checked.
- Wasm size estimates are guesses from native sizes; measure after the first build and set the `web.size` budget from that.

**Key paths**
- /Users/seanfunk/audio/plugins/FCompressor/CMakeLists.txt, CMakePresets.json, cmake/FcmpPlatform.cmake, cmake/FcmpArch.cmake, cmake/FcmpDeps.cmake, cmake/FcmpSources.cmake, cmake/FcmpProbes.cmake, Scripts/verify.sh, .github/workflows/ci.yml
- /Users/seanfunk/audio/plugins/FCompressor/Source/fcdsp/core/Simd.h, ScopedFtz.h, FastMath.h
- /Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiTargets.cmake, cmake/FunkGuiDeps.cmake, include/funkgui/test/Harness.h, tools/golden.py
- /opt/homebrew/Cellar/emscripten/6.0.3/libexec/cmake/Modules/Platform/Emscripten.cmake
