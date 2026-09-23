# K2: Risk review of drafts 01, 02 and 03 (realtime, DSP correctness, host integration)

Status: critique for the synthesiser. Written 2026-09-22. It does not edit the drafts.

What this review covers:
- audio-thread allocation, locks and syscalls;
- seqlock and ring races, torn reads, editor-lifetime gating;
- automation semantics across Mode changes;
- parameter-count stability and state migration;
- latency and plugin delay compensation (PDC);
- stability of the feedback solver and correctness of the Mode crossfade;
- float determinism against the golden strategy;
- oversampling and mix alignment;
- bgfx and ObjC across binaries;
- FetchContent pitfalls;
- AU and VST3 validation.

Citations:
- `01 §x`, `02 §x` and `03 §x` are the drafts. `A`–`F §x` are the research reports.
- `JUCE <file>:<lines>` is JUCE 8.0.4 as checked out read-only in HR's `build/_deps/juce-src/modules`. That checkout is 8.0.4 per `juce_StandardHeader.h:42-44`. It was read for evidence only. The same files will be in `~/audio/.deps/JUCE-8.0.4` once `deps.sh` has run (03 §1.3).

Severity:
- **blocker**: must change before the sprint-frozen interfaces freeze.
- **major**: wrong audio, a host-visible defect or a broken gate if left.
- **minor**: fix while implementing.

---

## Blockers

### 1. [blocker] The feedback solver interface cannot express the ballistics that the first eight Modes use in FB

**Where.**
- 01 §5.2: `GainComputerPolicy::solveFb(c, x, r1, alpha)`, and `BallisticsPolicy::alphaFor` / `commit`.
- 01 §5.3: the FB bullet.
- 01 §10.7: Mu 67 is "`ProgressiveKnee` inside `FeedbackZdf`, with `TcSelector` ballistics", with TC5/TC6 program-dependent. Diode 609 has A1/A2 dual release, and its `topo` is FB.
- E §2.6 and E §2.7: "TC5 and TC6 use `MultiStage`".

**What is wrong.** E's closed form solves `r = α·r1 + (1−α)·r̂(x − r)`. That form needs **one** one-pole, and `alphaFor` returns one α. The following ballistics have no single α:
- `DualRelease`: `r_s` follows `r_f`, and the output is `max`.
- `MultiStage3`: TC5/TC6.
- `Hold<>`.

An agent would have two choices:
- Run these ballistics explicitly after a solve. That reintroduces the one-sample-delay loop, which E §2.6 measured as unstable above 3.09:1 at a 20 µs attack.
- Improvise a per-Mode solver, which is outside the frozen contract.

Mu 67 and Diode 609 are both in the first eight Modes.

**Fix.** Freeze an affine form instead of α. Every linear ballistic path, given its state, maps r̂ affinely to r:

```cpp
namespace fcdsp {
struct FbAffine { simd::f32x4 A, B; };          // r = A + B·r̂_fb(x − r),  A ≥ 0,  0 ≤ B ≤ 1
template <class G> concept GainComputerPolicy = Designable<G> && requires (const typename G::Coeffs& c, simd::f32x4 x, FbAffine a) {
    { G::target(c, x) }     noexcept -> std::same_as<simd::f32x4>;
    { G::solveFb(c, x, a) } noexcept -> std::same_as<simd::f32x4>;   // unique root (see #5); a = {0, 1} → static FB curve
};
template <class B> concept BallisticsPolicy = Designable<B> && requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 v) {
    { B::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;                          // FF
    // FB: the ballistics own the solve. solve(FbAffine) -> r is supplied by ModeEngine (wraps G::solveFb).
    { B::stepFb(c, s, [](FbAffine) noexcept { return simd::f32x4{}; }) } noexcept -> std::same_as<simd::f32x4>;
    { B::seed(s, v) } noexcept;
    { B::releaseNowMs(c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
};
}
```

The closed forms for the quadratic and hard knee are E's, with `α·r1 → A` and `(1−α) → B`:
- linear region: `r = (A + B·k·(x−T)) / (1 + B·k)`;
- knee region: `c = b − A`, `κ = B·k/(2W)`;
- below the knee: `r = A`.

The ballistics map onto the form as follows:
- `SmoothBranching`: `{α·r1, 1−α}`, with α chosen by E's predictor.
- `DualRelease`:
  - fast path `{α_f·rf1, 1−α_f}`;
  - slow path `{α_s·rs1 + (1−α_s)·α_f·rf1, (1−α_s)(1−α_f)}`;
  - `r = max(solve(fast), solve(slow))`.
- `Hold` during hold: `{r1, 0}` on the release branch.
- The rest of `stepFb` updates the internal states from the solved r.

**Why max-of-roots is exact.** Let `g_i(r) = A_i + B_i·r̂(x−r)`, which is non-increasing in r. If `r1* ≥ r2*`, then `g2(r1*) ≤ g2(r2*) = r2* ≤ r1*`. So `r1*` solves `r = max(g1, g2)`.

**Probe.** Add a `dsp.static.<key>` spec row: every FB Mode's solved r agrees with 200-step bisection to 1e-5 dB, for every ballistics branch. This is E §2.6's check, applied per Mode.

### 2. [blocker] The test tap is a compile flag in 01 and a runtime pointer in 03

**Where.**
- 01 §5.4: `#if FCMP_TEST_TAP void setTap(TestTap*)`.
- 03 §0.5, §3.4 (second note, "The tap is `ControlIo::tap`, a runtime pointer") and §3.10 #4.
- 01 §5.3 `ControlIo`, which has no `tap` member.

