# Web demo scout report: FCompressor's editor without JUCE

Written by a read-only scout before Sprint A (ADR-93 design pass, 2026-10-01). Line numbers are as of main 3731e07.
Statements about WebAssembly behaviour were not built unless the report says a scratch test ran.

## Summary

Paths below: FC = /Users/seanfunk/audio/plugins/FCompressor, FG = /Users/seanfunk/audio/libraries/FunkGui. Nothing was built or edited.

The Panel is close to JUCE-free already. Of the 27 editor translation units outside `Source/editor/gpu` (17.8k lines), 5 include JUCE directly and 2 more get it through one FunkGui header. Every direct JUCE use in the views is behind an `ownerComponent() == nullptr` early return that the headless probes already take, so the same code paths are simply dead in a browser unless a replacement is provided.

**What each file uses JUCE for**
- `PreviewWorker.cpp`: `juce::Thread` only (worker, wait/notify, stopThread). With `PanelOptions::syncPreview` no thread is created.
- `EditControls.cpp`: `juce::PopupMenu` + `funkgui::MenuLook` for "Copy A to B", and `#if JUCE_MAC` for the Cmd/Ctrl name.
- `PresetStrip.cpp`: `juce::PopupMenu` + MenuLook for SAVE's context menu (Save / Save As).
- `PresetBrowser.cpp`: `juce::PopupMenu` (row menu, category menu), `juce::FileChooser`, `juce::File` and `juce::String` (import/export).
- `Settings.cpp`: one line, `juce::SystemClipboard::copyTextToClipboard`.
- `Panel.cpp` and `views/AnimationModel.cpp`: only through `funkgui/prefs/UiPreferences.h`, which includes `juce_data_structures`.
- `gpu/Editor.h/.cpp`: `juce::AudioProcessor`, `funkgui::EditorHost`, `juce::Logger`. Not compiled for web; a web host replaces it.

No editor file uses JUCE for time or strings beyond the above; the clock is `HostServices::nowSeconds()`.

**Smallest seam set**
1. `PreviewWorker.cpp`: swap `juce::Thread` for `std::thread`, or run the demo with `syncPreview`.
2. Menus, file choosers and clipboard: move behind three additive `funkgui::HostServices` virtuals, or split the JUCE functions of the four views into platform files.
3. `FactoryBank.cpp`: split the constant entry table and its default-filling from the `funkgui::presets::Preset` conversion, so the factory bank is available without JUCE.
4. A new `WebFacade` in a new directory (the lint forbids `EngineHost` inside `Source/editor`).
5. FunkGui prerequisites, outside my area: JUCE-free `UiPreferences`, `FontAtlasSdf` bake, `LineEdit.cpp`, and a web host in place of `EditorHost`.

**Can an existing facade drive it?** Not as they stand. `FakeFacade` pulls JUCE through `FactoryBank.h`, holds non-atomic values, logs every write into an unbounded vector, and its `apply()` only sets the Mode. `EngineFacade` is single-threaded and records all input and output. A new `WebFacade` should combine `FakeFacade`'s port, `EngineFacade`'s block-building and reconfigure recipe, and `Processor`'s atomics and batch epoch.

**How the four areas work through `WebFacade`**
- **Parameters:** 29 atomic plain values; a port stores `fcdsp::toPlain(pid, v01)` and reports gestures to `EditHistory`. The audio callback builds `BlockParams` exactly as `EngineFacade::blockParams` does.
- **Undo and A/B:** `EditHistory` is plain C++ and reusable verbatim over an 8-method `Host`.
- **Presets:** a new `WebPresets` over the JUCE-free factory table, with user presets in memory. `Presets.cpp` cannot be reused (APVTS, `PresetManager`, SQLite store, `juce::File`).
- **Telemetry:** `EngineHost::readUiFrame` and `history()` unchanged if the audio thread shares wasm memory; otherwise mirror posted frames and columns into a UI-side ring.

**What degrades in a demo**
- User presets last only for the session; import, export and preset file drops are refused.
- Popup menus are absent unless the menu service is built. "Copy A to B" and the Save As category are menu-only today, so they become unreachable.
- COPY REPORT is silent without a clipboard service.
- Settings diagnostics show a dash for JUCE; "NEW INSTANCES" preferences are inert; the display row prints a hard-coded "METAL".
- Host parameter menus are no-ops; session state and state notices do not exist.

