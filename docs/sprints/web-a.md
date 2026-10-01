# Web Sprint A — the engine in wasm, measured; FunkGui core without JUCE: task manifests

Status: **sprint base**, written by the lead from the judged plan (`docs/sprints/web/plan.md`, ADR-93 design pass,
2026-10-01). Manifest format as in `docs/sprints/s0.md` (read by
`Scripts/sprint/ownership.py docs/sprints/web-a.md#<ID> <worktree>`). The user authorised this sprint ("go"); Sprint B
needs a new authorisation.

Goal. Prove fcdsp runs on wasm32 with the plugin's exact arithmetic, measured, behind a C ABI the browser's audio thread
can drive; and make FunkGui's core buildable without JUCE. Nothing here changes the native plugin: the native goldens
prove it.

**Base.** Branch `web/sprint-a` at the commit that adds this file (on `main` 3731e07): FZ0–FZ5 frozen; FunkGui pin
v0.11.1 = `923ec44b8c5fb6f5d46f855555eca815ab31f939`.

**The lead has already made** (read them first): the `web` configuration (`CMakeLists.txt`, `cmake/FcmpPlatform.cmake`,
`FcmpArch.cmake`, `FcmpDeps.cmake`, `CMakePresets.json`: `cmake --workflow --preset web-verify`), `cmake/FcmpWeb.cmake`
(targets, and tests that register themselves from `// FCMP_WEB_TEST` lines), `Tools/web/WebCheck.h` and `Main.cpp` (the
check program's subcommands), the lint rules `web.juce` and `web.engine`, and `-DFCOMPRESSOR_WEB_FMA=exact|unfused`.

**Read before writing anything:** `docs/sprints/web/plan.md` (the plan; this manifest is binding where they differ) and
the scout report for your area in `docs/sprints/web/` (`map-dsp.md`, `map-build.md`, `map-funkgui.md`, `map-editor.md`,
`map-render.md`). The scouts' scratch experiments are in `docs/sprints/web/scratch/` (not built; reference only).

**Emscripten** 6.0.3 is installed (`em++`, `em-config`, node on PATH). Its system-library cache is shared by all three
cards: a first build of a new variant prints "generating system library" and takes a while; if a build fails with a
cache lock or a half-written library, wait and retry rather than clearing the cache.

**Frozen set.** FZ0–FZ5 as in `docs/sprints/s13.md`. This sprint's approved revisions: F-W adds a third backend branch to
`Source/fcdsp/core/{Simd.h,ScopedFtz.h,FastMath.h}` (no declaration changes, no change to the NEON or SSE branches);
W-E moves `program()` and `setOf()` out of `Tools/probes/dsp/print.cpp` into a shared header (the hashes must not move);
G-A makes additive FunkGui API changes for a MINOR release (the lead tags).

**Lead-owned, as always:** `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `docs/**`, `tests/golden/**`,
`.github/**`, `README.md`, `CLAUDE.md`. If the lead's skeleton has a bug that blocks you, make the minimal edit, keep its
meaning, and list it under "ownership deviations needing approval" in the handoff.

---

## F-W

```text
TASK    F-W   card: WEB-A.1   repo: FCompressor (isolation: worktree)   size: M
        branch: web-a/f-w (rename the harness-made branch: git branch -m web-a/f-w)
GOAL    fcdsp on wasm32: a native WASM SIMD128 backend with the contract's exact arithmetic
OWNS    Source/fcdsp/core/Simd.h Source/fcdsp/core/ScopedFtz.h Source/fcdsp/core/FastMath.h
        Source/fcdsp/core/FlushTiny.h Tools/web/simd*.cpp
FROZEN  FZ0–FZ5. In the three core headers: no declaration changes and no edit inside the NEON or SSE branches; you add
        a wasm branch beside them. Every path outside OWNS: never edit (W-E owns Source/web/** and Tools/web/engine*).
FUNKGUI pin v0.11.1 = 923ec44b8c5fb6f5d46f855555eca815ab31f939. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/map-dsp.md (the whole report: what Emscripten's emulation gets wrong, the op-by-op mapping,
        the exact-fma method) docs/sprints/web/scratch/{fma_ro.cpp.txt,wasm_backend.cpp.txt}
        docs/sprints/web/plan.md ("Decisions" 1) Source/fcdsp/core/Simd.h (the contract at the top) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Simd.h: `#elif defined(__wasm_simd128__)` including <wasm_simd128.h>, defining FCDSP_SIMD_WASM. f32x4 a
           float vector type and m32x4 a distinct mask type (as NEON has), every op of the frozen set mapped natively.
           min/max with the x86 second-operand policy (wasm pmin/pmax), so dsp.simd's existing non-NEON constants hold.
           sel = wasm_v128_bitselect (no asm fence). rsqrte/rsqrts through the portable helpers already in Simd.h.
        2. fma and fms with ONE rounding, exactly as hardware fma (the contract, Simd.h's header): built only from
           wasm-expressible operations (the scout's round-to-odd method, or the cheaper variant the plan suggests: a
           plain f64 multiply-add, taking the round-to-odd path only for lanes that land exactly on a float midpoint).
           No libm (lint fcdsp.libm), no scalar loop if a vector form exists. When FCDSP_WASM_FMA_UNFUSED is defined
           (cmake -DFCOMPRESSOR_WEB_FMA=unfused) they are a multiply, then an add: the measured fallback only.
        3. Denormals: wasm has no FTZ/DAZ. Results of add, sub, mul, div, fma and fms whose magnitude is below FLT_MIN
           become signed zero (decided after rounding, the x86 rule Simd.h already allows). No denormals-are-zero
           emulation on operands. If flushing add/sub costs measurably, put that half behind a macro and report both.
        4. ScopedFtz.h: a wasm branch with an empty constructor and destructor. FastMath.h: wasm branches for
           log2Split, scaleByPow2 and xorSign (and anything else there written with raw intrinsics).
        5. Tools/web/simd.cpp (see Tools/web/WebCheck.h): subcommand `simd`, test
           `// FCMP_WEB_TEST name=web.simd timeout=300 on=all args=simd`. It must pass on EVERY backend (arm64 NEON,
           x86-64 SSE, wasm), which is what makes it a contract check: every op against a scalar IEEE reference over
           edge sets (take dsp.simd's sets, Tools/probes/dsp/simd.cpp, leaving out denormal OPERANDS); fma/fms equal to
           std::fma bit for bit over at least 10^7 triples (random bit patterns, cancellation, exact midpoints, results
           near FLT_MIN, infinities and NaN payload-insensitive); the tiny-result rule; and fcdsp's FastMath functions
           (log2, exp2, tanh, logCosh, tanPi, sinPi, cosPi) hashed over fixed normal-range sweeps and compared with
           constants you record from the native arm64 build, so the wasm run must reproduce native bits. With
           FCDSP_WASM_FMA_UNFUSED the exact rows become NOTE lines giving the mismatch rate (the test still passes).
           A second subcommand `simdbench` (no test) prints ns per op for fma exact, fma unfused, mul, add, and for
           log2/exp2/tanh per lane.
        ACCEPTANCE (part of DONE)
        [DSP]   cmake --workflow --preset dsp-verify && Scripts/verify.sh "$WT/build-dsp": 0 blocking, 0 drift (the
                native backends are untouched), and web.simd passes natively.
        [AGENT] cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0 blocking, 0 drift.
        [WEB]   cmake --workflow --preset web-verify: fcdsp and fcmp_web_check build warning-free under Emscripten
                (-Werror), web.simd passes under node. Then the same with a second build directory configured
                -DFCOMPRESSOR_WEB_FMA=unfused (cmake -S . -B build-web-unfused --preset web is not possible: use
                `cmake --preset web -B "$WT/build-web-unfused" -DFCOMPRESSOR_WEB_FMA=unfused`).
        Numbers in the handoff: simdbench under node for exact and unfused; the op count of your fma; whether add/sub
        flushing is on.
NOTES   Emscripten 6.0.3 is Clang 23: the warning list has not met it. `std::atomic` without -pthread lowers to plain
        loads and stores; fcdsp needs no threads. The x86 CI runner, not this Mac, shows denormal cost: the lead runs
        that at integration.
```

## W-E

```text
TASK    W-E   card: WEB-A.2   repo: FCompressor (isolation: worktree)   size: L
        branch: web-a/w-e (rename the harness-made branch: git branch -m web-a/w-e)
GOAL    The engine module: a C ABI over fcdsp::EngineHost for an AudioWorklet, and the checks that prove it
OWNS    Source/web/engine/** Tools/web/engine*.cpp Tools/probes/common/PrintProgram.h Tools/probes/dsp/print.cpp
        web/tests/**
FROZEN  FZ0–FZ5. Source/fcdsp/** is read-only for you (F-W owns the three core headers; see NOTES for a local,
        uncommitted stand-in). Tools/web/Main.cpp and WebCheck.h are the lead's. Every path outside OWNS: never edit.
FUNKGUI pin v0.11.1 = 923ec44b8c5fb6f5d46f855555eca815ab31f939. No FunkGui change in this card.
DONE    03 §4.7 (ownership clean; no warnings; verify.sh 0 blocking; <= 60-line handoff) plus the Acceptance below.
READS   docs/sprints/web/plan.md (Decisions 2, 6; Sprint A) docs/sprints/web/map-dsp.md ("AudioWorklet fit", the C API
        sketch) docs/sprints/web/map-build.md (the page and worklet loading, node flags) cmake/FcmpWeb.cmake
        Tools/web/WebCheck.h Source/fcdsp/engine/EngineHost.h Source/plugin/Processor.cpp (buildBlockParams, render,
        the SetupWatcher's quality/budget rule: the model for raw values -> BlockParams and for reconfiguring)
        Tools/probes/plugin/EngineFacade.{h,cpp} Tools/probes/common/EngineRig.h Tools/probes/dsp/print.cpp
        docs/ARCHITECTURE.md §7 (real-time rules) CLAUDE.md
INPUTS  (none)
DELIVERABLES
        1. Source/web/engine/{WebEngine.h,WebEngine.cpp,WebProtocol.h}: portable C++ over fcdsp alone (lint web.engine:
           no JUCE, no Emscripten header, no funkgui/, plugin/ or editor/ include). A C ABI (extern "C", names
           fcmp_web_*), each function marked with a macro that is `__attribute__((export_name("<name>")))` under
           `#if defined(__wasm__)` and nothing natively:
             create / destroy; configure(sampleRate, maxBlock) -> latency samples (the only allocation point besides
             create); process(in L/R, out L/R, frames) for any frame count (the worklet gives 128); a message entry:
             post(bytes, n) and a reply buffer the caller reads; latency; a self-check that renders a short fixed
             program inside the module and returns a 64-bit hash (two i32 halves or a pointer: wasm32 exports cannot
             return i64 to JavaScript without BigInt glue, so do not rely on it).
           WebProtocol.h is the byte protocol, defined once in C++ (JavaScript only moves bytes): messages
           Params (the 30 plain parameter values in Pid order plus a snap flag), Attach (the UI is listening), Reset;
           reply: the latest UiFrame, the HistoryRing columns published since the last reply, a gap flag, the latency.
           Little-endian, versioned header, fixed sizes, no pointers; static_asserts on every layout.
        2. Behaviour: raw values -> resolveSlot -> resolve -> BlockParams exactly as Processor::buildBlockParams, with
           the globals (bypass, delta, listen, extkey, output). A quality or lookahead-budget change reconfigures
           inside the message handler, between quanta (the worklet has no other thread: ADR-93 records this deviation
           from "configure only from prepareToPlay/SetupWatcher"; say so in a comment). Denormal input samples are
           zeroed before the engine sees them (wasm has no DAZ). A silence gate: after exactly-zero input for longer
           than the engine's tail, reset the engine and output zeros until signal returns; it can be turned off (the
           tail test needs it off). No allocation, lock or libm in process().
        3. Tools/probes/common/PrintProgram.h: print.cpp's program() and setOf() (and what they need) moved there,
           JUCE-free and harness-free; print.cpp includes it. dsp.print's hashes must not move.
        4. Tools/web/enginecheck.cpp (see Tools/web/WebCheck.h; it links fcmp_web_engine_lib). Subcommands and tests:
             print      `// FCMP_WEB_TEST name=web.engine.print timeout=600 on=all args=print,{golden}`: every
                        registered Mode's dsp.print material through the C ABI in 128-frame quanta at the plugin's
                        default setup, each channel hashed as the Harness's hashFloats does (FNV-1a over the float bit
                        patterns: read FunkGui's include/funkgui/test/Harness.h and reproduce it), compared with
                        tests/golden/base/modes/<key>/dsp.print.txt (8 rows per Mode, 112 in all). Also print, per
                        set, a hash of the raw parameter values after the libm-using host mapping, so a musl-libm
                        difference in lo/hi/mid can be told from a DSP difference. Print PASS/FAIL per row.
             selfcheck  on=all: prints the C ABI self-check hash; the test passes when it equals a constant recorded
                        from the native build.
             tail       on=all, timeout 900: per Mode, a burst then 10 s of silence with the gate OFF must cost less
                        than 2x the active-signal time per block (denormal slowdown; trivially true natively).
             speed      on=web, timeout 900: prints the real-time factor per Mode at ECO, STD and HQ (48 kHz, 128-frame
                        quanta); fails if the worst Mode at HQ is below 4x real time.
        5. web/tests/engine.mjs: `// FCMP_WEB_TEST name=web.engine.abi timeout=120 args={engine},{build}`: node
           instantiates fcmp-engine.wasm with an EMPTY import object (as the worklet will), asserts the import list is
           empty and the export list is the C ABI plus memory, malloc, free and the runtime's few, calls _initialize,
           runs the self-check and compares with the constant.
        ACCEPTANCE (part of DONE)
        [DSP]   cmake --workflow --preset dsp-verify && Scripts/verify.sh "$WT/build-dsp": 0 blocking, 0 drift
                (dsp.print.* unchanged), and web.engine.print, web.engine.selfcheck and web.engine.tail pass natively:
                the 112 rows equal the goldens through the C ABI in 128-frame quanta. This proves the wrapper.
        [AGENT] cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent": 0 blocking, 0 drift.
        [WEB]   with the stand-in of NOTES: cmake --workflow --preset web-verify builds fcmp-engine.wasm and
                fcmp_web_check and runs web.engine.abi, selfcheck, tail and speed. With the stand-in's unfused fma the
                print rows and the selfcheck constant may differ under wasm: report exactly which rows differ. The lead
                runs the real thing after merging F-W.
        Numbers in the handoff: fcmp-engine.wasm's size, its import and export lists, the speed table.
NOTES   Until F-W merges, fcdsp does not compile for wasm (Simd.h #error). To build the web configuration in your
        worktree, patch Simd.h, ScopedFtz.h and FastMath.h LOCALLY with a throwaway wasm branch (start from
        docs/sprints/web/scratch/wasm_backend.cpp.txt; an unfused fma is fine) and NEVER commit those three files:
        `git checkout -- Source/fcdsp/core` before your handoff commit, and say in the handoff that you did.
        There are 30 parameters (Pid.h), not 29. performance.now() does not exist in the worklet scope: the engine
        does not time itself. fcmp_web_check runs under node with NODERAWFS, so it reads the goldens by path.
```

## G-A

```text
TASK    G-A   card: WEB-A.3   repo: FunkGui (lead-made worktree)   size: L
        worktree: /Users/seanfunk/audio/libraries/FunkGui.wt/web-a-ga   branch: web-a/g-a
GOAL    FunkGui's core without JUCE: a build option, a pre-baked font atlas, preferences behind a backend
OWNS    CMakeLists.txt cmake/** CMakePresets.json include/funkgui/** src/** test/** tools/** fonts/**
        (not: SEED.tsv docs/PROVENANCE.md test/golden/** CHANGELOG.md, and the VERSION in CMakeLists.txt stays 0.11.1)
FROZEN  FunkGui's public headers are API: additive changes only, each listed in the handoff for the lead's MINOR tag.
        Nothing under include/funkgui/gpu or src/gpu changes except what the build option strictly needs. Never bless:
        no golden row may move (a moved row is a bug in the card). The harness stays header-only and JUCE-free.
FUNKGUI base main = v0.11.1 = 923ec44b8c5fb6f5d46f855555eca815ab31f939 (your branch starts there).
DONE    FunkGui's CLAUDE.md "Done means" (agent and agent-gui build with no warnings; tools/verify.sh exits 0; a reason
        per candidate group; the cross-repo check) plus the Acceptance below.
READS   /Users/seanfunk/audio/plugins/FCompressor/docs/sprints/web/map-funkgui.md (the whole report: every JUCE
        dependency with file:line, the recommended seam, the atlas, the prefs backend) and docs/sprints/web/plan.md
        (Sprint A, FunkGui card G-A) in the same repository; FunkGui's CLAUDE.md and CHANGELOG.md
INPUTS  (none)
DELIVERABLES
        1. Option FUNKGUI_WITH_JUCE (default ON) and an interface define FUNKGUI_HAS_JUCE=0|1 beside FUNKGUI_HAS_BGFX.
           OFF: no JUCE check or version assert, JUCE links are no-ops, the JUCE-only sources leave the core, presets,
           JUCE tools and JUCE-app tests are off, and the font and atlas are embedded without juce_add_binary_data.
           ON: everything exactly as today (same targets, same flags, same goldens).
        2. The core JUCE-free by construction: whatever includes JUCE outside gpu/ and presets/ lives under src/juce/
           (the body of FontAtlasSdf::bake, writePng's encoder, printable(const juce::String&), JuceParamPort.cpp, the
           PropertiesFile preferences backend, MenuLook). The files they came from keep their JUCE-free parts.
        3. The atlas: FontAtlasSdf::load(const uint8_t*, size_t) and serialise() with a versioned header (magic, atlas
           size, base px, spread, glyph count, pixel hash); a committed blob of the macOS bake under fonts/; a tool
           mode that writes it; FontService::atlas() loads the embedded blob when FUNKGUI_HAS_JUCE is 0 and bakes as
           today otherwise; test fg.font.baked (registered on Apple only: the bake is the platform's) asserting the
           committed blob equals the live bake: pixels, every glyph record, the metrics.
        4. UiPreferences behind a storage backend: PropertiesFile under JUCE (file format, paths and behaviour
           unchanged), in memory without JUCE, and a setBackend() hook for a host (the browser's localStorage later).
           file() and defaultFile() are declared only when FUNKGUI_HAS_JUCE; the header includes no JUCE without it.
        5. src/core/CLocale.cpp: an __EMSCRIPTEN__ branch (its libc declares no strtol_l). A HeadlessGuiScope (holds
           JUCE's GUI initialiser when JUCE is present, nothing otherwise) for tests and consumers' probes.
        6. Harness.h accepts --arch wasm32 (its ScopedFtz a no-op there); tools/golden.py knows wasm32.
        7. Presets: a native `nojuce` (FUNKGUI_WITH_JUCE=OFF on the host compiler, building the core and running the
           JUCE-free fg.* tests against the existing goldens), and a `web` configure/build preset with Emscripten's
           toolchain that compiles the core (no tests needed under node yet).
        ACCEPTANCE (part of DONE)
        cmake --workflow --preset agent-verify && tools/verify.sh "$FWT/build-agent" and the same for agent-gui: 0
        blocking, 0 drift, 0 missing (the new fg.font.baked row aside, which is spec-only or arrives as one candidate
        with its reason); fg.font.baked passes on this Mac.
        The nojuce preset builds warning-free and its tests pass against the existing goldens with no drift.
        The web preset compiles FunkGui's core with Emscripten 6.0.3, warning-free.
        [XV] FCompressor's gate against your worktree, in the lead-made read-only worktree
        /Users/seanfunk/audio/plugins/FCompressor/.claude/worktrees/ro-web-a-ga:
          cd <that worktree> && cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<your worktree> &&
          cmake --build --preset agent && Scripts/verify.sh "<that worktree>/build-agent": 0 blocking, 0 drift.
        Build there, never edit it.
NOTES   The lead tags (v0.12.0 after Sprint B adds the services and the WebGL2 sink) and writes CHANGELOG.md: put the
        entry's text in your handoff, every API addition listed. Do not touch src/gpu's rendering path. An Emscripten
        build shares one system-library cache with two FCompressor cards: retry on a cache lock.
```
