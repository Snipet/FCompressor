// FCMP_PROBE layer=ui name=modebrowser scope=mode timeout=300
//
// ui.modebrowser.<key> (02 §8.4–§8.6, §8.9; K2 #4, #23; U5): the Mode browser with this Mode current, driven through the
// Panel API and HeadlessHost input over a FakeFacade (Panel{skipHint, syncPreview}, dpi 2, settled at 1/60 s; a fresh
// panel per scenario). Spec rows only. "The neighbour" is the Mode after this one in the global order (wrapping).
//
//   open.*       a click on the Mode name opens the browser and writes nothing; it settles.
//   row.*        this Mode's row: a visible listItem titled with its name, at ModeGrid's row rectangle inside the
//                overlay, checked (the only checked row), help = its spec line, description = its group.
//   draw.*       one BROWSER_CURRENT bar, 2×12 at (x − 6, row top) of this row; this row's name in ink100, the others in
//                ink52 (theme 0); every BROWSER_* primitive inside the browser's ground.
//   hover.*      the hovered row's spec line on the footer, its name in ink100, the pointing-hand cursor (normal off
//                the rows).
//   click.*      a click on the neighbour is one tap of `mode` and nothing else, never batched (02 §8.4.3, K2 #4); the
//                browser stays open and the current mark follows; a click back returns; a click on the current row
//                writes nothing.
//   alt.*        Alt-click = `mode`, then every live or stepped parameter whose Mode default differs, each in its own
//                gesture inside one facade batch and one host batch (02 §8.4.4, K2 #23), landing on modeDefaults();
//                Alt-click on the current row loads only its defaults (no `mode` write).
//   dblclick.*   a double-click switches once and closes; the second click never reaches the panel underneath.
//   cancel.*     dragging off a row, a popup click and the wheel write nothing and keep the browser open; Esc, a click
//                outside and a click on the Mode latch close it and write nothing.
//   keys.*       opened from the keyboard, the focus ring and the footer mark this row; ↑↓←→ Home End PageUp PageDown
//                move the highlight as ModeGrid::step says, writing nothing; Return commits the highlight (one tap)
//                and closes; Alt-Return commits with the defaults in one batch; type-ahead finds a Mode by name (a
//                space inside a pending buffer included) and its 1 s buffer expires.
//   a11y.*       press on a row = the click's commit (open stays); focus moves the highlight; eight group headings;
//                unique ids.
//   chars.*      on CHARACTERISTICS the browser opens over the same region, commits, and Esc returns to the screen.
//   focus.*      (S13 H1a) opened from the keyboard there is ONE focus ring in the whole frame, on this row (the Mode
//                latch underneath has given up the focus); while open, Tab and Shift-Tab walk the browser's rows only
//                (its focusOrder: the page's rows in column order), wrapping, with the highlight, footer and ring
//                following each stop and nothing written; Esc closes and gives the focus back to the Mode latch with
//                the ring shown; with the browser open, no item of another view whose centre lies under the browser's
//                ground is visible in a11y (the display row, the band), while the slot rows (outside it) stay visible.
//
// Review pictures (not a test): `fcmp_probe_plugin ui.modebrowser --mode <key> … -- --png-dir <dir>` writes
// modebrowser-<key>-{hover,keys,chars}.png (dpi 2, theme 0).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"
#include "editor/views/ModeBrowser.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/Input.h>
#include <funkgui/text/FontService.h>

#include <juce_gui_basics/juce_gui_basics.h>


