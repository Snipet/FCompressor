# Sprint 0 review (before FZ0)

Three read-only reviewers checked the S0 merges on 2026-09-23. Every finding below is accepted and fixed by the
S0 fix-up cards (FX-A: fcdsp headers, FX-B: FCompressor build/verify, FX-C: FunkGui) before FZ0 is declared.

## R-F0 — fcdsp contract headers (fixed by FX-A)

1. **Major.** The audio-path declarations that `process()` and `control()` call are not marked nonblocking, so `-Wfunction-effects -Werror` will fail on them. This blocks the rtsan build, which the S4 milestone gate and P1's (S7) acceptance both need.
   - **Where:**
     - `Source/fcdsp/engine/IEngine.h:69-82`: every virtual except `control`/`colour` has no `FCDSP_NONBLOCKING`, including `~IEngine`.
     - `Source/fcdsp/modes/Registry.h:29` (`ModeEntry::construct` pointer type) and `:39-41` (`bySlot`, `resolveSlot`).
     - `Source/fcdsp/engine/Oversampler.h:36-38` and `EngineHost.h:70` (`reset`).
     - Core: `ScopedFtz.h:15-16`, `Sanitize.h:12`, `Smoother.h:18-29`, `ControlTicker.h:14`, `FastMath.h:20-36`.
     - The macro itself is defined in `engine/IEngine.h:16-23`, so `core/` headers cannot use it without an upward include.
   - **What is wrong:** `EngineHost::process` and `ModeEngine::control` are nonblocking. They call functions that are not annotated and cannot be inferred: virtual calls, calls through function pointers, and out-of-line definitions.
   - **Verified** by a scratch compile with `-Wfunction-effects`. It warns on:
     - `IEngine::setParams`, `autoMakeupDb`, `finite` and `~IEngine`;
     - `ModeEntry::construct` and `bySlot`;
     - `ScopedFtz` ctor and dtor, `sanitize`, `Smoother4::tick`.
   - **Failure scenario:** F4 (S3) writes `process()`, which does a snapped swap with construct, `~IEngine`, `setParams`, `snapParams`, `seed`, then per block `autoMakeupDb`, `finite` and `up`/`down`. From then on `cmake --build --preset rtsan` fails, because `FCOMPRESSOR_WERROR` defaults to ON. F4 and F6 cannot fix it without changing frozen declarations.
   - **Fix:**
     - Move the `FCDSP_NONBLOCKING` definition into `core/` (top of `Simd.h`, or a new `core/Rt.h` that `Simd.h` includes). `IEngine.h` keeps using it.
     - Annotate `virtual ~IEngine() FCDSP_NONBLOCKING = default;` and every `IEngine` virtual (`prepare`, `reset`, `setParams`, `snapParams`, `carry`, `seed`, `autoMakeupDb`, `scDelaySamples`, `internals`, `telemetry`, `finite`). Repeat it on each `ModeEngine` override.
     - `static IEngine* construct(void*) noexcept FCDSP_NONBLOCKING;` in `ModeEngine`, and `IEngine* (*construct)(void*) noexcept FCDSP_NONBLOCKING;` in `ModeEntry`.
     - Annotate `bySlot` and `resolveSlot`, `EngineHost::reset`, and `Oversampler::reset`/`up`/`down`.
     - Core: annotate the `ScopedFtz` ctor/dtor, `sanitize`, `Smoother4`/`LinearRamp`/`ControlTicker` members and the FastMath functions. Better still, declare these `inline` with the bodies in their headers, which also keeps them inlinable in non-LTO agent builds.
     - A scratch compile of this annotated shape is clean under `-Wfunction-effects -Werror` (virtual dtor, function-pointer member, placement-new `construct`).

