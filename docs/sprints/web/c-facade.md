# Scout report: card W-F, "the web facade proven natively"

Paths: **FC** = `/Users/seanfunk/audio/plugins/FCompressor`, **FG** = `/Users/seanfunk/audio/libraries/FunkGui`, **JUCE** = `/Users/seanfunk/audio/.deps/JUCE-8.0.4/modules`. Read at FC `776a598` and FG `f2641a9` (both `web/sprint-b`).

## 1. Facts

**(a) ProcessorFacade surface** (`FC/Source/plugin/ProcessorFacade.h:106-128`; `Processor` = `FC/Source/plugin/Processor.cpp`, `Fake` = `FC/Tools/probes/plugin/FakeFacade.cpp`)

1. `port(Pid)` :110. There are 30 ports (`Pid.h:22`); the header comment still says 29. Processor returns a `HistoryPort` that wraps `JuceParamPort` and tells `EditHistory` about gesture begin and end (`Processor.h:159-176`, `Processor.cpp:437-442`). Fake's `FakePort` stores `toPlain(pid, clamp01(v))` and logs the write (:47-55).
2. `currentRaw()` :111. Processor copies the 22 raw atomics, sets `modeSlot = resolveSlot(modeSlotOf(raw mode)).slot`, and takes `budget` from the configured atomic `budget_`, not the `labudget` raw (:347-353, :385-390). Fake uses `lround` for the slot and the `labudget` port unless a budget is scripted (:388-401).
3. `readUiFrame` :113. Processor forwards to `EngineHost::readUiFrame` (:444). A `Seqlock` read returns true with an all-zero frame before any publish (`fcdsp/telemetry/Seqlock.h:47-60`). Fake returns false until `publish()` (:403-409).
4. `history()` :114 returns the engine's ring (:445); Fake owns one behind a `unique_ptr` (:373). `SlotModel` keeps the reference for its lifetime (`FC/Source/editor/SlotModel.cpp:363`), so the address must be stable.
5. `setUiAttached(bool)` :115 is a count in `EngineHost` (`EngineHost.cpp:1206-1218`); Fake counts too (:414-417).
6. `uiState()` :117. Processor runs `syncUi()` and returns `ui_` (:447-451); Fake returns a plain member. `stateNotice()` :118 reads a seqlock in Processor (:453-459); Fake returns a member, and serial 0 means "no load".
7. `beginBatch`/`endBatch` :121-122: see (b). The editor's only callers are `HostProxy` (`FC/Source/editor/Panel.cpp:179-188`) and preset apply.
8. `presets()` :123: see (f). `edits()` :124 is the processor's `EditHistory` (`Processor.h:143`); Fake has one too (:438).
9. `diagnostics()` :127: see (g).

**(b) Batch, epoch and snap**

10. `beginBatch` calls `history_.batchBegan()`, bumps `batchEpoch_` and `batch_`, then issues a release fence (:409-415).
11. `finishBatch(snap)` (:419-433) sets `snapWanted_` if `snap`, decrements the depth, and at depth 1 → 0 moves `snapWanted_` into `snapPending_`. It then calls `history_.batchEnded()`. `endBatch()` is `finishBatch(true)` (:417). The history's own batches use `finishBatch(false)` (:483), so undo, redo and A/B ramp.
12. Each audio block (`render` :609-615) does three things in order: `snap = snapPending_.exchange(false)`, then `pullBlockParams()`, then `requestSnap()` if `snap`. `pullBlockParams` (:595-607) returns early while `batch_ != 0`, and discards its snapshot if the epoch moved.
13. So the audio side sees, per block, either the complete raw set as of that block's start or, during a batch, the previous `BlockParams`. An outermost `endBatch()` snaps the engine at the next block even when no value changed.
14. `WebEngine` restates this (`FC/Source/web/engine/WebEngine.cpp:164-210`): a Params record replaces all 30 values and rebuilds the block (`buildBlock` :89-111); `snap != 0` or a closed gate calls `requestSnap()`; every `process` runs the last record's block.
15. "One message per batch, with the snap" must therefore mean:
    - depth > 0: post nothing;
    - outermost end: exactly one `ParamsMsg` with all 30 values and `snap` = OR of the snap requests in the nest, posted even when nothing changed if `snap` is 1;
    - depth 0: each port write that changes the raw posts one `ParamsMsg` with `snap = 0`.