`Source/plugin/Text.cpp` does not exist: host text is `HostText.cpp` (JUCE, not needed), and the editor formats values through the JUCE-free `fcdsp/params/Text.h`.

## Facts

- **ProcessorFacade.h is JUCE-free: it includes only fcdsp headers, funkgui/params/ParamPort.h and std headers, and declares PresetAccess, EditAccess, Diagnostics and ProcessorFacade (ports, currentRaw, readUiFrame, history, setUiAttached, uiState, stateNotice, begin/endBatch, presets, edits, diagnostics).**
  Evidence: FC/Source/plugin/ProcessorFacade.h:7-14 (includes), :31-57 (PresetAccess), :63-77 (EditAccess), :88-104 (Diagnostics), :106-128 (ProcessorFacade); FG/include/funkgui/params/ParamPort.h (no JUCE include, native() returns void*)
- **Outside Source/editor/gpu, exactly five editor .cpp files include a JUCE header directly: PreviewWorker.cpp, views/EditControls.cpp, views/PresetBrowser.cpp, views/PresetStrip.cpp, views/Settings.cpp. There are 27 non-gpu editor .cpp files, 17,767 lines.**
  Evidence: FC/Source/editor/PreviewWorker.cpp:25; views/EditControls.cpp:21; views/PresetBrowser.cpp:25; views/PresetStrip.cpp:24; views/Settings.cpp:26 (grep of 'juce' under Source/editor; find/wc count)
- **Two more editor TUs pull JUCE transitively through funkgui/prefs/UiPreferences.h, which includes juce_data_structures and exposes juce::File and juce::PropertiesFile.**
  Evidence: FC/Source/editor/Panel.cpp:35, :206, :446; FC/Source/editor/views/AnimationModel.cpp:7, :31, :67; FG/include/funkgui/prefs/UiPreferences.h:3, :67, :73, :78
- **PreviewWorker uses JUCE only for juce::Thread (name, threadShouldExit, wait(100), notify, startThread, signalThreadShouldExit, stopThread(1000)); the queue and handoff already use std::mutex. With synchronous=true the job runs inside tick() and no thread is ever created.**
  Evidence: FC/Source/editor/PreviewWorker.cpp:219-221 (compute takes const juce::Thread*), :243-272 (Worker), :352-357 (sync path returns before thread creation), :362-367 (thread start), :381-387 (stop); Panel passes options.syncPreview at FC/Source/editor/Panel.cpp:201
- **A synchronous preview renders 3 attack runs and 2 release runs (1.25 s and 12.05 s of samples each, about 27.85 s of audio at full rate) per request on the calling thread, rate-limited to 20 Hz, only while CHARACTERISTICS is the target screen. The wasm cost was not measured.**
  Evidence: FC/Source/editor/PreviewWorker.h:16-19, :39-40; FC/Source/editor/PreviewWorker.cpp:5-9 (stimulus lengths), :17 ('stop() joins within one run (<= ~0.1 s at 192 kHz)'), :57 (kMinIntervalS)