#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    using fcmp::probe::FakeFacade;
    using fcdsp::Pid;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    constexpr std::array<const char*, 8> kGroupNames { "VCA", "FET", "OPTO", "VARI-MU", "DIODE", "MODERN", "LIMIT",
                                                       "OTHER" };
    constexpr funkgui::Point kEmptyInOverlay { 500.0f, 300.0f };   // column 4, row 10: no Mode there (≤ 12 per group)
    constexpr funkgui::Point kOutside { 480.0f, 470.0f };          // a slot row, below the overlay

    int b(bool v) { return v ? 1 : 0; }

    uint32_t pack(funkgui::Col c)
    {
        return static_cast<uint32_t>(c.r) | static_cast<uint32_t>(c.g) << 8 | static_cast<uint32_t>(c.b) << 16
             | static_cast<uint32_t>(c.a) << 24;
    }

    funkgui::Point centre(const funkgui::Rect& r) { return { r.centreX(), r.centreY() }; }

    bool sameRect(const funkgui::Rect& a, const funkgui::Rect& c)
    {
        return a.x == c.x && a.y == c.y && a.w == c.w && a.h == c.h;
    }

    bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

    struct Rig
    {
        explicit Rig(std::string_view key, int theme = 0)
            : facade(key), panel(facade, kProbeOptions), host(panel, theme, 2.0f)
        {
            settled = host.settle(kMaxSettle, kDt);
            facade.resetCounts();
        }

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        uint8_t slot() const { return facade.currentRaw().modeSlot; }
        int  settle() { return host.settle(kMaxSettle, kDt); }
        bool open() const { return panel.overlay() == ui::Overlay::modeBrowser; }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
        int                   settled = 0;
    };

    // ---- observation ----------------------------------------------------------------------------------------------------

    int beginsExcept(FakeFacade& f, Pid except)
    {
        int n = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            if (static_cast<Pid>(i) != except)
                n += f.fakePort(static_cast<Pid>(i)).begins();
        return n;
    }

    // Exactly one write, to `pid`, inside its gesture and outside any batch; no gesture on any other port.
    bool oneTap(FakeFacade& f, Pid pid)
    {
        const auto w = f.writes();
        const fcmp::probe::FakePort& p = f.fakePort(pid);
        return w.size() == 1 && w[0].pid == pid && w[0].inGesture && w[0].batchDepth == 0 && p.begins() == 1
            && p.ends() == 1 && !p.inGesture() && beginsExcept(f, pid) == 0 && f.batches() == 0;
    }

    bool nothingWritten(FakeFacade& f) { return f.writes().empty() && beginsExcept(f, fcdsp::kNoPid) == 0; }

    uint32_t rowId(uint8_t slot) { return ui::a11yId(ui::ViewIndex::modeBrowser, 0x100u + slot); }

    std::vector<funkgui::A11yItem> browserItems(const Rig& r, funkgui::A11yRole role)
    {
        std::vector<funkgui::A11yItem> out;
        for (const funkgui::A11yItem& it : r.host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::modeBrowser) && it.visible && it.role == role)
                out.push_back(it);
        return out;
    }

    const funkgui::A11yItem* byId(const std::vector<funkgui::A11yItem>& items, uint32_t id)
    {
        for (const funkgui::A11yItem& it : items)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    std::string footerLine(const Rig& r)
    {
        for (const funkgui::A11yItem& it : r.host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::footer)
                && it.role == funkgui::A11yRole::staticText && it.title == "Footer")
                return it.value;
        return "<no footer>";
    }

    // The browser's row whose rectangle holds the prim's centre; -1: none.
    int rowAt(const ui::ModeGrid& g, const funkgui::Prim& p)
    {
        return g.hit(0, { 0.5f * (p.x0 + p.x1), 0.5f * (p.y0 + p.y1) });
    }

    // The text inks of the rows (theme 0): `lit` rows in ink100, every other row in ink52.
    bool rowInks(Rig& r, const ui::ModeGrid& g, std::initializer_list<int> lit)
    {
        const funkgui::Theme th = funkgui::Theme::byIndex(0);
        const funkgui::PrimList& pl = r.host.draw();
        int texts = 0;
        bool ok = true;
        for (const funkgui::Prim& p : pl.prims)
        {
            if (p.tag != ui::tag::browserRow)
                continue;
            const int i = rowAt(g, p);
            ++texts;
            const bool isLit = std::find(lit.begin(), lit.end(), i) != lit.end();
            ok = ok && i >= 0 && p.c0 == pack(isLit ? th.ink100 : th.ink52);
        }
        return ok && texts > 0;
    }

    // The one BROWSER_CURRENT bar lies at (x − 6, row top) of item i, 2×12 (its quad's centre, within 0.5 px).
    bool barAt(Rig& r, const ui::ModeGrid& g, int i)
    {
        const funkgui::PrimList& pl = r.host.draw();
        int n = 0;
        bool at = false;
        const funkgui::Rect row = g.rowRect(i);
        const float cx = row.x + L::browser::kCurrentBarDx + 0.5f * L::browser::kCurrentBarW;
        const float cy = row.y - L::browser::kRowHitDy + 0.5f * L::browser::kCurrentBarH;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == ui::tag::browserCurrent)
            {
                ++n;
                at = std::fabs(0.5f * (p.x0 + p.x1) - cx) <= 0.5f && std::fabs(0.5f * (p.y0 + p.y1) - cy) <= 0.5f;
            }
        return n == 1 && at;
    }

    // The focus ring drawn inside the overlay surrounds item i's row (FocusRing: hit.reduced(1), 1 px hairlines).
    bool ringAt(Rig& r, const ui::ModeGrid& g, int i)
    {
        const funkgui::PrimList& pl = r.host.draw();
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        int n = 0;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == funkgui::tags::focusRing && L::kOverlay.contains({ 0.5f * (p.x0 + p.x1), 0.5f * (p.y0 + p.y1) }))
            {
                ++n;
                x0 = std::min(x0, p.x0);
                y0 = std::min(y0, p.y0);
                x1 = std::max(x1, p.x1);
                y1 = std::max(y1, p.y1);
            }
        const funkgui::Rect row = g.rowRect(i);                     // ModeBrowser.cpp's ring: x − 8 … x + 102
        const funkgui::Rect want = funkgui::Rect { row.x - 8.0f, row.y, row.w + 4.0f, row.h }.reduced(1.0f);
        const auto near = [](float a, float c) { return std::fabs(a - c) <= 2.5f; };   // hairline quads + AA apron
        const bool ok = n > 0 && near(x0, want.x) && near(y0, want.y) && near(x1, want.right())
                     && near(y1, want.bottom());
        if (!ok)
            std::printf("NOTE     focus ring: %d prims over {%g,%g}-{%g,%g}, want the row {%g,%g}-{%g,%g}\n", n,
                        static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(x1),
                        static_cast<double>(y1), static_cast<double>(want.x), static_cast<double>(want.y),
                        static_cast<double>(want.right()), static_cast<double>(want.bottom()));
        return ok;
    }

    int browserOutsideGround(Rig& r)
    {
        const funkgui::PrimList& pl = r.host.draw();
        const funkgui::Rect ground { L::kOverlay.x - 8.0f, L::kOverlay.y - 4.0f, L::kOverlay.w + 16.0f,
                                     L::kOverlay.h + 8.0f };
        int n = 0;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag >= ui::tag::browserBg && p.tag <= ui::tag::browserPager)
                n += p.x0 < ground.x - 1.5f || p.y0 < ground.y - 1.5f || p.x1 > ground.right() + 1.5f
                      || p.y1 > ground.bottom() + 1.5f ? 1 : 0;
        return n;
    }

    // ---- driving --------------------------------------------------------------------------------------------------------

    void openByClick(Rig& r)
    {
        const funkgui::Point name = centre(L::header::kModeName);
        r.host.click(name.x, name.y);
        r.settle();
    }

    bool openByKeys(Rig& r)
    {
        for (const funkgui::A11yItem& it : r.host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::header)
                && it.role == funkgui::A11yRole::comboBox)
            {
                r.panel.a11yAction(it.id, funkgui::A11yAction::focus);
                r.host.keys("return");
                r.settle();
                return r.open();
            }
        return false;
    }

    void clickItem(Rig& r, const ui::ModeGrid& g, int i, funkgui::Mods m = {})
    {
        const funkgui::Point p = centre(g.rowRect(i));
        r.host.click(p.x, p.y, m);
    }

    // The HeadlessHost key tokens that type `name` ("BUS 25" -> "B,U,S,space,2,5").
    std::string typing(std::string_view name)
    {
        std::string s;
        for (const char c : name)
        {
            if (!s.empty())
                s += ",";
            if (c == ' ')
                s += "space";
            else
                s += c;
        }
        return s;
    }

    // Alt-commit to item `to`: the writes of 02 §8.4.4 against modeDefaults() over what the facade held before.
    bool defaultsBatch(Rig& r, const ui::ModeGrid& g, int to, const fcdsp::RawParams& before, bool expectMode,
                       std::string& why)
    {
        const fcdsp::ModeEntry* e = fcdsp::bySlot(g.slot(to));
        if (e == nullptr)
        {
            why = "no entry";
            return false;
        }
        fcdsp::RawParams want = before;
        want.modeSlot = g.slot(to);
        fcdsp::modeDefaults(*e->desc, want);
        std::set<int> expected;
        if (expectMode)
            expected.insert(static_cast<int>(Pid::mode));
        for (std::size_t k = 0; k < fcdsp::kNumModeParams; ++k)
            if (!funkgui::ease::sameBits(want.v[k], before.v[k]))
                expected.insert(static_cast<int>(k));
        const auto w = r.facade.writes();
        std::set<int> got;
        bool inside = true;
        for (const fcmp::probe::FakeWrite& x : w)
        {
            got.insert(static_cast<int>(x.pid));
            inside = inside && x.inGesture && x.batchDepth == 1;
        }
        // A port stores what its normalised write maps back to (FakePort, as the APVTS): toPlain(toNorm(default)).
        const fcdsp::RawParams after = r.facade.currentRaw();
        bool landed = after.modeSlot == g.slot(to);
        for (std::size_t k = 0; k < fcdsp::kNumModeParams; ++k)
        {
            const auto pid = static_cast<Pid>(k);
            const float stored = expected.count(static_cast<int>(k)) != 0
                               ? fcdsp::toPlain(pid, fcdsp::toNorm(pid, want.v[k])) : want.v[k];
            landed = landed && funkgui::ease::sameBits(after.v[k], stored);
        }
        const bool batched = expected.empty() ? r.facade.batches() == 0 && r.host.log.batches == 0
                                              : r.facade.batches() == 1 && r.host.log.batches == 1;
        const bool modeFirst = !expectMode || (!w.empty() && w[0].pid == Pid::mode);
        const bool ok = got == expected && w.size() == expected.size() && inside && batched && modeFirst && landed
                     && r.facade.batchDepth() == 0 && r.host.log.batchDepth == 0;
        if (!ok)
        {
            char buf[256];
            std::snprintf(buf, sizeof buf, "%zu writes for %zu expected, inside %d, batches %d/%d, mode first %d, "
                          "landed %d", w.size(), expected.size(), inside ? 1 : 0, r.facade.batches(),
                          r.host.log.batches, modeFirst ? 1 : 0, landed ? 1 : 0);
            why = buf;
        }
        return ok;
    }

    // ---- scenarios ------------------------------------------------------------------------------------------------------

    struct Case
    {
        std::string_view key;
        const fcdsp::ModeDescriptor* desc;
        ui::ModeGrid grid;
        int own = -1;                                             // this Mode's item
        int next = -1;                                            // the neighbour
        int after = -1;                                           // the neighbour's neighbour
    };

    void openAndRow(Probe& P, const Case& c)
    {
        Rig r(c.key);
        P.le("open.settle_initial", r.settled, kMaxSettle);
        const funkgui::Point name = centre(L::header::kModeName);
        r.host.click(name.x, name.y);
        P.le("open.settle", r.settle(), kMaxSettle);
        P.eq("open.click", b(r.open() && nothingWritten(r.facade)), 1);

        const std::vector<funkgui::A11yItem> rows = browserItems(r, funkgui::A11yRole::listItem);
        const funkgui::A11yItem* own = byId(rows, rowId(c.grid.slot(c.own)));
        const auto g = static_cast<std::size_t>(c.desc->group);
        P.eq("row.listed", b(own != nullptr && own->title == std::string(c.desc->name)), 1);
        P.eq("row.rect", b(own != nullptr && sameRect(own->bounds, c.grid.rowRect(c.own))), 1);
        P.eq("row.inside_overlay", b(own != nullptr && own->bounds.x >= L::kOverlay.x && own->bounds.y >= L::kOverlay.y
                                     && own->bounds.right() <= L::kOverlay.right()
                                     && own->bounds.bottom() <= L::kOverlay.bottom()), 1);
        int checked = 0;
        for (const funkgui::A11yItem& it : rows)
            checked += it.checkable && it.checked ? 1 : 0;
        P.eq("row.checked", b(own != nullptr && own->checkable && own->checked && checked == 1), 1);
        P.eq("row.help", b(own != nullptr && own->help == std::string(c.desc->specLine)), 1);
        P.eq("row.description", b(own != nullptr && own->description == (g < 8 ? kGroupNames[g] : "OTHER")), 1);
        P.eq("row.count", static_cast<int64_t>(rows.size()), c.grid.size());

        P.eq("draw.current_bar", b(barAt(r, c.grid, c.own)), 1);
        P.eq("draw.current_ink", b(rowInks(r, c.grid, { c.own })), 1);
        P.eq("draw.inside_ground", browserOutsideGround(r), 0);
        P.eq("draw.glyphs_missing", static_cast<int64_t>(r.host.draw().missingGlyphs), 0);
    }

    void hover(Probe& P, const Case& c)
    {
        Rig r(c.key);
        openByClick(r);
        const funkgui::Point p = centre(c.grid.rowRect(c.next));
        r.host.move(p.x, p.y);
        r.host.tick(1, kDt);
        P.eq("hover.spec", b(startsWith(footerLine(r), c.grid.spec(c.next))), 1);
        P.eq("hover.ink", b(rowInks(r, c.grid, { c.own, c.next })), 1);
        P.eq("hover.cursor", b(r.panel.cursor() == funkgui::Cursor::pointingHand), 1);
        r.host.move(kEmptyInOverlay.x, kEmptyInOverlay.y);
        r.host.tick(1, kDt);
        P.eq("hover.cursor_off_rows", b(r.panel.cursor() == funkgui::Cursor::normal), 1);
        P.eq("hover.ink_off_rows", b(rowInks(r, c.grid, { c.own })), 1);
        P.eq("hover.no_write", b(nothingWritten(r.facade)), 1);
    }

    void clicks(Probe& P, const Case& c)
    {
        Rig r(c.key);
        openByClick(r);
        r.facade.resetCounts();
        clickItem(r, c.grid, c.next);
        P.eq("click.only_mode", b(oneTap(r.facade, Pid::mode) && r.slot() == c.grid.slot(c.next)), 1);
        P.eq("click.stays_open", b(r.open()), 1);
        r.host.tick(1, kDt);
        const std::vector<funkgui::A11yItem> rows = browserItems(r, funkgui::A11yRole::listItem);
        const funkgui::A11yItem* own = byId(rows, rowId(c.grid.slot(c.own)));
        const funkgui::A11yItem* next = byId(rows, rowId(c.grid.slot(c.next)));
        P.eq("click.checked_follows", b(own != nullptr && next != nullptr && !own->checked && next->checked), 1);
        P.eq("click.bar_follows", b(barAt(r, c.grid, c.next)), 1);
        P.le("click.settle", r.settle(), kMaxSettle);

        r.facade.resetCounts();
        clickItem(r, c.grid, c.own);
        P.eq("click.back", b(oneTap(r.facade, Pid::mode) && r.slot() == c.grid.slot(c.own) && r.open()), 1);
        r.host.tick(1, kDt);
        r.facade.resetCounts();
        clickItem(r, c.grid, c.own);
        P.eq("click.current_writes_nothing", b(nothingWritten(r.facade) && r.open()), 1);
    }

    void alt(Probe& P, const Case& c)
    {
        funkgui::Mods altMods;
        altMods.alt = true;
        {
            Rig r(c.key);
            openByClick(r);
            const fcdsp::RawParams before = r.facade.currentRaw();
            r.facade.resetCounts();
            const int hostBatches = r.host.log.batches;
            clickItem(r, c.grid, c.next, altMods);
            r.host.log.batches -= hostBatches;
            std::string why;
            const bool ok = defaultsBatch(r, c.grid, c.next, before, true, why);
            if (!ok)
                std::printf("NOTE     alt-click: %s\n", why.c_str());
            P.eq("alt.click_batch", b(ok), 1);
            P.eq("alt.stays_open", b(r.open()), 1);
        }
        {
            Rig r(c.key);
            openByClick(r);
            const fcdsp::RawParams before = r.facade.currentRaw();
            r.facade.resetCounts();
            const int hostBatches = r.host.log.batches;
            clickItem(r, c.grid, c.own, altMods);
            r.host.log.batches -= hostBatches;
            std::string why;
            const bool ok = defaultsBatch(r, c.grid, c.own, before, false, why);
            if (!ok)
                std::printf("NOTE     alt-click on the current row: %s\n", why.c_str());
            P.eq("alt.current_row_defaults_only", b(ok), 1);
        }
    }

    void doubleClick(Probe& P, const Case& c)
    {
        Rig r(c.key);
        openByClick(r);
        r.facade.resetCounts();
        const funkgui::Point p = centre(c.grid.rowRect(c.next));
        r.host.doubleClick(p.x, p.y);
        const bool once = oneTap(r.facade, Pid::mode) && r.slot() == c.grid.slot(c.next);
        P.le("dblclick.settle", r.settle(), kMaxSettle);
        P.eq("dblclick.switches_once", b(once), 1);
        P.eq("dblclick.closes", b(!r.open() && r.panel.screen() == ui::Screen::panel
                                  && !r.facade.uiState().charExpanded), 1);
    }

    void cancels(Probe& P, const Case& c)
    {
        {
            Rig r(c.key);
            openByClick(r);
            r.facade.resetCounts();
            const funkgui::Point p = centre(c.grid.rowRect(c.next));
            r.host.drag(p.x, p.y, kEmptyInOverlay.x, kEmptyInOverlay.y);
            P.eq("cancel.drag_off", b(nothingWritten(r.facade) && r.open()), 1);
            funkgui::Mods ctrl;
            ctrl.ctrl = true;
            clickItem(r, c.grid, c.next, ctrl);
            P.eq("cancel.popup", b(nothingWritten(r.facade) && r.open() && r.host.log.menus == 0), 1);
            const std::size_t before = browserItems(r, funkgui::A11yRole::listItem).size();
            r.host.wheel(kEmptyInOverlay.x, kEmptyInOverlay.y, -1.0f);
            r.host.wheel(kEmptyInOverlay.x, kEmptyInOverlay.y, 1.0f);
            r.host.tick(1, kDt);
            P.eq("cancel.wheel", b(nothingWritten(r.facade) && r.open()
                                   && browserItems(r, funkgui::A11yRole::listItem).size() == before), 1);
            r.host.keys("backspace,delete");
            P.eq("cancel.delete_keys", b(nothingWritten(r.facade) && r.open()), 1);
            r.host.keys("escape");
            P.le("cancel.escape_settle", r.settle(), kMaxSettle);
            P.eq("cancel.escape", b(!r.open() && nothingWritten(r.facade)), 1);
        }
        {
            Rig r(c.key);
            openByClick(r);
            r.facade.resetCounts();
            r.host.click(kOutside.x, kOutside.y);
            r.settle();
            P.eq("cancel.click_outside", b(!r.open() && nothingWritten(r.facade)), 1);
            openByClick(r);
            const bool reopened = r.open();
            openByClick(r);                                       // the latch again
            P.eq("cancel.latch", b(reopened && !r.open() && nothingWritten(r.facade)), 1);
        }
    }

    void keys(Probe& P, const Case& c)
    {
        {
            Rig r(c.key);
            const bool opened = openByKeys(r);
            P.eq("keys.open", b(opened && nothingWritten(r.facade)), 1);
            r.host.tick(1, kDt);
            P.eq("keys.ring_on_current", b(ringAt(r, c.grid, c.own)), 1);
            P.eq("keys.footer_current", b(startsWith(footerLine(r), c.grid.spec(c.own))), 1);

            struct Step { const char* token; funkgui::Key key; };
            using K = funkgui::Key;
            constexpr std::array<Step, 10> kSteps { { { "end", K::end }, { "up", K::up }, { "home", K::home },
                                                      { "down", K::down }, { "right", K::right }, { "down", K::down },
                                                      { "left", K::left }, { "pagedown", K::pageDown },
                                                      { "pageup", K::pageUp }, { "right", K::right } } };
            int h = c.own;
            bool follows = true, rings = true;
            for (const Step& s : kSteps)
            {
                h = c.grid.step(h, s.key);
                r.host.keys(s.token);
                r.host.tick(1, kDt);
                const bool f = startsWith(footerLine(r), c.grid.spec(h));
                if (!f)
                    std::printf("NOTE     after %s the footer reads \"%s\" (want %s)\n", s.token, footerLine(r).c_str(),
                                c.grid.name(h));
                follows = follows && f;
                rings = rings && ringAt(r, c.grid, h);
            }
            P.eq("keys.nav_follows_grid", b(follows), 1);
            P.eq("keys.nav_ring", b(rings), 1);
            P.eq("keys.nav_writes_nothing", b(nothingWritten(r.facade) && r.open()), 1);
            r.host.keys("return");
            const bool committed = c.grid.slot(h) == c.grid.slot(c.own) ? nothingWritten(r.facade)
                                                                         : oneTap(r.facade, Pid::mode);
            P.eq("keys.return_commits", b(committed && r.slot() == c.grid.slot(h)), 1);
            P.le("keys.return_settle", r.settle(), kMaxSettle);
            P.eq("keys.return_closes", b(!r.open()), 1);
        }
        {   // a11y focus puts the (keyboard) highlight on the neighbour; Alt-Return commits it with the defaults
            Rig r(c.key);
            openByClick(r);
            r.panel.a11yAction(rowId(c.grid.slot(c.next)), funkgui::A11yAction::focus);
            r.host.tick(1, kDt);
            P.eq("a11y.focus_highlights", b(startsWith(footerLine(r), c.grid.spec(c.next)) && ringAt(r, c.grid, c.next)),
                 1);
            const fcdsp::RawParams before = r.facade.currentRaw();
            r.facade.resetCounts();
            const int hostBatches = r.host.log.batches;
            r.host.keys("alt+return");
            r.host.log.batches -= hostBatches;
            std::string why;
            const bool ok = defaultsBatch(r, c.grid, c.next, before, true, why);
            if (!ok)
                std::printf("NOTE     alt-return: %s\n", why.c_str());
            P.eq("keys.alt_return_batch", b(ok), 1);
            r.settle();
            P.eq("keys.alt_return_closes", b(!r.open()), 1);
        }
        {   // type-ahead: the neighbour's whole name (spaces included), then Return
            Rig r(c.key);
            openByClick(r);
            r.host.keys(typing(c.grid.name(c.next)).c_str());
            r.host.tick(1, kDt);
            P.eq("keys.typeahead_finds", b(startsWith(footerLine(r), c.grid.spec(c.next))), 1);
            r.host.keys("return");
            P.eq("keys.typeahead_commit", b(oneTap(r.facade, Pid::mode) && r.slot() == c.grid.slot(c.next)), 1);
        }
        {   // the buffer expires after 1 s: a first letter, 1.1 s, then another Mode's whole name finds that Mode
            Rig r(c.key);
            openByClick(r);
            const char first[2] = { c.grid.name(c.next)[0], '\0' };
            r.host.keys(first);
            r.host.tick(static_cast<int>(1.1f / kDt), kDt);
            r.host.keys(typing(c.grid.name(c.after)).c_str());
            r.host.tick(1, kDt);
            P.eq("keys.typeahead_expires", b(startsWith(footerLine(r), c.grid.spec(c.after))), 1);
            P.eq("keys.typeahead_writes_nothing", b(nothingWritten(r.facade) && r.open()), 1);
        }
    }

    void a11y(Probe& P, const Case& c)
    {
        Rig r(c.key);
        openByClick(r);
        r.facade.resetCounts();
        r.panel.a11yAction(rowId(c.grid.slot(c.next)), funkgui::A11yAction::press);
        P.eq("a11y.press_commits", b(oneTap(r.facade, Pid::mode) && r.slot() == c.grid.slot(c.next) && r.open()), 1);
        const std::vector<funkgui::A11yItem> headings = browserItems(r, funkgui::A11yRole::staticText);
        bool own = false;
        for (const funkgui::A11yItem& h : headings)
            own = own || (h.bounds.x == c.grid.rowRect(c.own).x
                          && startsWith(h.title, kGroupNames[static_cast<std::size_t>(c.desc->group) % 8]));
        P.eq("a11y.headings", b(headings.size() == 8 && own), 1);
        std::set<uint32_t> ids;
        int n = 0;
        for (const funkgui::A11yItem& it : r.host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::modeBrowser))
            {
                ++n;
                ids.insert(it.id);
            }
        P.eq("a11y.ids_unique", b(n > 0 && static_cast<int>(ids.size()) == n), 1);
    }

    void chars(Probe& P, const Case& c)
    {
        Rig r(c.key);
        const ui::ViewSpec* v = ui::findView("chars.sidechain");
        if (v == nullptr)
        {
            P.harnessError("ui.modebrowser: no chars.sidechain view");
            return;
        }
        r.panel.setView(*v, true);
        r.settle();
        openByClick(r);
        r.facade.resetCounts();
        P.eq("chars.opens", b(r.open() && r.panel.screen() == ui::Screen::characteristics), 1);
        clickItem(r, c.grid, c.next);
        P.eq("chars.click", b(oneTap(r.facade, Pid::mode) && r.slot() == c.grid.slot(c.next)), 1);
        r.host.keys("escape");
        r.settle();
        P.eq("chars.escape_returns", b(!r.open() && r.panel.screen() == ui::Screen::characteristics), 1);
    }

    // ---- keyboard focus and what the overlay covers (S13 H1a) -----------------------------------------------------------

    // The bounding box of every FOCUS_RING primitive in the frame, and how many there are.
    struct Rings
    {
        int   n = 0;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    };

    Rings rings(Rig& r)
    {
        Rings g;
        for (const funkgui::Prim& p : r.host.draw().prims)
            if (p.tag == funkgui::tags::focusRing)
            {
                ++g.n;
                g.x0 = std::min(g.x0, p.x0);
                g.y0 = std::min(g.y0, p.y0);
                g.x1 = std::max(g.x1, p.x1);
                g.y1 = std::max(g.y1, p.y1);
            }
        return g;
    }

    // Every ring primitive of the frame belongs to one ring around `want` (FocusRing: want.reduced(1), AA apron).
    bool oneRingAround(Rig& r, const funkgui::Rect& want)
    {
        const Rings g = rings(r);
        const funkgui::Rect w = want.reduced(1.0f);
        const auto near = [](float a, float c) { return std::fabs(a - c) <= 2.5f; };
        const bool ok = g.n > 0 && near(g.x0, w.x) && near(g.y0, w.y) && near(g.x1, w.right())
                     && near(g.y1, w.bottom());
        if (!ok)
            std::printf("NOTE     rings: %d prims over {%g,%g}-{%g,%g}, want one around {%g,%g}-{%g,%g}\n", g.n,
                        static_cast<double>(g.x0), static_cast<double>(g.y0), static_cast<double>(g.x1),
                        static_cast<double>(g.y1), static_cast<double>(w.x), static_cast<double>(w.y),
                        static_cast<double>(w.right()), static_cast<double>(w.bottom()));
        return ok;
    }

    funkgui::Rect rowRing(const ui::ModeGrid& g, int i)       // ModeBrowser.cpp's ring: x − 8 … x + 102
    {
        const funkgui::Rect row = g.rowRect(i);
        return { row.x - 8.0f, row.y, row.w + 4.0f, row.h };
    }

    uint32_t latchId(const Rig& r)
    {
        for (const funkgui::A11yItem& it : r.host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::header)
                && it.role == funkgui::A11yRole::comboBox)
                return it.id;
        return 0;
    }

    void focus(Probe& P, const Case& c)
    {
        {
            Rig r(c.key);
            const uint32_t latch = latchId(r);
            const bool opened = openByKeys(r);
            r.host.tick(1, kDt);
            P.eq("focus.one_ring", b(opened && r.panel.context().focus == rowId(c.grid.slot(c.own))
                                     && oneRingAround(r, rowRing(c.grid, c.own))), 1);

            // The browser's stops: the shown page's rows in column order (ModeBrowser::focusOrder).
            std::vector<int> order;
            for (int k = 0; k < c.grid.columns(); ++k)
                if (ui::ModeGrid::pageOf(k) == 0)
                    for (int row = 0; row < c.grid.column(k).count; ++row)
                        order.push_back(c.grid.column(k).first + row);
            const auto at = std::find(order.begin(), order.end(), c.own);
            bool trap = at != order.end() && order.size() == static_cast<std::size_t>(c.grid.size());
            bool follows = trap;
            std::size_t i = trap ? static_cast<std::size_t>(at - order.begin()) : 0;
            for (std::size_t step = 0; trap && step < order.size(); ++step)
            {
                i = (i + 1) % order.size();
                r.host.keys("tab");
                r.host.tick(1, kDt);
                const int want = order[i];
                trap = trap && r.panel.context().focus == rowId(c.grid.slot(want)) && r.open();
                follows = follows && startsWith(footerLine(r), c.grid.spec(want))
                       && oneRingAround(r, rowRing(c.grid, want));
            }
            r.host.keys("shift+tab");
            r.host.tick(1, kDt);
            const int back = order.empty() ? -1 : order[(i + order.size() - 1) % order.size()];
            trap = trap && back >= 0 && r.panel.context().focus == rowId(c.grid.slot(back));
            P.eq("focus.tab_trap", b(trap), 1);
            P.eq("focus.tab_follows", b(follows), 1);
            P.eq("focus.tab_writes_nothing", b(nothingWritten(r.facade)), 1);

            r.host.keys("escape");
            r.settle();
            const bool back2 = !r.open() && latch != 0 && r.panel.context().focus == latch
                            && r.panel.context().focusVisible;
            // Header.cpp's latch ring: ‹ name › as one rectangle.
            const funkgui::Rect latchCells { L::header::kModePrev.x, L::header::kModePrev.y,
                                             L::header::kModeNext.right() - L::header::kModePrev.x,
                                             L::header::kModePrev.h };
            P.eq("focus.returns_to_latch", b(back2 && oneRingAround(r, latchCells)), 1);
        }
        {
            Rig r(c.key);
            openByClick(r);
            int covered = 0, slots = 0;
            for (const funkgui::A11yItem& it : r.host.accessibility())
            {
                if (!it.visible)
                    continue;
                const int v = ui::viewIndexOf(it.id);
                const funkgui::Rect ground { L::kOverlay.x - 8.0f, L::kOverlay.y - 4.0f, L::kOverlay.w + 16.0f,
                                             L::kOverlay.h + 8.0f };
                if (v != static_cast<int>(ui::ViewIndex::modeBrowser)
                    && ground.contains({ it.bounds.centreX(), it.bounds.centreY() }))
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

    // ---- review pictures (`-- --png-dir <dir>`) ---------------------------------------------------------------------

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

    void pictures(Probe& P, const Case& c, const std::string& dir)
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto save = [&](Rig& r, const char* what) {
            r.host.draw();
            const std::string path = dir + "/modebrowser-" + std::string(c.key) + "-" + what + ".png";
            if (!r.host.writePng(path.c_str(), 2))
                P.harnessError("ui.modebrowser: cannot write " + path);
            else
                std::printf("PNG      %s\n", path.c_str());
        };
        {
            Rig r(c.key);
            openByClick(r);
            const funkgui::Point p = centre(c.grid.rowRect(c.next));
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);
            save(r, "hover");
        }
        {
            Rig r(c.key);
            openByKeys(r);
            r.host.keys("right");
            r.host.tick(1, kDt);
            save(r, "keys");
        }
        {
            Rig r(c.key);
            const ui::ViewSpec* v = ui::findView("chars.sidechain");
            if (v != nullptr)
                r.panel.setView(*v, true);
            r.settle();
            openByClick(r);
            save(r, "chars");
        }
    }
}

FCMP_PROBE(ui, modebrowser)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.modebrowser: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    Case c { C.key, entry->desc, ui::ModeGrid::registered() };
    c.own = c.grid.find(fcdsp::slotOf(*entry));
    if (c.own < 0 || c.grid.size() < 3)
    {
        P.harnessError("ui.modebrowser: '" + std::string(C.key) + "' is not in the browser, or fewer than 3 Modes");
        return P.finish();
    }
    c.next = (c.own + 1) % c.grid.size();
    c.after = (c.own + 2) % c.grid.size();
    openAndRow(P, c);
    hover(P, c);
    clicks(P, c);
    alt(P, c);
    doubleClick(P, c);
    cancels(P, c);
    keys(P, c);
    a11y(P, c);
    chars(P, c);
    focus(P, c);
    if (const std::string dir = pngDir(); !dir.empty())
        pictures(P, c, dir);
    return P.finish();
}