**(c) Parameter write to plain value**

16. UI writes go through FunkGui's `GestureController` only (`FG/src/params/GestureController.cpp:38-152`); no editor code calls `numSteps()` or `native()`.
17. A JUCE Float parameter holds two values. `setValueNotifyingHost(v)` sets the parameter value `P = legal(toPlain(clamp v))` (`JUCE/juce_audio_processors/utilities/juce_AudioParameterFloat.cpp:98`, `juce_RangedAudioParameter.cpp:54-58`, `FC/Source/plugin/ParamLayout.cpp:52-56`).
18. The APVTS adapter then computes `R' = convertFrom0to1(getValue()) = legal(toPlain(toNorm(P)))` and stores the raw `R = R'`, unless `approximatelyEqual(R, R')` and it is not the first notification (`juce_AudioProcessorValueTreeState.cpp:148-159`; tolerance `juce_core/maths/juce_MathsFunctions.h:311-323`).
19. Switches keep the host position as raw (`ParamLayout.cpp:112-132`; `juce_AudioParameterBool.cpp:78-79`); readers apply `>= 0.5` (`Processor.cpp:50`). Int parameters round.
20. A fresh `Processor` stores `h.def` exactly into every raw (:142-146). `WebEngine` does the same (`WebEngine.cpp:430-431`).
21. `HistoryHost::write` calls `setValueNotifyingHost(norm)` if `getValue() != norm`, then stores `plain` exactly into the raw (:469-480).
22. `PresetManager::apply` (`FG/src/presets/PresetManager.cpp:140-176`) computes `norm = convertTo0to1(plain)` per preset parameter and skips the write when its bits equal `getValue()` (:156-162). So a preset's raw is the host-map round trip, not the stored value; `proc.presets` allows 1e-6 of span (`FC/Tools/probes/plugin/presets.cpp:34`).
23. Scratch result (`rt.cpp`, `rt2.cpp`; see section 6): `toPlain(toNorm(toPlain(v))) == toPlain(v)` bitwise for 2 M random `v` per parameter. Round-tripping the table default moves knee (5.9999995), atk, rel, makeup (3.6e-7) and s2atk.
24. Scratch result (`model.cpp`, `model2.cpp`): a one-value port (`S = toPlain(v)`; preset write skipped when `toNorm(plain)` bits equal `toNorm(S)`; exact history writes) against the JUCE two-value model, 3 M random operations per continuous parameter. Zero mismatches after preset and exact writes. The only mismatches follow UI writes one float step away from the current value, where JUCE's `approximatelyEqual` keeps the old raw (1 ulp).
25. The Mode: `modeSlotOf` clamps and rounds with `+0.5f` (:35-49), then `resolveSlot` maps retired slots to their successor and unassigned ones to clean (`fcdsp/modes/Registry.h:51`).
26. `EditHistory::Host` (`FC/Source/plugin/portable/EditHistory.h:52-63`):
    - Processor (:465-491): `raw` = raw atomic; `write` as fact 21; `endBatch` = `finishBatch(false)`; `presetUuid`/`restorePreset` go to PresetAccess; `loadSerial` bumps per state load; `onMessageThread` asks JUCE.
    - Fake (:440-445, `FakeFacade.h:270-271`): `scriptPlain`, the facade's batch, FakePresets, a counter, `true`.
27. `EditHistory` reads `presetUuid()` at the outermost bracket end (`EditHistory.cpp:145`). A preset apply must therefore set the new identity before `endBatch`. `PresetManager` does (:170-175); FakePresets does (:128-131).

**(d) Telemetry**

