# Web demo scout report: fcdsp on WebAssembly

Written by a read-only scout before Sprint A (ADR-93 design pass, 2026-10-01). Line numbers are as of main 3731e07.
Statements about WebAssembly behaviour were not built unless the report says a scratch test ran.

## Summary

fcdsp can run on wasm32, but not through either Emscripten intrinsic-emulation route and not through the existing CMake presets. It needs a third, native WASM SIMD128 backend in three frozen core headers (Simd.h, ScopedFtz.h, FastMath.h), a separate web build configuration, and a small C wrapper around EngineHost. Nothing in the project was built or edited; I compiled scratch snippets with em++ and clang in the scratchpad to check specific claims.

**The three hard problems**

- **Fused multiply-add.** WASM SIMD128 has no deterministic fused op, and Simd.h's contract requires one rounding on every backend. Emscripten's `_mm_fmadd_ps` and its NEON `vfmaq_f32` both fall back to separate multiply and add. An exact software fma built only from WASM-expressible f64x2 operations matched hardware `fmaf` on 100,012,167 triples with 0 mismatches. It costs 55 SIMD instructions against 2 for the unfused form, and fcdsp has 159 fma/fms call sites.
- **Flush-to-zero.** WASM has no FP control register, so ScopedFtz can only be a no-op. The existing build-lead probe log shows that with flush-to-zero off, 11 of 14 Modes produce tens of thousands of denormal GR values in a release tail. Logic and sound are unaffected, but bits differ from native, and a decaying one-pole can stall at a denormal indefinitely. That is a CPU risk on x86 hosts unless the backend flushes tiny results itself.
- **Build refusals.** The project's CMake refuses Emscripten outright, and three headers plus the FunkGui probe harness have `#error` or arch-only code. The full list is in the facts.

**Smaller answers**

- **Threads and atomics:** fcdsp needs no pthreads. Without `-pthread`, `std::atomic` and the fences lower to plain loads and stores (checked in em++ output), so the seqlock and HistoryRing compile and work single-threaded.
- **Alignment and memory:** every size/alignment static_assert I found is pointer-free and holds on wasm32. Memory need is under 1 MiB, so no memory growth is needed.
- **Goldens:** there are 14 `xarch.` rows (8 in dsp.simd, 6 in dsp.os) and 8 exact `print.*.hash` rows per Mode (checked for bus-g). They can only hold with the exact fma, and only where no value passes through the denormal range. `xarch.simd.ops.hash` cannot hold without full input and output flush emulation, because it hashes operations on denormal edge values. None of this was run under wasm.
- **AudioWorklet fit:** EngineHost accepts any block length and chunks internally at 64 on an absolute grid, so a 128-frame quantum needs no FIFO and adds no latency. Latency is 4 samples at the default STD quality with lookahead off.

## Facts

