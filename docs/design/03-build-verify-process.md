# 03: Build, verification and agent process (design appendix C)

Status: **design appendix, synthesised 2026-09-22 from Draft 3 and critiques K1 (consistency), K2 (risk) and K3
(parallel build).** Scope: repository layout, CMake, the verification suite, how agents work in sprints (including the
**one** sprint plan, §4.9), and the release script outline. **01** (core contracts) owns the engine, the parameter
model, the Mode descriptor and the canonical source tree (01 §2.1); **02** owns FunkGui's API and the editor. This
appendix owns build, verification and process, and uses their spellings (`ModeDescriptor`, `EditorHost`,
`funkgui::Canvas`, `analysis::staticGain`, …; K1 #18). The top-level document is `docs/ARCHITECTURE.md`; decisions and
rejected options are in `docs/DECISIONS.md`.

**FZ0 errata** (S0 review `docs/sprints/s0-review.md` R-B0 #1-#13 and R-G1 #1, applied by card FX-B before FZ0) are
marked "FZ0 errata" in place: §1.1, §2.3, §2.6, §2.9, §2.10, §3.2.4, §3.2.5, §4.7 and §4.8.

Evidence tags:
- **[M]** measured on this Mac (Apple M5, 10 cores, macOS 27, Xcode 27.0, CMake 4.3.3, Ninja 1.13.2, git 2.54.0).
  The build-time numbers come from HardwareReverb's own `.ninja_log` files, parsed read-only on 2026-09-22.
- **[D]** derived from [M] numbers with the arithmetic shown.
- Research citations are file and section: A §6.3 means `docs/research/A-gui-stack.md` §6.3. HR means
  `/Users/seanfunk/audio/plugins/HardwareReverb` (read-only), and HR `CMakeLists.txt:229-235` is a file and line range.

---

## 0. Decisions on one screen

1. **Two repositories.** FCompressor lives at `/Users/seanfunk/audio/plugins/FCompressor`. FunkGui lives at
   `/Users/seanfunk/audio/libraries/FunkGui` (its own git repo, project and target `FunkGui`, namespace `funkgui`).
   FCompressor consumes FunkGui by FetchContent at a pinned tag, and consumes **only tags** (§4.5). FunkGui also owns the
   test harness (`FunkGui::harness`, namespace `funkgui::test`), so FunkGui's tests, FCompressor's probes and, later,
   HR's migration all use one harness (pending the user's confirmation, `DECISIONS.md` Q3).
2. **One machine-wide read-only dependency cache: `~/audio/.deps`.** It is populated once by `Scripts/deps.sh`, and
   `cmake/FcmpDeps.cmake` points the `FETCHCONTENT_SOURCE_DIR_*` variables at it automatically. There is no shared
   `FETCHCONTENT_BASE_DIR` (§2.4).
3. **Prebuilt `shaderc` in the cache.** 97.5 % of the CPU time of a cold bgfx build is the shaderc toolchain
   (2,060 of 2,112 CPU-s [M]). It is built once per machine, stamped with the bgfx.cmake SHA, and used by every GUI
   build (§2.5).
4. **Version assertions on every dependency, override or not.**
   - JUCE: the header version must equal 8.0.4, and the git SHA must equal the pin.
   - bgfx: `BGFX_API_VERSION` must be 153, and the bgfx.cmake and submodule SHAs must equal the pins.
   - FunkGui: the SHA must equal the pin. With `FETCHCONTENT_SOURCE_DIR_FUNKGUI` set, the override's HEAD must descend
     from the pinned SHA (`git merge-base --is-ancestor`, K2 #26b), and the override is printed loudly.
5. **`fcdsp` is a JUCE-free static library** (`Source/fcdsp/`, the canonical tree of 01 §2.1). The plugin and every
   probe link the same archive. Consequences:
   - Its include path has no JUCE, so a JUCE include fails to compile. The rule is enforced by construction.
   - The oversampler is FCompressor's own, not `juce::dsp::Oversampling` (§3.10 #5).
   - The test tap is a runtime pointer, not a compile flag (§3.10 #4).
6. **Three configurations** (options `FCOMPRESSOR_*`, Release by default):
   - GPU: bgfx, the real editor.
   - **Headless** (`FCOMPRESSOR_HEADLESS=ON`): no bgfx, but every UI probe still runs, because FunkGui's recorder is
     GPU-free.
   - **DSP-only** (`FCOMPRESSOR_DSP_ONLY=ON`): no JUCE at all. Configure takes about 2 s.
7. **Floating point is the same on every target:**
   - `-ffp-contract=off -fno-math-errno -fno-trapping-math -O3`, and never `-ffast-math`.
   - ISA flags per slice as `SHELL:-Xarch_<arch> <flag>` pairs (§2.6).
   - LTO only in Release, and only on the plugin and on `fcdsp`'s bitcode. Probes never LTO-compile JUCE.
8. **Three probe executables.**
   - `fcmp_probe_dsp`: `fcdsp` + harness. No JUCE.
   - `fcmp_probe_plugin`: processor + editor `Panel` + FunkGui core + harness. JUCE is compiled once for all probes.
   - `fcmp_bench`: not gating.

   Each executable takes a subcommand `<layer>.<name>`. **Probes self-register**: one file per probe,
   `Tools/probes/{dsp,plugin}/<name>.cpp`, whose first line declares its layer, scope and timeout; CMake reads that
   line and generates one CTest test per probe × Mode over `Source/fcdsp/modes/Modes.def` (§2.9; K3 #3). Adding a
   probe edits no shared file.
9. **`verify` is a CTest label.**
   - `ctest -L verify -j4` runs everything and never stops at the first failure.
   - The `verify` build target is only a convenience wrapper around that call.
   - `Scripts/verify.sh` is the agent's pass/fail gate. It sorts failures into blocking ones and golden candidates.
10. **Harness v2.**
    - Spec checks cannot be blessed. Golden checks detect drift.
    - Tolerance grammar: `exact`, `abs:`, `rel:`, `absrel:`, `le:`, `ge:`.
    - Duplicate keys are an error.
    - Probes cannot write into the source tree. They only emit `--bless-to` candidates into the build directory.
      Blessing is `Scripts/golden.py adopt`, which only the lead runs, from the main checkout, with
      `FCMP_ALLOW_BLESS=1`.
11. **Goldens use a symmetric per-arch overlay.**
    - Base files are in `tests/golden/base/`. Overlays are in `tests/golden/{arm64,x86_64}/` and hold only the rows
      that differ on that arch.
    - Rows whose keys start with `xarch.` may never be overridden by an overlay.
    - **Rosetta 2 is not installed on this Mac** [M], so x86_64 cannot be verified today. **v1 therefore ships
      arm64-only** unless an x86 verify has passed: `release.sh` refuses a universal build without
      `build-lead-x86/verify-passed-<sha>` (§5; K2 #15; user confirmation `DECISIONS.md` Q6).
12. **Agents.**
    - At most 3 at once **across both repositories**, each building with `-j4 -l 12` and testing with `ctest -j4`.
    - Each works in its own worktree and its own build directories (`<worktree>/build-<preset>`), on files it owns
      (§4.6). The build is designed so that no task needs to edit a shared file (§2.1, §2.9).
    - Agents never commit, never bless and never install. The lead commits, merges, blesses, tags FunkGui, bumps the
      pin and tags the sprint (§4.8).
13. **Host-validation and real-time gates.** `Scripts/validate.sh` (auval `-strict` + pluginval strictness 10) runs at
    every sprint end and before release; an `rtsan` preset (or, where unsupported, a lock/syscall interposer) and a
    `tsan-agent` preset cover what an allocation counter cannot (§2.10, §4.8; K2 #18–19).

---

## 1. Repositories and layout

### 1.1 FCompressor tree

The source tree (`Source/`, `Tools/probes/`, `tests/`) is **01 §2.1**, the one canonical tree (K1 #1, K3 #1). This
section adds the build- and process-facing files:

```
FCompressor/
  CMakeLists.txt                 top level, ~120 lines: project(), options, include(cmake/*.cmake), enable_testing()
  CMakePresets.json              §2.10 (schema version 6: configure, build, test and workflow presets)
  CLAUDE.md                      the agent rules of §4.6-§4.7 in short form, loaded by every agent session
  README.md  LICENSE             GPL-3.0, as HR (B §5)
  cmake/
    FcmpArch.cmake               FCMP_ARCHS, FCMP_RUN_ARCH, FCMP_CAN_RUN_PROBES, fcmp_flags, fcmp_lto, fcmp_warnings
    FcmpDeps.cmake               pins, ~/audio/.deps defaults, FetchContent, version assertions, prebuilt shaderc
    FcmpSources.cmake            the glob → target map (§2.1); written once in Sprint 0 (B0), never edited again
    FcmpPlugin.cmake             juce_add_plugin(FCompressor ...), resources, xattr hook, editor choice
    FcmpProbes.cmake             probe executables, self-registered CTest tests (§2.9), verify / verify-gui-live
    FcmpProduct.h.in             configure_file -> ${binary}/generated/FcmpProduct.h (name, version, codes, env prefix)
    FcmpBuiltFrom.cmake          -P script: built-from-{probes,plugin}.txt, the tree a build came from (FZ0 errata, §2.9)
    LintDeps.cmake               -P script: the lint.deps test (01 §2.2 include, libm and static rules)
  Source/                        01 §2.1: fcdsp/ (core params engine modes telemetry analysis), plugin/, editor/ (+ gpu/)
  Tools/
    probes/common/               ProbeMain.cpp (subcommand dispatch over the self-registration list, Mode loop,
                                 ScopedFtz), ProbeRegistry.h (FCMP_PROBE macro), Signals.h (own PCG32; no
                                 std::*_distribution), Measure.{h,cpp} (single-bin DFT, t63/t10-90, hf_ratio),
                                 Tolerances.h (C §5.13 table as constexpr), AllocCounter.cpp (operator new/delete
                                 counters), EngineRig.{h,cpp} (§3.4)
    probes/dsp/<name>.cpp        one file per dsp.* probe (§3.4)
    probes/plugin/<name>.cpp     one file per proc.* or ui.* probe (§3.5, §3.6); FakeFacade.{h,cpp}
    bench/Bench.cpp              fcmp_bench (E §3.7, C §5.8 CPU bench)
  tests/
    golden/base/global/<layer>.<name>.txt            arch-neutral rows
    golden/base/modes/<key>/<layer>.<name>.txt       (+ <layer>.<name>.<key>.lines sidecars)
    golden/arm64/...  golden/x86_64/...              overlays: only the rows that differ on that arch (§3.3)
    fixtures/state/<stateVersion>-<key>-<desc>.bin   real state blobs; write-once (C §5.7.3)
    fixtures/modeparam.tsv                           stateVersion, slot, normalised value, key; append-only (C §5.7.4)
    fixtures/modes-ever.tsv                          every slot/key ever released; append-only (registry lint)
  Scripts/
    deps.sh                      populate ~/audio/.deps (§2.4), build shaderc (§2.5) and pluginval (§4.8)
    verify.sh                    DoD gate: ctest -L verify + classification report (§4.7); --integration for the lead
    validate.sh                  auval -strict + pluginval (§4.8); --install puts <build>'s bundles in place first
    check-headers.sh             clang++ -std=c++20 -fsyntax-only on every frozen header (CTest lint.headers)
    golden.py                    5-line wrapper: runs FunkGui's tools/golden.py (the format owner, located through
                                 build-*/fcmp-deps.txt) with FCompressor defaults; report | diff | adopt (§3.2.5)
    gui-live.sh                  live Standalone capture and headless parity, serialised with lockf (§3.6)
    release.sh                   sign, verify, notarise, package (§5)
    sprint/ownership.py          checks a worktree's changed paths against the task's OWNS globs (§4.6); ignores .claude/
  Resources/  FCompressor.entitlements     (the font and the bgfx/bx/bimg licences come from FunkGui, 02 §1.6)
  docs/  ARCHITECTURE.md  DECISIONS.md  research/  design/  sprints/s<N>.md (task manifests, §4.2)  modes/<key>.md
```

**Include rules, enforced by the target graph rather than by review:**

| Directory | May include | Compiled into |
|---|---|---|
| `Source/fcdsp/**` | std, SIMD intrinsics, each other | `fcdsp` only |
| `Source/plugin` | `fcdsp` headers, JUCE, FunkGui core (`ParamPort`) and presets, `FcmpProduct.h` | `FCompressor` (shared code) and `fcmp_probe_plugin` |
| `Source/editor` | `fcdsp` headers, `Source/plugin/ProcessorFacade.h`, FunkGui core, JUCE | GPU plugin and `fcmp_probe_plugin` |
| `Source/editor/gpu` | the above plus FunkGui gpu | GPU plugin only |
| `Tools/probes/dsp` | `fcdsp`, `FunkGui::harness`, `probes/common` | `fcmp_probe_dsp` |
| `Tools/probes/plugin` | everything above except `editor/gpu` | `fcmp_probe_plugin` |

- Processor code reads product constants from the generated `FcmpProduct.h` and never uses `JucePlugin_*` directly.
  HR's StateProbe had to define `JucePlugin_Name` by hand for exactly that reason (HR `CMakeLists.txt:456-460`,
  B §4).
- **Every source list is a per-directory glob** (§2.1), so adding a file — a Mode, a stage policy, a view, a probe —
  edits no CMake (K3 #2). Draft 3's "GLOB only `Source/modes/*.cpp`, everything else explicit" is superseded.
- Every glob and lint is rooted at `${PROJECT_SOURCE_DIR}/Source` or `/Tools`, never `**` from the repo root, because
  agent worktrees live under `.claude/worktrees/` inside the checkout (K3 #26).

`.gitignore` additions to the existing `/build*/`:
- `/CMakeUserPresets.json`
- `/.claude/worktrees/`. Claude Code places harness-made worktrees there. If it uses another path, add that path
  instead.

### 1.2 The FunkGui repository and how it relates to FCompressor

```
/Users/seanfunk/audio/libraries/FunkGui/
  CMakeLists.txt              project(FunkGui VERSION x.y.z LANGUAGES C CXX); VERSION changes only in a tagging commit
  CMakePresets.json           agent, agent-gui, lead (same shape as FCompressor's)
  cmake/FunkGuiDeps.cmake     top-level: the same JUCE/bgfx pins, ~/audio/.deps defaults and SHA asserts as
                              FcmpDeps.cmake; consumed: version checks plus the guarded bgfx fallback (02 §1.4)
  cmake/FunkGuiTargets.cmake  per-directory globs; funkgui_configure_product(), funkgui_compile_shaders(), funkgui_add_font()
  include/funkgui/…  src/…  shaders/  fonts/          (the full layout is 02 §1.1)
  tools/                      FrameRender.cpp, AtlasDump.cpp, capture-frame.sh (app name and env prefix are arguments, A §6.4.8),
                              golden.py (golden format v2 tooling, shared by both repos), verify.sh (FunkGui's DoD gate)
  test/                       FunkGui's own probes + test/golden/{base,arm64,x86_64}/ (same format and rules as §3.3)
  SEED.tsv                    provenance: HR path, HR sha256 at snapshot time, FunkGui path (for HR's later migration)
```

What FetchContent gives FCompressor:

| Target / function | Kind | Available when |
|---|---|---|
| `FunkGui::harness` | INTERFACE, header-only `funkgui/test/Harness.h`. JUCE-free. | always (including DSP-only) |
| `FunkGui::core` | INTERFACE with sources: recorder, PrimList, fingerprint, FontAtlasSdf, Theme, widgets, a11y model, prefs | `juce::juce_gui_basics` exists |
| `FunkGui::gpu` | INTERFACE with sources: BgfxContext, BgfxSink, FramePump, NativeSurface.mm, DisplayLink.mm, `EditorHost`, A11yBridge | target `bgfx` exists |
| `FunkGui::presets` | INTERFACE with sources: FunkPresets (01 §9.2, 02 §1.2); links SQLite3 from the macOS SDK | `juce::juce_audio_processors` exists and `FUNKGUI_WITH_PRESETS` |
| `funkgui_framerender` | executable (EXCLUDE_FROM_ALL): dump to PNG and fingerprint | core available and `FUNKGUI_BUILD_TOOLS` (FCompressor sets it ON) |
| `funkgui_configure_product(<tgt> PRODUCT FCompressor ENV_PREFIX FCMP_ OBJC_PREFIX Fcmp PREFS_FOLDER FCompressor)` | function | always |
| `funkgui_compile_shaders(<tgt>)` | uses `FUNKGUI_SHADERC` if set, else `$<TARGET_FILE:shaderc>` | GPU |
| `funkgui_add_font(<tgt>)` | the OFL licence and the bgfx/bx/bimg/bgfx.cmake licences (from `${bgfx_SOURCE_DIR}`) as bundle resources, not a POST_BUILD copy (HR `CMakeLists.txt:301-332`) | GPU |

How the pieces fit:
- **INTERFACE-with-sources is required, not a STATIC library** (A §6.3). FunkGui must compile with each consumer's JUCE
  configuration.
- When FunkGui is consumed, its CMake **expects** the `juce::` targets, and in the GPU configuration the `bgfx`
  targets, to exist already. It never runs FetchContent for them. It does check them: JUCE 8.0.4 and
  `BGFX_API_VERSION` 153, otherwise FATAL.
- When FunkGui is the top-level project (its own tests and agents), it declares JUCE and bgfx itself with the same
  pins. The two pin sets are small, and each consumer build cross-checks them.
- **Versioning.**
  - Annotated tags `vMAJOR.MINOR.PATCH`. Before 1.0, any API change bumps MINOR.
  - FunkGui publishes `FUNKGUI_VERSION` as a `CACHE INTERNAL` variable so the parent project can read it.
  - FCompressor pins three values in `cmake/FcmpDeps.cmake`: `FCMP_FUNKGUI_TAG`, `FCMP_FUNKGUI_SHA` and
    `FCMP_FUNKGUI_VERSION`. `GIT_TAG` stays the **tag**, as the user decided; the SHA assert after population catches a
    moved tag (then delete `build-*/_deps/funkgui-*` and reconfigure).
  - For an override, the ancestry check (`git -C <dir> merge-base --is-ancestor ${FCMP_FUNKGUI_SHA} HEAD`) catches a
    worktree that does not descend from the pin; `VERSION` changes only in tagging commits, so a version check could
    not (K2 #26b).

### 1.3 Machine layout

```
/Users/seanfunk/audio/
  .deps/                                    Scripts/deps.sh; chmod -R a-w after population
    JUCE-8.0.4/                             181 MB [M B §7.6]; HEAD 51d11a2be6d5c97ccf12b4e5e827006e19f0555a
    bgfx.cmake-v1.153.9385-561/             642 MB [M B §7.6]; HEAD 99752df38e40179cf998bb880fe4c16c0b3d60ca
                                            submodules: bgfx c7684e20da1e385edc439ef39cdb42b8c661016f,
                                            bx 0b001f5f36579e8aea07efa5af139ca18dad9505, bimg 3b4baab0128ac499c5c3bc37202781bf54084049
    tools/shaderc-v1.153.9385-561/shaderc   + shaderc.stamp ("bgfx.cmake 99752df3...")
    tools/pluginval-<tag>/pluginval         built from a pinned upstream tag by deps.sh (§4.8); tag + SHA in DEPS.lock
    build/                                  scratch builds of shaderc and pluginval (deletable)
    DEPS.lock                               name <TAB> tag <TAB> sha <TAB> populated-at
  libraries/FunkGui/                        main checkout: lead only
  libraries/FunkGui.wt/s<N>-<task>/         FunkGui worktrees, one per FunkGui task (§4.4)
  libraries/FunkGui.wt/pin-<sha7>/          lead-made detached worktrees of a pinned SHA (§4.5)
  plugins/FCompressor/                      main checkout: lead only (build, build-lead, build-lead-x86, build-universal)
  plugins/FCompressor/.claude/worktrees/*   harness-made FCompressor worktrees (§4.3), and ro-<task> read-only ones (§4.4)
  plugins/HardwareReverb/                   read-only: never built, never referenced by CMake
```

The SHAs are the ones HR's own `build/_deps` checkouts report today (`git rev-parse HEAD`, read-only, 2026-09-22).
`deps.sh` verifies them after cloning from upstream.

---

## 2. CMake

### 2.1 Top-level order

```cmake
cmake_minimum_required(VERSION 3.30)      # CMP0168: FetchContent populates directly, with no sub-build (4.3.3 installed)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS")        # HR CMakeLists.txt:8
option(FCOMPRESSOR_UNIVERSAL "arm64+x86_64" OFF)
if(FCOMPRESSOR_UNIVERSAL) set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64") endif() # normal var, before project() (HR :30-35)
project(FCompressor VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20) ; set(CMAKE_CXX_STANDARD_REQUIRED ON) ; set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)                    # Release default (HR :48-52)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# (ADR-91: the macOS-only check became include(cmake/FcmpPlatform.cmake): macOS or Linux, Clang only, the formats per
#  platform, and JUCE 8.0.4's upstream-Clang workaround. Linux defaults CMAKE_CXX_COMPILER to clang++ before project().)
# options (§2.2) ...
include(cmake/FcmpArch.cmake)     # flags need FCMP_ARCHS; must precede targets
include(cmake/FcmpDeps.cmake)     # JUCE -> bgfx (GPU) -> FunkGui, with assertions
include(cmake/FcmpSources.cmake)  # the glob → target map below; defines FCMP_*_SOURCES
add_library(fcdsp STATIC ${FCMP_DSP_SOURCES})
target_link_libraries(fcdsp PUBLIC fcmp_flags PRIVATE fcmp_warnings fcmp_lto)
target_include_directories(fcdsp PUBLIC Source)
if(NOT FCOMPRESSOR_DSP_ONLY) include(cmake/FcmpPlugin.cmake) endif()
enable_testing()
include(cmake/FcmpProbes.cmake)
```

**The glob → target map** (`cmake/FcmpSources.cmake`, K3 #2). Every glob is `file(GLOB_RECURSE … CONFIGURE_DEPENDS)`
rooted at `${PROJECT_SOURCE_DIR}`; Ninja re-checks `CONFIGURE_DEPENDS` globs on every build, so a new file needs no
manual reconfigure and **no CMake edit**. The file is written once by B0 in Sprint 0 and never edited again.

| Glob | Variable → target |
|---|---|
| `Source/fcdsp/**/*.cpp` | `FCMP_DSP_SOURCES` → `fcdsp` (includes every `modes/<key>/*.cpp`; an unregistered Mode's entry is unreferenced and dropped by the linker) |
| `Source/plugin/*.cpp`, `Source/plugin/factory/*.cpp`, minus `CreateEditor*.cpp` | `FCMP_PLUGIN_SOURCES` → the plugin's shared code and `fcmp_probe_plugin` |
| `Source/plugin/CreateEditorGpu.cpp` or `CreateEditorGeneric.cpp` | chosen per configuration (GPU plugin: Gpu; headless plugin and `fcmp_probe_plugin`: Generic) |
| `Source/editor/**/*.cpp`, minus `gpu/` | `FCMP_EDITOR_SOURCES` → the plugin (every JUCE configuration) and `fcmp_probe_plugin` |
| `Source/editor/gpu/*.{cpp,mm}` | `FCMP_EDITOR_GPU_SOURCES` → the GPU plugin only |
| `Tools/probes/common/*.cpp` + `Tools/probes/dsp/*.cpp` | `fcmp_probe_dsp` |
| `Tools/probes/common/*.cpp` + `Tools/probes/plugin/*.cpp` | `fcmp_probe_plugin` |
| `Source/plugin/factory/*.inc` | ordered by `Modes.def` slot into `${binary}/generated/fcmp/FactoryIncludes.h`, with `FCMP_FACTORY_BANK_REVISION` = the first 32 bits of the SHA-256 of their concatenated contents (01 §9.2) |

`Modes.def` drives only the registry (01 §8.2) and the CTest matrix (§2.9). There is no `target_sources` loop and no
generated `ModeIncludes.h` (01 §8.3).

### 2.2 Options

| Option | Default | Meaning |
|---|---|---|
| `FCOMPRESSOR_UNIVERSAL` | OFF | arm64+x86_64. Set before `project()`. Not sticky (HR `:24-35`). |
| `FCOMPRESSOR_HEADLESS` | OFF | Successor to HR's `CLASSIC_GUI` (B §7.2) and C's `FCOMP_GPU=OFF` (C §6.2). No bgfx fetch, no shaders, no `FunkGui::gpu`. The plugin's editor is `juce::GenericAudioProcessorEditor` (`CreateEditorGeneric.cpp`). Every probe, UI probes included, still builds. |
| `FCOMPRESSOR_DSP_ONLY` | OFF | No JUCE at all: `fcdsp`, `fcmp_probe_dsp`, `fcmp_bench`, the `dsp.*` tests, and FunkGui for `FunkGui::harness` only. Implies HEADLESS. |
| `FCOMPRESSOR_INSTALL_AFTER_BUILD` | ON | `COPY_PLUGIN_AFTER_BUILD` (HR `:103-109`). OFF in every preset except `owner`. |
| `FCOMPRESSOR_LTO` | ON | `-flto` in **Release only**. It applies to the plugin (`juce::juce_recommended_lto_flags`) and to `fcdsp`'s objects (`fcmp_lto`). |
| `FCOMPRESSOR_WERROR` | ON | `-Werror`, target-wide on `fcdsp`. On the probe and plugin sources it is applied per file with `set_source_files_properties`, so the JUCE module TUs compiled into the same targets never get it. |
| `FCOMPRESSOR_DEPS_DIR` | `$ENV{HOME}/audio/.deps` | Where `deps.sh` put the checkouts (§2.4). |
| `FCOMPRESSOR_FUNKGUI_REPO` | `/Users/seanfunk/audio/libraries/FunkGui` | The FunkGui `GIT_REPOSITORY`. |
| `FCMP_TEST_JOBS` | 4 | `-j` for the `verify` convenience target. |
| `FCOMPRESSOR_RELEASE` | OFF | ON only in the `universal` and release builds: `dsp.registry` then fails on any `provisional` Mode (01 §4.3). |
| `FCOMPRESSOR_RTSAN` | OFF | `-fsanitize=realtime` (the `rtsan` preset, §2.10). A configure-time `check_cxx_source_compiles` probes support; if the toolchain lacks it, the option is refused with a message and the interposer fallback is used. |

Definitions on the plugin target: `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0` (HR `:296-299`).
In the GPU configuration: `FCOMPRESSOR_GPU_EDITOR=1`. `AU_SANDBOX_SAFE` stays FALSE (JUCE default; 01 §9.2).

### 2.3 Dependencies, pins and assertions (`cmake/FcmpDeps.cmake`)

```cmake
include(FetchContent)
set(FCMP_JUCE_TAG  8.0.4)            ; set(FCMP_JUCE_SHA  51d11a2be6d5c97ccf12b4e5e827006e19f0555a)
set(FCMP_BGFX_TAG  v1.153.9385-561)  ; set(FCMP_BGFX_SHA  99752df38e40179cf998bb880fe4c16c0b3d60ca)
set(FCMP_BGFX_API  153)
set(FCMP_BGFX_SUB_SHAS bgfx=c7684e20da1e385edc439ef39cdb42b8c661016f bx=0b001f5f36579e8aea07efa5af139ca18dad9505
                       bimg=3b4baab0128ac499c5c3bc37202781bf54084049)
set(FCMP_FUNKGUI_TAG v0.0.1) ; set(FCMP_FUNKGUI_SHA <set by the lead when tagging>) ; set(FCMP_FUNKGUI_VERSION 0.0.1)
# FZ0 errata (R-B0 #10): FCMP_FUNKGUI_SHA is the tagged COMMIT, `git rev-parse v0.1.0^{commit}`. FunkGui's tags are
# annotated, so a bare `git rev-parse v0.1.0` prints the tag object; every check peels the pin with <sha>^{commit}, so
# either value works, and fcmp-deps.txt records the commit.
# The first pin is v0.0.1, the lead's G0 snapshot with a harness-only CMake and empty placeholder core/gpu/presets
# targets (§4.9), so FCompressor configures from day one; B0's probe main uses only ScopedFtz until v0.1.0 (Harness v2)
# is pinned at the end of S0 (K3 #5).

# 1. Default to the machine cache, as a NORMAL variable (never cached), and only if the user gave no override.
macro(fcmp_default_source_dir NAME SUBDIR)
  if(NOT FETCHCONTENT_SOURCE_DIR_${NAME} AND EXISTS "${FCOMPRESSOR_DEPS_DIR}/${SUBDIR}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_${NAME} "${FCOMPRESSOR_DEPS_DIR}/${SUBDIR}")
  endif()
endmacro()
fcmp_default_source_dir(JUCE JUCE-${FCMP_JUCE_TAG})
fcmp_default_source_dir(BGFX bgfx.cmake-${FCMP_BGFX_TAG})
# FunkGui is never defaulted: without an override it clones the pinned tag from the local repo (< 1 s).

# 2. Declarations. These are identical to HR's for JUCE and bgfx (HR CMakeLists.txt:112-117, 229-241).
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${FCMP_JUCE_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(bgfx GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git GIT_TAG ${FCMP_BGFX_TAG} GIT_SHALLOW TRUE
                     SYSTEM EXCLUDE_FROM_ALL)   # FZ0 errata (R-B0 #13): ours is declared first and wins over FunkGui's;
                                                # without it every GPU `all` build compiled bimg_encode/_decode too
FetchContent_Declare(FunkGui GIT_REPOSITORY ${FCOMPRESSOR_FUNKGUI_REPO} GIT_TAG ${FCMP_FUNKGUI_TAG} GIT_SHALLOW FALSE)

# 3. Make available in dependency order, then assert.
if(NOT FCOMPRESSOR_DSP_ONLY)
  FetchContent_MakeAvailable(JUCE)
  fcmp_assert_juce("${juce_SOURCE_DIR}")          # parses juce_core/system/juce_StandardHeader.h:42-44 -> "8.0.4"
  fcmp_assert_git("${juce_SOURCE_DIR}" ${FCMP_JUCE_SHA} JUCE)
endif()
if(NOT FCOMPRESSOR_HEADLESS AND NOT FCOMPRESSOR_DSP_ONLY)
  fcmp_select_shaderc()                            # §2.5: sets BGFX_BUILD_TOOLS and FUNKGUI_SHADERC
  set(BGFX_BUILD_EXAMPLES OFF) ; set(BGFX_INSTALL OFF)              # normal variables (CMP0077), never CACHE FORCE
  FetchContent_MakeAvailable(bgfx)
  foreach(t bgfx bx bimg)                          # hidden symbols; release.sh checks `nm -gU` exports only entry points
    set_target_properties(${t} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  endforeach()
  fcmp_assert_bgfx("${bgfx_SOURCE_DIR}")          # "#define BGFX_API_VERSION UINT32_C(153)" in bgfx/include/bgfx/defines.h:18
  fcmp_assert_git("${bgfx_SOURCE_DIR}" ${FCMP_BGFX_SHA} bgfx.cmake)   # plus bgfx/, bx/, bimg/ against FCMP_BGFX_SUB_SHAS
endif()
set(FUNKGUI_BUILD_TOOLS ON)                        # funkgui_framerender for the DoD and verify-gui-live (K2 #26d)
FetchContent_MakeAvailable(FunkGui)
fcmp_assert_funkgui()   # no override: SHA == FCMP_FUNKGUI_SHA (a moved tag → FATAL: delete _deps/funkgui-*).
                        # Override: `git -C <dir> merge-base --is-ancestor ${FCMP_FUNKGUI_SHA} HEAD` must succeed, and
                        # message(WARNING "FunkGui OVERRIDE <dir> @ <git describe --tags --always --dirty> (pin <tag>)")
file(WRITE ${CMAKE_BINARY_DIR}/fcmp-deps.txt ...)   # provenance: name, dir, sha, override yes/no. Read by verify.sh and release.sh.
# After the targets exist (FcmpPlugin.cmake): FATAL if INTERFACE_LINK_LIBRARIES of FCompressor contains FunkGui*
# (FunkGui must be linked PRIVATE, K2 #26e).
```

Assertion rules:
- **`fcmp_assert_git(DIR SHA NAME)`** runs `git -C DIR rev-parse --show-toplevel` and `rev-parse HEAD`. It accepts the
  directory only if it is its own repository root. That rule stops `build/_deps/x-src` from resolving to the enclosing
  FCompressor worktree. A SHA mismatch is `FATAL_ERROR`. A directory that is not a git checkout gets a `WARNING`, and
  the version checks still apply.
  - **FZ0 errata (R-B0 #10):** HEAD is compared with `rev-parse -q --verify <SHA>^{commit}`, not with `<SHA>` itself,
    so a pin that names an annotated tag object is peeled to its commit (a commit SHA peels to itself). A pin that is
    not in DIR is `FATAL_ERROR` too. The override ancestry check uses `<SHA>^{commit}` the same way.
  - `.deps` checkouts are read-only, so only commands that do not write are used. Never `describe --dirty`, which
    refreshes the index.
- **Why assertions are needed.** `FETCHCONTENT_SOURCE_DIR_*` bypasses `GIT_TAG` completely (B §7.2). Without these
  checks, a stale cache or a mistyped override would build silently against the wrong JUCE or bgfx.
- **Never `FETCHCONTENT_FULLY_DISCONNECTED`.** B §7.6 shows it fails when an override is missing. It would also stop
  FunkGui's local clone. No network is needed anyway: JUCE and bgfx come from `.deps`, and FunkGui is a local path.
- **Sticky overrides.** `-DFETCHCONTENT_SOURCE_DIR_FUNKGUI` is cached. `verify.sh --integration` (the lead's
  sprint-end run) fails when `fcmp-deps.txt` shows any override (K2 #26c).
- **`GIT_TAG` stays the tag** (user decision), not a SHA as K2 #26a proposed: the SHA assert already refuses a moved
  tag, and the fix is one `rm -rf` of the populated directory (`DECISIONS.md`).

### 2.4 Why `~/audio/.deps` with overrides, and not a shared `FETCHCONTENT_BASE_DIR`

| Option | What goes wrong | Verdict |
|---|---|---|
| Per build directory FetchContent (the default) | Every worktree × preset clones 823 MB (JUCE 181 MB + bgfx.cmake with submodules 642 MB) [M B §7.6] over the network. HR's `build/` is 4.3 GB [M]. | no |
| Machine-wide `FETCHCONTENT_BASE_DIR` | The base directory holds `<dep>-src` **and** `<dep>-build`/`-subbuild`. Every build tree would share one `bgfx-build` and one `juce-build`, where juceaide is built, so concurrent agents' ninjas would write the same object files. Universal, single-arch, Release and RelWithDebInfo trees would also overwrite each other. | no |
| Point at HR's `build/_deps` (B §7.6 did this in a scratch test) | Forbidden: HR's owner may wipe it, and the user ruled it out. | no |
| **Pre-populated `~/audio/.deps` + `FETCHCONTENT_SOURCE_DIR_*`** | Sources only, read-only, shared. Every build tree keeps its own binary directories. B measured this exact arrangement [M B §7.6]: configure took 15.9 s including juceaide, all targets resolved, and **nothing was written under the `-src` directories** (checked with `find -newermt`). HR's `build-dsp` and `build-universal` are configured the same way today [M CMakeCache]. | **yes** |

**`Scripts/deps.sh`** is idempotent and is the only writer of `.deps`:
1. `git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git ~/audio/.deps/JUCE-8.0.4`
2. `git clone --depth 1 --branch v1.153.9385-561 --recurse-submodules --shallow-submodules
   https://github.com/bkaradzic/bgfx.cmake.git ~/audio/.deps/bgfx.cmake-v1.153.9385-561`
3. Verify the five SHAs of §1.3. On a mismatch, delete the checkout and exit 1.
4. `--seed-from <dir>` copies an existing checkout instead of cloning, for offline use. The SHAs are still verified. A
   one-time copy out of HR's `build/_deps` is a read, not a dependency.
5. Build shaderc (§2.5), write `DEPS.lock`, then `chmod -R a-w` the two source trees. Nothing in a build may write
   into them.

Cost: one network clone per machine, instead of one per build directory. Configure then takes 15.9 s in every
configuration that has JUCE [M B §7.6], and about 2 s in the DSP-only one [D: juceaide is most of the 15.9 s].

### 2.5 Prebuilt shaderc

Cold build of HR `build/` (bgfx, RelWithDebInfo, arm64, 814 steps) [M, `.ninja_log`]: **295 s wall, 2,112 CPU-s**.

| Target | CPU-s |
|---|---|
| tint | 854 |
| spirv-opt | 823 |
| glslang | 215 |
| spirv-cross | 106 |
| glsl-optimizer | 34 |
| shaderc | 27 |
| fcpp | 1 |
| **shaderc toolchain total** | **2,060** |
| bgfx runtime library | 40.5 |

HR `build-universal`: the shaderc toolchain took 3,248 CPU-s, because tint is compiled for both slices, and occupied
the first **414 s** of a **525 s** cold build [M].

Decision:
- `deps.sh` builds `shaderc` once: Release, host arch, the bgfx options from HR `:229-235`, output to
  `~/audio/.deps/tools/shaderc-v1.153.9385-561/shaderc`, plus a stamp file holding the bgfx.cmake SHA.
- `fcmp_select_shaderc()` checks that the stamp names the pinned SHA.
  - If it does: `BGFX_BUILD_TOOLS=OFF` and `FUNKGUI_SHADERC=<path>` (normal variables). No `shaderc` target then
    exists, so FunkGui's shaderc check is guarded on `FUNKGUI_SHADERC` (02 §1.4; Draft 2's unconditional check was a
    configure-breaking bug, K1 #3, K2 #17).
  - If it does not: it falls back to HR's settings, `BGFX_BUILD_TOOLS=ON`, `_SHADER=ON`, texture/geometry/bin2c OFF.
- This is a **build tool** built from the pinned upstream sources, not a vendored library; the user is asked to confirm
  it fits the FetchContent rule (`DECISIONS.md` Q8). The build-from-source fallback stays automatic.
- The shader include directory is `${bgfx_SOURCE_DIR}/bgfx/src` (HR `:260`), which `.deps` provides read-only.
- FunkGui's test `fg.shader.hash` fingerprints the generated `vs_ui/fs_ui.mtl.h`. A shaderc built from the wrong bgfx
  therefore fails a test, not the renderer.
- Savings [D]: ≈ 2,060 CPU-s (≈ 270 s wall) per cold single-arch GPU build directory, ≈ 3,250 CPU-s (≈ 410 s wall)
  per universal build. The one-time cost is ≈ 3–5 min per machine (B §7.6 measured 183 s for an unoptimised
  shaderc + bgfx).

### 2.6 Architectures and flags (`cmake/FcmpArch.cmake`)

These are HR `:62-91` and `:366-387`, renamed. They add the FP-contraction rule and fix one latent issue:

```cmake
# FCMP_ARCHS from CMAKE_OSX_ARCHITECTURES or the host. Only arm64 and x86_64 are allowed. FCMP_ARCH_COUNT.
# FCMP_RUN_ARCH: single arch -> that arch; universal -> the host (selects the golden overlay, §3.3).
set(FCMP_ARCH_FLAGS_arm64  -mcpu=apple-m1)
set(FCMP_ARCH_FLAGS_x86_64 -mavx2 -mfma)
foreach(a IN LISTS FCMP_ARCHS)
  foreach(f IN LISTS FCMP_ARCH_FLAGS_${a})
    if(FCMP_ARCH_COUNT EQUAL 1) list(APPEND FCMP_ISA_FLAGS ${f})
    else()                      list(APPEND FCMP_ISA_FLAGS "SHELL:-Xarch_${a} ${f}") endif()
  endforeach()
endforeach()
add_library(fcmp_flags INTERFACE)            # linked by fcdsp (PUBLIC), the plugin and every probe
target_compile_options(fcmp_flags INTERFACE ${FCMP_ISA_FLAGS}
    -O3 -fno-math-errno -fno-trapping-math -ffp-contract=off)   # never -ffast-math (HR :361-365)
add_library(fcmp_lto INTERFACE)              # compile+link -flto; fcdsp only (PRIVATE)
if(FCOMPRESSOR_LTO)
  target_compile_options(fcmp_lto INTERFACE $<$<CONFIG:Release>:-flto>)
  target_link_options(fcmp_lto INTERFACE $<$<CONFIG:Release>:-flto>)
endif()
# FZ0 errata (R-B0 #6): ONE warning list for every TU of ours (fcdsp, probes, bench, plugin and editor sources, per
# file where JUCE module TUs share the target) and for check-headers.sh: JUCE 8.0.4's clang list
# (juce_recommended_warning_flags, JUCEHelperTargets.cmake:51-86) + -Wextra (first, so JUCE's -Wno-ignored-qualifiers
# wins), without -Wfloat-equal (-Wno-float-equal last; exact float compares are intended in fcdsp, the probes and
# FunkGui's Harness.h). Before, the plugin's sources alone faced JUCE's list, so an fcdsp header could build in fcdsp
# and the probes and then break the plugin.
set(FCMP_WARNING_FLAGS -Wextra <JUCE's clang list> -Wno-float-equal [-Werror])
add_library(fcmp_warnings INTERFACE)         # fcdsp only; the per-file form is fcmp_warn_sources()
target_compile_options(fcmp_warnings INTERFACE ${FCMP_WARNING_FLAGS}
    -Wglobal-constructors -Wexit-time-destructors)   # FZ0 errata (R-B0 #3): no static constructors in fcdsp (C D12)
# FCMP_CAN_RUN_PROBES: try_run of a 1-line program built for FCMP_RUN_ARCH. OFF -> probe tests are DISABLED (§3.3).
# FZ0 errata (R-B0 #9): only ON is cached; an OFF result is checked again on every configure (about 1 s), so installing
# Rosetta and reconfiguring enables the probes.
```

Rules:
- **Why `SHELL:` pairs.** CMake de-duplicates repeated options. HR's universal compile lines therefore read
  `-Xarch_arm64 -mcpu=apple-m1 -Xarch_x86_64 -mavx2 -mfma` [M `build-universal/build.ninja`]: the second
  `-Xarch_x86_64` was dropped, and `-mfma` became unscoped.
  - clang 27 happens to discard it for the arm64 slice (checked with `-###`).
  - A plain arm64 compile with `-mfma` is a hard error (`unsupported option '-mfma' for target 'arm64-apple-darwin27'`) [M].
  - `SHELL:` keeps each pair intact.
- **Why `-ffp-contract=off` is plain, not `-Xarch_`.**
  - Both slices need it. Apple clang defaults to `-ffp-contract=on`, which fuses `a*b+c` inside an expression on
    arm64, and on x86 with `-mfma`.
  - HR's only compiler-side cross-arch divergence was a contraction (`sum += f*f`, HR `:535-542`, C §2.2).
  - Intended FMAs go through `fcdsp::simd::fma` (E §3.2) and are unaffected.
  - The flag reaches FunkGui's sources and the editor too, because they compile inside FCompressor targets. That keeps
    UI geometry arch-neutral.
- **Identical results come from the FP rules, not from identical machine code.** LTO inlines `fcdsp` differently into
  the plugin and into a probe. Without contraction, reassociation or libm on the audio path (E §0.9; 01 §2.2 rule 4,
  enforced by the `lint.deps` grep, K2 #14), every optimisation level and LTO setting produces the same bits. A hash
  that differs between an agent's RelWithDebInfo build and the lead's Release+LTO build is a **determinism bug**, never
  a golden update. The F1 spike (S1) proves this on the first hashes; if it fails, agents build Release with
  `FCOMPRESSOR_LTO=OFF` instead (`DECISIONS.md`).
- **LTO scope.**
  - `fcdsp` compiles with `-flto` in Release, so its archive holds bitcode.
  - The plugin uses `juce::juce_recommended_lto_flags` (`-flto` and `-Wl,-weak_reference_mismatches,weak`, Release
    only, `JUCEHelperTargets.cmake:140-147`).
  - Probes add `-flto` as a **link-only** option in Release. The probe's LTO link then optimises only `fcdsp`, never
    JUCE. HR's DspFlags gave every harness a JUCE LTO link, and StateProbe's took 37.9 s [M C §4].

### 2.7 Targets

| Target | Kind | Sources / links | In `all` |
|---|---|---|---|
| `fcdsp` | STATIC | `FCMP_DSP_SOURCES` (`Source/fcdsp/**`) → `fcmp_flags` (PUBLIC), `fcmp_warnings`, `fcmp_lto` | yes |
| `FCompressor` (+ `_AU`, `_VST3`, `_Standalone`) | `juce_add_plugin` | `FCMP_PLUGIN_SOURCES` + `FCMP_EDITOR_SOURCES` + (GPU: `FCMP_EDITOR_GPU_SOURCES` + `CreateEditorGpu.cpp`; headless: `CreateEditorGeneric.cpp`) → `fcdsp`, `juce::juce_audio_utils`, `juce::juce_recommended_{config,warning}_flags`, LTO, **PRIVATE** `FunkGui::core` + `FunkGui::presets` in every JUCE configuration, `FunkGui::gpu` in GPU only (K1 #17) | yes (not in DSP-only) |
| `fcmp_probe_dsp` | executable | `Tools/probes/common/*` + `Tools/probes/dsp/*` → `fcdsp`, `FunkGui::harness` | no |
| `fcmp_probe_plugin` | `juce_add_console_app` (sets `JUCE_STANDALONE_APPLICATION=1`, `JUCEUtils.cmake:2139`) | `FCMP_PLUGIN_SOURCES`, `FCMP_EDITOR_SOURCES` (never `gpu/`), `CreateEditorGeneric.cpp`, `Tools/probes/common/*` + `Tools/probes/plugin/*` → `fcdsp`, `FunkGui::core`, `FunkGui::presets`, `FunkGui::harness`, `juce::juce_audio_processors`, `juce::juce_dsp` (only for `proc.osref`). SQLite comes in transitively through `FunkGui::presets` | no |
| `fcmp_bench` | executable | `Tools/bench/Bench.cpp` → `fcdsp`, `FunkGui::harness` | no |
| `fcmp_probes` | custom | depends on the two probe executables (only `fcmp_probe_dsp` in DSP-only) | no |
| `verify` | custom | `ctest --test-dir <bin> -L verify -j ${FCMP_TEST_JOBS} --output-on-failure`, `DEPENDS fcmp_probes`. No `&&` chain. | no |
| `verify-gui-live` | custom (GPU only) | `Scripts/gui-live.sh ${CMAKE_BINARY_DIR}`; depends on `FCompressor_Standalone`, `fcmp_probe_plugin`, `funkgui_framerender` | no |

Why two probe executables and not C's three (C §5.14):
- `FcmpProcProbe` and `FcmpUiProbe` are merged into `fcmp_probe_plugin`. The UI probe needs a prepared processor anyway
  (C §5.9).
- JUCE modules are compiled into every `juce_add_*` consumer (C §4).
- The merge compiles JUCE once for all probes. The plugin's shared code is the second and last copy.

### 2.8 Plugin target specifics (`cmake/FcmpPlugin.cmake`)

`juce_add_plugin(FCompressor ...)` fields:

| Field | Value |
|---|---|
| `COMPANY_NAME` | "Funk" |
| `COMPANY_COPYRIGHT` | "Copyright (c) 2026 Sean Funk" |
| `PLUGIN_MANUFACTURER_CODE` | Funk |
| `PLUGIN_CODE` | Fcmp |
| `FORMATS` | AU VST3 Standalone |
| `PRODUCT_NAME` | "FCompressor" |
| `BUNDLE_ID` | com.funk.fcompressor |
| `IS_SYNTH` / `NEEDS_MIDI_INPUT` / `NEEDS_MIDI_OUTPUT` | FALSE |
| `VST3_CATEGORIES` | Fx Dynamics |
| `AU_MAIN_TYPE` | kAudioUnitType_Effect |
| `MICROPHONE_PERMISSION_ENABLED` | TRUE, with its text |
| `HARDENED_RUNTIME_ENABLED` | TRUE |
| `HARDENED_RUNTIME_OPTIONS` | "com.apple.security.device.audio-input" |
| `PLIST_TO_MERGE` | LSMinimumSystemVersion `${CMAKE_OSX_DEPLOYMENT_TARGET}` |
| `COPY_PLUGIN_AFTER_BUILD` | `${FCOMPRESSOR_INSTALL_AFTER_BUILD}` |

The other fields are copied from HR `:183-210` and B §4, §6.4.

Additional wiring:
- The font licence and `Resources/licences/*` are **bundle resources** on each format target, never a POST_BUILD
  copy, because a copy after JUCE's ad-hoc signature broke the seal (HR `:301-332`).
- `add_custom_command(TARGET FCompressor_AU PRE_BUILD COMMAND xattr -cr <bundle> || true)`, and the same for
  `_VST3`. This is GlueCompressor's guard against "detritus not allowed" (B §4). Ninja runs PRE_BUILD as PRE_LINK.
- `funkgui_configure_product(FCompressor PRODUCT FCompressor ENV_PREFIX FCMP_ OBJC_PREFIX Fcmp PREFS_FOLDER FCompressor)`
  covers the mandatory ObjC class prefix (A §6.1, §6.4.1).
- GPU configuration only: `target_link_libraries(FCompressor PRIVATE FunkGui::gpu)`, `funkgui_compile_shaders(FCompressor)`
  and `funkgui_add_font(FCompressor)` (inside `if(NOT FCOMPRESSOR_HEADLESS)`, as 02 §1.9 now also shows).
- `FunkGui::core` and `FunkGui::presets` are linked PRIVATE in every JUCE configuration, because the processor
  implements `ProcessorFacade::port()` with `funkgui::JuceParamPort` and the preset hooks (K1 #17).

### 2.9 Self-registering probe tests over `Modes.def` (`cmake/FcmpProbes.cmake`)

`Modes.def` line grammar (build-facing; 01 §8.1 owns the slot table and its edit protocol):
- `FCMP_MODE(<slot>, "<key>", <Traits>)`, starting at column 0.
- `FCMP_RETIRED(<slot>, "<key>", "<successor-key>")`.
- Anything else must be a comment or a blank line. A malformed `FCMP_` line is a configure error. A commented
  reservation (`// FCMP_MODE(…)`) is skipped by the `^FCMP_` regex.
- The key in quotes makes the X-macro valid C++. C §5.10's bare `fet-76` would tokenise as `fet - 76`.

**One file per probe, and the file registers itself** (K3 #3). A probe is `Tools/probes/dsp/<name>.cpp` (layers `dsp`)
or `Tools/probes/plugin/<name>.cpp` (layers `proc` and `ui`; UI files are named `ui_<name>.cpp`). Its first matching
line declares it:

```cpp
// FCMP_PROBE layer=dsp name=static scope=mode timeout=60
#include "ProbeRegistry.h"
FCMP_PROBE(dsp, static) {                 // (funkgui::test::Probe& P, const fcmp::probe::Ctx& C)
    /* … P.near(...), P.num(...) … */
    return P.finish();
}
```

`Tools/probes/common/ProbeRegistry.h` defines `FCMP_PROBE(layer, name)` as a static intrusive-list node that
`ProbeMain.cpp` walks to dispatch the subcommand `<layer>.<name>`. Static registration is legal in a probe executable;
the no-static-init rule (C D12) applies to `fcdsp` only. Adding a probe therefore edits no CMake list and no dispatch
table.

```cmake
set(FCMP_MODES_DEF ${PROJECT_SOURCE_DIR}/Source/fcdsp/modes/Modes.def)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FCMP_MODES_DEF})   # a new Mode re-runs configure
file(STRINGS ${FCMP_MODES_DEF} _lines REGEX "^FCMP_(MODE|RETIRED)\\(")
foreach(_l IN LISTS _lines)
  if(_l MATCHES "^FCMP_MODE\\( *([0-9]+) *, *\"([a-z0-9-]+)\" *, *([A-Za-z_][A-Za-z0-9_]*) *\\)")
    list(APPEND FCMP_MODE_KEYS ${CMAKE_MATCH_2})
  elseif(NOT _l MATCHES "^FCMP_RETIRED\\( *([0-9]+) *, *\"([a-z0-9-]+)\" *, *\"([a-z0-9-]+)\" *\\)")
    message(FATAL_ERROR "Modes.def: malformed: ${_l}")
  endif()
endforeach()

function(fcmp_probe_test layer exe probe mode timeout)
  if(mode) set(name ${layer}.${probe}.${mode}) ; set(margs --mode ${mode}) ; set(mlabel mode:${mode})
  else()   set(name ${layer}.${probe})         ; set(margs "")            ; set(mlabel global) endif()
  # FZ0 errata (R-B0 #1): the verdict is the exit code, through a /bin/sh wrapper (_fcmp_probe_sh): exit 0 passes, and
  # exit 2 / 3 pass only when this run's results JSON reports golden_drift / golden_missing; any other code, a signal
  # or a timeout fails. B0's PASS_REGULAR_EXPRESSION on the RESULT line made CTest ignore the exit code, so a
  # sanitizer report after a passing RESULT line (TSan exits 66) passed.
  add_test(NAME ${name} COMMAND /bin/sh -c "${_fcmp_probe_sh}" fcmp-probe ${CMAKE_BINARY_DIR}/probe-results/${name}.json
           $<TARGET_FILE:${exe}> ${layer}.${probe} ${margs}
           --golden-root ${PROJECT_SOURCE_DIR}/tests/golden --arch ${FCMP_RUN_ARCH}
           --bless-to ${CMAKE_BINARY_DIR}/golden-candidates --results ${CMAKE_BINARY_DIR}/probe-results)
  set(sb ${CMAKE_BINARY_DIR}/sandbox/${name})
  set_tests_properties(${name} PROPERTIES
      LABELS "verify;${layer};${mlabel};probe:${layer}.${probe}"
      TIMEOUT ${timeout}
      ENVIRONMENT "FCMP_PREFS_DIR=${sb};FCMP_PRESETS_DB=${sb}/presets.db;FCMP_UI_THEME=0")
  if(NOT FCMP_CAN_RUN_PROBES) set_tests_properties(${name} PROPERTIES DISABLED TRUE) endif()
endfunction()

function(fcmp_register_probes exe dir)
  file(GLOB _files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/Tools/probes/${dir}/*.cpp)
  foreach(f IN LISTS _files)
    file(STRINGS ${f} _hdr LIMIT_COUNT 1 REGEX "^// FCMP_PROBE ")
    if(NOT _hdr)
      continue()                                   # helpers (FakeFacade.cpp, …) carry no FCMP_PROBE line
    endif()
    if(NOT _hdr MATCHES "^// FCMP_PROBE layer=(dsp|proc|ui) name=([a-z0-9_]+) scope=(global|mode) timeout=([0-9]+)$")
      message(FATAL_ERROR "${f}: malformed FCMP_PROBE line")
    endif()
    set(layer ${CMAKE_MATCH_1}) ; set(probe ${CMAKE_MATCH_2}) ; set(scope ${CMAKE_MATCH_3}) ; set(t ${CMAKE_MATCH_4})
    if(scope STREQUAL "mode")
      foreach(key IN LISTS FCMP_MODE_KEYS)
        fcmp_probe_test(${layer} ${exe} ${probe} ${key} ${t})
      endforeach()
    else()
      fcmp_probe_test(${layer} ${exe} ${probe} "" ${t})
    endif()
  endforeach()
endfunction()

fcmp_register_probes(fcmp_probe_dsp dsp)
if(NOT FCOMPRESSOR_DSP_ONLY)
  fcmp_register_probes(fcmp_probe_plugin plugin)
endif()
# Lint tests (no probe executable): lint.deps (a cmake -P script grepping #include lines and the libm/FCMP_TEST_TAP
# patterns under Source/, 01 §2.2) and lint.headers (Scripts/check-headers.sh). Both carry the verify label.
# Not in verify: bench.<key> (LABELS "bench") and ui.live (LABELS "live").
```

Changing an `FCMP_PROBE` line (a new timeout, say) needs a reconfigure, which every preset's workflow runs anyway.
A duplicate `<layer>.<name>` is a CTest error at configure time.

**The v1 probe set** (union of Draft 1 §8.3 and Draft 3; K1 #4). Global probes: `dsp.selftest` (B0: the harness
and registration plumbing), `dsp.registry`, `dsp.simd`,
`dsp.units`, `dsp.sc`, `dsp.os`, `dsp.telemetry`, one `dsp.<policy>` unit probe per stage policy that has one,
`proc.layout`, `proc.modeparam`, `proc.fixtures`, `proc.chunk`, `proc.presets`, `proc.prefs`, `proc.osref`,
`ui.font`, `ui.browsers`. Per-Mode probes: `dsp.{static, time, quant, link, switch, zipper, latency, null, hostile,
srsweep, rt, analysis, print}`, `proc.{state, text, bypass, null, latency}`, `ui.{geometry, curve, truth, a11y, input,
textfit}`. `state` is a `proc` probe (it needs the processor); the registry lint is `dsp.registry`. `ui.dump` is a
subcommand for humans and agents (§4.7), not a test.

Rules for the test set:
- **Naming.** `dsp.static.fet-76`, `proc.state.bus-g`, `ui.geometry.clean`, `dsp.registry`, `lint.deps`.
- **Labels.**
  - `verify`, the layer (`dsp` / `proc` / `ui`), `global` or `mode:<key>`, and `probe:<layer>.<name>`.
  - `ctest -L mode:fet-76` is one Mode's inner loop (C §5.10.5).
  - `ctest -L probe:dsp.static` is one probe across all Modes.
- **Golden files** are `<root>/{base,<arch>}/{global,modes/<key>}/<layer>.<name>.txt`: the layer is part of the file
  name, because `dsp.null` and `proc.null` (and `dsp.latency`/`proc.latency`) would otherwise share one file.
- **Probes are `EXCLUDE_FROM_ALL`.** The owner's `cmake --build` stays fast. Agent build presets list `fcmp_probes`
  explicitly (§2.10).
- **The sandbox directory** is created by the probe itself. No probe ever touches the real
  `~/Library/Application Support/FCompressor` (C §6.6). **FZ0 errata (R-B0 #7):** ProbeMain empties a sandbox below a
  `sandbox` path component before the run, and `verify.sh` deletes `<build>/sandbox`, so no run sees the previous run's
  prefs or `presets.db` (`dsp.selftest` leaves a marker file and checks that its sandbox starts empty).
- **FZ0 errata (R-B0 #12): `built-from-probes.txt`.** The `fcmp_probes` target ends by writing
  `<build>/built-from-probes.txt` = `probes <HEAD sha> <clean|dirty> <UTC>` (`cmake/FcmpBuiltFrom.cmake`), and every
  build deletes it before the probe executables compile, so it exists only after a successful build. In JUCE
  configurations `fcmp_plugin_built_from` (in `all`) does the same for the AU/VST3/Standalone bundles
  (`built-from-plugin.txt`). `verify.sh` writes `verify-passed-<sha>` only when the probes' line names that HEAD and a
  clean tree, and `validate.sh` appends its result only when the plugin's line does: a commit, merge or checkout
  without a rebuild can no longer certify stale binaries.
- **FZ0 errata (R-B0 #3, #4): `lint.deps`.** The libm rule uses the regex in 01 §2.2 rule 4 as widened at FZ0
  (`cmake/LintDeps.cmake` `_libm_re`: adds `log10`, `log2`, `exp2`, `expm1`, the inverse and hyperbolic functions,
  the `f` forms, `cbrt`, `hypot`, `erf`, the gamma functions). The static rule scans `static` lines at any
  indentation and treats `static T x(<literal>...)` as a variable.

### 2.10 Presets (`CMakePresets.json`, schema version 6)

| Configure preset | binaryDir | Configuration | Who uses it |
|---|---|---|---|
| `owner` | `${sourceDir}/build` | GPU, Release, LTO, **install ON** | the user's daily tree, and the lead's install smoke test |
| `lead` | `${sourceDir}/build-lead` | GPU, Release, LTO, install OFF | integration verify, blessing, `verify-gui-live` |
| `lead-x86` | `${sourceDir}/build-lead-x86` | headless, Release, `CMAKE_OSX_ARCHITECTURES=x86_64` | the x86 overlay. Needs Rosetta; its tests are DISABLED otherwise. |
| `universal` | `${sourceDir}/build-universal` | GPU, Release, LTO, `FCOMPRESSOR_UNIVERSAL=ON`, `FCOMPRESSOR_RELEASE=ON`, install OFF | `release.sh`, only once `lead-x86` has passed (§5) |
| `release` | `${sourceDir}/build-release` | GPU, Release, LTO, arm64, `FCOMPRESSOR_RELEASE=ON`, install OFF | `release.sh` (the v1 default: arm64-only) |
| `agent` | `${sourceDir}/build-agent` | **headless**, RelWithDebInfo, install OFF | default for FCompressor agents |
| `agent-gui` | `${sourceDir}/build-agent-gui` | GPU, RelWithDebInfo, install OFF | GPU editor work (`EditorHost`, shaders) |
| `dsp` | `${sourceDir}/build-dsp` | DSP-only, RelWithDebInfo | inner loop for `fcdsp` tasks |
| `asan` / `tsan` | `${sourceDir}/build-{asan,tsan}` | DSP-only, RelWithDebInfo + `-fsanitize=address,undefined -fno-sanitize-recover=undefined` (FZ0 errata, R-B0 #5: UBSan halts instead of printing `runtime error:` and exiting 0) / `thread` | lead at milestones (S4, S8, S12; §4.8) |
| `tsan-agent` | `${sourceDir}/build-tsan-agent` | **headless**, RelWithDebInfo + `-fsanitize=thread`; runs `proc.*` and `ui.*` too, including a scripted editor attach/detach and the message-thread `SetupWatcher` | lead at milestones (K2 #18) |
| `rtsan` | `${sourceDir}/build-rtsan` | headless, RelWithDebInfo, `FCOMPRESSOR_RTSAN=ON` (`-fsanitize=realtime`; `[[clang::nonblocking]]` on `EngineHost::process`, the `IEngine` per-chunk calls and `Processor::processBlock`; `-Wfunction-effects` on `fcdsp` only, because JUCE functions carry no effect annotations). If unsupported, `fcmp_probe_plugin` interposes `malloc`, `free`, `pthread_mutex_lock`, `os_unfair_lock_lock`, `write` and `mach_msg` and counts calls on the audio thread | lead at milestones (K2 #18) |

Build presets:
- `<p>` builds `["all","fcmp_probes"]`. `agent-probes` builds `["fcmp_probes"]` only.
- Agents get `"jobs": 4, "nativeToolOptions": ["-l","12"]`.

Test presets:
- `<p>` uses `"filter": {"include": {"label": "verify"}}`, `"execution": {"jobs": 4}` and
  `"output": {"outputOnFailure": true}`.

Workflow presets:
- `agent-verify`, `lead-verify`, `dsp-verify`, `asan-verify`, `tsan-verify`, `tsan-agent-verify` and `rtsan-verify`
  each run configure, build and test. `cmake --workflow --preset agent-verify` is one command.

FunkGui's own `CMakePresets.json` uses the same names for its three presets: `agent` (headless), `agent-gui`, `lead`.

Why agents build RelWithDebInfo:
- JUCE's LTO is Release-only, so RelWithDebInfo skips the 38–60 s LTO links.
- It keeps `-g` for crash triage.
- `fcmp_flags` still forces `-O3` on everything we compile. HR's goldens blessed from RelWithDebInfo pass on Release
  (C §2.2).
- The lead blesses from `lead` (Release+LTO). §2.6 says why the two must agree bit for bit.

### 2.11 Build-time expectations

The inputs are measured. The FCompressor figures are derived and must be replaced by the real ones in Sprint 0–1 (B0
records the build numbers, F1 the `fcdsp` ones; §4.9).

Measured inputs:
- JUCE module compile times inside HR's plugin shared code, RelWithDebInfo, no LTO [M `build/.ninja_log`], in seconds:

  | Module | s |
  |---|---|
  | juce_gui_basics | 51.4 |
  | juce_graphics | 37.9 |
  | juce_core | 28.1 |
  | juce_audio_formats | 20.1 |
  | juce_audio_processors | 16.3 |
  | juce_dsp | 12.2 |
  | juce_audio_basics | 11.9 |
  | juce_gui_extra | 11.4 |
  | juce_audio_utils | 9.3 |
  | juce_audio_devices | 9.0 |
  | juce_events | 6.4 |
  | juce_data_structures | 4.3 |
  | **Total** | **218 CPU-s** |

  The whole HR shared-code target took 268 CPU-s.
- A JUCE `juce_audio_processors` console app (HR BankProbe, Release + LTO) took 188 CPU-s [M `build-dsp`].
- A juce_core-only tool is ready in 15.7 s cold [M C §4].
- HR's universal plugin phase took ≈ 111 s wall after its shaderc phase [M].
- Build directory sizes: `build-dsp` 212 MB, `build-universal` 692 MB, `build` 4.3 GB [M `du`].

| Preset | Configure | Cold build (3 agents, `-j4` each) | Incremental (edit one Mode .cpp) | Disk |
|---|---|---|---|---|
| `dsp` | ≈ 2 s | ≈ 15–25 s [D: ~25 TUs at 1–3 s each at -O3 (HR's FDNReverb.cpp is ~2.0 s/TU, C §4), /4] | 3–6 s | < 50 MB |
| `agent`, target `agent-probes` | 15.9 s [M B §7.6] | ≈ 70–90 s [D: ≈ 250 CPU-s /4; critical path juce_gui_basics ≈ 51 s] | 8–15 s (2 relinks, no LTO) | ≈ 210 MB |
| `agent`, full (DoD) | same | ≈ 150–180 s [D: + plugin shared code ≈ 270 CPU-s + wrappers ≈ 40 CPU-s] | 15–25 s | ≈ 250 MB |
| `agent-gui` | ≈ 17 s | `agent` + ≈ 15–20 s [D: bgfx 40.5 CPU-s + bimg + 2 shaders, with prebuilt shaderc] | as `agent` | ≈ 0.4 GB |
| `lead` (alone, -j10) | ≈ 17 s | ≈ 3–4 min [D: HR universal plugin phase for one slice + probes] | 45–70 s (plugin LTO link) | ≈ 0.5 GB |
| `universal` | ≈ 17 s | ≈ 3–4 min [D: HR's 525 s − 414 s shaderc phase + FCompressor extras] | LTO-bound | ≈ 0.7 GB |

The "edit one Mode .cpp" column holds because no TU instantiates every Mode: `FCDSP_DEFINE_MODE` instantiates each
`ModeEngine<T>` in its own Mode TU, and `Registry.cpp` only collects addresses (01 §8.2; K3 #6).

---

## 3. Verification

### 3.1 Principles

Kept from HR (C §5.0):
- `ScopedFtz` in every probe.
- One flags target for the plugin and the probes.
- Goldens are source.
- Control-instance differencing.
- Same-machine ratio gates.
- The 110 Hz "click" metric.

Changed:
1. **Spec checks and golden checks are different things.**
   - A *spec* check compares a measurement with a value the code declares: the Mode's `ModeDescriptor` and its
     citation (D §2), the parameter model (E §4.2) or an invariant such as a null or bit-exactness. The harness evaluates it in
     the process. It never appears in a golden file, and nothing can bless it.
   - A *golden* check detects drift against a blessed measurement.
   - A new Mode is held to its declared curve, knee, time constants, detents, latency and text fit **before** any
     golden exists.
2. **The suite is table-driven over the Mode registry.** Per-Mode golden files mean that adding a Mode never rewrites
   another Mode's goldens (C §5.10). To keep that true, a Mode owns every switch pair with a lower-slot Mode, and the
   global registry and browser probes have spec rows only (K3 #17).
3. **Tolerances carry the load, hashes prove neutrality.**
   - E's own `log2`/`exp2` via `fma()` and `-ffp-contract=off` should make both arches bit-identical.
   - The `dsp.simd` probe proves that claim with `xarch.` rows.
4. **CTest, one process per probe × scope.** Failures never hide each other, and `-j` works.

### 3.2 Harness v2 (`FunkGui::harness`, `include/funkgui/test/Harness.h`, namespace `funkgui::test`)

#### 3.2.1 API

```cpp
namespace funkgui::test {
struct ScopedFtz { ScopedFtz() noexcept; ~ScopedFtz() noexcept; };   // HR Tools/Harness.h:50-72, unchanged

struct Tol {                                   // golden-row tolerance; stored in the golden file
    enum class Kind : uint8_t { exact, abs, rel, absrel, le, ge };
    Kind kind = Kind::exact; double a = 0, b = 0;
    static constexpr Tol exact() noexcept;
    static constexpr Tol abs(double a) noexcept;               // |got - golden| <= a
    static constexpr Tol rel(double r) noexcept;               // |got - golden| <= r * |golden|
    static constexpr Tol absrel(double a, double r) noexcept;  // |got - golden| <= max(a, r * |golden|)
    static constexpr Tol le(double slack) noexcept;            // got <= golden + slack  (a regression guard; improvements pass)
    static constexpr Tol ge(double slack) noexcept;            // got >= golden - slack
    std::string text() const;  static std::optional<Tol> parse(std::string_view);  // bare number = abs (HR compat)
};

uint64_t fnv1a(const void* p, std::size_t n, uint64_t h = 1469598103934665603ull) noexcept;  // HR's hash
uint64_t hashFloats(std::span<const float>) noexcept;       // little-endian bit patterns, HR IrAnalyse.cpp:85-95

class Probe {
public:
    Probe(std::string_view probe, std::string_view mode /* "" = global */, int argc, char** argv);
    // golden rows: drift detection, blessable
    void num  (std::string_view key, double v, Tol t);        // written with "%.9g"
    void hash (std::string_view key, uint64_t h);             // "%016llx", exact
    void text (std::string_view key, std::string_view v);     // exact; no whitespace
    void lines(std::string_view key, std::span<const std::string> v);  // multi-line text (a11y model, tab order):
                                                              // sidecar <probe>.<key>.lines, line-by-line diff
    // spec rows: evaluated now, printed, never stored; any failure -> exit 1, whatever the golden says
    bool near(std::string_view key, double got, double want, double absTol, double relTol = 0);
    bool le  (std::string_view key, double got, double bound);
    bool ge  (std::string_view key, double got, double bound);
    bool in  (std::string_view key, double got, double lo, double hi);
    bool eq  (std::string_view key, int64_t got, int64_t want);        // counts, flags, bit patterns
    bool quick() const noexcept;                   // --quick: reduced grids, inner loop only (verify.sh forbids it)
    bool wants(std::string_view key) const;        // --only <glob>
    int  finish();                                 // compare, write candidates + results, print RESULT, return exit code
};
}
```

#### 3.2.2 Command line (every subcommand of every probe executable)

`<exe> <layer>.<name> [--mode <key>] --golden-root <dir> --arch arm64|x86_64 [--bless-to <dir>] [--results <dir>]
[--only <glob>] [--quick] [--verbose]`

CMake passes only the root and the arch. The probe resolves its own files:
- base: `<root>/base/<scope>/<layer>.<name>.txt`
- overlay: `<root>/<arch>/<scope>/<layer>.<name>.txt`
- `lines` sidecars: `<root>/base/<scope>/<layer>.<name>.<key>.lines` (e.g. `modes/clean/ui.a11y.panel.lines`)
- `<scope>` is `global` or `modes/<key>`.

Until FunkGui `v0.1.0` (Harness v2) is pinned, B0's probe main links the v0.0.1 harness and uses only its `ScopedFtz`
(§4.9).

This removes the path drift C §1 item 5 found. There is **no `--bless` option**.

#### 3.2.3 Golden file format v2

```
# funkgui-golden 2  probe=static  scope=modes/fet-76
# key<TAB>value<TAB>tolerance
static.r4.t-30.k6.x-20.out_db	-35.0012	abs:0.01
print.default.l.hash	4e1c9b78568fcec4	exact
xarch.simd.exp2.hash	9a0c51e2f3b87d10	exact
time.rel.tc5.t90_after_10s_ms	2410.5	rel:0.1
```

- The file is split on TAB into exactly 3 fields, with `std::getline`. There are no fixed buffers, which removes HR's
  255/127/512 limits (C §1 item 4).
- Keys match `^[a-z0-9][a-z0-9._:+-]{0,119}$`. Values have no whitespace and at most 64 characters.
- A duplicate key in a run, in a file, or in an overlay that is not in base is a **harness error**. That fixes C §1
  item 1, where duplicates shadowed each other silently.
- A bare numeric tolerance reads as `abs:` so HR's files still parse at migration time.

#### 3.2.4 Exit codes and outputs

| Exit | Status | Meaning | Blocking for an agent? |
|---|---|---|---|
| 0 | `pass` | all spec rows pass and all golden rows match | – |
| 1 | `spec_fail` | at least one spec row failed. Golden results are still reported. | **yes** |
| 2 | `golden_drift` | specs pass; at least one golden row differs, or is new or missing | no: reported as a candidate (Draft 1's "missing golden exits 2" is superseded by exit 3 below) |
| 3 | `golden_missing` | specs pass; there is no golden file for this probe × scope × arch | no: reported as a candidate |
| 4 | `harness_error` | duplicate or invalid key, malformed golden, overlay-only key, `xarch.` key in an overlay, unknown flag, unsettled UI ease | **yes** |
| crash / timeout | – | no results file | **yes** |

Every run:
- Prints `RESULT {"probe":"static","mode":"fet-76","arch":"arm64","status":"golden_drift","spec_pass":212,"spec_fail":0,"golden_rows":118,"golden_fail":3,"golden_new":0,"golden_missing_rows":0,"ms":812}`
  (C §5.11).
- Writes that JSON to `--results`.
- With `--bless-to`, writes the full measured row set as `<dir>/<arch>/<scope>/<probe>.txt`. It writes a temp file
  and then renames it (C §1 item 3), and adds `<probe>.diff` when there is drift.

**FZ0 errata (R-B0 #1): how CTest sees these codes.** The CTest test wraps the probe (§2.9): exit 0 passes; exit 2 or 3
passes only when the probe's own results JSON (deleted before the run) reports `golden_drift` or `golden_missing`, so
the workflow presets still succeed with candidates; everything else (1, 4, a sanitizer's code such as TSan's 66, a
signal, a timeout) fails. A probe that prints a passing `RESULT` line and then dies is therefore blocking, and
`verify.sh` reports it as "status pass but the process failed".

#### 3.2.5 Blessing: `golden.py`

`golden.py` is FunkGui's `tools/golden.py`. FCompressor calls it through `Scripts/golden.py`. It takes
`--allow-env <NAME>`: FCompressor uses `FCMP_ALLOW_BLESS`, FunkGui uses `FUNKGUI_ALLOW_BLESS`. **FZ0 errata (R-B0 #2,
R-G1 #1):** `Scripts/golden.py` passes `--allow-env FCMP_ALLOW_BLESS` to `adopt` only (FunkGui v0.1.0's `report` and
`diff` did not accept it and exited 2); `--golden-root` goes to every subcommand.

- **`report <build>`** reads `probe-results/*.json` and prints four groups:
  - BLOCKING: `spec_fail`, `harness_error`, missing results.
  - DRIFT: with each `.diff`.
  - MISSING.
  - IMPROVED: `le:`/`ge:` rows that moved in the good direction.

  It exits 0 when nothing is blocking. `verify.sh` calls it.
- **`adopt <build> [--x86 <build-x86>] --only <glob> --reason "<text>"`** is the lead's only way to change
  `tests/golden/`. It refuses when any of these holds:
  - `FCMP_ALLOW_BLESS=1` is not set;
  - it is run from a **linked worktree** (`git rev-parse --git-dir` ≠ `--git-common-dir`), so it runs only in the
    main checkout;
  - `tests/golden/` has uncommitted changes, so each adopt stays one reviewable diff;
  - the selected candidates include any blocking status;
  - a selected row belongs to a `provisional` Mode (K3 #9; `dsp.registry` writes the provisional keys into its results
    JSON), or is a `ui.geometry` row before the UI freeze FZ5 (K3 #21; the lead passes `--allow-geometry` from S12).
- **Merge rules for `adopt`:**
  - Arm64 candidates alone, which is the only case while Rosetta is missing: rows go to `base/`, overwriting it.
    Arch-neutral output is the design goal (E §0.9).
  - With `--x86`:
    - An exact row equal on both arches, or a numeric row where each arch passes against the arm64 value, goes to
      `base/`.
    - Any other row goes to both `arm64/` and `x86_64/` overlays.
    - An `xarch.` row that differs aborts the adopt, because it is a determinism bug.
  - It prints `git diff --stat tests/golden` and a commit-message stub that contains the reason.
- **The probes cannot bless at all.** This makes "agents never bless" structural rather than a convention (C §6.5).

### 3.3 Golden layout, arch overlay and Rosetta

- The file tree is §1.1. Overlays hold **only** differing rows. The loader merges base and then the overlay for
  `FCMP_RUN_ARCH`. HR kept a complete x86 copy that had to be synchronised by hand (C §2.3).
  - HR's copy has already drifted: `bankprobe.txt`, `presetprobe.txt` and `framerender-browser.txt` exist only in the
    arm64 set [M `ls Tools/golden/x86_64`].
- `xarch.` rows, for example `xarch.simd.exp2.hash`, state that arm64 and x86_64 must be bit-identical. They are never
  allowed in an overlay.
- Ordinary exact rows such as `print.*.hash` may take an overlay row. Each overlay row must name its cause, as HR does
  at `CMakeLists.txt:531-542`, for example a libm call in a colour stage.
- **Rosetta 2 is not installed on this Mac** [M].
  - A one-line x86_64 program fails with `bad CPU type in executable`.
  - `/Library/Apple/usr/libexec/oah/` holds only `RosettaLinux`.

  Consequences:
  - `lead-x86` configures and builds. `FCMP_CAN_RUN_PROBES` is OFF, so its tests are `DISABLED`, and configure prints
    the one user action that enables them: `softwareupdate --install-rosetta --agree-to-license`.
  - The universal build **compiles** the x86_64 slice, and `release.sh` checks it with `lipo -archs`, but nothing
    **executes** it.
  - The x86 overlay stays empty until Rosetta exists. See §6 Q1.

### 3.4 DSP probes (`fcmp_probe_dsp`, JUCE-free)

> **S2 lead revision.** D2 (`dsp.time`) drives the detector with a 1 kHz **square** wave, not a sine: with GR-domain
> peak ballistics a sine makes the measured attack a property of the waveform (F3). D1 (`dsp.static`) runs at the
> fastest attack and slowest release. Latency rows (D7) use LF phase / group delay (01 §5.6 FZ2 note).

The methods are C §5.2–5.8 unless stated otherwise. "Spec" rows cannot be blessed. "Golden" rows can. The tolerances
are §3.7. The **Driver** column says what each probe runs (K3 #10):
- **Rig** = `Tools/probes/common/EngineRig`: constructs a `ModeEntry` into an arena, calls `prepare`, `setParams`,
  `snapParams`, runs `control()` and `colour()` at base rate in 64-sample chunks and taps `ControlIo`. It is the same
  `ModeEngine` object code the plugin runs, so policy and Mode tasks can pass their DoD before `EngineHost` is complete.
- **Host** = `fcdsp::EngineHost` with `setTap` (01 §5.4).

| Test | Scope | Driver | Method | Spec | Golden |
|---|---|---|---|---|---|
| `dsp.registry` | global | – | The registry lint of **01 §8.3** (the canonical list: keys, slots, `kApvtsOrder`, step lists, reasons, DisplayMaps, fixed-point defaults, non-nullable pointers, internals, `provisional`, `crossmode.no_off`, `fb.monotone`, `thr.slope`, golden directories) against `tests/fixtures/modes-ever.tsv` (C §5.10.4) | all rows spec | **none** (spec-only, so adding a Mode rewrites no global golden; K3 #17) |
| `dsp.simd` | global | – | Sweep `log2`/`exp2` over every mantissa for exponents −40..+40; `tanh`, `logCosh`, `tanPi`, `sinPi`, `cosPi` over their domains. Every `fcdsp::simd` op on edge values: ±0, ±inf, NaN, denormals under FTZ. E §3.2, §9.2 row 1. | log2 error ≤ 2.3e-5 dB, exp2 error ≤ 7.4e-4 dB (E §0.9; tightened after the minimax refit); the new functions' targets are fixed by the F1 spike. Scalar == lane 0 bit for bit. The NaN policy of `min`/`max` (E §10.4) is pinned. | `xarch.simd.{log2,exp2,tanh,logcosh,tanpi,ops}.hash` |
| `dsp.units` | global | – | Round trips of the `TimeLaw` conversions, `alphaFromTau` against its closed form, dB↔lin (E §2.3, §9.2 row 2) | round trip ≤ 1e-6 relative | – |
| `dsp.sc` | global | – | SC HPF and tilt magnitude at 161 log frequencies against the design targets (E §8, §9.2 row 8) | HPF fc within ±1 %. Tilt ripple ≤ 0.1 dB. **Off = exact bypass** (bitwise). | curve points, `abs:0.01` |
| `dsp.os` | global | – | FCompressor's oversampler at each Quality: impulse, sine sweep, dry/wet phase identity (E §5.2–5.3) | Measured latency == `kOs[q].latency` (01 §5.6; K1 #29). STD LF group delay == `kStdLatency` within 0.01 samples up to 1 kHz (Thiran; τg at 10 kHz reported). Mix inside the OS domain nulls against the dry path at ≤ −120 dB. | passband ripple and image rejection, `abs:0.1` |
| `dsp.telemetry` | global | – | Seqlock and `HistoryRing` stress with a writer and a reader thread (also run under `tsan`): the writer fills every word of a column and a `UiFrame` with a function of its index or `publishCount`; 10⁷ reads. History columns at bs {1, 17, 64, 128, 512, 4096}. | torn reads = 0 (claim-word protocol, 01 §6.3; K2 #8). History columns are bit-identical across block sizes (E §7). | – |
| `dsp.<policy>` | global | – | One unit probe per stage policy that needs one (e.g. `dsp.quadknee`, `dsp.dualrelease`), owned by the task that adds the policy | per policy | per policy |
| `dsp.static.<key>` | Mode | Rig | D1: 1 kHz single-bin DFT staircase (C §5.2) | Curve error per Rigor. Measured ratio. Threshold. Independent textbook formula to 0.001 dB (`family == textbook`). Tap GR against audio-derived GR. **FB Modes:** the solved r agrees with 200-step bisection to 1e-5 dB for every ballistics branch (K2 #1). | curve at T−10, T, T+10, T+20: `abs:0.01` |
| `dsp.time.<key>` | Mode | Rig (+ Host for the Quality row) | D2: step response, extraction by `TimeLaw` (C §5.3) | τ per Rigor as `max(rel·τ, 1.5/fs_eff)`. Program-dependent Modes stay within `[lo,hi]`. τ measured at ECO, STD and HQ agree within 1.5 base samples (SC delay includes `D_up`; K2 #11). | t50/t90 after 1 s and 10 s of GR: `rel:0.1` |
| `dsp.quant.<key>` | Mode | Rig | D3: sweep 0→1 in 1/4096 steps through `resolve()` (C §5.4). The snap domains are E §4.2.3: log time, slope S for ratio, linear dB; ties go down; no hysteresis. | off_detent = 0, unreached = 0, nonmonotone = 0. Boundaries at the declared midpoints. Continuous parameters pass through within ≤ 1 ulp. D1 run between two detents measures the snapped detent. `look` obeys the budget clamp for each `LookaheadBudget`. | – |
| `dsp.link.<key>` | Mode | Rig | D9 (C §5.8) | Link law error per Rigor. Swapped L/R gives swapped output, bit-exact. Mono equals L of an L=R stereo run, bit-exact. **FB Modes:** a steady-state row with link applied after the per-lane solve (K2 #5b). | – |
| `dsp.switch.<key>` | Mode | Host | D4 for **{key ↔ other} for every other Mode with a lower slot, both directions**, so a new Mode owns all of its pairs and rewrites no other Mode's file (K3 #17). Also A→B→A. Extra pairs owned by `clean`/`fet-76`: an `stmode` flip within Clean (ST → M/S), FET 76 ↔ Clean at mix 0.5, an auto-makeup Mode ↔ a manual one (K2 #3). | `hf_ratio_db` ≤ +3. `level_dev` inside the ±1 dB envelope. `nonfinite` = 0. `latency_constant` = 1. **A→B→A leaves every raw parameter bit-identical and the Mode switch writes only `mode`** (§3.10 #1). Crossfade starts ≥ 50 ms apart under a hovering automation lane. | `settle_ms`: `rel:0.1` |
| `dsp.zipper.<key>` | Mode | Host | D5: step edge, block-rate ramp, block-size invariance (C §5.6). **Detent-edge rows** (K2 #4 iv): for each stepped or hybrid parameter, step between each pair of adjacent detents at t = 1 s — this covers AUTO, ALL, OFF, TC and Stage-2 OFF tag flips | edge `hf_ratio_db` ≤ +3, detent edges included. Ramp error ≤ −40 dB. `bs.mismatches` = 0 over {1, 17, 64, 128, 512, 4096}. | `err_db`: `abs:1` |
| `dsp.latency.<key>` | Mode | Host | D7 at engine level for each Quality × lookahead {off, 5 ms, 20 ms} (C §5.8, E §5.2) | measured = reported, exactly. **Identical for every Mode.** | – |
| `dsp.null.<key>` | Mode | Host | D8 (a), (b), (c), (e) at engine level (C §5.8) | **mix 0 (K1 #2, K2 #12):** ECO bit-exact against `delay(x, L)`; STD and HQ bit-exact against `down(up(delay(x, L_la)))` from a control `Oversampler` at the same Quality, plus passband flatness ≤ 0.01 dB to 20 kHz at 44.1 kHz. **Below threshold** (rigor `clean`, voice OFF): the same references as mix 0, GR exactly 0.0; `modelled`: ≤ −120 dB. mix 0.5 equals the mean of mix 0 and mix 1 within ≤ −120 dB. Brickwall: a 10-minute below-threshold soak keeps GR exactly 0.0. | character Modes: THD at −10 dBFS, `abs:0.5` |
| `dsp.hostile.<key>` | Mode | Host | D10: NaN, inf and 1e30 on the main and key inputs; DC; +40 dBFS; silence after a burst (C §5.8) | `nonfinite_out` = 0 (input sanitised before any delay, 01 §5.8; K2 #13). Recovery ≤ 1 block. Silence gives exactly 0 (Modes without colour). Denormal tail cost < 2× (same-machine ratio). | `tail_err_db`: `abs:1` |
| `dsp.srsweep.<key>` | Mode | Rig | D11: reduced D1 plus D2 at 44.1, 48, 88.2, 96, 176.4 and 192 kHz; Opto 2A also at 22.05 kHz (FB stability guard, K2 #5c) | τ(fs)/τ(48k) and curve against 48k, per Rigor; no instability | – |
| `dsp.rt.<key>` | Mode | Host | D12: allocation counter armed around every `process` call, including Mode switches; interleaved instances; repeated runs | `rt.allocs` = 0. Isolation is bit-exact. Determinism is bit-exact. (Locks and syscalls: the `rtsan` preset, §2.10.) | – |
| `dsp.analysis.<key>` | Mode | Rig | E §6.5 | `analysis::staticGr` is bit-equal to the audio kernel over a 1,024-point grid. FB settled GR is within 0.01 dB of `staticGr` (peak Modes, release ≥ 100 ms). `analysis::stepResponse` is bit-identical to an engine render of the same burst. | – |
| `dsp.print.<key>` | Mode | Host | Fixed program material (seeded PCG32 noise bursts plus a log sine sweep, 4 s, 48 kHz) at the Mode's defaults and at 3 parameter sets | – | `print.*.hash`: an exact refactor-neutrality proof (C §5.12). Overlay allowed, but only with a stated cause. Once the Mode is in `modes-ever.tsv`, a moved default hash also requires `revision++` (01 §0; K2 #10). |

Two notes on method:
- The **tap** is `EngineHost::setTap(TestTap*)`, a runtime pointer whose spans feed `ControlIo`'s nullable outputs
  (01 §5.4; K1 #6). It is null in the plugin. The probes therefore exercise the same `fcdsp` archive that the plugin
  links.
- **Signals** are FCompressor's own seeded PCG32 and closed-form sines, never `std::*_distribution`, whose output is
  not specified across library versions.

### 3.5 Processor probes (`fcmp_probe_plugin`, `proc.*`)

`proc.*` rows that depend on host-map values (`toPlain`/`toNorm` use libm) use `absrel`, never `exact` (K2 #14). State
probes never link FunkPresets' store: the `<PRESET>` hooks stay null until P3 (01 §9.1; K3 #14).

| Test | Scope | Method | Spec | Golden |
|---|---|---|---|---|
| `proc.layout` | global | Buses: main 1→1, 1→2 and 2→2 accepted, 2→1 rejected. Sidechain disabled, mono or stereo (B §7.4.7). Parameter IDs, ranges, versions and **APVTS order == `kApvtsOrder`** against the v1 table; Mode-filtered labels are `""`; `getNumPrograms() == 1` (K2 #9, #25). Scheduled with P1, not S0 (K3 #23). | exact | `layout.params.hash` |
| `proc.modeparam` | global | For every row of `tests/fixtures/modeparam.tsv`, `norm = slot/127` resolves to that key. Unassigned slots resolve to `clean`, retired ones to their successor (01 §8.2; C §5.7.4, E §4.3). | 0 mismatches | – |
| `proc.fixtures` | global | Every `tests/fixtures/state/*.bin` loads into a **non-fresh** instance with the expected key and values. A retired key loads its successor and sets `StateNotice::modeMigrated`; an old `modeRev` sets `modeRevised` (C §5.7.3–5). | values bitwise | render fingerprints: `absrel` |
| `proc.chunk` | global | Declared bs 64 but fed 1024; 0-length blocks; `processBlock` before `prepareToPlay` (HR StateProbe `:517-566`, `:705-724`) | hash equals the engine fed the processor's snapped values; no crash | – |
| `proc.presets` | global | Preset store and file round trip, import and export, against the scratch `FCMP_PRESETS_DB` (C §5.7.7); apply is one batch (the audio thread never sees a mixed set) | 0/1 checks | – |
| `proc.prefs` | global | HR PrefsCheck, sandboxed with `FCMP_PREFS_DIR` | 0/1 checks | – |
| `proc.osref` | global | `fcdsp`'s oversampler against `juce::dsp::Oversampling` (2× IIR max-Q, 4× FIR) as a **reference**, never as a dependency | passband within 0.1 dB and image rejection at least JUCE's. **No latency comparison**: latency is `dsp.os`'s declared-equals-measured row (K1 #29, K2 #28). | – |
| `proc.state.<key>` | Mode | D6.1–2: every parameter at a distinct snapped value, save, restore into a non-fresh instance; absent children get their defaults. The Mode is stored as its key string and `modeRev`. `listen` and `delta` saved as 1 load as 0 (K2 #25c). Restore runs inside one batch. | bitwise | – |
| `proc.text.<key>` | Mode | `getText`↔`getValueForText` for every parameter while this Mode is active; detent labels (HR StateProbe `:447-488`); U+2212 and `-` both parse; n/a prints U+2013 | mismatch = 0 | `text.defaults.hash` |
| `proc.bypass.<key>` | Mode | Host and parameter bypass: both ends; 110 Hz edge; the dry path is latency-aligned (B §1.6, §7.4.5) | `max_err` = 0 at both ends, every Quality. `hf_ratio_db` ≤ +3. | – |
| `proc.null.<key>` | Mode | mix 0 through `processBlock`, which proves the latency-aligned dry path at plugin level | ECO bit-exact against the delayed input; STD/HQ bit-exact against a control `Oversampler` round trip (same rule as `dsp.null`; K1 #2) | – |
| `proc.latency.<key>` | Mode | `getLatencySamples()` after `prepareToPlay` against the measured lag, for each Quality × lookahead. Plus: `quality` set **from a non-message thread while processing**; `SetupWatcher` must report the new latency within 100 ms, with 0 allocations and 0 locks on the audio thread (K2 #6) | exact. Mode-independent. | – |

### 3.6 GUI probes (headless: `fcmp_probe_plugin ui.*`), and live parity

**Mechanism** (C §3.3, FunkGui recorder, 02 §3):
1. The probe prepares the facade — a real processor (48 kHz, bs 128) for `ui.truth`, `FakeFacade` with scripted
   `UiFrame`s and columns elsewhere — selects the Mode, and feeds deterministic material for the live views.
2. It builds `fcmp::ui::Panel(facade, PanelOptions{.skipHint = true, .syncPreview = true})` and calls
   `panel.setView(v, /*instant*/ true)` for each `v` in **`fcmp::ui::views()`** (02 Part 2 intro).
3. It calls `funkgui::HeadlessHost::settle()` (fixed dt 1/60 s, at most 600 frames; not settled is a harness error).
   With `syncPreview`, worker-computed panes are finished inside `tick()`, so nothing depends on thread timing
   (02 §3.7 rule 7; K1 #11).
4. It draws into a `funkgui::Canvas` with the CPU-baked atlas, producing a `PrimList`.
5. It fingerprints the `PrimList` in the process. The fingerprint skips `live` primitives and text gamma (C §5.9 G1,
   F §6.2).

The views come from `views()`, not from a list in the probe, so a new view is covered automatically. They are
`panel` (the main panel with its always-visible band), `chars.sidechain` and `chars.colour` (the full-panel
Characteristics screen with each SC|COLOUR tab), `modebrowser` and `presetbrowser`. The band has no sub-views (02 §0.8).

| Test | Scope | Spec | Golden |
|---|---|---|---|
| `ui.font` | global | Every string any Mode can show (Mode names, slot labels, detent labels, briefs, units, spec lines, internals legends) has every glyph in the atlas: `missing` = 0. This matters as Modes add labels such as µ (F §0.9). Scheduled with U1a, after the descriptor wave (K3 #23). | `font.atlas.hash` (exact). An OS update to CoreGraphics can move it; that is a known drift reason. |
| `ui.browsers` | global | Every registered Mode appears once in the Mode browser, in `Group` then slot order. | **none** (spec-only; K3 #17). Per-Mode browser rows are fingerprinted in `ui.geometry.<key>` instead, so adding a Mode re-blesses nothing global. |
| `ui.geometry.<key>` | Mode | Every view renders without a harness error. **Theme invariance**: hash(theme 0) = hash(theme 1). | per view × dpi {1, 2}: hash exact; `static/text/segment/area` counts `abs:0`; extents `abs:0.01`; the Mode's own browser row. **Adopted only at the UI freeze FZ5** (S12); until then `golden_missing` is expected (K3 #21). |
| `ui.curve.<key>` | Mode | G2: `TRANSFER_CURVE` vertices lie on `axis(x + staticGain(x) − preGainDb)` within ≤ 0.5 px (K1 #10, #22). Chord error ≤ 0.25 px. `THRESHOLD_MARK` within ±0.5 px of `inputThresholdDb`. The ratio label equals the detent label. | – |
| `ui.truth.<key>` | Mode | G3 on a real processor: `OP_DOT` within 1 px of the D1-measured operating point, with the curve built by `resolve()` + `overlaySmoothed` (01 §6.2). GR meter within ±0.5 px. Newest history column within ±0.1 dB of the tap. The CONTROL PATH `internal0` lane's newest point, and READOUTS rows 11–18, equal `UiFrame.internals[i]` through their declared ranges, within ±0.5 px. | – |
| `ui.a11y.<key>` | Mode | Stepped slots are sliders in index space `{0..n−1}` with interval 1 (F §0.11). Locked and N/A items are disabled and have help text. | `ui.a11y.<view>` as `lines` sidecars (`modes/<key>/ui.a11y.<view>.lines`) |
| `ui.input.<key>` | Mode | G5 through the `Panel` API. In a stepped slot an arrow moves exactly one detent, and wheel and drag land only on detents. Locked slots write nothing. Every write is inside a begin/end gesture; multi-parameter writes are inside a batch. | `taborder` as `lines` |
| `ui.textfit.<key>` | Mode | G6: `text_overflow` = 0 for every label, brief and word × dpi; the detent pair-fit rule (02 §8.3) | – |

**Visual inspection** (not a test): `fcmp_probe_plugin ui.dump --view <id> --mode <key> --out x.dump [--png x.png]`
writes the settled frame; `funkgui_framerender x.dump x.png 2` renders any dump (K1 #10).

**Live parity, `ui.live` (label `live`, not `verify`).**
- `Scripts/gui-live.sh <build>` runs under `lockf -t 900 /tmp/fcmp-gui.lock` (`/usr/bin/lockf` exists [M]; `flock`
  does not).
- For views {`panel`, `chars.sidechain`, `modebrowser`} × Mode `clean`, it launches the Standalone with FunkGui's
  `capture-frame.sh`, setting `FCMP_CANVAS_DUMP`, `FCMP_UI_VIEW=<id>`, `FCMP_UI_FIXED_DT=0.0166667`,
  `FCMP_UI_NO_HINT=1`, `FCMP_UI_NO_LIVE=1`, a scratch `FCMP_PRESETS_DB` and `FCMP_PREFS_DIR` (one variable set, 02 §5.1).
- It fingerprints the dump with `funkgui_framerender --fingerprint` and requires **equality with the headless hash** of
  the same state (C §3.3.5).
- It needs a window server, and it may raise the microphone TCC prompt. It is run by the lead at milestones (S8
  gallery parity via G7, S10 first FCompressor parity via U7, S12), or by a GPU-editor agent on request, never in
  parallel.

### 3.7 Tolerances

C §5.13 is adopted as the table in `Tools/probes/common/Tolerances.h`, indexed by `Rigor`. Changing it is a lead-owned
source change, never a bless.

| Quantity | clean | modelled | character |
|---|---|---|---|
| Static curve outside / inside the knee | 0.05 / 0.10 dB | 0.25 / 0.35 dB | 0.5 / 0.75 dB |
| Measured ratio against declared | rel 1 % | rel 3 % | rel 8 % |
| Textbook formula check | 0.001 dB | 0.001 dB | n/a (custom curve) |
| τ against spec | max(2 %, 1.5 smp) | max(10 %, 1.5 smp) | within `[lo,hi]` + golden `rel:0.1` |
| τ(fs)/τ(48k) | 1 ± 0.02 | 1 ± 0.03 | 1 ± 0.05 |
| Curve against 48k | ±0.02 dB | ±0.05 dB | ±0.1 dB |
| Tap GR against audio GR; stereo link | 0.01 dB | 0.05 dB | 0.1 dB |
| mix 0 (K1 #2) | ECO: bit-exact vs `delay(x, L)`; STD/HQ: bit-exact vs a control `Oversampler` round trip, passband ≤ 0.01 dB to 20 kHz | same | same |
| Bypass passthrough | bit-exact vs `delay(x, L)`, every Quality | same | same |
| Below-threshold null (voice OFF) | bit-exact vs the mix-0 reference of the same Quality | ≤ −120 dB | THD golden ±0.5 dB |
| Click `hf_ratio_db` (switch, zipper, bypass) | ≤ +3 dB | ≤ +3 dB | ≤ +3 dB |
| Latency | exact | exact | exact |
| Curve on screen | 0.5 px, chord 0.25 px | same | same |
| Allocations in `process` | 0 | 0 | 0 |
| **New:** Mode switch parameter writes; A→B→A raw values | 0; bitwise | same | same |
| **New:** `xarch.` rows across arches | identical | identical | identical |
| **New:** `analysis::staticGr` against the audio kernel | bitwise | bitwise | bitwise |
| **New:** FB solve against 200-step bisection | 1e-5 dB | 1e-5 dB | 1e-5 dB |
| **New:** τ across ECO/STD/HQ | ≤ 1.5 base samples | same | same |

The clean-Mode curve tolerance of 0.05 dB leaves 67× headroom over E's measured poly `exp2` error of 7.4e-4 dB
(E §0.9).

### 3.8 How a new Mode is covered automatically

1. The descriptor-wave task (or the lead) adds `Source/fcdsp/modes/<key>/{<Traits>.h, <Traits>Desc.cpp,
   <Traits>.cpp}` with generic traits and `provisional = true`, plus the Mode's `FCMP_MODE` line in `Modes.def`
   (01 §8.1, §8.4). The per-directory glob picks up the files. Configure re-runs because of `CMAKE_CONFIGURE_DEPENDS`,
   and every `scope=mode` probe gains a `*.<key>` test: 13 `dsp`, 5 `proc` and 6 `ui`.
2. Every spec row runs at once from the `ModeDescriptor`: curve, knee, τ, detents, latency, link, text fit, a11y rules,
   allocation freedom, `crossmode.no_off`, and switch cleanliness against every lower-slot Mode.
3. Every golden-backed test exits 3 (`golden_missing`). `verify.sh` lists them as candidates, and
   `build-agent/golden-candidates/<arch>/modes/<key>/` holds the files the lead will review and adopt.
4. `dsp.registry` fails if the key or slot collides with, or reuses, anything in `modes-ever.tsv`. The lead appends the
   Mode to that file when the Mode first ships. `golden.py adopt` refuses `modes/<key>/` rows while the Mode is
   `provisional`, so the first goldens are blessed only after the Mode task installs the real traits (K3 #9).
5. The inner loop is `ctest --preset agent -L mode:<key>`, or `ctest --preset dsp -L mode:<key>` for DSP-only work.

### 3.9 Runtime budget

The estimate follows C §5.14: about 3 s of DSP per Mode (static 0.8 s, τ 0.2 s, sample-rate sweep 1 s). Our
additions are cheap:
- `switch` at N = 35 is about 1,190 runs × 2 s of audio × ≈ 20 ns/sample/channel, ≈ 9 s in total.
- UI recording is milliseconds per view. The atlas bake is 11–23 ms per process (A §0).
- `fcmp_probe_plugin` start-up costs about 0.1–0.3 s per test.

| Suite | Tests | CPU | Wall, `-j4` (agent) | Wall, `-j10` (lead, alone) |
|---|---|---|---|---|
| 8 Modes (v1) | ≈ 207 | ≈ 70 CPU-s | ≈ 20–25 s | ≈ 10 s |
| 35 Modes | ≈ 855 | ≈ 250 CPU-s | ≈ 65–75 s | ≈ 30 s |

For comparison, HR's `verify` took 17.4 s sequentially for 263 rows (C §4).

Budgets. `verify.sh` prints the 10 slowest tests; the budgets are reviewed by the lead, and timing is never a gate
(C §6.8):
- one test ≤ 10 CPU-s;
- one Mode ≤ 6 CPU-s in total;
- the full suite ≤ 90 s wall at `-j4` for 35 Modes;
- `TIMEOUT` 60 s per test (120 s for `switch` and `srsweep`).

Outside `verify`:
- `bench.<key>` (label `bench`), run alone by the lead.
- `ui.live`.
- `asan`/`tsan` runs at sprint end: the DSP-only suite, about 2–3× slower.

### 3.10 Where the research reports disagreed, and what the suite encodes

1. **Mode switch: snap raw values (B §7.4.2; F §10.5's variant) or keep raw and snap on read (E §4.2.1).** **E.**
   - A→B→A restoring every raw value bit for bit is a clean, testable invariant.
   - A UI Mode change then writes no host parameter other than `mode`, so hosts record one gesture and no other
     automation.
   - Undo is the host's, one step (there is no in-plugin `UndoManager`, 01 §9.1).
   - `resolve()` stays a pure function, which D3 needs.
   - Encoded as spec rows in `dsp.switch` (`abA.raw_bitexact`, `modeswitch.param_writes = 1`, the `mode` write) and in
     `dsp.registry` (`crossmode.no_off`, which guards snap-on-read's one hazard, K2 #4).
2. **Mode slots: 64 (B §7.4.3) or 128 (E §4.3).** **128.** D already lists 35 Modes and the list grows.
   `proc.modeparam` uses `norm = slot/127`.
3. **Latency: maximum over Modes (B §7.4.5) or a function of Quality and Lookahead-enable only (E §5.2).** **E.**
   `dsp.latency` asserts Mode-independence.
4. **Test tap: compile flag `FCMP_TEST_TAP` (C §5.1) or runtime buffers (E §3.4).** **Runtime.** A compile flag would
   need a second `fcdsp` build, and the probes would stop testing the shipped object code.
5. **Oversampler: a JUCE wrapper inside the engine (E §9.2 row 10) or a JUCE-free `fcdsp` (C §5.14 and this task).**
   **JUCE-free.**
   - `fcdsp` implements the halfband polyphase IIR (2×) and FIR (4×) itself.
   - E's measured JUCE latencies (4 and 61 samples) become targets; the declared values freeze at FZ2 (01 §5.6).
   - `proc.osref` keeps JUCE as a reference for passband and image rejection only.
   - 01 §5.6 confirms it (Draft 3's §6 Q3 is closed).
6. **Separate proc and UI probe executables (C §5.14).** **Merged** (§2.7).
7. **Arm64 base plus x86 overlay under `Tools/golden` (C §5.12).** **A symmetric base plus per-arch overlays under
   `tests/golden/`** (§3.3). The symmetric form fits the arch-neutral design goal and the missing Rosetta.
8. **Dependency cache location: `FCompressor/.deps` (C §6.3), `plugins/.deps` (B §7.6) or `~/audio/.deps`.**
   **`~/audio/.deps`.** One cache serves FCompressor, FunkGui and, later, HR.
9. **`FETCHCONTENT_FULLY_DISCONNECTED=ON` (B §7.6 command lines).** **Dropped** (§2.3).
10. **mix 0 null: bit-exact against the delayed input (C D8a, Draft 3) or against an OS round trip (01 §5.6).** **The
    latter at STD/HQ, the former at ECO** (§3.4, §3.7). With mix inside the OS domain, Draft 3's rows could never pass.
11. **Registration: explicit CMake lists (Draft 3 §2.9, Draft 1 §8.3) or self-registering probe files (K3 #3).**
    **Self-registering** (§2.9).

---

## 4. Agent process

### 4.1 Roles and limits

- **Lead:** the user's main session. The lead is the only party that:
  - commits, merges, blesses and tags;
  - edits `tests/golden/`, `tests/fixtures/`, `cmake/`, `CMakeLists.txt`, `CMakePresets.json`, `Scripts/`,
    `docs/design/`, and the frozen interfaces;
  - touches the main checkouts of both repositories;
  - installs plugins (the `owner` preset).
- **Agents:** at most **3 at once across both repositories** (the user's usage limit). The lead is not one of the 3.
  Each agent has one task, one worktree per repository it edits, and its own build directories.
- **CPU:** 3 agents × `-j4` with ninja `-l 12`, plus `ctest -j4`, on 10 cores. A single JUCE TU is already 30–51 s
  (C §4, §2.11), so oversubscribing only thrashes (C §6.9). `fcmp_bench` never runs while agents are building.

### 4.2 Sprint start (lead)

1. `main` holds the sprint base in **committed** form. Worktrees are created from a commit, so an uncommitted change in
   the main checkout is invisible to agents. The base includes:
   - the frozen interfaces **as of the last freeze point** (the index is `docs/ARCHITECTURE.md` §"Frozen interfaces";
     the freeze points FZ0–FZ5 are §4.9), all compiling standalone (`lint.headers`);
   - `docs/sprints/s<N>.md`;
   - any new `Modes.def` lines the lead adds itself.
2. **`Modes.def` protocol (K3 #8; 01 §8.1).** `Modes.def` is edited only by the lead in the sprint base, or by that
   sprint's single descriptor-wave task, which owns the file for the sprint. A slot is fixed the moment its line first
   appears, so each Mode task's CTest set, `proc.modeparam` row and browser order are known before it starts. Mode DSP
   tasks never touch the file. Draft 3's reservation scheme remains only as the fallback for a lone task that must add
   a brand-new Mode outside a wave: the lead adds a commented `// FCMP_MODE(<slot>, "<key>", <Traits>)` line (one blank
   line between reservations) and that task uncomments only its own line. Slots are permanent host values (E §4.3).
3. **Each task in `docs/sprints/s<N>.md` has a manifest:**

   ```
   TASK s2-f3   repo: FCompressor (isolation: worktree)   base: <sha>
   GOAL   one paragraph
   OWNS   Source/fcdsp/engine/ModeEngine.h  Source/fcdsp/engine/stages/detector/PeakLog.h
          Source/fcdsp/engine/stages/gain/QuadKnee.h  Source/fcdsp/modes/Registry.cpp  Source/fcdsp/modes/clean/Clean.{h,cpp}
          Tools/probes/common/EngineRig.*  Tools/probes/dsp/{registry,static,time}.cpp
   FROZEN Source/fcdsp/core/**  Source/fcdsp/params/*.h  Source/fcdsp/engine/{Stage,IEngine,TestTap}.h
          Source/fcdsp/modes/{ModeDescriptor,ModeKit,DefineMode}.h  (propose changes in the handoff; never edit)
   FUNKGUI pin v0.2.0 | own worktree <task-id> (base <sha>)
   PRESETS dsp (inner loop), agent (DoD)
   DONE   §4.7 for an fcdsp task, plus: <task-specific acceptance, e.g. "dsp.static.clean spec rows all PASS">
   ```

4. The lead spawns ≤ 3 agents: FCompressor tasks with `isolation: 'worktree'`, and FunkGui tasks with the FunkGui
   worktree path and base SHA in the prompt.

### 4.3 FCompressor agent

Agents' shells reset the working directory **and** shell variables between calls.
1. Learn the worktree path once, with `git rev-parse --show-toplevel` run from the starting directory.
2. Write that absolute path literally into every later command. `$WT` below stands for it.
3. Start every command with `cd "$WT" &&`.

```sh
cd "$WT" && git branch -m "s1/gc"               # name the harness-made branch after the task
cd "$WT" && cmake --preset dsp && cmake --build --preset dsp && ctest --preset dsp -L 'mode:clean'   # fcdsp inner loop
cd "$WT" && cmake --preset agent && cmake --build --preset agent-probes && ctest --preset agent -L 'mode:bus-g'
cd "$WT" && cmake --build --preset agent && Scripts/verify.sh "$WT/build-agent"                     # DoD gate
```

Without presets, `agent` is equivalent to:
`cmake -S "$WT" -B "$WT/build-agent" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFCOMPRESSOR_HEADLESS=ON -DFCOMPRESSOR_INSTALL_AFTER_BUILD=OFF`.
JUCE and bgfx come from `~/audio/.deps` automatically, and FunkGui from its pinned tag.

Rules:
- **Build directories are `<worktree>/build-<preset>`.** Worktrees are unique, so the paths are unique by
  construction. Never build in another agent's worktree, in the main checkout, or anywhere in HardwareReverb.
- **No network. No `git commit`, `push`, `rebase`, `worktree prune` or `gc`.** Change no ref other than your own
  branch. Never install. Never run `golden.py adopt`. Never write into `~/audio/.deps`.

### 4.4 FunkGui agent

```sh
FG=/Users/seanfunk/audio/libraries/FunkGui
FWT=/Users/seanfunk/audio/libraries/FunkGui.wt/s1-area                      # s<N>-<task>
git -C "$FG" worktree add -b s1/area "$FWT" <base-sha-from-manifest>
cd "$FWT" && cmake --preset agent-gui && cmake --build --preset agent-gui && ctest --preset agent-gui -L verify -j4
cd "$FWT" && tools/verify.sh "$FWT/build-agent-gui"                         # FunkGui's DoD gate
```

- The rules of §4.3 apply.
- FunkGui's `agent` preset (headless) covers `core`, the harness and the recorder tests. `agent-gui` adds `gpu`, the
  shaders and `fg.shader.hash`.
- **Verifying FCompressor from a FunkGui task** (K3 #22): for every FunkGui task that changes rendering or API, the
  lead creates a detached, read-only FCompressor worktree at the sprint base,
  `git -C /Users/seanfunk/audio/plugins/FCompressor worktree add --detach .claude/worktrees/ro-<task> <base-sha>`. The
  agent configures it with `-DFETCHCONTENT_SOURCE_DIR_FUNKGUI=$FWT`, builds and runs `Scripts/verify.sh` there, and
  never edits it.

### 4.5 Consuming an unreleased FunkGui change

**The pipelining rule (K3 #4).** FCompressor tasks consume only **tagged** FunkGui: a FunkGui feature an FCompressor
task needs in sprint N is scheduled in sprint N−1 or earlier, and the lead tags and pins it at the boundary. The
override below exists for (a) a single agent that owns both a FunkGui and an FCompressor worktree, (b) the lead, and
(c) FunkGui agents verifying FCompressor in an `ro-<task>` worktree (§4.4).

```sh
cd "$WT" && cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI=/Users/seanfunk/audio/libraries/FunkGui.wt/s1-area
# configure prints: FunkGui OVERRIDE /…/FunkGui.wt/s1-area @ v0.1.0-3-gabc1234-dirty (pin v0.1.0)
# and FATALs unless the override descends from the pinned SHA (git merge-base --is-ancestor; K2 #26b)
```

- **Allowed override targets:**
  - (a) your own FunkGui worktree;
  - (b) a detached worktree the lead created for a named SHA:
    `git -C "$FG" worktree add --detach /Users/seanfunk/audio/libraries/FunkGui.wt/pin-<sha7> <sha>`.

  **Never another agent's live worktree.** It changes under you.
- `verify.sh` copies the override line from `fcmp-deps.txt` into its report, so the handoff shows it.
- **The override disappears at integration.** The lead tags FunkGui, bumps the pin and re-verifies with no override
  (§4.8). If a task's FCompressor change cannot pass against the tagged FunkGui, the task is not done.

### 4.6 File ownership

| Path | Owner | Agents |
|---|---|---|
| `OWNS` globs of the task | the task's agent | edit freely |
| Frozen interfaces (listed in the sprint plan) | lead | propose in the handoff; never edit |
| `Source/fcdsp/modes/Modes.def` | lead, or the sprint's descriptor-wave task (§4.2) | Mode DSP tasks never edit it; a lone new-Mode task only uncomments its own reserved line |
| `Source/fcdsp/modes/<key>/**` | the Mode's task (descriptor wave first, then the Mode DSP task) | – |
| `Source/fcdsp/engine/stages/<slot>/<Policy>.h` | the task whose manifest names the policy | – |
| `Tools/probes/{dsp,plugin}/<name>.cpp` | the task whose manifest names that probe file (self-registering, §2.9) | a Mode task adds coverage through its `ModeDescriptor` data, not through shared probe code |
| `Tools/probes/common/**`, `Tolerances.h` | the build/verification task (B0, F3 for `EngineRig`), then the lead | propose changes |
| `Source/plugin/factory/<key>.inc` | the Mode's task, once the bank exists (P3) (K1 #31, K3 #18) | – |
| `docs/modes/<key>.md` | the Mode's task (the one exception to `docs/**`) | – |
| `tests/golden/**`, `tests/fixtures/**` | lead (`golden.py adopt`; fixtures are write-once) | never; candidates go to `build-*/golden-candidates` |
| `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `.gitignore`, `CLAUDE.md`, `docs/**`, `Resources/**` | lead (or a Sprint-0 build task named in its manifest) | never |
| FunkGui `include/funkgui/**` public headers | lead approves; any change bumps MINOR at tagging | the FunkGui task that owns them |
| `/Users/seanfunk/audio/plugins/HardwareReverb/**`, `~/audio/.deps/**` | nobody | read only |

`Scripts/sprint/ownership.py <manifest> <worktree>` compares `git -C <wt> status --porcelain` with the `OWNS` globs,
ignoring `.claude/` (K3 #26). Any path outside them is a DoD failure. The build (§2.1 globs, §2.9 self-registering
probes) and the layout (01 §2.1: one directory per Mode, one header per policy; 02 Part 2: one file pair per sub-view)
are designed so that **no task in the §4.9 plan needs a path another task in the same sprint owns**.

### 4.7 Definition of done

Every task:
1. **Ownership.** `ownership.py` is clean.
2. **Build.**
   - `cmake --build --preset agent` (or `agent-gui`) succeeds.
   - There are no warnings in `fcdsp` or the probe sources, which build with `-Werror`.
   - FunkGui tasks: FunkGui's `agent` and `agent-gui` presets build.
   - Tasks that add or change a frozen header: `Scripts/check-headers.sh` (CTest `lint.headers`) passes — every frozen
     header compiles standalone with `clang++ -std=c++20 -fsyntax-only`, every `constexpr` helper is inline, and the
     size asserts are live (`EngineParams` 116, `UiFrame` 288, `HistoryColumn` 32, `Prim` 84; K3 #20).
     **FZ0 errata (R-B0 #6, #8, #3):** the header check uses the same warning list as every TU of ours (§2.6:
     JUCE's clang list + `-Wextra`, without `-Wfloat-equal`), so a header that passes also compiles in the plugin;
     `-Wmissing-prototypes -Wmissing-variable-declarations` make a non-inline function or variable definition an error
     ("every helper is inline" is now checked, not assumed); `Source/fcdsp` headers also get `-Wglobal-constructors
     -Wexit-time-destructors`. CTest passes CMake's list to the script, which fails if its own copy differs.
3. **Verify.**
   - `Scripts/verify.sh <build>` exits 0. That means **0 spec failures, 0 harness errors, 0 crashes or timeouts, 0
     disabled tests** (a disabled test on arm64 means the configuration is broken).
   - `golden_drift` and `golden_missing` are allowed only when every affected key group carries a one-line **reason**
     in the handoff.
   - `verify.sh` wipes `golden-candidates/` and `probe-results/` first, and refuses `--quick`.
4. **Cross-repo.**
   - A FunkGui task that changes rendering or API also runs FCompressor's `verify.sh` in its lead-made
     `ro-<task>` worktree, configured with the override to its FunkGui worktree (§4.4).
   - An FCompressor task that uses an override declares it.
5. **Visuals** (GUI tasks). PNGs of each changed view × one stepped and one continuous Mode, written with
   `fcmp_probe_plugin ui.dump --view <id> --mode <key> --out x.dump --png x.png` (or `funkgui_framerender x.dump x.png
   2`), and saved under the build directory.

**Handoff**, the agent's final message: ≤ 40 lines, in this order.
1. Task id, worktree path(s), branch(es).
2. `git status --short`.
3. The `verify.sh` summary: counts per status, and the 5 slowest tests.
4. Candidates: for each key group, the reason ("dsp.static.bus-g: new Mode, 118 rows"; "ui.geometry.*: slot caret 1 px
   narrower, intended").
5. Interface-change requests.
6. The FunkGui override and SHA, if any.
7. PNG paths.
8. Known gaps.

### 4.8 Sprint end (lead)

1. **Collect** the handoffs. For each worktree:
   - review `git -C <wt> diff`;
   - run `ownership.py`;
   - read the candidate `.diff` files;
   - look at the PNGs;
   - accept or reject each interface request.
2. **FunkGui first** (only in sprints that merge a FunkGui task; FunkGui is never tagged otherwise).

   ```sh
   cd "$FWT" && git add -A && git commit -m "s1/area: <summary>"     # the lead commits the agent's work
   cd "$FG"  && git merge --no-ff s1/area                             # repeat for each FunkGui task
   cd "$FG"  && cmake --preset lead && cmake --build --preset lead && tools/verify.sh "$FG/build-lead"
   cd "$FG"  && FUNKGUI_ALLOW_BLESS=1 tools/golden.py adopt "$FG/build-lead" --allow-env FUNKGUI_ALLOW_BLESS --only '<globs>' --reason '<why>'
   # bump project(FunkGui VERSION 0.2.0); commit "FunkGui 0.2.0"; git tag -a v0.2.0 -m "<sprint summary>"
   ```

3. **Bump the pin** in `cmake/FcmpDeps.cmake`: `FCMP_FUNKGUI_TAG v0.2.0`, `FCMP_FUNKGUI_SHA <tag sha>`,
   `FCMP_FUNKGUI_VERSION 0.2.0`. **FZ0 errata (R-B0 #10):** `<tag sha>` is the tagged commit,
   `git -C "$FG" rev-parse v0.2.0^{commit}`; FunkGui's tags are annotated, and a bare `rev-parse v0.2.0` prints the tag
   object. FcmpDeps.cmake peels either to the commit, so both configure.
4. **Merge the FCompressor tasks.**
   - For each worktree: `git -C <wt> add -A && git -C <wt> commit -m "s1/<task>: <summary>"`, then in the main
     checkout `git merge --no-ff s1/<task>`.
   - Resolve `Modes.def`, where the reservations make conflicts rare.
5. **Integration verify** with the pin and no override:
   - `cmake --preset lead`, then `cmake --build --preset lead`, then `Scripts/verify.sh --integration build-lead`
     (fails if `fcmp-deps.txt` shows any override; K2 #26c).
   - The run must have no blocking results.
6. **Bless.**
   - `FCMP_ALLOW_BLESS=1 Scripts/golden.py adopt build-lead --only '<globs>' --reason '<why>'`, once per reason
     group. `adopt` refuses rows of `provisional` Modes, and `ui.geometry` rows before FZ5 (K3 #9, #21).
   - Rerun `verify.sh`. It must be **fully green, with 0 candidates** (other than the expected pre-FZ5
     `ui.geometry` and provisional-Mode `golden_missing` rows, listed by name in `docs/sprints/s<N>.md`).
   - **FZ0 errata (R-B0 #12):** `verify.sh` writes `verify-passed-<sha>` only for a clean tree whose HEAD is the one
     `build-lead/built-from-probes.txt` names (§2.9). After committing (step 10), run `cmake --build --preset lead`
     again (a no-op build that rewrites the built-from files) before the stamping `verify.sh` and `validate.sh` runs.
7. **Host validation, every sprint end** (K2 #19): `Scripts/validate.sh --install build-lead` (**FZ0 errata, R-B0
   #11**: `--install` first replaces the installed VST3 and AU bundles with `build-lead`'s; without it validate.sh
   refuses unless the installed `Contents/MacOS/FCompressor` binaries are byte-identical to
   `build-lead/FCompressor_artefacts/Release/`'s, so last sprint's install is never validated in this build's name;
   step 9's owner install replaces them again), against the installed bundles:
   1. `killall -9 AudioComponentRegistrar; auval -strict -v aufx Fcmp Funk`;
   2. `pluginval --strictness-level 10 --repeat 2 --randomise --timeout-ms 900000` on the `.vst3` and the
      `.component` (pluginval from `~/audio/.deps/tools`, built by `deps.sh` from a pinned tag);
   3. it writes its result into the `verify-passed-<sha>` stamp, only when `build-lead/built-from-plugin.txt` names
      that HEAD and a clean tree (R-B0 #12). It exercises Mode fuzzing (the crossfade latch),
      setup-parameter fuzzing (`SetupWatcher`), state restore into a non-fresh instance, editor open/close ×N (attach
      count, ObjC classes) and `getText` from background threads. GarageBand (sandboxed host) is a manual release
      check.
8. **Extra gates at milestones** (S4, S8, S12; K3 #21), or earlier when the named code changed:
   - `Scripts/gui-live.sh build-lead` (window server);
   - `cmake --workflow --preset asan-verify`, `tsan-verify`, `tsan-agent-verify` and `rtsan-verify`;
   - `bench` alone when DSP changed;
   - `lead-x86`, if Rosetta is installed;
   - a universal build.
9. **Install smoke test.**
   - `cmake --build --preset owner` installs into `~/Library/Audio/Plug-Ins`.
   - Open the Standalone once.
10. **Commit and tag.**
   - Commit, in order: the merges, then `deps: FunkGui v0.2.0`, then `goldens: s1 (<reasons>)`.
   - `git tag -a fcmp-s1`.
   - Append new released Modes to `tests/fixtures/modes-ever.tsv` and `modeparam.tsv`. At releases only, add state
     fixtures from the shipped build.
11. **Clean up.**
    - `git worktree remove` for every FCompressor and FunkGui worktree.
    - Keep the `s1/*` branches until the next sprint ends.
    - Update `docs/sprints/s1.md` with the outcome.

### 4.9 The sprint plan (both repositories, S0–S12)

This is the **only** sprint plan; it replaces Draft 2 §10 and Draft 3's S0/S1 outline (K1 #13, K3 §3). Rules:
- **≤ 3 agent tasks per sprint across both repositories**, each sized M (≈ one agent session, 600–1,500 new lines
  including tests). The lead is not one of the 3 (`DECISIONS.md` Q1 asks the user to confirm).
- A task depends only on work merged at an earlier sprint boundary; for FunkGui that means **tagged** (the pipelining
  rule, §4.5).
- Tasks in one sprint share no file (§4.6). Re-ordering is allowed whenever it keeps the "Depends on" column.

#### 4.9.1 Lead pre-work (before S0)

- `Scripts/deps.sh` (the `.deps` cache, prebuilt shaderc).
- **G0**, FunkGui seed (02 §2.1): commit 1 verbatim HR copy + `SEED.tsv`; commit 2 sed renames keeping HR file stems;
  commit 3 a ~15-line CMake exposing `FunkGui::harness`, empty placeholder `FunkGui::{core,gpu,presets}` targets and
  `FUNKGUI_VERSION`; tag **v0.0.1**; detached worktree `FunkGui.wt/pin-v0.0.1`.
- FCompressor base commit: this documentation, `.gitignore`, an empty `docs/sprints/s0.md` with the three manifests.
  B0 writes the CMake skeleton itself.

#### 4.9.2 Work items

"Owns" gives the manifest roots (paths per 01 §2.1). \* marks a spike: a task that settles an early risk.

| ID | Repo | Scope | Owns (roots) | Depends on |
|---|---|---|---|---|
| G1\* | FG | `FunkGuiDeps/Targets.cmake` (globs); Core/Gpu/Fonts/Harness/Shaders targets; **Harness v2**; `golden.py`, `verify.sh`; `fg.harness.self`, `fg.font.probe`, `fg.prefs.check`, `fg.shader.hash`; the GPU target and shaders build in `agent-gui` with the prebuilt shaderc → **v0.1.0** | `cmake/ tools/{golden.py,verify.sh} include/funkgui/test/ test/CMakeLists.txt CMakePresets.json` | G0 |
| G2 | FG | API contracts (02 §10): `Prim`, `PrimList`, `Canvas` (+`emit`), `Panel`, `Input`, `HostServices`, `ParamPort`, `ValueModel`, `CellModel`/`ToggleModel`, widget declarations, `A11yItem`, `Tags`; **implemented** `GestureController`, `ease`, `fmt`, `text`, `a11yDumpLine` | the new headers + `src/{params,core,text}` | G1 |
| G3\* | FG | Recorder: `Canvas.cpp`, dump v2, `Fingerprint`, `FontService`, `HeadlessHost`, `BgfxSink`, `BgfxContext` fonts; `fg.canvas.parity`, `fg.canvas.expansion` | `src/canvas/{Canvas,PrimList,Fingerprint}.cpp src/panel/ src/gpu/{BgfxSink,BgfxContext}.cpp` | G2 |
| G4 | FG | `CanvasShapes.cpp`, AREA shader + `SoftRaster` mirror, FrameRender CLI, `Glyphs.def` + subset, 32 MiB transient buffer | `src/canvas/{CanvasShapes,SoftRaster}.cpp shaders/ tools/FrameRender.cpp fonts/ include/funkgui/text/Glyphs.def` | G3 |
| G5 | FG | `RuleSlider`, `AttachedWord`, `FocusRing` + gallery sections | `src/widgets/{RuleSlider,AttachedWord,FocusRing}.cpp test/gallery/{RuleSlider,Word}Gallery.cpp` | G3 |
| G6 | FG | `SegmentedSelector`, `LatchToggle`, `ThemeCells`, `HintLine`, `DwellSelector`/`ScreenFader`, `LiveFeed`, `UiPreferences`, `MenuLook`, `LineEdit` + gallery | `src/widgets/{Segmented,Latch,Theme,Hint,Dwell}* src/live src/prefs src/juce` | G3 |
| G7\* | FG | `EditorHost`, `A11yBridge`, `FramePump`/`DisplayLink`/`NativeSurface` (`juce::ObjCClass` names), `CaptureConfig`, `capture-frame.sh`; gallery Standalone live == headless | `src/gpu/{EditorHost,A11yBridge,FramePump,DisplayLink,NativeSurface}* tools/capture-frame.sh tools/GalleryApp/` | G3 |
| G8 | FG | FunkPresets: snapshot or header-only re-implementation (lead decides by S6) | `include/funkgui/presets/ src/presets/` | G1; HR presets settled |
| F0 | FC | fcdsp contracts as **compiling** headers (01 §3–§8: `Pid`, `Setup`, `ParamSpec`, `EngineParams`, `Resolve.h`, `Text.h`, `Stage.h`, `IEngine.h`, `TestTap.h`, `EngineHost.h`, `ModeDescriptor.h`, `ModeKit.h`, `DefineMode.h`, `Registry.h`, `UiFrame.h`, `HistoryRing.h`, `Seqlock.h`, `Analysis.h`); **implemented** `HostParams.cpp`, `ModeKit.h`, `Units`, `Seqlock`/`UiFrame`/`HistoryRing` (header-only); `Source/plugin/ProcessorFacade.h` (02 §9.5; compile-checked from FZ1, once FunkGui v0.2.0 declares `ParamPort`); `Scripts/check-headers.sh` | `Source/fcdsp/**/*.h Source/fcdsp/params/HostParams.cpp Source/plugin/ProcessorFacade.h Scripts/check-headers.sh` | – |
| B0 | FC | Build skeleton: `CMakeLists.txt`, `cmake/*` (glob map §2.1, self-registering probes §2.9, `Modes.def` → CTest), `CMakePresets.json`, `Scripts/{verify.sh,golden.py,ownership.py,validate.sh}`, `Tools/probes/common/{ProbeMain,ProbeRegistry,Signals,Tolerances,AllocCounter}`, `dsp.selftest`, `lint.deps`, stub `Processor` + `CreateEditorGeneric` | `CMakeLists.txt cmake/ CMakePresets.json Scripts/ Tools/probes/common/ Source/plugin/{Processor.*,CreateEditorGeneric.cpp}` | G0 |
| F1\* | FC | `Simd`, `FastMath` (incl. `tanh`, `logCosh`, `tanPi`, `sinPi`, `cosPi`), `ScopedFtz`, `Sanitize`, `Smoother4`/`LinearRamp`, `ControlTicker`; `dsp.simd`, `dsp.units`. **Spike:** agent (RelWithDebInfo) and lead (Release+LTO) hashes bit-equal | `Source/fcdsp/core/ Tools/probes/dsp/{simd,units}.cpp` | F0, B0 |
| F2 | FC | `Resolve.cpp` (snap, budget clamp, `resolveView`, `resolve`, `modeDefaults`, `physicalDefault`), `Text.cpp` (`formatParts`/`formatValue`/`formatHost`/`parseHost`), Clean descriptor; `dsp.quant` | `params/{Resolve,Text}.cpp modes/clean/CleanDesc.cpp Tools/probes/dsp/quant.cpp` | F0, B0 |
| F3 | FC | Walking skeleton: `ModeEngine<T>` FF path; `PeakLog`, `QuadKnee` (FF), `LinkMax`, `SmoothBranching`, `ColourNone`, `Flat`; `Registry.cpp`; Clean traits + `Clean.cpp`; `Modes.def` slot 0; `EngineRig`, `Measure`; `dsp.registry`, `dsp.static`, `dsp.time` | `engine/ModeEngine.h stages/…(those 6) modes/{Registry.cpp,Modes.def} modes/clean/{Clean.h,Clean.cpp} Tools/probes/common/{EngineRig,Measure}* Tools/probes/dsp/{registry,static,time}.cpp` | F1, F2 |
| F6\* | FC | `Oversampler` (IIR 2× + Thiran, FIR 4×, constexpr coefficients), `stages/colour/Adaa.h`; **freezes `kStdLatency`, `kStdUpDelay`, `kHqLatency`, `kHqUpDelay`**; `dsp.os`, `proc.osref` | `engine/Oversampler.* stages/colour/Adaa.h Tools/probes/dsp/os.cpp Tools/probes/plugin/osref.cpp` | F1 |
| F9\* | FC | Clean complete + FB: `RmsLog`, `DualDet`, `DetSelect`, `Hold`, `CrestAuto`, `ColourSelect` + `TubeSym`/`DiodeAsym`/`Bright`; `QuadKnee::solveFb`, `SmoothBranching::solveFb/commitFb`, `FeedbackZdf`, `FeedbackDelayed` + the prepare()-time guard; `fb.monotone` rows | `stages/{detector,combinators,colour,gain}/… (those policies) modes/clean/` | F3 |
| F4 | FC | `EngineHost` ECO skeleton: sanitise, chunking, route, mono duplication, key select, `PathState` gain smoothers, colour call, mix at base rate, bypass ramp, poison, snap flag, telemetry accumulation and publish; `dsp.null`, `dsp.hostile`, `dsp.rt`, `dsp.zipper`, `dsp.telemetry` | `engine/EngineHost.cpp engine/host/{Ramps,TelemetryAccum}.h Tools/probes/dsp/{null,hostile,rt,zipper,telemetry}.cpp` | F3 |
| DW\* | FC | **Descriptor wave**: the 7 other descriptors in full (01 §10.4–10.7) + generic traits + `provisional`; `Modes.def` slots 1–7 | `modes/{bus-g,fet-76,opto-2a,mu-67,diode-609,bus-25,brickwall}/** modes/Modes.def` | F2, F3, F9 |
| F5 | FC | `ScFilter` (TPT SVF HPF, tilt), `Router` (6 `stmode`s, key bus), `LinkIndependent`/`Mean`/`CvSum`, `Delay`; `dsp.sc`, `dsp.link` | `engine/host/{ScFilter,Router,Delay}.h stages/link/* Tools/probes/dsp/{sc,link}.cpp` | F1 |
| F8 | FC | Analysis: `staticGain`, `staticGr`, `localRatio`, `netGainDb`, `inputThresholdDb`, `stepResponse`, `measure`, `scResponse`, `colourCurve`, `harmonicsDb`, `overlaySmoothed`; `dsp.analysis` | `Source/fcdsp/analysis/ Tools/probes/dsp/analysis.cpp` | F3, F5 |
| F7 | FC | `EngineHost` complete: OS domain (one `up`, one `down`), mix in OS, `D_up` SC delay, lookahead and budget, `Crossfade` (`PathState`, carry + `msDomain` seed, 50 ms gap, `keyExt`), delta, listen; `dsp.switch`, `dsp.latency`, `dsp.print` | `engine/EngineHost.cpp engine/host/Crossfade.h Tools/probes/dsp/{switch,latency,print}.cpp` | F4, F5, F6 |
| P1 | FC | Processor: buses, `SetupWatcher`, `prepareToPlay` configure + latency, `processBlock`/`Bypassed`, batch counter, `ProcessorFacade` implementation (ports), `ParamLayout` (`kApvtsOrder`, `""` labels, 1 program), `HostText`; `proc.{layout,text,chunk,latency,bypass,null}` | `Source/plugin/{Processor.*,SetupWatcher.h,ParamLayout.cpp,HostText.cpp} Tools/probes/plugin/{layout,text,chunk,latency,bypass,null}.cpp` | F7, F2 |
| P2 | FC | `State.cpp`, `StateMigration.cpp`, `modeId`/`modeRev` resolution, `StateNotice`, `listen`/`delta` reset, batch bracket; `proc.{state,modeparam,fixtures}` | `plugin/{State.*,StateMigration.cpp} Tools/probes/plugin/{state,modeparam,fixtures}.cpp` | P1 |
| P3 | FC | `Presets.cpp` (hooks, `PresetAccess`), `factory/FactoryBank.cpp` + `factory/*.inc`; `proc.{presets,prefs}` | `plugin/{Presets.cpp,factory/**} Tools/probes/plugin/{presets,prefs}.cpp` | P2, G8 |
| M1 | FC | Bus G: `DualRelease`, `AutoSwitch`, `VcaBus` | `modes/bus-g/** stages/ballistics/DualRelease.h stages/combinators/AutoSwitch.h stages/colour/VcaBus.h docs/modes/bus-g.md` | DW |
| M2 | FC | FET 76: `FetColour`, `law::FetVcr`, ALL (monotone), GR switch, linked minimum attack | `modes/fet-76/** stages/colour/FetColour.h stages/law/FetVcr.h docs/modes/fet-76.md` | DW, F7 |
| M3 | FC | Opto 2A: `OptoSense`, `OptoCell`, `OptoCellCurve`, `R37Shelf`, `TubeTransformer` | `modes/opto-2a/** (those policies) docs/modes/opto-2a.md` | DW |
| M4 | FC | Mu 67: `ProgressiveKnee` in `FeedbackZdf`, `TcSelector`, `MultiStage3`, `TubePushPull`, Lat/Vert | `modes/mu-67/** (those policies) docs/modes/mu-67.md` | DW, F5 |
| M5 | FC | Diode 609: `SharedElementMax`, `DiodeBridge`, `SlowHp`, A1/A2 in FB | `modes/diode-609/** (those policies) docs/modes/diode-609.md` | DW |
| M6 | FC | Bus 25: FF/FB per chunk, `LinkCvSum` in FB, `tmode` variant, 3 knees, `Thrust` | `modes/bus-25/** (those policies) docs/modes/bus-25.md` | DW, F5 |
| M7 | FC | Brickwall: `SlidingMaxBox` + scratch, true-peak SC 4× (`scDelaySamples`), `LoudClip` | `modes/brickwall/** (those policies) docs/modes/brickwall.md` | DW, F7 |
| U1a | FC | UI contracts + slots: `Panel` composition, `SubView`, complete `Layout.h`/`Tags.h`, `SlotModel`, `SlotGrid` (21 slots, words, landing carets), stub sub-views, `FakeFacade`; `ui.{textfit,a11y,input,font,geometry}` (spec rows) | `Source/editor/{Panel.*,SubView.h,Layout.h,Tags.h,SlotModel.*,views/SlotGrid.*} + stub views; Tools/probes/plugin/{FakeFacade.*,ui_*.cpp}` | G5, F2, DW |
| U1b | FC | Chrome: Header (Mode latch), DisplayRow (cells, latches), Footer (spec line, notices, THEME) | `views/{Header,DisplayRow,Footer}.*` | U1a, G6 |
| U2 | FC | Band: `HistoryStore`, `HistoryPlot`, `TransferPlot`, `MeterColumn`, live subs; `ui.curve`, `ui.truth` | `editor/HistoryStore.h views/{Band,HistoryPlot,TransferPlot,MeterColumn}.* Tools/probes/plugin/ui_{curve,truth}.cpp` | U1a, G4, F8 |
| U3 | FC | Characteristics A: `CharScreen`, `ControlPathPlot`, `Readouts`, fader | `views/{CharScreen,ControlPathPlot,Readouts}.*` | U2 |
| U4 | FC | Characteristics B: `StepPlot` + `PreviewWorker`, `SidechainPlot`, `ColourPlot` | `views/{StepPlot,SidechainPlot,ColourPlot}.* editor/PreviewWorker.*` | U1a, F8 |
| U5 | FC | `ModeBrowser`, Tab order and a11y audit across sub-views | `views/ModeBrowser.*` | U1b |
| U6 | FC | `PresetStrip` + `PresetBrowser` over `PresetAccess` | `views/{PresetStrip,PresetBrowser}.*` | U1b, G8 |
| U7 | FC | GPU `Editor`, `CreateEditorGpu.cpp`, `gui-live.sh`, `FCMP_UI_FIXED_DT`/`NO_LIVE`/`NO_HINT` | `Source/editor/gpu/ Source/plugin/CreateEditorGpu.cpp Scripts/gui-live.sh` | G7, P1, U1b |
| H1 | both | Hardening: asan/tsan/rtsan fixes, UI geometry goldens adopted (FZ5), bench, [H]-fit triage | assigned by the lead | all |

#### 4.9.3 Sprint table

| Sprint | Slot 1 | Slot 2 | Slot 3 | FunkGui tag at end | Lead extras |
|---|---|---|---|---|---|
| S0 | G1\* FunkGui build + Harness v2 (FG) | F0 fcdsp contracts | B0 build skeleton (on pin v0.0.1) | v0.1.0 | pin v0.1.0; **FZ0** |
| S1 | F1\* math core | F2 resolver/text/Clean descriptor | G2 FunkGui API + utilities (FG) | v0.2.0 | **FZ1**; first `lead`-preset determinism check |
| S2 | F3 walking-skeleton engine | F6\* oversampler | G3\* recorder (FG) | v0.3.0 | **FZ2**; first Clean DSP goldens |
| S3 | F9\* Clean complete + FB | F4 EngineHost ECO | G5 RuleSlider/words (FG) | v0.4.0 | – |
| S4 | DW\* descriptor wave | F5 SC/router/link/delay | G4 shapes/AREA/raster/glyphs (FG) | v0.5.0 | **FZ3**; milestone gates; the lead reads HR's preset status |
| S5 | F8 analysis | U1a UI contracts + slots | G6 cell widgets (FG) | v0.6.0 | **FZ4** |
| S6 | F7 EngineHost complete | U2 band | U1b chrome | – | FunkPresets snapshot-or-reimplement decision |
| S7 | P1 processor | U4 Characteristics B | M1 Bus G | – | first `validate.sh` with a real processor |
| S8 | M2 FET 76 | U3 Characteristics A | G7\* EditorHost + gallery parity (FG) | v0.7.0 | milestone gates; gui-live (gallery) |
| S9 | M3 Opto 2A | M4 Mu 67 | U5 browser + Tab/a11y | – | – |
| S10 | M5 Diode 609 | M6 Bus 25 | U7 GPU editor + live parity | – | first FCompressor gui-live |
| S11 | M7 Brickwall | P2 state/migration | G8 FunkPresets (FG) | v0.8.0 | – |
| S12 | P3 presets + factory bank | U6 preset strip | H1 hardening + UI golden freeze | – | **FZ5**; milestone gates; release build; install smoke test |

- **Critical chain:** F1/F2 → F3 → F9 → DW → U1a → U2 → U3 (S1–S8). DW is on it because the UI's `textfit`/`a11y`
  gates need all 8 descriptors and the generic FB traits need F9. The plan is throughput-bound (≈ 39 tasks over 3
  slots), so the FunkGui chain and the Mode tasks have slack.
- **Likely pull-forwards:** P2 into S8 (swapped with U3; state is v1-forever, so earlier evidence is worth more); M1 into
  S5 if U1a slips. If F9 runs long (it is M+), F4 and G5 still merge; the lead may carry F9 into S4 and push DW to S5,
  which delays the UI chain by one sprint. S6 has two M+ tasks: start U2 and F7 first.
- **Mode waves after v1** follow the same pattern: one DW task per wave (≤ 6 descriptors, owning the wave's
  `Modes.def` lines), then ≤ 3 Mode DSP tasks per sprint, with no shared files.

#### 4.9.4 Freeze points

| Freeze | End of | What becomes Sprint-frozen | Evidence required |
|---|---|---|---|
| FZ0 | S0 | 01 §3–§8 headers (with `TestTap`, `EngineTelemetry`, `ControlIo::bits`, `FbAffine`, `Setup.h`, `kApvtsOrder`, `DefineMode.h`), `<UI charExpanded scTab>`; Harness v2 API, golden format v2, exit codes; FunkGui CMake target and function names; the probe registration macro and CLI; the glob map | `lint.headers` green; `fg.harness.self`; `dsp.selftest` |
| FZ1 | S1 | FunkGui canvas, panel and widget API (G2); `ProcessorFacade.h` (compiles against v0.2.0); `resolve`/`snap` semantics | `dsp.quant.clean`; G2 syntax + utility unit tests; `lint.headers` incl. the facade |
| FZ2 | S2 | `ModeEngine<T>`/`EngineRig` behaviour; `kStdLatency`, `kStdUpDelay`, `kHqLatency`, `kHqUpDelay` (**v1-forever** from here on) | `dsp.static.clean`, `dsp.os` |
| FZ3 | S4 | `ParamSpec`/`ModeDescriptor` schema, proven by all 8 descriptors | DW: `dsp.registry` + `dsp.quant.*` for 8 Modes |
| FZ4 | S5 | FCompressor UI composition: `SubView`, `Layout.h`, `Tags.h`, `FakeFacade`, `ViewSpec`/`views()` | `ui.textfit.*`, `ui.a11y.*` for 8 Modes |
| FZ5 | S12 (H1) | UI geometry: `ui.geometry` goldens adopted | `gui-live` parity |
| v1 | v1 tag | `kHostParams`, `kApvtsOrder`, `Modes.def` slots and keys, state XML, preset payload (01 §0) | release checklist (§5) |

#### 4.9.5 Spikes

| Spike | Task / sprint | Question it answers | Fallback if it fails |
|---|---|---|---|
| GPU build chain | G1 / S0 | Do `FunkGuiGpu`, the INTERFACE `.mm` sources and the shaders build via the prebuilt shaderc? | Build shaderc from source (§2.5 fallback) |
| Config determinism | F1 / S1 | Are agent (RelWithDebInfo) and lead (Release+LTO) hashes bit-equal (§2.6)? | Agents build Release with `FCOMPRESSOR_LTO=OFF` |
| Recorder parity | G3 / S2 | Is the `BgfxSink` expansion bit-identical to HR's vertex stream (02 §3.2)? | Keep HR's `SdfCanvas` path under the recorder until fixed |
| Oversampler | F6 / S2 | Integer latency ≤ 4 / ≤ 64 samples with the Thiran section, constexpr coefficients, passband spec (01 §5.6) | Accept JUCE's measured latencies as the targets |
| FB solvers | F9 / S3 | Closed-form and Newton ZDF with `FbAffine`: stable, monotone, `staticGr` bit-equal to the audio path (E §2.6, §6.5) | `FeedbackDelayed` for opto only |
| Descriptor schema | DW / S4 | Does the `ParamSpec` schema express all 8 Modes? | Revise the schema at FZ3, before any UI depends on it |
| Live parity | G7 / S8 | Is a live capture equal to the headless fingerprint (C G1)? | Parity gate on geometry only, text excluded |
| FunkPresets availability | lead / by S6 | Has HR's preset code settled? | Seed from HR's headers and re-implement (02 §11 Q7) |

#### 4.9.6 Shared-file hotspots and how each is removed (K3 §2.5)

| File | Who would collide | Mechanism |
|---|---|---|
| `CMakeLists.txt`, `cmake/*.cmake`, FunkGui `cmake/` | every task that adds a file | per-directory globs (§2.1); never edited after S0 |
| probe lists, `ProbeMain` dispatch | every probe-adding task | self-registering probe files (§2.9) |
| `Modes.def` | Mode tasks | the lead in the sprint base, or the single DW task (§4.2) |
| `Registry.cpp`, a generated `ModeIncludes.h` | Mode tasks | `FCDSP_DEFINE_MODE` in each Mode's TU; no include list (01 §8.2) |
| `stages/{Detector,Ballistics,Colour,…}.h` | Mode and policy tasks | one header per policy, no umbrella (01 §5.2) |
| `ModeKit.h` | Mode tasks adding helpers | frozen per sprint; Mode-local helpers stay in `modes/<key>/` |
| a shared factory-preset file and counter | Mode tasks | P3 writes the bank once; later `factory/<key>.inc`; hashed revision (01 §9.2) |
| `modes/clean/dsp.switch.txt`, global registry/browser goldens | Mode tasks' candidates | switch pairs owned by the higher slot; spec-only globals (§3.4, §3.6) |
| `EngineHost.cpp` | host features | components in `engine/host/*.h`; one owner per sprint (01 §5.4) |
| `Processor.cpp` | P1, P2, P3 | P1 owns it; state and presets through their own files and hooks |
| `editor/Panel.cpp`, `Layout.h`, `Tags.h` | UI tasks | sub-view composition; complete and frozen in U1a (02 Part 2) |
| FunkGui `GalleryPanel.cpp`, `Glyphs.def`, `Canvas.cpp` | G4, G5, G6 | per-widget gallery files; G4 alone owns glyphs; `emit()` + `CanvasShapes.cpp` (02 §3.3) |
| `Tolerances.h`, `tests/golden/**`, `tests/fixtures/**`, `CMakePresets.json`, `CLAUDE.md` | – | lead only (§4.6) |
| `docs/modes/<key>.md` | Mode tasks | owned by that Mode's task (§4.6) |

---

## 5. Release script outline (`Scripts/release.sh`, adapted from HR `Scripts/release.sh`)

`Scripts/release.sh <build-dir> [<out-dir>]`, POSIX `sh`, `set -eu`. It is HR's 63 lines (B §5) with these changes.

1. **Names.**
   - Bundles are found under `$BUILD/FCompressor_artefacts/Release/`:
     `AU/FCompressor.component`, `VST3/FCompressor.vst3`, `Standalone/FCompressor.app`.
   - Entitlements: `Resources/FCompressor.entitlements` (audio-input only).
   - Environment: `FCMP_SIGNING_IDENTITY`, which is `Developer ID Application: Sean Funk (Y29FLXW57M)` in the login
     keychain today [M]. `FCMP_NOTARY_PROFILE` for notarisation.
2. **New pre-flight refusals.** The script stops when:
   - the build type is not Release, or `FCOMPRESSOR_RELEASE` is not ON (read from the CMakeCache);
   - **architecture (K2 #15):** v1 ships **arm64-only** (the `release` preset; `lipo -archs` must be `arm64`). A
     universal build (`FCOMPRESSOR_UNIVERSAL=ON`, `lipo -archs` = `x86_64 arm64`) is accepted only when
     `build-lead-x86/verify-passed-<HEAD sha>` exists, i.e. the SSE backend has actually executed the verify suite under
     Rosetta. The adopt log marks arm64-only golden rows as `arch: arm64-only`, never as arch-neutral;
   - `fcmp-deps.txt` shows any override;
   - the working tree is dirty, or HEAD is not tagged `v<project version>`;
   - there is no `build-lead/verify-passed-<HEAD sha>` stamp, or it does not record a passing `validate.sh` (auval
     `-strict` + pluginval, §4.8). `verify.sh` writes the stamp only after a fully green run;
   - `nm -gU` on a main binary exports more than the plugin entry points (bgfx/bx/bimg must be hidden, K2 #26f).
3. **Clean the bundles:**
   - `chflags -R nouchg` on each bundle, because codesigned installs pick up immutable flags (B §6.2);
   - `xattr -cr`.
4. **Sign each bundle** as in HR:
   - `codesign --force --options runtime --entitlements … --timestamp --sign "$ID"`, or ad-hoc `--sign -` as a dry run
     when the identity is unset;
   - `codesign --verify --strict --verbose=2`;
   - print the entitlement, identifier and team.
5. **Package:** `ditto -c -k --keepParent` into `$OUT/FCompressor-<version>/`.
6. **Notarise** when `FCMP_NOTARY_PROFILE` is set, per bundle:
   - `xcrun notarytool submit --wait`;
   - `stapler staple`;
   - `stapler validate`;
   - re-zip the stapled bundle.
7. **Manifest:** write `$OUT/FCompressor-<version>/MANIFEST.txt`:
   - version and git SHA;
   - the JUCE, bgfx.cmake and FunkGui tags and SHAs from `fcmp-deps.txt`;
   - the flags line;
   - `SHA256SUMS` of the zips.
8. **Post-flight** (printed as a checklist, not automated):
   - install the zips on a clean user account;
   - run `auval -v aufx Fcmp Funk`;
   - load the plugin in one AU host and one VST3 host.

A `.pkg` installer through `productbuild` is out of scope for v1.

---

## 6. Open questions

Draft 3's questions, as resolved by the synthesis. The user-level ones are collected, with a recommended default each,
in `docs/DECISIONS.md` §"Open questions for the user".

1. **Rosetta 2 / Intel slice** — resolved by default: v1 ships arm64-only; a universal build needs an executed x86
   verify (§5). Whether to install Rosetta (and ship universal) is the user's call: `DECISIONS.md` Q6.
2. **`FunkGui::harness` in FunkGui** — kept (one harness for FunkGui, FCompressor and HR's migration); it extends the
   user's "GUI library" decision, so it is `DECISIONS.md` Q3.
3. **In-house oversampler** — closed: 01 §5.6 adopts it; `proc.osref` keeps JUCE as a passband/rejection reference.
4. **Prebuilt tools in `~/audio/.deps/tools`** (shaderc; pluginval added by K2 #19) — build tools from pinned upstream
   sources, not vendored libraries: `DECISIONS.md` Q8.
5. **Agents RelWithDebInfo, lead Release+LTO** — kept; the F1 spike proves bit-equality, with Release/no-LTO agents
   as the fallback (§2.6, §4.9.5).
6. **Agents do not commit; the lead commits each worktree on its branch at sprint end** — kept as the reading of
   "commits at sprint boundaries by the lead" (§4.8).
7. **`gui-live.sh` launches the Standalone** (window server, possible microphone TCC prompt) — lead-only, milestones
   only (§3.6, §4.8).
8. **Mode-browser golden churn** — closed: `ui.browsers` is spec-only and each Mode's browser row is fingerprinted in
   its own `ui.geometry.<key>` (§3.6; K3 #17).