28. `EngineHost` publishes one `UiFrame` per `process()` call while attached (`EngineHost.cpp:1094-1166`). `historyWritten` (:1129) is not read anywhere under `FC/Source/editor`.
29. `HistoryRing` (`fcdsp/telemetry/HistoryRing.h`): `written_` advances only through `push` (:50-60). There is no way to set it, so a mirror's `written()` is the count of columns pushed.
30. Per frame the Panel calls `currentRaw()`, `readUiFrame()` (a frame is new when `publishCount` differs), then `HistoryStore::drain(history())` (`Panel.cpp:379-401`, :452).
31. `drain` reads from its own `ringNext_` and inserts one gap marker only when the ring it reads lapped it (`FC/Source/editor/HistoryStore.h:39-57`, marker :147-155). `operatingX` also reads the ring directly (`FC/Source/editor/views/Telemetry.cpp:72-83`).
32. The Reply (`FC/Source/web/engine/WebProtocol.h:92-118`, filled at `WebEngine.cpp:231-261`) carries: columns oldest first, at most 512; `kReplyGap` when the engine ring lapped the engine-side reader; `kReplyMore`; `firstColumn` (low 32 bits); `latencySamples`; and the frame, valid only with `kReplyFrame`.
33. The engine's attach is a boolean, and only transitions reach `EngineHost` (:212-220). On attach, `historyNext = written()`.
34. A mirror must reproduce column order and content, including the engine's own attach-gap bit b5. It must turn `kReplyGap` into one pushed marker column, because the `HistoryStore` will never see the mirror lap.

**(e) Quality and labudget**

35. Plugin: `SetupWatcher::poll()` (:708-747) compares the raws with the configured atomics, reconfigures under `suspendProcessing`, and calls `setLatencySamples`. Probes call `setupWatcher().poll()` themselves (`FC/Tools/probes/plugin/state.cpp:331`). `configureEngine` rebuilds `block_` unless a batch is open (:550-566).
36. Web: the Params handler reconfigures at once (`WebEngine.cpp:172-191`). The snap is not requested separately then, because a new engine starts snapped. Latency travels in every reply (:256). A failed reconfigure returns `kPostFailed`, which a MessagePort cannot carry back.

**(f) Presets** (`FC/Source/plugin/Presets.cpp`)

37. List: factory bank in bank order, then user presets by the store's `Sort::name` (:152-174, :464-475). The sort key is case-, diacritic- and width-folded with digit runs padded (`FG/src/presets/PresetStore.cpp:64-75`, :1180).
38. `Row.modeKey` is the resolved Mode's key (`modeOf` :107-113, :170-171), not the stored string. FakePresets copies the raw `modeKey` (`FakeFacade.cpp:85`).
39. `apply` (:251-258, hooks :383-400): `beginBatch`; Mode first (`writeMode` :414-422); the 22 parameters in `kApvtsOrder`; baseline = live raws; identity set; `endBatch` (snap); then `markUsed`.
40. `modified()` (:227-233) is `isModified` (per parameter `|raw − snapToLegal(baseline)| > halfStep`; `halfStep` is 1e-6 × span for interval-0 ranges and 0.5 for Int; `PresetManager.cpp:98-100`, :178-192) OR baseline Mode slot ≠ live slot.
41. `step` wraps; from untitled, +1 → 0 and −1 → last (:260-268; Fake identical :134-142).
42. `currentUuid` :193. `restoreCurrent` (:195-225): same uuid → nothing; a factory or user uuid → that preset's stored values become the baseline; `""` or unknown → untitled with the live values as baseline.
43. `saveAs` (:270-287): trimmed name, empty refused, unique name ("Name 2", `PresetStore.cpp:894-917`), becomes current and unmodified. `rename`/`remove`/`overwrite` rules are in the header comment (:33-62). `importFile`/`exportFile` need files.
44. A fresh instance is Init, current 0, unmodified (`PresetManager.cpp:107-108`).
45. `FactoryData` (`FC/Source/plugin/portable/FactoryData.h:23-35`) gives uuid, name, category, notes, modeKey, modeRev and all 22 plain values, in the same order as `factoryBank()`.

**(g) Diagnostics** (`ProcessorFacade.h:88-104`; Processor :493-520)

46. The web can fill: `version` and `funkgui` (`FcmpProduct.h`, generated in every configuration, `FC/cmake/FcmpSources.cmake:42`); `juce = ""`; `prepared` (`kReplyConfigured`); `sampleRate` (frame); `maxBlock` 128; 2/2/0 channels; `quality`/`budget` (the posted values); `latencySamples` (reply).
47. The web cannot fill `loadAvg`, `loadPeak`, `overruns`, `blocks`: they are not in the protocol. Settings prints a dash for load when `blocks == 0` (`FC/Source/editor/views/Settings.cpp:431`). `format` and `host` need the page.

**(h) Layer rules and targets**