2. **Major.** The stage concepts give the per-sample smoothers (01 §5.1) no way to reach the gain computer or stage 2.
   - **Where:** `Source/fcdsp/engine/Stage.h:27-29`, `42-47`, `66-69`; `ModeEngine.h:45`.
   - **What is wrong:** 01 §5.1 smooths `{thrDb, slope, range}` (`lvl_`) and `{s2ThrDb, kneeDb}` (`lvl2_`) per sample. But:
     - `G::target(c, x)` and `G::solveFb(c, x, a)` take only `Coeffs` and `x`.
     - `S2::combine(c, s, r, x)` takes only `Coeffs`, state, `r` and `x`.
     - `design()` is documented as "runs on control ticks only".
   - **Failure scenario:** F3 (S2) must either call `G::design`/`S2::design` every sample with a patched `EngineParams` (breaking the documented contract, and costing a divide per sample in `QuadKnee`'s `inv2W`), or apply thr/slope/knee/s2thr in 16-sample steps. Steps would make `lvl_`/`lvl2_` dead weight and cause zipper noise. Later computers (ProgressiveKnee, TableCurve, OptoCellCurve, LimiterCurve), written by Mode tasks that cannot edit `ModeEngine.h`, would each have to guess F3's workaround.
   - **Fix:** add a per-sample control struct and thread it through:
     ```cpp
     struct LevelCtl { simd::f32x4 thrDb, slope, kneeDb, s2ThrDb; };
     { G::target(c, x, l) } noexcept -> std::same_as<simd::f32x4>;
     { G::solveFb(c, x, l, a) } noexcept -> std::same_as<simd::f32x4>;
     { S2::combine(c, s, r, x, l) } noexcept -> std::same_as<simd::f32x4>;
     ```
     `staticGr` and analysis build `l` from `EngineParams`.

3. **Minor.** The concept set does not cover everything `ModeEngine` must do.
   - **Where:** `Stage.h:49-50`, `66-76`; `ModeEngine.h:35-37`, `44`, `69`.
   - **What is wrong:**
     - `Stage2Policy` has no `seed(s, v)`, so `Carry::s2GrDb` (01 §5.5 step 3) cannot be applied generically.
     - There is no `ScShapePolicy`, although `ModeEngine` holds `M::ScShape::State`/`Coeffs` and must implement `scShapeDb()` from it.
     - `LinkPolicy` is missing from `ModeEngine`'s `static_assert`.
     - `ColourPolicy::process(c, s, x, gr, n, 0)` does not say what the 6th argument means, and `transfer(c, 0.f, 0.f)` does not give its argument order.
   - **Failure scenario:** a Mode switch into Diode 609 with stage 2 engaged restarts stage 2 from 0 dB, overshooting during the fade. R37Shelf/SlowHp/Thrust (S9–S11, written by Mode tasks that cannot edit `Stage.h` or `ModeEngine.h`) have no frozen signature to follow. One colour policy reads the 6th argument as a channel, another as drive.
   - **Fix:**
     - Add `{ S2::seed(s, v) } noexcept;` to `Stage2Policy`.
     - Add `template<class S> concept ScShapePolicy = Designable<S> && requires(const typename S::Coeffs& c, typename S::State& s, simd::f32x4 v, float hz, float fs){ { S::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>; { S::magDb(c, hz, fs) } noexcept -> std::same_as<float>; };`
     - Add `LinkPolicy<typename M::Link> && ScShapePolicy<typename M::ScShape>` to the `static_assert`.
     - Name the parameters in comments: `process(c, s, x, grDb, n, channel)` and `transfer(c, x, grDb)`.

4. **Minor.** `kSnapDomain` is positional and nothing checks it against `Pid`.
   - **Where:** `Source/fcdsp/params/Pid.h:47-53`.
   - **What is wrong:** the lane names exist only in comments. `Pid` is documented as "free to regroup between sprints". `kHostParams` checks `idx(pid)==i` and `kResolveOrder` uses names, but this table has no guard.
   - **Failure scenario:** someone moves `hold` ahead of `atk` in `Pid`. `atk` then silently snaps in the linear domain, so stepped attack boundaries and `print` hashes move with no compile error.
   - **Fix:** replace the table with `constexpr SnapDomain snapDomain(Pid p) noexcept { switch (p) { case Pid::atk: case Pid::rel: case Pid::s2atk: case Pid::s2rel: return SnapDomain::log; case Pid::schpf: return SnapDomain::host; default: return SnapDomain::linear; } }`. Or keep the array and `static_assert` each non-linear entry by name.

5. **Minor.** The `Pid` indexers accept all 29 Pids but back only 22 values.
   - **Where:** `Source/fcdsp/params/Resolve.h:37-38`, `:47`; `Text.h:26-28`.
   - **What is wrong:** `RawParams::operator[](Pid)` and `ParamView::operator[](Pid)` accept any `Pid`, but hold `kNumModeParams` = 22 entries. `formatHost`/`parseHost` take any `Pid` with the documented behaviour "resolves current with `v[pid] = plain`".
   - **Failure scenario:** P1's HostText lambdas call `formatHost` uniformly for all 29 APVTS parameters. For `mode`, `quality` and the other globals, `v[22..28]` is written out of bounds. That is undefined behaviour on the host's text thread.
   - **Fix:** document the precondition `idx(pid) < kNumModeParams` on both functions and the indexers, and add `assert(idx(p) < kNumModeParams)` in the indexers. Also name who formats the 7 globals: the registry name for `mode`, and `choices`/OFF-ON for the rest.

6. **Minor.** `ModeEngine`'s state is private, so the Traits `internals()` hook cannot read it.
   - **Where:** `Source/fcdsp/engine/ModeEngine.h:38-47`.
   - **What is wrong:** the documented hook is `static void internals(const auto& engine, float out[kInternals])` (01 §5.3), but `det_`, `bal_` and `s2_` are private, with no friend and no accessor.
   - **Failure scenario:** Clean's REL EFF, CREST and PEAK/RMS DET internals, or FET's LOOP CV, fail to compile with "'bal_' is a private member". Mode tasks M1–M7 own only `modes/<key>/**` and cannot add access.
   - **Fix:** add `friend M;` to `ModeEngine`, or public `const auto& detState() const noexcept` / `balState()` / `s2State()` accessors, now while the header is still open.

7. **Minor.** `BlockParams` and `Carry` have fields with no default initializers.
   - **Where:** `Source/fcdsp/engine/EngineHost.h:42-46`; `IEngine.h:34-42`.
   - **What is wrong:** in `BlockParams`, `slot`, `bypass`, `delta`, `listen` and `extKey` have none. In `Carry`, `valid` ("0 = cold start") has none.
   - **Failure scenario:** the processor's "previous `BlockParams`", reused while a batch is open (01 §2.3), is read before its first write, meaning an indeterminate `bool`. A host path that writes `Carry c;` for a cold start seeds from a garbage `valid`.
   - **Fix:** `uint8_t slot = 0; EngineParams eng{}; bool bypass = false, delta = false, listen = false, extKey = false;` and `uint8_t msDomain = 0; uint8_t pad[3]{}; uint32_t valid = 0; float relNowMs[2]{};`. Give the `f32x4` members `{}`.

8. **Minor.** Bit 7 of `SpecFlag` is listed as reserved but is already taken by `kClamped`.
   - **Where:** `Source/fcdsp/params/ParamSpec.h:62` against `Resolve.h:27,30`.
   - **What is wrong:** the comment says "bits 4..7 reserved", but `ResolvedParam::flags` = `ParamSpec::flags | kClamped` with `kClamped = 1u << 7`.
   - **Failure scenario:** a future `SpecFlag` added at bit 7 (the schema allows additions until FZ3) makes every spec with that flag read as "clamped" in UI, text and probes.
   - **Fix:** change the comment to "bits 4..6 reserved; bit 7 is `kClamped` (Resolve.h)". Add `static_assert((kClamped & (kFlagExtension | kFlagHwReversed | kFlagProgram | kFlagPlotIsPlain)) == 0);` in `Resolve.h`.

**Checked and found clean:**
- **Pid:** `Pid`, `kApvtsOrder` and `kResolveOrder`, including the permutation `static_assert`s.
- **HostParams:** all 29 rows match 01 §3.1 (ranges, maps, defaults; ratio as S 0–2; mode 0..127 non-automatable; mix 0–2; tmode 0..7 so the FET GR OFF step at 7 is reachable). The `ratio3` maps and their inverses are correct, and the power exponents are 2.585, 3.322 and 2.644.
- **Layouts**, checked with `offsetof` asserts: `EngineParams` 116 B (`m`@72, `tags`@112), `UiFrame` 288 B, `HistoryColumn` 32 B, `Carry` 64 B, `KernelKey` 6 B.
- **Memory ordering:** the `Seqlock` and `HistoryRing` claim-word ordering is correct by fence analysis. A single-thread lap test gave first = 905 and 4095 columns, as expected.
- **Registry and `FCDSP_DEFINE_MODE`:** in a scratch build with slots 0 and 2 active plus a retired slot 9, two fake Modes compiled and linked. `bySlot`, `resolveSlot`, `resolveKey` and `slotOf` returned the right results. No static initialisers were emitted.
- **Descriptors:** design-style designated-initializer descriptors compile under `-Werror`.
- **Headers:** all 31 compile standalone for arm64 and for x86_64 with `-mavx2 -mfma`.
- **Lint:** `lint.deps` reports 0 violations.
- **Docs:** the `modes()` → `modeSlots()` and `DefineMode` errata are already in the lead's uncommitted diff to 01, 02 and SPRINTS.

Nothing in either repo was modified, and the scratch files are deleted.

## R-B0 — build skeleton and verification plumbing (fixed by FX-B)

**B0 review (Sprint 0): 13 findings, most severe first**

Where I verified something with a run, it was either read-only or a tiny scratch run under `/private/tmp`, deleted afterwards. I did not run the DSP-only configure. The disk had 763 MB free, which is under the 1.2 GB threshold, and partway through the review it filled completely (ENOSPC).

**1. major · `cmake/FcmpProbes.cmake:118`: the probe tests ignore the exit code, so sanitizer failures can never fail**
- **What's wrong:** each probe test uses `PASS_REGULAR_EXPRESSION`. When that property is set, CTest ignores the process exit code.
- **Failure scenario:** TSan finds a race, prints a warning, the probe still prints `RESULT {..."status":"pass"...}` and the process exits 66.
  - I reproduced this in a scratch CTest project: the test passes and the JUnit status is `run`.
  - `verify.sh` then reports PASS. Its own "process failed after its results" branch (`verify.sh:203-205`) can only fire on a signal.
  - Result: `tsan-verify` and `tsan-agent-verify` cannot fail on a race, and ASan or RealtimeSanitizer errors after `finish()` pass too.
- **Fix:** drop the regex and map exit codes 2 and 3 to pass. Every other exit code, and every signal, then fails:
  `add_test(NAME ${name} COMMAND /bin/sh -c "\"$@\"; rc=$?; case $rc in 2|3) exit 0;; esac; exit $rc" fcmp-probe $<TARGET_FILE:${exe}> ${layer}.${probe} ${margs} --golden-root … --results …)`
- **Lighter alternative:** keep the regex and add `FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|UndefinedBehaviorSanitizer|RealtimeSanitizer|runtime error: "`.

**2. major · `Scripts/golden.py:41-42`: `report` and `diff` break once the pin moves to v0.1.0**
- **What's wrong:** the wrapper passes `--allow-env FCMP_ALLOW_BLESS` to every subcommand. In FunkGui's `tools/golden.py`, only `adopt` accepts that flag.
- **Failure scenario:** after the pin bump, `Scripts/golden.py report build-lead` and `… diff …` both exit 2 with "unrecognized arguments: --allow-env FCMP_ALLOW_BLESS". I confirmed this against FunkGui main.
- **Fix:** add `["--allow-env","FCMP_ALLOW_BLESS"]` only when `argv[0] == "adopt"`.

**3. major · `cmake/LintDeps.cmake:128-131`: the `fcdsp.static` rule misses the most common forms**
- **What's wrong:** the lint treats any `(` as a function declaration. It also scans only indented `static` lines.
- **Failure scenario:** in a scratch copy, the lint passed both of these, which 01 §2.2 rule 5 forbids:
  - `    static std::vector<float> t(8);` inside a function (a lock and an allocation on first call);
  - `static std::vector<float> g = …;` or `std::vector<float> g(64);` at namespace scope (static constructors).
- **Fix:**
  - Add `-Wglobal-constructors -Wexit-time-destructors` to `fcmp_warnings` (`FcmpArch.cmake:78-79`; it applies to fcdsp only, with -Werror). F0's `HostParams.cpp` and `Registry.cpp` pass with these flags; I checked.
  - In `_lint_static`, also flag the `(` form when the first argument is a literal (a digit, quote, `-` or `{`).

**4. major · `cmake/LintDeps.cmake:78`: the libm regex misses the likely calls**
- **What's wrong:** the regex is exactly the one in 01 §2.2 rule 4, so it doesn't match `std::log10`, `std::log2`, `std::exp2`, `expm1`, `atan`, `sinh`, `sinf`/`cosf` and similar.
- **Failure scenario:** a Mode computes dB as `20*std::log10(x)`, or calls `std::exp2`. I confirmed the lint passes both. The `print.*` and `xarch.` hashes then depend on Apple's libm and CPU architecture, and nobody notices until an OS update or an x86 run.
- **Fix:** use `(^|[^A-Za-z0-9_])((std::)?(a?(sin|cos|tan)h?f?|atan2f?|exp(2|m1)?f?|log(2|10|1p)?f?|powf?|cbrtf?|hypotf?|erfc?f?|[lt]gammaf?))[ \t]*\(`, keeping the existing exemptions. Make the same edit in 01 §2.2. F0's current code still passes: every hit is a declaration or a `fcdsp::`-qualified call.

**5. major · `CMakePresets.json:87-88`: UBSan in the asan preset cannot fail**
- **What's wrong:** `-fsanitize=undefined` recovers by default. It prints `runtime error:` and the process exits 0.
- **Failure scenario:** `asan-verify` passes with undefined behaviour in the log.
- **Fix:** add `-fno-sanitize-recover=undefined` to `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`. This also needs fix 1, so that halting after the RESULT line counts as a failure.

**6. minor · `cmake/FcmpPlugin.cmake:55-57` with `Scripts/check-headers.sh:25`: our plugin sources face a stricter warning set than fcdsp and lint.headers**
- **What's wrong:** the plugin's own sources get JUCE's strict warning set (`-Wconversion`, `-Wshorten-64-to-32`, `-Wfloat-equal`, `-Wsign-conversion` and others) combined with our per-file `-Werror`. fcdsp, `lint.headers` and `fcmp_probe_plugin` use only `-Wall -Wextra -Wshadow -Wpedantic`.
- **Failure scenario:** an F4 or F7 header with `int n = span.size();` builds in fcdsp and in the probes. It then breaks the plugin build at P1, because Processor.cpp includes `EngineHost.h`. The fix would mean editing a header another card owns.
- **Fix:** use JUCE's clang warning list in `FCMP_WARNING_FLAGS` and in `check-headers.sh`. All 29 of F0's headers pass the full list with `-Werror` today (checked), so this is cheap before the FZ0 freeze.

**7. minor · `Scripts/verify.sh:78` and `Tools/probes/common/ProbeMain.cpp:83-102`: probe sandboxes are never reset**
- **What's wrong:** `sandbox/<test>/` (the probe's scratch prefs directory and `presets.db`) is created but never wiped.
- **Failure scenario:** a second `verify.sh` run finds the first run's `presets.db`, so `proc.presets` and `proc.prefs` see leftover state (duplicate names on import) and give run-dependent results.
- **Fix:** `rm -rf "$BUILD/sandbox"` in `verify.sh`. In ProbeMain, call `fs::remove_all` before creating the directory when the path contains a `/sandbox/` component.