**What is wrong.**
- A flag that adds a member to `EngineHost` changes the class layout. The plugin and the probes link one `fcdsp` archive, so the flag must be identical in both. If it is not, that is an ODR violation. If it is, the plugin carries the tap anyway.
- Either way the drafts contradict each other on a frozen public API.
- 03 references a `ControlIo::tap` field that 01 does not have.

**Fix.**
- Make `void setTap(TestTap*) noexcept;` unconditional.
- Define `struct TestTap { std::span<simd::f32x4> grDb, detDb, tgtDb, s2GrDb; std::span<uint8_t> phase; uint64_t firstSample; };`.
- The host holds `std::atomic<TestTap*>`, loads it once per block, and forwards its spans through the existing optional pointers of `ControlIo`. `ControlIo` gains no new member.
- The plugin never calls `setTap`.
- A lint greps for `FCMP_TEST_TAP` and expects 0 hits.

### 3. [blocker] The Mode and kernel crossfade is underspecified, and preGain reaches the dry path

**Where.**
- 01 §5.3 `Carry`.
- 01 §5.4 steps 2a, 2d, 2e and 2f.
- 01 §5.5.
- 01 §5.1: the host `Smoother4 {preGainDb, makeupTotalDb, mix, spare}`.
- 01 §10.5: FET's `preGainDb = kT0 − thr`.

**What is wrong.**
- **(a) The outgoing engine's parameters are undefined.** `BlockParams` carries only `resolve(newSlot)`. The meaning of `EngineParams::m[8]` depends on the Mode. Feeding the new Mode's values to the old engine for 20 ms is garbage. Not feeding it anything is also unstated.
- **(b) Per-Mode host quantities are shared across the two paths.** `preGainDb` (FET INPUT, up to ±24 dB), `makeupTotalDb` and `driveDb` differ per Mode. `makeupTotalDb` includes each engine's own `autoMakeupDb()`. During the fade the old path is driven by the new Mode's gain staging, so `level_dev_db` (C §5.5) moves for the wrong reason.
- **(c) preGain reaches the dry path.** Step 2a applies preGain to "main" before the paths. Step 2f mixes against `up(delayed main)`. So at mix < 1 the dry signal carries the FET INPUT gain, which is not what parallel compression means, and the mix = 0 null fails.
- **(d) Carry lanes can be in the wrong domain.** `stmode` is part of the kernel key, so an ST↔M/S change seeds L/R lanes with M/S GR, or the reverse.
- **(e) The upsampler may run twice.** "Upsample the shared main" is listed inside the per-engine loop. Calling one stateful `Oversampler::up()` twice per chunk corrupts it.

**Fix. Write this into 01 §5.4/§5.5:**

```cpp
struct PathState {                 // one per arena slot, owned by EngineHost
    EngineParams eng;              // incoming path: updated every block; outgoing path: FROZEN at its last value
    Smoother4    gain;             // {preGainDb, makeupTotalDb (= makeup + this engine's autoMakeupDb()), driveDb, spare}
};
struct Carry { simd::f32x4 grDb, detDb, s2GrDb; float relNowMs[2]; uint8_t msDomain; uint8_t pad[3]; uint32_t valid; };
```

Rules:
- `up()` runs **once** per chunk into `osMain`, and one `down()` runs after the blend and the mix.
- preGain is folded into each path's gain: `g = linFromDb(preGainDb − gr)`, interpolated at the OS rate. The internal SC gets each path's preGain at base rate before encoding.
- **The dry path is never pre-gained.**
- `mix` stays host-level, using the incoming Mode's resolved value, because it is applied after the blend.
- `seed()` rule: if `carry.msDomain` differs from the new engine's domain, every lane of `grDb`, `detDb` and `s2GrDb` takes `max(lane0, lane1)` of the old engine. This never under-compresses.
- `dsp.switch` gains three pairs:
  - an `stmode` flip within Clean (ST → M/S);
  - FET 76 ↔ Clean at mix 0.5;
  - an auto-makeup Mode ↔ a manual one.

---

## Major

### 4. [major] Snap-on-read turns FET 76 off after a switch from almost any Mode; GR OFF also steps

**Where.**
- 01 §10.5: `kAtkOff = {1.0f, "OFF", …, kTagOff}` and `hybrid(0.02f, 0.8f, kAtkOff, 0.2f)`.
- 01 §4.4: the `snap()` rule for hybrid.
- 01 §10.3: Clean `atk` defaults to 10 ms.
- 01 §10.4: Bus G `atk` defaults to 10 ms.
- `fetPhysical` sets `kEngGrOff`.

**What is wrong.**
- In the log domain the boundary between `hi = 0.8` and OFF at 1.0 falls at √0.8 = 0.894 ms.
- **Every raw attack above 0.894 ms resolves to OFF.** That includes the defaults of Clean, Bus G, Bus 25 and Diode 609, and Opto's raw value. So selecting FET 76 from any of them yields a "compressor" that does no gain reduction.
- D4 (03 §3.4 `dsp.switch`) cannot catch this, because its CB control has the same raw value.
- Separately, `kEngGrOff` toggles at a control tick, and GR jumps to 0. An automation lane crossing 0.894 ms clicks.

