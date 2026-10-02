# Scout report: card E-2, "the views leave JUCE" (Web Sprint C)

State read: FCompressor `web/sprint-b` @ 776a598, FunkGui `web/sprint-b` @ f2641a9. Nothing was edited, built or run in either repository.

**Headline:** two existing `ui.settings` spec rows cannot survive E-2 (they assert the headless no-clipboard behaviour that HeadlessHost now replaces), so the plan's "zero drift on every existing row" needs one stated exception. Everything else holds, provided `Panel::HostProxy` forwards `services()` before any view reads it.

## 1. Facts

### A. JUCE under `Source/editor` outside `gpu/` (complete)
1. Four `.cpp` files include JUCE: `EditControls.cpp:16,21`, `PresetStrip.cpp:18,24`, `PresetBrowser.cpp:20,25` (each `funkgui/juce/MenuLook.h` plus `juce_gui_basics`), and `Settings.cpp:26` (`juce_gui_basics` only).
2. `juce::` / `JUCE_` in code:
   - `EditControls.cpp:86,95` (`#if JUCE_MAC`), `:107`, `:304-318`.
   - `PresetStrip.cpp:112`, `:390-411`.
   - `PresetBrowser.cpp:258`, `:1092-1107`, `:1113-1140`, `:1217-1251`, `:1256-1295`.
   - `Settings.cpp:564,567`.
   - `Panel.cpp:174` (`juce::Component* ownerComponent() override`).
3. JUCE-only declarations in headers:
   - `EditControls.h:27` (`class MenuLook;`), `:100` (`menuLook_`).
   - `PresetStrip.h:59-62` (forward declaration), `:153` (`menuLook_`).
   - `PresetBrowser.h:105-113` (`juce::FileChooser`, `MenuLook` forward declarations), `:333-334` (`menuLook_`, `chooser_`).
   - `Settings.h` has no JUCE member, only comments (`:28`, `:83`).
4. Everything else is comments (`PreviewWorker.cpp:5,9,62`, `PreviewCompute.cpp:2`, header prose). `AnimationModel.cpp` has no direct JUCE; it includes `funkgui/prefs/UiPreferences.h` (`:7`), as does `Panel.cpp:35`. No `jassert`/`DBG`/`jmin`-style macros anywhere.
5. `Settings.cpp:339` uses `d.juce` (a `Diagnostics` field): not a JUCE use, but a lint token rule must not match it.

### B. `Panel::HostProxy` (`Panel.cpp:164-193`)
6. It forwards `setUnboundedDrag`, `showParamMenu`, `nudgeFullRate`, `nowSeconds`, `themeIndex`, `ownerComponent`, `zoomPercent`, `setZoomPercent`, `zoomSteps`, `zoomFits`, and `beginBatch`/`endBatch` (facade bracket).
7. It forwards none of the six v0.12.0 calls. Through `ctx_.host` (`Panel.cpp:372`) a view today gets the defaults: `services()` = 0, `showMenu`/`chooseFiles`/`copyText` refuse, `commandKeyIsMeta()` = the compile platform (`HostServices.h:162-205`).

### C. Menus today, and the replacing call
All four build a `juce::PopupMenu` with a per-view `MenuLook` themed `productTheme(ctx_.host->themeIndex())`. The anchor is `Rectangle<float>(r*s).toNearestInt()` → `localAreaToGlobal`, with `s = owner->getWidth()/layout::kWidth`. All return early when `ownerComponent()` is null. Each callback is guarded `alive.expired() || id <= 0` and ends with `nudgeFullRate()`.

`EditorHost::showMenu` does the same scaling and rounding (`EditorHost.cpp:933-938`, `HostServicesJuce.cpp:73-118`). So the views pass `MenuRequest{items, anchor in Panel px (no ×s), theme = productTheme(themeIndex())}`.