48. Today `Source/web/**` has only `web.juce` (`FC/cmake/LintDeps.cmake:271-273`), and `web/engine` has `web.engine` (:274-281). There is no `web.facade` rule yet. Lint strips comments before matching.
49. `Source/web/facade` is in no glob: `FcmpSources.cmake:104` globs `web/engine/*.cpp` only. `fcmp_probe_plugin`'s sources are `_own` at `FC/cmake/FcmpProbes.cmake:84-86`.
50. `fcmp_web_engine_lib` exists in every configuration (`FC/cmake/FcmpWeb.cmake:48-53`; included at `FC/CMakeLists.txt:133`). `FcmpProbes` is not included for web (`CMakeLists.txt:130-131`).
51. The portable sources (`EditHistory.cpp`, `FactoryData.cpp`) are already in `fcmp_probe_plugin` (`FcmpSources.cmake:90-91`). `lint.headers` does not scan `Source/web` (`FC/Scripts/check-headers.sh:70-83`).

**(i) Probe grammar**

52. First line: `// FCMP_PROBE layer=<dsp|proc|ui> name=<[a-z0-9_]+> scope=<global|mode> timeout=<s>[ platform=…]` (`FcmpProbes.cmake:163-168`). Tests are named `<layer>.<name>`; helper files without the line register nothing. So the three files are `webnull.cpp` (`layer=proc name=webnull`), `webpresets.cpp`, and `ui_web.cpp` (`layer=ui name=web`).
53. The body is `FCMP_PROBE(proc, webnull) { …; return P.finish(); }` (`FC/Tools/probes/common/ProbeRegistry.h:8-14`). Spec rows (`P.eq`, `near`, `ge`) need no golden. `P.num`, `hash` and `text` rows are golden rows; a new one exits 3 and writes a candidate (`FG/include/funkgui/test/Harness.h:230-241`).
54. Models:
    - `proc.null`: Processor against an `EngineHost`, bitwise mismatch counts (`FC/Tools/probes/plugin/null.cpp:58-120`, :182-205, :235).
    - `proc.history`: gestures through `port()` (`history.cpp:57-71`).
    - `EngineFacade`: block building and batch reuse in a probe (`EngineFacade.cpp:119-196`).
    - `ui.edits`: a Panel over a facade through `HeadlessHost` (`ui_edits.cpp:57-88`).
55. `funkgui::HeadlessGuiScope` exists in the pin (`FG/include/funkgui/panel/HeadlessGuiScope.h`).

## 2. What the card must do (in build order)

1. **Lead first** (the card cannot compile without it):
   - `FcmpSources.cmake`: `file(GLOB FCMP_WEB_FACADE_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/web/facade/*.cpp)`.
   - `FcmpProbes.cmake:84`: add that list to `_own`, and `target_link_libraries(fcmp_probe_plugin PRIVATE fcmp_web_engine_lib)`. Linking the library is smaller than compiling `WebEngine.cpp` twice.
   - `LintDeps.cmake`: rule `web.facade` (Q3 has the wording).
2. `Source/web/facade/EngineLink.h`, header only:
   ```cpp
   namespace fcmp::web {
   struct ReplySink { virtual void reply(std::span<const std::uint8_t> record) = 0; protected: ~ReplySink() = default; };
   class EngineLink { public: virtual ~EngineLink() = default;
       virtual void post(std::span<const std::uint8_t> record) = 0;   // one WebProtocol record; never blocks; no result
       virtual void setSink(ReplySink*) = 0; };                        // nullptr: replies are dropped
   }
   ```
   Contract: replies come on the facade's thread, in order, at most one per Pull, possibly inside `post()` (loopback), possibly later, possibly never. The bytes are valid only during `reply()`.