**Fix.**
- **(i) Take OFF out of `atk`.** Use `atk = hwrev(cont(0.02f, 0.8f, 0.2f))`, so a raw value above 0.8 clamps to the slowest attack, which is OFF's hardware neighbour.
- Move OFF to FET's `tmode`, which is n/a today, as `stepped({{0,"ON","ATTACK ON"},{7,"OFF","ATTACK OFF (NO GAIN REDUCTION)",kTagOff}}, 0)`. It is labelled `ATTACK`, and the boundary is at 3.5. Every other Mode writes `tmode` ∈ {0, 1}.
- **(ii) Add a registry lint, `dsp.registry` row `crossmode.no_off`.** For every ordered pair (A, B), take `modeDefaults(A)` applied over the host defaults, resolve it in B, and require:
  - no `kTagOff` and no `kEngGrOff`;
  - `staticGr(T + 12 dB) > 0` for every Mode that is not a limiter-only Mode.
- **(iii) `kEngGrOff` never steps.** The target GR is multiplied by a 20 ms linear ramp, `offAmt`, advanced per sample.
- **(iv) `dsp.zipper.<key>` gets "detent-edge" rows.** For each stepped or hybrid parameter, step between each pair of adjacent detents at t = 1 s, and require `hf_ratio_db ≤ +3`. This covers AUTO, ALL, OFF, TC and Stage-2 OFF tag flips, none of which D5 covers today, because D5 covers only continuous parameters (C §5.6).

### 5. [major] Feedback-loop uniqueness and linking are not guaranteed

**Where.**
- 01 §5.3, FB bullets.
- 01 §10.5: "ALL: custom curve … from `kTagAll`".
- E §2.7: ALL is "a custom curve (a negative-slope region)".
- E §2.6: `F′(r) = 1 + (1−α)·r̂_fb′ ≥ 1`.
- 01 §5.2 `LinkPolicy`: lanes 0–1, applied to targets.
- 01 §10.2 link column: FET LINK, Opto LINK, Mu 67 LINK, Diode STEREO, and Bus 25 OLD (FB) at 50–90 %.
- E §8: "applied to the gain-computer targets before ballistics".

**What is wrong.**
- **(a) Uniqueness needs a monotone curve.** It holds only if `r̂_fb′(y) ≥ 0`. A negative-slope transfer in FB needs `r̂′ < −1`. Then `F′` can be ≤ 0, the loop can have 0 or 2 equilibria, and E's "two fixed Newton steps" can jump between them.
- **(b) FB has no "target before ballistics" point.** The link concept therefore has no defined place in the fused loop. Partial link (Bus 25 OLD) is a coupled 2-D implicit system with no closed form.
- **(c) The opto guard cannot be a `static_assert`.** The guard is `k ≤ α/(1−α)`, but α depends on fs at runtime, and the opto loop gain depends on level.

**Fix.**
- **(a)** Rule plus registry row `fb.monotone`: for every FB Mode and every step, `r̂_fb` is non-decreasing on y ∈ [−80, +40] dB, sampled every 0.1 dB. ALL is realised with a larger k, a threshold shift, and attack and colour changes, never with a negative `r̂′`.
- **(b)** In FB kernels, link is applied **after the per-lane solve and before `commit`**: `r_ch ← r_ch + k·(combine(r) − r_ch)`. The linked value becomes the next state.
  - `max` and `mean` are non-expansive, so each lane stays a contraction.
  - Static curves use the per-lane solve.
  - `dsp.link.<key>` adds an FB steady-state row.
- **(c)** Remove the `static_assert` wording. `prepare()` computes the bound at the actual fs. If it is violated, the Mode falls back to `FeedbackZdf`. `dsp.srsweep` adds a 22.05 kHz stability row for Opto 2A.

### 6. [major] Setup and Mode changes on the wrong thread; latency not set in `prepareToPlay`

**Where.**
- 01 §3.1 Rules: "The processor reacts on the message thread: 1. AsyncUpdater …".
- 01 §4.6: "On a Mode change the message thread calls `updateHostDisplay`".
- 01 §5.6: "reported through `setLatencySamples` from the message thread only".

**Evidence.**
- JUCE's VST3 wrapper applies parameter changes **inside `process()`**, through `processParameterChanges` → `setValueAndNotifyIfChanged` → `setValueNotifyingHost` (JUCE `juce_audio_plugin_client_VST3.cpp:3487-3531, 754-761`). APVTS listeners therefore fire on the audio thread.
- JUCE warns that `triggerAsyncUpdate` "may block" on a real-time thread (`juce_AsyncUpdater.h:63-76`).
- `setLatencySamples` calls `updateHostDisplay` (`juce_AudioProcessor.cpp:415-421`).
- Hosts read latency after `prepareToPlay`, which is not guaranteed to run on the message thread.

**Fix.**
- **No parameter listeners in the processor.** It has one message-thread poller:

```cpp
// plugin/SetupWatcher.h: juce::Timer, 20 Hz, owned by the processor, started in the constructor
void timerCallback() override {
    const auto q = quality(), b = budget();
    if (q != cfg_.quality || b != cfg_.budget) {
        cfg_.quality = q; cfg_.budget = b;
        proc_.suspendProcessing(true);                 // takes callbackLock only to set the flag (JUCE AudioProcessor.cpp:583-587)
        engine_.configure(cfg_, proc_.blockParams());  // allocates; audio outputs silence meanwhile
        proc_.setLatencySamples(fcdsp::EngineHost::latencyFor(cfg_));
        proc_.suspendProcessing(false);
    }
    if (const int s = modeSlotRaw(); s != lastNotifiedSlot_) {
        lastNotifiedSlot_ = s;
        proc_.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
    }
}
```

