// FCMP_PROBE layer=ui name=zoom scope=mode timeout=300
//
// ui.zoom.<key> (S12.0, UF1b; ADR-68, ADR-68a): the footer's ZOOM cells over FunkGui v0.8.0's UI zoom, driven through
// HeadlessHost input over a FakeFacade (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0). HeadlessHost
// stays logical: HeadlessHost::setZoom({100, 125, 150, 175}, p) and setZoomFitLimit(n) simulate EditorHost's answers,
// and log.zooms counts setZoomPercent calls. Spec rows only.
//
//   geom.*       the caption and the four cells sit inside the footer band and the content columns (x 40–920), 16 px
//                tall on THEME's row, 4 px apart, in step order; no overlap with THEME, 12 px clear of it; the spec
//                line's fit box ends before the caption; drawn: every ZOOM glyph inside its cell (or the caption's box)
//                and, with a long notice on the line, every glyph of the line left of the caption.
//   line.*       the longest lines that are not a slot's spec fit the narrower line whole: the THEME and ZOOM specs,
//                the notices, and the lookahead hint of a Mode that wants lookahead (Brickwall: 552.8 of 554 px).
//   host.*       the Panel's HostServices proxy forwards zoomPercent, zoomSteps, zoomFits and setZoomPercent.
//   select.*     the selected cell is zoomPercent(): 125 at rest, then whatever the host says (another editor chose
//                100; a pin at 110 selects none); a click on another cell calls setZoomPercent once with its step and
//                the selection follows at once; a click on the selected cell makes no call; nothing writes a parameter
//                or opens a batch.
//   fit.*        with setZoomFitLimit(125): 150 and 175 are unavailable — drawn in ink16 (100 and 125 in their own
//                inks), a11y enabled = false with the help "Needs a larger display", a click on either makes no call,
//                and hovering either puts "<n> % NEEDS A LARGER DISPLAY" on the footer line; the focus spec names them.
//   nosteps.*    a host with no zoom steps (HeadlessHost before setZoom): all four cells available, 100 selected (the
//                live editor under its CANVAS_DUMP pin draws the same, so gui-live parity holds).
//   keys.*       Tab reaches the ZOOM group as the stop right before THEME (THEME stays last); → ← Home End select
//                through setZoomPercent, skipping unavailable steps; Return makes no call; a key acts on the host's
//                zoom as it is at that moment, before the footer's next tick.
//   a11y.*       the radioGroup "Zoom" over the cells, one radioButton per cell titled "ZOOM <n> %" at its rect,
//                checked on the selected one; press on a cell selects it; its value is the selected title.
//   spec.*       hovering an available cell shows "ZOOM   100 · 125 · 150 · 175 %   MACHINE-WIDE, …".
//
// Review pictures (not a test): probe-own flags after "--": -- --png-dir <dir> writes <dir>/zoom-<key>-125.png (the
// panel with 125 selected), zoom-<key>-hover-100.png (the pointer on 100), zoom-<key>-focus.png (the ZOOM group focused)
// and zoom-<key>-fit125-hover-150.png (setZoomFitLimit(125): 150 and 175 unavailable, the pointer on 150 and its hint).
//
// The probe needs FCMP_PREFS_DIR (CTest sets a sandbox): the panel reads the span, scale and theme preferences.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    namespace F = fcmp::ui::layout::footer;
    using fcmp::probe::FakeFacade;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    constexpr std::size_t kCells = F::kZoomSteps.size();
    const std::vector<int> kSteps(F::kZoomSteps.begin(), F::kZoomSteps.end());
    constexpr const char* kSepU = " \xC2\xB7 ";                 // " · "
    constexpr float kGlyphSlack = 2.0f;                          // a glyph quad's AA apron beyond its advance box

    int b(bool v) { return v ? 1 : 0; }

    // A FakeFacade, a Panel over it and a HeadlessHost; `zoom` > 0 gives the host the product's steps at that zoom
    // before the first frame (0: the host has no zoom, HostServices' defaults).
    struct Rig
    {
        explicit Rig(std::string_view key, int zoom = 125, int fitLimit = 0)
            : facade(key), panel(facade, kProbeOptions), host(panel, 0, 2.0f)
        {
            if (zoom > 0)
                host.setZoom(kSteps, zoom);
            host.setZoomFitLimit(fitLimit);
            settled = host.settle(kMaxSettle, kDt);
            host.tick(1, kDt);                                   // the footer reads the host's zoom every tick
            facade.resetCounts();
            zoomsBefore = host.log.zooms;
        }

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        const ui::PanelContext& ctx() const { return panel.context(); }
        int zooms() const { return host.log.zooms - zoomsBefore; }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
        int                   settled = 0;
        int                   zoomsBefore = 0;
    };

    funkgui::Point centre(const funkgui::Rect& r) { return { r.centreX(), r.centreY() }; }

    bool inside(const funkgui::Rect& inner, const funkgui::Rect& outer, float slack = 0.0f)
    {
        return inner.x >= outer.x - slack && inner.y >= outer.y - slack && inner.right() <= outer.right() + slack
            && inner.bottom() <= outer.bottom() + slack;
    }

    bool overlaps(const funkgui::Rect& a, const funkgui::Rect& c)
    {
        return a.x < c.right() && c.x < a.right() && a.y < c.bottom() && c.y < a.bottom();
    }

    uint32_t pack(funkgui::Col c)
    {
        return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16)
             | (static_cast<uint32_t>(c.a) << 24);
    }

    funkgui::PrimKind kindOf(const funkgui::Prim& p)
    {
        return static_cast<funkgui::PrimKind>(static_cast<int>(p.d2[2] + 0.5f));
    }

    // The caption's advance box: "ZOOM" in kCaption, its top at kZoomCaption.
    funkgui::Rect captionBox()
    {
        const float w = funkgui::text::width(funkgui::FontService::get().atlas(), "ZOOM", funkgui::type::kCaption);
        return { F::kZoomCaption.x, F::kZoomCaption.y, w, funkgui::type::kCaption.px };
    }

    float labelWidth(const std::string& s)
    {
        return funkgui::text::width(funkgui::FontService::get().atlas(), s.c_str(), funkgui::type::kLabel);
    }

    std::vector<funkgui::A11yItem> items(const Rig& r) { return r.host.accessibility(); }

    const funkgui::A11yItem* findItem(const std::vector<funkgui::A11yItem>& all, ui::ViewIndex v, funkgui::A11yRole role,
                                      std::string_view title = {})
    {
        for (const funkgui::A11yItem& it : all)
            if (ui::viewIndexOf(it.id) == static_cast<int>(v) && it.role == role && (title.empty() || it.title == title))
                return &it;
        return nullptr;
    }

    const funkgui::A11yItem* zoomGroup(const std::vector<funkgui::A11yItem>& all)
    {
        return findItem(all, ui::ViewIndex::footer, funkgui::A11yRole::radioGroup, "Zoom");
    }

    // The ZOOM radioButtons in cell order (nullptr where one is missing).
    std::array<const funkgui::A11yItem*, kCells> zoomButtons(const std::vector<funkgui::A11yItem>& all)
    {
        std::array<const funkgui::A11yItem*, kCells> out{};
        const funkgui::A11yItem* g = zoomGroup(all);
        if (g == nullptr)
            return out;
        for (const funkgui::A11yItem& it : all)
            if (it.parent == g->id && it.role == funkgui::A11yRole::radioButton && it.id > g->id
                && it.id - g->id - 1 < kCells)
                out[it.id - g->id - 1] = &it;
        return out;
    }

    // The checked ZOOM cell per accessibility; -1: none.
    int checkedCell(const Rig& r)
    {
        const auto all = items(r);
        const auto buttons = zoomButtons(all);
        int checked = -1;
        for (std::size_t i = 0; i < kCells; ++i)
            if (buttons[i] != nullptr && buttons[i]->checked)
                checked = checked < 0 ? static_cast<int>(i) : -2;   // -2: more than one
        return checked;
    }

    std::string footerLine(const Rig& r)
    {
        const auto all = items(r);
        const funkgui::A11yItem* it = findItem(all, ui::ViewIndex::footer, funkgui::A11yRole::staticText, "Footer");
        return it != nullptr ? it->value : std::string("<no item>");
    }

    bool nothingWritten(FakeFacade& f)
    {
        int begins = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            begins += f.fakePort(static_cast<fcdsp::Pid>(i)).begins();
        return f.writes().empty() && begins == 0 && f.batches() == 0;
    }

    std::string stepsText()                                      // "100 · 125 · 150 · 175"
    {
        std::string s;
        for (std::size_t i = 0; i < kCells; ++i)
            s += (i > 0 ? kSepU : "") + std::to_string(F::kZoomSteps[i]);
        return s;
    }

    // ---- geometry -------------------------------------------------------------------------------------------------------

    void geometry(Probe& P, std::string_view key)
    {
        const funkgui::Rect content { L::kContentLeft, L::kFooter.y, L::kContentWidth, L::kFooter.h };
        const funkgui::Rect cap = captionBox();
        bool inBand = inside(cap, L::kFooter) && inside(cap, content);
        bool sized = true, ordered = true, clearOfTheme = true;
        for (std::size_t i = 0; i < kCells; ++i)
        {
            const funkgui::Rect& c = F::kZoomCells[i];
            inBand = inBand && inside(c, L::kFooter) && inside(c, content);
            sized = sized && c.h == F::kThemeCells[0].h && c.y == F::kThemeCells[0].y;
            if (i > 0)
                ordered = ordered && c.x - F::kZoomCells[i - 1].right() == F::kThemeCells[1].x - F::kThemeCells[0].right();
            for (const funkgui::Rect& t : F::kThemeCells)
                clearOfTheme = clearOfTheme && !overlaps(c, t) && !overlaps(cap, t);
        }
        P.eq("geom.inside_band", b(inBand), 1);
        P.eq("geom.theme_row_and_gaps", b(sized && ordered), 1);
        P.eq("geom.clear_of_theme", b(clearOfTheme), 1);
        P.ge("geom.theme_gap_px", static_cast<double>(F::kThemeCells[0].x - F::kZoomCells[kCells - 1].right()), 8.0);
        P.ge("geom.caption_gap_px", static_cast<double>(F::kZoomCells[0].x - cap.right()), 4.0);
        P.ge("geom.line_gap_px", static_cast<double>(cap.x - (L::footer::kSpec.x + F::kSpecLineW)), 8.0);

        // Drawn: every ZOOM glyph inside its cell or the caption's box, and in the band.
        {
            Rig r(key);
            const funkgui::PrimList& pl = r.host.draw();
            const funkgui::Rect zone { cap.x - kGlyphSlack, L::kFooter.y, F::kZoomCells[kCells - 1].right() - cap.x
                                       + 2.0f * kGlyphSlack, L::kFooter.h };
            int glyphs = 0, stray = 0;
            for (const funkgui::Prim& p : pl.prims)
            {
                if (p.tag != funkgui::tags::cell || kindOf(p) != funkgui::PrimKind::text)
                    continue;
                const funkgui::Rect q { p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0 };
                if (!overlaps(q, zone))
                    continue;
                ++glyphs;
                bool home = inside(q, cap, kGlyphSlack);
                for (const funkgui::Rect& c : F::kZoomCells)
                    home = home || inside(q, c, kGlyphSlack);
                if (!home || !inside(q, L::kFooter, kGlyphSlack))
                    ++stray;
            }
            P.eq("geom.drawn_glyphs", glyphs, 4 + 3 * static_cast<int>(kCells));   // "ZOOM" + four 3-digit labels
            P.eq("geom.drawn_inside", stray, 0);
        }
        // Drawn: a long line (three notices joined, > 554 px) is fitted and stops left of the caption.
        {
            Rig r(key);
            fcmp::StateNotice n;
            n.serial = 7;
            n.newerSession = true;
            n.modeRevised = true;
            n.savedRev = 1;
            n.currentRev = 2;
            r.facade.setStateNotice(n);
            r.host.tick(1, kDt);
            const std::string line = footerLine(r);
            float right = 0.0f;
            int glyphs = 0;
            for (const funkgui::Prim& p : r.host.draw().prims)
                if (p.tag == ui::tag::notice && kindOf(p) == funkgui::PrimKind::text)
                {
                    right = std::max(right, p.x1);
                    ++glyphs;
                }
            P.ge("geom.long_line_px", static_cast<double>(labelWidth(line)), static_cast<double>(F::kSpecLineW) + 1.0);
            P.ge("geom.long_line_drawn", glyphs, 1);
            P.le("geom.long_line_right", static_cast<double>(right),
                 static_cast<double>(L::footer::kSpec.x + F::kSpecLineW + kGlyphSlack));
        }
    }

    // ---- the narrower line ----------------------------------------------------------------------------------------------

    void lines(Probe& P, const fcdsp::ModeDescriptor& desc)
    {
        const double w = static_cast<double>(F::kSpecLineW);
        P.le("line.theme_spec", static_cast<double>(labelWidth("THEME   GRAPHITE \xC2\xB7 PAPER   MACHINE-WIDE, NOT SAVED "
                                                                 "WITH THE SESSION")), w);
        P.le("line.zoom_spec", static_cast<double>(labelWidth("ZOOM   " + stepsText() + " %   MACHINE-WIDE, NOT SAVED "
                                                                "WITH THE SESSION")), w);
        P.le("line.zoom_spec_unavailable", static_cast<double>(labelWidth("ZOOM   " + stepsText() + " %   " + stepsText()
                                                                            + " % NEED A LARGER DISPLAY")), w);
        P.le("line.notice_newer", static_cast<double>(labelWidth("SESSION FROM A NEWER FCOMPRESSOR \xE2\x80\x94 LOADED "
                                                                   "BEST EFFORT")), w);
        P.le("line.first_run", static_cast<double>(labelWidth("DRAG A VALUE OR THE CURVE. DOUBLE-CLICK TO RESET.")), w);
        if (desc.wantsLookahead)
            P.le("line.lookahead_hint", static_cast<double>(labelWidth(std::string(desc.name) + " WITHOUT LOOKAHEAD CAN "
                                                                         "OVERSHOOT \xE2\x80\x94 SET LOOKAHEAD 5 MS ABOVE "
                                                                         "(+5 MS LATENCY)")), w);
    }

    // ---- HostServices forwarding ----------------------------------------------------------------------------------------

    void forwarding(Probe& P, std::string_view key)
    {
        Rig r(key, 125, 125);
        funkgui::HostServices* h = r.ctx().host;
        if (h == nullptr)
        {
            P.harnessError("ui.zoom: the Panel has no host after attach");
            return;
        }
        const std::span<const int> steps = h->zoomSteps();
        P.eq("host.zoom_percent", h->zoomPercent(), 125);
        P.eq("host.zoom_steps", b(std::equal(steps.begin(), steps.end(), kSteps.begin(), kSteps.end())), 1);
        P.eq("host.zoom_fits", b(h->zoomFits(125) && !h->zoomFits(150)), 1);
        h->setZoomPercent(100);
        P.eq("host.set_zoom", b(r.zooms() == 1 && r.host.zoomPercent() == 100), 1);
    }

    // ---- selection ------------------------------------------------------------------------------------------------------

    void selection(Probe& P, std::string_view key)
    {
        {
            Rig r(key);
            P.le("select.settle", r.settled, kMaxSettle);
            P.eq("select.rest_125", checkedCell(r), 1);
            const funkgui::Point p150 = centre(F::kZoomCells[2]);
            r.host.click(p150.x, p150.y);
            P.eq("select.click_calls_once", r.zooms(), 1);
            P.eq("select.click_step", r.host.zoomPercent(), 150);
            P.eq("select.follows_at_once", checkedCell(r), 2);    // no tick between the click and the read
            P.eq("select.writes_nothing", b(nothingWritten(r.facade)), 1);
            const funkgui::Point p150again = centre(F::kZoomCells[2]);
            r.host.click(p150again.x, p150again.y);
            P.eq("select.active_no_call", r.zooms(), 1);
            P.eq("select.settles", b(r.host.settle(kMaxSettle, kDt) <= kMaxSettle), 1);
        }
        {   // the host changes on its own (another editor wrote the preference): the footer follows on the next tick
            Rig r(key);
            r.host.setZoom(kSteps, 100);
            r.host.tick(1, kDt);
            P.eq("select.follows_host", checkedCell(r), 0);
            r.host.setZoom(kSteps, 110);                         // e.g. a UI_ZOOM=110 pin: not a step, none selected
            r.host.tick(1, kDt);
            P.eq("select.none_off_step", checkedCell(r), -1);
            P.eq("select.follows_writes_nothing", b(nothingWritten(r.facade) && r.zooms() == 0), 1);
        }
    }

    // ---- steps that do not fit (ADR-68a) ----------------------------------------------------------------------------

    void fit(Probe& P, std::string_view key)
    {
        Rig r(key, 125, 125);
        const auto all = items(r);
        const auto buttons = zoomButtons(all);
        bool a11yOk = true;
        for (std::size_t i = 0; i < kCells; ++i)
        {
            const bool fits = F::kZoomSteps[i] <= 125;
            a11yOk = a11yOk && buttons[i] != nullptr && buttons[i]->enabled == fits
                  && buttons[i]->help == (fits ? "" : "Needs a larger display");
        }
        P.eq("fit.a11y_states", b(a11yOk), 1);

        // Inks: 150 and 175 in ink16 (n/a); 100 at rest in ink32, 125 selected in ink70.
        const funkgui::Theme th = funkgui::Theme::byIndex(0);
        const std::array<uint32_t, kCells> want { pack(th.ink32), pack(th.ink70), pack(th.ink16), pack(th.ink16) };
        std::array<int, kCells> right{}, wrong{};
        for (const funkgui::Prim& p : r.host.draw().prims)
        {
            if (p.tag != funkgui::tags::cell || kindOf(p) != funkgui::PrimKind::text)
                continue;
            const funkgui::Point c { 0.5f * (p.x0 + p.x1), 0.5f * (p.y0 + p.y1) };
            for (std::size_t i = 0; i < kCells; ++i)
                if (F::kZoomCells[i].contains(c))
                    ++(p.c0 == want[i] ? right[i] : wrong[i]);
        }
        bool inks = true;
        for (std::size_t i = 0; i < kCells; ++i)
            inks = inks && right[i] == 3 && wrong[i] == 0;
        P.eq("fit.inks", b(inks), 1);

        for (const std::size_t i : { std::size_t{ 2 }, std::size_t{ 3 } })
        {
            const std::string step = std::to_string(F::kZoomSteps[i]);
            const funkgui::Point p = centre(F::kZoomCells[i]);
            r.host.click(p.x, p.y);
            P.eq("fit.click_" + step + "_no_call", r.zooms(), 0);
            P.eq("fit.click_" + step + "_keeps", r.host.zoomPercent(), 125);
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);
            P.eq("fit.hint_" + step, b(footerLine(r) == step + " % NEEDS A LARGER DISPLAY"), 1);
            P.eq("fit.cursor_" + step, b(r.panel.cursor() == funkgui::Cursor::normal), 1);
        }
        {
            const funkgui::Point p = centre(F::kZoomCells[0]);
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);
            P.eq("fit.available_cursor", b(r.panel.cursor() == funkgui::Cursor::pointingHand), 1);
            r.host.click(p.x, p.y);
            P.eq("fit.available_click", b(r.zooms() == 1 && r.host.zoomPercent() == 100), 1);
        }
        P.eq("fit.writes_nothing", b(nothingWritten(r.facade)), 1);

        // Focus: the spec names the steps this display cannot show; the arrows skip them.
        Rig k(key, 125, 125);
        const funkgui::A11yItem* g = zoomGroup(items(k));
        if (g == nullptr)
        {
            P.harnessError("ui.zoom: no ZOOM radioGroup");
            return;
        }
        k.panel.a11yAction(g->id, funkgui::A11yAction::focus);
        k.host.tick(1, kDt);
        const std::string unavailable = std::to_string(F::kZoomSteps[2]) + kSepU + std::to_string(F::kZoomSteps[3]);
        P.eq("fit.focus_spec", b(footerLine(k) == "ZOOM   " + stepsText() + " %   " + unavailable
                                                   + " % NEED A LARGER DISPLAY"), 1);
        k.host.keys("right");
        P.eq("fit.keys_skip", b(k.zooms() == 0 && k.host.zoomPercent() == 125), 1);
        k.host.keys("end");
        P.eq("fit.keys_end_skip", b(k.zooms() == 0 && k.host.zoomPercent() == 125), 1);
        k.host.keys("left");
        P.eq("fit.keys_left", b(k.zooms() == 1 && k.host.zoomPercent() == 100), 1);
    }

    // ---- a host with no zoom steps ------------------------------------------------------------------------------------

    void noSteps(Probe& P, std::string_view key)
    {
        Rig r(key, 0);
        const auto buttons = zoomButtons(items(r));
        bool all = true;
        for (const funkgui::A11yItem* it : buttons)
            all = all && it != nullptr && it->enabled;
        P.eq("nosteps.all_available", b(all), 1);
        P.eq("nosteps.selected_100", checkedCell(r), 0);
        const funkgui::Theme th = funkgui::Theme::byIndex(0);
        int ink16 = 0;
        for (const funkgui::Prim& p : r.host.draw().prims)
            if (p.tag == funkgui::tags::cell && kindOf(p) == funkgui::PrimKind::text && p.c0 == pack(th.ink16))
                ++ink16;
        P.eq("nosteps.no_unavailable_ink", ink16, 0);
    }

    // ---- keyboard -------------------------------------------------------------------------------------------------------

    void keyboard(Probe& P, std::string_view key)
    {
        Rig r(key);
        const auto all = items(r);
        const funkgui::A11yItem* zoom = zoomGroup(all);
        const funkgui::A11yItem* theme = findItem(all, ui::ViewIndex::footer, funkgui::A11yRole::radioGroup, "Theme");
        if (zoom == nullptr || theme == nullptr)
        {
            P.harnessError("ui.zoom: no ZOOM or THEME radioGroup");
            return;
        }
        std::vector<uint32_t> stops;
        for (int i = 0; i < 1024; ++i)
        {
            r.host.keys("tab");
            const uint32_t f = r.ctx().focus;
            if (!stops.empty() && f == stops.front())
                break;
            stops.push_back(f);
        }
        const std::size_t n = stops.size();
        P.eq("keys.tab_before_theme", b(n >= 2 && stops[n - 2] == zoom->id && stops[n - 1] == theme->id), 1);

        r.panel.a11yAction(zoom->id, funkgui::A11yAction::focus);
        r.host.keys("right");
        P.eq("keys.right", b(r.zooms() == 1 && r.host.zoomPercent() == 150), 1);
        r.host.keys("home");
        P.eq("keys.home", b(r.zooms() == 2 && r.host.zoomPercent() == 100), 1);
        r.host.keys("end");
        P.eq("keys.end", b(r.zooms() == 3 && r.host.zoomPercent() == 175), 1);
        r.host.keys("left");
        P.eq("keys.left", b(r.zooms() == 4 && r.host.zoomPercent() == 150), 1);
        r.host.keys("return");
        P.eq("keys.return_no_call", r.zooms(), 4);
        P.eq("keys.selected", checkedCell(r), 2);
        // A key before the footer's next tick acts on the host's zoom as it is now (EditorHost replays UI_KEYS before
        // the first frame ticks): the host moved to 175 on its own, "left" selects 150.
        r.host.setZoom(kSteps, 175);
        r.host.keys("left");
        P.eq("keys.reads_host_now", b(r.zooms() == 5 && r.host.zoomPercent() == 150), 1);
        P.eq("keys.writes_nothing", b(nothingWritten(r.facade)), 1);
    }

    // ---- accessibility ----------------------------------------------------------------------------------------------

    void accessibility(Probe& P, std::string_view key)
    {
        Rig r(key);
        const auto all = items(r);
        const funkgui::A11yItem* g = zoomGroup(all);
        const auto buttons = zoomButtons(all);
        bool ok = g != nullptr && g->value == "ZOOM 125 %";
        for (std::size_t i = 0; i < kCells; ++i)
        {
            const funkgui::A11yItem* it = buttons[i];
            const funkgui::Rect& c = F::kZoomCells[i];
            ok = ok && it != nullptr && it->title == "ZOOM " + std::to_string(F::kZoomSteps[i]) + " %"
              && it->bounds.x == c.x && it->bounds.y == c.y && it->bounds.w == c.w && it->bounds.h == c.h
              && it->checkable && it->checked == (i == 1) && it->enabled && it->visible;
        }
        P.eq("a11y.items", b(ok), 1);
        if (buttons[0] != nullptr)
            r.panel.a11yAction(buttons[0]->id, funkgui::A11yAction::press);
        P.eq("a11y.press", b(r.zooms() == 1 && r.host.zoomPercent() == 100 && checkedCell(r) == 0), 1);
        const auto after = items(r);
        const funkgui::A11yItem* g2 = zoomGroup(after);
        P.eq("a11y.value_follows", b(g2 != nullptr && g2->value == "ZOOM 100 %"), 1);
    }

    // ---- spec line --------------------------------------------------------------------------------------------------

    void spec(Probe& P, std::string_view key)
    {
        Rig r(key);
        const funkgui::Point p = centre(F::kZoomCells[0]);
        r.host.move(p.x, p.y);
        r.host.tick(1, kDt);
        P.eq("spec.hover", b(footerLine(r) == "ZOOM   " + stepsText() + " %   MACHINE-WIDE, NOT SAVED WITH THE SESSION"),
             1);
        const funkgui::A11yItem* g = zoomGroup(items(r));
        P.eq("spec.hand_is_zoom", b(g != nullptr && r.ctx().hand.item == g->id), 1);
        r.host.move(480.0f, 300.0f);                             // off the footer: the spec goes
        r.host.tick(1, kDt);
        P.eq("spec.leaves", b(footerLine(r).rfind("ZOOM", 0) != 0), 1);
    }
}

