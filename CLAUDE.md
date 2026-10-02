# FCompressor — rules for every Claude session

All-in-one compressor plugin (AU/VST3/Standalone, macOS arm64; VST3/Standalone on Linux x86-64 with Clang, ADR-92)
built on JUCE 8.0.4 and the FunkGui library. The design is final: `docs/ARCHITECTURE.md` (overview),
`docs/DECISIONS.md` (ADR-nn, Qn), `docs/SPRINTS.md` (the plan: cards, ownership, commands), and the binding appendices
in `docs/design/`: `01-core-contracts.md` (contracts), `02-funkgui-and-ui.md` (UI), `03-build-verify-process.md`
(build, verify, process). Appendices beat ARCHITECTURE; SPRINTS beats 03 §4.9 on scheduling and ownership only.

## Who you are

- **Lead**: the user's main session. Only the lead commits, merges, tags, blesses goldens, bumps the FunkGui pin,
  installs, pushes, opens and merges GitHub PRs (`Snipet/FCompressor`, `Snipet/FunkGui`), and touches the main checkouts (`/Users/seanfunk/audio/plugins/FCompressor`,
  `/Users/seanfunk/audio/libraries/FunkGui`).
- **Agent**: one card of `docs/sprints/s<N>.md` (your manifest: TASK, OWNS, FROZEN, FUNKGUI, DONE). At most 6 agents
  run at once across both repositories (the user raised it from 3 on 2026-10-01). Read your manifest and the sections
  it cites before writing anything.

## Never (agents)

- `git commit` on any branch but your own; `push`, `rebase`, `worktree prune`, `gc`, or change any ref other than your
  own branch. When the lead's prompt asks for it, finish with ONE handoff commit on your own branch (`s<N>/<code>`) so
  the worktree cannot be cleaned up with your work in it; the lead reviews it before anything reaches `main`.
- Bless (`golden.py adopt`), install (`owner` preset, `~/Library/Audio/Plug-Ins`), or use the network.
- Touch `/Users/seanfunk/audio/plugins/HardwareReverb` (read-only: never edited, never built, never referenced by CMake)
  or write into `~/audio/.deps` (read-only machine cache, written only by `Scripts/deps.sh`).
- Build in the main checkout, in another agent's worktree, or anywhere in HardwareReverb. Edit FunkGui from an
  FCompressor worktree. Point an override at another agent's live FunkGui worktree.

## Ownership (03 §4.6; checked by `Scripts/sprint/ownership.py docs/sprints/s<N>.md#<ID> <worktree>`)

- Edit only paths matched by your manifest's `OWNS` globs. Any other changed path fails the definition of done.
- `FROZEN` files: never edit. Propose changes as an interface-change request in the handoff. A card may add bodies to a
  frozen header it OWNS, never rename, remove or change a frozen declaration (SPRINTS §0.3).