3. `LoopbackLink.{h,cpp}`: owns an `FcmpWebEngine` (create and destroy). `post` calls `fcmp_web_post`; a positive result calls `sink->reply({fcmp_web_reply(e), n})`; a negative result counts in `refused()`. `engine()` lets the probe act as the worklet (`fcmp_web_configure`, `fcmp_web_process`, `fcmp_web_set_gate`).
4. `WebFacade.{h,cpp}`: `final : public ProcessorFacade, private ReplySink`, constructed over an `EngineLink&`.
   - **Values**: 30 host values starting at `kHostParams[i].def` (Q1 decides the model).
   - **Ports**: 30 ports that call `EditHistory::gestureBegan`/`gestureEnded` as `HistoryPort` does. `default01`, `numSteps`, `id` and `native() = nullptr` as `FakePort`.
   - **`currentRaw()`**: `modeSlotOf`/`resolveSlot` as `WebEngine.cpp:71-94`; `budget = budgetOf(plain[labudget])`.
   - **Batches**: `depth_` and `snapWanted_` per fact 15; `beginBatch` calls `history_.batchBegan()`; a private `finishBatch(bool snap)` ends with `history_.batchEnded()`.
   - **Telemetry**: `setUiAttached` keeps a count and posts `AttachMsg` on 0↔1 only. `readUiFrame` returns true with the last frame, zero before any reply, as Processor does. The mirror `HistoryRing` is behind a `unique_ptr`.
   - **`reply()`**: check magic, version, kind and `bytes == replyBytes(columnCount) <= n`, and `memcpy` the head and each column (the engine's reply buffer is aligned, but a span from a port need not be, and `-Wcast-align` is an error). On `kReplyGap` push one marker column (levels −200, GR 0, `bits = 1u << 5`), then the columns. Take the frame only with `kReplyFrame`. Store the latency and flags.
   - **Not on ProcessorFacade**: `pull()` (posts a Pull with a fresh tag, with at most one outstanding and a give-up after N calls; loops while a synchronous reply said `kReplyMore`, at most 9 times); `resync()` (Params with snap 1, plus Attach if the count is > 0, for a link that just connected); `resetEngine()` (ResetMsg); `plain(Pid)`; setters for `format`, `host` and the sample rate.
   - **Rest**: `uiState()` is a member; `stateNotice()` returns `{}`; `diagnostics()` per facts 46-47; `edits()` is an `EditHistory` over a nested Host (`raw` = raw value, `write` = exact write, `endBatch` = `finishBatch(false)`, `loadSerial` 0, `onMessageThread` true).
   - **Destructor**: `link.setSink(nullptr)`.
5. `WebPresets.{h,cpp}`: `final : public PresetAccess`.
   - **Rows**: `factoryRows()`, then session user rows sorted by folded name (ASCII lower case, digit runs padded). `modeKey` is resolved as in fact 38.
   - **`apply`**: `facade.beginBatch()`; Mode; the 22 values through the same write rule as `PresetManager` (fact 22); baseline = live raws; identity; revision; `facade.endBatch()`.
   - **`modified()`** per fact 40. **`revision()`** bumps on list, selection or modified change, lazily as `Presets.cpp:235-249`.
   - **Management**: `step`, `saveAs`, `rename`, `remove`, `overwrite`, `currentUuid`, `restoreCurrent` per facts 41-43. Unique names follow `FakePresets::uniqueName` (`FakeFacade.cpp:220-239`). User uuids come from a session counter or generator, never the factory pattern.
   - **Files**: `importFile` and `exportFile` keep the refusing defaults.
6. Probes `Tools/probes/plugin/{webnull,webpresets,ui_web}.cpp` (section 4). `ui_web.cpp` uses `HeadlessGuiScope` and no JUCE header from the start, because Sprint D runs every `ui_*` probe under node.

## 3. Traps

1. **Plan text against JUCE.** "Ports store `fcdsp::toPlain`" is not what the APVTS holds in every case (facts 17-24). Without the skip rule, Init after an undo gives knee 5.9999995 where Processor keeps 6.0. Without the `approximatelyEqual` rule, a one-ulp drag step differs by one ulp. See Q1.
2. **The silence gate is on by default** (`WebEngine.cpp:49`, :516-535). It resets the engine after about 100 ms of exact zeros, and Processor never does. `proc.webnull` must call `fcmp_web_set_gate(e, 0)` or avoid silent input. A closed gate also snaps on any record (:192).
3. **Empty batches snap** in Processor (fact 13). A facade that posts only on change loses that snap, and a mid-ramp `endBatch` then differs.
4. **Quality timing.** Web reconfigures per record; Processor reconfigures at `poll()`. The probe must call `proc.setupWatcher().poll()` after each quality or labudget write and before the next block. Compare `currentRaw().budget` only after the poll.
5. **Block size.** `UiFrame` is per `process()` call and parameters land per block. Run Processor at `prepareToPlay(48000, 128)` with 128-frame buffers. Use `proc.null`'s layout, stereo with the side chain disabled (`null.cpp:73-77`), so `keyChans` is 0 on both sides.
6. **Reply buffer lifetime.** It is valid until the next `fcmp_web_post` (`WebEngine.h:67`). `reply()` must copy everything before it posts again. A full `Reply` is 16 704 bytes: parse in place, not on the wasm stack.
7. **Attach.** It is a count on the facade and a boolean in the engine. Forwarding every call would make two editors detach each other.
8. **Mirror indices.** The mirror's `written()` differs from the engine's after a gap or a re-attach. Never compare it with `UiFrame::historyWritten` in a probe after such an event.
9. **Lint.** No `EngineHost` token may appear in facade code, so compute a pre-reply latency as `FakeFacade.cpp:492-493` does (`kOs[q].latency + lookaheadSamples`). `-Wswitch-enum`, `-Wshadow-all`, `-Wconversion` and `-Wcast-align` are errors (`FC/cmake/FcmpArch.cmake:122-131`).
10. **OWNS overlap.** The plan gives E-2 `Tools/probes/plugin/ui_*.cpp`, and W-F creates `ui_web.cpp`. The manifests must exclude it from E-2. E-2 also changes the undo chord test (`#if JUCE_MAC` → `commandKeyIsMeta()`). `ui_web`'s chord rows must be written so both orders of merge pass: copy `ui_edits.cpp:132`, :190 and rebase at integration.
11. **Golden rows through libm.** `toPlain` uses `pow`, `log`, `expm1` (`fcdsp/params/HostParams.cpp:114-130`; K2 #14). An output-hash golden for `proc.webnull` could differ between macOS and Linux. Keep these probes spec-only (mismatch counts).
12. **Frozen files.** `ProcessorFacade.h`, `FakeFacade.h`, `EditHistory.h`, `HistoryRing.h`, `UiFrame.h`, and in practice `WebProtocol.h`/`WebEngine.{h,cpp}` (not in the plan's OWNS). Any need there, such as load figures or a Params acknowledgement, is an interface-change request.
13. **`LoopbackLink` in the editor module.** If Sprint D globs `web/facade/*.cpp` into the browser's editor module, `LoopbackLink.cpp` pulls the engine in. That is fine for `fcmp_probe_web` but wrong for the page; W-U must exclude it.

## 4. How to verify

**Existing cover.** `web.engine.*` (the wrapper against goldens), `proc.null`, `proc.history`, `proc.presets`, `ui.edits` and `ui.presets` (Processor and FakeFacade behaviour), `lint.deps`.

**`proc.webnull`** (global, spec-only). Processor P (128-frame blocks, attached) and WebFacade W over a LoopbackLink (gate off, `fcmp_web_configure(e, 48000, 128)`, attached). Both get the same input: sine on L, seeded noise on R, never silent. W is wrapped in a probe-local recording link that counts records and their snap flags. After each step, render K quanta on both and call `W.pull()` after each quantum.

Steps:
1. Defaults.
2. A THRESHOLD gesture with two sets and audio between them.
3. A batch as ModeBrowser makes it (Mode plus several parameters), with two quanta rendered while it is open.
4. An empty `beginBatch`/`endBatch` during a ramp.
5. `presets().apply(k)` on a row of another Mode, then `step(+1)`.
6. Undo, then redo.
7. `selectSlot(1)`, an edit, `selectSlot(0)`.
8. OUTPUT, BYPASS on and off.
9. Quality → HQ, labudget → 5 ms, look 2 ms, with `poll()` after each write.
10. `P.reset()` against `W.resetEngine()`.
11. Only if Q1 is B: a one-ulp drag step, and undo followed by re-applying Init.

Rows, per step unless noted:
- `out.mismatches == 0`: both channels bitwise. This proves the block building, the batch and snap timing, and the reconfigure.
- `raw.mismatches == 0`: `proc.rawValue(pid)` against `W.plain(pid)` for all 30, bitwise.
- `frame.mismatches == 0`: `memcmp` of the two UiFrames after every quantum.
- `columns.mismatches == 0`, once: every column of both rings, with equal `written()` and one attach at the start.
- `latency`, per setup: `getLatencySamples()` against W's.
- `records`: gesture = 2 with snap 0; open batch = 0; batch = 1 with snap 1; empty batch = 1 with snap 1; preset = 1 with snap 1; undo = 1 with snap 0.
- `refused == 0`.
- Non-vacuity: `max_diff >= 1e-3` between consecutive steps' outputs.

**`proc.webpresets`** (global, spec-only), against a Processor's `presets()`:
- fresh: count, current 0, unmodified, every factory row's uuid, name, category and modeKey;
- per factory row: apply, then raws bitwise equal, same current, unmodified, and the 8 globals untouched;
- a nudge reads modified; a Mode switch reads modified;
- `step` wraps, including from untitled;
- `saveAs` with a duplicate name and with an empty name; the user-row order with names like "b", "A", "a 10", "a 2"; `rename`, `remove`, `overwrite`, each compared for list, current and modified. User uuids are not compared.
- `restoreCurrent` for a factory uuid, a user uuid and `""`;
- `revision()` moved after each success and held after each refusal.

**`ui.web`** (global): a Panel over WebFacade and LoopbackLink through `HeadlessHost`. Rows: a preset click, undo and redo by click and by chord, A/B, a typed value, and the record counts for each. If the engine is configured and rendered, also check that the reply frame's `thrDb` follows the edit.

**Not automatable here:** real MessagePort timing, a suspended AudioContext, a background tab's lap (the gap path can be unit-driven with a hand-built reply carrying `kReplyGap`), and browser libm.

## 5. Open questions for the lead

**Q1. Port value model.**
- (A) As the plan says: one value, `toPlain`, plus the preset skip rule. Bit-equal to Processor except one-ulp UI steps (fact 24), which the script would then avoid.
- (B) Mirror JUCE: `{param, raw, first}` per parameter, about 15 lines, equal in every case including the corner rows.
- I recommend B: it removes the need for the script to dodge a case. It does write a JUCE quirk into our code, which `proc.webnull` would keep honest.

**Q2. Where the Pull is issued.**
- An explicit `WebFacade::pull()` called by the frame loop (and by probes), or inside the const `readUiFrame()` with mutable state.
- I recommend the explicit call; W-U calls it before `Panel::tick`.

**Q3. `LoopbackLink` location and the `web.facade` rule.**
- I recommend `Source/web/facade/LoopbackLink.{h,cpp}` as planned, because `fcmp_probe_web` needs it under node.
- Suggested rule: no Emscripten header; no `EngineHost` token and no `fcdsp/engine/EngineHost.h`; from `plugin/` only `ProcessorFacade.h` and `portable/*`; from `funkgui/` only `params/ParamPort.h`; nothing from `editor/`; `web/engine/*` allowed.

**Q4. Diagnostics load.**
- Leave the four fields 0 (Settings shows a dash and "0 OF 0 BLOCKS"), or raise the protocol to v2.
- I recommend 0 now and listing it under "what differs from the plugin" in Sprint D. I did not check whether a worklet can time itself.

**Q5. `kPostFailed` is invisible over a port.**
- Accept it, or add an acknowledgement in a later protocol version.
- I recommend accepting it: the reply's latency exposes a failed reconfigure.

**Q6. Writes outside a batch.**
- Post on every changed write (the exact analogue of the per-block pull, and what `proc.webnull` can prove), or coalesce per frame.
- I recommend posting on every changed write.

**Q7. Goldens.** The plan says to bless new rows. I recommend all three probes stay spec-only, unless `ui.web` needs a frame fingerprint.

## 6. What I ran and what I did not check

**Ran**, all under `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-c/wf`:
- `rt.cpp`, `rt2.cpp`, `model.cpp`, `model2.cpp`: clang++ `-std=c++20 -O1 -ffp-contract=off` against the repository's `HostParams.cpp`, on macOS arm64 only.
- Compile-only (`-c`) of `EditHistory.cpp` and of a file including `ProcessorFacade.h`, `EditHistory.h`, `FactoryData.h`, `WebProtocol.h` and `WebEngine.h`, under em++ 6.0.3 and clang. All compiled.

**Not checked:**
- No facade prototype was built or run against the real engine. Building fcdsp beside the running gate was avoided, so the loopback design and the UiFrame/column bit-equality claim are from reading only.
- Linux libm for facts 23-24.
- `std::mutex` at run time under wasm (compile only).
- The claim that the Int and Choice maps equal fcdsp's index map is taken from the comment at `ParamLayout.cpp:11`.
- `State.cpp` (no state loads in the demo), the Settings quality write path, and `Tools/web/enginecheck.cpp`.
- Whether Chrome delivers port messages to a worklet while the AudioContext is suspended.