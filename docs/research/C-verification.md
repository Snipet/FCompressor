# C: How HardwareReverb verifies itself, and a proposed test suite for FCompressor

Scope: this covers HardwareReverb's (HR) harnesses, golden files, GUI capture path and build configuration. It measures how long the check suite takes. It then proposes a verification suite for FCompressor that is built for compressors and for a Mode list that keeps growing.

All HR paths are relative to `/Users/seanfunk/audio/plugins/HardwareReverb`. Line numbers are from the files as they were on 2026-09-22.

HR was only read, never changed. To take timings, the harness binaries and the three `verify-gui*.dump` files were copied to the scratchpad. The copies were run there with `--check` pointing at HR's goldens, and `--check` only reads. Nothing was built, configured or launched inside HR.

> Observed state of HR today. `Tools/BankProbe.cpp` and `Tools/PresetProbe.cpp` are 4-line stubs (`// STUB — replaced by the real harness.`, dated 19:26). `CMakeLists.txt` was modified at 19:49. `build-dsp/` was rebuilt at 19:28–19:29. Someone is working on HR right now. `verify-gui` refers to `Tools/golden/framerender-browser.txt` (`CMakeLists.txt:597-599`), and that file does not exist in either golden directory. The target would currently stop at that step with exit 2.

---

## 1. `Tools/Harness.h`: API and conventions

`Harness.h` is header-only and does not depend on JUCE (`<cstdint> <cstdio> <cstdlib> <cmath> <cstring> <string> <vector>`, plus `<xmmintrin.h>` on x86). Everything is in `namespace hrvb`. It has two jobs (`:5-21`): put the tool in the plugin's floating-point mode, and turn printed tables into pass/fail.

| Symbol | Lines | Exact signature / behaviour |
|---|---|---|
| `struct ScopedFtz` | 50-72 | RAII. arm64 sets `FPCR \|= 1<<24` (FZ) through `mrs/msr fpcr`. x86-64 sets `MXCSR \|= 0x8040` (FTZ 0x8000 + DAZ 0x0040) through `_mm_getcsr/_mm_setcsr`. The destructor restores the saved value. Any other arch hits `#error`. It copies what `juce::ScopedNoDenormals` does, and the processor opens `processBlock` with exactly that (`Source/PluginProcessor.cpp:338`). Without it, 9 IrAnalyse configurations fingerprint differently (`:7-14`, README 495-500). |
| `kExact` | 76 | `static constexpr double kExact = -1.0;`. A tolerance below 0 means the text must match exactly. |
| `struct Metric` | 78-83 | `{ std::string key; std::string value; double tol = kExact; }`. The value is stored **as text**. |
| `addNum` | 85-90 | `void addNum(std::vector<Metric>&, const std::string& key, double v, double tol)`. The value is formatted `%.6g`, so 6 significant digits. The comparison later uses `atof` of that text. |
| `addHash` | 92-97 | `void addHash(std::vector<Metric>&, const std::string& key, uint64_t h)`. Formatted `%016llx`, always exact. |
| `writeGolden` | 99-113 | `bool writeGolden(const char* path, const std::vector<Metric>&)`. Writes two `#` header lines, then `key\tvalue\texact` or `key\tvalue\t%g`. Prints `blessed N metrics -> path`. |
| `checkGolden` | 117-183 | `int checkGolden(const char* path, const std::vector<Metric>&)`. Parses with `sscanf(line, "%255s\t%127s\t%63s")` (`:131`) and skips `#` and blank lines. Prints **every** mismatch, then a summary. Returns 0 on pass, 1 on any mismatch, 2 if there is no golden. |
| `finish` | 186-196 | `int finish(int argc, char** argv, const std::vector<Metric>&)`. Looks for `--check <path>` or `--bless <path>` anywhere in argv. With neither, it returns 0 and the tool is in report mode. |

Conventions every tool follows:
- `main` opens with `const hrvb::ScopedFtz ftz;`. The DSP tools do. The GUI tools don't need it.
- The tool prints a readable table first. Metrics are collected along the way and `return hrvb::finish(argc, argv, metrics);` comes last.
- Hashes are FNV-1a 64 (offset `1469598103934665603`, prime `1099511628211`). The hash function is re-declared in each tool.
- Keys are dotted: `<table>.<case>.<quantity>`. They must contain no whitespace, because `%255s` stops at the first space. IrAnalyse maps its labels through `keyOf()` (`IrAnalyse.cpp:97-103`: space→`_`, collapse `__`).
- Every tolerance is **absolute**. Counts and 0/1 flags use tolerance `0`, and hashes use `exact`.

Weaknesses FCompressor should fix in its own copy:
1. **Duplicate keys shadow silently.** `checkGolden` takes the first match on both sides (`:140`, `:172`).
2. **No relative or one-sided tolerances.** "Click ≤ +3 dB" can only be written as "the blessed value ± something".
3. **`--bless` can launder a regression.** There is no analytic spec check that works independently of the golden.
4. **Buffer limits.** Key ≤ 255 chars, value ≤ 127 chars, line ≤ 512 chars (`:126-131`). Mode-prefixed keys have to stay short.
5. **Golden paths are passed in by CMake and have drifted.** The comments say `Tools/golden/ir.txt` (`CMakeLists.txt:515`) and `Tools/golden/reset.txt` (`ResetProbe.cpp:22`). The real files are `iranalyse.txt` and `resetprobe.txt`.

### 1.1 Each tool