- Lead-only: `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `.gitignore`, `CLAUDE.md`, `README.md`,
  `LICENSE`, `docs/**`, `Resources/**`, `tests/golden/**`, `tests/fixtures/**` (write-once) — except where a Sprint-0
  card's OWNS names them. Exception: `docs/modes/<key>.md` belongs to that Mode's card.
- `Source/fcdsp/modes/Modes.def`: lead or the sprint's descriptor-wave card only. A slot is permanent once its line
  appears; a lone new-Mode card only uncomments its own reserved line. Mode DSP cards never edit it.
- Golden files are never written by probes: candidates go to `build-*/golden-candidates/`; the lead blesses.

## Layer rules (01 §2.2; enforced by `lint.deps` and the target graph)

- `Source/fcdsp/**` is JUCE-free: no `<juce_*>`, no `funkgui/*`, nothing from `plugin/` or `editor/`. std and SIMD
  intrinsics only.
- **No libm on the audio path**: under `Source/fcdsp/{core,engine,modes}` the lint rejects
  `(^|[^A-Za-z0-9_])((std::)?(a?(sin|cos|tan)h?f?|atan2f?|exp(2|m1)?f?|log(2|10|1p)?f?|powf?|cbrtf?|hypotf?|erfc?f?|[lt]gammaf?))[ \t]*\(`; use
  `fcdsp::log2/exp2/tanh/logCosh/tanPi/sinPi/cosPi` instead (`params/` and `analysis/` are exempt).
- No function-local statics, lazily initialised globals or static constructors in `fcdsp`. The registry and the
  descriptors are constant-initialised. `FCMP_TEST_TAP` never appears: the test tap is a runtime pointer.
- `plugin/` never includes `editor/` (sole exception: `CreateEditorGpu.cpp`). `editor/` reaches the processor **only**
  through `Source/plugin/ProcessorFacade.h`, never `fcdsp::EngineHost`. Product constants come from the generated
  `FcmpProduct.h`, never `JucePlugin_*`.
- Floating point: `-ffp-contract=off`, never `-ffast-math`. No warnings: our sources build with `-Werror`.
- Sources are per-directory globs rooted at `Source/` and `Tools/`; probes self-register from their first line. Adding
  a Mode, policy, view or probe edits no CMake and no shared file.

## Real-time rules (ARCHITECTURE §7)

- The audio thread never allocates, locks, makes a syscall, logs, writes a parameter, or calls `setLatencySamples`,
  `updateHostDisplay` or `triggerAsyncUpdate`. `EngineHost::configure` (from `prepareToPlay`/`SetupWatcher`) is the
  only allocation point.
- No parameter listeners and no `AsyncUpdater` in the processor; the 20 Hz message-thread `SetupWatcher` applies
  `quality`/`labudget` and announces Mode changes.
- Multi-parameter writes (state load, preset apply, UI) are bracketed by `beginBatch()/endBatch()`.
- Telemetry is lock-free: `UiFrame` seqlock, `HistoryRing` SPSC with a claim word, both gated by the attach count.
- `ScopedFtz` in `EngineHost::process` and every analysis entry point; inputs sanitised before any delay line;
  block-size invariant; nothing steps (every gain change ramps, 20 ms).

## Worktree, builds, presets (03 §4.3, §2.10; commands tagged in SPRINTS §0.2)

- Learn your worktree once (`git rev-parse --show-toplevel`), write it literally as `$WT` into every command, and start
  every command with `cd "$WT" &&` (shell state resets between calls). Rename the branch: `git branch -m s<N>/<code>`.
- Build directories are `$WT/build-<preset>` only. Presets for agents:
  - `dsp` — DSP-only, no JUCE, configure ≈ 2 s: the `fcdsp` inner loop. `[DSP]` = `cmake --workflow --preset
    dsp-verify`.
  - `agent` — headless, RelWithDebInfo: the default and the DoD build. `[AGENT]` = `cmake --workflow --preset
    agent-verify && Scripts/verify.sh "$WT/build-agent"`. Probes only: `cmake --build --preset agent-probes`.
  - `agent-gui` — GPU editor work. `[GPU]` = `cmake --workflow --preset agent-gui-verify`.
  - `owner`, `lead`, `lead-x86`, `release`, `universal`, `asan`, `tsan`, `tsan-agent`, `rtsan`: lead only.
- One card's tests: `ctest --preset agent -L 'mode:bus-g'` or `-L 'probe:dsp\.(simd|units)'`. Build presets cap
  parallelism at `-j6` (no load limit; it serialised builds under load); do not raise it. Never run `fcmp_bench` while others build.
- FunkGui is consumed only as a **tag** (the pin in `cmake/FcmpDeps.cmake`). An override
  (`-DFETCHCONTENT_SOURCE_DIR_FUNKGUI=…`) is allowed only to your own FunkGui worktree or a lead-made
  `FunkGui.wt/pin-<sha7>`, must descend from the pin, and must be declared in the handoff.
- Sprint 0 only: there is no CMake on `main` until B0 merges; follow the commands in your manifest.

## Verify and done (03 §3.2.4, §4.7; SPRINTS §0.1 rule 7)

- `Scripts/verify.sh "$WT/build-agent"` is the gate. Probe exit codes: 0 pass, 1 spec fail (blocking), 2 golden
  drift, 3 golden missing (candidates, need a reason), 4 harness error (blocking). `--quick` is refused.
- Done means: `ownership.py` clean; the build succeeds with no warnings; frozen headers you touched pass
  `lint.headers`; `verify.sh` exits 0 (0 spec failures, 0 harness errors, 0 crashes/timeouts, 0 disabled tests);
  every `golden_drift`/`golden_missing` key group has a one-line reason; UI cards save PNGs of each changed view × one
  stepped and one continuous Mode (`[PNG]`); FunkGui API/rendering changes also pass FCompressor's `verify.sh` in the
  lead-made `ro-` worktree (`[XV]`); **plus the card's Acceptance**.

## Handoff (your final message, ≤ 40 lines, in this order)

1. Task id, worktree path(s), branch(es).
2. `git status --short`.
3. `verify.sh` summary: counts per status and the 5 slowest tests.
4. Candidates: per key group, a one-line reason.
5. Interface-change requests (frozen files you need changed).
6. The FunkGui override and SHA, if any.
7. PNG paths.
8. Known gaps.
