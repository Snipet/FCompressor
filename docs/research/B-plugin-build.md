# B: plugin, processor and build map (HardwareReverb, with FCompressor in mind)

Research date 2026-09-22. Source of truth: `/Users/seanfunk/audio/plugins/HardwareReverb` (read-only; abbreviated **HR** below).
Paths without a prefix are relative to the HR root. Line numbers are for the files as they were on 2026-09-22 around 19:50.

> **Warning: HR is changing right now.** `Source/presets/*`, `Source/PresetPanel.*`, `Source/gui/TagPalette.h`,
> `Source/gui/TypeScale.h`, `Source/PluginProcessor.h` and `CMakeLists.txt` were modified between 19:25 and 19:49 on
> 2026-09-22. Six files are marked `STUB`: `Source/presets/{PresetFile,FactoryPresets,PresetStore,PresetManager}.cpp`
> and `Tools/{PresetProbe,BankProbe}.cpp`. FCompressor should **snapshot-copy** HR code. It should never reference HR
> source files from its own CMake, and it should treat the preset *headers* as the contract, not the `.cpp` files.

---

## 1. Parameter system

### 1.1 Mechanism
- **Plain JUCE APVTS.** `juce::AudioProcessorValueTreeState apvts;` is a **public** member (`Source/PluginProcessor.h:49`).
  It is constructed with no UndoManager and tree type `"PARAMS"`: `apvts(*this, nullptr, "PARAMS", createLayout())`
  (`PluginProcessor.cpp:210`). The layout comes from `static ParameterLayout createLayout()` (`.h:59`, `.cpp:85-204`).
- **Parameter IDs** live in `namespace pid` as `static constexpr const char*` (`.cpp:11-41`), plus a `pid::all[]` array
  (`.cpp:37-40`) that `setStateInformation` uses to reset absent parameters.
- **No APVTS listeners on the audio path.** The processor caches `std::atomic<float>*` from
  `apvts.getRawParameterValue(id)` in a `Raw raw_` struct (`.h:99-118`, filled at `.cpp:212-228`). It polls them with a
  relaxed `load()` at the top of every block (`pushParametersToEngine()`, `.cpp:260-278`, called from `render` at `.cpp:358`).
  The comment at `.h:92-98` gives the reason: the listener route takes two per-parameter pthread mutexes without priority
  inheritance on every automation point. **Copy this pattern.** GlueCompressor uses the listener route, which HR rejected.

### 1.2 Parameter table (`PluginProcessor.cpp:85-204`)
| id | type | range / step / skew | default | ParameterID version | host text |
|---|---|---|---|---|---|
| `order` | `AudioParameterChoice` | `{"4","8","16","32"}` | index 1 (= 8) | 1 | choice names |
| `size` | Float | 0..1, 0.0001 | 0.5 | 1 | `percentAttributes()` → `"%"` |
| `modSpeed` | Float | 0..8, 0.001, **skew 0.4** | 0.3 | 1 | label `Hz` |
| `modAmount` | Float | 0..32, 0.01 | 2.0 | 1 | label `smp` |
| `decay` | Float | 0..1, 0.0001 | 0.6 | 1 | custom: RT60 seconds, 2 decimal places; parses seconds back through the same exponential (`.cpp:113-126`) |
| `damp` | Float | 0..1 | 0.35 | 1 | % |
| `mix` | Float | 0..1 | 0.35 | 1 | % |
| `preDelay` | Float | 0..200, 0.1, **skew 0.5** | 0 | 2 | `ms` |
| `diffusion` | Float | 0..1 | 0 | 2 | % |
| `smear` | Float | 0..1 | 0 | **3** | % |
| `bassMult` | Float | 0.25..2, 0.001, `setSkewForCentre(1.0)` | 1.0 | 2 | `x` |
| `xover` | Float | 100..2000, 1, `setSkewForCentre(500)` | 500 | 2 | `Hz` |
| `width` | Float | 0..2, 0.001 | 1.0 | 2 | `String(v, 2)` |
| `loCut` | Float | 0..500, 1, `setSkewForCentre(120)` | 0 | 2 | `Hz` |
| `hiCut` | Float | 1000..20000, 1, `setSkewForCentre(6000)` | 20000 | 2 | `Hz` |
| `freeze` | `AudioParameterBool` | | false | 2 | |
| `bypass` | `AudioParameterBool` | | false | **4** | |

- The helper `percentAttributes()` (`.cpp:65-77`) is
  `AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(roundToInt(v*100)).withValueFromStringFunction(trim "% ", *0.01)`.
  Without it JUCE derives decimal places from the interval, and a host lane would read `0.3500`.
- **Versioning:** `ParameterID{ id, N }` hints are 1 for the original set, 2 for the advanced set, 3 for `smear` and 4 for
  `bypass`. Every newer parameter has a **neutral default**, so older sessions restore bit-identically
  (`.cpp:21-35`, `.cpp:149-150`, `.cpp:198-199`).
- **Units:** the host sees plain units. Values that need engine knowledge (Size in ms, the Decay dB/pass sub-readout)
  are printed only by the editor. For Decay, the host text duplicates the engine's map `rt60 = 0.2 * 150^v`
  (`.cpp:48-54`, mirroring `FDNReverb.cpp:68-69`).

### 1.3 How the stepped Order (4/8/16/32) is modelled
- Host side, it is an `AudioParameterChoice` whose raw atomic holds the **index** 0..3. The engine converts it with
  `reverb.setOrder(1 << (2 + (int) raw_.order->load()))` (`.cpp:262`). The engine applies the request on the audio thread
  (`requestedOrder_` atomic, `FDNReverb.h:116-118`), with a warm-up for lines that rejoin.
- UI side, it is not a slider. It is **four segmented cells** laid out at x = 640 + i·52, y = 20, 48×32
  (`BgfxEditor.cpp:224-226`). A click sets `p->convertTo0to1((float) cell)` inside begin/endChangeGesture
  (`BgfxEditor.cpp:1709-1720`). Each cell is also an accessible button (`BgfxEditor.cpp:440`, `press()` at about `:292-300`).
- **Consequence for FCompressor:** a Choice parameter has a fixed option list, so it only works for a stepped set that
  never changes. A "2-4-10 ratio" that exists only in some Modes needs a different model (§7.4).

### 1.4 Smoothing
- **Processor:** no smoothing. Each block it stores raw values into engine setters, which are relaxed atomic stores with
  a clamp (`FDNReverb.cpp:265-281`).
- **Engine, block-rate one-pole:** `tau = kSmoothTauSec = 0.03 s` (`FDNReverb.cpp:75`). The coefficient is recomputed
  from the **actual block length** whenever the block length changes: `a = 1 - exp(-n / (tau * fs))` (`:643-649`).
  This keeps glide time the same in wall-clock terms at any buffer size, so offline renders match realtime, and a gate
  enforces it (IrAnalyse renders at block sizes 1, 17, 64, 128, 512 and 4096 and requires bit equality).
- **Per-sample ramps** within a block for signal-facing values (mix, depth, diffusion, smear, width): the block holds
  start values and increments `(end - start) / n` (`:651-657`, `:852-859`).
- **`smoothSnap(value, target, eps)`** lands exactly on the target once within eps (`:681-704`). Without it a one-pole
  stalls a few ulps short, which would defeat exact-neutral bypass values.
- **Snap on recall:** `requestParameterSnap()` sets an atomic flag (`:290`). The next `processBlock` runs
  `exchange(false, acquire)` → `snapSmoothedParameters` (`:661-663`). It is used by `setStateInformation` (`PluginProcessor.cpp:483`)
  and by the PresetManager `onApplied` callback (`PluginProcessor.h:90`).