- **EditControls.cpp uses JUCE for one popup menu ('Copy A to B' / 'Copy B to A' via juce::PopupMenu + funkgui::MenuLook, anchored with owner->localAreaToGlobal), a dismissAllActiveMenus in the destructor, and #if JUCE_MAC for commandOnly()/commandKeyName(). showMenu returns early when ownerComponent() is nullptr.**
  Evidence: FC/Source/editor/views/EditControls.cpp:16, :21, :84-100 (#if JUCE_MAC), :107, :302-326 (showMenu; early return :304-306)
- **EditAccess::copySlot() is reachable only through that popup menu (right-click on A/B, or the a11y showMenu action). Without a menu service, A/B select works but copy does not.**
  Evidence: FC/Source/editor/views/EditControls.cpp:244-248 (e.popup -> showMenu), :322 (the only copySlot call), :386-387 (a11y showMenu)
- **JUCE_MAC is undefined in a JUCE-free TU and the warning list has no -Wundef, so EditControls would silently take the non-Mac branch: Ctrl is the command key and the footer says CTRL, also in a Mac browser.**
  Evidence: FC/Source/editor/views/EditControls.cpp:86-99; FC/cmake/FcmpArch.cmake:103-112 (FCMP_WARNING_FLAGS, no -Wundef); Panel's undo chord uses commandOnly at FC/Source/editor/Panel.cpp:700-701
- **PresetStrip.cpp uses JUCE only for SAVE's context menu (Save / Save As through juce::PopupMenu + MenuLook) and the destructor's dismissAllActiveMenus; headless it returns early. Both commands are also reachable without the menu (the SAVE click, and the browser's SAVE AS cell).**
  Evidence: FC/Source/editor/views/PresetStrip.cpp:18, :24, :108-113, :388-419 (early return :390-392), :421-431 (activate: save without the menu); FC/Source/editor/views/PresetBrowser.cpp:1298-1329 (saveAs action cell)
- **PresetBrowser.cpp uses JUCE for: juce::FileChooser + juce::File + juce::FileBrowserComponent (import, several files; export with a legal file name in Documents), two juce::PopupMenu menus (row/background menu; Save As category menu) with MenuLook, juce::String conversions, and dismissAllActiveMenus. All four entry points return early without an owner component.**
  Evidence: FC/Source/editor/views/PresetBrowser.cpp:20, :25, :254-259, :1090-1109 (chooseImport), :1111-1139 (chooseExport), :1215-1252 (showMenu), :1254-1294 (showCategoryMenu)
- **The Save As category can only be chosen through showCategoryMenu, so without a menu service every saved preset is 'NO CATEGORY'. IMPORT is always enabled and EXPORT is enabled for any selected row, so in a browser they are dead clicks.**
  Evidence: FC/Source/editor/views/PresetBrowser.cpp:1924-1926 and :2005-2008 (the only callers besides a11y :2377), :1298-1313 (actionEnabled), :1325-1326
- **The view headers hold std::unique_ptr members of forward-declared JUCE-side types (funkgui::MenuLook in three views, juce::FileChooser in PresetBrowser). A JUCE-free .cpp cannot destroy them, so either the members change or the menu/chooser code leaves the views.**
  Evidence: FC/Source/editor/views/EditControls.h:27, :100; PresetStrip.h:61, :153; PresetBrowser.h:105-113, :333-334; FG/include/funkgui/juce/MenuLook.h:19 (includes juce_gui_basics)
- **Settings.cpp uses JUCE for exactly one call: juce::SystemClipboard::copyTextToClipboard(juce::String::fromUTF8(report())) in copyReport(), which returns false headless. The diagnostics rows read Diagnostics::juce and print a dash when it is empty.**
  Evidence: FC/Source/editor/views/Settings.cpp:26, :562-571 (owner check :564, clipboard :567), :338-339
- **Settings prints a hard-coded "METAL" whenever RenderInfo::gpu is true, and RenderInfo has no renderer name, so a WebGL host would be labelled METAL.**
  Evidence: FC/Source/editor/views/Settings.cpp:485-489; FC/Source/editor/SubView.h:168-176
- **gpu/Editor is the JUCE host glue (a funkgui::EditorHost built on juce::AudioProcessor, juce::Logger, JUCE_LEAK_DETECTOR). What a web host must replicate from it: setUiAttached forwarding, the zoom steps and default, Panel::setRenderInfo, an optional initial view, and Panel::shutdown() before teardown.**
  Evidence: FC/Source/editor/gpu/Editor.h:44-45, :60-80; FC/Source/editor/gpu/Editor.cpp:44-56 (makeConfig), :58-64 (makePanel), :72-83 (setRenderInfo), :86-91 (juce::String/Logger), :95-101 (teardown order)
- **Panel reaches host services only through funkgui::HostServices, a JUCE-free header that forward-declares juce::Component for ownerComponent() (default nullptr). The editor uses nowSeconds, showParamMenu, setUnboundedDrag, nudgeFullRate, themeIndex, the zoom calls and begin/endBatch.**
  Evidence: FG/include/funkgui/panel/HostServices.h:8-11, :22-32, :43-50, :63-83; FC/Source/editor/Panel.cpp:164-193 (HostProxy forwards all, batches reach the facade :179-188)
- **Panel construction needs two FunkGui singletons whose implementations use JUCE today: FontService (the atlas is baked by JUCE's glyph rasteriser and needs JUCE's GUI side initialised) and UiPreferences (juce::PropertiesFile). The atlas is used for text measurement, so layout depends on its metrics.**
  Evidence: FC/Source/editor/Panel.cpp:202, :206; FG/src/text/FontService.cpp:8-11, :41-48; FG/src/text/FontAtlasSdf.cpp:3, :85, :121; FG/src/prefs/UiPreferences.cpp:18-39, :96-99; FC/Source/editor/SubView.h:192; probes hold juce::ScopedJuceInitialiser_GUI for this (FC/Tools/probes/plugin/ui_dump.cpp:218)
- **FunkGui core sources that include JUCE: SoftRaster.cpp (PNG write), FontAtlasSdf.cpp, LineEdit.cpp (only the juce::String printable overload), UiPreferences.cpp, juce/MenuLook.cpp, params/JuceParamPort.cpp. Canvas, PrimList, HeadlessHost, GestureController, CaptureConfig, Env and all widgets do not; ThemeCells and SegmentedSelector call UiPreferences.**
  Evidence: grep -l juce over FG/src; FG/src/canvas/SoftRaster.cpp:3, :264-283; FG/src/text/LineEdit.cpp:5, :85-92; FG/src/widgets/ThemeCells.cpp:25-51; FG/src/widgets/SegmentedSelector.cpp:379-431
- **FakeFacade is not JUCE-free and not suitable as a live facade: it includes FactoryBank.h and funkgui/presets/PresetTypes.h (juce::String), its ports hold a plain non-atomic float, every setValue01 is appended to an unbounded vector, FakePresets::apply only scripts the Mode (no parameter values), and diagnostics are fixed numbers.**
  Evidence: FC/Tools/probes/plugin/FakeFacade.cpp:5, :11, :78-92, :49-57, :121-134, :477-500, :516-519; FC/Tools/probes/plugin/FakeFacade.h:93
- **EngineFacade already drives a real fcdsp::EngineHost with no JUCE in its own code: blockParams() is processBlock's raw -> resolveSlot -> resolve -> BlockParams recipe, reconfigure() is SetupWatcher's configure, and the outermost endBatch raises requestSnap. It is single-threaded, renders a deterministic Program, and appends all input and output to growing vectors, so it is a model, not a reusable class.**
  Evidence: FC/Tools/probes/plugin/EngineFacade.cpp:128-146, :148-162, :115-120, :166-192 (recording :186-189); FC/Tools/probes/plugin/EngineFacade.h:17, :124-131
- **EditHistory is plain C++ (std only plus fcdsp/params/HostParams.h) over an 8-method Host interface (raw, write, beginBatch, endBatch, presetUuid, restorePreset, loadSerial, onMessageThread). FakeFacade already shows a JUCE-free Host, and its port and batch hooks call gestureBegan/gestureEnded/batchBegan/batchEnded.**
  Evidence: FC/Source/plugin/EditHistory.h:35-45, :52-63, :79-82; FC/Source/plugin/EditHistory.cpp:2-8; FC/Tools/probes/plugin/FakeFacade.cpp:42-47, :59-65, :424-447; the processor's history batch ends without the snap at FC/Source/plugin/Processor.cpp:483
- **The factory bank's data is JUCE-free constant data (Entry/Value structs, per-Mode .inc files, FCMP_FACTORY_BANK_REVISION from a configure-generated header), but it sits in FactoryBank.cpp's anonymous namespace and is exposed only as funkgui::presets::Preset (juce::String, juce::int64). materialise() fills unstated parameters with pure fcdsp calls and then converts.**
  Evidence: FC/Source/plugin/factory/FactoryBank.h:21-22, :37-40; FC/Source/plugin/factory/FactoryBank.cpp:61-82 (Entry, kEntries), :87-136 (materialise: fcdsp part to ~:120, juce conversion after), :165; FG/include/funkgui/presets/PresetTypes.h:16, :75-121; FC/cmake/FcmpSources.cmake:151-188
- **The processor's PresetAccess (Presets.cpp) cannot be reused without JUCE: it is built on juce::AudioProcessorValueTreeState, funkgui::presets::PresetManager, the SQLite PresetStore through juce::SharedResourcePointer, PresetFile XML and juce::File. Its semantics to copy: apply inside one batch with the Mode written first, modified() also true when the Mode differs from the baseline, step() wrap, list = bank then users by name.**
  Evidence: FC/Source/plugin/Presets.cpp:71-74, :121-127, :132-141, :227-233, :251-268, :383-400, :414-422, :457-462, :517; tolerance rule at FG/src/presets/PresetManager.cpp:98-100, :178-192
- **Processor.cpp shows what a web facade must copy without JUCE: the batch depth/epoch/snap scheme, pullBlockParams, render -> EngineHost::process with DSP-load timing, diagnostics(), and SetupWatcher's 20 Hz quality/labudget reconfigure under suspendProcessing. JUCE-bound parts are the APVTS atomics, juce::Time ticks, PluginHostType, suspendProcessing/setLatencySamples/updateHostDisplay, and MessageManager thread checks.**
  Evidence: FC/Source/plugin/Processor.cpp:409-433, :595-607, :609-657 (juce::Time :654-656), :659-675, :493-520, :708-760, :81, :491; FC/Source/plugin/SetupWatcher.h:25, :33
- **The telemetry primitives are std::atomic-only and JUCE-free: UiFrame through a Seqlock, HistoryRing as a claim-word SPSC ring, both gated by the attach count. The Panel reads them only through facade.readUiFrame and facade.history().**
  Evidence: FC/Source/fcdsp/telemetry/HistoryRing.h:8-18, :47-58; FC/Source/fcdsp/telemetry/Seqlock.h:28-29; FC/Source/fcdsp/engine/EngineHost.h:6-9; FC/Source/editor/Panel.cpp:397-409, :452
- **The parameter map a web port needs is JUCE-free: fcdsp::toPlain is declared the only normalised-to-plain map, with toNorm and legal beside it; FakePort already implements a ParamPort over them.**
  Evidence: FC/Source/fcdsp/params/HostParams.h:39-41; FC/Tools/probes/plugin/FakeFacade.cpp:36-40, :49-53, :67-69
- **There is no Source/plugin/Text.cpp. Host value text is HostText.cpp (juce::String, includes Processor.h); the editor formats values through the JUCE-free fcdsp/params/Text.h. State.h/State.cpp/StateMigration.cpp are JUCE (ValueTree, XML, MemoryBlock, APVTS).**
  Evidence: ls FC/Source/plugin; FC/Source/plugin/HostText.cpp:18-23, :55-81; FC/Source/editor/SlotModel.cpp:17, views/SlotGrid.cpp:14, views/TransferPlot.cpp:21, views/OutputTrim.cpp:10; FC/Source/plugin/State.h:32, :52-58, :65, :75, :92; FC/Source/plugin/State.cpp:226-281
- **File drops reach presets as Panel::filesDropped -> PresetBrowser::filesDropped -> importFiles -> PresetAccess::importFile(path); only paths ending in .fcmppreset are of interest. A dropped audio file is not the Panel's business and must be handled by the web shell.**
  Evidence: FC/Source/editor/Panel.cpp:823-832; FC/Source/editor/views/PresetBrowser.cpp:986-999; FC/Source/plugin/ProcessorFacade.h:47
- **The layer lint forbids EngineHost and any plugin/ include other than ProcessorFacade.h inside Source/editor, and forbids editor/ includes inside Source/plugin, so a WebFacade that owns an EngineHost and constructs a Panel must live in a new directory. All source lists are per-directory globs and editor variants are chosen by CMake, never by #if.**
  Evidence: FC/cmake/LintDeps.cmake:27-30; FC/cmake/FcmpSources.cmake:85-98, :100 (editor choice 'never by #if'); FC/CLAUDE.md Layer rules
- **Today no build compiles the editor without JUCE: the headless UI probes run the Panel through funkgui::HeadlessHost inside fcmp_probe_plugin, which links FunkGui::core, FunkGui::presets and juce_audio_processors. The DSP-only configuration (no JUCE) builds only fcdsp and FunkGui::harness.**
  Evidence: FC/cmake/FcmpProbes.cmake:81-97; FC/CMakeLists.txt:10-11, :48-49; FC/cmake/FcmpDeps.cmake:245-250
- **Emscripten 6.0.3 on this machine ships the Wasm Audio Worklet API and a requestAnimationFrame loop, so one wasm module with the audio callback sharing the UI's linear memory is available.**
  Evidence: emcc --version: 6.0.3-git; /opt/homebrew/Cellar/emscripten/6.0.3/libexec/system/include/emscripten/webaudio.h:43, :84, :139, :172; html5.h:472

## Blockers and options

- **FunkGui core cannot be compiled without JUCE, and the Panel constructor needs two of the affected pieces: FontService's atlas (baked by JUCE's glyph rasteriser) and UiPreferences (juce::PropertiesFile, with juce::File in its public header). LineEdit.cpp and SoftRaster.cpp also include JUCE. Text measurement drives layout, so a different rasteriser could shift the web UI against native.** (FG/src/text/FontAtlasSdf.cpp:3,85,121; FG/src/text/FontService.cpp:8-11; FG/include/funkgui/prefs/UiPreferences.h:3,67,73; FG/src/text/LineEdit.cpp:5,85-92; FG/src/canvas/SoftRaster.cpp:3,264-283; consumed at FC/Source/editor/Panel.cpp:202,206,446)
  Options: (a) FunkGui gains a JUCE-free core configuration: atlas loaded from a blob pre-baked by a native JUCE tool at build time (identical metrics to native), UiPreferences over an in-memory or localStorage backend with the juce::File accessors moved out of the header, the juce::String printable overload moved to a JUCE-only TU, SoftRaster's PNG writer excluded. (b) Bake with stb_truetype or FreeType in wasm: simpler build, but metrics and goldens may differ. This is FunkGui work and must land, and be tagged, before any FCompressor web target links.
- **Menus, file choosers and the clipboard are JUCE calls inside four product views, and the view headers own unique_ptr<funkgui::MenuLook> and unique_ptr<juce::FileChooser>, which a JUCE-free TU cannot destroy. 'Copy A to B' and the Save As category are reachable only through these menus.** (FC/Source/editor/views/EditControls.cpp:302-326 and EditControls.h:100; PresetStrip.cpp:388-419 and PresetStrip.h:153; PresetBrowser.cpp:1090-1139, 1215-1294 and PresetBrowser.h:333-334; Settings.cpp:562-571)
  Options: (a) Recommended: add three additive HostServices virtuals with no-op defaults (show a menu from a list of {id, label, enabled, checked, separator} at a Panel rect with a completion callback; choose files to open or save with a callback of paths; copy text). EditorHost implements them with PopupMenu + MenuLook, FileChooser and SystemClipboard; the web host with an overlay or DOM menu and navigator.clipboard; HeadlessHost logs them, so probes can finally test menus. The views lose every JUCE include and the two members. This needs a FunkGui minor version and a pin bump, and edits FZ4-frozen view headers (private members only). (b) No FunkGui change: move the JUCE functions of each view into views/native/*.cpp and add views/web/*.cpp stubs, selected by CMake glob, with the members behind a pimpl. Menus, import/export and COPY REPORT are then simply absent in the demo, and the same header edits are still needed.
- **PreviewWorker's juce::Thread. The synchronous path avoids the thread but renders about 27.85 s of control-path audio per request on the browser's main thread while CHARACTERISTICS is open; the wasm cost is unmeasured.** (FC/Source/editor/PreviewWorker.cpp:243-272, 352-367, 381-387; FC/Source/editor/PreviewWorker.h:16-19)
  Options: (a) Replace juce::Thread with std::thread + condition_variable + atomic<bool> on every platform (about 40 lines, behaviour unchanged, removes JUCE from the file) and run the web demo with syncPreview=true at first. (b) Same, and use the worker in the web build when it is built with pthreads (already needed for a shared-memory audio worklet); stop() joins, so the thread pool must be pre-sized. (c) Add a time-sliced mode (one run per tick) if (a) janks and threads are not available. Measure (a) in the browser before choosing (b) or (c).
- **The factory bank is only available as funkgui::presets::Preset (JUCE strings), so neither a web PresetAccess nor FakeFacade can list or apply factory presets without JUCE and FunkPresets.** (FC/Source/plugin/factory/FactoryBank.h:21-22,37-40; FC/Source/plugin/factory/FactoryBank.cpp:61-136,165)
  Options: (a) Recommended: split out a JUCE-free FactoryData.h/.cpp (same directory, picked up by the existing glob) exposing a span of {uuid, name, category, notes, modeKey, modeRev, 22 plain values} built by the existing fcdsp-only half of materialise(); FactoryBank.cpp converts that to Preset. One source of truth; proc.presets keeps checking it. (b) Generate a second table for web at configure time: no change to shipped code, but two representations to keep equal.
- **Threading model of the web audio path decides how the facade's telemetry and parameter reads work. This is another agent's area, but the facade design depends on it.** (FC/Source/fcdsp/telemetry/HistoryRing.h:8-18; FC/Source/fcdsp/telemetry/Seqlock.h:28-29; FC/Source/plugin/Processor.cpp:409-433, 595-607; emscripten webaudio.h:84,172)
  Options: (a) Wasm Audio Worklet with shared memory: one module; WebFacade is 'Processor without JUCE' and the seqlock, ring and batch epoch work verbatim. Needs pthreads and cross-origin isolation headers on the demo host. (b) Separate worklet instance with message passing: the UI side keeps mirrored atomics, a posted UiFrame and its own HistoryRing fed by posted columns (FakeFacade::publish/pushColumn are the model, FakeFacade.cpp:465-473); batches become one message. No special headers, more glue, telemetry delayed by a message hop. (c) Main-thread ScriptProcessor: single-threaded like EngineFacade, simplest, but deprecated and glitch-prone.
- **QUALITY and LOOKAHEAD changes need an engine reconfigure while audio is stopped; the processor uses juce suspendProcessing, which does not exist on the web, and a browser's main thread cannot block waiting on the audio thread.** (FC/Source/plugin/Processor.cpp:708-747; FC/Tools/probes/plugin/EngineFacade.cpp:148-162)
  Options: (a) WebFacade::pollSetup() at 20 Hz from the frame loop with an atomic handshake: request suspend, the audio callback acknowledges at a block start and outputs silence, the next poll configures and resumes. (b) Fix quality and lookahead for the demo: simpler, but the Settings cells would write parameters that never take effect.
- **Where the web glue lives and how it is checked. Source/editor may not name EngineHost and Source/plugin may not include editor/, and Source/plugin/*.cpp is globbed whole into JUCE targets.** (FC/cmake/LintDeps.cmake:27-30; FC/cmake/FcmpSources.cmake:88-93)
  Options: (a) New Source/web/ (WebFacade, WebPresets, entry point) with a new lint rule 'web.juce: no JUCE header', and a web source list of editor sources + Source/web + EditHistory.cpp + FactoryData.cpp. (b) Additionally build that same list natively without JUCE as a probe target, so the existing ui.* probes can run against WebFacade and the web path is verified by verify.sh without a browser. CMake, lint and docs are lead-only files.
- **Demo-scope decisions for features with no browser equivalent: user presets, preset import/export and preset file drops, session state, NEW INSTANCES preferences, host parameter menus, the JUCE_MAC command-key branch, and the hard-coded METAL label.** (FC/Source/plugin/ProcessorFacade.h:42-51; FC/Source/editor/views/PresetBrowser.cpp:1298-1313; FC/Source/editor/views/EditControls.cpp:86-99; FC/Source/editor/views/Settings.cpp:485-489; FC/Source/plugin/Processor.cpp:151-158)
  Options: User presets: in-memory for the session (FakePresets' rules), or persisted to localStorage in a small text format. Import/export: refuse (PresetAccess defaults return false; the IMPORT and EXPORT cells stay visible but dead), or later map to upload/download with a JUCE-free preset file parser. State: none, or a URL-hash/localStorage snapshot of the 29 values. Command key: have the web host set Mods.cmd for both Ctrl and Meta, or replace #if JUCE_MAC with a host-supplied flag. METAL: add a renderer name to RenderInfo. Each is a small product decision for the user.

## Recommended approach

Do it as four small FCompressor seams plus one new directory, after the FunkGui prerequisites.

**0. FunkGui first (other area).** A JUCE-free core configuration (pre-baked atlas, `UiPreferences` backend, `LineEdit` split), a web host that implements `HostServices` and feeds a PrimList to bgfx/WebGL, and the three new `HostServices` services (menu, file chooser, clipboard) with `EditorHost` and `HeadlessHost` implementations. Tag it; the lead bumps the pin.

**1. `PreviewWorker.cpp`.** Replace `juce::Thread` with `std::thread`, condition variable and `atomic<bool>`, same contract. The file becomes JUCE-free on all platforms; the web demo starts with `syncPreview=true`.

**2. The four views.** Route `EditControls`, `PresetStrip`, `PresetBrowser` and `Settings` through the new `HostServices` calls and drop their JUCE and `MenuLook` includes, the `menuLook_`/`chooser_` members and the `ownerComponent()` checks. Replace `#if JUCE_MAC` with a host-provided answer or a web-host key mapping. After this, nothing under `Source/editor` outside `gpu/` includes JUCE; add a lint rule to keep it so.

**3. `FactoryData.h/.cpp`.** Split from `FactoryBank.cpp`: the entry table and the fcdsp-only default filling, returning plain rows. `FactoryBank.cpp` and `FakeFacade` convert from it.

**4. New `Source/web/`** (JUCE-free, lint-checked):
- **`WebFacade : ProcessorFacade`** owns an `fcdsp::EngineHost` and 29 `std::atomic<float>` plain values.
  - Ports: `value01 = toNorm`, `setValue01` stores `toPlain(pid, clamp01(v))`, begin/endGesture call `EditHistory::gestureBegan/Ended` (as `Processor::HistoryPort` and `FakePort` do).
  - `currentRaw()`: `Processor::snapshot`'s logic.
  - `beginBatch`/`endBatch`: `Processor`'s depth, epoch and snap scheme, calling `batchBegan`/`batchEnded`.
  - Audio callback: pull `BlockParams` (`EngineFacade::blockParams` recipe, skipped while a batch is open), `requestSnap` after the pull, `EngineHost::process`, load timing with `emscripten_get_now`.
  - `readUiFrame`/`history`/`setUiAttached`: forward to the engine in the shared-memory model.
  - `uiState()`: a plain member. `stateNotice()`: default (serial 0, no notices).
  - `diagnostics()`: product version, FunkGui version, `juce=""`, `format="WEB"`, the AudioContext rate and 128-sample quantum, measured load.
  - `pollSetup()`: 20 Hz from the frame loop for quality/labudget with a suspend handshake.
- **`EditHistory`** verbatim, with a `Host` whose `write` stores the exact plain value, whose `endBatch` ends without the snap (as `Processor::HistoryHost`), `loadSerial` constant, `onMessageThread` true.
- **`WebPresets : PresetAccess`**: rows are `FactoryData` then in-memory user rows sorted by name.
  - `apply`: beginBatch, write `mode` from the row's modeKey, write the 22 values, record the baseline, endBatch.
  - `modified()`: half a step (or 1e-6 of the span) against the baseline, or the Mode differs.
  - `step` wraps; `saveAs`/`rename`/`remove`/`overwrite` follow `FakePresets`' rules; `currentUuid`/`restoreCurrent` for undo and A/B.
  - `importFile`/`exportFile` keep the refusing defaults.
- **Entry point**: build `WebFacade`, `Panel(facade, {syncPreview = true})`, attach the web host, `setUiAttached(true)`, `setRenderInfo`, run the frame loop, and call `Panel::shutdown()` before teardown.

**5. Verify natively.** Build the same source list without JUCE as a probe target and run the existing ui probes against `WebFacade`, plus a null test of `WebFacade`'s render against `EngineFacade`/`Processor`, before any browser run.

Lead-only files are touched throughout (CMake, lint, docs, a new ADR, the FunkGui pin) and the view headers are FZ4-frozen, so this needs lead revisions rather than plain agent cards.

## Effort notes

Rough sizes for the FCompressor side, from reading only (nothing built or measured):

- **PreviewWorker thread swap:** about 40 changed lines in one file.
- **Four views onto HostServices:** about 150-200 lines removed or rewritten across `EditControls.cpp`, `PresetStrip.cpp`, `PresetBrowser.cpp`, `Settings.cpp` and three headers. The existing ui probes cover the non-menu paths; menus become testable through `HeadlessHost` for the first time.
- **FactoryData split:** about 120 lines moved; `proc.presets` and the factory revision are unaffected if the table stays byte-identical.
- **`Source/web`:** about 600-800 new lines. `WebFacade` is roughly 300 (mostly transcribed from `Processor.cpp:347-433`, `:595-675` and `EngineFacade.cpp:128-162`), `WebPresets` roughly 250 (from `FakeFacade.cpp:94-359` plus a real apply), the rest is the `EditHistory` host and the entry point.
- **Lead-only work:** a web preset and toolchain, the source list, a new lint rule, an ADR, a native JUCE-free probe target.

**Dependencies and ordering.** The FunkGui prerequisites are the critical path: nothing in FCompressor links for web until FunkGui core builds without JUCE and has a web host. Seams 1 and 3 are independent of FunkGui and can land on main now with no behaviour change. Seam 2 needs the FunkGui API addition and pin bump.

**Risks.**
- Synchronous preview cost on the browser main thread is unmeasured.
- Atlas metrics decide whether the web layout matches native goldens.
- The view headers are FZ4-frozen.
- The shared-memory audio model needs cross-origin isolation on whatever hosts the demo.
- A hand-written `WebFacade` duplicates `Processor`'s batch and pull logic, so it needs its own null and batch probes to stay in step.

Not covered here (other areas): fcdsp's SIMD under wasm, bgfx/WebGL, audio decoding and the worklet itself.
