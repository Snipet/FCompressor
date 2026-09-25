// FCMP_PROBE layer=ui name=browsers scope=global timeout=300
//
// ui.browsers (03 §3.6; 02 §8.6; U5): the Mode browser lists every registered Mode once, in Group then slot order.
// Spec-only (K3 #17, ADR-52): adding a Mode re-blesses nothing global; each Mode's own browser row is fingerprinted in
// its ui.geometry.<key> instead.
//
//   registry.*   over a real Panel (FakeFacade, the modebrowser view, dpi 2): one visible listItem per registered Mode
//                (retired slots never appear, and nothing else is listed), titled with its name, read column by column
//                in the global order (Group, then slot), each in the column headed by its Group, at the row rectangle
//                of 02 §8.6 ({40 + 110·k, 92 + 20·r − 4, 106, 20}); the headings name the eight groups in enum order
//                with their counts ("FET  1"); one page, so no pager items.
//   draw.*       every BROWSER_* primitive lies inside the browser's ground, which covers the overlay {40,64,880,286};
//                one BROWSER_CURRENT bar; eight headings; no BROWSER_PAGER; each name inside its column; no missing
//                glyph.
//   grid.*       ModeGrid (ModeBrowser.h) on synthetic Mode lists, which eight registered Modes cannot reach: the sort,
//                96 Modes in 8 × 12 without a change (the capacity of 02 §8.6), a 13th VCA wrapping under "VCA (2)",
//                paging by 8 columns, 128 Modes, the item cap, an empty registry; the hit test (every row, the 4 px
//                gutter, empty cells, other pages); the navigation (↑↓ stop at a column's ends, ←→ skip empty columns
//                and cross a page edge, Home / End, PageUp / PageDown, from nothing) and the type-ahead search.
//   view.*       a ModeBrowser over a synthetic two-page grid (13 VCA + one per other group; its own PanelContext with
//                the overlay open), through the SubView API: page 1 shows all but OTHER with "1 of 2" and ‹ disabled;
//                ←/→ cross the page edge both ways and bump a11yRevision; PageDown / PageUp; the wheel pages one notch
//                at a time (smooth deltas in notches, clamped); the pager (‹ refused on page 1, › pages, a11y buttons,
//                the cursor) writes nothing; Return commits the page-2 highlight with one tap of `mode`.
//
// Review pictures (not a test): `fcmp_probe_plugin ui.browsers … -- --png-dir <dir>` writes browsers-paged-<n>.png, the
// synthetic browser on page 1 then page 2 (the overlay alone, dpi 2, theme 0).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/SubView.h"
#include "editor/Tags.h"
#include "editor/views/ModeBrowser.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/SoftRaster.h>
#include <funkgui/core/Theme.h>
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
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    namespace B = fcmp::ui::layout::browser;
    using fcmp::probe::FakeFacade;
    using fcdsp::Group;
    using Item = ui::ModeGrid::Item;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    constexpr std::array<const char*, 8> kGroupNames { "VCA", "FET", "OPTO", "VARI-MU", "DIODE", "MODERN", "LIMIT",
                                                       "OTHER" };

    int b(bool v) { return v ? 1 : 0; }

    bool sameRect(const funkgui::Rect& a, const funkgui::Rect& c)
    {
        return a.x == c.x && a.y == c.y && a.w == c.w && a.h == c.h;
    }

    funkgui::Point centre(const funkgui::Rect& r) { return { r.centreX(), r.centreY() }; }

    // The registered Modes in the global order (02 §8.5): Group, then slot.
    std::vector<const fcdsp::ModeSlot*> globalOrder()
    {
        std::vector<const fcdsp::ModeSlot*> v;
        for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
            if (s.entry != nullptr && s.entry->desc != nullptr)
                v.push_back(&s);
        std::stable_sort(v.begin(), v.end(), [](const fcdsp::ModeSlot* a, const fcdsp::ModeSlot* c) {
            const auto ga = static_cast<int>(a->entry->desc->group), gc = static_cast<int>(c->entry->desc->group);
            return ga != gc ? ga < gc : a->slot < c->slot;
        });
        return v;
    }

    // ---- the real browser -----------------------------------------------------------------------------------------------

    void registry(Probe& P)
    {
        const std::vector<const fcdsp::ModeSlot*> order = globalOrder();
        fcmp::probe::FakeFacade facade;
        ui::Panel panel(facade, kProbeOptions);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        const ui::ViewSpec* view = ui::findView("modebrowser");
        if (view == nullptr)
        {
            P.harnessError("ui.browsers: no modebrowser view");
            return;
        }
        panel.setView(*view, true);
        P.le("registry.settle", host.settle(kMaxSettle, kDt), kMaxSettle);
        P.eq("registry.open", b(panel.overlay() == ui::Overlay::modeBrowser), 1);

        // The browser's visible items.
        std::vector<funkgui::A11yItem> rows, headings;
        int others = 0;
        for (const funkgui::A11yItem& it : host.accessibility())
        {
            if (ui::viewIndexOf(it.id) != static_cast<int>(ui::ViewIndex::modeBrowser) || !it.visible)
                continue;
            if (it.role == funkgui::A11yRole::listItem)
                rows.push_back(it);
            else if (it.role == funkgui::A11yRole::staticText)
                headings.push_back(it);
            else
                ++others;
        }
        P.eq("registry.count", static_cast<int64_t>(rows.size()), static_cast<int64_t>(order.size()));
        int once = 0;
        for (const fcdsp::ModeSlot* s : order)
        {
            int n = 0;
            for (const funkgui::A11yItem& it : rows)
                n += it.title == std::string(s->entry->desc->name) ? 1 : 0;
            once += n == 1 ? 1 : 0;
            if (n != 1)
                std::printf("NOTE     %.*s is listed %d times\n", static_cast<int>(s->key.size()), s->key.data(), n);
        }
        P.eq("registry.each_once", once, static_cast<int64_t>(order.size()));

        // Column by column (x, then y): the global order.
        std::vector<funkgui::A11yItem> read = rows;
        std::stable_sort(read.begin(), read.end(), [](const funkgui::A11yItem& a, const funkgui::A11yItem& c) {
            return a.bounds.x != c.bounds.x ? a.bounds.x < c.bounds.x : a.bounds.y < c.bounds.y;
        });
        bool inOrder = read.size() == order.size();
        for (std::size_t i = 0; inOrder && i < read.size(); ++i)
            inOrder = read[i].title == std::string(order[i]->entry->desc->name);
        P.eq("registry.global_order", b(inOrder), 1);

        // Each row in its Group's column, at the row rectangle of 02 §8.6; the ids name the slot.
        bool inColumn = true, atRow = true, ids = true;
        for (const fcdsp::ModeSlot* s : order)
            for (const funkgui::A11yItem& it : rows)
                if (it.title == std::string(s->entry->desc->name))
                {
                    const int k = static_cast<int>((it.bounds.x - B::kColumnX0) / B::kColumnW);
                    const int r = static_cast<int>((it.bounds.y - B::kRowY0 - B::kRowHitDy) / B::kRowPitch);
                    inColumn = inColumn && k == static_cast<int>(s->entry->desc->group);
                    atRow = atRow && k >= 0 && k < B::kColumns && r >= 0 && r < B::kRows
                         && sameRect(it.bounds, B::rowHit(k, r));
                    ids = ids && it.id == ui::a11yId(ui::ViewIndex::modeBrowser, 0x100u + s->slot);
                }
        P.eq("registry.group_column", b(inColumn), 1);
        P.eq("registry.row_rect", b(atRow), 1);
        P.eq("registry.row_ids", b(ids), 1);

        // Retired slots never appear.
        int retiredListed = 0;
        for (const fcdsp::Retired& r : fcdsp::retired())
            for (const funkgui::A11yItem& it : rows)
                retiredListed += it.id == ui::a11yId(ui::ViewIndex::modeBrowser, 0x100u + r.slot) ? 1 : 0;
        P.eq("registry.no_retired", retiredListed, 0);

        // The headings: the eight groups in enum order, with their counts; no pager (one page).
        std::array<int, 8> counts{};
        for (const fcdsp::ModeSlot* s : order)
            ++counts[static_cast<std::size_t>(s->entry->desc->group)];
        std::stable_sort(headings.begin(), headings.end(),
                         [](const funkgui::A11yItem& a, const funkgui::A11yItem& c) { return a.bounds.x < c.bounds.x; });
        bool named = headings.size() == 8;
        for (std::size_t g = 0; named && g < 8; ++g)
            named = headings[g].title == std::string(kGroupNames[g]) + "  " + std::to_string(counts[g])
                 && headings[g].bounds.x == B::columnX(static_cast<int>(g));
        P.eq("registry.headings", b(named), 1);
        P.eq("registry.no_pager", others, 0);

        // ---- drawing ----
        const funkgui::PrimList& pl = host.draw();
        const funkgui::Rect& o = L::kOverlay;
        float gx0 = 1e9f, gy0 = 1e9f, gx1 = -1e9f, gy1 = -1e9f, bestArea = 0.0f;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == ui::tag::browserBg && (p.x1 - p.x0) * (p.y1 - p.y0) > bestArea)
            {
                bestArea = (p.x1 - p.x0) * (p.y1 - p.y0);
                gx0 = p.x0;
                gy0 = p.y0;
                gx1 = p.x1;
                gy1 = p.y1;
            }
        P.eq("draw.ground_covers_overlay", b(gx0 <= o.x && gy0 <= o.y && gx1 >= o.right() && gy1 >= o.bottom()), 1);
        int outside = 0, bars = 0, pager = 0, headingTexts = 0, rowTexts = 0, rowOutsideColumn = 0;
        for (const funkgui::Prim& p : pl.prims)
        {
            if (p.tag < ui::tag::browserBg || p.tag > ui::tag::browserPager)
                continue;
            outside += p.x0 < gx0 || p.y0 < gy0 || p.x1 > gx1 || p.y1 > gy1 ? 1 : 0;
            bars += p.tag == ui::tag::browserCurrent ? 1 : 0;
            pager += p.tag == ui::tag::browserPager ? 1 : 0;
            headingTexts += p.tag == ui::tag::browserHeading ? 1 : 0;
            if (p.tag == ui::tag::browserRow)
            {
                ++rowTexts;
                const int k = static_cast<int>((0.5f * (p.x0 + p.x1) - B::kColumnX0) / B::kColumnW);
                const float left = B::columnX(k) - 2.0f;
                const float right = B::columnX(k) + B::kColumnW - 12.0f + 2.0f;
                rowOutsideColumn += p.x0 < left || p.x1 > right ? 1 : 0;
            }
        }
        P.eq("draw.inside_ground", outside, 0);
        P.eq("draw.current_bar", bars, 1);
        P.eq("draw.no_pager", pager, 0);
        P.eq("draw.headings_drawn", b(headingTexts > 0), 1);
        P.eq("draw.rows_drawn", b(rowTexts > 0), 1);
        P.eq("draw.rows_in_column", rowOutsideColumn, 0);
        P.eq("draw.glyphs_missing", static_cast<int64_t>(pl.missingGlyphs), 0);

        // ModeGrid::registered() is what the browser shows.
        const ui::ModeGrid g = ui::ModeGrid::registered();
        bool same = g.size() == static_cast<int>(order.size()) && g.columns() == 8 && g.pages() == 1;
        for (int i = 0; same && i < g.size(); ++i)
            same = g.slot(i) == order[static_cast<std::size_t>(i)]->slot
                && std::string_view(g.name(i)) == order[static_cast<std::size_t>(i)]->entry->desc->name;
        P.eq("registry.grid", b(same), 1);
    }

    // ---- ModeGrid on synthetic lists ---------------------------------------------------------------------------------

    // Names "<GROUP> <slot>" kept alive for the grids (ModeGrid copies names; the spec views must outlive it).
    struct Synthetic
    {
        std::vector<std::string> names;
        std::vector<Item> items;

        void add(Group g, int slot)
        {
            const auto k = std::min<std::size_t>(static_cast<std::size_t>(g), kGroupNames.size() - 1);
            names.push_back(std::string(kGroupNames[k]) + " " + std::to_string(slot));
            items.push_back({ static_cast<uint8_t>(slot), g, {}, "SPEC" });
        }
        ui::ModeGrid grid()
        {
            for (std::size_t i = 0; i < items.size(); ++i)
                items[i].name = names[i];
            return ui::ModeGrid(items);
        }
    };

    bool columnIs(const ui::ModeGrid& g, int k, Group group, int part, int count)
    {
        if (k < 0 || k >= g.columns())
            return false;
        const ui::ModeGrid::Column& c = g.column(k);
        return c.group == group && c.part == part && c.count == count;
    }

    std::string heading(const ui::ModeGrid& g, int k)
    {
        char buf[64];
        g.heading(k, buf, sizeof buf);
        return buf;
    }

    void grids(Probe& P)
    {
        {   // the sort: (group, slot), whatever the input order; a value past `other` counts as `other`
            Synthetic s;
            s.add(Group::fet, 9);
            s.add(Group::vca, 5);
            s.add(Group::vca, 2);
            s.add(Group::limit, 1);
            s.add(static_cast<Group>(200), 3);
            const ui::ModeGrid g = s.grid();
            P.eq("grid.sort", b(g.size() == 5 && g.slot(0) == 2 && g.slot(1) == 5 && g.slot(2) == 9 && g.slot(3) == 1
                                && g.slot(4) == 3 && g.group(4) == Group::other
                                && std::string_view(g.name(0)) == "VCA 2"), 1);
            P.eq("grid.sort.columns", b(g.columns() == 8 && columnIs(g, 0, Group::vca, 0, 2)
                                        && columnIs(g, 1, Group::fet, 0, 1) && columnIs(g, 2, Group::opto, 0, 0)
                                        && columnIs(g, 6, Group::limit, 0, 1) && columnIs(g, 7, Group::other, 0, 1)), 1);
            P.eq("grid.find", b(g.find(9) == 2 && g.find(4) == -1), 1);
        }
        {   // capacity: 96 Modes, 12 per group, fill 8 × 12 with one page
            Synthetic s;
            for (int grp = 0; grp < 8; ++grp)
                for (int r = 0; r < 12; ++r)
                    s.add(static_cast<Group>(grp), grp * 12 + r);
            const ui::ModeGrid g = s.grid();
            bool ok = g.size() == 96 && g.columns() == 8 && g.pages() == 1;
            for (int k = 0; ok && k < 8; ++k)
                ok = columnIs(g, k, static_cast<Group>(k), 0, 12);
            P.eq("grid.capacity_96", b(ok), 1);
            P.eq("grid.capacity_96.last_row", b(g.rowOf(95) == 11 && g.columnOf(95) == 7
                                                && sameRect(g.rowRect(95), B::rowHit(7, 11))), 1);
            P.eq("grid.heading", b(heading(g, 3) == "VARI-MU  12"), 1);
        }
        Synthetic wrap;                                           // 13 VCA + one per other group: 9 columns, 2 pages
        for (int i = 0; i < 13; ++i)
            wrap.add(Group::vca, i);
        for (int grp = 1; grp < 8; ++grp)
            wrap.add(static_cast<Group>(grp), 20 + grp);
        const ui::ModeGrid w = wrap.grid();
        {
            P.eq("grid.wrap.columns", b(w.columns() == 9 && columnIs(w, 0, Group::vca, 0, 12)
                                        && columnIs(w, 1, Group::vca, 1, 1) && columnIs(w, 2, Group::fet, 0, 1)
                                        && columnIs(w, 8, Group::other, 0, 1)), 1);
            P.eq("grid.wrap.headings", b(heading(w, 0) == "VCA  13" && heading(w, 1) == "VCA (2)"
                                         && heading(w, 2) == "FET  1"), 1);
            P.eq("grid.wrap.pages", b(w.pages() == 2 && ui::ModeGrid::pageOf(7) == 0 && ui::ModeGrid::pageOf(8) == 1
                                      && w.pageOfItem(w.size() - 1) == 1), 1);
            P.eq("grid.wrap.page2_rect", b(sameRect(w.rowRect(w.size() - 1), B::rowHit(0, 0))), 1);
        }
        {   // 128 Modes in one group: 11 VCA columns + 7 empty groups = 18 columns, 3 pages; past 128 dropped
            Synthetic s;
            for (int i = 0; i < 130; ++i)
                s.add(Group::vca, i);
            const ui::ModeGrid g = s.grid();
            P.eq("grid.full", b(g.size() == 128 && g.columns() == 18 && g.pages() == 3 && columnIs(g, 10, Group::vca, 10, 8)
                                && columnIs(g, 11, Group::fet, 0, 0) && heading(g, 10) == "VCA (11)"
                                && g.columnOf(127) == 10 && g.rowOf(127) == 7), 1);
        }
        {   // nothing registered: the eight empty columns, no item anywhere
            const ui::ModeGrid g{ std::span<const Item>() };
            P.eq("grid.empty", b(g.size() == 0 && g.columns() == 8 && g.pages() == 1
                                 && g.step(-1, funkgui::Key::down) == -1 && g.hit(0, centre(B::rowHit(0, 0))) == -1
                                 && g.typeAhead(0, "A") == -1 && g.onPage(-1, 0) == -1 && heading(g, 7) == "OTHER  0"),
                 1);
        }

        {   // the hit test: every row on its page; the gutter, an empty cell and the other page miss
            bool every = true;
            for (int i = 0; i < w.size(); ++i)
                every = every && w.hit(w.pageOfItem(i), centre(w.rowRect(i))) == i;
            P.eq("grid.hit.rows", b(every), 1);
            const funkgui::Rect r0 = B::rowHit(0, 0);
            P.eq("grid.hit.gutter", w.hit(0, { r0.right() + 1.0f, r0.centreY() }), -1);
            P.eq("grid.hit.edges", b(w.hit(0, { r0.x, r0.y }) == 0 && w.hit(0, { r0.right(), r0.centreY() }) == -1
                                     && w.hit(0, { r0.x - 0.5f, r0.centreY() }) == -1), 1);
            P.eq("grid.hit.empty_cell", w.hit(0, centre(B::rowHit(2, 1))), -1);   // FET has one row
            P.eq("grid.hit.other_page", b(w.hit(1, centre(B::rowHit(1, 0))) == -1
                                          && w.hit(1, centre(B::rowHit(0, 0))) == w.size() - 1), 1);
            P.eq("grid.hit.outside", b(w.hit(0, { 30.0f, 100.0f }) == -1 && w.hit(0, { 925.0f, 100.0f }) == -1
                                       && w.hit(0, { 100.0f, 80.0f }) == -1), 1);
        }

        {   // navigation
            using funkgui::Key;
            const int vca5 = 5, vca11 = 11, vca12 = 12;           // column 0 rows 5 and 11; column 1 row 0
            const int fet = w.itemAt(2, 0), limit = w.itemAt(7, 0), other = w.itemAt(8, 0);
            P.eq("grid.nav.up_down", b(w.step(vca5, Key::up) == 4 && w.step(vca5, Key::down) == 6
                                       && w.step(0, Key::up) == 0 && w.step(vca11, Key::down) == vca11
                                       && w.step(vca12, Key::down) == vca12), 1);
            P.eq("grid.nav.across", b(w.step(vca5, Key::right) == vca12 && w.step(vca12, Key::right) == fet
                                      && w.step(fet, Key::left) == vca12 && w.step(vca12, Key::left) == 0
                                      && w.step(vca5, Key::left) == vca5), 1);
            P.eq("grid.nav.page_edge", b(w.step(limit, Key::right) == other && w.step(other, Key::left) == limit
                                         && w.step(other, Key::right) == other), 1);
            P.eq("grid.nav.home_end", b(w.step(vca5, Key::home) == 0 && w.step(vca5, Key::end) == w.size() - 1), 1);
            P.eq("grid.nav.pages", b(w.step(vca5, Key::pageDown) == other && w.step(other, Key::pageUp) == 0
                                     && w.step(other, Key::pageDown) == other && w.step(vca5, Key::pageUp) == vca5),
                 1);
            P.eq("grid.nav.from_none", b(w.step(-1, Key::down) == 0 && w.step(-1, Key::end) == w.size() - 1
                                         && w.step(-1, Key::enter) == -1), 1);
            P.eq("grid.nav.other_keys", b(w.step(vca5, Key::enter) == vca5 && w.step(vca5, Key::character) == vca5), 1);

            Synthetic gaps;                                       // FET and OPTO empty: → from VCA reaches VARI-MU
            gaps.add(Group::vca, 1);
            gaps.add(Group::vca, 2);
            gaps.add(Group::varimu, 3);
            const ui::ModeGrid g = gaps.grid();
            P.eq("grid.nav.skip_empty", b(g.step(1, Key::right) == 2 && g.step(2, Key::left) == 0
                                          && g.step(2, Key::right) == 2), 1);
            P.eq("grid.onpage", b(w.onPage(vca5, 1) == other && w.onPage(other, 0) == 0 && w.onPage(vca5, 2) == -1
                                  && g.onPage(2, 0) == 2), 1);
        }

        {   // type-ahead: prefix, ASCII case-insensitive, from `from` inclusive, wrapping
            Synthetic s;
            s.add(Group::vca, 1);                                 // "VCA 1"
            s.add(Group::fet, 2);                                 // "FET 2"
            s.add(Group::limit, 3);                               // "LIMIT 3"
            const ui::ModeGrid g = s.grid();
            P.eq("grid.typeahead", b(g.typeAhead(0, "f") == 1 && g.typeAhead(1, "FET") == 1 && g.typeAhead(2, "v") == 0
                                     && g.typeAhead(0, "LIMIT 3") == 2 && g.typeAhead(0, "LIMIT 4") == -1
                                     && g.typeAhead(0, "") == -1 && g.typeAhead(-1, "VCA") == 0), 1);
        }
    }

    // ---- the view over a synthetic two-page grid ----------------------------------------------------------------------

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

    // A ModeBrowser over 13 VCA Modes (slot 0, the current Mode, first) and one per other group: 9 columns, OTHER alone
    // on page 2. It runs on a PanelContext of its own (overlay open, the Panel's frame, a GestureController over the
    // HeadlessHost), driven through the SubView API.
    void pagedView(Probe& P)
    {
        FakeFacade facade;
        ui::Panel panel(facade, kProbeOptions);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        host.settle(kMaxSettle, kDt);
        facade.resetCounts();
        const ui::PanelOptions options = kProbeOptions;
        ui::HistoryStore history;
        ui::PreviewWorker worker(true);
        ui::PanelContext ctx(panel, facade, options, funkgui::FontService::get().atlas(), history, worker);
        funkgui::GestureController gestures(host);
        ctx.host = &host;
        ctx.gestures = &gestures;
        ctx.frame = panel.context().frame;
        ctx.overlay = ui::Overlay::modeBrowser;

        Synthetic s;
        for (int i = 0; i < 13; ++i)
            s.add(Group::vca, i);
        for (int grp = 1; grp < 8; ++grp)
            s.add(static_cast<Group>(grp), 20 + grp);
        const ui::ModeGrid g = s.grid();
        ui::ModeBrowser mb(ctx, g);

        const auto tick = [&] {
            ctx.seconds += static_cast<double>(kDt);
            ctx.handNext = ui::HandState{};
            mb.tick(kDt);
            ctx.hand = ctx.handNext;
        };
        const auto items = [&] {
            std::vector<funkgui::A11yItem> v;
            mb.accessibility(v);
            return v;
        };
        const auto shownRows = [&] {
            int n = 0;
            for (const funkgui::A11yItem& it : items())
                n += it.role == funkgui::A11yRole::listItem && it.visible ? 1 : 0;
            return n;
        };
        const auto shown = [&](int i) {
            for (const funkgui::A11yItem& it : items())
                if (it.id == ui::a11yId(ui::ViewIndex::modeBrowser, 0x100u + g.slot(i)))
                    return it.visible;
            return false;
        };
        const auto pagerState = [&](std::string& text, bool& prev, bool& next) {
            text = "<none>";
            prev = next = false;
            for (const funkgui::A11yItem& it : items())
            {
                if (it.title == "Page")
                    text = it.value;
                else if (it.title == "Previous page")
                    prev = it.enabled;
                else if (it.title == "Next page")
                    next = it.enabled;
            }
        };
        const auto key = [&](funkgui::Key k, bool alt = false) {
            funkgui::KeyEvent e;
            e.key = k;
            e.mods.alt = alt;
            return mb.key(e);
        };
        const auto press = [&](funkgui::Rect r) {
            funkgui::PointerEvent e;
            e.x = r.centreX();
            e.y = r.centreY();
            mb.pointerMove(e);
            mb.pointerDown(e);
            mb.pointerUp(e);
        };
        const std::string pictures = pngDir();
        int shot = 0;
        const auto draw = [&](int& pagerPrims, uint32_t& missing) {
            funkgui::Canvas canvas(funkgui::FontService::get().atlas());
            funkgui::FrameInfo fi;
            fi.logicalW = L::kWidth;
            fi.logicalH = L::kHeight;
            fi.dpi = 2.0f;
            fi.clear = funkgui::Theme::byIndex(0).ground;
            canvas.begin(fi);
            mb.draw(canvas, funkgui::Theme::byIndex(0));
            const funkgui::PrimList& pl = canvas.end();
            pagerPrims = 0;
            for (const funkgui::Prim& p : pl.prims)
                pagerPrims += p.tag == ui::tag::browserPager ? 1 : 0;
            missing = pl.missingGlyphs;
            if (!pictures.empty())                                // review pictures, not a test
            {
                std::error_code ec;
                std::filesystem::create_directories(pictures, ec);
                const std::string path = pictures + "/browsers-paged-" + std::to_string(++shot) + ".png";
                if (funkgui::writePng(funkgui::rasterise(pl, funkgui::FontService::get().atlas(), 2), path.c_str()))
                    std::printf("PNG      %s\n", path.c_str());
            }
        };
        constexpr funkgui::Rect kPrev { 840.0f, 328.0f, 20.0f, 20.0f };
        constexpr funkgui::Rect kNext { 900.0f, 328.0f, 20.0f, 20.0f };
        const int other = g.itemAt(8, 0), limit = g.itemAt(7, 0);

        tick();
        std::string text;
        bool prev = false, next = false;
        pagerState(text, prev, next);
        int pagerPrims = 0;
        uint32_t missing = 0;
        draw(pagerPrims, missing);
        P.eq("view.page1", b(shownRows() == g.size() - 1 && !shown(other) && shown(0) && text == "1 of 2" && !prev && next),
             1);
        P.eq("view.pager_drawn", b(pagerPrims > 0 && missing == 0), 1);

        // Keys cross the page edge both ways; PageDown / PageUp page.
        bool edge = key(funkgui::Key::right) && shown(0);            // VCA 0 -> VCA (2)
        for (int i = 0; i < 6; ++i)
            edge = edge && key(funkgui::Key::right);                  // ... -> LIMIT
        edge = edge && shown(limit) && !shown(other);
        const uint32_t rev0 = mb.a11yRevision();
        edge = edge && key(funkgui::Key::right) && shown(other) && shownRows() == 1;
        const bool bumped = mb.a11yRevision() != rev0;
        edge = edge && key(funkgui::Key::left) && shown(limit) && !shown(other);
        P.eq("view.keys_cross_pages", b(edge), 1);
        P.eq("view.revision_bumps", b(bumped), 1);
        P.eq("view.page_keys", b(key(funkgui::Key::pageDown) && shown(other) && key(funkgui::Key::pageUp) && shown(0)
                                 && !shown(other)), 1);

        // The wheel: one notch per page (down = forward), smooth deltas in notches, clamped at the ends.
        funkgui::WheelEvent w;
        w.x = 480.0f;
        w.y = 200.0f;
        w.dy = -1.0f;
        const bool down = mb.wheel(w) && shown(other);
        w.dy = -1.0f;
        const bool clamp = mb.wheel(w) && shown(other);
        w.dy = 1.0f;
        const bool up = mb.wheel(w) && shown(0) && !shown(other);
        host.tick(40, kDt);                                           // a new burst
        w.smooth = true;
        w.dy = -0.05f;
        const bool half = mb.wheel(w) && !shown(other);
        const bool whole = mb.wheel(w) && shown(other);
        P.eq("view.wheel", b(down && clamp && up && half && whole), 1);
        w.smooth = false;
        w.dy = 1.0f;
        mb.wheel(w);

        // The pager: ‹ disabled on page 1, › pages, the a11y buttons press, the cursor.
        press(kPrev);
        const bool prevRefused = shown(0) && !shown(other);
        funkgui::PointerEvent over;
        over.x = kNext.centreX();
        over.y = kNext.centreY();
        mb.pointerMove(over);
        const bool hand = mb.cursor({ over.x, over.y }) == funkgui::Cursor::pointingHand
                       && mb.cursor({ kPrev.centreX(), kPrev.centreY() }) == funkgui::Cursor::normal;
        press(kNext);
        pagerState(text, prev, next);
        draw(pagerPrims, missing);
        const bool paged = shown(other) && shownRows() == 1 && text == "2 of 2" && prev && !next && pagerPrims > 0;
        mb.a11yAction(ui::a11yId(ui::ViewIndex::modeBrowser, 0x10), funkgui::A11yAction::press, 0.0);
        const bool a11yPrev = shown(0) && !shown(other);
        mb.a11yAction(ui::a11yId(ui::ViewIndex::modeBrowser, 0x12), funkgui::A11yAction::press, 0.0);
        P.eq("view.pager", b(prevRefused && hand && paged && a11yPrev && shown(other)), 1);
        P.eq("view.pager_writes_nothing", b(facade.writes().empty()), 1);

        // Return commits the highlight the page change moved onto page 2: one tap of `mode` to OTHER's slot.
        tick();
        key(funkgui::Key::enter);
        const auto writes = facade.writes();
        P.eq("view.commit_on_page2", b(writes.size() == 1 && writes[0].pid == fcdsp::Pid::mode && writes[0].inGesture
                                       && writes[0].batchDepth == 0
                                       && writes[0].value01 == fcdsp::toNorm(fcdsp::Pid::mode,
                                                                             static_cast<float>(g.slot(other)))), 1);
    }
}

FCMP_PROBE(ui, browsers)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    if (globalOrder().empty())
    {
        P.harnessError("ui.browsers: no Mode is registered");
        return P.finish();
    }
    registry(P);
    grids(P);
    pagedView(P);
    return P.finish();
}
