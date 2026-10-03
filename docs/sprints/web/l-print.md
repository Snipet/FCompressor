# Scout report: print rows through the real worklet, and the audio rows

## Headline
- All 112 golden `dsp.print` rows equal through the shipped `fcmp-worklet.js` over the shipped `fcmp-engine.wasm` in an `OfflineAudioContext`, headless Chrome 154 (arm64, muted): 3.9 s of page time, 4.4 s wall.
- A plain snapped Params record is not a fresh engine: 108 of 112 rows fail. Each row needs a reconfigure (a record at another QUALITY, then the wanted one).
- Recommend option (ii), a test-only module beside the site. The print rows do not touch the denormal range; there the wasm engine differs from native arm64 by at most 9e-36 (bus-g, octo tails), and that needs its own rows.

## Facts
**The chain today**
- Goldens: `tests/golden/base/modes/<key>/dsp.print.txt`, 14 files × 8 rows (`print.<default|lo|hi|mid>.<l|r>.hash`, exact). Base only, no overlay.
- `Tools/probes/dsp/print.cpp`: `EngineHost` directly, STD, budget off, 48 kHz, blocks of 512 (:47, :52-56); hashes the whole 192,000-frame output, latency included (:99-100).
- `Tools/probes/common/PrintProgram.h`: 4 s stereo, a -12 dBFS log sweep plus -18 dBFS noise bursts (:47-70), never silent. Sets at :125-131; lo/hi/mid pass through libm `toNorm`/`toPlain` (:109-110). `Signals.h` uses no libm.
- Hash: FNV-1a 64 over float bit patterns (`enginecheck.cpp:160-174`).
- `web.engine.print` goes through the C ABI, not `EngineHost`: one Params record with snap, then `fcmp_web_configure(48000, 128)`, 128-frame quanta (`enginecheck.cpp:268-272`, :78). It relies on block-size invariance (goldens made at 512).
- `fcmp_web_selfcheck` (`WebEngine.cpp:266-297`, :383-414, :615-640): five Modes (clean, fet-76, opto-2a, octo, brickwall) × three segments of 4,096 frames: STD defaults; a ramped edit (thr -30, mix 0.75, output -3); HQ with a 5 ms budget and 2 ms lookahead. 1.28 s of audio in all.
  - Input is a triangle plus LCG noise at 0.5 / 0.03125: never silent, no tail, nothing near FLT_MIN. No ECO. Nine Modes never run.
- Page self-test (`web/main.js`), run today from a scratch copy of the site:
  - `engine.selfcheck.main` (:497-507) and `.worklet`: the worklet runs it in its message handler (`fcmp-worklet.js:144-154`), not in `process()`; 41 ms.
  - `worklet.render` (:509-554): relative, Mode 0 at create defaults, STD; 0 of 480,000 frames differ; 97× real time.
  - `engine.silence` (:556-612): main thread, Mode 0 only, gate off, limit ×3; printed "24.0 us / 24.0 us, x1.00".
- `web/tests/worklet.mjs` is relative too (the script against the module driven directly, under node).

**Measured in scratch** (logs in `<scratch>/out/`)
- A scratch relink of the engine with build-web's flags is byte-identical to the shipped `fcmp-engine.wasm` (545,704 bytes).
- Option (i) size: engine plus the print exports is 552,160 bytes (+6,456, 1.2 %; gzip +3,063). The standalone test module is 440,258 bytes with 0 imports.
- A native scratch tool through the C ABI reproduces 112 of 112 goldens at STD, so the test module's records are right.
- Chrome, real worklet, reconfigure method:
  - 112 golden rows pass with one context per row (56), per Mode (14, suspend/resume) or one context (56 rows back to back). 3.8 to 4.0 s each way.
  - 448 rows pass (STD against goldens; ECO, HQ, HQ + 5 ms budget + 2 ms lookahead against native arm64 values from scratch): 19.7 s.
  - Latency: 4 (STD), 0 (ECO), 61 (HQ), 301 (HQ + lookahead) samples.
  - Real-time factor through the worklet: STD 36× (mu-67) to 99× (bus-g); HQ 23.7× (mu-67) to 48.6× (bus-g).