- **Simd.h has only NEON and SSE backends and refuses every other architecture; the x86 branch also refuses a build without FMA.**
  Evidence: /Users/seanfunk/audio/plugins/FCompressor/Source/fcdsp/core/Simd.h:25-36 (#error at :35 'no SIMD backend for this architecture'; #error at :31-33 'needs FMA'). Emscripten defines neither __aarch64__, __ARM_NEON nor __x86_64__: `em++ -msimd128 -mavx2 -mfma -dM -E` printed only __SSE4_1__, __AVX2__, __FMA__; -mfpu=neon defines __ARM_NEON__ with trailing underscores (/opt/homebrew/Cellar/emscripten/6.0.3/libexec/tools/compile.py:137-138).
- **ScopedFtz.h refuses any architecture other than arm64 and x86-64, and its bodies are FPCR inline asm or _mm_getcsr/_mm_setcsr. EngineHost::process, ModeEngine and every analysis entry point open one.**
  Evidence: /Users/seanfunk/audio/plugins/FCompressor/Source/fcdsp/core/ScopedFtz.h:15-19 (#error at :18), bodies :40-70. Users: Source/fcdsp/engine/EngineHost.cpp:791; Source/fcdsp/engine/ModeEngine.h:577,602,635,646; Source/fcdsp/analysis/Analysis.cpp:292,334,354,369,380,391,449,468,490,497.
- **FastMath.h has three helpers written with raw intrinsics in a NEON-else-SSE split, so a wasm build falls into the SSE branch and fails on __m128i. Outside Simd.h, ScopedFtz.h and FastMath.h, fcdsp contains no architecture-specific code.**
  Evidence: /Users/seanfunk/audio/plugins/FCompressor/Source/fcdsp/core/FastMath.h:75-89 (log2Split), :95-99 (scaleByPow2), :105-110 (xorSign). A grep of Source/fcdsp for __aarch64__, __x86_64__, _mm_, NEON intrinsics, __asm__ and #error, excluding those three headers, returned nothing.
- **The asm fence in the SSE sel does not compile for wasm.**
  Evidence: Simd.h:175-179 (`__asm__("" : "+x"(mask))` at :177). A scratch snippet with the same statement under `em++ -msimd128 -fsyntax-only` gave: error: invalid output constraint '+x' in asm. The fence exists only because maxps flushes a denormal operand under DAZ (Simd.h:170-174); WASM's pmin/pmax are defined as exact selects, so a wasm backend needs no fence.
- **Emscripten's SSE emulation does not fit Simd.h: its fma is unfused (or non-deterministic with relaxed SIMD), MXCSR cannot be written, and the float-to-int conversion used by exp2 is a scalar loop.**
  Evidence: /opt/homebrew/Cellar/emscripten/6.0.3/libexec/system/include/compat/fmaintrin.h:10-16 and :39-47, :61-69 (_mm_fmadd_ps = add(mul) without -mrelaxed-simd; relaxed_madd 'implementation-defined' with it). compat/xmmintrin.h:696-701 (_mm_getcsr returns a constant with FLUSH_ZERO_OFF); no _mm_setcsr exists (scratch compile: 'use of undeclared identifier _mm_setcsr'). compat/emmintrin.h:1061-1073 (_mm_cvttps_epi32 is a per-lane loop with lrint, 'TODO: optimize'). _mm_blendv_ps itself is fine (compat/smmintrin.h:57-61) and _mm_min_ps/_mm_max_ps reproduce the x86 second-operand policy (compat/xmmintrin.h:253-270).
- **Emscripten's NEON emulation (SIMDe) does not fit either: vfmaq_f32 is unfused, vrsqrteq_f32 is a bit-hack approximation rather than the FRSQRTE table, vrsqrtsq_f32 is unfused, and vrndmq_f32 is a scalar floorf loop.**
  Evidence: /opt/homebrew/Cellar/emscripten/6.0.3/libexec/system/include/compat/arm_neon.h:56404-56425 (vfmaq_f32 falls to vaddq(a, vmulq(b, c))), :128473-128514 (vrsqrteq_f32), :128757-128770 (vrsqrtsq_f32), :126010-126031 (vrndmq_f32). Simd.h:11-14 requires fused fma/fms and NEON's FRSQRTE on every backend.
- **A native WASM SIMD128 backend maps every op in Simd.h's set one to one except fma/fms, rsqrte and rsqrts; the portable rsqrte/rsqrts helpers already exist, and no fcdsp code outside Simd.h calls rsqrte or rsqrts.**
  Evidence: A scratch draft of the op set (load/store/splat/add/mul/div/min/pmin/abs/sqrt/floor/gt/bitselect/extract_lane/replace_lane, plus FastMath's integer helpers with i32x4 sub/shr/shl/add, convert and trunc_sat) compiled cleanly with `em++ -std=c++20 -O3 -msimd128 -ffp-contract=off -Wall -Wextra` (clang 23, Emscripten 6.0.3): scratchpad/wasm_backend.cpp. Portable helpers: Simd.h:196-288 (detail::frsqrteBits, rsqrtsPortable). Grep for rsqrte(/rsqrts( in Source/fcdsp outside core/Simd.h: no hits.
- **An exact one-rounding float fma can be built from operations WASM SIMD128 has, at 55 SIMD instructions against 2 for mul+add; fcdsp has 159 fma/fms call sites.**
  Evidence: Method: exact f64 product, TwoSum, round-to-odd on the f64 bits, demote. Scratch test /private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/fma_ro.cpp, native clang on arm64 against std::fmaf in the default FP mode: 'checked 100012167 triples, 0 mismatches' (random bit patterns, cancellation cases, midpoint cases, an edge set cubed). The first attempt failed on infinities until the inexact test excluded NaN error terms. Instruction count from the em++ -S output of fmaExact in wasm_backend.cpp. Call-site count: grep of simd::fma/fms in Source/fcdsp = 159; log2 uses 8 per call and exp2 5 (FastMath.h:122-130, :142-147).
- **The project's own design says linear-domain states rely on flush-to-zero and claims GR-domain states never go denormal; the existing probe log contradicts the second half for 11 of 14 Modes.**
  Evidence: /Users/seanfunk/audio/plugins/FCompressor/docs/design/01-core-contracts.md:1322-1326. /Users/seanfunk/audio/plugins/FCompressor/build-lead/Testing/Temporary/LastTest.log (Oct 1 2026), NOTE lines of dsp.analysis.<key>: a render without ScopedFtz differs in 25106 (fet-76) to 153883 (mu-67) samples, all or most counted as denormal GR values, for bus-25, bus-g, clean, console-e, diode-54, diode-609, fet-76, mu-67, mu-mastering, octo, opto-tube-1b; 0 for brickwall, opto-2a, opto-3a. The probe code that produces the note: Tools/probes/dsp/analysis.cpp:1189-1196 and :1313-1327.
- **Without flush-to-zero the engine's logic does not change, but a decaying one-pole can stall at a denormal rather than reach zero, and some audio-path recurrences are scalar code that a SIMD-level fix would not cover.**
  Evidence: Explicit landings make the 'exactly 0' cases independent of FTZ: Source/fcdsp/core/Smoother.h:41-48 and :54-61 (eps and stall landing), Source/fcdsp/engine/stages/ballistics/SlidingMaxBox.h:65-68 (lands within 1e-6 dB). Floors before every log2: FastMath.h:20, :185-193. Stall: for the smallest denormals d*a rounds back to d when a > 0.5 (arithmetic, not measured). Scalar per-sample recurrences: Source/fcdsp/engine/stages/colour/OctoDist.h:103-112 (one-pole HP and SVF integrators in plain float). Sanitize copies denormal inputs through unchanged: Source/fcdsp/core/Sanitize.cpp:1-5.
- **fcdsp creates no threads and takes no locks; its only concurrency primitives are std::atomic and atomic_thread_fence, which compile to plain operations on wasm without -pthread.**
  Evidence: No <thread>/<mutex>/pthread include under Source/fcdsp (grep; the hits are in Source/plugin, Source/editor and Tools). Atomics: Source/fcdsp/engine/EngineHost.h:87-91, Source/fcdsp/telemetry/Seqlock.h:28-29, Source/fcdsp/telemetry/HistoryRing.h:103-105. Scratch em++ -S output for a release store, a fence and a 64-bit fetch_add: i32.store, i64.load, i64.add, i64.store and no fence instruction; `emcc -dM -E` shows __GCC_ATOMIC_LLONG_LOCK_FREE 2, and a static_assert on atomic<uint64_t>::is_always_lock_free passed.
- **The size and alignment static_asserts in fcdsp contain no pointers and hold on wasm32; configure() is the only allocation point and its footprint is small.**
  Evidence: Source/fcdsp/engine/IEngine.h:41 (Carry 64 bytes, align 16), Source/fcdsp/telemetry/UiFrame.h:55 (288 bytes), Source/fcdsp/params/EngineParams.h:35 (116 bytes), Source/fcdsp/telemetry/HistoryRing.h:42, Source/fcdsp/modes/DefineMode.h:55-56 (engine fits 8192-byte arena, align <= 64). v128_t is 16-byte aligned (scratch static_assert). Allocation: EngineHost.cpp:705 (make_unique<Impl>, over-aligned to 64 by :361), :718-720 (two scratch vectors of 4 * nextPow2(960 + 64) floats at 48 kHz); HistoryRing is 128 KiB (HistoryRing.h:46). Emscripten defaults: ALLOW_MEMORY_GROWTH false (/opt/homebrew/Cellar/emscripten/6.0.3/libexec/src/settings.js:226). Over-aligned operator new under Emscripten was not run.
- **The existing CMake cannot configure a wasm build: the platform file refuses Emscripten, and the arch file derives ISA flags from the host processor, which em++ rejects.**
  Evidence: /Users/seanfunk/audio/plugins/FCompressor/cmake/FcmpPlatform.cmake:29-37 (FATAL_ERROR 'builds on macOS and Linux' at :36); the Emscripten toolchain sets CMAKE_SYSTEM_NAME Emscripten and compiler ID Clang (/opt/homebrew/Cellar/emscripten/6.0.3/libexec/cmake/Modules/Platform/Emscripten.cmake:17, :167), so the Clang check at FcmpPlatform.cmake:39-43 passes. cmake/FcmpArch.cmake:33-47 (CMAKE_HOST_SYSTEM_PROCESSOR, arm64/x86_64 only), :63-68 (flags). Scratch: `em++ -mcpu=apple-m1` -> 'unknown target CPU'; `-march=armv8-a` -> 'unsupported option -march= for target wasm32-unknown-emscripten'; `-mavx2 -mfma` -> 'also requires passing -msimd128'. CMakeLists.txt:81-83 includes FcmpPlatform first, so that is the first failure; CMakeLists.txt:92-93 also forces position-independent code on fcdsp.
- **The DSP-only preset still pulls FunkGui's test harness, which has its own #error and accepts only two arch names, and several dsp probes contain arm64-or-x86 FP-control code.**
  Evidence: CMakePresets.json:78-82 (dsp preset), cmake/FcmpDeps.cmake (FUNKGUI_HARNESS_ONLY for DSP-only), cmake/FcmpProbes.cmake:49 and :148 (--arch ${FCMP_RUN_ARCH}). /Users/seanfunk/audio/libraries/FunkGui/include/funkgui/test/Harness.h:123-142 (#error at :141), :855-858 ('--arch must be arm64 or x86_64'). Probes: Tools/probes/dsp/units.cpp:62-80, Tools/probes/dsp/analysis.cpp:211-235, Tools/probes/dsp/hostile.cpp:97-119, Tools/probes/dsp/simd.cpp:376-381 and :390-404.
- **Cross-arch bit identity is asserted by 14 xarch rows (dsp.simd and dsp.os) and, in practice, by exact print hashes per Mode; the Linux x86 run matched all arm64 DSP goldens with no overlay.**
  Evidence: tests/golden/base/global/dsp.simd.txt (8 rows: xarch.simd.{ops,log2,exp2,tanh,logcosh,tanpi,sinpi,cospi}.hash) and dsp.os.txt (xarch.os.{std,hq}.hash, xarch.adaa.{tanh,asymtanh,softclip,hardclip}.hash); tests/golden/base/modes/bus-g/dsp.print.txt has 8 exact rows of 8. Only tests/golden/base exists (no overlays). Rule: docs/design/03-build-verify-process.md:75-78, :944-945. Linux result: docs/DECISIONS.md ADR-92 ('every DSP golden matched the arm64 values bit for bit, save two x86 findings').
- **xarch.simd.ops.hash and several spec rows depend on flush-to-zero being active, so they cannot pass on wasm without full input and output flush emulation.**
  Evidence: Tools/probes/dsp/simd.cpp:196-301 (the ops hash runs every op over an edge set that includes denormals, under ProbeMain's ScopedFtz), :346 and :365-369 (ops.rsqrte.specials, ops.ftz.tiny_result_flushed, ops.ftz.denormal_input_flushed). Also Tools/probes/dsp/selftest.cpp:77-82 (ftz.subnormal_result_flushed) and dsp.units section 8 (units.cpp:522-534). The min/max policy rows are keyed NEON-else-x86 (simd.cpp:376-381); wasm_f32x4_pmin(b, a) and pmax(b, a) reproduce the x86 policy exactly, so the existing non-NEON constants would hold.
- **The DSP path uses no libm, so musl's libm cannot move audio bits; the parameter maps and analysis do use libm, and their golden rows are tolerance rows by rule.**
  Evidence: CLAUDE.md layer rules (libm lint on core/engine/modes); FastMath.h:3-5 and :47-50. libm users: Source/fcdsp/params/HostParams.cpp, Resolve.cpp, Text.cpp, Source/fcdsp/analysis/Analysis.cpp, and std::ceil in Source/fcdsp/engine/Oversampler.h:137. Source/fcdsp/params/HostParams.h:7-8 ('Golden rows that depend on host-map values use absrel, never exact'). FP flags: cmake/FcmpArch.cmake:80-82 (-ffp-contract=off, never -ffast-math).
- **EngineHost fits a 128-frame render quantum without a FIFO, and latency is fixed by setup only.**
  Evidence: Source/fcdsp/engine/EngineHost.h:56 (n: any length >= 0, chunked internally); EngineHost.cpp:868-872 (chunks aligned to absolute multiples of kChunk = 64, IEngine.h:21). maxBlock is only forwarded to the oversampler, which ignores it (EngineHost.cpp:734; Oversampler.h:65-66). Latency: EngineHost.cpp:766-774; Oversampler.h:49-59 (STD 4, HQ 61 samples) plus lookaheadSamples (Oversampler.h:136-138; 5 or 20 ms). Defaults are quality STD and budget off (Source/fcdsp/params/HostParams.cpp:55-56). The engine's own bypass is latency-aligned (EngineHost.cpp:27-28, :1054-1055); UiFrame carries latencySamples and sampleRate (EngineHost.cpp:1125-1126).
- **The plugin builds BlockParams from 22 raw host-plain values plus the mode slot and budget through fcdsp::resolve, which is JUCE-free and intended to run per block; a web wrapper can do the same.**
  Evidence: Source/plugin/Processor.cpp:347-353 (snapshot), :366-383 (buildBlockParams). Source/fcdsp/params/Resolve.h:3-5, :40-48 (RawParams), :84 (resolve). Source/fcdsp/engine/EngineHost.h:37-58 (HostConfig, BlockParams, ProcessIo). configure() is not safe against a concurrent process(): the plugin calls it under suspendProcessing (EngineHost.h:8-10; EngineHost.cpp:703-764 swaps impl_).
- **The editor reaches telemetry as an EngineHost-style seqlock read and a HistoryRing reference, so a web facade must provide both objects on the UI side.**
  Evidence: Source/plugin/ProcessorFacade.h:113-115 (readUiFrame, history(), setUiAttached). HistoryRing::push is single-producer (HistoryRing.h:49-60). Emscripten's shared-memory audio worklet path exists but depends on WASM_WORKERS and SharedArrayBuffer (/opt/homebrew/Cellar/emscripten/6.0.3/libexec/src/settings.js:1663-1677; system/include/emscripten/webaudio.h:119-139, :172).

## Blockers and options

- **WASM SIMD128 has no deterministic fused multiply-add, while Simd.h's frozen contract says fma/fms round once on every backend and that an unfused fallback is 'a different arithmetic'.** (/Users/seanfunk/audio/plugins/FCompressor/Source/fcdsp/core/Simd.h:11-12, :19, :58-59; 159 call sites in Source/fcdsp)
  Options: (a) Exact software fma in the wasm backend (f64x2 product, TwoSum, round-to-odd, demote): bit-identical to native where no denormal is involved, 55 SIMD instructions per call against 2; my rough estimate is a few percent to about 10 % of one core at HQ, not measured. (b) Unfused mul+add: fast, inaudible difference, but FastMath's measured error bounds and every exact golden no longer apply to the web build. (c) -mrelaxed-simd relaxed_madd: fused only where the engine chooses, non-deterministic by spec, and a module using it fails to load in a browser without relaxed SIMD. I recommend (a) behind a compile-time switch with (b) as the fallback if a benchmark says (a) is too slow.
- **WASM has no flush-to-zero control, and the existing probe log shows GR values go denormal in release tails for 11 of 14 Modes when FTZ is off. On x86 hosts, denormal operands can be very slow, and a stalled one-pole keeps them present indefinitely.** (/Users/seanfunk/audio/plugins/FCompressor/Source/fcdsp/core/ScopedFtz.h:15-19; docs/design/01-core-contracts.md:1322-1326; build-lead/Testing/Temporary/LastTest.log (analysis.ftz notes); scalar recurrences at Source/fcdsp/engine/stages/colour/OctoDist.h:103-112)
  Options: (a) ScopedFtz becomes a no-op on wasm and nothing else: correct sound, unknown CPU behaviour on Intel/AMD. (b) The wasm backend flushes tiny results of mul/div/fma/fms (about 3 extra SIMD ops each), which stops the SIMD recurrences from holding denormals; add a wrapper-level silence gate (reset and skip after the tail when input is exactly zero) for the scalar audio-path filters. (c) Full FZ emulation on inputs and outputs of every op, used only if the probe suite must pass bit for bit. I recommend (b). Unverified from memory: Chrome and WebKit may already run the audio render thread with denormals disabled; a runtime self-test in the worklet (a denormal product computed from volatile operands) would settle it per browser.
- **Three frozen core headers need a third backend body, and the build system needs a configuration that the current platform and arch files refuse.** (Source/fcdsp/core/Simd.h:25-36, :290-304; Source/fcdsp/core/ScopedFtz.h:15-19, :40-70; Source/fcdsp/core/FastMath.h:75-110; cmake/FcmpPlatform.cmake:29-37; cmake/FcmpArch.cmake:33-47, :63-68; CMakeLists.txt:92-93)
  Options: (a) Add `#elif defined(__wasm_simd128__)` branches (bodies only; names and signatures unchanged) and a new web configuration: FcmpPlatform accepts Emscripten, FcmpArch gets a wasm32 arch with `-msimd128` and the same FP flags, PIC off, built from a new preset with the Emscripten toolchain file. (b) Leave the main CMake alone and add a small standalone CMake project for the web target that globs Source/fcdsp itself. Either way this is lead-owned work and probably wants its own ADR.
- **Whether the DSP probe suite should run under wasm (node) as verification, given that the harness and several probes are arm64/x86-only and the xarch rule forbids overlays.** (/Users/seanfunk/audio/libraries/FunkGui/include/funkgui/test/Harness.h:141, :855-858; Tools/probes/dsp/{units,analysis,hostile,simd,selftest}.cpp; docs/design/03-build-verify-process.md:75-78)
  Options: (a) Do not port the probes; verify the wasm build by rendering the dsp.print material in node and comparing hashes with tests/golden/base by script. (b) Port fcmp_probe_dsp to node as an informational run (not a verify.sh gate): needs a FunkGui release that accepts a wasm32 arch and a no-op ScopedFtz, plus wasm branches in five probe files, with the FTZ-control rows expected to fail or be skipped. (c) Make it a gate with full FZ emulation. I recommend (a) first; (b) only if the hashes differ and need diagnosing.
- **How the audio thread and the editor share the engine: one shared-memory module or two instances with messages.** (Source/fcdsp/engine/EngineHost.h:8-10, :87-91; Source/plugin/ProcessorFacade.h:113-115)
  Options: (a) Two instances, no shared memory: the DSP module lives in the AudioWorkletGlobalScope, the editor module on the main thread; parameters go down and UiFrame plus new HistoryColumns come up over the MessagePort into a local mirror. No COOP/COEP headers, works on any static host, no configure handshake (messages are handled between quanta). (b) One module with SharedArrayBuffer and Emscripten's AUDIO_WORKLET/WASM_WORKERS: the seqlock and ring work as designed across threads, but the page must be cross-origin isolated and configure() needs a suspend handshake with the worklet. The C API below is the same for both; I recommend (a) for a demo. This overlaps the editor agent's area.

## Recommended approach

Add a native WASM SIMD128 backend to fcdsp, a separate web build configuration, and a C wrapper around EngineHost. Do not use Emscripten's SSE or NEON emulation: both give an unfused fma, and each fails on at least one hard error or wrong op.

**1. Backend (three headers, bodies only)**
- **Simd.h:** add `#elif defined(__wasm_simd128__)` including `<wasm_simd128.h>` and defining `FCDSP_SIMD_WASM`.
  - Types: make `f32x4` a float vector type (16-byte aligned) and `m32x4` `v128_t`, mirroring NEON's distinct mask type. `v128_t` is an int32 vector, so a stray `a + b` on it would be an integer add.
  - Direct maps: load/store/splat, add/sub/mul/div, sqrt, floor, abs, neg, gt/ge, `wasm_v128_bitselect` for sel (no asm fence), and/or, extract/replace lane.
  - min/max: `wasm_f32x4_pmin(b, a)` and `pmax(b, a)`. This reproduces the x86 second-operand policy bit for bit, so dsp.simd's existing non-NEON constants hold.
  - fma/fms: the exact f64x2 round-to-odd form (see scratchpad/fma_ro.cpp and wasm_backend.cpp), behind a switch such as `FCDSP_WASM_FMA_UNFUSED` for the fallback.
  - rsqrte/rsqrts: `detail::frsqrteBits(x, false)` per lane and `detail::rsqrtsPortable`.
  - Denormals: flush results of mul/div/fma/fms with magnitude below FLT_MIN to zero.
- **ScopedFtz.h:** add a wasm branch with an empty constructor and destructor.
- **FastMath.h:** add wasm branches for log2Split, scaleByPow2 (`wasm_i32x4_trunc_sat_f32x4`) and xorSign.

**2. Build**
- New web configuration with the Emscripten toolchain file. Flags: `-msimd128 -O3 -fno-math-errno -fno-trapping-math -ffp-contract=off`, no `-mrelaxed-simd`, no `-pthread` for the two-instance design, PIC off.
- Link the DSP module standalone: `-sSTANDALONE_WASM --no-entry`, exported `fcmp_*` plus malloc/free, memory growth off.

**3. C API (new file, e.g. Source/web/FcmpWeb.h; not in fcdsp)**
```c
typedef struct FcmpWeb FcmpWeb;
FcmpWeb* fcmp_create(void);
void     fcmp_destroy(FcmpWeb*);
int      fcmp_configure(FcmpWeb*, double sampleRate, int maxBlock, int quality, int budget, int inChannels, int outChannels); /* returns latency in samples; allocates */
int      fcmp_latency_samples(const FcmpWeb*);
double   fcmp_tail_seconds(const FcmpWeb*);
float*   fcmp_params(FcmpWeb*);                 /* float[30], host-plain values in Pid order */
void     fcmp_params_changed(FcmpWeb*, int snap); /* re-resolve at the next process; snap=1 also requestSnap */
float*   fcmp_input(FcmpWeb*, int channel);     /* float[maxBlock], module-owned */
float*   fcmp_output(FcmpWeb*, int channel);
void     fcmp_process(FcmpWeb*, int frames, int inChannels);
void     fcmp_reset(FcmpWeb*);
void     fcmp_set_ui_attached(FcmpWeb*, int attached);
int      fcmp_read_ui_frame(const FcmpWeb*, void* out288);
int      fcmp_drain_history(FcmpWeb*, void* outColumns32, int capacity, int* gap); /* keeps its own cursor: no i64 across JS */
int      fcmp_ftz_active(void);                 /* runtime self-test */
```
- `fcmp_process` builds BlockParams as Processor::buildBlockParams does (RawParams, resolveSlot, resolve, the globals), with no key bus and extKey false.
- quality or budget changes go through `fcmp_configure`, since they change latency.

**4. Worklet**
- Create the context at a fixed rate (`new AudioContext({ sampleRate: 48000 })`) so the engine never follows a 24 kHz Bluetooth device, and pass the worklet's `sampleRate` to `fcmp_configure`.
- Per quantum: copy inputs into the module's buffers, `fcmp_process(h, out[0].length, channels)`, copy out. Use the array length rather than a hard-coded 128.
- Use the engine's own bypass for A/B (it is latency-aligned); Web Audio has no latency compensation to report to.
- After the input has been exactly zero for longer than `fcmp_tail_seconds`, call `fcmp_reset` and skip processing until audio returns.
- Post UiFrame and drained history columns to the main thread at about 60 Hz.

**5. First spike, before committing to the design**
- Render the dsp.print material in node and compare hashes with tests/golden/base.
- Benchmark exact against unfused fma at ECO, STD and HQ in Chrome and Safari on one Intel and one Apple machine.
- Run the `fcmp_ftz_active` self-test in each browser's worklet.

## Effort notes

What I ran: read-only inspection of both repositories and Emscripten's headers, plus scratch-only compiles in the session scratchpad (em++ `-fsyntax-only`, `-S` and `-dM -E` on small snippets; a native clang test of the exact fma). Nothing in FCompressor or FunkGui was built, run or edited, and no fcdsp source was compiled for wasm, so the list of refusals comes from reading, not from a compiler run over the project.

Not verified:
- Performance of the exact fma in a browser; the CPU figures are my estimate from op counts.
- Whether print hashes and xarch rows actually hold under wasm.
- Over-aligned `operator new` under Emscripten.
- Whether Chrome, Firefox or Safari run the audio render thread with denormals disabled (from memory only, no network used).
- Whether clang 23's warning set stays clean under the project's `-Werror` list.
- Scalar per-sample state: I found OctoDist's by grep, but did not audit all 14 Modes for others.

Rough size of the work:
- Backend bodies in the three headers: about 120 lines.
- C wrapper: about 200 lines.
- Web CMake configuration: about 80 lines.
- Worklet JS: about 150 lines.
- Porting fcmp_probe_dsp to node, if wanted, is the largest optional piece: five probe files, ProbeMain, and a FunkGui harness release.

Adjacent, outside my area: the editor's PreviewWorker is a juce::Thread (/Users/seanfunk/audio/plugins/FCompressor/Source/editor/PreviewWorker.cpp:25, :243-246), so the editor side has its own JUCE and threading questions.

Scratch files: /private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/{fma_ro.cpp, wasm_backend.cpp, wasm_backend.s, fence.cpp, sse.cpp}.