| # | Site | Items (id, label, enabled, tick) | Anchor (Panel px) | Callback |
|---|---|---|---|---|
| 8 | `EditControls::showMenu` `:302-326`; from popup click on A/B (`:244-248`) and a11y showMenu on the group (`:386`) | one item: `slot_==0 ? (2,"Copy A to B") : (1,"Copy B to A")`, enabled, no tick | `{562,43,38,16}` (`E::kA.x`, `kA.y`, `kB.right()-kA.x`, `kA.h`) | `edits(ctx_).copySlot()`; the id is not read |
| 9 | `PresetStrip::showMenu` `:388-419`; from popup on SAVE (`:456`) and a11y showMenu (`:634`) | `menu()` `:365-376`: (1,"Save"), (2,"Save As..."), both enabled; `refresh()` first | `kSaveBox {556,21,44,18}` | `run(Command(id))` |
| 10 | `PresetBrowser::showMenu(entry, anchor)` `:1215-1252`; row popup (`:1846-1848`, after `select(entry,false)`), background popup (`:1850-1852`), a11y showMenu (`:2337-2339`, after `select(entry,true)`) | `menu()` `:1143-1166`. Row: (1,"Load"), sep, (2,"Save As..."), (3,"Rename...", `!factory`), (4,"Export..."), (5,"Import..."), sep, (6,"Delete", `!factory`). Background: (2,"Save As..."), (5,"Import..."), no separator | `rowRect(shown index)`; background `{p.x,p.y,1,1}` | captures the row's uuid; `at = indexOf(uuid)`; returns if the row is gone; `run(Command(id), at)` |
| 11 | `PresetBrowser::showCategoryMenu` `:1254-1296`; category release (`:1924-1925`), Return on focused category (`:2007-2008`), a11y press (`:2377`). Only when `edit_ == Edit::saveAs` | (1,"No Category", ticked iff `saveCategory_.empty()`); sep iff any names; (100+i, `names[i]`, ticked iff `sameNoCase`) in filter order | `{618,67,180,18}` (`kEntryCategoryX`, `kEntryCentreY-9`, `kEntryCategoryMaxW`, 18) | captures `names` by value; also guards `edit_ != Edit::saveAs`; sets or clears `saveCategory_`; `++revision_` |

12. Destructors: `~EditControls` (`:104-108`) calls `dismissAllActiveMenus()` unconditionally. `~PresetStrip` (`:109-114`) and `~PresetBrowser` (`:1058-1063`) call it only when `menuLook_` exists. All reset `alive_` first.

### D. Choosers and clipboard
13. Import (`chooseImport` `:1090-1109`):
    - Today: title "Import presets", start in Documents, pattern `"*" + kFileExtension` = `*.fcmppreset` (`FcmpProduct.h.in:23`), native, parented on owner, open + several files. Callback: `importFiles(paths)`; an empty list returns at `:999`.
    - Replacement: `chooseFiles({Mode::openMany, "Import presets", "*.fcmppreset"}, cb)`.
14. Export (`chooseExport` `:1111-1141`):
    - Today: title "Export preset", initial `Documents/createLegalFileName(e.name) + ".fcmppreset"`, save mode with overwrite warning. Callback: cancelled → return with no nudge; adds the extension via `hasFileExtension`/`withFileExtension`; `indexOf(uuid) < 0` → flash "THAT PRESET IS GONE", else `exportTo`; then nudge.
    - Replacement: `chooseFiles({Mode::save, "Export preset", "*.fcmppreset", suggestedName = e.name + ".fcmppreset"}, cb)`.
15. The host already does what the view did: Documents, legal stem with the extension put back, forced extension on the result (`HostServicesJuce.cpp:130-197`; HeadlessHost `withExtension`, `HeadlessHost.cpp:99-109,508-509`). The view must drop its own extension code and treat an empty `paths` as cancel.
16. `Settings::copyReport` (`:562-571`): returns false without an owner; otherwise `refresh()`, copy `report()`, `copiedUntil_ = seconds + kCopiedS`, `++revision_`, true. Called from pointer-up (`:815`), Return/Space (`:829`), a11y press (`:908`). Replacement: `ctx_.host->copyText(report())`, and COPIED only when it returns true.

### E. Where the cells are enabled
17. `PresetBrowser::actionEnabled` (`:1298-1313`) is the single gate for draw ink (`:1741`), click (`:1872`), cursor (`:2132`), a11y `enabled` (`:2256`), the Tab order (`:2304`) and `runAction` (`:1317`). Today `importFiles` is always true and `exportFile` is `sel != nullptr`.
18. The menu items "Export..." and "Import..." are always enabled (`:1162-1163`).
19. COPY REPORT, Copy A to B and the save-as category have no enabled state: always drawn, listed and focusable (`Settings.cpp:747-757,880-886,896-897`; `PresetBrowser.cpp:2162-2171,2301`).
20. EditorHost and HeadlessHost report all three services (`EditorHost.cpp:928-931`, `HeadlessHost.cpp:415-418`).

