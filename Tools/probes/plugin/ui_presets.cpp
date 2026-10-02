// FCMP_PROBE layer=ui name=presets scope=global timeout=300
//
// ui.presets (02 §6.2–§6.3, §8.9; S12 lead revisions 5, 8 and 11; U6, P3c): the preset strip and the preset browser
// over a FakeFacade's PresetAccess (15 rows: 12 factory, 3 user; current = row 1), driven through the Panel API and
// HeadlessHost input (Panel{skipHint, syncPreview}, dpi 2, settled at 1/60 s; a fresh panel per scenario). Global and
// spec-only: the views read no Mode-specific data beyond the Mode names of the rows.
//
//   strip.*     the four a11y items (‹, the "Preset" comboBox with the current name, ›, SAVE) and their Tab stops right
//               after the header's (the Mode latch, then v1.2's settings gear); ‹ and › are one
//               PresetAccess::step(∓1) each (one apply, one batch); keys and a11y on the focused stops; nothing to step
//               without presets; redraw on revision(): idle frames re-read nothing, a revision (modified, a new list)
//               is shown on the next frame (the marker, ", modified", UNTITLED).
//   browser.*   a click on the name opens it without applying; the rows shown (11 of 15, in PresetAccess order, the
//               current one checked); a click on a row is exactly one apply of that index and keeps the browser open; a
//               click on the current, unmodified row applies nothing (modified: one apply); a double-click applies once
//               and closes; ↓ Home End apply as they move, Return closes without a second apply; filters (click, ← →,
//               a11y); the wheel scrolls the list (a notch glides 3 rows); type-ahead selects without applying; Esc
//               closes.
//   scroll.*    (ADR-84) a trackpad scrolls the list by the pixel with the content whatever `reversed` says (natural
//               scrolling), 1:1 at the UI zoom, clamped; a row the list's edge cuts is listed by the part that shows
//               and its primitives are clipped to the list (y 88–308); a notch glides at full rate and lands on its row;
//               nothing is applied.
//   scroll.clip_*  (web Sprint C, ADR-93) at a dpi where whole logical px are not device px (1.5625) the list's
//               clip is on device px: the rows are cut at device rows 138 and 481 (88 and 308 are 137.5 and 481.25).
//   saveas.*    the strip's SAVE opens the browser with the name pre-filled and selected (the store's unique name
//               announced when it is taken); typing replaces it; Return is one saveAs(name, category) and the browser
//               closes (opened for the save), the strip showing the new preset; Esc cancels and keeps the browser open;
//               SAVE AS inside the browser keeps it open on the new row; an empty name is refused before the call; the
//               category follows a chosen category filter; Init starts empty; a click elsewhere cancels.
//   save.*      (P3c, S12.5) SAVE with a modified user preset current ("My Bus", applied, then edited): the strip's
//               SAVE is one overwrite(current), no dialog, and clears MODIFIED; SAVED shows in the sub-line for 2 s,
//               then goes; with a factory preset or none it is the save as, never an overwrite; a refused overwrite falls
//               back to the save as; Return on the focused SAVE saves over, Shift-Return saves as; a11y help and the
//               footer line name the preset saved over; SAVE's popup click and showMenu, left unanswered, do nothing;
//               the strip's menu is Save, Save As... and runs each; the browser's SAVE saves over the current preset
//               whatever row is selected and stays open ("SAVED 'MY BUS'"), is SAVE AS with a factory preset, and a
//               refused overwrite says so and starts a save as.
//   rename.*    RENAME on a user row edits its name in place; Return is one rename(index, name); factory rows cannot;
//               a taken name (case-insensitive) is refused before the call; a refused call keeps the edit; a click
//               elsewhere confirms.
//   delete.*    DELETE (or the Delete key, or Cmd-Backspace) arms, the second press is one remove(index) and the
//               selection moves on; a double-click only arms; factory rows cannot; the arm lapses after 3 s and Esc
//               takes it back.
//   import.*    Panel::filesInterest accepts .fcmppreset paths only (any case); Panel::filesDropped opens the browser and
//               makes one importFile per preset file (others ignored); one file is also loaded (one apply); two are
//               not; a refused import says so.
//   export.*, menu.*  a PresetBrowser of the probe's own over the same facade: exportTo is one exportFile(index,
//               path); the context menu's items per row kind and background; run() of Load, Rename, Delete; IMPORT /
//               EXPORT ask the host for a chooser and, left unanswered, call nothing.
//   The host's services (web Sprint C, ADR-93): HeadlessHost shows nothing and keeps a menu or a chooser pending until
//   the probe answers it; the requests are read from its log.
//   menu.strip_*    SAVE's menu in PAPER at a 150 % UI zoom: (1, Save), (2, Save As...), anchored on SAVE's box in the
//               Panel's own px, in the product's palette; Save As... opens the browser's edit; Save is one overwrite; a
//               dismissed menu does nothing.
//   menu.row_*, menu.background_*   a popup click on a row selects it and asks for the items of menu() with its
//               separators, anchored on the row; Load is one apply; Delete on a user row is one remove; a row that
//               went while the menu was open gets nothing, one that moved is found again by its uuid; a dismissed menu
//               does nothing; a popup click on the background asks for Save As... and Import... at a 1 x 1 anchor
//               under the pointer, and Import... asks for the chooser.
//   saveas.category_*   the save-as category word asks for No Category, a separator and the categories in filter
//               order, the edit's one ticked; choosing one sets it and the save takes it; an answer that comes after
//               the edit was cancelled does nothing.
//   export.chooser_*, import.chooser_*   EXPORT asks for a save chooser ("Export preset", *.fcmppreset, the preset's
//               name with the extension as the suggested file name: the host makes it legal); the path the host
//               returns (with the extension it adds) is one exportFile; a cancel, or a preset that went meanwhile,
//               exports nothing. IMPORT asks for an openMany chooser; two paths are two importFile calls; a cancel
//               imports nothing.
//   services.view_gone   a PresetStrip and a PresetBrowser of the probe's own that go while their menu and chooser are
//               open: the answers reach nobody, and nothing was dismissed from a destructor.
//   import.no_chooser*   a host that reports no file chooser: IMPORT and EXPORT are disabled in a11y, out of the Tab
//               order and show no hand; pressing them asks no host for anything; the row menu still opens, with
//               Export... and Import... off. The footer line under the hand says each is not available here, EXPORT's
//               with or without a selection, and fits the footer's line; with the chooser (import.chooser_hand) the
//               lines are what they were.
//   a11y.*      ids non-zero and unique with the browser open and editing; press / focus on a row; the filter radios.
//   draw.*      every BROWSER_* primitive inside the browser's ground, the strip's inside its rectangle, no missing
//               glyph in any state drawn here.
//   writes.none the views never write a parameter: every recall is PresetAccess's.
//   focus.*     (S13 H1a) opened from the keyboard (the strip's name, Return) the focus comes onto the selected row with
//               one ring in the frame; while open, Tab walks the browser only (the filters, the rows shown, the available
//               action cells, left to right), wrapping, one ring on each stop, applying nothing; Tab onto a row selects
//               it (Return then loads it and closes); ↓ on a focused row applies and the focus follows; on the filters
//               the arrows choose the filter; Return on a focused SAVE AS starts the edit with the focus on its commit
//               cell, Esc cancels it (nothing saved or applied) and the focus returns to the selection (the last row
//               Tab passed); Esc closes and the focus goes back to the
//               strip's name; with the browser open, no item of another view centred under its ground is visible in
//               a11y, and the 21 slots are.
//
// Review pictures (not a test): `fcmp_probe_plugin ui.presets … -- --png-dir <dir>` writes presets-<state>.png (dpi 2,
// theme 0; one in theme 1).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/ProductTheme.h"
#include "editor/SubView.h"
#include "editor/Tags.h"
#include "editor/views/PresetBrowser.h"
#include "editor/views/PresetStrip.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HeadlessGuiScope.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/panel/Input.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    using fcmp::probe::FakeFacade;
    using fcmp::probe::FakePresets;
    using Row = fcmp::PresetAccess::Row;
    using Call = FakePresets::Call;
    using PB = ui::PresetBrowser;
    using PS = ui::PresetStrip;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    constexpr funkgui::Rect kGround { 32.0f, 60.0f, 896.0f, 294.0f };  // PresetBrowser.cpp: kOverlay grown 8 / 4 px

    int b(bool v) { return v ? 1 : 0; }

    // The sample list: the factory bank's shape (Init, then by Mode) and three user presets, sorted by name.
    std::vector<Row> sampleRows()
    {
        const auto row = [](const char* uuid, const char* name, const char* category, const char* mode, bool factory) {
            Row r;
            r.uuid = uuid;
            r.name = name;
            r.category = category;
            r.modeKey = mode;
            r.factory = factory;
            return r;
        };
        return {
            row("f-00", "Init", "Init", "clean", true),
            row("f-01", "Gentle Glue", "Bus", "clean", true),
            row("f-02", "Vocal Leveler", "Vocal", "clean", true),
            row("f-03", "Drum Punch", "Drums", "clean", true),
            row("f-04", "Master -1 dBTP", "Master", "brickwall", true),
            row("f-05", "Mix Bus Glue", "Bus", "bus-g", true),
            row("f-06", "Drum Bus Snap", "Drums", "bus-g", true),
            row("f-07", "Mix Bus 2:1", "Bus", "bus-25", true),
            row("f-08", "Smooth Vocal", "Vocal", "opto-2a", true),
            row("f-09", "Mix Bus Thickener", "Bus", "diode-609", true),
            row("f-10", "Mastering Glue", "Master", "mu-67", true),
            row("f-11", "Vocal Grab", "Vocal", "fet-76", true),
            row("u-00", "Kick Room", "Drums", "fet-76", false),
            row("u-01", "My Bus", "Bus", "bus-g", false),
            row("u-02", "Vox Chain", "Vocal", "opto-2a", false),
        };
    }
    constexpr int kUserFirst = 12;

    struct Rig
    {
        explicit Rig(int current = 1, std::vector<Row> rows = sampleRows(), int theme = 0, float dpi = 2.0f)
            : facade("clean")
        {
            facade.fakePresets().setRows(std::move(rows));
            facade.fakePresets().setCurrent(current);
            panel = std::make_unique<ui::Panel>(facade, kProbeOptions);
            host = std::make_unique<funkgui::HeadlessHost>(*panel, theme, dpi);
            settled = host->settle(kMaxSettle, kDt);
            presets().resetCounts();
            facade.resetCounts();
        }

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        FakePresets& presets() { return facade.fakePresets(); }
        const ui::PanelContext& ctx() const { return panel->context(); }
        int  settle() { return host->settle(kMaxSettle, kDt); }
        std::vector<funkgui::A11yItem> items() const { return host->accessibility(); }
        bool open() const { return panel->overlay() == ui::Overlay::presetBrowser; }

        void click(const funkgui::Rect& r, funkgui::Mods m = {})
        {
            host->click(r.centreX(), r.centreY(), m);
            host->tick(1, kDt);
        }
        void keys(const char* spec)
        {
            host->keys(spec);
            host->tick(1, kDt);
        }
        void a11y(uint32_t id, funkgui::A11yAction a)
        {
            panel->a11yAction(id, a, 0.0);
            host->tick(1, kDt);
        }

        FakeFacade                             facade;
        std::unique_ptr<ui::Panel>             panel;       // destroyed after the host (its destructor closes gestures)
        std::unique_ptr<funkgui::HeadlessHost> host;
        int                                    settled = 0;
    };

    uint32_t stripId(uint32_t local) { return ui::a11yId(ui::ViewIndex::presetStrip, local); }
    uint32_t browserId(uint32_t local) { return ui::a11yId(ui::ViewIndex::presetBrowser, local); }
    uint32_t rowId(int index) { return browserId(PB::kRowLocal0 + static_cast<uint32_t>(index)); }

    const funkgui::A11yItem* byId(const std::vector<funkgui::A11yItem>& v, uint32_t id)
    {
        for (const funkgui::A11yItem& it : v)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    // The browser's visible rows, top to bottom.
    std::vector<funkgui::A11yItem> rows(const Rig& r)
    {
        std::vector<funkgui::A11yItem> v;
        for (const funkgui::A11yItem& it : r.items())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::presetBrowser)
                && it.role == funkgui::A11yRole::listItem && it.visible)
                v.push_back(it);
        std::stable_sort(v.begin(), v.end(),
                         [](const funkgui::A11yItem& a, const funkgui::A11yItem& c) { return a.bounds.y < c.bounds.y; });
        return v;
    }

    const funkgui::A11yItem* rowNamed(const std::vector<funkgui::A11yItem>& v, std::string_view name)
    {
        for (const funkgui::A11yItem& it : v)
            if (it.title == name)
                return &it;
        return nullptr;
    }

    std::string value(const Rig& r, uint32_t id)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = byId(v, id);
        return it != nullptr ? it->value : std::string("<none>");
    }

    bool enabled(const Rig& r, uint32_t id)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = byId(v, id);
        return it != nullptr && it->enabled && it->visible;
    }

    funkgui::Rect bounds(const Rig& r, uint32_t id)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = byId(v, id);
        return it != nullptr ? it->bounds : funkgui::Rect{ -100.0f, -100.0f, 1.0f, 1.0f };
    }

    std::string status(const Rig& r) { return value(r, browserId(PB::kStatusLocal)); }
    std::string stripValue(const Rig& r) { return value(r, stripId(PS::kNameLocal)); }
    bool editing(const Rig& r) { return byId(r.items(), browserId(PB::kEditLocal)) != nullptr; }

    // Opens the browser the way a user does: a click on the strip's name.
    void openBrowser(Rig& r)
    {
        r.click(bounds(r, stripId(PS::kNameLocal)));
        r.settle();
    }

    // Selects a row without loading it (a11y focus: VoiceOver's cursor).
    void selectRow(Rig& r, int index) { r.a11y(rowId(index), funkgui::A11yAction::focus); }

    int stripPrims(const funkgui::PrimList& pl)
    {
        int n = 0;
        for (const funkgui::Prim& p : pl.prims)
            n += p.tag == ui::tag::presetStrip ? 1 : 0;
        return n;
    }

    // Every BROWSER_* primitive inside the ground; every PRESET_STRIP one inside the strip's columns (x 228–600) and
    // the header band (y 0–60): glyph quads carry the SDF padding below the sub-line's text (± 1.5 px of AA apron).
    bool insideRegions(const funkgui::PrimList& pl)
    {
        const auto inside = [](const funkgui::Prim& p, const funkgui::Rect& r) {
            constexpr float kApron = 1.5f;
            return p.x0 >= r.x - kApron && p.y0 >= r.y - kApron && p.x1 <= r.right() + kApron
                && p.y1 <= r.bottom() + kApron;
        };
        const funkgui::Rect strip { L::kPresetStrip.x, 0.0f, L::kPresetStrip.w, L::kHeader.bottom() };
        for (const funkgui::Prim& p : pl.prims)
        {
            const bool browser = p.tag >= ui::tag::browserBg && p.tag <= ui::tag::browserPager;
            if (browser && !inside(p, kGround))
                return false;
            if (p.tag == ui::tag::presetStrip && !inside(p, strip))
                return false;
        }
        return true;
    }

    // ---- strip ----------------------------------------------------------------------------------------------------------

    void strip(Probe& P)
    {
        {
            Rig r;
            P.le("strip.settle", r.settled, kMaxSettle);
            const std::vector<funkgui::A11yItem> v = r.items();
            const funkgui::A11yItem* prev = byId(v, stripId(PS::kPrevLocal));
            const funkgui::A11yItem* name = byId(v, stripId(PS::kNameLocal));
            const funkgui::A11yItem* next = byId(v, stripId(PS::kNextLocal));
            const funkgui::A11yItem* save = byId(v, stripId(PS::kSaveLocal));
            P.eq("strip.a11y.items", b(prev != nullptr && prev->role == funkgui::A11yRole::button
                                       && prev->title == "Previous preset" && prev->enabled
                                       && name != nullptr && name->role == funkgui::A11yRole::comboBox
                                       && name->title == "Preset" && name->value == "Gentle Glue"
                                       && name->description == "Bus, factory"
                                       && next != nullptr && next->role == funkgui::A11yRole::button
                                       && next->title == "Next preset" && next->enabled
                                       && save != nullptr && save->role == funkgui::A11yRole::button
                                       && save->title == "Save preset"), 1);
            bool inside = true;
            for (const funkgui::A11yItem* it : { prev, name, next, save })
                inside = inside && it != nullptr && it->bounds.x >= L::kPresetStrip.x
                      && it->bounds.right() <= L::kPresetStrip.right() && it->bounds.y >= L::kPresetStrip.y
                      && it->bounds.bottom() <= L::kPresetStrip.bottom();
            P.eq("strip.a11y.inside", b(inside), 1);

            // Tab: the Mode latch and (v1.2, ADR-85) the settings gear, then ‹ name › SAVE (02 §8.9 items 1–2).
            std::vector<uint32_t> stops;
            for (int i = 0; i < 6; ++i)
            {
                r.host->keys("tab");
                stops.push_back(r.ctx().focus);
            }
            P.eq("strip.taborder", b(stops.size() == 6
                                     && ui::viewIndexOf(stops[0]) == static_cast<int>(ui::ViewIndex::header)
                                     && ui::viewIndexOf(stops[1]) == static_cast<int>(ui::ViewIndex::header)
                                     && stops[2] == stripId(PS::kPrevLocal) && stops[3] == stripId(PS::kNameLocal)
                                     && stops[4] == stripId(PS::kNextLocal) && stops[5] == stripId(PS::kSaveLocal)), 1);
        }
        {
            Rig r;
            r.click(bounds(r, stripId(PS::kPrevLocal)));
            const bool prev = r.presets().steps() == 1 && r.presets().applies() == 1 && r.presets().current() == 0
                           && r.facade.batches() == 1 && r.facade.batchDepth() == 0;
            r.click(bounds(r, stripId(PS::kNextLocal)));
            r.click(bounds(r, stripId(PS::kNextLocal)));
            const bool next = r.presets().steps() == 3 && r.presets().applies() == 3 && r.presets().current() == 2
                           && r.facade.batches() == 3;
            P.eq("strip.prev_is_one_step", b(prev), 1);
            P.eq("strip.next_is_one_step", b(next), 1);
            P.eq("strip.shows_current", b(stripValue(r) == "Vocal Leveler"), 1);
            P.eq("strip.stays_closed", b(!r.open()), 1);
        }
        {
            Rig r;
            r.keys("tab,tab,tab");                               // ‹ (after the Mode latch and the gear)
            r.keys("return");
            const bool prevKey = r.presets().steps() == 1 && r.presets().current() == 0;
            r.keys("tab");                                       // the name
            r.keys("right");
            r.keys("up");
            const bool arrows = r.presets().steps() == 3 && r.presets().current() == 2;
            r.keys("left");
            const bool back = r.presets().steps() == 4 && r.presets().current() == 1;
            r.keys("return");
            r.settle();
            P.eq("strip.keys", b(prevKey && arrows && back && r.open()), 1);
        }
        {
            Rig r;
            r.a11y(stripId(PS::kPrevLocal), funkgui::A11yAction::press);
            const bool press = r.presets().steps() == 1 && r.presets().current() == 0;
            r.a11y(stripId(PS::kNameLocal), funkgui::A11yAction::increment);
            const bool inc = r.presets().steps() == 2 && r.presets().current() == 1;
            r.a11y(stripId(PS::kNameLocal), funkgui::A11yAction::press);
            r.settle();
            P.eq("strip.a11y.actions", b(press && inc && r.open() && r.presets().steps() == 2), 1);
        }
        {
            Rig r(-1, {});
            r.click(bounds(r, stripId(PS::kPrevLocal)));
            r.click(bounds(r, stripId(PS::kNextLocal)));
            const std::vector<funkgui::A11yItem> v = r.items();
            const funkgui::A11yItem* prev = byId(v, stripId(PS::kPrevLocal));
            P.eq("strip.empty", b(r.presets().steps() == 0 && prev != nullptr && !prev->enabled
                                  && stripValue(r) == "No presets"), 1);
        }
        {
            // Redraw on revision(): idle frames re-read nothing; a revision is on screen the next frame.
            Rig r;
            const int before = stripPrims(r.host->draw());
            r.presets().resetCounts();
            r.host->tick(30, kDt);
            P.eq("strip.idle_reads", r.presets().reads(), 0);
            r.presets().setModified(true);
            r.host->tick(1, kDt);
            const int reads = r.presets().reads();
            const int after = stripPrims(r.host->draw());
            P.eq("strip.revision_modified", b(reads > 0 && stripValue(r) == "Gentle Glue, modified" && after >= before + 2),
                 1);
            std::vector<Row> other = sampleRows();
            other.resize(3);
            r.presets().setRows(std::move(other));               // current = -1: untitled
            r.host->tick(1, kDt);
            P.eq("strip.revision_list", b(stripValue(r) == "Untitled"), 1);
            r.presets().setCurrent(2);
            r.host->tick(1, kDt);
            P.eq("strip.revision_current", b(stripValue(r) == "Vocal Leveler"), 1);
        }
    }

    // ---- browser: recall ------------------------------------------------------------------------------------------------

    void browser(Probe& P)
    {
        {
            Rig r;
            openBrowser(r);
            const std::vector<funkgui::A11yItem> v = rows(r);
            const std::vector<Row> all = sampleRows();
            bool order = v.size() == 11;
            int checked = 0;
            for (std::size_t i = 0; order && i < v.size(); ++i)
            {
                order = v[i].title == all[i].name && v[i].id == rowId(static_cast<int>(i));
                checked += v[i].checked ? 1 : 0;
            }
            P.eq("browser.open", b(r.open() && r.presets().applies() == 0 && r.presets().saves() == 0), 1);
            P.eq("browser.rows", b(order), 1);
            P.eq("browser.current_checked", b(checked == 1 && v.size() > 1 && v[1].checked), 1);
            P.eq("browser.status_count", b(status(r) == "15 PRESETS"), 1);

            // One click on a row: one apply of that index; the browser stays open.
            r.click(v[3].bounds);
            const bool one = r.presets().applies() == 1 && r.presets().current() == 3 && r.facade.batches() == 1;
            P.eq("browser.click_applies_once", b(one && r.open()), 1);
            r.click(v[3].bounds);                                // current and unmodified: nothing
            const bool none = r.presets().applies() == 1;
            r.presets().setModified(true);
            r.host->tick(1, kDt);
            r.click(v[3].bounds);                                // modified: back to the preset
            P.eq("browser.click_current", b(none && r.presets().applies() == 2), 1);
            const std::vector<funkgui::A11yItem> after = rows(r);
            P.eq("browser.checked_follows", b(after.size() > 3 && after[3].checked && !after[1].checked), 1);

            // A double-click: one apply, then closed.
            r.host->doubleClick(v[4].bounds.centreX(), v[4].bounds.centreY());
            r.settle();
            P.eq("browser.dblclick", b(r.presets().applies() == 3 && r.presets().current() == 4 && !r.open()), 1);
        }
        {
            // Dragging off a row cancels.
            Rig r;
            openBrowser(r);
            const std::vector<funkgui::A11yItem> v = rows(r);
            r.host->drag(v[5].bounds.centreX(), v[5].bounds.centreY(), v[5].bounds.centreX(), 330.0f);
            r.host->tick(1, kDt);
            P.eq("browser.drag_off", b(r.presets().applies() == 0 && r.open()), 1);
        }
        {
            // Keys: ↓ Home End apply as they move; Return closes without applying again.
            Rig r;
            openBrowser(r);
            r.keys("down");
            const bool down = r.presets().applies() == 1 && r.presets().current() == 2;
            r.keys("end");
            const std::vector<funkgui::A11yItem> v = rows(r);
            const bool end = r.presets().applies() == 2 && r.presets().current() == 14 && !v.empty()
                          && v.back().title == "Vox Chain" && v.back().checked;
            r.keys("end");                                       // at the end: nothing
            const bool clamp = r.presets().applies() == 2;
            r.keys("home");
            const bool home = r.presets().applies() == 3 && r.presets().current() == 0 && rows(r).front().title == "Init";
            r.keys("return");
            r.settle();
            P.eq("browser.keys", b(down && end && clamp && home), 1);
            P.eq("browser.return_closes", b(!r.open() && r.presets().applies() == 3), 1);
        }
        {
            // Filters: click, ← →, a11y; the list follows.
            Rig r;
            openBrowser(r);
            const auto names = [&] {
                std::vector<std::string> n;
                for (const funkgui::A11yItem& it : rows(r))
                    n.push_back(it.title);
                return n;
            };
            const auto filterId = [&](std::string_view title) {
                for (const funkgui::A11yItem& it : r.items())
                    if (it.role == funkgui::A11yRole::radioButton && it.title == title
                        && ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::presetBrowser))
                        return it.id;
                return 0u;
            };
            r.click(bounds(r, filterId("User")));
            const std::vector<std::string> user = names();
            const bool userOk = user == std::vector<std::string>{ "Kick Room", "My Bus", "Vox Chain" }
                             && status(r) == "15 PRESETS \xC2\xB7 3 SHOWN";
            r.click(bounds(r, filterId("Drums")));
            const std::vector<std::string> drums = names();
            const bool drumsOk = drums == std::vector<std::string>{ "Drum Punch", "Drum Bus Snap", "Kick Room" };
            r.keys("left");                                      // the category before DRUMS: BUS
            const bool bus = names().size() == 5 && names().front() == "Gentle Glue";
            r.a11y(filterId("Factory"), funkgui::A11yAction::press);
            const bool factory = names().size() == 11 && status(r) == "15 PRESETS \xC2\xB7 12 SHOWN";
            P.eq("browser.filters", b(userOk && drumsOk && bus && factory), 1);
            P.eq("browser.filters_apply_nothing", r.presets().applies(), 0);
            const std::vector<funkgui::A11yItem> v = r.items();
            int radios = 0;
            for (const funkgui::A11yItem& it : v)
                radios += it.role == funkgui::A11yRole::radioButton
                              && ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::presetBrowser)
                              && it.visible ? 1 : 0;
            P.eq("browser.filter_items", radios, 3 + 5);          // ALL FACTORY USER, BUS DRUMS INIT MASTER VOCAL
        }
        {
            // The wheel scrolls the list (3 rows a notch, glided, clamped); type-ahead selects without applying; Esc
            // closes.
            Rig r;
            openBrowser(r);
            r.host->wheel(500.0f, 200.0f, -1.0f);
            r.settle();
            const bool down3 = rows(r).front().title == "Drum Punch";
            r.host->wheel(500.0f, 200.0f, -1.0f);
            r.settle();
            const bool clamp = rows(r).front().title == "Master -1 dBTP" && rows(r).back().title == "Vox Chain";
            r.host->wheel(500.0f, 200.0f, 1.0f);
            r.host->wheel(500.0f, 200.0f, 1.0f);
            r.settle();
            const bool up = rows(r).front().title == "Init";
            P.eq("browser.wheel", b(down3 && clamp && up && r.presets().applies() == 0), 1);
            r.keys("s");
            r.keys("return");
            r.settle();
            P.eq("browser.typeahead", b(r.presets().applies() == 1 && r.presets().current() == 8 && !r.open()), 1);
            openBrowser(r);
            r.keys("escape");
            r.settle();
            P.eq("browser.escape", b(!r.open()), 1);
        }
        {
            // ADR-84: the trackpad moves the list by the pixel, with the content.
            Rig r;
            openBrowser(r);
            const auto swipe = [&r](float points, bool reversed) {
                funkgui::WheelEvent w;
                w.x = 500.0f;
                w.y = 200.0f;
                w.dy = -points / 512.0f;                         // JUCE on macOS: scrollingDeltaY / 512; < 0 = content up
                w.smooth = true;
                w.reversed = reversed;
                const bool used = r.panel->wheel(w);
                r.host->tick(1, kDt);
                return used;
            };
            const auto front = [&r] {
                const std::vector<funkgui::A11yItem> v = rows(r);
                return v.empty() ? funkgui::A11yItem{} : v.front();
            };
            // Natural scrolling (reversed): fingers up by 30 pt move the list up 30 px; row 1 shows its lower 10 px.
            const bool natural = swipe(30.0f, true) && front().title == "Gentle Glue" && front().bounds.y == 88.0f
                                 && front().bounds.h == 10.0f;
            P.eq("scroll.natural_follows_content", b(natural), 1);
            int outside = 0, cutAtEdge = 0;
            for (const funkgui::Prim& p : r.host->draw().prims)
                if (p.tag == ui::tag::browserRow || p.tag == ui::tag::browserCurrent)
                {
                    outside += p.y0 < 88.0f || p.y1 > 308.0f ? 1 : 0;
                    cutAtEdge += p.y0 == 88.0f || p.y1 == 308.0f ? 1 : 0;
                }
            P.eq("scroll.rows_clipped_to_list", outside, 0);
            P.ge("scroll.rows_cut_at_edge", cutAtEdge, 2);
            // The same fingers without natural scrolling: the same way (JUCE's delta already carries the setting).
            P.eq("scroll.classic_same_way", b(swipe(10.0f, false) && front().title == "Vocal Leveler"
                                              && front().bounds.h == 20.0f), 1);
            // At a 150 % UI zoom 30 pt are 20 logical px.
            r.host->setZoom({ 100, 125, 150, 175 }, 150);
            P.eq("scroll.zoom_one_to_one", b(swipe(30.0f, true) && front().title == "Drum Punch"
                                             && front().bounds.h == 20.0f), 1);
            r.host->setZoom({}, 100);
            // Clamped at both ends.
            const bool end = swipe(1000.0f, true) && front().title == "Master -1 dBTP" && rows(r).back().title == "Vox Chain"
                             && rows(r).back().bounds.bottom() == 308.0f;
            const bool top = swipe(-1000.0f, true) && front().title == "Init" && front().bounds.y == 88.0f;
            P.eq("scroll.clamped", b(end && top), 1);
            // A notch glides: at full rate until it lands on its row.
            r.host->wheel(500.0f, 200.0f, -1.0f);
            r.host->tick(1, kDt);
            const bool gliding = r.panel->wantsFullRate() && front().title != "Drum Punch";
            r.settle();
            P.eq("scroll.notch_glides", b(gliding && front().title == "Drum Punch" && front().bounds.h == 20.0f
                                          && !r.panel->wantsFullRate()), 1);
            P.eq("scroll.applies_nothing", r.presets().applies(), 0);
        }
    }

    // ---- save as --------------------------------------------------------------------------------------------------------

    void saveAs(Probe& P)
    {
        {
            // From the strip's SAVE: the browser opens with the name pre-filled; Return saves once and closes.
            Rig r;
            r.click(bounds(r, stripId(PS::kSaveLocal)));
            r.settle();
            const bool opened = r.open() && editing(r) && value(r, browserId(PB::kEditLocal)) == "Gentle Glue"
                             && status(r) == "TAKEN: IT WILL BE SAVED AS 'GENTLE GLUE 2'"
                             && value(r, browserId(PB::kCategoryLocal)) == "Bus";
            P.eq("saveas.strip_opens", b(opened), 1);
            r.keys("M,y,space,G,l,u,e");
            const bool typed = value(r, browserId(PB::kEditLocal)) == "My Glue" && status(r) == "RETURN SAVES   ESC CANCELS"
                            && enabled(r, browserId(PB::kCommitLocal)) && r.presets().saves() == 0;
            P.eq("saveas.typing_replaces", b(typed), 1);
            r.keys("return");
            r.settle();
            const Row saved = r.presets().row(r.presets().count() - 1);
            P.eq("saveas.one_call", r.presets().saves(), 1);
            P.eq("saveas.saved", b(saved.name == "My Glue" && saved.category == "Bus" && !saved.factory
                                   && r.presets().current() == r.presets().count() - 1), 1);
            P.eq("saveas.closes_after_strip_save", b(!r.open() && stripValue(r) == "My Glue"), 1);
            P.eq("saveas.applies_nothing", r.presets().applies(), 0);
        }
        {
            // Esc cancels the name and keeps the browser; a second Esc closes it.
            Rig r;
            r.click(bounds(r, stripId(PS::kSaveLocal)));
            r.keys("x");
            r.keys("escape");
            r.settle();
            const bool cancelled = r.open() && !editing(r) && r.presets().saves() == 0;
            r.keys("escape");
            r.settle();
            P.eq("saveas.escape_cancels", b(cancelled && !r.open()), 1);
        }
        {
            // SAVE AS inside the browser keeps it open on the new row.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.keys("N,e,w");
            r.keys("return");
            r.settle();
            const std::vector<funkgui::A11yItem> v = r.items();
            const funkgui::A11yItem* row = byId(v, rowId(15));
            P.eq("saveas.browser_stays", b(r.open() && r.presets().saves() == 1 && row != nullptr && row->visible
                                           && row->title == "New" && row->checked && status(r) == "SAVED 'NEW'"), 1);
        }
        {
            // An empty name never reaches saveAs; the SAVE cell is unavailable.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.keys("backspace");
            const bool off = !enabled(r, browserId(PB::kCommitLocal)) && value(r, browserId(PB::kEditLocal)).empty();
            r.keys("return");
            P.eq("saveas.empty_refused", b(off && r.presets().saves() == 0 && editing(r)
                                           && status(r) == "TYPE A NAME FIRST"), 1);
        }
        {
            // The category follows a chosen category filter; the save lands in view.
            Rig r;
            openBrowser(r);
            for (const funkgui::A11yItem& it : r.items())
                if (it.role == funkgui::A11yRole::radioButton && it.title == "Drums")
                    r.click(it.bounds);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.keys("K,i,t");
            r.keys("return");
            const Row saved = r.presets().row(r.presets().count() - 1);
            P.eq("saveas.category_from_filter", b(r.presets().saves() == 1 && saved.name == "Kit"
                                                  && saved.category == "Drums"), 1);
        }
        {
            // Init starts empty, with no category.
            Rig r(0);
            r.click(bounds(r, stripId(PS::kSaveLocal)));
            P.eq("saveas.init_empty", b(editing(r) && value(r, browserId(PB::kEditLocal)).empty()
                                        && value(r, browserId(PB::kCategoryLocal)) == "None"), 1);
        }
        {
            // A click elsewhere cancels a save as: nothing saved, nothing loaded.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.keys("Z");
            r.click(rows(r)[5].bounds);
            P.eq("saveas.click_away_cancels", b(!editing(r) && r.presets().saves() == 0 && r.presets().applies() == 0
                                                && r.open()), 1);
        }
    }

    // ---- save over a user preset (P3c) ------------------------------------------------------------------------------

    // "My Bus" (a user row, Mode bus-g) applied, then edited: current and modified, as after turning a knob. The counts
    // start at zero.
    void userModified(Rig& r)
    {
        r.presets().apply(kUserFirst + 1);
        r.presets().setModified(true);
        r.host->tick(1, kDt);                                    // settle() ticks nothing while the panel is idle
        r.settle();                                              // the header's Mode crossfade
        r.presets().resetCounts();
        r.facade.resetCounts();
    }

    void saveOver(Probe& P)
    {
        constexpr int kMyBus = kUserFirst + 1;
        {
            // The strip's SAVE on a modified user preset: one overwrite of the current row, no dialog, MODIFIED gone.
            Rig r;
            userModified(r);
            const bool before = stripValue(r) == "My Bus, modified";
            r.click(bounds(r, stripId(PS::kSaveLocal)));
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::overwrite);
            P.eq("save.strip_user_one_call",
                 b(before && calls.size() == 1 && calls[0].index == kMyBus && calls[0].ok), 1);
            P.eq("save.strip_user_clears_modified", b(stripValue(r) == "My Bus" && !r.presets().modified()
                                                      && r.presets().current() == kMyBus), 1);
            P.eq("save.strip_user_no_dialog", b(!r.open() && !editing(r) && r.presets().saves() == 0
                                                && r.presets().applies() == 0 && r.facade.writes().empty()), 1);

            // SAVED shows in the sub-line, then goes (2 s of panel time; the strip asks for full rate meanwhile).
            const int early = stripPrims(r.host->draw());
            const bool busy = r.panel->wantsFullRate();
            r.host->tick(150, kDt);
            const int late = stripPrims(r.host->draw());
            P.eq("save.strip_saved_shows_then_goes", b(busy && early >= late + 5 && !r.panel->wantsFullRate()), 1);
        }
        {
            // A factory preset, or none: SAVE is a save as, as before, and never an overwrite.
            Rig r;
            r.click(bounds(r, stripId(PS::kSaveLocal)));
            const bool factory = r.open() && editing(r) && value(r, browserId(PB::kEditLocal)) == "Gentle Glue"
                              && r.presets().overwrites() == 0;
            Rig n(-1);
            n.click(bounds(n, stripId(PS::kSaveLocal)));
            const bool none = n.open() && editing(n) && n.presets().overwrites() == 0;
            P.eq("save.strip_factory_is_saveas", b(factory && none), 1);
        }
        {
            // A refused overwrite falls back to the save as, so the sound is not lost; nothing is saved without a name.
            Rig r;
            userModified(r);
            r.presets().script(Call::overwrite, false);
            r.click(bounds(r, stripId(PS::kSaveLocal)));
            r.settle();
            P.eq("save.strip_refused_falls_back", b(r.presets().overwrites() == 1 && r.open() && editing(r)
                                                    && value(r, browserId(PB::kEditLocal)) == "My Bus"
                                                    && status(r) == "TAKEN: IT WILL BE SAVED AS 'MY BUS 2'"
                                                    && r.presets().saves() == 0
                                                    && stripValue(r) == "My Bus, modified"), 1);
        }
        {
            // Keys on the focused SAVE: Return saves over; Shift-Return is the save as.
            Rig r;
            userModified(r);
            r.keys("tab,tab,tab,tab,tab,tab");                   // the Mode latch, the gear, ‹ name › SAVE
            const bool focused = r.ctx().focus == stripId(PS::kSaveLocal);
            r.keys("return");
            const bool over = r.presets().overwrites() == 1 && !r.open() && stripValue(r) == "My Bus";
            r.keys("shift+return");
            r.settle();
            P.eq("save.strip_keys", b(focused && over && r.open() && editing(r) && r.presets().overwrites() == 1), 1);
        }
        {
            // A11y: SAVE's help names the preset it saves over; press saves over; a popup click or showMenu asks the
            // host for the menu and, left unanswered, calls nothing.
            Rig r;
            userModified(r);
            const std::vector<funkgui::A11yItem> v = r.items();
            const funkgui::A11yItem* save = byId(v, stripId(PS::kSaveLocal));
            const bool help = save != nullptr && save->title == "Save preset"
                           && save->help == "Saves over My Bus. Its menu has Save as";
            funkgui::Mods ctrl;
            ctrl.ctrl = true;
            r.click(bounds(r, stripId(PS::kSaveLocal)), ctrl);
            r.a11y(stripId(PS::kSaveLocal), funkgui::A11yAction::showMenu);
            const bool noMenu = r.presets().overwrites() == 0 && !r.open() && stripValue(r) == "My Bus, modified";
            r.a11y(stripId(PS::kSaveLocal), funkgui::A11yAction::press);
            P.eq("save.strip_a11y", b(help && noMenu && r.presets().overwrites() == 1 && !r.open()), 1);

            Rig f;
            const std::vector<funkgui::A11yItem> w = f.items();
            const funkgui::A11yItem* fs = byId(w, stripId(PS::kSaveLocal));
            P.eq("save.strip_a11y_factory",
                 b(fs != nullptr && fs->help == "Saves the current sound as a new preset"), 1);
        }
        {
            // The footer line under the hand says which save a click is.
            Rig r;
            userModified(r);
            const funkgui::Rect s = bounds(r, stripId(PS::kSaveLocal));
            r.host->move(s.centreX(), s.centreY());
            r.host->tick(1, kDt);
            const std::string user = r.ctx().hand.spec;
            Rig f;
            f.host->move(s.centreX(), s.centreY());
            f.host->tick(1, kDt);
            const std::string factory = f.ctx().hand.spec;
            P.eq("save.strip_hand", b(user == "SAVE OVER 'MY BUS'   RIGHT-CLICK OR SHIFT-RETURN: SAVE AS"
                                      && factory == "SAVE THE CURRENT SOUND AS A NEW PRESET"), 1);
        }
        {
            // SAVE's context menu (a strip of the probe's own, over the same panel): Save, Save As...; Save is the
            // click's overwrite, Save As... the browser's save as.
            Rig r;
            userModified(r);
            const ui::PanelOptions options = kProbeOptions;
            ui::HistoryStore history;
            ui::PreviewWorker worker(true);
            ui::PanelContext ctx(*r.panel, r.facade, options, funkgui::FontService::get().atlas(), history, worker);
            ctx.host = r.host.get();
            ctx.frame = r.panel->context().frame;
            PS ps(ctx);
            std::array<PS::MenuItem, 4> items{};
            const int n = ps.menu(items);
            std::string text;
            for (int i = 0; i < n; ++i)
            {
                const PS::MenuItem& it = items[static_cast<std::size_t>(i)];
                text += std::string(it.label) + (it.enabled ? "" : "(off)") + ";";
            }
            P.eq("save.strip_menu_items", b(text == "Save;Save As...;"), 1);
            ps.run(PS::Command::save);
            const bool save = r.presets().overwrites() == 1 && !r.presets().modified();
            ps.run(PS::Command::saveAs);
            r.host->tick(1, kDt);
            P.eq("save.strip_menu_run", b(save && r.presets().overwrites() == 1 && r.open() && editing(r)
                                          && value(r, browserId(PB::kEditLocal)) == "My Bus"), 1);
        }
        {
            // The browser's SAVE: over the current user preset whatever row is selected; the browser stays open.
            Rig r;
            userModified(r);
            openBrowser(r);
            selectRow(r, 3);
            const std::vector<funkgui::A11yItem> v = r.items();
            const funkgui::A11yItem* save = byId(v, browserId(PB::kSaveLocal));
            const bool item = save != nullptr && save->title == "Save" && save->enabled
                           && save->help == "Saves over My Bus";
            r.click(bounds(r, browserId(PB::kSaveLocal)));
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::overwrite);
            const std::vector<funkgui::A11yItem> shown = rows(r);
            const funkgui::A11yItem* row = rowNamed(shown, "My Bus");
            P.eq("save.browser_user", b(item && calls.size() == 1 && calls[0].index == kMyBus && calls[0].ok && r.open()
                                        && !editing(r) && status(r) == "SAVED 'MY BUS'" && row != nullptr
                                        && row->checked && stripValue(r) == "My Bus" && r.presets().saves() == 0
                                        && r.presets().applies() == 0), 1);
            r.a11y(browserId(PB::kSaveLocal), funkgui::A11yAction::press);
            P.eq("save.browser_a11y_press", r.presets().overwrites(), 2);
        }
        {
            // The browser's SAVE with a factory preset current is SAVE AS; a refused overwrite says so and starts one.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, browserId(PB::kSaveLocal)));
            P.eq("save.browser_factory_is_saveas", b(editing(r) && value(r, browserId(PB::kEditLocal)) == "Gentle Glue"
                                                     && r.presets().overwrites() == 0 && r.open()), 1);
            Rig f;
            userModified(f);
            f.presets().script(Call::overwrite, false);
            openBrowser(f);
            f.click(bounds(f, browserId(PB::kSaveLocal)));
            P.eq("save.browser_refused", b(f.presets().overwrites() == 1 && editing(f)
                                           && status(f) == "COULD NOT SAVE OVER 'MY BUS'" && f.presets().saves() == 0),
                 1);
        }
    }

    // ---- rename ---------------------------------------------------------------------------------------------------------

    void rename(Probe& P)
    {
        {
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst + 1);                        // "My Bus"
            const bool can = enabled(r, browserId(PB::kRenameLocal));
            r.click(bounds(r, browserId(PB::kRenameLocal)));
            const bool edit = editing(r) && value(r, browserId(PB::kEditLocal)) == "My Bus";
            r.keys("B,u,s,space,T,w,o");
            r.keys("return");
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::rename);
            P.eq("rename.one_call", b(can && edit && calls.size() == 1 && calls[0].index == kUserFirst + 1
                                      && calls[0].text == "Bus Two" && calls[0].ok), 1);
            P.eq("rename.shown", b(!editing(r) && rowNamed(rows(r), "Bus Two") != nullptr
                                   && status(r) == "RENAMED TO 'BUS TWO'" && r.presets().applies() == 0), 1);
        }
        {
            // Factory rows cannot be renamed.
            Rig r;
            openBrowser(r);
            selectRow(r, 2);
            const bool off = !enabled(r, browserId(PB::kRenameLocal));
            r.click(bounds(r, browserId(PB::kRenameLocal)));
            P.eq("rename.factory_refused", b(off && !editing(r) && r.presets().count(Call::rename) == 0), 1);
        }
        {
            // A taken name (any case) never reaches rename; a refused call keeps the edit.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);
            r.click(bounds(r, browserId(PB::kRenameLocal)));
            r.keys("g,e,n,t,l,e,space,g,l,u,e");
            const bool taken = !enabled(r, browserId(PB::kCommitLocal)) && status(r) == "'GENTLE GLUE' IS TAKEN";
            r.keys("return");
            const bool refused = r.presets().count(Call::rename) == 0 && editing(r);
            P.eq("rename.taken_refused", b(taken && refused), 1);
            r.keys("backspace,backspace,backspace,backspace");
            r.presets().script(Call::rename, false);
            r.keys("return");
            P.eq("rename.failure_keeps_edit", b(r.presets().count(Call::rename) == 1 && editing(r)
                                                && status(r) == "COULD NOT RENAME THE PRESET"), 1);
        }
        {
            // A click elsewhere confirms a rename; the click loads nothing.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst + 2);
            r.click(bounds(r, browserId(PB::kRenameLocal)));
            r.keys("V,o,x");
            r.click(rows(r)[2].bounds);
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::rename);
            P.eq("rename.click_away_confirms", b(calls.size() == 1 && calls[0].text == "Vox" && !editing(r)
                                                 && r.presets().applies() == 0), 1);
        }
    }

    // ---- delete ---------------------------------------------------------------------------------------------------------

    void remove(Probe& P)
    {
        {
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);                            // "Kick Room"
            r.click(bounds(r, browserId(PB::kDeleteLocal)));
            const std::vector<funkgui::A11yItem> v = r.items();
            const funkgui::A11yItem* del = byId(v, browserId(PB::kDeleteLocal));
            const bool armed = r.presets().count(Call::remove) == 0 && del != nullptr && del->title == "Confirm delete"
                            && status(r) == "PRESS DELETE AGAIN TO DELETE 'KICK ROOM'";
            r.click(bounds(r, browserId(PB::kDeleteLocal)));
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::remove);
            const bool removed = calls.size() == 1 && calls[0].index == kUserFirst && calls[0].ok
                              && rowNamed(rows(r), "Kick Room") == nullptr;
            const std::vector<funkgui::A11yItem> w = r.items();
            const funkgui::A11yItem* next = rowNamed(rows(r), "My Bus");
            P.eq("delete.arms", b(armed), 1);
            P.eq("delete.second_press_removes", b(removed && status(r) == "DELETED 'KICK ROOM'"), 1);
            P.eq("delete.selection_moves_on", b(next != nullptr && enabled(r, browserId(PB::kDeleteLocal))), 1);
        }
        {
            // A double-click on DELETE arms it and no more: its second press is not the confirmation.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);
            const funkgui::Rect d = bounds(r, browserId(PB::kDeleteLocal));
            r.host->doubleClick(d.centreX(), d.centreY());
            r.host->tick(1, kDt);
            P.eq("delete.double_click_only_arms", b(r.presets().count(Call::remove) == 0
                                                    && status(r) == "PRESS DELETE AGAIN TO DELETE 'KICK ROOM'"), 1);
        }
        {
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst + 1);
            r.keys("delete");
            r.keys("delete");
            const bool del = r.presets().count(Call::remove) == 1;
            selectRow(r, kUserFirst);
            r.keys("cmd+backspace");
            r.keys("cmd+backspace");
            P.eq("delete.keys", b(del && r.presets().count(Call::remove) == 2 && r.open()), 1);
        }
        {
            Rig r;
            openBrowser(r);
            selectRow(r, 3);
            const bool off = !enabled(r, browserId(PB::kDeleteLocal));
            r.keys("delete");
            r.keys("delete");
            P.eq("delete.factory_refused", b(off && r.presets().count(Call::remove) == 0
                                             && status(r) == "FACTORY PRESETS CANNOT BE DELETED"), 1);
        }
        {
            // The arm lapses after 3 s; Esc takes it back without closing.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);
            r.keys("delete");
            r.host->tick(200, kDt);
            r.keys("delete");
            const bool lapsed = r.presets().count(Call::remove) == 0;
            r.keys("escape");
            r.keys("delete");
            P.eq("delete.lapses_and_escape", b(lapsed && r.presets().count(Call::remove) == 0 && r.open()), 1);
        }
    }

    // ---- import ---------------------------------------------------------------------------------------------------------

    void import(Probe& P)
    {
        {
            Rig r;
            const bool interest = !r.panel->filesInterest({ "/x/readme.txt" })
                               && r.panel->filesInterest({ "/x/A.FCMPPRESET" })
                               && r.panel->filesInterest({ "/x/readme.txt", "/x/b.fcmppreset" })
                               && !r.panel->filesInterest({});
            P.eq("import.interest", b(interest), 1);

            r.panel->filesDropped({ "/tmp/Snare Room.fcmppreset", "/tmp/readme.txt" });
            r.settle();
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::importFile);
            const std::vector<funkgui::A11yItem> shown = rows(r);    // kept: `row` points into it
            const funkgui::A11yItem* row = rowNamed(shown, "Snare Room");
            P.eq("import.one_call_per_file", b(calls.size() == 1 && calls[0].text == "/tmp/Snare Room.fcmppreset"), 1);
            P.eq("import.opens_and_loads", b(r.open() && row != nullptr && row->checked && r.presets().applies() == 1
                                             && r.presets().current() == 15
                                             && status(r) == "IMPORTED 'SNARE ROOM'"), 1);
        }
        {
            Rig r;
            r.panel->filesDropped({ "/tmp/One.fcmppreset", "/tmp/Two.fcmppreset" });
            r.settle();
            P.eq("import.two_files", b(r.presets().count(Call::importFile) == 2 && r.presets().applies() == 0
                                       && status(r) == "IMPORTED 2 PRESETS" && rowNamed(rows(r), "One") != nullptr), 1);
        }
        {
            Rig r;
            r.presets().script(Call::importFile, false);
            r.panel->filesDropped({ "/tmp/Bad.fcmppreset" });
            r.settle();
            P.eq("import.refused", b(r.presets().count(Call::importFile) == 1 && r.presets().count() == 15
                                     && r.presets().applies() == 0 && r.open()
                                     && status(r) == "COULD NOT IMPORT 'BAD.FCMPPRESET'"), 1);
        }
    }

    // ---- export and the menus (a browser of the probe's own) ------------------------------------------------------------

    void exportAndMenus(Probe& P)
    {
        Rig r;
        const ui::PanelOptions options = kProbeOptions;
        ui::HistoryStore history;
        ui::PreviewWorker worker(true);
        ui::PanelContext ctx(*r.panel, r.facade, options, funkgui::FontService::get().atlas(), history, worker);
        funkgui::GestureController gestures(*r.host);
        ctx.host = r.host.get();
        ctx.gestures = &gestures;
        ctx.frame = r.panel->context().frame;
        ctx.overlay = ui::Overlay::presetBrowser;
        PB pb(ctx);
        const auto tick = [&] {
            ctx.seconds += static_cast<double>(kDt);
            ctx.handNext = ui::HandState{};
            pb.tick(kDt);
            ctx.hand = ctx.handNext;
        };
        const auto a11y = [&] {
            std::vector<funkgui::A11yItem> v;
            pb.accessibility(v);
            return v;
        };
        const auto statusOf = [&] {
            const std::vector<funkgui::A11yItem> v = a11y();
            const funkgui::A11yItem* it = byId(v, browserId(PB::kStatusLocal));
            return it != nullptr ? it->value : std::string();
        };
        tick();

        const bool ok = pb.exportTo(5, "/tmp/Mix Bus Glue.fcmppreset");
        const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::exportFile);
        P.eq("export.one_call", b(ok && calls.size() == 1 && calls[0].index == 5
                                  && calls[0].text == "/tmp/Mix Bus Glue.fcmppreset"
                                  && statusOf() == "EXPORTED 'MIX BUS GLUE.FCMPPRESET'"), 1);
        r.presets().script(Call::exportFile, false);
        P.eq("export.refused", b(!pb.exportTo(5, "/tmp/x.fcmppreset") && r.presets().count(Call::exportFile) == 2
                                 && statusOf() == "COULD NOT EXPORT 'X.FCMPPRESET'"), 1);

        // IMPORT and EXPORT ask the host for a chooser; left unanswered, they call nothing.
        pb.a11yAction(rowId(5), funkgui::A11yAction::focus, 0.0);
        pb.a11yAction(browserId(PB::kExportLocal), funkgui::A11yAction::press, 0.0);
        pb.a11yAction(browserId(PB::kImportLocal), funkgui::A11yAction::press, 0.0);
        P.eq("export.headless_no_chooser", b(r.presets().count(Call::exportFile) == 2
                                             && r.presets().count(Call::importFile) == 0), 1);

        // The context menu per row kind.
        std::array<PB::MenuItem, 8> items{};
        const auto menuText = [&](int index) {
            const int n = pb.menu(index, items);
            std::string s;
            for (int i = 0; i < n; ++i)
            {
                const PB::MenuItem& it = items[static_cast<std::size_t>(i)];
                s += std::string(it.separatorBefore ? "|" : "") + it.label + (it.enabled ? "" : "(off)") + ";";
            }
            return s;
        };
        P.eq("menu.factory", b(menuText(2) == "Load;|Save As...;Rename...(off);Export...;Import...;|Delete(off);"), 1);
        P.eq("menu.user", b(menuText(kUserFirst) == "Load;|Save As...;Rename...;Export...;Import...;|Delete;"), 1);
        P.eq("menu.background", b(menuText(-1) == "Save As...;Import...;"), 1);

        r.presets().resetCounts();
        pb.run(PB::Command::load, 7);
        const bool load = r.presets().applies() == 1 && r.presets().current() == 7;
        pb.run(PB::Command::rename, 2);                          // factory: nothing
        const bool factoryRename = byId(a11y(), browserId(PB::kEditLocal)) == nullptr;
        pb.run(PB::Command::rename, kUserFirst);
        const std::vector<funkgui::A11yItem> v = a11y();
        const funkgui::A11yItem* edit = byId(v, browserId(PB::kEditLocal));
        const bool rename = edit != nullptr && edit->value == "Kick Room";
        pb.run(PB::Command::remove, kUserFirst + 2);             // the menu choice is the confirmation
        const bool remove = r.presets().count(Call::remove) == 1
                         && r.presets().calls(Call::remove)[0].index == kUserFirst + 2;
        pb.run(PB::Command::remove, 1);                          // factory: nothing
        P.eq("menu.run", b(load && factoryRename && rename && remove && r.presets().count(Call::remove) == 1), 1);
    }

    // ---- the list's clip on device px (web Sprint C, ADR-93) --------------------------------------------------------

    // A browser's dpi is physical height / logical height: 1.5625 is 1000 physical px for the Panel's 640. There 88
    // and 308 are device rows 137.5 and 481.25, and an unsnapped clip would cut through the pixel centres.
    void clipSnap(Probe& P)
    {
        constexpr float kDpi = 1.5625f;
        Rig r(1, sampleRows(), 0, kDpi);
        openBrowser(r);
        funkgui::WheelEvent w;
        w.x = 500.0f;
        w.y = 200.0f;
        w.dy = -30.0f / 512.0f;                                  // the list up by 30 px: rows are cut at both edges
        w.smooth = true;
        const bool used = r.panel->wheel(w);
        r.host->tick(1, kDt);
        float top = 1.0e9f, bottom = -1.0e9f;
        int rowPrims = 0;
        for (const funkgui::Prim& p : r.host->draw().prims)
            if (p.tag == ui::tag::browserRow || p.tag == ui::tag::browserCurrent)
            {
                ++rowPrims;
                top = std::min(top, p.y0);
                bottom = std::max(bottom, p.y1);
            }
        P.eq("scroll.clip_rows_drawn", b(used && rowPrims > 0), 1);
        P.near("scroll.clip_top_device_px", static_cast<double>(top) * static_cast<double>(kDpi), 138.0, 1.0e-3);
        P.near("scroll.clip_bottom_device_px", static_cast<double>(bottom) * static_cast<double>(kDpi), 481.0, 1.0e-3);
    }

    // ---- the host's services: menus and file choosers (web Sprint C, ADR-93) ----------------------------------------

    funkgui::Mods popupMods()
    {
        funkgui::Mods m;
        m.ctrl = true;                                           // a ctrl-click is a popup click (HeadlessHost)
        return m;
    }

    // A request's items as one line: "id:label" with "(off)" and "(x)" for a disabled and a ticked one, "|" for a
    // separator.
    std::string menuLine(const funkgui::MenuRequest& m)
    {
        std::string s;
        for (const funkgui::MenuItem& it : m.items)
            s += it.separator ? std::string("|")
                              : std::to_string(it.id) + ":" + it.label + (it.enabled ? "" : "(off)")
                                    + (it.checked ? "(x)" : "") + ";";
        return s;
    }

    bool sameRect(const funkgui::Rect& a, const funkgui::Rect& c)
    {
        return a.x == c.x && a.y == c.y && a.w == c.w && a.h == c.h;
    }

    bool sameCol(funkgui::Col a, funkgui::Col c) { return a.r == c.r && a.g == c.g && a.b == c.b && a.a == c.a; }

    // The product's PAPER (ProductTheme.h: ink100 0B0C0E): not FunkGui's, and not a request's default GRAPHITE.
    bool isProductPaper(const funkgui::Theme& t)
    {
        const funkgui::Theme want = ui::productTheme(ui::kThemePaper);
        return sameCol(t.ground, want.ground) && sameCol(t.ink100, want.ink100) && sameCol(t.ink70, want.ink70)
            && sameCol(t.ink52, want.ink52) && sameCol(t.ink32, want.ink32) && sameCol(t.ink16, want.ink16)
            && sameCol(t.accent, want.accent) && sameCol(t.accentDim, want.accentDim) && sameCol(t.signal, want.signal)
            && t.textGamma == want.textGamma && sameCol(t.ink100, funkgui::Col{ 0x0B, 0x0C, 0x0E });
    }

    // The sample list without the rows whose uuid is given: another process changed the store.
    std::vector<Row> rowsWithout(std::string_view uuid)
    {
        std::vector<Row> rows = sampleRows();
        rows.erase(std::remove_if(rows.begin(), rows.end(), [uuid](const Row& r) { return r.uuid == uuid; }),
                   rows.end());
        return rows;
    }

    // A host that serves less than HeadlessHost: every call goes on to it, except that services() loses the `without`
    // bits and a call for a service that is not reported refuses, as HostServices' defaults do (`refused` counts
    // them). While it lives the Panel is attached to it; input, ticks and frames stay the HeadlessHost's.
    class LesserHost final : public funkgui::HostServices
    {
    public:
        LesserHost(ui::Panel& panel, funkgui::HeadlessHost& inner, unsigned without)
            : panel_(panel), inner_(inner), without_(without)
        {
            panel_.attach(*this);
        }
        ~LesserHost() override { panel_.attach(inner_); }

        LesserHost(const LesserHost&) = delete;
        LesserHost& operator=(const LesserHost&) = delete;

        void   setUnboundedDrag(bool on) override { inner_.setUnboundedDrag(on); }
        void   showParamMenu(funkgui::ParamPort& p, float x, float y) override { inner_.showParamMenu(p, x, y); }
        void   nudgeFullRate() override { inner_.nudgeFullRate(); }
        double nowSeconds() const override { return inner_.nowSeconds(); }
        void   beginBatch() override { inner_.beginBatch(); }
        void   endBatch() override { inner_.endBatch(); }
        int    themeIndex() const override { return inner_.themeIndex(); }
        int    zoomPercent() const override { return inner_.zoomPercent(); }
        void   setZoomPercent(int percent) override { inner_.setZoomPercent(percent); }
        std::span<const int> zoomSteps() const override { return inner_.zoomSteps(); }
        bool   zoomFits(int percent) const override { return inner_.zoomFits(percent); }
        unsigned services() const override { return inner_.services() & ~without_; }
        bool   showMenu(const funkgui::MenuRequest& request, funkgui::MenuCallback done) override
        {
            return serves(funkgui::hostservice::menus) && inner_.showMenu(request, std::move(done));
        }
        void   dismissMenus() override { inner_.dismissMenus(); }
        bool   chooseFiles(const funkgui::FileRequest& request, funkgui::FilesCallback done) override
        {
            return serves(funkgui::hostservice::fileChooser) && inner_.chooseFiles(request, std::move(done));
        }
        bool   copyText(std::string_view utf8) override
        {
            return serves(funkgui::hostservice::clipboard) && inner_.copyText(utf8);
        }
        bool   commandKeyIsMeta() const override { return inner_.commandKeyIsMeta(); }

        int refused = 0;

    private:
        bool serves(unsigned service)
        {
            if ((without_ & service) == 0u)
                return true;
            ++refused;
            return false;
        }

        ui::Panel&             panel_;
        funkgui::HeadlessHost& inner_;
        unsigned               without_;
    };

    void stripMenu(Probe& P)
    {
        {
            // The request, in PAPER at a 150 % UI zoom (the anchor stays in the Panel's own px: the host scales it).
            Rig r(1, sampleRows(), ui::kThemePaper);
            userModified(r);
            r.host->setZoom({ 100, 150 }, 150);
            r.click(bounds(r, stripId(PS::kSaveLocal)), popupMods());
            const funkgui::MenuRequest* m = r.host->pendingMenu();
            P.eq("menu.strip_request", b(m != nullptr && r.host->log.menuRequests == 1
                                         && menuLine(*m) == "1:Save;2:Save As...;"), 1);
            P.eq("menu.strip_anchor", b(m != nullptr && sameRect(m->anchor, { 556.0f, 21.0f, 44.0f, 18.0f })), 1);
            P.eq("menu.strip_theme", b(m != nullptr && isProductPaper(m->theme)), 1);
            // Save As...: the browser's edit, whatever is current; nothing is saved over.
            const bool chosen = r.host->chooseMenuItem("Save As...");
            r.host->tick(1, kDt);
            P.eq("menu.strip_save_as", b(chosen && r.open() && editing(r)
                                         && value(r, browserId(PB::kEditLocal)) == "My Bus"
                                         && r.presets().overwrites() == 0 && r.presets().saves() == 0), 1);
        }
        {
            // A dismissed menu does nothing; Save is the click's overwrite (here from a11y showMenu).
            Rig r;
            userModified(r);
            r.a11y(stripId(PS::kSaveLocal), funkgui::A11yAction::showMenu);
            const bool cancelled = r.host->cancelMenu();
            r.host->tick(1, kDt);
            P.eq("menu.strip_cancel", b(cancelled && r.presets().overwrites() == 0 && !r.open()
                                        && stripValue(r) == "My Bus, modified"), 1);
            r.a11y(stripId(PS::kSaveLocal), funkgui::A11yAction::showMenu);
            const bool chosen = r.host->chooseMenuItem(1);
            r.host->tick(1, kDt);
            P.eq("menu.strip_save", b(chosen && r.presets().overwrites() == 1 && !r.open()
                                      && stripValue(r) == "My Bus"), 1);
        }
    }

    void browserMenus(Probe& P)
    {
        {
            // A popup click on a factory row, in PAPER at a 150 % UI zoom: the items of menu() with its separators,
            // anchored on the row.
            Rig r(1, sampleRows(), ui::kThemePaper);
            openBrowser(r);
            r.host->setZoom({ 100, 150 }, 150);
            const funkgui::Rect row = bounds(r, rowId(5));       // "Mix Bus Glue"
            r.click(row, popupMods());
            const funkgui::MenuRequest* m = r.host->pendingMenu();
            const char* factoryRow = "1:Load;|2:Save As...;3:Rename...(off);4:Export...;5:Import...;|6:Delete(off);";
            P.eq("menu.row_request", b(m != nullptr && r.host->log.menuRequests == 1 && menuLine(*m) == factoryRow), 1);
            P.eq("menu.row_anchor", b(m != nullptr && row.w > 0.0f && sameRect(m->anchor, row)), 1);
            P.eq("menu.row_theme", b(m != nullptr && isProductPaper(m->theme)), 1);
            // Dismissed: nothing. The popup click selected the row: Return loads it and closes.
            const bool cancelled = r.host->cancelMenu();
            r.host->tick(1, kDt);
            const bool nothing = r.presets().applies() == 0 && r.open() && !editing(r);
            r.keys("return");
            r.settle();
            P.eq("menu.row_cancel", b(cancelled && nothing), 1);
            P.eq("menu.row_selects", b(r.presets().applies() == 1 && r.presets().current() == 5 && !r.open()), 1);
        }
        {
            // Load is one apply of the row and keeps the browser open; a disabled item cannot be chosen.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, rowId(7)), popupMods());
            const bool off = !r.host->chooseMenuItem("Delete");
            const bool chosen = r.host->chooseMenuItem("Load");
            r.host->tick(1, kDt);
            P.eq("menu.row_load", b(off && chosen && r.presets().applies() == 1 && r.presets().current() == 7
                                    && r.open() && r.presets().count(Call::remove) == 0), 1);
        }
        {
            // Delete on a user row (a11y showMenu): the menu choice is the confirmation, one remove.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);                            // "Kick Room", brought into view
            r.a11y(rowId(kUserFirst), funkgui::A11yAction::showMenu);
            const funkgui::MenuRequest* m = r.host->pendingMenu();
            const bool items = m != nullptr
                            && menuLine(*m) == "1:Load;|2:Save As...;3:Rename...;4:Export...;5:Import...;|6:Delete;"
                            && sameRect(m->anchor, bounds(r, rowId(kUserFirst)));
            const bool chosen = r.host->chooseMenuItem("Delete");
            r.host->tick(1, kDt);
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::remove);
            P.eq("menu.row_delete", b(items && chosen && calls.size() == 1 && calls[0].index == kUserFirst
                                      && calls[0].ok && status(r) == "DELETED 'KICK ROOM'"), 1);
        }
        {
            // The row went while the menu was open (another process changed the store): the answer does nothing, not
            // even for a command that needs no row (Save As... would start an edit).
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);
            r.click(bounds(r, rowId(kUserFirst)), popupMods());
            r.presets().setRows(rowsWithout("u-00"));
            r.presets().resetCounts();
            const bool answered = r.host->chooseMenuItem("Save As...");
            r.host->tick(1, kDt);
            P.eq("menu.row_gone", b(answered && !editing(r) && r.open() && r.presets().count() == 14
                                    && r.presets().saves() == 0 && r.presets().applies() == 0), 1);
        }
        {
            // The row moved while the menu was open: it is found again by its uuid.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst + 1);                        // "My Bus", u-01
            r.click(bounds(r, rowId(kUserFirst + 1)), popupMods());
            r.presets().setRows(rowsWithout("u-00"));            // "My Bus" is row 12 now
            r.presets().resetCounts();
            const bool answered = r.host->chooseMenuItem("Delete");
            r.host->tick(1, kDt);
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::remove);
            P.eq("menu.row_refound", b(answered && calls.size() == 1 && calls[0].index == kUserFirst && calls[0].ok
                                       && status(r) == "DELETED 'MY BUS'"), 1);
        }
        {
            // The background (here the filter column, under the filters): Save As... and Import..., a 1 x 1 anchor
            // under the pointer. Import... asks for the chooser.
            Rig r;
            openBrowser(r);
            r.host->click(100.0f, 300.0f, popupMods());
            r.host->tick(1, kDt);
            const funkgui::MenuRequest* m = r.host->pendingMenu();
            P.eq("menu.background_request", b(m != nullptr && menuLine(*m) == "2:Save As...;5:Import...;"
                                              && sameRect(m->anchor, { 100.0f, 300.0f, 1.0f, 1.0f })), 1);
            const bool chosen = r.host->chooseMenuItem("Import...");
            const funkgui::FileRequest* f = r.host->pendingFiles();
            P.eq("menu.background_import", b(chosen && f != nullptr && f->mode == funkgui::FileRequest::Mode::openMany
                                             && r.presets().count(Call::importFile) == 0), 1);
        }
    }

    void categoryMenu(Probe& P)
    {
        const funkgui::Rect kWord { 618.0f, 67.0f, 180.0f, 18.0f };   // the category word of the save-as line
        {
            // The request, in PAPER at a 150 % UI zoom: No Category, a separator, the categories in filter order, the
            // edit's one ticked.
            Rig r(1, sampleRows(), ui::kThemePaper);
            openBrowser(r);
            r.host->setZoom({ 100, 150 }, 150);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.click(bounds(r, browserId(PB::kCategoryLocal)));
            const funkgui::MenuRequest* m = r.host->pendingMenu();
            P.eq("saveas.category_menu",
                 b(m != nullptr && editing(r)
                   && menuLine(*m) == "1:No Category;|100:Bus(x);101:Drums;102:Init;103:Master;104:Vocal;"), 1);
            P.eq("saveas.category_anchor", b(m != nullptr && sameRect(m->anchor, kWord)), 1);
            P.eq("saveas.category_theme", b(m != nullptr && isProductPaper(m->theme)), 1);
            // Choosing one sets the edit's category, and the save takes it.
            const bool chosen = r.host->chooseMenuItem("Drums");
            r.host->tick(1, kDt);
            const bool shown = value(r, browserId(PB::kCategoryLocal)) == "Drums" && editing(r);
            r.keys("K,i,t");
            r.keys("return");
            const Row saved = r.presets().row(r.presets().count() - 1);
            P.eq("saveas.category_chosen", b(chosen && shown && r.presets().saves() == 1 && saved.name == "Kit"
                                             && saved.category == "Drums"), 1);
        }
        {
            // No Category (from a11y press on the word), with "Drums" ticked after the filter gave it.
            Rig r;
            openBrowser(r);
            for (const funkgui::A11yItem& it : r.items())
                if (it.role == funkgui::A11yRole::radioButton && it.title == "Drums")
                    r.click(it.bounds);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.a11y(browserId(PB::kCategoryLocal), funkgui::A11yAction::press);
            const funkgui::MenuRequest* m = r.host->pendingMenu();
            const bool ticked = m != nullptr
                             && menuLine(*m) == "1:No Category;|100:Bus;101:Drums(x);102:Init;103:Master;104:Vocal;";
            const bool chosen = r.host->chooseMenuItem(1);
            r.host->tick(1, kDt);
            P.eq("saveas.category_none", b(ticked && chosen && value(r, browserId(PB::kCategoryLocal)) == "None"), 1);
        }
        {
            // The edit was cancelled while the menu was open: the answer changes nothing and asks for no frame.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.click(bounds(r, browserId(PB::kCategoryLocal)));
            const bool pending = r.host->pendingMenu() != nullptr;
            r.keys("escape");
            const int nudges = r.host->log.nudges;
            const uint32_t rev = r.panel->a11yRevision();
            const bool answered = r.host->chooseMenuItem("Drums");
            P.eq("saveas.category_stale", b(pending && answered && !editing(r) && r.open()
                                            && r.host->log.nudges == nudges && r.panel->a11yRevision() == rev
                                            && r.presets().saves() == 0), 1);
        }
    }

    void choosers(Probe& P)
    {
        {
            // EXPORT: a save chooser named after the selected preset; the host makes the name legal and gives the
            // chosen path the extension.
            Rig r;
            openBrowser(r);
            selectRow(r, 4);                                     // "Master -1 dBTP"
            r.click(bounds(r, browserId(PB::kExportLocal)));
            const funkgui::FileRequest* f = r.host->pendingFiles();
            P.eq("export.chooser_request", b(f != nullptr && r.host->log.fileRequests == 1
                                             && f->mode == funkgui::FileRequest::Mode::save
                                             && f->title == "Export preset" && f->pattern == "*.fcmppreset"
                                             && f->suggestedName == "Master -1 dBTP.fcmppreset"
                                             && r.presets().count(Call::exportFile) == 0), 1);
            const bool returned = r.host->returnFiles({ "/tmp/x" });
            r.host->tick(1, kDt);
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::exportFile);
            P.eq("export.chooser_result", b(returned && calls.size() == 1 && calls[0].index == 4
                                            && calls[0].text == "/tmp/x.fcmppreset" && r.host->pendingFiles() == nullptr
                                            && status(r) == "EXPORTED 'X.FCMPPRESET'"), 1);
            // Cancelled: nothing more is exported and nothing is said.
            r.host->tick(200, kDt);                              // the message lapses (3 s)
            const std::string before = status(r);
            r.click(bounds(r, browserId(PB::kExportLocal)));
            const bool cancelled = r.host->pendingFiles() != nullptr && r.host->cancelFiles();
            r.host->tick(1, kDt);
            P.eq("export.chooser_cancel", b(cancelled && r.presets().count(Call::exportFile) == 1
                                            && status(r) == before && before.find("EXPORTED") == std::string::npos), 1);
        }
        {
            // The preset went while the chooser was open: nothing is exported, and the browser says so.
            Rig r;
            openBrowser(r);
            selectRow(r, kUserFirst);
            r.a11y(browserId(PB::kExportLocal), funkgui::A11yAction::press);
            const bool pending = r.host->pendingFiles() != nullptr
                              && r.host->pendingFiles()->suggestedName == "Kick Room.fcmppreset";
            r.presets().setRows(rowsWithout("u-00"));
            const bool returned = r.host->returnFiles({ "/tmp/Kick Room.fcmppreset" });
            r.host->tick(1, kDt);
            P.eq("export.chooser_gone", b(pending && returned && r.presets().count(Call::exportFile) == 0
                                          && status(r) == "THAT PRESET IS GONE"), 1);
        }
        {
            // IMPORT: an openMany chooser; two paths are two importFile calls; a cancel imports nothing.
            Rig r;
            openBrowser(r);
            r.click(bounds(r, browserId(PB::kImportLocal)));
            const funkgui::FileRequest* f = r.host->pendingFiles();
            P.eq("import.chooser_request", b(f != nullptr && r.host->log.fileRequests == 1
                                             && f->mode == funkgui::FileRequest::Mode::openMany
                                             && f->title == "Import presets" && f->pattern == "*.fcmppreset"
                                             && f->suggestedName.empty()), 1);
            const bool cancelled = r.host->cancelFiles();
            r.host->tick(1, kDt);
            P.eq("import.chooser_cancel", b(cancelled && r.presets().count(Call::importFile) == 0
                                            && r.presets().count() == 15 && r.host->pendingFiles() == nullptr), 1);
            r.click(bounds(r, browserId(PB::kImportLocal)));
            const bool returned = r.host->returnFiles({ "/tmp/One.fcmppreset", "/tmp/Two.fcmppreset" });
            r.settle();
            const std::vector<FakePresets::CallLog>& calls = r.presets().calls(Call::importFile);
            P.eq("import.chooser_result", b(returned && calls.size() == 2 && calls[0].text == "/tmp/One.fcmppreset"
                                            && calls[1].text == "/tmp/Two.fcmppreset" && r.presets().applies() == 0
                                            && status(r) == "IMPORTED 2 PRESETS"), 1);
        }
    }

    // Views of the probe's own over the Panel's host, gone while their menu and chooser are open: the host still
    // holds the callbacks, and the answers reach nobody (the weak `alive` guards). No destructor called a service.
    void viewsGone(Probe& P)
    {
        Rig r;
        userModified(r);
        const ui::PanelOptions options = kProbeOptions;
        ui::HistoryStore history;
        ui::PreviewWorker worker(true);
        ui::PanelContext ctx(*r.panel, r.facade, options, funkgui::FontService::get().atlas(), history, worker);
        ctx.host = r.host.get();
        ctx.frame = r.panel->context().frame;
        ctx.overlay = ui::Overlay::presetBrowser;
        const int dismissals = r.host->log.menuDismissals;
        bool pending = true;
        {
            PS ps(ctx);
            ps.a11yAction(stripId(PS::kSaveLocal), funkgui::A11yAction::showMenu, 0.0);
            pending = pending && r.host->pendingMenu() != nullptr;
        }
        const bool stripAnswered = r.host->chooseMenuItem("Save");
        const bool stripNothing = r.presets().overwrites() == 0;
        {
            PB pb(ctx);
            pb.a11yAction(rowId(kUserFirst), funkgui::A11yAction::showMenu, 0.0);
            pb.a11yAction(browserId(PB::kImportLocal), funkgui::A11yAction::press, 0.0);
            pending = pending && r.host->pendingMenu() != nullptr && r.host->pendingFiles() != nullptr;
        }
        const bool menuAnswered = r.host->chooseMenuItem("Delete");
        const bool filesAnswered = r.host->returnFiles({ "/tmp/One.fcmppreset" });
        P.eq("services.view_gone", b(pending && stripAnswered && stripNothing && menuAnswered && filesAnswered
                                     && r.presets().count(Call::remove) == 0
                                     && r.presets().count(Call::importFile) == 0
                                     && r.host->log.menuDismissals == dismissals), 1);
    }

    void noChooser(Probe& P)
    {
        Rig r;
        LesserHost lesser(*r.panel, *r.host, funkgui::hostservice::fileChooser);
        openBrowser(r);
        selectRow(r, kUserFirst);
        // The cells: disabled in a11y, no hand over them, and not Tab stops (SAVE AS still is).
        const funkgui::Rect imp = bounds(r, browserId(PB::kImportLocal)), exp = bounds(r, browserId(PB::kExportLocal));
        const bool off = !enabled(r, browserId(PB::kImportLocal)) && !enabled(r, browserId(PB::kExportLocal))
                      && enabled(r, browserId(PB::kSaveAsLocal)) && enabled(r, browserId(PB::kDeleteLocal));
        const auto cursorOver = [&r](const funkgui::Rect& cell) {
            r.host->move(cell.centreX(), cell.centreY());
            r.host->tick(1, kDt);
            return r.panel->cursor();
        };
        const bool noHand = cursorOver(imp) == funkgui::Cursor::normal && cursorOver(exp) == funkgui::Cursor::normal
                         && cursorOver(bounds(r, browserId(PB::kSaveAsLocal))) == funkgui::Cursor::pointingHand;
        bool tabbed = false, saveAsStop = false;
        for (int k = 0; k < 40; ++k)
        {
            r.keys("tab");
            tabbed = tabbed || r.ctx().focus == browserId(PB::kImportLocal)
                  || r.ctx().focus == browserId(PB::kExportLocal);
            saveAsStop = saveAsStop || r.ctx().focus == browserId(PB::kSaveAsLocal);
        }
        P.eq("import.no_chooser", b(off && noHand && !tabbed && saveAsStop), 1);
        // Pressing them (a click, a11y) asks no host for a chooser and calls nothing.
        selectRow(r, kUserFirst);
        r.click(imp);
        r.click(exp);
        r.a11y(browserId(PB::kImportLocal), funkgui::A11yAction::press);
        r.a11y(browserId(PB::kExportLocal), funkgui::A11yAction::press);
        P.eq("import.no_chooser_press", b(lesser.refused == 0 && r.host->log.fileRequests == 0
                                          && r.presets().count(Call::importFile) == 0
                                          && r.presets().count(Call::exportFile) == 0), 1);
        // Menus are not gated: the row's menu opens, with Export... and Import... off.
        r.a11y(rowId(kUserFirst), funkgui::A11yAction::showMenu);
        const funkgui::MenuRequest* m = r.host->pendingMenu();
        P.eq("import.no_chooser_menu",
             b(m != nullptr
               && menuLine(*m) == "1:Load;|2:Save As...;3:Rename...;4:Export...(off);5:Import...(off);|6:Delete;"), 1);
        // A dropped file still imports: that is the Panel's, not the chooser's.
        r.panel->filesDropped({ "/tmp/Snare Room.fcmppreset" });
        r.settle();
        P.eq("import.no_chooser_drop", r.presets().count(Call::importFile), 1);
    }

    // The footer line under the hand over IMPORT and EXPORT, with the chooser and without it.
    void chooserHands(Probe& P)
    {
        // The hand's line over IMPORT, over EXPORT with a user row selected, and over EXPORT with nothing selected (the
        // FACTORY filter does not show that row).
        const auto lines = [](Rig& r) {
            const auto over = [&r](uint32_t id) {
                const funkgui::Rect cell = bounds(r, id);
                r.host->move(cell.centreX(), cell.centreY());
                r.host->tick(1, kDt);
                return std::string(r.ctx().hand.spec);
            };
            openBrowser(r);
            selectRow(r, kUserFirst);
            std::array<std::string, 3> l;
            l[0] = over(browserId(PB::kImportLocal));
            l[1] = over(browserId(PB::kExportLocal));
            r.a11y(browserId(PB::kFilterLocal0 + 1), funkgui::A11yAction::press);
            l[2] = over(browserId(PB::kExportLocal));
            return l;
        };
        const auto fits = [](const Rig& r, const std::string& line) {
            return funkgui::text::fits(r.ctx().atlas, line.c_str(), funkgui::type::kLabel, L::footer::kSpecLineW);
        };
        {
            Rig r;
            const std::array<std::string, 3> l = lines(r);
            P.eq("import.chooser_hand", b(l[0] == "IMPORT .FCMPPRESET FILES   OR DROP THEM ON THE PLUGIN"
                                          && l[1] == "EXPORT 'KICK ROOM' TO A FILE"
                                          && l[2] == "SELECT A PRESET TO EXPORT"), 1);
        }
        {
            Rig r;
            LesserHost lesser(*r.panel, *r.host, funkgui::hostservice::fileChooser);
            const std::array<std::string, 3> l = lines(r);
            P.eq("import.no_chooser_import_hand",
                 b(l[0] == "IMPORTING PRESET FILES IS NOT AVAILABLE HERE" && fits(r, l[0])), 1);
            P.eq("import.no_chooser_export_hand",
                 b(l[1] == "EXPORTING PRESET FILES IS NOT AVAILABLE HERE" && l[2] == l[1] && fits(r, l[1])), 1);
        }
    }

    // ---- a11y, drawing, writes ------------------------------------------------------------------------------------------

    void a11yAndDraw(Probe& P, const std::string& pictures)
    {
        const auto shoot = [&](Rig& r, const char* what) {
            if (pictures.empty())
                return;                                          // review pictures, not a test
            std::error_code ec;
            std::filesystem::create_directories(pictures, ec);
            r.host->draw();
            const std::string path = pictures + "/presets-" + what + ".png";
            if (r.host->writePng(path.c_str(), 2))
                std::printf("PNG      %s\n", path.c_str());
        };
        const auto idsOk = [](const std::vector<funkgui::A11yItem>& v) {
            std::set<uint32_t> seen;
            for (const funkgui::A11yItem& it : v)
                if (it.id == 0 || !seen.insert(it.id).second)
                    return false;
            return true;
        };
        int missing = 0;
        bool inside = true;
        const auto frame = [&](Rig& r) {
            r.settle();                                          // the header's Mode crossfade, the hover eases
            const funkgui::PrimList& pl = r.host->draw();
            missing += static_cast<int>(pl.missingGlyphs);
            inside = inside && insideRegions(pl);
        };

        {
            Rig r;
            r.presets().setModified(true);
            r.host->tick(1, kDt);
            frame(r);
            shoot(r, "strip-modified");
            openBrowser(r);
            selectRow(r, kUserFirst + 1);
            r.host->move(bounds(r, rowId(5)).centreX(), bounds(r, rowId(5)).centreY());
            r.host->tick(1, kDt);
            frame(r);
            shoot(r, "browser");
            const std::vector<funkgui::A11yItem> v = r.items();
            P.eq("a11y.ids", b(idsOk(v)), 1);
            const std::string footer = r.ctx().hand.spec;
            P.eq("a11y.row_hand", b(footer.find("CLICK: LOAD") == 0), 1);

            r.a11y(rowId(6), funkgui::A11yAction::focus);
            const bool focus = r.presets().applies() == 0;
            r.a11y(rowId(6), funkgui::A11yAction::press);
            P.eq("a11y.row_actions", b(focus && r.presets().applies() == 1 && r.presets().current() == 6), 1);

            r.click(bounds(r, browserId(PB::kSaveAsLocal)));
            r.keys("G,e,n,t,l,e,space,G,l,u,e");
            frame(r);
            shoot(r, "saveas");
            P.eq("a11y.ids_editing", b(idsOk(r.items())), 1);
            r.keys("escape");
            selectRow(r, kUserFirst);
            r.click(bounds(r, browserId(PB::kRenameLocal)));
            r.keys("K,i,c,k,space,B,i,g");
            frame(r);
            shoot(r, "rename");
            r.keys("escape");
            r.keys("delete");
            frame(r);
            shoot(r, "delete-armed");
            r.keys("escape");
            for (const funkgui::A11yItem& it : r.items())
                if (it.role == funkgui::A11yRole::radioButton && it.title == "User")
                    r.click(it.bounds);
            frame(r);
            shoot(r, "filter-user");
        }
        {
            Rig r(1, sampleRows(), 1);
            openBrowser(r);
            frame(r);
            shoot(r, "browser-paper");
        }
        {
            Rig r;
            r.panel->filesDropped({ "/tmp/Snare Room.fcmppreset" });
            r.settle();
            frame(r);
            shoot(r, "import");
            P.eq("writes.none", b(r.facade.writes().empty()), 1);
        }
        {
            // P3c: a modified user preset before and after SAVE, in the strip and in the browser. The frames after a
            // save are drawn inside SAVED's 2 s (no settle), with the pointer still on the SAVE it pressed.
            const auto now = [&](Rig& r) {
                const funkgui::PrimList& pl = r.host->draw();
                missing += static_cast<int>(pl.missingGlyphs);
                inside = inside && insideRegions(pl);
            };
            Rig r;
            userModified(r);
            r.host->tick(240, kDt);                              // past the footer's 3 s Mode summary: the spec shows
            frame(r);
            shoot(r, "save-strip-before");
            const funkgui::Rect s = bounds(r, stripId(PS::kSaveLocal));
            r.host->move(s.centreX(), s.centreY());
            r.host->tick(20, kDt);
            now(r);
            shoot(r, "save-strip-hover");
            r.click(s);
            r.host->tick(20, kDt);
            now(r);
            shoot(r, "save-strip-after");

            Rig w;
            userModified(w);
            w.host->tick(240, kDt);
            openBrowser(w);
            const funkgui::Rect c = bounds(w, browserId(PB::kSaveLocal));
            w.host->move(c.centreX(), c.centreY());
            w.host->tick(20, kDt);
            now(w);
            shoot(w, "save-browser-before");
            w.click(c);
            w.host->tick(20, kDt);
            now(w);
            shoot(w, "save-browser-after");
            P.eq("save.writes_none", b(r.facade.writes().empty() && w.facade.writes().empty()
                                       && r.presets().overwrites() == 1 && w.presets().overwrites() == 1), 1);
        }
        P.eq("draw.glyphs_missing", missing, 0);
        P.eq("draw.inside", b(inside), 1);
    }

    // ---- keyboard focus and what the overlay covers (S13 H1a) -----------------------------------------------------------

    // Every FOCUS_RING primitive of the frame lies in one ring around `want` (FocusRing: want.reduced(1), AA apron).
    bool oneRingAround(Rig& r, const funkgui::Rect& want)
    {
        int n = 0;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const funkgui::Prim& p : r.host->draw().prims)
            if (p.tag == funkgui::tags::focusRing)
            {
                ++n;
                x0 = std::min(x0, p.x0);
                y0 = std::min(y0, p.y0);
                x1 = std::max(x1, p.x1);
                y1 = std::max(y1, p.y1);
            }
        const funkgui::Rect w = want.reduced(1.0f);
        const auto near = [](float a, float c) { return std::fabs(a - c) <= 2.5f; };
        const bool ok = n > 0 && near(x0, w.x) && near(y0, w.y) && near(x1, w.right()) && near(y1, w.bottom());
        if (!ok)
            std::printf("NOTE     rings: %d prims over {%g,%g}-{%g,%g}, want one around {%g,%g}-{%g,%g}\n", n,
                        static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(x1),
                        static_cast<double>(y1), static_cast<double>(w.x), static_cast<double>(w.y),
                        static_cast<double>(w.right()), static_cast<double>(w.bottom()));
        return ok;
    }

    // Opens the browser from the keyboard: the strip's name focused (its ring shown), then Return.
    void openByKeys(Rig& r)
    {
        r.a11y(stripId(PS::kNameLocal), funkgui::A11yAction::focus);
        r.keys("return");
        r.settle();
    }

    // The stops the browser should offer, in order: the filters, the rows shown (top to bottom), the enabled buttons of
    // the bottom bar (left to right).
    std::vector<uint32_t> expectedStops(const Rig& r)
    {
        std::vector<uint32_t> stops { browserId(PB::kFiltersLocal) };
        for (const funkgui::A11yItem& it : rows(r))
            stops.push_back(it.id);
        std::vector<funkgui::A11yItem> cells;
        for (const funkgui::A11yItem& it : r.items())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::presetBrowser)
                && it.role == funkgui::A11yRole::button && it.visible && it.enabled
                && it.bounds.y >= L::kOverlay.y + 200.0f)
                cells.push_back(it);
        std::stable_sort(cells.begin(), cells.end(),
                         [](const funkgui::A11yItem& a, const funkgui::A11yItem& c) { return a.bounds.x < c.bounds.x; });
        for (const funkgui::A11yItem& it : cells)
            stops.push_back(it.id);
        return stops;
    }

    void focus(Probe& P)
    {
        {   // opened from the keyboard: the focus comes onto the selection (the current row), one ring in the frame
            Rig r;
            openByKeys(r);
            P.eq("focus.open_from_keys", b(r.open() && r.presets().applies() == 0 && r.ctx().focus == rowId(1)
                                           && r.ctx().focusVisible && oneRingAround(r, bounds(r, rowId(1)))), 1);

            // Tab walks the browser only: the filters, the rows shown, the available cells; it wraps; nothing applies
            const std::vector<uint32_t> want = expectedStops(r);
            const auto at = std::find(want.begin(), want.end(), rowId(1));
            bool trap = at != want.end() && want.size() >= 1 + 11 + 4;
            std::size_t i = trap ? static_cast<std::size_t>(at - want.begin()) : 0;
            bool rings = trap;
            for (std::size_t k = 0; trap && k < want.size(); ++k)
            {
                i = (i + 1) % want.size();
                r.keys("tab");
                trap = trap && r.ctx().focus == want[i] && r.open();
                const uint32_t loc = want[i] & 0xFFFFu;
                if (loc != PB::kFiltersLocal)                     // the filters' ring is the chosen filter's
                    rings = rings && oneRingAround(r, bounds(r, want[i]));
                else
                    rings = rings && oneRingAround(r, bounds(r, browserId(PB::kFilterLocal0)));   // ALL
            }
            r.keys("shift+tab");
            trap = trap && r.ctx().focus == want[(i + want.size() - 1) % want.size()];
            P.eq("focus.tab_trap", b(trap), 1);
            P.eq("focus.tab_rings", b(rings), 1);
            P.eq("focus.tab_applies_nothing", b(r.presets().applies() == 0 && r.facade.writes().empty()), 1);
        }
        {   // Tab onto a row selects it without applying; Return then loads that row and closes
            Rig r;
            openByKeys(r);
            r.keys("tab");                                        // row 1 -> row 2
            const bool selected = r.ctx().focus == rowId(2) && r.presets().applies() == 0;
            r.keys("return");
            r.settle();
            P.eq("focus.tab_selects", b(selected && r.presets().applies() == 1 && r.presets().current() == 2
                                        && !r.open()), 1);
        }
        {   // the arrows on a focused row: one apply each, and the focus moves with the selection
            Rig r;
            openByKeys(r);
            r.keys("down");
            P.eq("focus.arrows_follow", b(r.presets().applies() == 1 && r.presets().current() == 2
                                          && r.ctx().focus == rowId(2) && oneRingAround(r, bounds(r, rowId(2)))), 1);
        }
        {   // the filters: the arrows choose the filter, nothing applies, the ring is on the chosen filter
            Rig r;
            openByKeys(r);
            r.keys("shift+tab");                                  // row 1 -> row 0
            r.keys("shift+tab");                                  // -> the filters
            const bool onFilters = r.ctx().focus == browserId(PB::kFiltersLocal);
            r.keys("right");
            P.eq("focus.filters_keys", b(onFilters && value(r, browserId(PB::kFiltersLocal)) == "FACTORY"
                                         && r.presets().applies() == 0 && r.ctx().focus == browserId(PB::kFiltersLocal)
                                         && oneRingAround(r, bounds(r, browserId(PB::kFilterLocal0 + 1)))), 1);
        }
        {   // an action cell: Return presses it (SAVE AS: the edit starts, the focus goes to its commit cell); Esc
            // cancels the edit, keeps the browser open, and the focus comes back to the selection
            Rig r;
            openByKeys(r);
            uint32_t lastRow = rowId(1);                          // Tab selects each row it passes (the last one stays)
            for (int k = 0; k < 40 && r.ctx().focus != browserId(PB::kSaveAsLocal); ++k)
            {
                r.keys("tab");
                if ((r.ctx().focus & 0xFFFFu) >= PB::kRowLocal0)
                    lastRow = r.ctx().focus;
            }
            const bool onCell = r.ctx().focus == browserId(PB::kSaveAsLocal);
            r.keys("return");
            const bool edit = editing(r) && r.ctx().focus == browserId(PB::kCommitLocal)
                           && oneRingAround(r, bounds(r, browserId(PB::kCommitLocal)));
            r.keys("escape");
            P.eq("focus.cell_return", b(onCell && edit), 1);
            P.eq("focus.edit_escape", b(!editing(r) && r.open() && r.ctx().focus == lastRow
                                        && r.presets().saves() == 0 && r.presets().applies() == 0), 1);
        }
        {   // Esc closes, and the focus goes back to the strip's name with its ring
            Rig r;
            openByKeys(r);
            r.keys("down");
            r.keys("escape");
            r.settle();
            P.eq("focus.returns_to_strip", b(!r.open() && r.ctx().focus == stripId(PS::kNameLocal)
                                             && r.ctx().focusVisible), 1);
        }
        {   // with the browser open, what it covers is not visible in a11y; the slot rows (below it) are
            Rig r;
            openBrowser(r);
            int covered = 0, slots = 0;
            for (const funkgui::A11yItem& it : r.items())
            {
                if (!it.visible)
                    continue;
                const int v = ui::viewIndexOf(it.id);
                if (v != static_cast<int>(ui::ViewIndex::presetBrowser)
                    && kGround.contains({ it.bounds.centreX(), it.bounds.centreY() }))
                {
                    ++covered;
                    std::printf("NOTE     covered but visible: %s\n", funkgui::a11yDumpLine(it).c_str());
                }
                if (v == static_cast<int>(ui::ViewIndex::slotGrid) && (it.id & 0xFFFFu) >= 1 && (it.id & 0xFFFFu) <= 21)
                    ++slots;
            }
            P.eq("focus.a11y_covered_hidden", covered, 0);
            P.eq("focus.a11y_slots_visible", slots, 21);
        }
    }

    // `-- --png-dir <dir>` (after the lone "--": the probe's own flags); "" when absent.
    std::string pngDir()
    {
        const int argc = fcmp::probe::argc();
        char** argv = fcmp::probe::argv();
        bool own = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a = argv[i] != nullptr ? argv[i] : "";
            if (a == "--")
                own = true;
            else if (own && a == "--png-dir" && i + 1 < argc && argv[i + 1] != nullptr)
                return argv[i + 1];
        }
        return {};
    }
}

FCMP_PROBE(ui, presets)
{
    const funkgui::HeadlessGuiScope gui;                          // what FontService needs before it bakes the atlas
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    strip(P);
    browser(P);
    saveAs(P);
    saveOver(P);
    rename(P);
    remove(P);
    import(P);
    exportAndMenus(P);
    a11yAndDraw(P, pngDir());
    focus(P);
    clipSnap(P);
    stripMenu(P);
    browserMenus(P);
    categoryMenu(P);
    choosers(P);
    viewsGone(P);
    noChooser(P);
    chooserHands(P);
    return P.finish();
}