namespace
{
    // `-- --png-dir <dir>` (after the lone "--": the probe's own flags, S5 lead revision); "" when absent.
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

    void pictures(Probe& P, std::string_view key, const std::string& dir)
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto save = [&](Rig& r, const char* what) {
            r.host.draw();
            const std::string path = dir + "/zoom-" + std::string(key) + "-" + what + ".png";
            if (!r.host.writePng(path.c_str(), 2))
                P.harnessError("ui.zoom: cannot write " + path);
            else
                std::printf("PNG      %s\n", path.c_str());
        };
        {
            Rig r(key);
            save(r, "125");
        }
        {
            Rig r(key);
            const funkgui::Point p = centre(F::kZoomCells[0]);
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);                                 // no input ticks the clock: the first frame sees it
            r.host.settle(kMaxSettle, kDt);
            save(r, "hover-100");
        }
        {
            Rig r(key);
            const funkgui::A11yItem* g = zoomGroup(items(r));
            if (g != nullptr)
                r.panel.a11yAction(g->id, funkgui::A11yAction::focus);
            r.host.tick(1, kDt);
            save(r, "focus");
        }
        {
            Rig r(key, 125, 125);
            const funkgui::Point p = centre(F::kZoomCells[2]);
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);
            r.host.settle(kMaxSettle, kDt);
            save(r, "fit125-hover-150");
        }
    }
}

FCMP_PROBE(ui, zoom)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.zoom: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.zoom reads UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    geometry(P, C.key);
    lines(P, *entry->desc);
    forwarding(P, C.key);
    selection(P, C.key);
    fit(P, C.key);
    noSteps(P, C.key);
    keyboard(P, C.key);
    accessibility(P, C.key);
    spec(P, C.key);
    if (const std::string dir = pngDir(); !dir.empty())
        pictures(P, C.key, dir);
    return P.finish();
}