### F. Command key
21. `EditControls::commandOnly(const Mods&)` and `commandKeyName()` are public statics (`EditControls.h:56-57`), switched by `#if JUCE_MAC` (`.cpp:84-100`). Mac: `cmd && !ctrl && !alt`, "CMD". Elsewhere: `cmd && !alt`, "CTRL".
22. Callers: `Panel.cpp:701` (the undo chord), `EditControls.cpp:162,166` (footer lines). No probe calls them.
23. `JUCE_MAC` exists in `EditControls.cpp` only because of the JUCE include.

### G. The one clip
24. `PresetBrowser.cpp:1622-1625,1678`: `off = c.snapY(scrollPx_)`; `cut` is true when `off` is not a multiple of 20; then `pushClip({kGround.x, kList.y, kGround.w, kList.h})` = `{32, 88, 896, 220}`. It is the only clip in FCompressor.
25. Rows span x 186–920, so the x edges (32, 928) never cut anything. Only y 88 and 308 matter.
26. Scratch result (`scout-c/e2/snap.cpp`, `clang++ -O2 -ffp-contract=off`, `Canvas::snapY`'s formula): at dpi 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, `snapY(88)` and `snapY(308)` are bit-identical to 88 and 308. `h = bottom - top` is exactly 220, and popClip's `cy1 = (double)y + (double)h` is exactly 308.
27. No golden can move:
    - `ui.geometry` (dpi 1 and 2) draws the browser at rest, where `cut` is false and no clip is pushed.
    - `scroll.rows_clipped_to_list` and `scroll.rows_cut_at_edge` (`ui_presets.cpp:562-570`, dpi 2) compare with 88.0f and 308.0f, which stay exact.
28. At a web dpi of 1.5625 the edges move to 88.32 and 307.84 (device 138 and 481). Without the snap, y 88 is device 137.5, exactly the tie row that `WebGlSink.h:26-36` describes.

### H. Probes (`Tools/probes/plugin/ui_*.cpp`, 24 files)
29. Twenty-three files include `<juce_gui_basics/juce_gui_basics.h>` and hold one `juce::ScopedJuceInitialiser_GUI` (for example `ui_a11y.cpp:41,245`, `ui_presets.cpp:86,1436`, `ui_dump.cpp:46,218`). The exception is `ui_font_linux.cpp`, which has none.
30. Other JUCE uses:
    - `juce::Thread::sleep(1)` at `ui_chars.cpp:782` and `ui_geometry.cpp:243`.
    - `#if JUCE_MAC` at `ui_edits.cpp:132,190`.
    - `ui_dump.cpp:36,165-172`: `plugin/factory/FactoryBank.h`, `funkgui::presets::Preset`, `.toStdString()`. This is JUCE through FunkGui::presets and a plain `juce` grep misses it.
31. `funkgui::HeadlessGuiScope` (`HeadlessGuiScope.h`, `src/juce/HeadlessGuiScope.cpp`) holds exactly that initialiser with JUCE and nothing without. It replaces the include and the declaration one for one.
32. No probe exercises a menu, chooser or copy today. The rows that assert "headless does nothing":
    - `popup.headless_nothing` (`ui_edits.cpp:249-250`).
    - `save.strip_a11y` (`ui_presets.cpp:775-779`).
    - `export.headless_no_chooser` (`:1080-1085`).
    - `copy.headless_copies_nothing` and `copy.title_kept` (`ui_settings.cpp:441-446`).
33. `ui.edits`, `ui.presets` and `ui.settings` are spec-only (22, 114 and 63 `P.*` rows). `tests/golden/base/global` has no file for them. The FZ5 goldens are `modes/*/ui.{geometry,a11y,input}*`.
34. `ui.input`'s `handledKeys` presses Return and Space on every Tab stop, including Import, Export and Copy report (`ui_input.cpp:815-835`). The taborder lines are recorded before the presses.

### I. Compiling without JUCE
35. I ran `clang++ -fsyntax-only -DFUNKGUI_HAS_JUCE=0` with no JUCE include path over the 28 non-gpu editor `.cpp` files (serial, niced, no output files). 24 pass today. The 4 failures are exactly the four views, each at its JUCE or MenuLook include.
36. With `FUNKGUI_HAS_JUCE=1` (every native build), `UiPreferences.h:5-7` includes `juce_data_structures`, so `Panel.cpp` and `AnimationModel.cpp` still need JUCE's include path.

## 2. What the card must do (in this order)

1. **`Panel.cpp` HostProxy**: add six forwards: `services()`, `showMenu(req, std::move(cb))`, `dismissMenus()`, `chooseFiles(req, std::move(cb))`, `copyText(sv)`, `commandKeyIsMeta()`. Keep `ownerComponent()` until step 6. No behaviour changes yet.
2. **EditControls** (header revision, see Q1):
   - `commandOnly(const Mods&, bool meta)` and `commandKeyName(bool meta)`, with the same two rules selected by `meta`.
   - `tick()` passes `ctx_.host != nullptr ? ctx_.host->commandKeyIsMeta() : <platform default>`; `Panel.cpp:701` passes `ctx_.host->commandKeyIsMeta()`.
   - `showMenu()`: return if the host is null; build the one `funkgui::MenuItem`; call `ctx_.host->showMenu(req, [this, alive](int id){...})` with the same body.
   - The destructor keeps `alive_.reset()` only.
   - Drop the two includes, `class MenuLook;` and `menuLook_`.
   - `ui_edits.cpp`: `#if JUCE_MAC` becomes `r.host.commandKeyIsMeta()` at runtime.
3. **PresetStrip**: `showMenu()` maps `menu()`'s items to `funkgui::MenuItem{int(command), label, enabled}`. Same callback. The destructor keeps `alive_.reset()` only. Drop the includes, the forward declaration and `menuLook_`.
4. **PresetBrowser**:
   - `showMenu`: `separatorBefore` becomes a `{.separator = true}` item before the entry.
   - `showCategoryMenu`: `checked` carries the ticks.
   - `chooseImport` / `chooseExport` as in facts 13-15. The export callback keeps the "GONE" flash and the nudge.
   - `actionEnabled`: `importFiles` → `hasChooser()`; `exportFile` → `sel != nullptr && hasChooser()`.
   - `menu()`: Export.../Import... enabled = `hasChooser()` (see Q4).
   - `hasChooser()` is `ctx_.host != nullptr && (ctx_.host->services() & funkgui::hostservice::fileChooser) != 0`.
   - The clip: `const float top = c.snapY(kList.y), bottom = c.snapY(kList.bottom()); c.pushClip({kGround.x, top, kGround.w, bottom - top});`.
   - The destructor keeps `alive_.reset()` only. Drop the includes, both forward-declaration blocks, `menuLook_` and `chooser_`.
5. **Settings** and `ui_settings.cpp` in one commit: `copyReport()` per fact 16 (null host → false; `refresh()`; `if (!ctx_.host->copyText(report())) return false;`). Drop the include. Replace the two `copy.*` rows (see Traps 1).
6. **`Panel.cpp`**: delete the `ownerComponent()` override (`:174`) and fix the comment at `:159-163`.
7. **Probes**: in the 23 files of fact 29, replace the include with `<funkgui/panel/HeadlessGuiScope.h>` and the declaration with `const funkgui::HeadlessGuiScope gui;`. The two sleeps become `std::this_thread::sleep_for(std::chrono::milliseconds(1))`. `ui_dump.cpp`'s `applyPreset` moves to `plugin/portable/FactoryData` rows (see Q5).
8. New spec rows (section 4) and header comments (the "live editor only / headless nothing" prose in the four headers).
9. **Lead**: the `editor.juce` lint rule (below) and the header revision approval.

**Proposed `editor.juce`** (in LintDeps' `_in_editor AND NOT _in_editor_gpu` branch, on comment-stripped code):
- no include matching `(^|/)(juce_[^/]*|JuceHeader\.h)(/|$)`;
- no include matching `(^|/)funkgui/(juce|presets)/`;
- no token `juce::`, `namespace juce`, `JUCE_[A-Z0-9_]+`, `MenuLook` or `ownerComponent`.

It must forbid direct uses only, because of fact 36. It is not a "no JUCE include path" guarantee natively; that proof arrives with Sprint D's web target, where fact 35 says all 28 files will pass. `d.juce` passes the token rules. Present violations are exactly the lines in facts 1-3.

## 3. Traps

1. **Two existing rows cannot hold.** After E-2, `own.copyReport()` reaches HeadlessHost through the proxy and returns true, and the a11y title becomes "Copied". So `copy.headless_copies_nothing` and `copy.title_kept` (`ui_settings.cpp:441-446`) fail as spec (exit 1). The manifest must approve replacing them.
2. **Forward `services()` first.** Without it the Panel's views see 0, IMPORT and EXPORT disable, and FZ5 goldens move: `ui.a11y.presetbrowser.lines:65-66`, `ui.input.taborder.presetbrowser.lines:15-16`, and the browser's `ui.geometry` hash (ink52 → ink16).
3. **`#if JUCE_MAC` fails silently.** Remove the include and it evaluates to 0 with no warning (`-Wundef` is not in the list), so macOS would print CTRL and accept Ctrl-Cmd-Z. On macOS only `click.footer` and `keys.ctrl_cmd_z` catch it. This is why the lint needs the `JUCE_` token rule.
4. **Never call a service from a view destructor** (`HostServices.h:155-157`). The host drops callbacks itself (`EditorHost.cpp:218,366`; `HeadlessHost.cpp:197-204`). Do not turn `dismissAllActiveMenus()` into `ctx_.host->dismissMenus()`.
5. **Keep `alive_`.** `ui_presets.cpp:812,1052` and `ui_settings.cpp:432` build views of their own that die before the HeadlessHost, which still holds their pending callbacks.
6. **Name clash.** `PresetStrip::MenuItem` and `PresetBrowser::MenuItem` are public nested types that the probes use. Inside members, write `funkgui::MenuItem` and `funkgui::MenuRequest`.
7. **`MenuRequest::theme` defaults to graphite.** Set `productTheme(themeIndex())`, or PAPER and the other product themes get wrong menu colours. No existing row catches it.
8. **The anchor is Panel px.** Keeping the `* s` zoom factor would double-scale at 125–175 %.
9. **`MenuItem.id` must be > 0** or the whole menu is refused (`HostServicesJuce.cpp:50-63`). All current ids qualify (1–6, 1 and 100+).
10. **Save extension.** The host replaces whatever follows the last '.', so `returnFiles({"/tmp/Mix v1.2"})` gives `/tmp/Mix v1.fcmppreset`. That is the same as today's `withFileExtension`, but it surprises when writing rows.
11. **Web key mods.** `commandOnly(meta=false)` needs `mods.cmd` set. G-D's WebHost must set `cmd` together with `ctrl` off Apple platforms (JUCE's rule, `HostServices.h:194-196`), or Ctrl-Z is dead in the browser.
12. **Web dpi and the "at rest" rule.** At a non-dyadic dpi, `snapY(20k) != 20k`, so `cut` is true at rest and rows sit up to half a device px off. Harmless, but the comment at `:1617-1619` no longer holds there.
13. **Linux fractional desktop scales** (for example 1.1) move the snapped clip edges by up to half a device px natively. No golden covers them.
14. **`ui_dump.cpp`** stays JUCE-bound through FactoryBank even after the include swap. A "probes are JUCE-free" claim or lint would be false unless Q5 is done.
15. **Header revisions.** `Scripts/check-headers.sh:80-83` checks every `views/*.h` standalone, and the classes are FZ4. Private-member and forward-declaration removals need the manifest's "approved here". The `commandOnly`/`commandKeyName` signature change is a public declaration change.

## 4. How to verify

**Existing rows that must keep their values:**
- `ui.edits`: all 22, in particular `click.footer`, `click.footer_redo`, `keys.cmd_z`, `keys.shift_cmd_z`, `keys.ctrl_cmd_z`, `popup.headless_nothing` (still 1: the menu is pending, unanswered).
- `ui.presets`: all 114, in particular `save.strip_a11y`, `save.strip_menu_items`, `save.strip_menu_run`, `export.one_call`, `export.refused`, `export.headless_no_chooser` (still 1: the chooser is pending), `menu.factory`, `menu.user`, `menu.background`, `menu.run`, `saveas.*`, `scroll.rows_clipped_to_list`, `scroll.rows_cut_at_edge`.
- `ui.settings`: 61 of 63 (all but the two `copy.*`).
- Every `modes/*/ui.{geometry,a11y,input}` golden, and `input.keys.handled` = 0.

**New rows (HeadlessHost scripting):**
- `ui.edits`
  - `menu.copy_request`: ctrl-click A gives one item (2, "Copy A to B"), anchor `{562,43,38,16}`, theme = productTheme(0). Proves traps 7 and 8.
  - `menu.copy_a_to_b`: `chooseMenuItem` → `slotUsed(1)`, slot still 0, B holds A's value.
  - `menu.copy_b_to_a`: a11y showMenu on B gives id 1.
  - `menu.cancel`: `cancelMenu()` leaves the revision unchanged.
  - `keys.meta_on.*` / `keys.meta_off.*`: `setCommandKeyIsMeta(true/false)` gives both footer names and both Ctrl-Cmd-Z outcomes on one platform. Proves trap 3 everywhere.
- `ui.presets`
  - `menu.strip_request` (items, anchor `kSaveBox`) and `menu.strip_save_as` (browser opens editing).
  - `menu.row_request` (items and separators equal `menu()`, anchor = the row's bounds, row selected), `menu.row_delete` (one remove), `menu.row_gone` (list changed while pending → nothing), `menu.background_request` (1×1 anchor).
  - `saveas.category_menu` (ticks, separator, order), `saveas.category_chosen` (value "Drums", then Return saves with it), `saveas.category_stale` (edit cancelled while pending → nothing).
  - `export.chooser_request` (save, title, pattern, `suggestedName`), `export.chooser_result` (`returnFiles({"/tmp/x"})` → `exportFile(.., "/tmp/x.fcmppreset")`), `export.chooser_cancel`.
  - `import.chooser_request` (openMany), `import.chooser_result` (two paths → two `importFile` calls).
  - `import.no_chooser`: a host without the chooser bit → IMPORT and EXPORT disabled, absent from the Tab order, press calls nothing, menu items off.
- `ui.settings`
  - `copy.report_copied` (`log.copies == 1`, `lastCopy == report()`), `copy.title_copied`, `copy.title_back` (after `kCopiedS`).
  - `copy.no_clipboard` (a host without the clipboard bit → false, title kept): the old two rows' meaning.

**Gates:** `[AGENT]`, `[GPU]`, `lint.deps` with `editor.juce`, `lint.headers`, and `gui-live.sh` 5/5 (lead).

**Cannot be automated:** the real popup's position and colours at each zoom and theme, the native chooser (Documents, suggested name, overwrite warning), and the real clipboard. The lead checks these by hand in the Standalone, as the plan says.

## 5. Open questions for the lead

1. **`commandOnly` / `commandKeyName` signatures.**
   - (a) Replace with `(…, bool commandKeyIsMeta)`: two call sites, a public change in a `views/*.h` header.
   - (b) Keep the old statics and add overloads.
   - (c) Keep them with `#if __APPLE__`: wrong in a browser on a Mac.
   - Recommendation: (a), approved in the manifest.
2. **The two `copy.*` rows:** rename and replace as in section 4 (recommended), or keep the keys with a new meaning.
3. **A host without services for the `no_chooser` / `no_clipboard` rows.**
   - (a) A small `HostServices` wrapper in the probe that forwards to HeadlessHost and masks `services()`. No FunkGui change.
   - (b) `HeadlessHost::setServices(mask)` in v0.13.0.
   - Recommendation: (a) now.
4. **Menu items "Export..." / "Import..." without a chooser:** disabled (recommended; `menu.*` rows are unchanged under HeadlessHost) or omitted. Should COPY REPORT, Copy A to B and the category word also gate on `services()`? Recommendation: no. All three hosts serve menus and the clipboard, and gating adds a11y states that no golden covers.
5. **`ui_dump.cpp`:** move `applyPreset` to FactoryData in E-2 (recommended; it is in the plan's `ui_*.cpp` scope and Sprint D links "every ui_*"), or exclude it from the web target.
6. **A lint for the probes** (`Tools/probes/plugin/ui_*.cpp`: no JUCE include, no `juce::`/`JUCE_`): add it with `editor.juce` once Q5 is settled, or leave it to Sprint D's build to enforce.
7. **The clip snap** is not in plan.md's E-2 list (only in ADR-93, `DECISIONS.md:1117`). Confirm it belongs to E-2 (`PresetBrowser.cpp` is already owned).

## Not checked
- No probe or build was run; the row outcomes in section 4 are from reading the code.
- How JUCE decodes a non-ASCII `std::string` category name today versus the host's `fromUTF8`.
- Whether `verify.sh` or `golden.py` tracks removed spec keys.
- `gui-live.sh` internals.
- Linux behaviour at fractional scales.
- G-D's actual key conversion.
- The probe bodies were grepped for service-reaching input, not all read line by line.