- Why a plain record fails: a Mode change in a snapped block still crossfades (`EngineHost.cpp:72-74`), and the kernel key holds slot, topo, det, stmode, voice (`host/Crossfade.h:38`). The worklet configures at defaults in its constructor (`fcmp-worklet.js:67-84`).
- Why the reconfigure is sound: `applyParams` reconfigures on a setup change (`WebEngine.cpp:180-191`), and `EngineHost::configure` builds a new `Impl` keeping only `publishCount` (`EngineHost.cpp:705`, :762).
- Other rates (`std@44100`, `std@96000`, `hq@44100`; 14 Modes, default set): native = node wasm = Chrome worklet, 42 of 42.
- Denormal range, 14 Modes (2 s of program, then the source ends, or a ±2^-120 floor):
  - Chrome worklet = node wasm in 28 of 28.
  - Native arm64 differs from wasm: tail in bus-g (44 samples) and octo (67); floor in bus-g, octo, opto-2a, opto-3a, opto-tube-1b. Largest difference 9.0e-36 (-701 dBFS).
  - A stopped source reaches the worklet as 0 input channels.
- FP environment in Chrome 154 arm64: denormals are computed unflushed (f32, f32x4, f64, JS doubles) on the main thread, in the worklet's constructor and message handler, and inside `process()`, offline and live. x86 is unmeasured (no Rosetta here).
- Chrome honours `renderSizeHint`: quanta of 256, 100 and 2048 reached the worklet's piecewise path (`fcmp-worklet.js:178-188`), and bus-g `print.default.l` equals the golden each time.
- Memory (node, shipped module): 378 reconfigures (14 Modes × 3 qualities × 3 budgets × 3 rounds) at each of 44.1 to 768 kHz: 0 refused, a block of at least 8 MB still free of the fixed 16 MB.
- Cost of silence in Chrome, 14 Modes, main thread, gate off, node's method: ×0.85 to ×1.04 of signal; 7.5 s. Through the worklet (five Modes): burst then zeros with the shipped gate ×0.24 to ×0.45; burst then a 2^-120 floor ×0.88 to ×1.06.
- `--autoplay-policy=no-user-gesture-required` with `--mute-audio` gives a running live context headless: `page.audio` is then judged (112 quanta in 0.3 s).
- Node twin: the shipped worklet script in a stand-in scope passes 112 of 112 in 4.0 s; with a plain record 108 fail, with the same wrong hashes as Chrome.

## Question 1: what "equals the print hashes" adds in a browser
- **Absolute values for the nine Modes no browser row runs**, and for lo/hi/mid, over 4 s each instead of 85 ms. The self-check's 41 ms is consistent with mostly baseline-tier code (not shown); the print rows run 3.7 s of renders.
- **Arithmetic inside `process()`.** `engine.selfcheck.worklet` runs in the message handler. `worklet.render` runs in `process()` but compares the browser with itself, so an error common to both threads cancels.
- **Other wasm compilers.** Node proves V8 only. JavaScriptCore and SpiderMonkey have run nothing; the page gives 112 absolute rows there by hand.
- **Not at risk:**
  - libm and `Math.*`: the module has 0 imports and musl is compiled in.
  - Memory growth: fixed 16 MB, measured above.
  - NaN bits: the output is finite.
- **Not covered by print rows, so needs other rows:**
  - The denormal range (print is never silent). A browser that sets flush-to-zero on the audio thread would change bus-g, octo and the opto Modes below 1e-35; only a tail or floor row against the node value sees it.
  - Rates other than 48 kHz, ECO and HQ beyond the self-check, and the worklet's piecewise copy (`oddQuanta` is asserted 0 today).