- `prepareToPlay` reads `quality` and `labudget` from the raw atomics, then calls `configure`, then `setLatencySamples`, on whatever thread it is called from.
- A Quality or Lookahead change is documented as "one block of silence plus a PDC change". VST3 hosts restart processing on `kLatencyChanged`.
- `proc.latency.<key>` adds a row: set `quality` from a non-message thread while processing; `getLatencySamples()` is correct within 100 ms; the audio thread makes 0 allocations and takes 0 locks (#18).

### 7. [major] Remove the APVTS UndoManager

**Where.**
- 01 §9.1 ("constructed with an `UndoManager`") and 01 §11 #16.
- 01 §9.2: apply is "one `UndoManager` transaction".
- 02 §8.4 items 3–4.
- 02 §9.5 `beginUndoTransaction`.

**Evidence.**
- The APVTS timer flushes **every** parameter change, host automation included, into the tree with the UndoManager (`juce_AudioProcessorValueTreeState.cpp:120-135, 459-476`).
- `UndoManager::perform` appends to the current transaction until `beginNewTransaction`. It coalesces only with the immediately preceding action (`juce_UndoManager.cpp:120-165`).
- `dropOldTransactionsIfTooLarge` never drops the current transaction and keeps at least 30 (`:202-215`).
- So automating two or more parameters grows one transaction without bound, calls `sendChangeMessage` for every action, and makes "undo" revert host automation.
- No key in 02 §8.9 invokes undo anyway.

**Fix.**
- `apvts(*this, nullptr, "PARAMS", layout())`.
- Delete `beginUndoTransaction` from `ProcessorFacade`.
- Undo belongs to the host: every UI write is already a begin/set/end gesture (02 §5.2), and hosts record those.
- If an in-plugin undo is wanted later, `GestureController` records `(paramId, before, after)` at `endGesture`, capped at 64 entries.

### 8. [major] `HistoryRing` can hand the UI torn columns

**Where.** 01 §6.3 (`push`: "relaxed word stores, then `written_` (release)"; `read`: "re-reading `written()` after the copy").

**What is wrong.**
- While the writer stores column j, `written_` still reads j, and slot `j % 4096` holds column j−4096. A reader that re-checks `written_ == j` accepts column j−4096 while it is half-overwritten.
- The writer's relaxed stores for column j+1 come *after* its release store of `written_`. A reader can observe those stores and still load a stale `written_`, because nothing orders them.
- The result is a column whose `grMaxDb` and `grMinDb` come from different milliseconds: a visible spike in HISTORY and CONTROL PATH.

**Fix.** Use a claim word and seqlock-style fences:

```cpp
void push(const HistoryColumn& c) noexcept {                 // audio thread
    const uint64_t j = written_.load(std::memory_order_relaxed);
    claim_.store(j, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);      // claim is ordered before the data
    /* 8 relaxed word stores into slot j & (kCapacity - 1) */
    written_.store(j + 1, std::memory_order_release);
}
// read(): w = written_.load(acquire); copy [max(from, w − kCapacity + 1), w) with relaxed loads;
//         std::atomic_thread_fence(acquire); c = claim_.load(relaxed);
//         keep column i iff i + kCapacity > c   (any overwrite the reader saw implies claim ≥ its index)
```

`dsp.telemetry` must be able to detect a tear:
- The writer fills every word of a column, and of a `UiFrame`, with a function of its index or `publishCount`.
- The reader asserts consistency over 10⁷ reads, under `tsan` too.

### 9. [major] `Pid` order cannot stay both "APVTS order" and "append-only"

**Where.**
- 01 §3.2: the `Pid` comment "Enum order == APVTS layout order == §3.1 table. Append only".
- 01 §3.1 Rules, first bullet.
- 01 §4.1: `ParamTable::e` is `std::array<ParamEntry, kNumModeParams>`.
- 01 §12.4: `lshape` and `out` as future appends.

**What is wrong.** The Mode-filtered block must be contiguous (`kModeCount`), and the globals follow it. A v2 Mode-filtered parameter therefore cannot be appended without either:
- inserting it before `mode`, which breaks the frozen APVTS order; or
- putting it outside the Mode block, which breaks `ModeDescriptor`.

**Fix.**
- Decouple the two orders:

```cpp
// Pid: sprint-frozen, free to regroup; never persisted, never sent to a host.
inline constexpr std::array<Pid, kNumParams> kApvtsOrder { Pid::thr, …, Pid::labudget /* v2 appends go here */ };  // v1-forever
```

- IDs, not order, are the host identity: VST3 and AU IDs are hashes of the strings (01 §3.1), and state is keyed by ID.
- A new Mode-filtered parameter is added to the `Pid` Mode block and appended to `kApvtsOrder`. `allNa()` gives every existing Mode a neutral entry.
- The registry lint checks that `kApvtsOrder` is a permutation of all `Pid`s.

### 10. [major] Sound-changing edits to a shipped Mode have no state migration

**Where.**
- 01 §0: "Owner-local … [H] constants, Mode `physical()` bodies … Freely".
- 01 §9.1: `stateVersion` is bumped "only when the meaning of a stored plain value changes".
- 01 §12.5.

**What is wrong.** Raw values do not depend on the Mode, so the sound of a saved session is defined by descriptor and policy code. Fitting FET `kT0` or Bus G's dial offset after v1 silently changes every existing session and preset. `stateVersion` cannot express a per-Mode change.

**Fix.**
- Add `uint16_t revision` to `ModeDescriptor`, starting at 1.
- Write `modeRev` into the `<PARAMS>` root and into preset `ATTR`s.
- Rule: once a key is in `tests/fixtures/modes-ever.tsv`, any change that moves its `dsp.print.<key>` default hash requires `revision++` plus an entry in `docs/modes/<key>.md`.
- Loading an older revision raises a footer notice, `FET 76 UPDATED SINCE THIS SESSION (REV 1 → 2)`, through `StateNotice` (02 §9.5).
- The step `plain` values and `DisplayMap`s of shipped Modes join the v1-forever row of 01 §0.

### 11. [major] Oversampler timing: fractional STD latency, and gain applied early to the upsampled audio

**Where.**
- 01 §5.6: `kStdLatency` = "base-rate group delay at 1 kHz, rounded up".
- 01 §5.4 step 2c (SC delay `L_la − look`) and step 2d (`g = linFromDb(−gr_interp)`).
- E §2.9: JUCE max-quality IIR 2× is 3.14 samples, or 4 in its integer-latency variant.

**What is wrong.**
- **(a) Rounding up is not the same as adding delay.** Without a fractional delay, the processed path sits about 0.86 samples earlier than the bypass path (which is delayed by `latencySamples()`) and than PDC. The bypass crossfade and any external parallel dry track comb at HF.
- **(b) The gain leads the audio by the up-filter's delay.** The GR for base sample n is applied to upsampled audio that represents time n − D_up. So the gain acts D_up early: about 1.5 samples at STD and about 30 at HQ. The effective lookahead, and with it attack behaviour and `dsp.time`, then depends on Quality. At HQ a 20 µs FET attack becomes pre-emptive.

**Fix.**
- **STD** = the polyphase IIR followed by a constexpr first-order Thiran allpass at base rate, so the group delay equals `kStdLatency` within 0.01 samples up to 1 kHz. `dsp.os` asserts this and reports τg at 10 kHz.
- **SC delay** = `L_la − look + D_up(quality)`, where D_up is the integer base-rate delay of the up stage (ECO 0, STD 2, HQ per design). This leaves latency unchanged.
- **Gain interpolation** is specified: OS sample F·n + k takes `gr[n−1] + (k/F)·(gr[n] − gr[n−1])`.
- New spec row in `dsp.time.<key>`: the τ measured at ECO, STD and HQ agree within 1.5 base samples.

### 12. [major] The drafts disagree on the mix = 0 null

**Where.**
- 01 §5.6: D8a is redefined as "bit-exact against a control `Oversampler` round trip".
- 03 §3.4 `dsp.null`: "mix 0 is bit-exact against the delayed input".
- 03 §3.5 `proc.null`: bit-exact.
- 03 §3.7: "mix 0, bypass passthrough: bit-exact".

**What is wrong.** At STD and HQ the 03 spec cannot pass, and agents will chase it.

**Fix.** 03's rows become:
- ECO: bit-exact against `delay(x, L)`.
- STD and HQ: bit-exact against `down(up(delay(x, L_la)))` from a control `Oversampler`, plus a passband flatness of ≤ 0.01 dB up to 20 kHz.
- Bypass: bit-exact at every Quality.

### 13. [major] Non-finite input reaches the delay lines and the "dry" fallback

**Where.**
- 01 §5.4 steps 0 and 3.
- 01 §5.8: on poison, output "latency-aligned dry"; reset is "O(1)".
- 03 §3.4 `dsp.hostile`: "Recovery ≤ 1 block", `nonfinite_out = 0`.

**What is wrong.**
- A NaN on the main or key input is written into the lookahead delay, the dry delay and the OS filters.
- It emerges L samples later, possibly several blocks later, and the fallback "dry" *is* that NaN.
- An input of 1e30 squared in `RmsLog` or the meters overflows to inf.

**Fix.**
- At the entry of `EngineHost::process`, before any delay, sanitise main and key per sample:
  - replace NaN and inf with 0 (bit test `(bits & 0x7f800000) != 0x7f800000`);
  - clamp to |x| ≤ 1e6 (+120 dBFS).
- The poison fallback outputs the sanitised, latency-aligned dry signal.
- Reset clears the delay lines, which is O(L) and bounded, and is allowed.
- Meters floor and clamp before publishing.

### 14. [major] libm is still on the audio path, so goldens drift across arches and macOS updates

**Where.**
- 01 §2.2 rule 4 and 01 §5.1: `FastMath` has only `log2`/`exp2`.
- 01 §5.2 colour policies, and E §2.9: ADAA `log cosh = |x| + log1p(e^(−2|x|)) − ln 2`.
- E §8: SVF `g = tan(π·fc/fs)`, recomputed per tick (01 §5.1).
- 01 §3.3: "The host mapping uses libm".
- 03 §2.6: "Identical results come from the FP rules".

**What is wrong.**
- `tanh`, `log1p` and `tan` in colour stages (per sample) and in SC-filter design (per tick) come from Apple libm.
- Apple libm differs between arm64 and x86, and **between macOS releases**. `print.*.hash` and the `xarch.` rows would therefore drift on an OS update.
- `APVTS` plain values are produced by `toPlain`, which calls libm `pow`/`log`, so `proc.*` exact rows are affected too.

**Fix.**
- Add these to `FastMath.h`, written with `fma` only and bit-identical in scalar and vector form:
  - `tanh(f32x4)`, as `1 − 2/(exp2(2x·log2e) + 1)` with a small-|x| polynomial;
  - `logCosh(f32x4)`;
  - `tanPi(float)`, a minimax polynomial for the SVF and the tilt prewarp;
  - `sinPi`/`cosPi` as needed.
- Extend `lint.deps` (01 §2.2) to reject `\b(std::)?(tan|tanh|exp|expf|log|logf|log1p|pow|powf|sin|cos|tanf|tanhf)\s*\(` under `fcdsp/{core,engine,modes}`. `params/` and `analysis/` are exempt.
- Golden rows that depend on host-map values (`proc.*`) use `absrel`, never `exact`.

### 15. [major] The x86 SIMD backend has never been executed

**Where.** 03 §0.11, 03 §3.3, 03 §6 Q1, and 01 §5.1 (`FCDSP_SIMD_SSE`).

**What is wrong.** Rosetta is absent, so no x86 goldens exist. Worse, every SSE code path (`sel`, `blendv` operand order, `cvtt`, `min`/`max` NaN order) ships without ever having run. The universal release would carry an untested DSP backend.

**Fix.**
- Decide now: **v1 ships arm64-only.**
- `release.sh` refuses `FCOMPRESSOR_UNIVERSAL=ON` unless `build-lead-x86/verify-passed-<sha>` exists.
- `lead-x86` stays in the sprint-end build, so the SSE backend at least compiles and does not rot.
- `golden.py adopt` must not describe arm64-only rows as arch-neutral. Mark them `arch: arm64-only` in the adopt log.

### 16. [major] ObjC class names still collide between formats and versions of the same product

**Where.**
- 02 §1.8: `OBJC_PREFIX Fcmp` → `FcmpRenderView` / `FcmpDisplayLinkTarget`.
- 02 §2.3, ObjC row.
- A §6.1.

**What is wrong.**
- The prefix separates products, not binaries. FCompressor AU and FCompressor VST3 in one Live or Reaper process, or an installed build next to a dev build, both define `FcmpRenderView`.
- The runtime reports "implemented in both" and resolves by-name lookups to one image, whose methods call into *that* binary's `FramePump` and `BgfxContext` singletons.
- JUCE avoids this by registering every class at runtime under a randomised name (JUCE `juce_ObjCHelpers_mac.h:307, 365`).

**Fix.**
- `NativeSurface.mm` and `DisplayLink.mm` define `JUCE_CORE_INCLUDE_OBJC_HELPERS 1` (`juce_core.h:376-379`).
- The classes become `juce::ObjCClass<NSView>` / `juce::ObjCClass<NSObject>`, held in function-local statics, with name roots `FUNKGUI_OBJC_PREFIX "RenderView_"` and `"DisplayLinkTarget_"`.
- The macro stays only as a readable root.
- HR's migration goldens are unaffected, because class names never appear in dumps.

### 17. [major] FunkGui's shaderc check breaks the default GPU configure

**Where.**
- 02 §1.4: `if(NOT TARGET shaderc) message(FATAL_ERROR …)`. It sits **outside** the `if(NOT TARGET bgfx)` guard.
- 03 §2.5: with the prebuilt shaderc, `BGFX_BUILD_TOOLS=OFF`, and `FUNKGUI_SHADERC=<path>`.

**What is wrong.** Every GPU configure that has a stamped prebuilt shaderc stops with a FATAL error.

**Fix.**
- The check becomes `if(NOT FUNKGUI_SHADERC AND NOT TARGET shaderc)`. When `FUNKGUI_SHADERC` is set, FunkGui also checks that the path `EXISTS`.
- In the fallback block, FunkGui sets the `BGFX_*` options as normal variables (CMP0077), not `CACHE … FORCE`. A FORCE write into the consumer's cache outlives the block.

### 18. [major] Real-time safety is gated only by an allocation counter

**Where.**
- 03 §3.4 `dsp.rt` (D12, `operator new` counter).
- 03 §2.10: `asan`/`tsan` are DSP-only.

**What is wrong.**
- #6, #7 and #16 are lock, syscall and thread hazards that a `new`/`delete` counter cannot see.
- `malloc` from C code also bypasses the counter.
- TSan never runs the processor, the telemetry readers or the UI probe, which is where the cross-thread code lives.

**Fix.**
- Add a configure-time check. If `-fsanitize=realtime` is supported (RTSan, clang ≥ 20):
  - add an `rtsan` preset;
  - mark `EngineHost::process`, `IEngine::control`/`colour` and `Processor::processBlock` with `[[clang::nonblocking]]`;
  - compile with `-Wfunction-effects`.
- Otherwise, `fcmp_probe_plugin` interposes `malloc`, `free`, `pthread_mutex_lock`, `os_unfair_lock_lock`, `write` and `mach_msg`, and counts calls on the audio thread's id.
- Add `tsan-agent`: the headless configuration, running `proc.*` and `ui.*`, including a scripted editor attach/detach and a message-thread `SetupWatcher`.

### 19. [major] AU and VST3 validation are not gates

**Where.**
- 03 §4.8 step 8 and 03 §5 step 8: `auval -v` only, as a smoke test.
- 03 §4.7 DoD: none.

**Fix.**
- `Scripts/validate.sh <build>` runs at every sprint end and before `release.sh`. It writes into the `verify-passed` stamp.
- Steps:
  1. `killall -9 AudioComponentRegistrar; auval -strict -v aufx Fcmp Funk` against the installed component.
  2. pluginval `--strictness-level 10 --repeat 2 --randomise --timeout-ms 900000` on both the `.vst3` and the `.component`. pluginval is built from a pinned tag by `deps.sh` into `~/audio/.deps/tools`, the same treatment as shaderc.
- Paths it will exercise:
  - Mode fuzzing: the crossfade latch (#3);
  - setup-parameter fuzzing: `SetupWatcher` (#6);
  - state restore into a non-fresh instance;
  - editor open and close ×N: the attach count and ObjC (#16);
  - `getText` from background threads (01 §4.6).
- Decide `AU_SANDBOX_SAFE` explicitly. In sandboxed hosts the preset DB and preferences fall back to in-memory (B §2), and GarageBand is the test.

---

## Minor

### 20. [minor] Sentinels pass through smoothers, and Stage-2 smoothing is unspecified

**Where.** 01 §4.2 (`kRangeOff = kS2Off = 1000`), 01 §5.1 (the smoother lists do not name `s2ThrDb`), and 01 §10.7 (Diode 609 `s2thr` OFF step).

**What is wrong.**
- Smoothing 1000 → −18 dB takes about 7 τ (≈ 140 ms) to reach the target. Stage 2 therefore engages late.
- An unsmoothed OFF → ON switch steps GR.

**Fix.**
- Smooth `range` clamped to 60, where 60 means OFF; the value is never 1000.
- Stage 2 gets `s2On`, a 20 ms linear amount, plus `s2ThrDb` smoothed within [−40, 24] in a second `Smoother4 {s2ThrDb, kneeDb, spare, spare}`.

### 21. [minor] Brickwall correctness

**Where.** 01 §10.7 Brickwall ("true peak … does not affect latency"), 01 §5.4 step 2c, and E §5.4.

**What is wrong, and the fix for each:**
- **(a) The true-peak interpolator delays the SC** by D_tp, about 6 samples.
  - SC delay = `L_la − look + D_up − D_tp`.
  - TP requires `labudget ≥ 5 MS`. Otherwise the UI says overshoot is possible.
- **(b) The ceiling is not guaranteed after downsampling.**
  - Spec: in HQ, the 4×-reconstructed output peak ≤ CEILING + 0.1 dB.
  - In STD, add a final base-rate ADAA safety clip, or document the overshoot.
- **(c) The box average's running float sum drifts over time.**
  - Re-sum exactly every 4096 samples, amortised.
  - `dsp.null` adds a 10-minute soak row: below threshold, GR is exactly 0.0.
- **(d) Automating `look` jumps the SC read position.**
  - Slew the read position by ≤ 1 sample per tick.
  - Change the box length only at ticks, with a resync.

### 22. [minor] Changes that reach the kernel without a crossfade, and kernel crossfade storms

**Where.** 01 §5.3 (`ControlIo::keyExternal`), and 01 §5.4 step 1 (the kernel key).

**What is wrong.**
- Toggling `extkey`, or the host (de)activating the key bus, flips an FB kernel between FB and FF with no crossfade.
- A host lane or MIDI-mapped knob hovering at a `det`/`voice`/`stmode` boundary starts back-to-back 20 ms crossfades.

**Fix.**
- Add the effective `keyExternal` to the kernel key.
- Require a minimum of 2400 samples at 48 kHz between kernel-crossfade starts, counted in samples, so behaviour stays deterministic.

### 23. [minor] Torn parameter sets during state load and multi-parameter writes

**Where.** 01 §9.1 load steps 3–6, 01 §9.2 apply order, 02 §5.2 `tapMany`, and 02 §8.4.4.

**What is wrong.** The audio thread can resolve a block with a mix of the new Mode and old values, or the reverse. That can start an intermediate kernel crossfade, or a transient "OFF" (#4).

**Fix.**
- Add `ProcessorFacade::beginBatch()`/`endBatch()` over `std::atomic<int> batch_`.
- `setStateInformation`, `PresetManager::apply` and `tapMany` bracket their writes with it.
- While `batch_ > 0`, the audio thread reuses the previous `BlockParams`.
- `requestSnap()` is raised in `endBatch()`.

### 24. [minor] Analysis off the audio thread does not reproduce what the audio thread runs

**Where.**
- 01 §7: `stepResponse` is "Bit-identical to the plugin".
- 02 §9.3: `PreviewWorker`.
- 02 §9.1, last rows: the UI "builds an `EngineParams` from these (smoothed) fields".

**What is wrong.**
- The worker runs without FTZ. The probes run with `ScopedFtz`, so they pass while the shipped UI differs.
- `UiFrame` lacks `m[8]`, `topo` and `flags`, so Opto (`m[0]`/`m[1]`) and FET ALL curves are wrong while live.

**Fix.**
- Every `analysis::` entry point opens `fcdsp::ScopedFtz`.
- The UI builds `EngineParams` as `resolve(raw).eng` and then overwrites only the smoothed continuous fields from `UiFrame`.

### 25. [minor] Host-facing text and persistence

**Where.** 01 §3.1 (the unit and host-text column, and the Rules), 01 §4.6, and 01 §9.2 ("Host programs are the factory bank").

**Fixes:**
- **(a) Units.** Give every Mode-filtered parameter the label `""` and put the unit in the text. Otherwise AU and VST3 hosts append the fixed label (for example "INPUT 30 dB").
- **(b) Names.** Keep the host names universal forever; only the text relabels.
- **(c) Monitoring latches.** Reset `listen` and `delta` to 0 in `setStateInformation`. They are APVTS parameters, so a session otherwise reopens monitoring the side chain.
- **(d) Host programs.** `getNumPrograms() = 1`. JUCE's VST3 wrapper adds a "Program" parameter whose `stepCount = numPrograms − 1` (`juce_audio_plugin_client_VST3.cpp:643-653, 963`), so adding factory presets would remap program automation. FCompressor has its own browser.

### 26. [minor] FetchContent hygiene

**Where.** 03 §2.3, 02 §1.7, 02 §1.9, and 02 §1.2.

**Fixes:**
- **(a) Pin by SHA.** Use `GIT_TAG ${FCMP_FUNKGUI_SHA}  # v0.x.y`. Changing the pin then changes the declared details, and FetchContent re-populates. A moved tag cannot leave a stale `_deps/funkgui-src`.
- **(b) Override ancestry check.** Replace "override VERSION ≥ pin" with `git -C <override> merge-base --is-ancestor ${FCMP_FUNKGUI_SHA} HEAD`. VERSION changes only in tagging commits (03 §1.2), so it cannot tell a stale worktree apart.
- **(c) Sticky overrides.** `-DFETCHCONTENT_SOURCE_DIR_FUNKGUI` is cached and sticky. `verify.sh --integration`, used by the lead, fails when `fcmp-deps.txt` shows any override.
- **(d) FunkGui tools.** `FcmpDeps.cmake` sets `FUNKGUI_BUILD_TOOLS ON` (the tools are EXCLUDE_FROM_ALL), because `verify-gui-live` and the DoD need `funkgui_framerender` (03 §2.7, §4.7). 02 §1.7 defaults it OFF when FunkGui is consumed.
- **(e) Link visibility.** Add a configure assert: `INTERFACE_LINK_LIBRARIES` of `FCompressor` must not contain `FunkGui*`. A `PUBLIC` link would compile FunkGui's `.mm` files into every format wrapper as well.
- **(f) bgfx visibility.** After `MakeAvailable(bgfx)`, set `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON` on `bgfx`, `bx` and `bimg`. `release.sh` checks with `nm -gU` that the plugin exports only its entry points.
- **(g) FunkGui's own flags.** FunkGui's own test targets use `-ffp-contract=off`, so the gallery goldens stay arch-neutral.

### 27. [minor] Editor teardown order

**Where.** 02 §5.1 (`~EditorHost` calls `panel->closeGestures()`), 02 §9.5 (`JuceParamPort` wrapped "by the editor"), and 02 §9.3 (`PreviewWorker`).

**What is wrong.** The derived `fcmp::ui::Editor` members are destroyed before the base destructor ends gestures. If the ports or the worker live in the derived class, that is a use-after-free.

**Fix.**
- The Panel owns its ports.
- `fcmp::ui::Editor::~Editor()` first stops `PreviewWorker` (`stopThread(1000)`), then calls `panel().closeGestures()`.

### 28. [minor] `proc.osref` pins JUCE's latency values

**Where.** 03 §3.5 `proc.osref` ("latency equal to JUCE's integer latency"), against 01 §5.6 (targets ≤ 4 and ≤ 64).

**Fix.** The reference check compares passband and alias rejection only. Latency is `dsp.os`'s declared-equals-measured row.

---

## Top 5 changes

1. **Fix the engine contracts before the freeze (#1–#3).**
   - Replace `alphaFor`/`solveFb(…, alpha)` with the affine `FbAffine` + `B::stepFb(solve)` form.
   - Make `setTap` unconditional and runtime-only.
   - Give each arena slot a `PathState` (frozen outgoing `EngineParams` and its own preGain/makeup/drive smoother).
   - Add `msDomain` to `Carry`.
   - Upsample once per chunk.
   - Never let preGain reach the dry path.
2. **Processor threading rules (#6, #7).**
   - No APVTS listeners and no `AsyncUpdater`.
   - A 20 Hz message-thread `SetupWatcher` handles `quality`, `labudget` and `mode`.
   - `setLatencySamples` is also called in `prepareToPlay`.
   - The APVTS is built with `nullptr` as its UndoManager.
3. **Mode-switch safety (#4, #5, #20, #22).**
   - Move FET OFF out of `atk`.
   - Add the cross-Mode `crossmode.no_off` lint and the `fb.monotone` lint.
   - Ramp every GR OFF and Stage-2 OFF.
   - Define link inside FB loops.
   - Add detent-edge zipper rows.
4. **Oversampling and signal integrity (#11–#13, #8).**
   - Fractional-delay-exact `kStdLatency`.
   - SC delay compensated by D_up.
   - A consistent mix = 0 null spec.
   - Input sanitisation before the delay lines.
   - The `HistoryRing` claim-fence protocol.
5. **Determinism and host-validation gates (#14–#19).**
   - libm-free `tanh`/`logCosh`/`tanPi`, with the `lint.deps` grep.
   - Ship arm64-only until x86 runs.
   - JUCE `ObjCClass` runtime names.
   - The shaderc FATAL fix.
   - RTSan or a lock interposer, plus `tsan-agent`.
   - `validate.sh` (auval `-strict` + pluginval 10) as a sprint-end and release gate.