| Tool (lines) | Links | Inputs / args | What it measures and **asserts** (golden rows, tolerance) |
|---|---|---|---|
| **IrAnalyse** (476) | `FDNReverb.cpp`, `juce_core`, DspFlags | optional `nosettle` as argv[1] (report only) | 14 configs at 48 kHz/bs 128, 6 s impulse response after a 1.5 s settle (`settleSamplesFor`, `:190-193`). `<cfg>.ir` = FNV-1a of the raw float bits of the mid IR (`:85-95`), exact. `<cfg>.rt60` = Schroeder backward integration T30×2 (`:62-81`), ±0.02 s. The sample-rate subset at 44.1/96/192k (`:296-316`) uses the same two rows. `bounds.fs*` = longest delay requested against what `prepare()` allocated (±1 sample, `ok` exact). `bs.<cfg>.mismatches` = number of block sizes {1,17,64,128,512,4096} whose hash differs from bs 128 (tol 0, `:393-430`). The advanced parameters get L and R hashed separately plus rt60 (`:444-473`). **99 metrics.** |
| **ResetProbe** (356) | same | none | `cross.<cfg>.<fs>`: hash of a noise→`reset()`→silence render (exact), plus `.post_peak` = peak after the reset (tol 0). `cost.under_one_block.<fs>` = 1 if `reset()` costs less than one 64-sample `processBlock` **on this machine**, a ratio gate that travels between machines (`:129-178`). `hostile.*`: NaN, +inf and 1e30 as one sample and as a whole block, injected into a running tail next to a control instance. `nonfinite_out` tol 0, `tail_err_db` ±1 dB. The `guard` row (3e38 for 4 blocks) asserts `first_bad_block=38`, `recover_blocks=1`, `post_peak=0` (`:180-353`). **40 metrics.** |
| **DuckProbe** (248) | same | none | Order change under a live tail, test instance against an untouched control on identical noise. `dip_db`/`lift_db`/`warm_db` ±0.1, `act_ms` (read from the UI frame) ±5, energy ratio in windows 0.3–0.5 / 0.5–1 / 1–2 s after activation ±1.5 dB (`:33-172`). `level.fromX.toY.err_db` = driven level after switching against an instance born at that order, ±0.1 (`:179-246`). **61 metrics.** |
| **SmearProbe** (472) | same | none | `step.*.step_minus_rms_db`: worst sample-to-sample slew minus RMS in a 40 ms window around a knob step, ±2 dB (`:88-122`). `freeze.*.drift_db`: 60 s lossless hold, ±0.25 (`:128-187`). `stale.*.burst_db`: sections re-entering after silence, ±6 (`:188-254`). `seam.*.hole_db` ±5, `seam.period_dev_samples` ±0.01 (`:256-356`). `seam.null.*`: null depth of a 3-instance comparison at g→0, ±3 dB (`:358-469`). **29 metrics.** |
| **StateProbe** (727) | `PluginProcessor.cpp` + `PluginEditor.cpp` (classic) + `FDNReverb.cpp` + preset core, `juce_audio_processors`, DspFlags. `JucePlugin_Name` is defined by hand (`CMakeLists.txt:447-463`) | none | The only tool that goes through the processor. `state.roundtrip.*`: every parameter set to a distinct value snapped through its range (`:373-374`), saved and restored into a fresh instance, **bitwise compare** of `getValue()` (`:391-393`). `state.absent.*`: a blob with the newer children stripped, restored into a **non-fresh** instance, must reset those to default (`:401-445`). `text.defaults.hash` = hash of `id=text\|label;` for every parameter, exact. `text.roundtrip.bad` = the `getText→getValueForText` round trip at 0.5, tol ±1.1 % (`:447-488`). `tail.*` ±1e-3 s. `layout.XinYout` 0/1. `factory.*`: impulse through `processBlock` at bs 64/128 and "declared 64, fed 1024" must **hash-equal** the engine fed the processor's own snapped values (`:517-566`). Mono→stereo. `bypass.*`: bit-exact passthrough (`max_err` tol 0), edge slew ±0.5 dB, `tone.*.hf_ratio_db` = energy above 8 kHz in ±2 ms around the bypass edge on a 110 Hz tone, against the control, ±3 dB (`:666-703`). `unprepared.max_err` = processBlock before prepare must return (`:705-724`). **34 metrics.** |
| **DspBench** (97) | same | `[sampleRate] [blockSize]` | ns/sample, % of one core and MB of state per order × smear. Reports only: it does not call `finish` (`:96`). Kept out of `verify` because timings depend on the machine. |
| **FontProbe** (98) | `FontAtlasSdf.cpp`, `UiPreferences.cpp`, `juce_gui_basics`, font blob | `[--check g] [candidate.ttf ...]` | `font.atlas` = FNV-1a of the baked 1024×512 SDF atlas bytes (exact). `font.cap/x/ascent/digit` ±1e-3. `font.missing` (tol 0). Each candidate font is compared texel by texel. Any difference sets the exit to 1 (`:96-97`). **6 metrics.** CPU only: the atlas bakes without a GPU. |
| **PrefsCheck** (121) | same | `self` \| `write n` \| `read` \| `clamp` | Before the singleton reads the environment it sets `HRVB_PREFS_DIR` to a temp dir (`:31-40`). It then asserts write→disk, reload sees another process's write, clamp(99) stays in range, and sandboxed. Each is 0/1 with tol 0 (`:83-116`). The exit is forced to 1 if any check fails, whatever the golden says (`:119-120`). **4 metrics.** |
| **FrameRender** (311) | same | `<dump> <out.png> [ss 1-4]` or `--fingerprint <dump> [--check g]` | Parses a canvas dump and either rasterises it on the CPU (a reimplementation of `fs_ui.sc`'s SDF evaluation for rrect, segment and text) or fingerprints its geometry. The fingerprint is covered in §2.2. **9 metrics.** |
| **AtlasDump** (188) | same | `[outDir] [face names...]` | Bakes the atlas, times it, and writes the raw field PNG plus a specimen sheet. Report only. |
| **BankProbe / PresetProbe** | processor + presets / store + SQLite | none | **Stubs today.** They return `finish` with an empty metric vector, so `--check` against any golden fails every row as "missing". Their CMake targets exist (`:467-499`) and are not in `verify`. |

A pattern worth copying: **control-instance differencing** (DuckProbe, ResetProbe hostile, SmearProbe null, StateProbe bypass). Run an untouched instance on identical input and gate on the *difference*. That separates the artefact from the signal's natural behaviour.

---

## 2. Golden files

### 2.1 Format
The file is plain TSV (`Tools/golden/*.txt`):
```
# key	value	tolerance ('exact' or an absolute number)
# Regenerate deliberately with --bless; review the diff.
order_8,_diff_0_(factory).ir	4e1c9b78568fcec4	exact
order_8,_diff_0_(factory).rt60	3.47542	0.02
hostile.guard.first_bad_block	38	0
layout.max_y	508.189	0.01
```
Row counts (arm64): iranalyse 99, resetprobe 40, duckprobe 61, smearprobe 29, stateprobe 34, fontprobe 6, prefscheck 4, framerender / -filter / -mod 9 each. `verify` gates 263 rows.

### 2.2 How fingerprints are made, and what survives float noise
- **DSP fingerprints are not quantised at all.** They are FNV-1a over the 4 little-endian bytes of every `float` sample (`IrAnalyse.cpp:85-95`, the same code appears in ResetProbe and StateProbe). By design they are exact null tests ("any change claimed to be arithmetically neutral must leave every configuration identical", README 521-523). They do not *survive* float noise. Float noise is *removed* from the system instead:
  1. **The same FP mode everywhere** (`ScopedFtz`).
  2. **The same compile flags.** There is one `INTERFACE` target, `HardwareReverbDspFlags` (`CMakeLists.txt:379-387`: arch ISA flags, `-O3 -fno-math-errno -fno-trapping-math`, JUCE recommended config + LTO), linked by the plugin (`:608-613`) and by every DSP harness. Measured today: the Release `build-dsp` binaries pass the goldens that were blessed from the RelWithDebInfo `build`, because the interface `-O3` overrides the configuration's `-O`.
  3. **Bit-identical SIMD backends.** An integer emulation of AArch64 `FRSQRTE` on x86. `sum += f*f` split into two statements so clang does not fuse it on x86 only (README 90-110, `CMakeLists.txt:526-548`).
  4. **One golden set per arch** for what is left, which is libm `powf`/`exp` last-bit differences.
- **The numeric rows** carry 6 significant digits of text (`%.6g`) and an absolute tolerance chosen for each row, with the reason in a comment. Examples: 0.02 s for RT60, 0.1 dB for dip ("far below audibility but far above float noise", `DuckProbe.cpp:154-156`), 2–5 dB for single-realisation statistics (`SmearProbe.cpp:115-118, 344-347`). A few metrics are **ratios against this machine** instead of absolutes: ResetProbe cost < 1 block, and the energy ratio test/control.
- **The GUI fingerprint** (`FrameRender.cpp:108-150`) is FNV-1a over the float bits of `x0 y0 x1 y1, d0[4], e0[2], d1[4], d2[4]` for every primitive. `c0` and `c1` (colours) are **not** hashed. The one real quantisation step is incidental. The dump writes every float with `%g`, which is 6 significant digits (`SdfCanvas.cpp:296, 308-315`). FrameRender then `sscanf`s the text back (`:94-101`) and hashes the *reparsed* floats. Sub-1e-6 relative noise usually disappears, but values that sit on a rounding boundary still flip. The filter view (curves through libm) hashes `cc3061c4a5c08734` on arm64 and `250dfa2fe8d69a2c` on x86_64.
  - **Live-geometry exclusion is a layout heuristic.** `inRank = y0 >= 165 && y1 <= 295 && !text && !seg`, and a cap is `inRank && d0[3] <= 2.0` (`:122-127`). The Rank's energy caps are skipped. FCompressor needs an explicit flag instead (see §5.9).
  - **Text gamma is hashed.** `d1[2]` of text is `th_.textGamma`, which is per theme (`BgfxEditor.cpp:1032`, `Theme.h:22,44`). The geometry hash is therefore *theme-dependent*, and `capture-frame.sh` pins `HRVB_UI_THEME=0`.
  - Extra metrics next to the hash: `static_count`, `text_count`, `rank_strokes` and `segments` (tol 0), `view_w/h` (0), `max_x/max_y` (±0.01). When the hash fails, these counts help explain what changed.
- The font fingerprint hashes the raw atlas bytes, which are `uint8` distance values (`FontProbe.cpp:34-39`). Only a change of face, subset, baker or FreeType moves it.

### 2.3 Choosing the golden set per architecture
`HRVB_RUN_ARCH` (`CMakeLists.txt:80-91`) is the arch the harnesses will *execute* as. A single-arch configuration runs as that arch (an x86_64-only build runs under Rosetta on Apple Silicon). A universal build runs as the host. Then (`:549-553`):
```cmake
if(HRVB_RUN_ARCH STREQUAL "arm64")  set(HRVB_GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tools/golden")
else()                               set(HRVB_GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tools/golden/${HRVB_RUN_ARCH}")
```
`Tools/golden/x86_64/` holds a **complete copy** of every file. Today it differs in 4 rows: iranalyse `order_32,_short_decay.fs96000.ir`, resetprobe `cross.order8_mid.48000` and `cross.order32_smear.48000`, and framerender-filter `layout.geometry`. There are also last-digit differences in 4 smearprobe rows that sit inside their tolerance. The two sets are kept in step **by hand** (`:546-548`).

### 2.4 `--check` against `--bless`, and how failures are reported
- `--check <file>` loads the golden and compares every golden row with the run. Missing key: `FAIL  <key> missing from this run (golden expects V)`. Exact mismatch: `FAIL  <key> expected A  got B`. Numeric: `FAIL  <key> expected b +/- tol  got a  (drift +d)`. A key produced by the run but absent from the golden: `FAIL  <key> not in the golden (new metric: --bless to adopt)`. The summary line is `PASS  N metrics match <path>` or `FAIL  k of N metrics differ from <path>`. Exit codes: 0, 1, and 2 when there is no golden.
- `--bless <file>` overwrites the file with the current measurement and its tolerance. The file is rewritten, not written through a temp file and rename. Nothing stops it: it ignores failures, it has no spec gate and no lock. The README makes blessing a human act ("read the diff before keeping it", 486-493). There was no git history, so "the golden file IS the history" (`Harness.h:16-21`).
- The **`verify` custom target** (`CMakeLists.txt:554-572`) chains `$<TARGET_FILE:HardwareReverb<T>> --check <dir>/<t>.txt` for IrAnalyse, ResetProbe, DuckProbe, SmearProbe and StateProbe, with `WORKING_DIRECTORY` set to the source dir. The Ninja generator joins a custom target's COMMANDs with `&&`, so **the first failing harness stops the chain** and later harnesses never report. The target depends on the five tool targets, so it builds them first.

---

## 3. Testing the GUI without a screen, as HR does it

**HR's editor has no headless mode.** The README says so (459-461) and so does `FrameRender.cpp:5-7`. "Offscreen" in HR means *the rasterising and fingerprinting* are offscreen. The frame itself is produced by the **real editor in the real Standalone app on a real Metal surface**.

### 3.1 Why the drawing code cannot run without a GPU today
- `SdfCanvas` is built from a `BgfxContext&` (`SdfCanvas.h:21`). `begin()` calls `bgfx::setViewFrameBuffer/setViewRect/setViewClear/setViewMode/touch` (`SdfCanvas.cpp:70-82`). `end()` allocates a transient VB and calls `bgfx::submit` (`:321-342`).
- `text()` returns early unless `ctx_.hasFont()` (`:209`). That is `fontBaked_ && bgfx::isValid(fontTex_)` (`BgfxContext.h:56`), so text needs a GPU texture even though the atlas is baked on the CPU.
- The editor creates the canvas each frame inside `submitFrame` after `BgfxContext::get().valid()` (`BgfxEditor.cpp:1026-1036`). Drawing is done by member functions of the `AudioProcessorEditor`: `drawFrame(SdfCanvas&)` → `drawHeader/drawDisplay/drawRank/drawFilterView/drawModulationView/drawControls/drawFooter` plus `PresetPanel::drawStrip/drawBrowser` (`:1068-1087`, `BgfxEditor.h:93-134`, `PresetPanel.h:37-38`). They read state that `submitFrame` eases with **wall-clock `dt`**.
- `submitFrame` returns before drawing if `!isShowing()` (`:838`) or there is no surface (`:840-873`). `TypeScale.h:8` includes `SdfCanvas.h`, so even the type scale depends on bgfx.

### 3.2 The capture path
1. **Canvas dump.** `HRVB_CANVAS_DUMP=<path>` with an optional `HRVB_CANVAS_DUMP_AFTER=n` is read in the editor constructor (`BgfxEditor.cpp:136-141`). It calls `SdfCanvas::dumpNextFrameTo(path, n)`, a process-global one-shot (`SdfCanvas.cpp:271-284`), and `FramePump::get().forceFallbackClock()` (`FramePump.cpp:117-125`). The display link stops while the window is occluded, so the dump must not depend on visibility (README 442-445). In `end()`, before submit, frame *n* is written as text (`SdfCanvas.cpp:290-319`):
   ```
   clear 16171a
   view 880 520 dpi 2 clock timer fps 0.0 rate full
   p x0 y0 x1 y1  c0 %08x c1 %08x  d0 a b c d  e0 u v  d1 a b c d  d2 bw soft kind 0
   ```
   The line holds vertex 0 (TL) and vertex 2 (BR) of each 6-vertex quad, which assumes the quad is axis-aligned. `kind` = `d2[2]`: 0 rrect, 1 text, 2 segment.
2. **Other environment hooks.** These are read only by the editor and do nothing in a host (README 588-600):
   - `HRVB_UI_THEME=0|1`: capture palette, not persisted (`:152-153`, polled at `:826`).
   - `HRVB_UI_SCALE=s` with `_AFTER=n`: fakes the backing scale (`:712-723`).
   - **`HRVB_UI_KEYS=tab,shift+tab,up,...`**: key replay runs once before the first frame, through the real `keyPressed(juce::KeyPress(code, mods, text))` (`:877-886`, `replayKeys :1914-1956`). Named keys are tab, up, down, left, right, escape, return, backspace, delete and space. A single character is also accepted. Modifier prefixes are `shift+`, `cmd+` and `alt+`.
   - `HRVB_UI_BROWSER=1`: opens the preset browser with no ease (`PresetPanel.cpp:486-490`).
   - `HRVB_UI_VIEW=filter|mod`: pins the band view (`:998-1010`).
   - `HRVB_PREFS_DIR` (`UiPreferences.cpp:34-38`) and `HRVB_PRESETS_DB` (`PresetStore.cpp:21`) are sandboxes.
3. **Accessibility dump.** `HRVB_A11Y_DUMP=<path>` is written once, right after key replay, on the first frame. It cannot be written in the constructor because JUCE creates a handler only for a component that has a window (`:889-894`). `dumpAccessibilityTo` (`:478-495`) writes one line per **visible** item: role int (padded 2), title (padded 18), bounds (padded 20), then `value=<string>` or `checked` / `unchecked`. The items are invisible `juce::Component`s built from the same hit rectangles the painter uses (`:426-452`). **No golden gates the a11y dump or the key replay.** `verify-gui` uses only `HRVB_UI_VIEW` and `HRVB_UI_BROWSER`.
4. **`Scripts/capture-frame.sh <build-dir> <out.dump>`** (28 lines):
   - Finds `HardwareReverb_artefacts/**/Standalone*/MacOS/HardwareReverb` (`:13`).
   - Creates a scratch `HRVB_PRESETS_DB` in `mktemp -d`, removed by a trap (`:20-23`).
   - Launches `HRVB_CANVAS_DUMP=$OUT HRVB_CANVAS_DUMP_AFTER=8 HRVB_UI_THEME=0 "$APP" &` (`:24`).
   - Polls 40 × 0.5 s, a **20 s maximum** (`:26`), then kills the app. It exits 1 with "no frame captured" if there is no window server or the GPU path never came up.
   - Frame 8 is chosen because the first-run hint is still drawn at that point; only its alpha fades, and alpha is not hashed. It does not set `HRVB_PREFS_DIR`, so the Standalone reads the real preferences file. The theme override does not persist.
5. **`verify-gui`** exists only when `NOT HARDWAREREVERB_CLASSIC_GUI` (`CMakeLists.txt:582-606`). It runs FontProbe, then PrefsCheck, then 4 × (capture → `FrameRender --fingerprint dump --check framerender*.txt`) for the default, `HRVB_UI_VIEW=filter`, `HRVB_UI_VIEW=mod` and `HRVB_UI_BROWSER=1` states. It depends on `HardwareReverb_Standalone`. It is kept out of `verify` because it needs a window server (`:524, 578-581`).

### 3.3 What this means for how FCompressor's editor code is structured
HR's approach works, but it only works on the logged-in desktop. It launches a whole app, which may trigger a microphone TCC prompt (`MICROPHONE_PERMISSION_ENABLED`, `CMakeLists.txt:202-203`) and touches JUCE's shared Standalone settings file. It relies on wall-clock easing and hard-coded layout heuristics, and a capture can take up to 20 s. For parallel agents that must verify UI changes, FCompressor should make drawing **callable with a canvas, a font and a fixed `dt`, with no window, no Component peer and no GPU**:

1. **Split `SdfCanvas` into a recorder and a sink.**
   - `sdfui::Canvas` is a CPU-only recorder. It produces a `PrimList` (`std::vector<Prim>`: kind, geometry fields, colours, `tag`, `live` flag).
   - Its only dependency is a `const FontAtlasSdf&` for metrics and UVs. `FontAtlasSdf` is already CPU-only: `FontAtlasSdf.h:8-11`, and FontProbe bakes it in a console process.
   - `hasFont()` should test the CPU atlas (`atlas.baked()`), not the GPU texture.
   - `BgfxSink::submit(const PrimList&, view, fb, uniforms)` does the bgfx calls from `begin()`/`end()`.
   - The dump becomes `PrimList::writeText()`, used by both the live app and the harness.
2. **Keep all layout, state and drawing in a `Panel` that does not derive from `juce::Component`.**
   - `Panel(ProcessorFacade&, const ModeRegistry&)` with `layout()`, `tick(float dt)`, `draw(sdfui::Canvas&, const Theme&)`, and input as plain structs: `pointerDown/Drag/Up(x, y, mods)`, `wheel(dy)`, `key(code, mods, ch)`. It also needs `accessibilityModel()` returning a list of `{role, title, bounds, valueText, checked}` and `setView(View)` / `setMode(key)` as API calls rather than env vars.
   - The `EditorShell` (the `AudioProcessorEditor` plus GPU surface, FramePump, OS a11y bridge; see A-gui-stack.md §6.5) owns one Panel. It forwards events and turns the a11y model into JUCE handlers.
   - Every eased value must converge deterministically with a fixed `dt`. The headless harness calls `tick(1/60.f)` k times, then `draw`.
3. **No `getenv` in drawing code.** Capture overrides go through an `sdfui::CaptureConfig` read once. A's §6.4 gives the env-var prefix `FCMP_`. The headless harness sets the same fields directly.
4. **Semantic tags and axes in the dump,** so geometry can be checked against DSP maths (§5.9). A per-primitive `tag` (uint16) and a `live` bit (A proposes `d2.w`) are needed, plus `axis <tag> x0 x1 v0 v1 y0 y1 w0 w1` lines for every plot.
5. The GPU path is still exercised by an optional `verify-gui-live` (a HR-style capture). It becomes a smoke test, not the main layout gate. The headless recorder output and the live capture must produce the **same geometry hash** for the same state, and that equality is itself a test.

---

## 4. Suite duration and build configuration

Measured on this machine (Apple **M5**, 10 cores) on 2026-09-22. The Release `build-dsp` binaries were copied to the scratchpad and run one after another with `--check`:

| Harness | Wall time | Result |
|---|---|---|
| IrAnalyse | 7.52 s | PASS 99 |
| DuckProbe | 3.78 s | PASS 61 |
| SmearProbe | 3.48 s | PASS 29 |
| ResetProbe | 1.84 s | PASS 40 |
| StateProbe | 0.78 s | PASS 34 |
| **`verify` total** | **≈17.4 s** sequential. The `build-dsp/.ninja_log` records one earlier `CMakeFiles/verify` step at 9.08 s wall, so machine load explains roughly 2× |
| FontProbe | 0.27 s | PASS 6 |
| FrameRender `--fingerprint` (×3 dumps) | 0.03–0.22 s each | PASS 9 each |
| FrameRender PNG (filter, ss=2 → 1760×1040) | 0.25 s | n/a |
| capture-frame.sh | not run (it would launch HR's Standalone). The bound is 20 s per capture, 4 captures in `verify-gui` | — |

**Build configuration.**
- `verify` works in the **bgfx-free configuration** (`-DHARDWAREREVERB_CLASSIC_GUI=ON`). The DSP harnesses are unconditional (`CMakeLists.txt:420-436`). StateProbe compiles the processor with the classic editor in *both* configurations because `HARDWAREREVERB_BGFX_GUI` is defined only on the plugin target (`:438-446`).
- `build-dsp/` is exactly that configuration: `CLASSIC_GUI=ON`, `Release`, `INSTALL_AFTER_BUILD=OFF`, `FETCHCONTENT_SOURCE_DIR_JUCE=<HR>/build/_deps/juce-src`, `FETCHCONTENT_FULLY_DISCONNECTED=ON`. It takes 203 MB against 4.2 GB for the bgfx `build/`.
- `verify-gui` needs the bgfx configuration plus the Standalone plus a window server.

**Cold build cost**, from `build-dsp/.ninja_log`:
- The 4 DSP tools that use only juce_core were ready 17.3 s after start. The critical path is `juce_core.mm` at 15.6 s, **compiled once per target**. `FDNReverb.cpp` takes about 2.0 s per target and each link about 1.4 s.
- StateProbe finished at 106.5 s. It compiles its own copy of `juce_gui_basics.mm` (45.3 s), `juce_graphics_Harfbuzz.cpp` (34.2 s) and `juce_graphics.mm` (29.8 s), and its **LTO link takes 37.9 s**.
- Every `juce_add_console_app` recompiles the JUCE modules it uses. In `build/`, bgfx adds `spirv-cross` TUs of 28–38 s each for shaderc.

What this means for FCompressor: (a) keep the DSP static library free of JUCE so probes build in seconds; (b) use **few harness executables with subcommands**, so JUCE compiles and LTO-links once per binary, not once per probe; (c) run it under CTest so a failure does not hide the rest.

---

## 5. Proposed FCompressor verification suite

### 5.0 Principles carried over from HR, and what changes
Carried over as they are:
- The FP mode matches `processBlock` (`ScopedFtz`).
- One flags interface target is shared by the plugin and the probes.
- Golden files are source and blessing is deliberate.
- Control-instance differencing.
- Ratio gates against the same machine.
- The "click" metric: energy above 8 kHz in ±2 ms around an edge, test against control, on a **110 Hz** sustained tone. It is the discriminator StateProbe validated: +42.8 dB for a hard cut against +0.3 dB for a 20 ms ramp (README 559-563).

New:
1. **Two kinds of check.** A *spec* check compares a measurement with the Mode's **declared analytic value**, and `--bless` cannot overwrite it. A *golden* check is drift detection, as in HR. A new Mode is held to its declared curve and time constants **before any golden exists**.
2. **The suite is table-driven over the Mode registry.** Every probe loops over `modeRegistry()`. Per-Mode golden files mean that adding a Mode never rewrites another Mode's goldens.
3. **Arch-robust assertions carry the load.** Compressors put `exp`/`log10`/`pow` on the signal path (dB↔linear in every gain computer), so exact output hashes will differ between arm64 and x86 far more than HR's did. The assertions that matter are tolerance-based measurements. Exact hashes are kept as refactor-neutrality proofs in a **per-arch overlay** (§5.12).
4. The suite runs under CTest, with every probe × Mode as its own test, labels, `-j`, and no `&&` chain.

### 5.1 The Mode contract the probes consume
It lives in the DSP library. It is pure C++ with no JUCE, and is shared by the DSP, the characteristics screen and the probes:
```cpp
namespace fcmp {
enum class DetectorLaw  { peak, rms };                 // what "input level" means for the static curve
enum class TimeLaw      { expDb, expLin, t10_90 };     // how the declared attack/release time is defined
enum class LinkLaw      { max, mean, sum, independent };
enum class Rigor        { clean, modelled, character };// selects the tolerance row in §5.13
enum class CurveFamily  { textbook, custom };          // textbook = Giannoulis/Massberg/Reiss 2012 hard/soft knee

struct Detents { std::span<const float> values; std::span<const char* const> labels; }; // strictly increasing
struct Constraint { bool stepped; float lo, hi; Detents detents; };                      // per shared parameter

struct TimeSpec { double seconds; TimeLaw law; double lo = 0, hi = 0; };   // lo/hi: published range for character Modes

struct ModeSpec {
    std::string_view key;              // stable forever, [a-z0-9-]{1,24}: "vca", "fet-76", "opto-2a", "bus-g" ...
    std::string_view displayName;
    int  introducedInSchema;
    DetectorLaw detector; LinkLaw link; Rigor rigor; CurveFamily family; bool hasColour;
    Constraint threshold, ratio, knee, attack, release, makeup /*, ... every shared param */;
    double   (*staticGainDb)(double levelDb, const ParamSnapshot&);   // steady-state gain; the ONE curve
    TimeSpec (*attackSpec)(const ParamSnapshot&);
    TimeSpec (*releaseSpec)(const ParamSnapshot&);
    int      (*latencySamples)(double fs, int osFactor, const ParamSnapshot&);
    double   ctBudgetNsPerSample;       // for the bench's loose budget
};
std::span<const ModeSpec> modeRegistry();   // built from Source/modes/Modes.def (X-macro), see 5.14
std::span<const std::string_view> retiredModeKeys();
}
```
**Test tap.** The probe build, and only the probe build (`FCMP_TEST_TAP=1`, a compile definition on the probe targets' copy of the processing code, or a template policy), can record the per-sample applied gain in dB and the detector level. The shipping plugin never compiles the tap.

### 5.2 DSP probe D1: static curve against the analytic transfer function (knee included)
- **Signal.** A 1000 Hz sine. A 100 ms window is N = fs/10 samples, which is an **integer number of cycles at all six rates** (44.1/48/88.2/96/176.4/192 kHz), so a single-bin DFT has no leakage. Gain = 20·log10(|Y(f0)|/|X(f0)|). Using the fundamental only means colour or saturation harmonics do not bias the gain. Levels follow `detector`: peak = sine peak, rms = peak − 3.0103 dB.
- **Staircase.** −60 … +6 dBFS in 1 dB steps, plus 0.25 dB steps across [T − W/2 − 2, T + W/2 + 2]. The level goes **up**, so each step settles through the attack. Each step holds `max(0.3 s, 8·τA)`, then measures a 0.1 s window. The run uses release = the slowest detent (or ≥ 100 ms) so gain ripple at 2f0 stays below 0.01 dB. Ripple at fast release is a separate golden-only row.
- **Configurations for each Mode.**
  - Ratio: every ratio detent (stepped), or {1.5, 2, 4, 10, 20, ∞} clamped to the Mode's range (continuous).
  - Threshold: {−30, −10} dB.
  - Knee: {0, 6, 12} dB if the knee is continuous, the detents if stepped, or the fixed value.
  - Stepped Modes are measured **at each detent value exactly**.
- **Assertions.**
  - `max_err_db_outside_knee` and `max_err_db_in_knee`: measured against `staticGainDb` (spec, §5.13).
  - `ratio_meas`: ΔIn/ΔOut between T+10 and T+20 dB, compared with the declared ratio. For ∞ the spec is slope ≤ 0.01 dB/dB.
  - `thr_meas_db`: the input where GR first exceeds 0.1 dB, compared with the analytic value.
  - Golden rows for the curve points at T−10, T, T+10 and T+20.
- **Formula check.** For `family == textbook`, the Mode's `staticGainDb` is also compared with the probe's own **independent** textbook implementation to 0.001 dB:
  `y = x` if 2(x−T) < −W;
  `y = x + (1/R − 1)(x − T + W/2)²/(2W)` if 2|x−T| ≤ W;
  `y = T + (x−T)/R` if 2(x−T) > W.
  Without this second implementation a bug in the shared function would pass both the DSP check and the screen check.
- **Meter truth.** With the tap: tap GR and audio-derived GR must agree within 0.01 dB for clean Modes. This proves the GR the UI shows is the GR applied.

### 5.3 DSP probe D2: attack and release time constants against spec
- **Signal.** A 1 kHz sine at T−20 dB for 0.5 s (GR = 0), then a step to T+20 dB held for `max(0.5 s, 10τA)`, then back to T−20 held for `max(1 s, 10τR)`. Character Modes use 5 s to see program dependence.
- **Gain trace.**
  - Gain-only Modes use the exact ratio `g[n] = y[n+L]/x[n]` on samples where |x| > 0.5·A (L = reported latency), interpolated linearly across the gaps.
  - Otherwise the tap is used. For clean Modes the tap and the audio-derived trace must agree within 0.01 dB, which qualifies the tap.
- **Extraction.** Per `TimeLaw`: `expDb` gives t63 of the GR change in dB, `expLin` gives it in linear gain, and `t10_90` gives the 10→90 % time (= τ·ln 9 for a one-pole). Attack comes from the up-step and release from the down-step.
- **Configurations.** Every attack/release detent (stepped), or min / mid / max (continuous). Auto or program-dependent release adds t50 and t90 after 1 s and after 10 s of GR as golden rows (±10 %) and a spec range check against `[lo, hi]`.
- **Tolerance** is `max(rel_tol·τ, 1.5/fs_eff)`. The absolute term matters for FET-style 20 µs attacks, which are 0.88 samples at 44.1 kHz without oversampling.

### 5.4 DSP probe D3: stepped-parameter quantisation
For each Mode, and each shared parameter that is `stepped` in that Mode:
1. **Sweep** the host-normalised value 0→1 in 1/4096 steps. The effective value (through the Mode's `ValueModel`, read by the tap or by a pure `effectiveValue(norm)` function) must be **bit-equal** to an entry of `detents.values`. It must be monotone non-decreasing and reach every detent. The transitions must sit at the declared boundaries (midpoints, or as documented with hysteresis). Metrics: `off_detent_count = 0`, `unreached = 0`, `nonmonotone = 0` (spec).
2. **Text.** `getText(norm)` equals the detent label. `getValueForText(label)` maps back to the same detent (`text_mismatch = 0`).
3. **DSP agreement.** D1 and D2 already run *at each detent*. D3 also runs D1 once at a normalised value **between** two detents and asserts the measured ratio equals the snapped detent, not an interpolated value.
4. **Continuous control group.** In a Mode where the parameter is continuous, a random value passes through unchanged (`|eff − raw| ≤ 1 ulp`).
5. **Special detents** such as an "all-buttons" 1176 setting are their own `ratio` entry with a `custom` curve. They are covered by D1 through `staticGainDb`.

### 5.5 DSP probe D4: clicks and discontinuities on a Mode switch
- **Setup.** Three instances on identical input: T switches A→B at t = 1.0 s (the Mode parameter is set between blocks, as a host does); CA stays in A; CB stays in B. The input is a 110 Hz sine at −6 dBFS. Threshold −20 dB, ratio nearest to 4, defaults otherwise. **All ordered pairs** A→B: the N controls run once each, then N(N−1) test runs.
- **Assertions.**
  - `hf_ratio_db` = 10·log10(E>8k(T, ±2 ms) / max(E>8k(CA), E>8k(CB))), spec ≤ +3 dB.
  - `level_dev_db`: in 5 ms RMS windows from −20 ms to +100 ms, T must stay within [min(CA,CB) − 1 dB, max(CA,CB) + 1 dB].
  - `nonfinite = 0`.
  - `latency_constant = 1`: the reported latency does not change across the switch, and the measured input→output lag at +200 ms equals the lag before the switch. A switch must never be a time jump. This forces a policy: either a fixed plugin latency (the maximum over Modes, with padding) or a documented re-latency.
  - `settle_ms`: the time until T nulls against CB to ≤ −60 dB. Golden, ±10 %.
- **Cost** at N = 12 is about 132 runs × 2 s. It stays under about 2 s because compressor DSP is cheap.

### 5.6 DSP probe D5: parameter-automation zipper
For each Mode and each parameter the Mode leaves continuous (threshold, ratio, knee, attack, release, makeup, mix, output, sidechain HPF):
1. **Step edge.** The 110 Hz tone at −6 dBFS with GR about 6 dB and attack/release at their *slowest*, so nothing legitimate is fast. Set the parameter 25 % → 75 % in one block at t = 1 s against a control that starts at 75 %. Spec: `hf_ratio_db ≤ +3 dB`. This catches parameters applied after the ballistics without smoothing, for example a threshold that feeds an unsmoothed gain.
2. **Block-rate ramp.** A 0→1 linear ramp over 2 s, set once per block at bs = 512, against the same ramp set per sample (bs = 1). A 1 kHz tone at −12 dBFS. `err_db` = energy(y512 − y1)/energy(y1). Golden, with a spec cap of ≤ −40 dB. The line at f0 ± fs/bs is reported.
3. **Block-size invariance with static parameters.** A settled render at bs {1, 17, 64, 128, 512, 4096} must produce identical output: `bs.mismatches = 0` exact, which is IrAnalyse's invariant (`:393-430`). It needs per-sample smoothing with coefficients derived from fs and no block-rate decisions. It is cheap and catches hoisting bugs.

### 5.7 DSP probe D6: state and preset round trip, including version migration
This extends StateProbe.
1. **Round trip.** For every Mode: select the Mode, set every parameter to a distinct non-default value snapped through its range (StateProbe `:373-374`), save, restore into a **non-fresh** instance, and compare bitwise. The Mode is included, stored by **key string** and never by index.
2. **Absent children.** A blob with the newer parameter children removed restores them to their defaults (StateProbe `:401-445`).
3. **Migration fixtures.** Real state blobs from every released schema version live in `Tools/fixtures/state/<schema>-<mode>-<desc>.bin`. They are write-once and never regenerated. `Tools/golden/state-fixtures.txt` gives each fixture's expected parameter values and Mode key, plus a render fingerprint (tolerance-based, see §5.12).
4. **Mode-parameter mapping is stable.** A host automates the Mode parameter by *normalised value*. If it is an `AudioParameterChoice`, adding a Mode changes `index/(N−1)` and every old automation lane lands on a different Mode. The probe keeps a fixture table of (normalised → key) for each released version and asserts it still resolves. This effectively requires an **append-only index table with a fixed maximum** (for example `AudioParameterInt 0..127`, unused slots mapping to the default) or an equivalent design. **Architecture decision needed.**
5. **Removed Modes.** An unknown or retired Mode key in state loads the Mode's declared successor (from `retiredModeKeys()`) and sets a "migrated" flag. It never crashes and never silently picks index 0.
6. **Text round trip** for every parameter, and for stepped parameters the detent labels (StateProbe `:447-488`).
7. **Presets.** If HR's preset stack is reused, PresetProbe-style checks run against a scratch DB (`FCMP_PRESETS_DB`).

### 5.8 DSP probes D7–D12

| Probe | Method | Assertions |
|---|---|---|
| **D7 Latency report** | For each Mode × OS factor {1,2,4} × lookahead {0, max} × fs: reported latency comes from `getLatencySamples()` after `prepareToPlay`. The measurement uses a −40 dBFS impulse (below threshold, so GR = 0) and takes argmax\|y\|. Min-phase oversampling filters use band-limited noise (20 Hz–10 kHz) with cross-correlation and a parabolic peak | `lat_err_samples = 0` for integer designs, ≤ 0.5 for fractional. Constant across Mode switch (D4). A change goes through `setLatencySamples` in prepare only |
| **D8 Null tests** | (a) mix = 0: out = in delayed by L. (b) Input 20 dB below threshold−knee/2 for a Mode without colour: GR is exactly 0 dB. (c) ratio 1:1 where the Mode allows it. (d) Bypass engaged, and the bypass edge on the 110 Hz tone | (a) `max_err = 0`, **bit-exact**, which also proves dry-path latency compensation. (b) and (c): the tap's GR is exactly 0.0 and the output is bit-exact for clean Modes. For colour Modes, a golden THD at −10 dBFS ±0.5 dB instead. (d) `max_err = 0` and `hf_ratio_db ≤ +3` (StateProbe `:602-703`). (e) mix = 0.5 equals 0.5·(mix0 + mix1) within ≤ −120 dB |
| **D9 Stereo link** | L = sine at T+12 dB, R silent or −40 dB, at link 0, 50 and 100 %. Also swap L and R. Also mono-in/mono-out against a stereo run with L = R | Link 0: R's GR = 0. Link 100: \|GR_R − GR_L\| ≤ 0.01 dB (clean) under the declared `LinkLaw`. 50 %: according to the declared law. Symmetry: swapped input gives swapped output, **bit-exact**. Mono equals the L channel of L=R stereo, bit-exact |
| **D10 Hostile input** | ResetProbe's table on a running compressor next to a control: NaN, +inf and 1e30 as one sample and as a whole block, on the **main and sidechain** inputs. DC 0.5 for 2 s. +40 dBFS. Silence after a burst. processBlock before prepare, 0-length blocks, and "declared 64 fed 1024" | `nonfinite_out = 0`. Tail within 1 dB of control 100 ms later. Recovery ≤ 1 block. DC: GR follows the declared behaviour (sidechain HPF → 0 GR; otherwise GR = staticGain at −6.02 dB). Silence → output **exactly 0** for Modes without colour. Denormals: with `ScopedFtz` **off**, the per-block time on the post-burst silence tail is < 2× normal (a same-machine ratio, loose margin) |
| **D11 Sample-rate sweep** | D1 (a reduced grid) and D2 at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz. Robustness only at 22.05k and 384k | τ(fs)/τ(48k) = 1 ± the class tolerance. Static curve against 48k within ±0.02 dB (clean). Sidechain HPF fc within ±1 %. Latency in ms consistent |
| **D12 Real-time safety and isolation** | The probe TU replaces the global `operator new`/`delete` with a counter armed around each `processBlock`, across every Mode, Mode switch and parameter sweep. Two instances with different Modes processing interleaved, against each alone. Two identical runs | `rt.allocs = 0`. Isolation: bit-exact, which catches shared statics in the registry or lazily built tables. Determinism: identical hashes, which catches uninitialised state |

**CPU bench** (outside `verify`): each Mode × {48k/128, 96k/64, 192k/32} × OS {1,2,4}. It measures ns/sample and % of one core, with the signal generated outside the timed region (`DspBench.cpp:28-36`). It reports only. `bench --budget` compares each Mode with `ctBudgetNsPerSample × 3` and should be run alone, never while other agents are compiling.

### 5.9 GUI probes (headless, based on §3.3)
The probe is a harness `FcmpUiProbe`. It sets up a processor (prepared, fed a fixed test signal where live data matters) and a `Panel`. It ticks at a fixed dt of 1/60 until the Panel's eases converge. It draws into the recorder and checks.

| Probe | What / method | Assertions |
|---|---|---|
| **G1 Geometry fingerprint per view** | View ∈ {main, characteristics/transfer, characteristics/GR history, characteristics/internals (detector, sidechain), preset browser, settings} × every Mode × dpi {1, 2}. The hash covers the fields HR hashes (§2.2) **minus text gamma**, and skips primitives with `live = 1` instead of using a y-band heuristic | Exact hash per arch, plus counts (static, text, segment, area) with tol 0 and extents ±0.01. **Theme invariance**: hash(theme 0) == hash(theme 1), exact. **Live/headless parity**: the hash from `verify-gui-live` for the default state equals the headless hash |
| **G2 Characteristics curve against the analytic function** | Primitives tagged `TRANSFER_CURVE` are mapped back through their `axis` line to (inDb, outDb) | Each vertex: \|y − axisY(inDb + staticGainDb(inDb))\| ≤ 0.5 px. The endpoints span the full axis. **Chord error** (the maximum deviation of the true curve from each drawn chord, sampled at 8 points) ≤ 0.25 px, which forces dense sampling in the knee. `THRESHOLD_MARK` x = axisX(T) ± 0.5 px. The ratio label equals the detent label |
| **G3 Operating point and meters tell the truth** | A fixed tone at level L for 1 s, then draw | `OP_DOT` centre within 1 px of (axisX(L), axisY(L + g_meas)), where g_meas comes from D1's method on the same run. The GR meter length equals the processor's GR ± 0.5 px. The GR history's newest column equals the tap GR ± 0.1 dB |
| **G4 Accessibility model** | `panel.accessibilityModel()` written as text in HR's dump format (§3.2.3) | **A golden text file compared line by line** (`Tools/golden/ui/a11y/<view>-<mode>.txt`), not a hash, so a failure is readable. A stepped parameter speaks its detent label and a detent group has radio roles |
| **G5 Key and pointer replay** | API calls: tab order, arrows on ratio, drag, wheel | In a stepped Mode an up-arrow moves **exactly one detent**, and wheel and drag land only on detents (with hysteresis as declared). Continuous Modes move by the declared step. The tab order is golden |
| **G6 Text-fit lint** | For every Mode × label × dpi: `textWidth(label) ≤ box.w` | `text_overflow = 0` (spec). This matters when Modes keep arriving with longer names or labels |

### 5.10 How a new Mode gets covered automatically
1. Adding a Mode is one line in `Source/modes/Modes.def`, e.g. `FCMP_MODE(fet-76, Fet76Mode)`, plus its source. The C++ registry is built from that X-macro, and CMake parses the same file:
   ```cmake
   file(STRINGS Source/modes/Modes.def FCMP_MODE_LINES REGEX "^FCMP_MODE\\(")
   foreach(l ${FCMP_MODE_LINES})
     string(REGEX REPLACE "^FCMP_MODE\\(([a-z0-9-]+),.*" "\\1" key "${l}")
     foreach(p static time quant switch zipper state latency null link hostile srsweep rt)
       add_test(NAME dsp.${p}.${key} COMMAND FcmpDspProbe ${p} --mode ${key} --golden-dir ${FCMP_GOLDEN_ROOT})
       set_tests_properties(dsp.${p}.${key} PROPERTIES LABELS "dsp;mode:${key};probe:${p}" TIMEOUT 120
                            ENVIRONMENT "FCMP_PREFS_DIR=${CMAKE_BINARY_DIR}/sandbox;FCMP_PRESETS_DB=${CMAKE_BINARY_DIR}/sandbox/p.db")
     endforeach()
     foreach(v ui.geometry ui.curve ui.truth ui.a11y ui.input ui.textfit)
       add_test(NAME ${v}.${key} COMMAND FcmpUiProbe ${v} --mode ${key} --golden-dir ${FCMP_GOLDEN_ROOT})
     endforeach()
   endforeach()
   ```
   `switch` covers "all pairs where this Mode is the source", so pair coverage grows automatically.
2. **Spec checks run immediately** for the new Mode. They come from its `ModeSpec`: curve, knee, τ, detents, latency, link, text fit. They do not depend on a golden.
3. **A missing golden file fails** with exit 2 and gives the exact bless command. Drift goldens are created on purpose by the integrator (§6).
4. **Registry lint** (`registry` test):
   - Keys are unique, match `[a-z0-9-]{1,24}`, and are never reused.
   - Detents are strictly increasing, labels are non-empty, and every function pointer is set.
   - `introducedInSchema` ≤ current.
   - Every Mode directory under `Tools/golden/modes/` belongs to a registered **or retired** key. Otherwise the test fails with "mode removed: old sessions would not load".
5. `ctest -L mode:fet-76` runs everything for one Mode, which is the inner loop of an agent that owns that Mode.

### 5.11 Harness v2 (`Tools/Harness.h`, FCompressor's own copy, `namespace fcmp::test`)
- Keep: `ScopedFtz`, `Metric`, `addNum`, `addHash`, the `--check` output format, and the exit codes 0/1/2.
- Add **`spec(key, measured, expected, tolSpec)`**. It prints `SPEC PASS/FAIL key measured expected tol`, exits 1 on failure whatever the golden says, and **makes `--bless` refuse to write**.
- Tolerance column grammar: `exact` | `<abs>` | `rel:<r>` | `le:<v>` | `ge:<v>` | `range:<a>:<b>`. It stays compatible with HR's files.
- Duplicate-key detection (FAIL). Keys are asserted free of whitespace and ≤ 120 chars.
- `--golden-dir <root>` + `--mode <key>` resolve `<root>/modes/<key>/<probe>.txt`, with an arch **overlay** (§5.12). This removes path drift.
- `--only <glob>` for fast iteration.
- A final machine-readable line: `RESULT {"probe":"static","mode":"fet-76","pass":true,"spec_fail":0,"golden_fail":0,"n":212}`.
- `--bless` writes to `<file>.tmp` and then `rename()`s it (atomic). It requires `FCMP_ALLOW_BLESS=1` for any path inside the source tree. `--bless-to <dir>` writes candidates anywhere, for agents.

### 5.12 Fingerprints, arches and float noise
- **`FcmpDspFlags` INTERFACE** (HR's pattern) plus `-ffp-contract=off` for the DSP library. HR had to split `sum += f*f` by hand because clang fused it on x86 and not through NEON (README 98-101). Turning contraction off removes that whole class of divergence. Intended FMAs go through the SIMD type explicitly.
- **Decision to make.** Use deterministic in-house `exp2`/`log2` approximations (plain `+ × ` polynomial, no libm) for dB↔linear on the audio path. They are faster and bit-identical across arches, so exact hashes could be shared. With libm, expect most `.hash` rows to live in the x86 overlay.
- **The overlay.** `Tools/golden/modes/<key>/<probe>.txt` is the arm64 base. `Tools/golden/x86_64/modes/<key>/<probe>.txt` holds **only the rows that differ**, and the loader merges base then overlay. HR's full copy needs manual syncing (`CMakeLists.txt:546-548`); the overlay does not.
- **Which checks gate.** Tolerance measurements (dB, τ, px) do the gating and pass on both arches. Exact hashes (`*.hash`) exist to prove refactor neutrality ("this change is arithmetically neutral").

### 5.13 Tolerance table (proposed defaults, by `Rigor`)

| Quantity | clean | modelled | character |
|---|---|---|---|
| Static curve error, outside knee | 0.05 dB | 0.25 dB | 0.5 dB |
| Static curve error, inside knee | 0.10 dB | 0.35 dB | 0.75 dB |
| Measured ratio against declared | rel 1 % | rel 3 % | rel 8 % |
| Textbook formula check | 0.001 dB | 0.001 dB | n/a (`custom`) |
| τ attack/release against spec | max(2 %, 1.5 smp) | max(10 %, 1.5 smp) | within `[lo,hi]` + golden ±10 % |
| τ(fs)/τ(48k) | 1 ± 0.02 | 1 ± 0.03 | 1 ± 0.05 |
| Static curve against 48k | ±0.02 dB | ±0.05 dB | ±0.1 dB |
| Tap GR against audio GR | 0.01 dB | 0.05 dB | 0.1 dB |
| Stereo link GR match | 0.01 dB | 0.05 dB | 0.1 dB |
| mix = 0 null, bypass passthrough | bit-exact | bit-exact | bit-exact |
| Below-threshold null | bit-exact | ≤ −120 dB | THD golden ±0.5 dB |
| Click (`hf_ratio_db`): Mode switch, zipper edge, bypass | ≤ +3 dB | ≤ +3 dB | ≤ +3 dB |
| Latency | exact (integer) / ±0.5 smp | same | same |
| Curve on screen against function | 0.5 px, chord 0.25 px | same | same |
| Allocations in `processBlock` | 0 | 0 | 0 |

### 5.14 Suggested targets and executables
- `FCompressorDsp` (STATIC, no JUCE): the Modes, detectors, gain computers, registry, and `FcmpDspFlags`. It is linked by the plugin **and** the probes, so the fingerprints cover the **same object code**, not just the same flags.
- `FcmpDspProbe` (links FCompressorDsp only): D1–D5, D7–D12 at engine level, and the registry lint.
- `FcmpProcProbe` (processor + `juce_audio_processors`, no GPU): D6, D7/D8 through `processBlock`, buses, bypass, chunking, and the Mode-parameter mapping.
- `FcmpUiProbe` (Panel + recorder + FontAtlasSdf + `juce_gui_basics`, **no bgfx**): G1–G6 and the font atlas gate. It runs in the GPU-free configuration.
- `FcmpFrameRender`: HR's FrameRender generalised. It is the PNG rasteriser for people and reads tags, `live` and `axis`.
- `FcmpBench`: not in `verify`.
- Targets: `verify` = `ctest -L "dsp|proc|ui|registry" -j8 --output-on-failure`; `verify-gui-live` = the HR-style Standalone capture parity check; `bench`.
- **Time budget target.** At 10 Modes, `verify` should take ≤ 30 s wall with `-j8` on an M-series machine. The estimate is about 3 s of DSP work per Mode (static about 0.8 s, τ about 0.2 s, sample-rate sweep about 1 s, the rest small). UI probes take under 1 s total, because recording a frame costs milliseconds.

---

## 6. Parallel agents: building and testing without colliding

1. **One build directory per agent, never shared:** `FCompressor/build/<agent>-dsp` (git-ignored). If FCompressor becomes a git repo (recommended), each agent works in its own **worktree**, and source edits cannot collide either. Goldens then get real diff history, which weakens HR's "the golden file is the history" argument (`Harness.h:16-21`) but keeps "bless is deliberate".
2. **The GPU-free configuration by default:** `-DFCOMP_GPU=OFF`. It builds everything except BgfxContext, NativeSurface, DisplayLink, FramePump and the shaders. It includes the Panel, the recorder and the UI probes. HR's equivalent is `build-dsp` (`CLASSIC_GUI=ON`), at 203 MB against 4.2 GB, with no bgfx or shaderc TUs of 28–38 s. Only the integrator or a GUI-owner agent builds `FCOMP_GPU=ON`.
3. **Share dependency sources read-only, and never re-clone them per build dir.**
   - Sprint 0 populates `FCompressor/.deps/juce` (tag 8.0.4) and `.deps/bgfx.cmake` once. Every agent configures with:
     `-DFETCHCONTENT_SOURCE_DIR_JUCE=<that> -DFETCHCONTENT_SOURCE_DIR_BGFX=<that> -DFETCHCONTENT_FULLY_DISCONNECTED=ON`.
   - This is exactly how HR's `build-dsp` is configured today (`FETCHCONTENT_SOURCE_DIR_JUCE=.../HardwareReverb/build/_deps/juce-src`).
   - **Do not point FCompressor at HR's `build/_deps`**: HR's owner may wipe or rebuild it.
4. **Never install from an agent build.** Set `-DFCOMP_INSTALL_AFTER_BUILD=OFF`. It mirrors HR's `HARDWAREREVERB_INSTALL_AFTER_BUILD` (`CMakeLists.txt:103-109`), which exists for this reason. `COPY_PLUGIN_AFTER_BUILD` would otherwise overwrite `~/Library/Audio/Plug-Ins/*` and confuse the AU registrar cache (README 114-117).
5. **Never bless.**
   - Agents run `--check` only. Harness v2 refuses to write inside the source tree unless `FCMP_ALLOW_BLESS=1`.
   - An agent that needs a new or changed golden runs `--bless-to $BUILD/golden-candidates` and reports the diff with its reason (for example, "D1 fet-76 knee rows moved 0.03 dB because of X").
   - The integrator reviews and blesses, one agent at a time. The rename-based atomic write prevents torn files.
   - Spec checks cannot be blessed at all.
6. **Sandbox all shared state.** CTest `ENVIRONMENT` sets `FCMP_PREFS_DIR` and `FCMP_PRESETS_DB` inside the build dir. No probe writes the real `~/Library/Application Support/FCompressor/*`, and PrefsCheck's sandbox pattern (`PrefsCheck.cpp:31-40`) becomes the rule for every probe.
7. **No window server in agent runs.** UI verification is headless (§3.3). `verify-gui-live` launches a Standalone, which brings TCC prompts, JUCE's shared Standalone settings file and window focus. It runs only on request and **serialised** with a lock (`/tmp/fcmp-gui.lock` taken by `flock`).
8. **Keep timing out of the gates.** With 4–6 agents compiling, per-process timing noise is large. Only same-machine *ratio* gates with a wide margin belong in `verify`, like ResetProbe's cost < 1 block, which was about 15× before its fix (`ResetProbe.cpp:164-168`). The bench stays out.
9. **Share the CPU.** Each agent builds with `cmake --build <dir> -j4`, or ninja `-l 10` to cap load, and runs `ctest -j4`. Ten cores shared by 5 agents at `-j10` each thrash, and one StateProbe-sized JUCE TU is already 30–45 s.
10. **Build JUCE once per executable, not once per probe.** HR's layout, one `juce_add_console_app` per tool, recompiled `juce_core.mm` (15.6 s) in each of 5 targets, plus an LTO link of up to 37.9 s per tool. The 3–4 probe executables in §5.14 keep an agent's cold build to roughly one StateProbe-sized build, about 100 s on this M5, and incremental builds to seconds.
11. **Pin the harness arch per configuration** as HR does (`HRVB_RUN_ARCH`), so an agent that configures `-DCMAKE_OSX_ARCHITECTURES=x86_64` (Rosetta) automatically gets the x86 overlay. Universal builds run as the host.
12. **Agent handoff contract.** "Done" means `ctest -L "mode:<key>|registry"` is green in the agent's own build dir, the `RESULT` JSON lines are attached, and blessing candidates are listed separately. The integrator's `verify` across all Modes decides whether the work merges.

---

### Appendix: numbers mentioned in this report
- HR golden rows gated by `verify`: 263 (99 + 40 + 61 + 29 + 34). GUI gate rows: 6 + 4 + 3×9.
- Frame sizes: default 450 static primitives (368 text, 39 rank strokes, 0 segments). Filter view 752 (381, 8, 320). Mod view 812 (380, 5, 384). View 880×520 at dpi 2, extent 842.5 × 508.189.
- Font metrics at kBasePx 48: cap 26.5455, x 20, ascent 37.0909, max digit advance 21.8182. Atlas 1024×512, spread 6.
- Arch differences today: 4 rows (3 DSP hashes plus the filter-view geometry), plus last-digit differences inside tolerance in 4 smearprobe rows.