## Design and change list (option ii)
- **Rejected: (i)** grows the shipped module, changes the ABI list in `web/tests/engine.mjs`, and puts `Tools/probes/common` under `Source/web/engine` against lint `web.engine`.
- **Rejected: (iii)** moves `kSelfCheckHash` in four files, still runs outside `process()`, and one hash cannot say which Mode moved.
- **Test module**: `Tools/web/live/PrintModule.cpp` (prototype: `<scratch>/src/PrintWeb.cpp`), over `PrintProgram.h`, `WebProtocol.h` and fcdsp. Exports: `fcmp_print_frames`, `_program(l, r)`, `_modes`, `_key(i)`, `_set(s)`, `_record(mode, set, quality, budget, lookMs, snap, out140)`, `_hash(v, n, out2)`, plus malloc and free.
- **CMake**:
  - `cmake/FcmpSources.cmake`: a glob `FCMP_WEB_LIVE_SOURCES` for `Tools/web/live/*.cpp`.
  - `cmake/FcmpWeb.cmake`: target `fcmp_web_print`, web only, with the engine's link options (:97-100), output `build-web/live/fcmp-print.wasm`, a dependency of `fcmp_web`. Never under `build-web/site` (`web.size` lists exact files, `size.mjs:85-90`).
- **Page**: `web/live/fcmp-print.html` and `fcmp-print.js` (prototype: `<scratch>/site/fcmp-print.js`).
  - It fetches the shipped `fcmp-worklet.js` and `fcmp-engine.wasm`, the test module, and the 14 golden files copied verbatim, and parses the TSV itself.
  - One `OfflineAudioContext` per Mode, 4 × 192,000 frames, the program in a looping `AudioBufferSourceNode`.
  - Per row: `suspend(k × 4 s)`, post two 140-byte records (another QUALITY, then the row's), await a `stats` round trip, `resume()`. Without `suspend`, one context per row.
  - Each row asserts `quanta === k × 1500`, `records` as sent, `refused === 0`, the latency, `oddQuanta === 0`, and the hash from `fcmp_print_hash`.
  - End rows: `rows.count === 8 × Modes`; a Mode without golden rows fails; `program.hash` is `2e3621bff17e9df5 9960d5f0d72e0b18`.
- **Extra expectations** (ECO, HQ, tail, floor, 44.1 kHz): `web/live/expect.mjs`, a node script over the shipped engine and the test module (prototype: `<scratch>/extra-node.mjs`), run by `web-live.sh` into `build-web/live/`.
- **`Scripts/web-live.sh`**:
  - stage `build-web/live` as a copy of the site plus `web/live/*`, the test module and the goldens;
  - run `check-page.mjs build-web/live --page fcmp-ui`, then `--page fcmp-print`;
  - flags `--mute-audio --use-angle=metal --autoplay-policy=no-user-gesture-required`.
- **Node twin**: `web/tests/print.mjs`, `// FCMP_WEB_TEST name=web.worklet.print timeout=120 args=...` (prototype: `<scratch>/print-node.mjs`), so the method stays in the gate without a browser.
- **Run everything each time.** 112 rows take 4 s here; I would expect about twice that on the CI runner. No subset is needed.

## Question 3: the flush-to-zero result to record
- **What `engine.silence` shows today:** one Mode (clean) on the main thread. The scalar colour stages the plan worries about sit in bus-g (VcaBus), octo (OctoDist), brickwall (LoudClip) and clean's non-default colours.
- **Record three things:**
  - The FP environment per place, from a test-only processor. JS doubles suffice (`DBL_MIN × 0.5`, a denormal × 1e300, operands read from an array).
  - Per-Mode cost on the main thread with the gate off, at node's ×2 limit.
  - Through the worklet: tail and floor values against the node file, and render-time ratios.
- **The script should print:**
```
NOTE  web.live.fpmode process(): flush-to-zero OFF, denormals-are-zero OFF; handler OFF/OFF; main OFF/OFF
PASS  web.live.tail bus-g main.cost: signal 21.6 us a quantum, silence at worst 20.8 us (x0.96, limit x2; gate off)
NOTE  web.live.tail bus-g worklet: burst + zeros x0.33 of signal; burst + 2^-120 floor x1.03
PASS  web.live.tail values: 14 Modes, tail and floor equal to the engine under node
NOTE  web.live.load: worst Mode at HQ mu-67 23.7x real time; at STD mu-67 35.9x
```
- This Mac cannot show a denormal slowdown; the numbers matter on the x86 runner and the Intel or AMD machine.

## Traps
- **Record race.** A record posted before `startRendering()` can land after the render: without the `stats` round trip, bus-g rendered at HQ (`ecd865e1d6589704`) because the second record arrived late.
- **Instance limit.** The 125th live engine instance in one page throws `RangeError: WebAssembly.Instance(): Out of memory`. Offline contexts are not freed in time, so keep to 14 (or at most 56) per page load.
- **Runner constraints.** `check-page.mjs --page <stem>` needs `<stem>.html`, `.js` and `.wasm` (:66-72), takes no query string, and any `--chrome-flag` replaces its defaults (:82-84).
- **Tail and floor expectations must come from the node run**, never from native.
- **Buffers at the context's rate**, or the browser resamples.
- **Emscripten cache.** A non-LTO `emcc` link wrote `crt1_reactor.o` and `libstandalonewasm-nocatch.a` into the shared Homebrew cache (my probe did this once). The LTO links wrote nothing.
- **From memory, unchecked:**
  - Firefox has no `OfflineAudioContext.suspend`.
  - Safari and Firefox clocks are 1 ms, which makes `engine.silence`'s 3 ms windows meaningless there.

## Tests (each can fail)
- `web.live.print <key> std print.<set>.<ch>.hash` × 112: fails on a plain record (shown: 108 fail).
- `web.worklet.print` (node twin): the same failure, shown.
- `rows.count` and `program.hash`: guard an empty or wrong run.
- `web.live.print <key> <eco|hq|hqla|std@44100>` against the node file.
- `web.live.tail values` against the node file: fails where a browser flushes on the audio thread. NOTE-level until x86 is known.
- `web.live.quantum`: one row at `renderSizeHint` 256 and 100, asserting `oddQuanta > 0`; NOTE where unsupported.
- `web.live.tail <key> main.cost` × 14.

**Rows that can pass vacuously today**
- `page.audio`: skipped with a NOTE whenever the context is suspended, which is every headless run without the autoplay flag.
- `engine.silence`: one Mode, on a machine with no denormal penalty, at ×3.
- `worklet.render`: equal for any engine behaviour common to both threads; the peak bounds are its only absolute check.
- `engine.selfcheck.worklet`: says nothing about `process()`.

## Open questions, with recommendations
1. **Build the test module in CMake and add the node twin?** Yes: about 4 s in the gate, and the browser page cannot rot unnoticed.
2. **Native goldens for ECO and HQ?** Not in the lead phase. Compare the browser with the node file now, and record in ADR-93 that 336 non-STD rows and 42 other-rate rows equalled native arm64 in scratch on 2026-10-02. Extending `dsp.print` is a follow-up that needs blessing.
3. **ADR-93's "bit for bit".** Qualify it: equal on the print material; in the denormal range wasm differs from native arm64 by at most 9e-36 in bus-g, octo and the opto Modes.
4. **Judge tail values on x86?** Record first. If the runner's `process()` shows flush-to-zero and the rows differ from node, that is a browser fact to write down, not a defect.
5. **Add the autoplay flag to `web-live.sh`?** Yes, always with `--mute-audio`.
6. **Per-Mode silence cost in `?selftest=1`?** No: keep the shipped page short and put the 14-Mode row on the live page.

## What I ran
- All work is under `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-l/audio` (`<scratch>` above). Nothing in either repository was edited, built or run; `git status` is clean.
- `em++` links via `<scratch>/build.sh` (`print`, `plain`, `plus`) against `build-web/libfcdsp.a`; one `emcc` for the 318-byte FP probe.
- `/usr/bin/c++` builds of `src/printx.cpp`, `src/extrax.cpp`, `src/proghash.cpp` against build-lead's two archives.
- Node: `golden.mjs`, `extra-node.mjs`, `mem-node.mjs`, `print-node.mjs`.
- Chrome 154 headless through `check-page.mjs <scratch>/site --page <stem> --chrome-flag --mute-audio --chrome-flag --use-angle=metal`, stems `fcmp-print` (seven configurations), `fcmp-extra`, `fcmp-tail`, `fcmp-fpmode`, `fcmp-quantum`, `fcmp-ui`. `fcmp-fpmode` and `fcmp-ui` also ran with the autoplay flag.
- Every run was muted and headless. No server or browser of mine is left (`pgrep -f scout-l/audio`: 0). Safari and the network were not used.