- `prepareToPlay` pushes parameters **before** `prepare()`, so the smoothers start on the real values instead of gliding
  from hard-coded defaults (`.cpp:282-289`).

### 1.5 Denormals and non-finite values
- `juce::ScopedNoDenormals _;` at the top of `render()` (`.cpp:338`).
- The harnesses use the same FP mode through `hrvb::ScopedFtz` (`Tools/Harness.h:50-72`): arm64 sets `FPCR |= 1<<24`,
  x86-64 sets `MXCSR |= 0x8040` (FTZ+DAZ). The comment records that 9 IrAnalyse configurations fingerprint differently
  without FTZ.
- Block-rate NaN/Inf guard in the engine: it checks the last output sample and the recursive state, and calls `reset()`
  when poisoned (`FDNReverb.cpp:1177-1183`). `reset()` is O(1) (a DelayLine `clean_` counter) because it runs on the audio thread.

### 1.6 Bypass
- `bypass` is an `AudioParameterBool` (v4), returned from `getBypassParameter()` (`.cpp:235-238`). JUCE maps it to the
  host's own bypass (VST3 kIsBypass, AU bypass).
- `processBlockBypassed()` is overridden (`.cpp:331-334`). `processBlock` and `processBlockBypassed` both call
  `render(buffer, hostBypassed)` (`.cpp:326-334`), and `wantBypass = hostBypassed || raw_.bypass > 0.5` (`.cpp:360`).
- **20 ms linear crossfade** (`kBypassRampSeconds = 0.02f`, `.cpp:82`, step computed in prepare `.cpp:298`). The DSP keeps
  running under bypass. At either end the output is a straight copy, so it is bit-exact (`.cpp:388-426`). The state after
  prepare starts at the saved bypass value, not mid-fade (`.cpp:296-301`).
- The ramp is per sample, audio-thread-only state (`.h:127-130`).
- **Compressor implication:** HR reports no latency. A compressor with lookahead or oversampling must delay the **dry**
  path by the reported latency, both in the bypass crossfade and in Mix. HR's code has no such delay.

### 1.7 Latency, tail, programs, buses, chunking
- **Latency:** there is no `setLatencySamples` anywhere in HR.
- **Tail:** `getTailLengthSeconds()` is computed live from parameters (`.cpp:240-258`). While frozen it returns `+inf`
  for VST3 and 60.2 s (`kMaxFiniteTailSeconds`) for AU and Standalone, because the AU TailTime property has no infinity.
- **Programs:** stubbed. `getNumPrograms()` returns 1 and `setCurrentProgram` is a no-op (`.h:40-44`), even though
  `FactoryPresets.h:3-5` says the factory bank is meant to become the host program list. It is not wired yet.
- **Buses:** the default is stereo in and stereo out (`.cpp:207-209`). `isBusesLayoutSupported` accepts 1→2, 2→2 and
  1→1, and rejects 2→1 (`.cpp:313-324`). A mono input feeds both engine inputs (`inR = nIn > 1 ? ... : inL`, `.cpp:368`),
  and the ramp reads both dry samples before writing because of aliasing (`.cpp:406-410`).
- **Chunking:** `scratch.setSize(2, max(samplesPerBlock, 32))` is allocated in prepare only (`.cpp:294`). If a host sends a
  larger block, `render` processes it in `maxChunk` pieces and never resizes (`.cpp:374-381`).
