// FCMP_PROBE layer=ui name=presets scope=global timeout=300
//
// ui.presets (02 §6.2–§6.3, §8.9; S12 lead revisions 5 and 8; U6): the preset strip and the preset browser over a
// FakeFacade's PresetAccess (15 rows: 12 factory, 3 user; current = row 1), driven through the Panel API and
// HeadlessHost input (Panel{skipHint, syncPreview}, dpi 2, settled at 1/60 s; a fresh panel per scenario). Global and
// spec-only: the views read no Mode-specific data beyond the Mode names of the rows.
//
//   strip.*     the four a11y items (‹, the "Preset" comboBox with the current name, ›, SAVE) and their Tab stops right
//               after the Mode latch; ‹ and › are one PresetAccess::step(∓1) each (one apply, one batch); keys and a11y on
//               the focused stops; nothing to step without presets; redraw on revision(): idle frames re-read nothing,
//               a revision (modified, a new list) is shown on the next frame (the marker, ", modified", UNTITLED).
//   browser.*   a click on the name opens it without applying; the rows shown (11 of 15, in PresetAccess order, the
//               current one checked); a click on a row is exactly one apply of that index and keeps the browser open; a
//               click on the current, unmodified row applies nothing (modified: one apply); a double-click applies once
//               and closes; ↓ Home End apply as they move, Return closes without a second apply; filters (click, ← →,
//               a11y); the wheel scrolls the list; type-ahead selects without applying; Esc closes.
//   saveas.*    the strip's SAVE opens the browser with the name pre-filled and selected (the store's unique name
//               announced when it is taken); typing replaces it; Return is one saveAs(name, category) and the browser
//               closes (opened for the save), the strip showing the new preset; Esc cancels and keeps the browser open;
//               SAVE AS inside the browser keeps it open on the new row; an empty name is refused before the call; the
//               category follows a chosen category filter; Init starts empty; a click elsewhere cancels.
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
//               path); the context menu's items per row kind and background; run() of Load, Rename, Delete; headless,
//               IMPORT / EXPORT open no chooser and call nothing.
//   a11y.*      ids non-zero and unique with the browser open and editing; press / focus on a row; the filter radios.
//   draw.*      every BROWSER_* primitive inside the browser's ground, the strip's inside its rectangle, no missing
//               glyph in any state drawn here.
//   writes.none the views never write a parameter: every recall is PresetAccess's.
//
// Review pictures (not a test): `fcmp_probe_plugin ui.presets … -- --png-dir <dir>` writes presets-<state>.png (dpi 2,
// theme 0; one in theme 1).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/SubView.h"
#include "editor/Tags.h"
#include "editor/views/PresetBrowser.h"
#include "editor/views/PresetStrip.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/Input.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/FontService.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
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
        explicit Rig(int current = 1, std::vector<Row> rows = sampleRows(), int theme = 0) : facade("clean")
        {
            facade.fakePresets().setRows(std::move(rows));
            facade.fakePresets().setCurrent(current);
            panel = std::make_unique<ui::Panel>(facade, kProbeOptions);
            host = std::make_unique<funkgui::HeadlessHost>(*panel, theme, 2.0f);
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

            // Tab: the Mode latch, then ‹ name › SAVE (02 §8.9 items 1–2).
            std::vector<uint32_t> stops;
            for (int i = 0; i < 5; ++i)
            {
                r.host->keys("tab");
                stops.push_back(r.ctx().focus);
            }
            P.eq("strip.taborder", b(stops.size() == 5
                                     && ui::viewIndexOf(stops[0]) == static_cast<int>(ui::ViewIndex::header)
                                     && stops[1] == stripId(PS::kPrevLocal) && stops[2] == stripId(PS::kNameLocal)
                                     && stops[3] == stripId(PS::kNextLocal) && stops[4] == stripId(PS::kSaveLocal)), 1);
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
            r.keys("tab,tab");                                   // ‹
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
            // The wheel scrolls the list (3 rows a notch, clamped); type-ahead selects without applying; Esc closes.
            Rig r;
            openBrowser(r);
            r.host->wheel(500.0f, 200.0f, -1.0f);
            r.host->tick(1, kDt);
            const bool down3 = rows(r).front().title == "Drum Punch";
            r.host->wheel(500.0f, 200.0f, -1.0f);
            r.host->tick(1, kDt);
            const bool clamp = rows(r).front().title == "Master -1 dBTP" && rows(r).back().title == "Vox Chain";
            r.host->wheel(500.0f, 200.0f, 1.0f);
            r.host->wheel(500.0f, 200.0f, 1.0f);
            r.host->tick(1, kDt);
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
            const funkgui::A11yItem* row = rowNamed(rows(r), "Snare Room");
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

        // Headless: IMPORT and EXPORT open no chooser and call nothing.
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
        P.eq("draw.glyphs_missing", missing, 0);
        P.eq("draw.inside", b(inside), 1);
    }

    // `-- --png-dir <dir>` (after the lone "--": the probe's own flags); "" when absent.
    std::string pngDir()
    {
        const int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();
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
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    strip(P);
    browser(P);
    saveAs(P);
    rename(P);
    remove(P);
    import(P);
    exportAndMenus(P);
    a11yAndDraw(P, pngDir());
    return P.finish();
}