**8. minor · `Scripts/check-headers.sh:25`: lint.headers cannot catch non-inline definitions**
- **What's wrong:** 03 §4.7 says the check proves every helper is inline. `float f(float x){…}` or `float g = 1;` in a header passes, and I confirmed that. It later causes duplicate-symbol link errors.
- **Fix:** add `-Wmissing-prototypes -Wmissing-variable-declarations`. F0's headers are clean with these (checked).

**9. minor · `cmake/FcmpArch.cmake:126-141`: a failed "can the probes run?" check is cached forever**
- **Failure scenario:** the lead follows the configure warning and installs Rosetta. Reconfiguring `build-lead-x86` still leaves every probe DISABLED, and `verify.sh` stays BLOCKING until the cache entry is deleted.
- **Fix:** cache only an ON result, or re-run the check on every configure (about 1 s).

**10. minor · `cmake/FcmpDeps.cmake:68-72` (the FunkGui check at :234): the SHA check never peels an annotated tag**
- **What's wrong:** FunkGui's tags are annotated (v0.0.1's tag object is `caa2ac2…`, its commit `99ef118…`), and 03 §4.8 step 3 says to pin "<tag sha>".
- **Failure scenario:** `git rev-parse v0.1.0` returns the tag object's SHA. If the lead pins that, every configure FATALs with "at <commit>, the pin is <tag object>".
- **Fix:** compare HEAD with `git rev-parse -q --verify ${sha}^{commit}`, or FATAL unless the pin is a commit.

**11. minor · `Scripts/validate.sh:102-127`: the stamp is written without checking which bundles were validated**
- **Failure scenario:** 03 §4.8 runs validation (step 7) before the install (step 9). The installed bundles are last sprint's, but "validate: pass" is appended to `build-lead/verify-passed-<new sha>`.
- **Fix:** refuse unless the installed `Contents/MacOS` binaries match the ones under `<build>/FCompressor_artefacts/<cfg>/`.

**12. minor · `Scripts/verify.sh:241-251`: the pass stamp can name a commit the binaries weren't built from**
- **What's wrong:** `verify.sh` never builds; it stamps the current HEAD after running `ctest` on whatever binaries exist.
- **Failure scenario:** after a merge or commit with no rebuild, `verify-passed-<new HEAD>` certifies stale binaries, and `release.sh` trusts it.
- **Fix:** record the SHA at build time (a custom command writing `built-from.txt`) and refuse to stamp on a mismatch, or build first.

**13. minor · `cmake/FcmpDeps.cmake:104`: the bgfx declaration lacks `EXCLUDE_FROM_ALL` (and `SYSTEM`)**
- **What's wrong:** FCompressor declares bgfx first, so its declaration wins over FunkGui's `SYSTEM EXCLUDE_FROM_ALL` one.
- **Failure scenario:** every GPU `all` build (lead, owner, agent-gui) also compiles `bimg_encode` and `bimg_decode` and their bundled third-party code (astc, nvtt, squish and others), which cost build time and disk. I confirmed they are unconditional in bgfx.cmake.
- **Fix:** `FetchContent_Declare(bgfx … GIT_SHALLOW TRUE SYSTEM EXCLUDE_FROM_ALL)`.

**Checked and found OK**
- **PRIVATE-link check:** it does not trip on a real PRIVATE link. I simulated it: `INTERFACE_LINK_LIBRARIES` holds `$<LINK_ONLY:FunkGui::core>`, which the check exempts.
- **FunkGui v0.1.0:** the target and function names, the option names and harness-only mode are all compatible, apart from finding 2.
- **Globs:** they pick up `modes/<key>/*.cpp` and new probe files. Uncommenting a `Modes.def` line reconfigures and registers the per-Mode tests.
- **Lints on main:** `lint.deps` reports 0 violations and fails when I inject a violation. `lint.headers` passes all 29 of F0's headers.
- **`ownership.py`:** the manifest parsing and the `*`, `**` and `{a,b}` glob behaviour are correct.
- **Stub processor:** it is realtime-safe, and its bus layouts match 03 §3.5.
- **Flags:** the `SHELL:-Xarch_` pairs and LTO-in-Release-only are correct; `-ffp-contract=off` reaches fcdsp, the plugin and the probes.
- **Tolerance table:** the order in `Tolerances.h` matches `fcdsp::Rigor`.

## R-G1 — FunkGui build, Harness v2, golden.py (fixed by FX-C; item 1's wrapper side by FX-B)

1. **major** · FCompressor `Scripts/golden.py:41-42` and FunkGui `tools/golden.py:798-819`
   - **Problem:** The wrapper adds `--allow-env FCMP_ALLOW_BLESS` to every subcommand, but only the `adopt` subparser defines `--allow-env`.
   - **Failure:** `Scripts/golden.py report build-lead` and `Scripts/golden.py diff build-lead` always exit 2 with "unrecognized arguments: --allow-env FCMP_ALLOW_BLESS". I ran FunkGui's golden.py with the exact argv the wrapper builds and got this error for both. SPRINTS D8 makes `report` a lead tool, so the lead's report and diff never work through the wrapper. `adopt` does work.
   - **Fix:** In golden.py `common()`, add `p.add_argument("--allow-env", default="FUNKGUI_ALLOW_BLESS")` so report and diff accept and ignore it (and remove it from the adopt parser). Alternatively, have the wrapper add `--allow-env` only when `argv[0] == "adopt"`.

2. **major** · `cmake/FunkGuiTargets.cmake:244-248`
   - **Problem:** `FunkGuiFonts` is built by `juce_add_binary_data`, which is a STATIC library with default visibility. JUCE hides only its own plugin and shared-code targets, and FunkGui hides only bgfx, bx and bimg.
   - **Failure:** Every FCompressor .component, .vst3 and Standalone exports `funkguifonts::JetBrainsMonoRegularsubset_ttf`, `getNamedResource` and the other generated symbols. release.sh's `nm -gU` check (03 §5, K2 #26f) then fails. I confirmed this with a scratch bundle: `-fvisibility=hidden` plugin code linked to a default-visibility archive exports its `D`/`T` symbols. The fix is in a CMake file that is frozen after S0.
   - **Fix:** After `juce_add_binary_data`, add `set_target_properties(FunkGuiFonts PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)`.

3. **major** · `include/funkgui/test/Harness.h:759` (flag parse) and `:1158-1180` (candidate write)
   - **Problem:** Nothing stops `--bless-to` from being equal to, or inside, `--golden-root`. Candidates go to `<bless-to>/<arch>/<scope>/<probe>.txt`, which is exactly the arm64 overlay path.
   - **Failure:** Running `fcmp_probe_dsp dsp.static --mode clean --golden-root tests/golden --arch arm64 --bless-to tests/golden` writes the measured rows as the arm64 overlay, and the next run passes. In a scratch run the first run exited 2 (drift) and the second exited 0. This breaks 03 §3.2.5's "the probes cannot bless at all"; only [OWN] and review would catch it.
   - **Fix:** In the Probe constructor, compare `weakly_canonical` of both paths and call `harnessError("--bless-to must be outside --golden-root")` when blessTo equals root or is under it (and vice versa).

4. **minor** · `Harness.h:1177-1191`; SPRINTS §3.2 step 2
   - **Problem:** The harness deletes a stale `<probe>.diff` but never deletes stale `<probe>.<key>.lines` candidates for keys the run no longer produces. FunkGui's bless path (the `lead-verify` workflow, then `golden.py adopt build-lead`) never wipes `golden-candidates/` or `probe-results/`; only tools/verify.sh does.
   - **Failure:** golden.py `discover()` attributes the stale sidecar to its probe, and `plan_sidecars` writes it into base. Every later run then reports it as a MISSING lines row, so the probe drifts forever. Candidates and results of removed or renamed probes in the persistent build-lead are also adoptable.
   - **Fix:** When writing candidates, remove `<dir>/<probe>.*.lines` files whose key is not in `lines_`. Also make SPRINTS §3.2 step 2 run `tools/verify.sh build-lead` (which wipes) before `adopt`.

5. **minor** · `tools/golden.py:549-590`
   - **Problem:** The provisional refusal fails open. When no results JSON in the build has a `provisional` member (dsp.registry crashed, was filtered out, or wrote nothing), the provisional set is empty. Separately, adopt accepts any status not in BLOCKING, including the `unreadable` status from `load_results`.
   - **Failure:** If dsp.registry crashes, `adopt --only 'modes/bus-g/*'` blesses a provisional Mode (K3 #9).
   - **Fix:**
     - If any selected candidate is mode-scoped, refuse unless some results JSON has `"provisional"` as a list.
     - Require dsp.registry to always `note("provisional", "[]")`, even when there are none (a contract to pin at FZ0).
     - Accept only statuses in `{pass, golden_drift, golden_missing}`.

6. **minor** · `Harness.h:846` and `:1086`
   - **Problem:** A golden row stores `fmtNum(v)` ("%.9g"), but the comparison uses the unrounded `r.num` against the rounded `g.num`.
   - **Failure:** A row with `le:0`, `ge:0`, `abs:0` or `rel:0` on a non-integer value drifts against its own freshly blessed candidate. In a scratch run, `num("x", 0.1f, Tol::le(0))` was blessed and then re-run and printed "DRIFT golden 0.100000001 got 0.100000001", exit 2.
   - **Fix:** In `num()`, store the numeric value as `std::strtod(fmtNum(v).c_str(), nullptr)`, so the harness compares exactly what the file holds (golden.py already does).

7. **minor** · `Harness.h:837-846`; `tools/golden.py read_rows`
   - **Problem:** `num()` accepts NaN and ±Inf.
   - **Failure:** A NaN measurement blessed as `nan<TAB>exact` passes forever; a scratch run exited 0. With a numeric tolerance the same row drifts forever instead.
   - **Fix:** In `num()`, `if (!std::isfinite(v)) harnessError("num '<key>': non-finite value")`. golden.py should refuse nan and inf values.

8. **minor** · `tools/golden.py:560`
   - **Problem:** `--allow-env` accepts any variable name, so the guard passes whenever that variable equals "1".
   - **Failure:** `golden.py adopt … --allow-env SHLVL` passes in most shells; `CLICOLOR` does too on many Macs. Together with `--golden-root <main checkout>/tests/golden`, a caller in a linked worktree gets past all three refusals using only CLI arguments.
   - **Fix:** Refuse unless the name matches `^[A-Z][A-Z0-9_]*_ALLOW_BLESS$`, or is one of `{FCMP_ALLOW_BLESS, FUNKGUI_ALLOW_BLESS}`.

9. **minor** · `test/smoke/shader_hash.cpp:31-34`; `cmake/FunkGuiDeps.cmake:16`
   - **Problem:** 03 §2.5 says "a shaderc built from the wrong bgfx fails a test", but both shader hashes are golden rows. A mismatch is golden_drift, which passes CTest and verify.sh lists as a DRIFT candidate. A user-supplied `FUNKGUI_SHADERC` gets only an EXISTS check, no stamp check.
   - **Failure:** With `-DFUNKGUI_SHADERC=<old shaderc>`, the gate stays green with one DRIFT line that looks like a shader edit.
   - **Fix:** Require a `shaderc.stamp` next to any `FUNKGUI_SHADERC` whose first line is `bgfx.cmake ${FUNKGUI_BGFX_SHA}`, FATAL in top-level mode. Also pass the stamp line as a define so fg.shader.hash asserts it with a spec row.

10. **minor** · `tools/golden.py:624` (selftest); `test/` has no registration
    - **Problem:** `golden.py selftest` is the only test of adopt's refusals, the x86 merge rules and report classification, and no gate runs it. It currently passes when run by hand.
    - **Fix:** Add a stub such as `test/smoke/golden_py.cpp` with the first line `// FUNKGUI_TEST name=fg.golden.self timeout=300 gpu=0 exe=tools/golden.py args="selftest"`.

11. **minor** · `test/smoke/smoke_gpu.mm:38-44`
    - **Problem:** The test asserts the static class names through `objc_getClass(FUNKGUI_OBJC_PREFIX_STR "RenderView")`, the very names G7 must replace with randomised runtime names (02 §1.8, K2 #16).
    - **Failure:** G7's correct change spec-fails fg.smoke.gpu, and G7 cannot fix it because test/smoke/** belongs to G1.
    - **Fix:** Assert that `NSStringFromClass([v class])` has the prefix `FUNKGUI_OBJC_PREFIX_STR "RenderView"`, using the view `createRenderView` returns, and drop the `objc_getClass` rows. Otherwise, put test/smoke/smoke_gpu.mm in G7's OWNS.

12. **minor** · `CMakePresets.json:51-52, 58-59` (committed HEAD)
    - **Problem:** The agent and agent-gui build presets pass `nativeToolOptions ["-l","12"]`. With the load average of 500–800 recorded in s0.md, ninja then runs one job at a time.
    - **Failure:** Every FunkGui agent build is effectively serial. The fix (`jobs: 6`, no `-l`) exists only as an uncommitted change in the main checkout, and this file freezes at FZ0.
    - **Fix:** Commit `"jobs": 6, "nativeToolOptions": []` for both build presets, and `"jobs": 6` in the test preset, before tagging v0.1.0.

I edited no file in either repository; the only local change, to FunkGui `CMakePresets.json`, was already there. My scratch compiles went in `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/gp` and are deleted. I did not build the FCompressor consumer against v0.1.0, so the consumer build path is checked by reading the CMake only. I found no blockers.

Files:
- `/Users/seanfunk/audio/libraries/FunkGui/include/funkgui/test/Harness.h`
- `/Users/seanfunk/audio/libraries/FunkGui/tools/golden.py`
- `/Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiTargets.cmake`
- `/Users/seanfunk/audio/libraries/FunkGui/cmake/FunkGuiDeps.cmake`
- `/Users/seanfunk/audio/libraries/FunkGui/test/smoke/shader_hash.cpp`
- `/Users/seanfunk/audio/libraries/FunkGui/test/smoke/smoke_gpu.mm`
- `/Users/seanfunk/audio/libraries/FunkGui/CMakePresets.json`
- `/Users/seanfunk/audio/plugins/FCompressor/Scripts/golden.py`
- `/Users/seanfunk/audio/plugins/FCompressor/docs/SPRINTS.md`