- **Unprepared guard:** if `scratch` is 0×0 (a VST3 host called process() before setActive, as FL Studio's Patcher does),
  the input passes through instead of spinning forever (`.cpp:350-356`).
- `reset()` forwards to `reverb.reset()` (`.cpp:306-311`).

### 1.8 State get/set, versioning, migration
- **get** (`.cpp:439-452`): `apvts.copyState()`, then `setProperty("advancedUI", ...)` (vestigial), then
  `removeProperty("uiTheme")` (a retired property). It is written as XML with `copyXmlToBinary`, which is JUCE's
  `VC2!` magic, a size and a UTF-8 XML string. The root is `<PARAMS advancedUI=...><PARAM id=".." value="<plain>"/>...`.
  APVTS stores **plain (unnormalised)** values in the tree.
- **set** (`.cpp:454-485`): `getXmlFromBinary` → `ValueTree::fromXml` → read `advancedUI` → `apvts.replaceState(incoming)`.
  **Migration:** any id in `pid::all` without a child in the incoming tree is reset with
  `p->setValueNotifyingHost(p->getDefaultValue())` (`.cpp:467-476`), because `replaceState` leaves absent parameters at
  their live values. Then `reverb.requestParameterSnap()`.
- There is **no explicit state-version attribute**. Migration relies on "absent means default" and on neutral defaults.
- Preset identity is **not** in the session state yet. `PresetManager::writeState/readState` exist in the API
  (`PresetManager.h:37-40`), but they are empty stubs (`PresetManager.cpp:64-65`) and the processor never calls them.
- **Preferences vs state:** the theme is machine-wide and never in state. `hrvbgui::UiPreferences` is a never-destroyed
  singleton backed by `juce::PropertiesFile` at `~/Library/Application Support/HardwareReverb/preferences.settings`
  (key `theme`), saved eagerly on every change (`gui/UiPreferences.h:9-18`, `.cpp:13-73`). `HRVB_PREFS_DIR` redirects it
  for harnesses (`UiPreferences.cpp:38-40`). Editors re-read it on open (`BgfxEditor.cpp:149-151`) and watch `revision()`.

---

## 2. Preset system (`Source/presets/*`, `Source/PresetPanel.*`)

**Status: the API headers are real, and four of the implementations are STUBs** (see the warning at the top).

- **Contract (`PresetTypes.h`, "change only by adding"):** `struct Preset { uuid, name, category, author, notes,
  isFactory, format = kFormat(1), std::vector<ParamValue{id, float value /*plain units*/}> params, createdMs, modifiedMs,
  lastUsedMs, TagMask tags }`. Values are stored in **plain units keyed by parameter id**, never normalised, so a preset
  survives range or skew changes, and an absent id loads at its default (`PresetTypes.h:8-13`). There are 7 Finder-style
  colour tags (`Tag` enum, `TagMask` bitset), plus a `Query { text, category, Source{all,factory,user}, anyOfTags,
  Sort{name,category,recent,created} }`.
- **File format (`PresetFile.h`):** extension `.hrvbpreset`, XML. The stub writes root `<HardwareReverbPreset format uuid
  name category author notes>` with `<PARAM id value/>` children (`PresetFile.cpp:8-12`). Tags and timestamps are never
  exported. API: `toXmlString`, `fromXmlString(s, String* err)`, `write(p, File, err)`, `read(File, err)`, `safeFileName`.
- **Factory bank (`FactoryPresets.h/.cpp`):** a compiled-in `const std::vector<Preset>& factoryBank()`.
  `factoryBankRevision()` must be bumped whenever the bank changes. It also provides `findFactory(uuid)` and
  `factoryIndexOf(uuid)`. Index 0 is `"Init"` (all defaults). The stub bank holds 5 entries with fixed UUIDs
  `00000000-0000-4000-8000-00000000000N`, and only mentioned parameters are listed.
- **Store (`PresetStore.h`):** SQLite from the **macOS system library** (`find_package(SQLite3 REQUIRED)`,
  `CMakeLists.txt:181`), at `~/Library/Application Support/HardwareReverb/Presets.db` or `$HRVB_PRESETS_DB`
  (`PresetStore.cpp:19-24`). There is one connection per process through `juce::SharedResourcePointer<PresetStore>`: it
  opens with the first editor and closes with the last. It is **message-thread only**, uses WAL and a busy timeout for
  multi-process access, and falls back to in-memory storage (`isPersistent()==false`) when it cannot write. API:
  `syncFactory(bank, rev)`, `query(Query)`, `get(uuid)`, `categories()`, `count(Source)`, `saveNew(Preset&)`,
  `overwrite`, `rename`, `setCategory`, `setNotes`, `remove`, `setTags`, `markUsed`, `tagLabel/setTagLabel`,
  `uniqueName`, `importPreset → ImportResult{ok,uuid,name,renamed,duplicate,error}`, `revision()`,
  `pollExternalChanges()`. **The current `.cpp` is an in-memory stub.**
- **Manager (`PresetManager.h`):** one per instance, owned by the processor as
  `presets_{ apvts, [this]{ reverb.requestParameterSnap(); } }` (`PluginProcessor.h:90`) and exposed through
  `presets()` (`.h:62`). No database access, so it is safe from host program-change threads. API: `isPresetParameter(id)`
  (all ids except `bypass` and `freeze`), `capture()`, `apply(Preset)` (absent ids go to defaults, `mix` is skipped when
  `mixLocked()`, then `onApplied`, then the baseline is recorded), `isModified()` (1e-4 normalised tolerance),
  `current()`/`setCurrent()` under a CriticalSection, `revision()` (atomic), `setMixLocked()`, `writeState/readState`
  (stubs).
- **PresetPanel UI coupling:** `PresetPanel(HardwareReverbProcessor&, juce::Component& owner)` (`PresetPanel.h:32`) is
  drawn on the editor's `SdfCanvas` with `Theme`, text entry included, because a `juce::TextEditor` would sit under the
  Metal view. Its geometry is fixed for an 880×520 editor: strip y 14..58 × x 204..572, browser y 70..304 × x 40..840
  (`.h:14-18`). Its only processor dependency is `proc_.presets()`, and it holds
  `SharedResourcePointer<PresetStore> store_` (`.h:123-125`). The editor calls `presetPanel_.tick(dt)` each frame
  (`BgfxEditor.cpp:897`) and routes pointer, keyboard, drag-and-drop and accessibility through it. **To reuse it, change
  the constructor to take `PresetManager&` and a product name/extension instead of `HardwareReverbProcessor&`, and
  re-derive the strip and browser rectangles from the FCompressor layout.**

---

## 3. Audio → UI data path

- **Payload:** `FDNReverb::UiFrame` is a trivially copyable POD whose size is a multiple of 4 bytes (`FDNReverb.h:26-39`,
  `static_assert`s at `:309-312`). Fields: `publishCount, order, liveOrder, warmupPushed, sampleRate,
  currentDelay[32], feedback[32], lineEnergy[32], freeze, preDelaySamples, dryRms, wetRms`.
- **Gate:** `std::atomic<bool> uiAttached_` (`FDNReverb.h:300`) is set by `setUiAttached()`, forwarded by the processor
  (`PluginProcessor.h:74`). The editor sets it to true in its constructor (`BgfxEditor.cpp:159`) and false in its
  destructor (`:170`). It is keyed to **editor lifetime, not visibility**. `processBlock` reads it once per block
  (`FDNReverb.cpp:845`) and only then zeroes and accumulates meters: `energyAcc_[i] += y*y` per line (`:995`) and
  `dryAcc_/wetAcc_` (`:1087-1088`). It publishes at the end of the block (`:1185-1186`).
- **Metering math (`publishUiFrame`, `:1190-1252`):** block RMS = `sqrt(acc/n)`. The envelope has **instant attack** and
  a **40 ms release** (`rel = 1 - exp(-blockSec/0.040)`), so a transient shows on the frame it happens and the display
  decays at the signal's own rate, not a UI time constant.
- **Seqlock write (`:1213-1251`):** the frame is staged in a private `uiStage_`, then `memcpy` into `uint32_t words[]`,
  then `uiSeq_.fetch_add(1, release)` (odd), release fence, relaxed stores into
  `std::array<std::atomic<uint32_t>, kUiWords> uiWords_`, release fence, `fetch_add(1, release)` (even). The slot is atomic
  words, not a plain struct, so it is data-race-free under [intro.races] and TSan (`FDNReverb.h:302-308`).
- **Seqlock read (`readUiFrame`, `:1254-1271`):** up to 8 attempts. Each attempt does an acquire load of the sequence,
  skips if odd, does relaxed word loads, an acquire fence and a relaxed re-check of the sequence, then `memcpy`. It
  returns false on contention, and the editor then reuses the previous frame. The processor forwards it as
  `bool readUiFrame(UiFrame&) const noexcept` (`PluginProcessor.h:67-70`).
- **Editor consumption (`BgfxEditor.cpp:938-952`):** reads once per frame in `submitFrame`. A stale `publishCount`
  (transport stopped) increments `liveStaleSec_`, and the display falls back to a model derived from parameters instead
  of freezing.
- **Other things the editor needs from the processor:** `proc.apvts.getParameter(id)` (normalised
  `getValue()`/`setValueNotifyingHost`, `convertTo0to1`, gestures) and `getRawParameterValue(id)`. The editor reads
  parameters directly from the APVTS. There is no separate UI model.
- **Limit for a compressor:** a seqlock gives the *latest* snapshot only. A 60–120 Hz UI loses everything between frames
  except what the audio thread folds into envelopes. For a GR / level **history scope**, pair the seqlock "state" frame
  with an SPSC ring (`juce::AbstractFifo` or a custom index pair) of fixed-rate points, for example one {inPk, outPk, GR,
  detector dB} tuple every 1–2 ms. See §7.5.

---

## 4. CMake anatomy (`CMakeLists.txt`, 636 lines)

G = generic (copy and rename), S = HardwareReverb-specific.

| Lines | What | G/S |
|---|---|---|
| 1 | `cmake_minimum_required(VERSION 3.22)` | G |
| 8, 93 | `CMAKE_OSX_DEPLOYMENT_TARGET "14.0"`, set before `project()` and then FORCE'd again | G |
| 30-35 | `option(HARDWAREREVERB_UNIVERSAL OFF)`. When ON it sets `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` as a **normal** variable (not sticky), before `project()` | G |
| 37-41 | `project(HardwareReverb VERSION 0.1.0 LANGUAGES C CXX)`, C++20, no extensions | G (rename) |
| 48-52 | Defaults `CMAKE_BUILD_TYPE` to **Release** when neither it nor `CMAKE_CONFIGURATION_TYPES` is set | G |
| 58-60 | `if(NOT APPLE) FATAL_ERROR` | G |
| 64-91 | `HRVB_ARCHS` (from `CMAKE_OSX_ARCHITECTURES` or the host) must be arm64/x86_64. `HRVB_ARCH_COUNT`. `HRVB_RUN_ARCH` is the arch the harnesses *execute* as (single arch = that arch, universal = the host) | G |
| 95-97 | `option(HARDWAREREVERB_ENABLE_SME OFF)`. A macro-only hook; 612-636 adds `-DHARDWAREREVERB_ENABLE_SME=1` (scoped with `-Xarch_arm64` in universal builds) and deliberately no `-march` | S (drop) |
| 99-101 | `option(HARDWAREREVERB_CLASSIC_GUI OFF)`. When ON, it skips bgfx entirely and compiles `PluginEditor.cpp` | G |
| 107-109 | `option(HARDWAREREVERB_INSTALL_AFTER_BUILD ON)` → `COPY_PLUGIN_AFTER_BUILD` | G |
| 111-117 | `FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)` + `MakeAvailable` | G |
| 153-161 | Font: `HRVB_FONT_FACE = Resources/fonts/JetBrainsMono-Regular-subset.ttf` (103 glyphs, 10,288 bytes) and `HRVB_FONT_LICENCE`. `juce_add_binary_data(HardwareReverbFonts HEADER_NAME HardwareReverbFonts.h NAMESPACE hrvbfonts SOURCES face licence)`. Consumed by `gui/BundledFont.h` (`hrvbfonts::JetBrainsMonoRegularsubset_ttf[Size]`) | G (rename) |
| 165-166 | Cache strings `HRVB_COMPANY_WEBSITE` and `HRVB_COMPANY_EMAIL` (empty by default) | G |
| 174-181 | Source lists `HRVB_PRESET_CORE_SOURCES` (FactoryPresets, PresetManager; no DB) and `HRVB_PRESET_STORE_SOURCES` (PresetStore, PresetFile), plus `find_package(SQLite3 REQUIRED)` | G-ish |
| 183-210 | `juce_add_plugin(HardwareReverb ...)`, fields listed below | S values |
| 212-217 | `target_sources(PluginProcessor.cpp, dsp/FDNReverb.cpp, presets)`, `target_include_directories(Source)` | S |
| 219-220 | Classic GUI: adds `Source/PluginEditor.cpp` | S |
| 229-235 | bgfx options: `BGFX_BUILD_TOOLS ON`, `_SHADER ON`, `_TEXTURE/_GEOMETRY/_BIN2C OFF`, `BGFX_BUILD_EXAMPLES OFF`, `BGFX_INSTALL OFF` (all CACHE FORCE). Removing texturec, geometryc and bin2c removed about a third of the 958 clean-build steps | G |
| 236-241 | `FetchContent_Declare(bgfx GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git GIT_TAG v1.153.9385-561 GIT_SHALLOW TRUE)` | G |
| 243-266 | `HRVB_SHADER_DIR = Source/gui/shaders` and `HRVB_SHADER_OUT = ${binary}/generated/shaders`. `function(hrvb_compile_shader NAME TYPE)` adds a custom command that runs `$<TARGET_FILE:shaderc> -f X.sc -o X.mtl.h --type TYPE --platform osx -p metal --varyingdef varying.def.sc -i ${bgfx_SOURCE_DIR}/bgfx/src --bin2c X_mtl` and depends on shaderc and the sources | G |
| 271-277 | `hrvb_compile_shader(vs_ui vertex)`, `(fs_ui fragment)`, custom target `HardwareReverbShaders`, and `add_dependencies(HardwareReverb HardwareReverbShaders)`. It is **one program for the whole UI**. `BgfxContext.cpp:9-10` includes `<shaders/vs_ui.mtl.h>` and `<shaders/fs_ui.mtl.h>` | G |
| 279-293 | GUI sources: `BgfxEditor.cpp PresetPanel.cpp gui/{BgfxContext,SdfCanvas,FontAtlasSdf,FramePump,UiPreferences}.cpp gui/{NativeSurface,DisplayLink}.mm`. Include dir `${binary}/generated`. Definition `HARDWAREREVERB_BGFX_GUI=1`. Links `bgfx bx bimg HardwareReverbFonts` | G (+S editor) |
| 296-299 | `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0` | G |
| 320-332 | The licence font and `Resources/licences/*` are bundle **resources** (`MACOSX_PACKAGE_LOCATION Resources` / `Resources/licences`) added to each `_VST3/_AU/_Standalone` target. They are deliberately **not** a POST_BUILD copy, because a copy after JUCE's ad-hoc codesign broke the seal (`codesign --verify --strict` failed on the first clean build) | G |
| 366-387 | ISA flags: `arm64: -mcpu=apple-m1`, `x86_64: -mavx2 -mfma`. They are plain in single-arch builds and `-Xarch_<arch>` in universal builds (the Xarch form would otherwise warn about 500 times). `add_library(HardwareReverbDspFlags INTERFACE)`: ISA + `-O3 -fno-math-errno -fno-trapping-math`, linking `juce::juce_recommended_config_flags` and `juce::juce_recommended_lto_flags`. **No `-ffast-math`** (JUCE constexpr `infinity()`) | G |
| 395-418 | GUI harnesses (bgfx config only): `AtlasDump FrameRender PrefsCheck FontProbe`, each `juce_add_console_app(HardwareReverb${T})` with `Tools/${T}.cpp gui/FontAtlasSdf.cpp gui/UiPreferences.cpp`, definition `JUCE_STANDALONE_APPLICATION=1`, links `juce_gui_basics`, config flags and fonts. `EXCLUDE_FROM_ALL` | G |
| 424-436 | DSP harnesses (every config): `DspBench IrAnalyse DuckProbe ResetProbe SmearProbe`. Each compiles `Tools/X.cpp + Source/dsp/FDNReverb.cpp` again and links only `juce::juce_core HardwareReverbDspFlags`. `EXCLUDE_FROM_ALL` | S names, G pattern |
| 447-463 | `HardwareReverbStateProbe` links **the real processor**: `Tools/StateProbe.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/dsp/FDNReverb.cpp + preset core`. It never defines `HARDWAREREVERB_BGFX_GUI`, so the processor falls into the classic-editor branch (no bgfx, no window server). Adds `JucePlugin_Name="HardwareReverb"` because `getName()` needs it outside `juce_add_plugin`. Links `juce::juce_audio_processors HardwareReverbDspFlags` | G pattern |
| 467-483 | `HardwareReverbBankProbe`, same shape (currently a STUB) | S |
| 488-499 | `HardwareReverbPresetProbe`: store and file sources, `SQLite3::SQLite3` (STUB) | G-ish |
| 549-553 | Golden selection: `HRVB_RUN_ARCH == arm64` → `Tools/golden/`, otherwise `Tools/golden/${HRVB_RUN_ARCH}` (i.e. `Tools/golden/x86_64/`) | G |
| 554-572 | `verify`: for each of `IrAnalyse ResetProbe DuckProbe SmearProbe StateProbe`, `COMMAND $<TARGET_FILE:X> --check ${GOLDEN}/<lower>.txt`, with `WORKING_DIRECTORY` = source, `VERBATIM USES_TERMINAL`, and dependencies on the tools. DspBench (timing) and PrefsCheck are deliberately excluded | G framework, S list |
| 582-603 | `verify-gui` (bgfx only): FontProbe `--check fontprobe.txt`, PrefsCheck `--check prefscheck.txt`, then 3× (`Scripts/capture-frame.sh` launches the Standalone, then `FrameRender --fingerprint dump --check framerender[-filter|-mod].txt`, with `HRVB_UI_VIEW=filter|mod`). Depends on `HardwareReverb_Standalone`, so it needs a window server | G framework, S views |
| 605-610 | Plugin links `SQLite3::SQLite3 juce_audio_utils juce_dsp juce_recommended_warning_flags HardwareReverbDspFlags` | G |

**`juce_add_plugin` fields (183-210):** `COMPANY_NAME "Funk"`, `COMPANY_COPYRIGHT "Copyright (c) 2026 Sean Funk"`,
`COMPANY_WEBSITE/EMAIL` from the cache, `PLUGIN_MANUFACTURER_CODE Funk`, `PLUGIN_CODE Hrvb`, `FORMATS AU VST3 Standalone`,
`PRODUCT_NAME "HardwareReverb"`, `BUNDLE_ID com.funk.hardwarereverb`, `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT/OUTPUT FALSE`,
`VST3_CATEGORIES Fx Reverb`, `AU_MAIN_TYPE kAudioUnitType_Effect`, `MICROPHONE_PERMISSION_ENABLED TRUE`,
`MICROPHONE_PERMISSION_TEXT "..."`, `HARDENED_RUNTIME_ENABLED TRUE`,
`HARDENED_RUNTIME_OPTIONS "com.apple.security.device.audio-input"`,
`PLIST_TO_MERGE "<plist ...><key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>..."`
(JUCE does not emit LSMinimumSystemVersion), and `COPY_PLUGIN_AFTER_BUILD ${HARDWAREREVERB_INSTALL_AFTER_BUILD}`.

**LTO:** comes only from `juce_recommended_lto_flags`, which JUCE defines as `$<$<CONFIG:Release>:-flto>`
(`juce-src/extras/Build/CMake/JUCEHelperTargets.cmake:146-147`). HR's day-to-day `build/` is configured
**RelWithDebInfo** (its CMakeCache), so it gets `-O3` from DspFlags but **no LTO**. `build-dsp` and `build-universal`
are Release.

**Post-build sealing and signing:** HR adds **no** post-build hooks of its own. JUCE's chain runs per format target:
PkgInfo, ad-hoc codesign through `checkBundleSigning.cmake` (twice for the VST3, on either side of `moduleinfo.json`),
then the install copy to `~/Library/Audio/Plug-Ins/{Components,VST3}` (described in `CMakeLists.txt:306-319`). Real
signing happens only in `Scripts/release.sh`. GlueCompressor and FunkPluginTemplate additionally run
`add_custom_command(TARGET X_AU/X_VST3 PRE_BUILD COMMAND xattr -cr <bundle>)` to avoid "detritus not allowed" after a
codesign run. That hook is cheap and worth adopting.

**Harness contract (`Tools/Harness.h`):** `Metric{key, value(string), tol}`. `kExact = -1` compares text exactly
(hashes). Helpers: `addNum(%.6g, tol)` and `addHash(%016llx)`. `finish(argc, argv, metrics)`: `--check <file>` returns 0
on pass, 1 on a mismatch and 2 when there is no golden. It prints every mismatch plus new or missing keys.
`--bless <file>` writes a TSV (`key\tvalue\ttolerance|exact`). Goldens are **source files**. HR is not a git repo, so the
golden file is the only record of what was agreed.

**Golden per arch (`CMakeLists.txt:526-548`, README "Architectures"):** the NEON and SSE backends agree bit for bit
(x86 emulates NEON `FRSQRTE`). The remaining differences come from libm `powf` last bits and from clang contracting
`sum += f*f` into an FMA on x86 but not through NEON. 221/229 (CMake comment) and 261/264 (README) rows match across
arches. FCompressor will call `exp`/`log` for per-sample dB conversions, so expect **more** cross-arch libm differences:
keep per-arch golden directories from day one.

### 4.1 Full rename list for a new plugin (every HR-specific token, from a grep)
- **CMake:** `project(HardwareReverb)`. Options `HARDWAREREVERB_{UNIVERSAL,ENABLE_SME,CLASSIC_GUI,INSTALL_AFTER_BUILD}`.
  Compile definition `HARDWAREREVERB_BGFX_GUI`. Variables `HRVB_{ARCHS,ARCH_COUNT,RUN_ARCH,A,F,ISA_FLAGS,ARCH_FLAGS_arm64,ARCH_FLAGS_x86_64,FONT_FACE,FONT_LICENCE,COMPANY_WEBSITE,COMPANY_EMAIL,PRESET_CORE_SOURCES,PRESET_STORE_SOURCES,PRESET_SOURCES,SHADER_DIR,SHADER_OUT,THIRD_PARTY_LICENCES,FMT,TOOL,DSPTOOL,GOLDEN_DIR,VERIFY_TOOLS,VERIFY_CMDS,V,V_LOWER}`.
  Function `hrvb_compile_shader`. Targets `HardwareReverb` (and `_AU/_VST3/_Standalone`), `HardwareReverbFonts`,
  `HardwareReverbShaders`, `HardwareReverbDspFlags`, `HardwareReverb{AtlasDump,FrameRender,PrefsCheck,FontProbe,DspBench,IrAnalyse,DuckProbe,ResetProbe,SmearProbe,StateProbe,BankProbe,PresetProbe}`.
  Binary data `HEADER_NAME HardwareReverbFonts.h` and `NAMESPACE hrvbfonts`. `JucePlugin_Name="HardwareReverb"`
  (×2). The plugin fields `PLUGIN_CODE`, `PRODUCT_NAME`, `BUNDLE_ID`, `VST3_CATEGORIES` and `MICROPHONE_PERMISSION_TEXT`.
- **C++:** classes `HardwareReverbProcessor`, `HardwareReverbBgfxEditor` and `HardwareReverbEditor`. Namespaces `hrvb`
  (plus `hrvb::presets` and `hrvb::simd`), `hrvbgui` (85 uses) and `hrvbfonts`. Macros `HRVB_SIMD_NEON` and
  `HRVB_SIMD_SSE`. The `#error` strings in `Simd.h:64,67`. `UiPreferences.cpp:29` `folderName "HardwareReverb"`.
  `PresetStore.cpp:23` `"Application Support/HardwareReverb/Presets.db"`. `PresetFile.h:12` `".hrvbpreset"` and the XML
  root `"HardwareReverbPreset"`. `FactoryPresets.cpp:12` author `"HardwareReverb"`. `BundledFont.h:3,17-18`.
- **Environment variables:** `HRVB_PRESETS_DB`, `HRVB_PREFS_DIR`, `HRVB_CANVAS_DUMP(_AFTER)`, `HRVB_UI_THEME`,
  `HRVB_UI_SCALE(_AFTER)`, `HRVB_UI_KEYS`, `HRVB_UI_BROWSER`, `HRVB_A11Y_DUMP`, `HRVB_UI_VIEW`,
  `HRVB_SIGNING_IDENTITY`, `HRVB_NOTARY_PROFILE`. Reads happen in `BgfxEditor.cpp` (:136-152, :718-720, :885-894, :998),
  `UiPreferences.cpp:38` and `PresetStore.cpp:21`.
- **Resources and scripts:** `Resources/HardwareReverb.entitlements`. `Resources/licences/THIRD-PARTY.txt` (header and
  GPL sentence). `Scripts/release.sh` (the `ENT` path, `HardwareReverb_artefacts`, the `.component/.vst3/.app` names).
  `Scripts/capture-frame.sh` (`-name HardwareReverb`, `HardwareReverb_artefacts`, the `hrvb-capture.XXXXXX` temp
  prefix, the Presets.db comment).
- **Tree type `"PARAMS"`** can stay.

---

## 5. Scripts

- **`Scripts/release.sh <build-dir> [<out-dir>]`** (63 lines, `set -eu`). It finds
  `$BUILD/HardwareReverb_artefacts/<config>/` and requires the AU, VST3 and Standalone bundles. For each bundle it runs
  `codesign --force --options runtime --entitlements Resources/HardwareReverb.entitlements` with
  `--sign "$HRVB_SIGNING_IDENTITY" --timestamp`, or ad-hoc `--sign -` as a dry run when the identity is unset. It then
  runs `codesign --verify --strict --verbose=2`, checks that `audio-input` is in the entitlements, prints the
  Identifier/Team/Runtime/Flags, and zips with `ditto -c -k --keepParent` into `$OUT/<bundle>.zip`. When
  `HRVB_NOTARY_PROFILE` is set, it also runs `xcrun notarytool submit --keychain-profile --wait`, `xcrun stapler staple`
  and `stapler validate` for each bundle, then re-zips the stapled bundle. It replaces JUCE's ad-hoc signatures and signs
  the Standalone, which JUCE never signs. **Generic apart from the names.**
- **`Scripts/capture-frame.sh <build-dir> <out.dump>`** (28 lines). It finds the Standalone binary and creates a
  scratch `HRVB_PRESETS_DB` in a mktemp dir (trap-cleaned). It launches with `HRVB_CANVAS_DUMP=$OUT
  HRVB_CANVAS_DUMP_AFTER=8 HRVB_UI_THEME=0` in the background, polls for the dump every 0.5 s up to 40 times (20 s),
  kills the app, and exits 1 when no frame appears. It needs a window server. **Generic apart from the names.**
- **Entitlements (`Resources/HardwareReverb.entitlements`):** only
  `com.apple.security.device.audio-input = true` (Standalone microphone; inert in a host).
- **Licences (`Resources/licences/`):** `THIRD-PARTY.txt` (index), `GPL-3.0.txt`, `JUCE-LICENSE.md`,
  `VST3-SDK-LICENSE.txt`, `bgfx/bx/bimg-LICENSE.txt` (BSD-2) and `bgfx.cmake-LICENSE.txt` (CC0). The font licence is
  `Resources/fonts/JetBrainsMono-LICENSE.txt` (OFL 1.1). HR is GPLv3 because JUCE and the VST3 SDK are used under their
  GPLv3 options (README "Third-party licences"). FCompressor inherits that choice unless it moves to commercial JUCE and
  VST3 licences.

---

## 6. Sibling plugins

### 6.1 GlueCompressor (`../GlueCompressor`)
- **Framework:** JUCE 8.0.4 plus **visage** (VitalAudio) pinned at `b0b2ee89abb3f1173dad4d80460197eb96afa724`
  (`CMakeLists.txt:54-57`). It is arm64-only and hard-errors on anything else (`:33-41`). Languages
  `C CXX OBJC OBJCXX`. A custom `StandaloneApp.cpp` (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`). The shaders and
  `visage_embed_shaders` are dead code per its `CLAUDE.md`. The GUI is visage canvas vector knobs plus `GrMeter`,
  760×340. **No UI code can be reused with bgfx/SdfCanvas.**
- **DSP (`Source/dsp/GlueCompressor.h`, 242 lines, header-only, std only, namespace `gluecompressor`):**
  - Feed-forward, VCA-style, **log-domain** side-chain. Stereo-linked detector `sc = 0.5*(|L|+|R|)` (`:189`). There is
    no RMS option, no sidechain HPF and no external sidechain.
  - `sc_db = 20/ln10 * log(max(sc, 1e-10))`, computed per sample (`:190`).
  - Static curve `curveGrDb(x, T, R, W)` (`:130-146`): `slope = 1-1/R`, `over = x - T`. It returns 0 below `-W/2`,
    `slope*over` above `+W/2`, and `slope*(over+W/2)^2/(2W)` inside the knee. This is the standard
    Giannoulis–Massberg–Reiss (JAES 2012) quadratic soft knee, written as positive GR in dB. **Reusable as the shared
    "gain computer"**, and the transfer-curve screen should call the same function (as HR's filter view calls the
    engine's own expressions).
  - Ballistics: **branching one-pole on GR in dB** (`:196-201`). If `target > env` it uses attack, otherwise release.
    `onePoleCoef(tau, fs) = 1 - exp(max(-1/(tau*fs), -50))` (`:121-128`).
  - "Auto" release is a **two-stage cascade** (`:206-216`): the first stage releases with a fixed 50 ms tau, and the
    second stage takes `max()` instantly and releases with a 600 ms tau. This is an SSL-style program-dependent release.
  - Output: `gain = exp(-GR*ln10/20) * makeup`, then a dry/wet mix per channel (`:222-230`).
  - Meter: `gr_meter_` is an atomic holding `max(blockPeakGR, prev*0.6)` per block (`:236-240`). **The decay is
    block-size dependent**, which HR explicitly avoids.
  - Gaps FCompressor must fix: no parameter smoothing (threshold, makeup and mix step at block edges, causing zipper
    noise). Parameters arrive through an APVTS **listener** (`PluginProcessor.cpp:78-80,129-138`), the mutex path HR
    rejected. `mix` is 0..100 scaled by 0.01. No bypass parameter, no latency, no lookahead, no oversampling, no state
    migration. `std::log` and `std::exp` run per sample. Knee is fixed at 4 dB, and `setKneeDb` exists but is not exposed.
  - Parameters: `threshold` −30..+6 dB (default −12), `ratio` 1.5..20 with skew 0.4 (default 2), `attack` 0.1..30 ms
    with skew 0.3 (default 10), `release` 50..2000 ms with skew 0.4 (default 200), `auto` bool, `makeup` 0..24 dB,
    `mix` 0..100 %. All use ParameterID version 1.
- **Verdict:** reuse the curve formula and the cascade-release idea as the algorithmic seed for an "SSL-bus" Mode.
  Rewrite everything else to the HR conventions.

### 6.2 FunkPluginTemplate (`../FunkPluginTemplate`)
visage template (JUCE 8.0.4 plus the same visage commit), arm64-only, with a `Gain` example.
`scripts/plugin.py` (692 lines, stdlib only) provides `clone`, `rename`, `codesign` and `notarize`, and does
case-sensitive bulk replacement across text suffixes. Its AGENT.md documents the threading contract (APVTS listener →
DSP atomics) and **"never -ffast-math"**. The README warns that after `codesign` the installed bundles get immutable
flags (`chflags -R nouchg ...` before rebuilding). It is not the right base for FCompressor, whose base is HR, but
`plugin.py rename`'s replacement logic is a reasonable model for a one-shot HR→FCompressor rename script.

### 6.3 DiffusorX (`../DiffusorX/CMakeLists.txt`)
JUCE, visage, nlohmann_json and kissfft as **git submodules** under `third_party/` (`add_subdirectory`, not FetchContent).
`juce_add_plugin(DiffusorX COMPANY_NAME SeanFunk PLUGIN_MANUFACTURER_CODE Snfk PLUGIN_CODE Difx ...)` with no
`BUNDLE_ID`, so JUCE uses its default `com.${COMPANY_NAME}.${target}`, i.e. `com.SeanFunk.DiffusorX`
(`JUCEUtils.cmake:1717`). There is no install option. Not relevant to the architecture.

### 6.4 Code registry (all siblings plus the installed AUs)
| Plugin | Mfr | Code | AU type | Bundle ID |
|---|---|---|---|---|
| HardwareReverb | Funk | Hrvb | aufx | com.funk.hardwarereverb |
| GlueCompressor | Funk | Glcm | aufx | com.funk.gluecompressor |
| FunkPluginTemplate | Funk | Ftpl | aufx | com.funk.funkplugintemplate |
| SampleAtlas | Funk | Smpl | aumu | com.funk.sampleatlas |
| TestingPlayground | Funk | **Tstn** | aufx | com.funk.testingplayground |
| TestingPlaygroundSynth | Funk | **Tstn** | aumu | com.funk.testingplaygroundsynth |
| DiffusorX | Snfk | Difx | (default) | com.SeanFunk.DiffusorX (JUCE default) |

The installed components were confirmed with `PlistBuddy` on `~/Library/Audio/Plug-Ins/Components/*.component` and with
`auval -a` (189 AUs, and only the six `Funk` entries above).
**Existing collision:** TestingPlayground and TestingPlaygroundSynth both use `Funk/Tstn`. Their AU identities differ only
by type. The **VST3 class IDs are identical**, because JUCE builds them solely from `JucePlugin_ManufacturerCode` and
`JucePlugin_PluginCode` (`juce_audio_plugin_client_VST3.cpp:2587`, `INLINE_UID(0xABCDEF01, 0x9182FAEB, mfr, code)`).
This is not FCompressor's problem, but it is worth telling the user.

**Proposal for FCompressor:** `PLUGIN_MANUFACTURER_CODE Funk`, **`PLUGIN_CODE Fcmp`** (one upper-case letter first,
which GarageBand requires; free in both the sibling list and the installed AU list), **`BUNDLE_ID com.funk.fcompressor`**,
`PRODUCT_NAME "FCompressor"`, `COMPANY_NAME "Funk"`, `AU_MAIN_TYPE kAudioUnitType_Effect`,
`VST3_CATEGORIES Fx Dynamics`. Fallbacks if needed: `Fcps` or `Fcmx`. **Never change the codes after the first session
is saved**: they are the AU and VST3 identity.

---

## 7. FCompressor skeleton proposal

### 7.1 Directory layout
```
FCompressor/
  CMakeLists.txt            adapted from HR, all generic sections kept
  CMakePresets.json         dev / dsp / gui-agent / universal (§7.6)
  README.md  CLAUDE.md  LICENSE (GPLv3, copied)
  Resources/
    FCompressor.entitlements        copy, rename
    fonts/                          copy verbatim (subset TTF, licence, upstream/)
    licences/                       copy verbatim, edit the THIRD-PARTY.txt header
  Scripts/  release.sh  capture-frame.sh   copy, rename HRVB_ to FCMP_
  Source/
    PluginProcessor.{h,cpp}         rewrite on HR's skeleton (raw_ polling, render(), bypass, chunking, state)
    params/  ParamIds.h  ParamLayout.cpp  ParamText.{h,cpp}     layout, valueToText, ranges (superset)
    modes/   ModeId.h  ModeDescriptor.h  ModeRegistry.{h,cpp}  <Mode>.cpp ...
    dsp/     (JUCE-free, std only)
             Simd.h (copied)  GainComputer.h  Detector.h  Ballistics.h  Lookahead.h
             CompressorEngine.{h,cpp}  Meters.h  (Oversampling later)
    telemetry/ Seqlock.h (template extracted from FDNReverb)  UiFrame.h  HistoryRing.h
    presets/ PresetTypes.h PresetStore.h PresetManager.h PresetFile.h FactoryPresets.h  (HR contract, copied)
             *.cpp  (own implementations, or HR's once they are no longer stubs)
    gui/     copied verbatim from HR (20 files + shaders/), mechanical renames only (§7.3)
    ui/      Editor.{h,cpp} (bgfx)  PresetPanel.{h,cpp} (adapted)  views/{TransferView,GrHistoryView,InternalsView}.cpp
  Tools/
    Harness.h                       copy (namespace rename)
    StateProbe.cpp                  rewrite on HR's pattern (layout, text, state, absent→default, buses, bypass, unprepared)
    CurveProbe.cpp  BallisticsProbe.cpp  ModeProbe.cpp  DspBench.cpp   new
    FrameRender.cpp FontProbe.cpp AtlasDump.cpp PrefsCheck.cpp        copy (FrameRender dynamic-primitive filter changed)
    golden/  golden/x86_64/
```

### 7.2 CMake options (renamed) and structure
- `FCOMPRESSOR_UNIVERSAL` (OFF), `FCOMPRESSOR_CLASSIC_GUI` (OFF; ON = bgfx-free build whose editor is
  `juce::GenericAudioProcessorEditor`, so no second hand-written editor to maintain) and
  `FCOMPRESSOR_INSTALL_AFTER_BUILD` (ON). **Drop `ENABLE_SME`.** Definition `FCOMPRESSOR_BGFX_GUI=1`.
  Variable prefix `FCMP_`. Function `fcmp_compile_shader`. Interface target `FCompressorDspFlags`. Fonts target
  `FCompressorFonts` with namespace `fcmpfonts`.
- **Put the JUCE-free DSP in a static library `FCompressorDsp`** (`Source/dsp/*.cpp` and the pure parts of
  `modes/*.cpp`), linking only `FCompressorDspFlags`. The plugin and every probe link it, so the DSP compiles once and not
  once per tool (HR compiles `FDNReverb.cpp` into seven targets). JUCE-dependent sources (processor, params, presets)
  stay per target, as in HR. JUCE modules are INTERFACE libraries that compile their sources into each consumer, so a
  static library that links `juce::` modules would duplicate symbols.
- A helper function `fcmp_add_harness(NAME SOURCES ... LINKS ...)` replaces the repeated
  `juce_add_console_app`/definitions/`EXCLUDE_FROM_ALL` blocks (HR has 5 of them).
- Keep: the Release default, the arch and `RUN_ARCH` logic, the `-Xarch_` ISA flags, the licence **resources** (not
  POST_BUILD), `PLIST_TO_MERGE` LSMinimumSystemVersion, hardened runtime, microphone permission text for the Standalone,
  `find_package(SQLite3)`, `verify` / `verify-gui` with `--check`/`--bless`, and per-arch goldens.
- Add: the `xattr -cr` PRE_BUILD hook from GlueCompressor. **Pin checks**, because `FETCHCONTENT_SOURCE_DIR_*`
  bypasses `GIT_TAG`: after MakeAvailable, `if(NOT JUCE_VERSION VERSION_EQUAL 8.0.4) FATAL_ERROR`, and grep
  `${bgfx_SOURCE_DIR}/bgfx/include/bgfx/defines.h` for `BGFX_API_VERSION UINT32_C(153)`.
- Consider adding `-ffp-contract=off` to `FCompressorDspFlags` **from day one**. HR could not do this without
  re-blessing (its cross-arch mismatches include x86 fusing `sum += f*f`). FCompressor has no legacy goldens. Explicit
  `simd::fma` intrinsics are unaffected.
- `verify` list: `StateProbe CurveProbe BallisticsProbe ModeProbe`. `verify-gui`: FontProbe, PrefsCheck, and FrameRender
  for each view (main, transfer, history, internals) through an `FCMP_UI_VIEW` capture hook. Change FrameRender's
  dynamic-primitive exclusion: HR hard-codes the Rank band `y 165..295` and caps `d0[3] <= 2`
  (`Tools/FrameRender.cpp:118-124`). Mark live meters and dots with a flag in the dump instead.

### 7.3 Copy verbatim vs rewrite
- **Copy with mechanical renames only:** `Source/gui/*` (BgfxContext, SdfCanvas, FontAtlasSdf, FramePump, DisplayLink,
  NativeSurface, Theme, Col, TypeScale, TagPalette, UiPreferences, BundledFont, and the shaders vs_ui/fs_ui/varying).
  Product literals in gui/ exist only in `UiPreferences.cpp:29,38` (folder name, env var) and `BundledFont.h:3,17-18`
  (font header and namespace). Route those through one CMake-generated `ProductConfig.h` (`configure_file`) so that
  gui/ has no product names, and rename the namespace with sed (`hrvbgui`→`fcmpgui`, or a neutral `funkgui`) so that a
  sed-normalised diff against HR stays clean for future back-ports. Also copy `Tools/Harness.h`,
  `Tools/{FrameRender,FontProbe,AtlasDump,PrefsCheck}.cpp`, `Resources/fonts`, `Resources/licences`, `LICENSE`,
  `Scripts/*`, `dsp/Simd.h` (only if SIMD is wanted), and the preset **headers**.
- **Copy as a pattern, rewrite the body:** `PluginProcessor` (`render()` with bypass ramp, chunk loop, unprepared guard,
  `raw_` polling, state get/set with absent→default, snap on recall), `FDNReverb`'s telemetry (turned into a generic
  `Seqlock<T>`), the smoothing machinery (block-length-compensated one-pole plus `smoothSnap`), `PresetPanel` (decouple
  it from the processor type and re-layout), `BgfxEditor` (new layout, same frame-pump, accessibility and capture-hook
  plumbing), and `StateProbe`.
- **Do not copy:** `FDNReverb`, `DelayLine` (a Thiran allpass, the wrong tool for integer lookahead; only its O(1)
  `clean_` reset idea transfers), `LFO.h`, `Hadamard.h`, the reverb probes, `advancedUI`, the SME hook, and the classic
  `PluginEditor`.

### 7.4 Parameter model for Modes and stepped values (recommendation)
1. **One stable, superset parameter set for all Modes.** The ids, ranges and skews are final at v1: host automation is
   stored **normalised**, so a later range change remaps recorded automation even though APVTS session state is plain.
   Ranges must cover every Mode, for example ratio 1..∞ with a `∞` text at the top, and attack and release spanning
   the fastest and slowest units.
2. **Stepped controls are continuous host floats plus a per-Mode quantiser.** `ModeDescriptor` lists, per parameter,
   `Kind{continuous, stepped, fixed, unused}`, `steps[]` in plain units with display labels (e.g. `{2,4,10}` shown as
   "2 / 4 / 10"), an optional sub-range, and a fixed value. One pure function
   `effective(mode, param, raw) → plain` is called by (a) the audio thread in `pushParametersToEngine`, (b) the editor,
   (c) the transfer-curve view and (d) the `valueToText` lambda, which captures a pointer to the processor's current-mode
   atomic so the host lane prints the snapped value. The UI draws stepped parameters as HR-style segmented cells
   (`BgfxEditor.cpp:224-226, 1709-1720`) and writes `convertTo0to1(stepValue)` inside a gesture. When the user changes
   Mode from the UI (message thread), snap the raw values with `setValueNotifyingHost` so the host lanes agree. Host
   automation is snapped silently on the audio thread. **Never write parameters from the audio thread.**
3. **The Mode parameter.** A growing `AudioParameterChoice` changes the normalised↔index mapping every time a Mode is
   added (`index = round(norm*(n-1))`), which breaks recorded Mode automation and normalised host storage. Use a
   fixed-capacity `AudioParameterInt("mode", 0..63)` (or a Choice with 64 slots, unused ones named "—"), preferably
   `.withAutomatable(false)`, append-only. Also write a stable string `modeId` into the state root and the preset, so a
   future re-ordering can be resolved by id.
4. **Presets:** follow HR's plain-unit contract. The Mode is a preset parameter, and the file stores `modeId` too.
   `isPresetParameter` excludes `bypass` and any performance latches.
5. **Latency:** keep one constant reported latency across Modes (the maximum lookahead or oversampling delay, with padded
   dry paths), set in `prepareToPlay`. Changing latency on a Mode switch is handled poorly by many hosts during
   playback. The bypass ramp and Mix must use the **latency-aligned dry** signal.
6. **State:** add a `stateVersion` attribute to the root. Keep HR's absent→default loop (`.cpp:467-476`). Actually call
   `PresetManager::writeState/readState`. Snap the smoothers on recall.
7. **Buses:** main stereo/stereo and mono/mono (optionally mono→stereo, as HR does), plus an optional **sidechain**
   input bus: `.withInput("Sidechain", stereo(), false)`. `isBusesLayoutSupported` accepts disabled, mono or stereo.

### 7.5 Telemetry for the characteristics screen
- `UiFrame` (seqlock, published per block while the editor exists): mode id, sample rate, the effective (snapped)
  parameter values, detector level dB, current and block-max GR dB, input and output peak/RMS per channel (instant
  attack, 40 ms release as in HR), sidechain level, and a small fixed `float internals[16]` whose meaning comes from the
  `ModeDescriptor` (e.g. program-dependent release state, opto "memory", FET bias).
- A `HistoryRing` SPSC FIFO of fixed-rate points (for example every 1 ms: in dB, out dB, GR dB, detector dB) for
  scrolling GR/level history, so nothing between UI frames is lost.
- The transfer-curve view evaluates `GainComputer` (the same header the DSP uses) over x ∈ [−60, 0] dBFS and draws the
  live detector point from `UiFrame`.

### 7.6 Build commands
**Dependency reuse is verified.** HR's own `build-universal` was configured with
`FETCHCONTENT_SOURCE_DIR_BGFX=/Users/seanfunk/audio/plugins/HardwareReverb/build/_deps/bgfx-src` and
`..._JUCE=.../juce-src` and produced full artefacts (its CMakeCache). I also configured a scratch project that pinned
JUCE 8.0.4 and bgfx.cmake `v1.153.9385-561` with the same bgfx options against those two directories, in my scratchpad.
Results:
- Configure succeeded in **15.9 s**, including building juceaide. Targets `shaderc`, `bgfx` and
  `juce::juce_audio_processors` all resolved.
- Building `shaderc` and `bgfx` took **183 s wall on 10 cores** (no build type set, so unoptimised; a Release build will
  differ).
- **Nothing was written** under HR's `_deps/*-src` (checked with `find -newermt`).
- `juce-src` is 181 MB and `bgfx-src` is 642 MB. `bgfx-src` is a grafted shallow clone at tag `v1.153.9385-561` with
  `bgfx/`, `bx/` and `bimg/` populated. `juce-src` is at tag 8.0.4.
- bgfx.cmake's `configure_file` outputs go to the binary dir only (`cmake/bgfx/shared.cmake:12-20`).

```sh
J=/Users/seanfunk/audio/plugins/HardwareReverb/build/_deps/juce-src
B=/Users/seanfunk/audio/plugins/HardwareReverb/build/_deps/bgfx-src
F=/Users/seanfunk/audio/plugins/FCompressor

# Owner's tree: bgfx GUI, installs into ~/Library/Audio/Plug-Ins
cmake -S $F -B $F/build -G Ninja -DFETCHCONTENT_SOURCE_DIR_JUCE=$J -DFETCHCONTENT_SOURCE_DIR_BGFX=$B -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build $F/build

# Fast agent tree: no bgfx, no install (the equivalent of HR's build-dsp)
cmake -S $F -B $F/build-dsp -G Ninja -DFCOMPRESSOR_CLASSIC_GUI=ON -DFCOMPRESSOR_INSTALL_AFTER_BUILD=OFF \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=$J -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build $F/build-dsp --target verify          # or a single probe, e.g. FCompressorStateProbe

# GUI agent tree: bgfx, no install
cmake -S $F -B $F/build-gui -G Ninja -DFCOMPRESSOR_INSTALL_AFTER_BUILD=OFF \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=$J -DFETCHCONTENT_SOURCE_DIR_BGFX=$B -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build $F/build-gui --target verify verify-gui   # verify-gui needs a window server

# Shipping build
cmake -S $F -B $F/build-universal -G Ninja -DFCOMPRESSOR_UNIVERSAL=ON -DFETCHCONTENT_SOURCE_DIR_JUCE=$J \
      -DFETCHCONTENT_SOURCE_DIR_BGFX=$B -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build $F/build-universal && Scripts/release.sh $F/build-universal
```
Notes:
- With `FETCHCONTENT_FULLY_DISCONNECTED=ON` and bgfx enabled, you **must** pass `FETCHCONTENT_SOURCE_DIR_BGFX`.
  Otherwise FetchContent assumes `build/_deps/bgfx-src` already exists and fails.
- Encode these configurations in `CMakePresets.json`. A preset schema version of 3 or lower works with CMake 3.22.
- **Fragility:** this ties FCompressor to HR's `build/` directory, which the user may wipe. A sturdier option is a
  one-time `cp -R` of both source trees to a shared location outside HR, for example
  `/Users/seanfunk/audio/plugins/.deps/{JUCE-8.0.4,bgfx.cmake-v1.153.9385-561}` (about 823 MB), pointed to by the
  presets. Copying out of HR only reads it.
- **Parallel agents:** give each agent its own build directory (`build-dsp-<agent>`). Never run two ninjas in one build
  directory. Always use `INSTALL_AFTER_BUILD=OFF` except in the owner's `build/`. Consider `git init` for FCompressor:
  parallel sprint agents benefit from worktrees and diffs, and HR relies on golden files as its only history because it
  has no git.
