// FCMP_PROBE layer=ui name=charscreen scope=mode timeout=300
//
// ui.charscreen.<key> (02 §7, §8.8, §8.9, §9.1–9.2; S9.3 U3): the full-panel Characteristics screen, driven through the
// Panel API and HeadlessHost input over a FakeFacade (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0).
// Spec rows only (no golden rows).
//
//   chrome.<view>.*   Q4: every primitive of the chrome (header, preset strip, display row, footer: all outside the middle
//                     region y 124–600) is byte-identical on PANEL and on chars.sidechain / chars.colour, except the
//                     CHARACTERISTICS latch's own, which keeps its rectangle and reads ON (a11y checked); the middle differs
//   toggle.*          every way in and out, none of which writes a parameter: the latch (click; Return and Space while it
//                     is focused; a11y press), a double-click on an empty part of the band (a double-click on a band
//                     handle goes to the handle and stays on PANEL), Esc in 02 §7.1's order (browser → focus ring →
//                     screen); UiState::charExpanded follows every time
//   fade.*            the ScreenFader crossfade (τ 0.12 s, snap 1e-3): both screens are drawn for exactly the frames
//                     funkgui::ease::toward takes at 60 Hz, and input reaches the target screen at once
//   tab.*             SC|COLOUR on UiState::scTab: a click on each cell, ←/→/Home/End on the focused group, a11y press on
//                     each radio button; the hidden pane draws nothing and its a11y items are invisible; nothing written
//   taborder.<tab>.*  02 §7.5: Mode latch, preset strip, QUALITY, LOOKAHEAD, DELTA, BYPASS, CHARACTERISTICS, the span,
//                     scale and SC|COLOUR groups, meter reset, the handles THRESHOLD KNEE RATIO RANGE ATTACK RELEASE (n/a
//                     skipped) and SC HPF (SIDECHAIN only), THEME; Tab wraps and never visits a stop twice
//   handle.<pid>.*    the handles proxy their slots (02 §7.4, §7.5): the a11y item equals the slot's on PANEL (role, title,
//                     description, help, value, state, value interface); → then Home on the focused handle write exactly
//                     what they write on the focused slot; a drag writes only that parameter's port, inside one gesture
//                     (locked / derived: nothing). handle.thr_line.*: the HISTORY threshold line likewise
//   cp.*              CONTROL PATH over scripted HistoryColumns (a live scripted frame, 17 columns per frame): its columns
//                     are HISTORY's (the same segment edges as HIST_GR); applied / min / target at every column centre
//                     equal the column's max grMaxDb / min grMinDb / max tgtMaxDb (<= 0.01 dB, through CP_AXIS); the
//                     internal lane equals internal0 through the declared lo…hi (<= 0.5 px); the phase lane's runs and
//                     inks; each event row's stripes; gaps break every trace; stale and press-and-hold (HISTORY) hold
//                     the strip and the freeze cursor crosses both plots at one x; the image title names the internal
//   readouts.*        READOUTS over scripted frames, through their a11y rows (the text drawn): names; single values, the
//                     10 ms envelope for DET / TARGET, L/R and M/S pairs (caption and title), "–" when not live or while
//                     the audio runs another Mode; every row fits; the drawn glyph count equals the text's
//   meters.*          the IN SC GR OUT labels never touch (lead revision 5a) nor TRANSFER's labels on the same row, and
//                     stay near their bars; the SC meter shows "–" without an external key and its bar with one
//   stage.*           STAGE_CURVE on this screen iff the Mode has a second stage, on staticGain({stage2 = false}) <= 0.5 px
//
// The probe sets the span and scale preferences to their defaults: it refuses to run without FCMP_PREFS_DIR (CTest sets a
// sandbox), so it never touches the user's real preferences file.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    constexpr ui::PanelOptions kOpts { true, true, false };        // skipHint, syncPreview, !ignoreLive
    constexpr const char* kDash = "\xE2\x80\x93";                  // U+2013
    constexpr int   kColumnsPerFrame = 17;                         // ~1 ms of audio per column at 60 Hz frames
    constexpr float kMinLabelGap = 5.5f;                           // meter labels: 3/4 of a kMicro word space (7.35 px)
    constexpr float kLabelBarPx = 3.0f;                            // a meter label stays this close to its bar's centre

    int b(bool v) { return v ? 1 : 0; }

    // ---- reading a frame back -------------------------------------------------------------------------------------------

    struct Box { float x = 0, y = 0, w = 0, h = 0; };
    struct Seg { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    struct Col { float x0 = 0, x1 = 0, top0 = 0, top1 = 0, bot0 = 0, bot1 = 0; };

    Box boxOf(const funkgui::Prim& p)                              // an rrect's own rectangle (apron removed)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx - p.d0[2], cy - p.d0[3], 2.0f * p.d0[2], 2.0f * p.d0[3] };
    }

    Seg segOf(const funkgui::Prim& p)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx + p.d1[0], cy + p.d1[1], cx + p.d1[2], cy + p.d1[3] };
    }

    Col colOf(const funkgui::Prim& p)
    {
        const float cy = 0.5f * (p.y0 + p.y1);
        return { p.x0, p.x1, cy + p.d1[0], cy + p.d1[1], cy + p.d1[2], cy + p.d1[3] };
    }

    funkgui::PrimKind kindOf(const funkgui::Prim& p)
    {
        return static_cast<funkgui::PrimKind>(static_cast<int>(p.d2[2] + 0.5f));
    }

    std::vector<const funkgui::Prim*> tagged(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        std::vector<const funkgui::Prim*> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == t)
                v.push_back(&p);
        return v;
    }

    int axes(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        int n = 0;
        for (const funkgui::AxisRec& a : pl.axes)
            n += a.tag == t ? 1 : 0;
        return n;
    }

    const funkgui::AxisRec* axisOf(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        for (const funkgui::AxisRec& a : pl.axes)
            if (a.tag == t)
                return &a;
        return nullptr;
    }

    float toPx(const funkgui::AxisMap& m, float v) { return m.px0 + (v - m.v0) * (m.px1 - m.px0) / (m.v1 - m.v0); }
    float toValue(const funkgui::AxisMap& m, float px) { return m.v0 + (px - m.px0) * (m.v1 - m.v0) / (m.px1 - m.px0); }

    uint32_t pack(funkgui::Col c)
    {
        return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16)
             | (static_cast<uint32_t>(c.a) << 24);
    }

    bool samePrim(const funkgui::Prim& a, const funkgui::Prim& c) { return std::memcmp(&a, &c, sizeof a) == 0; }

    const funkgui::FontAtlasSdf& atlas() { return funkgui::FontService::get().atlas(); }

    // ---- the rig ----------------------------------------------------------------------------------------------------------

    // A FakeFacade, a Panel over it at `viewId` and a HeadlessHost, settled; `prep` runs on the facade first.
    struct Rig
    {
        template <class Prep>
        Rig(std::string_view key, Prep&& prep, const char* viewId)
            : facade(key), prepared((prep(facade), true)), panel(facade, kOpts), host(panel, 0, 2.0f)
        {
            if (const ui::ViewSpec* v = ui::findView(viewId))
                panel.setView(*v, true);
            settled = host.settle(kMaxSettle, kDt);
            facade.resetCounts();
        }
        Rig(std::string_view key, const char* viewId) : Rig(key, [](FakeFacade&) {}, viewId) {}

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        const ui::PanelContext& ctx() const { return panel.context(); }
        int  settle() { return host.settle(kMaxSettle, kDt); }
        void focus(uint32_t id) { panel.a11yAction(id, funkgui::A11yAction::focus, 0.0); }
        bool chars() const { return panel.screen() == ui::Screen::characteristics; }
        uint8_t slot() const { return static_cast<uint8_t>(ctx().frame.res.view.slot); }

        FakeFacade            facade;
        bool                  prepared;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
        int                   settled = 0;
    };

    funkgui::Point centre(const funkgui::Rect& r) { return { r.centreX(), r.centreY() }; }

    const funkgui::A11yItem* byId(const std::vector<funkgui::A11yItem>& items, uint32_t id)
    {
        for (const funkgui::A11yItem& it : items)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    const funkgui::A11yItem* find(const std::vector<funkgui::A11yItem>& items, ui::ViewIndex v, funkgui::A11yRole role,
                                  std::string_view title)
    {
        for (const funkgui::A11yItem& it : items)
            if (ui::viewIndexOf(it.id) == static_cast<int>(v) && it.role == role && it.title == title)
                return &it;
        return nullptr;
    }

    uint32_t latchId(const Rig& r)
    {
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        const funkgui::A11yItem* it = find(items, ui::ViewIndex::displayRow, funkgui::A11yRole::toggleButton,
                                           "CHARACTERISTICS");
        return it != nullptr ? it->id : 0;
    }

    bool nothingWritten(FakeFacade& f)
    {
        int begins = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            begins += f.fakePort(static_cast<Pid>(i)).begins();
        return f.writes().empty() && begins == 0;
    }

    std::string pidName(Pid p) { return std::string(fcdsp::kHostParams[fcdsp::idx(p)].id); }

    uint32_t slotItemId(Pid p)
    {
        for (std::size_t i = 0; i < L::kSlots.size(); ++i)
            if (L::kSlots[i].pid == p)
                return ui::a11yId(ui::ViewIndex::slotGrid, static_cast<uint32_t>(1 + i));
        return 0;
    }

    // The plot of the Characteristics screen whose handle proxies `p` (CharScreen.h's plot indices), or -1.
    int handlePlot(Pid p)
    {
        if (p == Pid::thr || p == Pid::knee || p == Pid::ratio || p == Pid::range)
            return 1;                                             // TRANSFER
        if (p == Pid::atk)
            return 5;
        if (p == Pid::rel)
            return 6;
        return p == Pid::schpf ? 7 : -1;
    }

    constexpr std::array<const char*, 4> kPhaseNames { "IDLE", "ATTACK", "HOLD", "RELEASE" };

    constexpr std::array<Pid, 7> kHandlePids { Pid::thr, Pid::knee, Pid::ratio, Pid::range, Pid::atk, Pid::rel,
                                               Pid::schpf };

    // The handle item of `p` on the Characteristics screen: in its plot's handle ids, titled as the slot.
    const funkgui::A11yItem* handleItem(const std::vector<funkgui::A11yItem>& items, Pid p)
    {
        const funkgui::A11yItem* slotIt = byId(items, slotItemId(p));
        const int plot = handlePlot(p);
        if (slotIt == nullptr || plot < 0)
            return nullptr;
        for (const funkgui::A11yItem& it : items)
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::charScreen) && ui::isHandleId(it.id)
                && ui::plotIndexOf(it.id) == plot && it.title == slotIt->title)
                return &it;
        return nullptr;
    }

    fcdsp::SlotState stateOf(const Rig& r, Pid p) { return r.ctx().frame.res.view.p[fcdsp::idx(p)].state; }

    // ---- chrome: identical on both screens (Q4) ---------------------------------------------------------------------------

    bool inMiddle(const funkgui::Prim& p) { return !(p.y1 <= L::kMiddleTop || p.y0 >= L::kMiddleBottom); }

    bool inLatch(const funkgui::Prim& p)
    {
        const funkgui::Rect& r = L::display::kCharacteristics;
        return p.x0 >= r.x - 2.0f && p.x1 <= r.right() + 2.0f && p.y0 >= r.y - 2.0f && p.y1 <= r.bottom() + 2.0f;
    }

    struct Chrome
    {
        std::vector<funkgui::Prim> rest, latch, middle;
    };

    Chrome chromeOf(const funkgui::PrimList& pl)
    {
        Chrome c;
        for (const funkgui::Prim& p : pl.prims)
            (inMiddle(p) ? c.middle : inLatch(p) ? c.latch : c.rest).push_back(p);
        return c;
    }

    bool same(const std::vector<funkgui::Prim>& a, const std::vector<funkgui::Prim>& c)
    {
        if (a.size() != c.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!samePrim(a[i], c[i]))
                return false;
        return true;
    }

    Box bounds(const std::vector<funkgui::Prim>& v)
    {
        if (v.empty())
            return {};
        float x0 = v[0].x0, y0 = v[0].y0, x1 = v[0].x1, y1 = v[0].y1;
        for (const funkgui::Prim& p : v)
        {
            x0 = std::min(x0, p.x0);
            y0 = std::min(y0, p.y0);
            x1 = std::max(x1, p.x1);
            y1 = std::max(y1, p.y1);
        }
        return { x0, y0, x1 - x0, y1 - y0 };
    }

    void chrome(Probe& P, std::string_view key)
    {
        Rig r(key, "panel");
        const Chrome panel = chromeOf(r.host.draw());
        const uint32_t latch = latchId(r);
        const funkgui::A11yItem* offItem = byId(r.host.accessibility(), latch);
        const bool offUnchecked = offItem != nullptr && !offItem->checked;
        P.in("chrome.prims", static_cast<double>(panel.rest.size()), 50.0, 4000.0);
        for (const char* id : { "chars.sidechain", "chars.colour" })
        {
            r.panel.setView(*ui::findView(id), true);
            r.settle();
            const Chrome ch = chromeOf(r.host.draw());
            const std::string k = std::string("chrome.") + id;
            P.eq(k + ".identical", b(same(panel.rest, ch.rest)), 1);
            const Box a = bounds(panel.latch), c = bounds(ch.latch);
            P.le(k + ".latch_rect_px", std::max({ std::fabs(a.x - c.x), std::fabs(a.y - c.y), std::fabs(a.w - c.w),
                                                  std::fabs(a.h - c.h) }), 0.01);
            const funkgui::A11yItem* onItem = byId(r.host.accessibility(), latch);
            P.eq(k + ".latch_on", b(offUnchecked && onItem != nullptr && onItem->checked && !same(panel.latch, ch.latch)),
                 1);
            P.eq(k + ".middle_differs", b(!same(panel.middle, ch.middle)), 1);
        }
        P.eq("chrome.nothing_written", b(nothingWritten(r.facade)), 1);
    }

    // ---- every way in and out ---------------------------------------------------------------------------------------------

    // A point of the band's HISTORY plot where no plot offers an interactive cursor (an "empty part", 02 §7.1).
    bool emptyBandPoint(Rig& r, funkgui::Point& out)
    {
        const funkgui::Rect& p = L::kBandHistory.plot;
        for (float y = p.y + 10.0f; y < p.bottom() - 10.0f; y += 12.0f)
            for (float x = p.x + 20.0f; x < p.right() - 20.0f; x += 40.0f)
            {
                r.host.move(x, y);
                if (r.panel.cursor() == funkgui::Cursor::normal)
                {
                    out = { x, y };
                    return true;
                }
            }
        return false;
    }

    void toggles(Probe& P, std::string_view key)
    {
        const funkgui::Point lp = centre(L::display::kCharacteristics);
        {   // the latch, clicked
            Rig r(key, "panel");
            r.host.click(lp.x, lp.y);
            r.settle();
            const bool on = r.chars() && r.facade.uiState().charExpanded;
            r.host.click(lp.x, lp.y);
            r.settle();
            const bool off = !r.chars() && !r.facade.uiState().charExpanded;
            P.eq("toggle.latch.click", b(on && off && nothingWritten(r.facade)), 1);
        }
        {   // Return and Space on the focused latch
            Rig r(key, "panel");
            r.focus(latchId(r));
            r.host.keys("return");
            r.settle();
            const bool on = r.chars() && r.facade.uiState().charExpanded;
            r.host.keys("return");
            r.settle();
            const bool off = !r.chars() && !r.facade.uiState().charExpanded;
            P.eq("toggle.latch.return", b(on && off && nothingWritten(r.facade)), 1);
            r.host.keys("space");
            r.settle();
            const bool on2 = r.chars();
            r.host.keys("space");
            r.settle();
            P.eq("toggle.latch.space", b(on2 && !r.chars() && nothingWritten(r.facade)), 1);
        }
        {   // the latch's a11y press
            Rig r(key, "panel");
            const uint32_t id = latchId(r);
            r.panel.a11yAction(id, funkgui::A11yAction::press, 0.0);
            r.settle();
            const bool on = r.chars() && r.facade.uiState().charExpanded;
            r.panel.a11yAction(id, funkgui::A11yAction::press, 0.0);
            r.settle();
            P.eq("toggle.latch.a11y", b(id != 0 && on && !r.chars() && nothingWritten(r.facade)), 1);
        }
        {   // a double-click on an empty part of the band
            Rig r(key, "panel");
            funkgui::Point p{};
            if (!emptyBandPoint(r, p))
            {
                P.harnessError("ui.charscreen: no empty point in the band's HISTORY");
                return;
            }
            r.host.doubleClick(p.x, p.y);
            r.settle();
            P.eq("toggle.band_dblclick", b(r.chars() && r.facade.uiState().charExpanded && nothingWritten(r.facade)), 1);
        }
        {   // a double-click on the band's threshold handle goes to the handle: PANEL stays
            Rig r(key, "panel");
            const float scale = static_cast<float>(r.ctx().meterScaleDb);
            const float tIn = fcdsp::analysis::inputThresholdDb(r.ctx().frame.eng);
            const float x = L::transferX(L::kBandTransfer, tIn, scale);
            const float y = L::kBandTransfer.level.y(tIn, scale);
            if (L::kBandTransfer.plot.contains({ x, y }))
            {
                r.host.move(x, y);
                r.settle();
                r.host.doubleClick(x, y);
                r.settle();
                P.eq("toggle.band_handle_dblclick_stays", b(!r.chars() && !r.facade.uiState().charExpanded), 1);
            }
        }
        {   // Esc: an open browser first, then the focus ring, then the screen (02 §7.1)
            Rig r(key, "chars.sidechain");
            const funkgui::Point name = centre(L::header::kModeName);
            r.host.click(name.x, name.y);
            r.settle();
            const bool opened = r.panel.overlay() == ui::Overlay::modeBrowser;
            r.host.keys("escape");
            r.settle();
            P.eq("toggle.esc.browser_first", b(opened && r.panel.overlay() == ui::Overlay::none && r.chars()), 1);
            r.host.keys("tab");
            const bool ring = r.ctx().focusVisible;
            r.host.keys("escape");
            r.settle();
            P.eq("toggle.esc.ring_second", b(ring && !r.ctx().focusVisible && r.chars()), 1);
            r.host.keys("escape");
            r.settle();
            P.eq("toggle.esc.leaves", b(!r.chars() && !r.facade.uiState().charExpanded && nothingWritten(r.facade)), 1);
        }
    }

    // ---- the crossfade ----------------------------------------------------------------------------------------------------

    void fade(Probe& P, std::string_view key)
    {
        // Frames with both screens drawn: the draw right after the click (a = 0) and after every tick while a < 1.
        int want = 1;
        for (float a = 0.0f; a < 1.0f; ++want)
        {
            a = funkgui::ease::toward(a, 1.0f, kDt, L::chars::kScreenFadeTau, L::chars::kScreenFadeSnap);
            if (!(a < 1.0f))
                break;
        }
        {
            Rig r(key, "panel");
            const funkgui::Point lp = centre(L::display::kCharacteristics);
            r.host.click(lp.x, lp.y);
            int both = 0;
            for (int k = 0; k < 200; ++k)
            {
                if (axes(r.host.draw(), ui::tag::histAxis) != 2)
                    break;
                ++both;
                r.host.tick(1, kDt);
            }
            P.eq("fade.frames", both, want);
            std::printf("NOTE     ui.charscreen: crossfade %d frames at 60 Hz (tau %.2f s, snap %g)\n", both,
                        static_cast<double>(L::chars::kScreenFadeTau), static_cast<double>(L::chars::kScreenFadeSnap));
        }
        {   // input goes to the target screen immediately: a tab click during the fade's first frame
            Rig r(key, "panel");
            const funkgui::Point lp = centre(L::display::kCharacteristics);
            r.host.click(lp.x, lp.y);
            const funkgui::Point tp = centre(L::kTabColour);
            r.host.click(tp.x, tp.y);
            P.eq("fade.input_immediate", b(r.panel.scTab() == fcmp::ScTab::colour && axes(r.host.draw(), ui::tag::histAxis) == 2),
                 1);
        }
    }

    // ---- SC|COLOUR ----------------------------------------------------------------------------------------------------------

    bool paneVisible(const std::vector<funkgui::A11yItem>& items, int plot)
    {
        for (const funkgui::A11yItem& it : items)
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::charScreen) && ui::plotIndexOf(it.id) == plot
                && it.visible)
                return true;
        return false;
    }

    void tabs(Probe& P, std::string_view key)
    {
        Rig r(key, "chars.sidechain");
        const auto shows = [&](fcmp::ScTab t) {
            const funkgui::PrimList& pl = r.host.draw();
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const bool sc = t == fcmp::ScTab::sidechain;
            return r.panel.scTab() == t && r.facade.uiState().scTab == t && (axisOf(pl, ui::tag::scAxis) != nullptr) == sc
                && (axisOf(pl, ui::tag::colourAxis) != nullptr) == !sc && paneVisible(items, 7) == sc
                && paneVisible(items, 8) == !sc;
        };
        P.eq("tab.initial", b(shows(fcmp::ScTab::sidechain)), 1);
        const funkgui::Point colour = centre(L::kTabColour), side = centre(L::kTabSidechain);
        r.host.click(colour.x, colour.y);
        r.settle();
        P.eq("tab.click.colour", b(shows(fcmp::ScTab::colour)), 1);
        r.host.click(side.x, side.y);
        r.settle();
        P.eq("tab.click.sidechain", b(shows(fcmp::ScTab::sidechain)), 1);

        const uint32_t group = ui::a11yId(ui::ViewIndex::charScreen, 1);
        const auto press = [&](const char* k) {
            r.host.keys(k);
            r.host.tick(1, kDt);
        };
        r.focus(group);
        press("right");
        const bool right = shows(fcmp::ScTab::colour);
        press("left");
        const bool left = shows(fcmp::ScTab::sidechain);
        press("end");
        const bool end = shows(fcmp::ScTab::colour);
        press("home");
        const bool home = shows(fcmp::ScTab::sidechain);
        P.eq("tab.keys", b(right && left && end && home), 1);

        r.panel.a11yAction(ui::a11yId(ui::ViewIndex::charScreen, 3), funkgui::A11yAction::press, 0.0);
        r.host.tick(1, kDt);
        const bool pressC = shows(fcmp::ScTab::colour);
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        const funkgui::A11yItem* cItem = byId(items, ui::a11yId(ui::ViewIndex::charScreen, 3));
        const funkgui::A11yItem* sItem = byId(items, ui::a11yId(ui::ViewIndex::charScreen, 2));
        const bool checked = cItem != nullptr && sItem != nullptr && cItem->checked && !sItem->checked;
        r.panel.a11yAction(ui::a11yId(ui::ViewIndex::charScreen, 2), funkgui::A11yAction::press, 0.0);
        r.host.tick(1, kDt);
        P.eq("tab.a11y", b(pressC && checked && shows(fcmp::ScTab::sidechain)), 1);
        P.eq("tab.nothing_written", b(nothingWritten(r.facade)), 1);
    }

    // ---- the Tab order (02 §7.5) --------------------------------------------------------------------------------------------

    void tabOrder(Probe& P, std::string_view key, const char* viewId, bool sidechain)
    {
        Rig r(key, viewId);
        const std::string k = std::string("taborder.") + (sidechain ? "sidechain" : "colour");
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        std::vector<uint32_t> order;
        bool wraps = false;
        for (int n = 0; n < 1024; ++n)
        {
            r.host.keys("tab");
            const uint32_t id = r.ctx().focus;
            if (!order.empty() && id == order.front())
            {
                wraps = true;
                break;
            }
            order.push_back(id);
        }
        const std::set<uint32_t> unique(order.begin(), order.end());
        P.eq(k + ".wraps", b(wraps), 1);
        P.eq(k + ".unique", b(unique.size() == order.size()), 1);

        // Sub-views in 02 §7.5's order: header, preset strip, display row, the screen, footer; never the band or slots.
        const std::array<ui::ViewIndex, 5> views { ui::ViewIndex::header, ui::ViewIndex::presetStrip,
                                                   ui::ViewIndex::displayRow, ui::ViewIndex::charScreen,
                                                   ui::ViewIndex::footer };
        std::size_t at = 0;
        bool inOrder = true;
        std::vector<uint32_t> display, screen, footer;
        for (const uint32_t id : order)
        {
            const int v = ui::viewIndexOf(id);
            while (at < views.size() && static_cast<int>(views[at]) != v)
                ++at;
            if (at == views.size())
            {
                inOrder = false;
                break;
            }
            if (v == static_cast<int>(ui::ViewIndex::displayRow))
                display.push_back(id);
            else if (v == static_cast<int>(ui::ViewIndex::charScreen))
                screen.push_back(id);
            else if (v == static_cast<int>(ui::ViewIndex::footer))
                footer.push_back(id);
        }
        P.eq(k + ".views", b(inOrder && !order.empty() && ui::viewIndexOf(order.front()) == 0), 1);
        const funkgui::A11yItem* first = byId(items, order.empty() ? 0 : order.front());
        P.eq(k + ".mode_latch_first", b(first != nullptr && first->role == funkgui::A11yRole::comboBox), 1);

        // Display row: QUALITY, LOOKAHEAD, DELTA, BYPASS, CHARACTERISTICS.
        const std::array<const char*, 5> wantDisplay { "Quality", "Lookahead budget", "DELTA", "BYPASS", "CHARACTERISTICS" };
        bool dOk = display.size() == wantDisplay.size();
        for (std::size_t i = 0; dOk && i < display.size(); ++i)
        {
            const funkgui::A11yItem* it = byId(items, display[i]);
            dOk = it != nullptr && it->title == wantDisplay[i];
        }
        P.eq(k + ".display", b(dOk), 1);

        // The screen: span, scale, SC|COLOUR, meter reset; then the handles.
        std::vector<uint32_t> chrome, handles;
        for (const uint32_t id : screen)
            (ui::isHandleId(id) ? handles : chrome).push_back(id);
        bool cOk = chrome.size() == 4;
        if (cOk)
        {
            const funkgui::A11yItem* span = byId(items, chrome[0]);
            const funkgui::A11yItem* scale = byId(items, chrome[1]);
            const funkgui::A11yItem* reset = byId(items, chrome[3]);
            cOk = span != nullptr && span->role == funkgui::A11yRole::radioGroup && ui::plotIndexOf(chrome[0]) == 0
               && scale != nullptr && scale->role == funkgui::A11yRole::radioGroup && ui::plotIndexOf(chrome[1]) == 1
               && chrome[2] == ui::a11yId(ui::ViewIndex::charScreen, 1)
               && reset != nullptr && reset->role == funkgui::A11yRole::button && ui::plotIndexOf(chrome[3]) == 2;
        }
        P.eq(k + ".screen_groups", b(cOk), 1);
        bool afterChrome = true;                                   // every handle stop after every chrome stop
        bool seenHandle = false;
        for (const uint32_t id : screen)
        {
            seenHandle = seenHandle || ui::isHandleId(id);
            afterChrome = afterChrome && !(seenHandle && !ui::isHandleId(id));
        }
        P.eq(k + ".handles_last", b(afterChrome), 1);

        std::vector<Pid> want;
        for (const Pid p : kHandlePids)
            if (stateOf(r, p) != fcdsp::SlotState::na && (p != Pid::schpf || sidechain))
                want.push_back(p);
        bool hOk = handles.size() == want.size();
        std::string got;
        for (std::size_t i = 0; i < handles.size(); ++i)
        {
            const funkgui::A11yItem* it = byId(items, handles[i]);
            const funkgui::A11yItem* slotIt = i < want.size() ? byId(items, slotItemId(want[i])) : nullptr;
            got += (it != nullptr ? it->title : std::string("?")) + ";";
            hOk = hOk && it != nullptr && slotIt != nullptr && it->title == slotIt->title
               && ui::plotIndexOf(handles[i]) == handlePlot(want[i]);
        }
        P.eq(k + ".handles", b(hOk), 1);
        if (!hOk)
            std::printf("NOTE     ui.charscreen: %s handle stops: %s\n", k.c_str(), got.c_str());
        const funkgui::A11yItem* theme = footer.size() == 1 ? byId(items, footer[0]) : nullptr;
        P.eq(k + ".theme_last", b(theme != nullptr && theme->role == funkgui::A11yRole::radioGroup
                                  && !order.empty() && order.back() == footer[0]), 1);
    }

    // ---- handles proxy their slots (02 §7.4, §7.5) ---------------------------------------------------------------------------

    bool sameItem(const funkgui::A11yItem& a, const funkgui::A11yItem& c, std::string& why)
    {
        const auto differ = [&](const char* f, const std::string& x, const std::string& y) {
            why = std::string(f) + " '" + x + "' vs '" + y + "'";
            return false;
        };
        if (a.role != c.role)
            return differ("role", std::to_string(static_cast<int>(a.role)), std::to_string(static_cast<int>(c.role)));
        if (a.title != c.title)
            return differ("title", a.title, c.title);
        if (a.description != c.description)
            return differ("description", a.description, c.description);
        if (a.help != c.help)
            return differ("help", a.help, c.help);
        if (a.value != c.value)
            return differ("value", a.value, c.value);
        if (a.enabled != c.enabled || a.readOnly != c.readOnly)
            return differ("state", std::to_string(a.enabled) + std::to_string(a.readOnly),
                          std::to_string(c.enabled) + std::to_string(c.readOnly));
        const auto near = [](double x, double y) { return std::fabs(x - y) <= 1e-9 * std::max(1.0, std::fabs(x)); };
        if (!near(a.v, c.v) || !near(a.lo, c.lo) || !near(a.hi, c.hi) || !near(a.step, c.step))
            return differ("value interface", std::to_string(a.v) + " " + std::to_string(a.lo) + ".." + std::to_string(a.hi),
                          std::to_string(c.v) + " " + std::to_string(c.lo) + ".." + std::to_string(c.hi));
        return true;
    }

    // The writes of one key sequence on a focused item: the port's plain value after each key, and the Pids written.
    struct KeyTrace
    {
        std::vector<float> plains;
        std::set<Pid>      pids;
    };

    KeyTrace keyTrace(Rig& r, uint32_t id, Pid p)
    {
        KeyTrace t;
        r.focus(id);
        for (const char* k : { "right", "home" })
        {
            r.host.keys(k);
            r.host.tick(1, kDt);
            t.plains.push_back(r.facade.fakePort(p).plain());
        }
        for (const fcmp::probe::FakeWrite& w : r.facade.writes())
            t.pids.insert(w.pid);
        return t;
    }

    void handles(Probe& P, std::string_view key)
    {
        for (const Pid p : kHandlePids)
        {
            const std::string k = "handle." + pidName(p);
            Rig panel(key, "panel");
            Rig chars(key, "chars.sidechain");
            const fcdsp::SlotState st = stateOf(chars, p);
            const std::vector<funkgui::A11yItem> ci = chars.host.accessibility();
            const funkgui::A11yItem* h = handleItem(ci, p);
            if (st == fcdsp::SlotState::na)
            {
                P.eq(k + ".na_absent", b(h == nullptr), 1);          // 02 §7.5: n/a handles are skipped
                continue;
            }
            const std::vector<funkgui::A11yItem> pi = panel.host.accessibility();
            const funkgui::A11yItem* s = byId(pi, slotItemId(p));
            std::string why;
            const bool same = h != nullptr && s != nullptr && sameItem(*h, *s, why);
            P.eq(k + ".a11y_equal", b(same), 1);
            if (!same)
                std::printf("NOTE     ui.charscreen: %s a11y: %s\n", k.c_str(), h == nullptr ? "no handle item" : why.c_str());
            if (h == nullptr || s == nullptr)
                continue;

            // → then Home: the handle writes exactly what the slot writes.
            const KeyTrace ts = keyTrace(panel, s->id, p);
            const KeyTrace th = keyTrace(chars, h->id, p);
            bool bits = ts.plains.size() == th.plains.size();
            for (std::size_t i = 0; bits && i < ts.plains.size(); ++i)
                bits = funkgui::ease::sameBits(ts.plains[i], th.plains[i]);
            P.eq(k + ".keys_equal", b(bits && ts.pids == th.pids), 1);
            const bool writable = st == fcdsp::SlotState::live || st == fcdsp::SlotState::stepped;
            P.eq(k + ".keys_port", b(writable ? (th.pids.size() <= 1 && (th.pids.empty() || *th.pids.begin() == p))
                                              : th.pids.empty()), 1);

            // A drag of the handle writes only its port, inside one gesture.
            Rig d(key, "chars.sidechain");
            const int plot = handlePlot(p);
            const funkgui::Rect& area = plot == 1 ? L::kCharsTransfer.plot : plot == 5 ? L::kStepAttack.plot
                                      : plot == 6 ? L::kStepRelease.plot : L::kSidechain.plot;
            d.host.move(area.centreX(), area.centreY());
            d.settle();
            const std::vector<funkgui::A11yItem> di = d.host.accessibility();
            const funkgui::A11yItem* dh = handleItem(di, p);
            if (dh == nullptr || (std::fabs(dh->bounds.w - area.w) < 0.5f && std::fabs(dh->bounds.h - area.h) < 0.5f))
            {
                std::printf("NOTE     ui.charscreen: %s: the handle is not drawn (outside its plot); no drag row\n",
                            k.c_str());
                continue;
            }
            bool overlaps = false;                                // another drawn handle of the plot within reach
            for (const Pid o : kHandlePids)
                if (o != p && handlePlot(o) == plot)
                    if (const funkgui::A11yItem* oh = handleItem(di, o); oh != nullptr
                        && !(std::fabs(oh->bounds.w - area.w) < 0.5f && std::fabs(oh->bounds.h - area.h) < 0.5f)
                        && std::fabs(oh->bounds.centreX() - dh->bounds.centreX()) < 10.0f
                        && std::fabs(oh->bounds.centreY() - dh->bounds.centreY()) < 10.0f)
                        overlaps = true;
            if (overlaps)
            {
                std::printf("NOTE     ui.charscreen: %s: another handle within 10 px; no drag row\n", k.c_str());
                continue;
            }
            // Towards the far side of the plot (a stepped handle must travel past a detent), then back if that wrote
            // nothing (the value sat at that end).
            const float x0 = dh->bounds.centreX(), y0 = dh->bounds.centreY();
            const bool rightward = x0 < area.centreX();
            const float x1 = plot == 1 ? std::clamp(x0 + (rightward ? 60.0f : -60.0f), area.x + 2.0f, area.right() - 2.0f)
                                       : (rightward ? area.right() - 2.0f : area.x + 2.0f);
            const float y1 = plot == 1 ? std::clamp(y0 + (y0 > area.centreY() ? -60.0f : 60.0f), area.y + 2.0f,
                                                    area.bottom() - 2.0f)
                                       : y0;
            fcmp::probe::FakePort& port = d.facade.fakePort(p);
            d.host.move(x0, y0);
            d.settle();
            d.facade.resetCounts();
            d.host.drag(x0, y0, x1, y1, 12);
            if (d.facade.writes().empty() && writable)
            {
                d.settle();
                d.facade.resetCounts();
                d.host.move(x0, y0);
                d.settle();
                d.host.drag(x0, y0, x0 - (x1 - x0), y0 - (y1 - y0), 12);
            }
            std::set<Pid> pids;
            for (const fcmp::probe::FakeWrite& w : d.facade.writes())
                pids.insert(w.pid);
            if (writable)
            {
                P.eq(k + ".drag_port", b(pids.size() == 1 && *pids.begin() == p), 1);
                P.eq(k + ".drag_gesture", b(port.begins() == 1 && port.ends() == 1 && !port.inGesture()
                                            && port.setsOutsideGesture() == 0 && d.facade.batches() == 0), 1);
            }
            else
            {
                P.eq(k + ".drag_refused", b(nothingWritten(d.facade)), 1);
            }
        }

        // The HISTORY threshold line: a vertical drag writes THRESHOLD only.
        Rig r(key, "chars.sidechain");
        const float scale = static_cast<float>(r.ctx().meterScaleDb);
        const float tIn = fcdsp::analysis::inputThresholdDb(r.ctx().frame.eng);
        const L::HistoryGeom& hg = L::kCharsHistory;
        if (tIn > L::kLevelTopDb - scale + 2.0f && tIn < L::kLevelTopDb - 6.0f)
        {
            const float x = hg.plot.x + 100.0f, y = hg.level.y(tIn, scale);
            const fcdsp::SlotState st = stateOf(r, Pid::thr);
            const bool writable = st == fcdsp::SlotState::live || st == fcdsp::SlotState::stepped;
            r.host.move(x, y);
            r.facade.resetCounts();
            r.host.drag(x, y, x, y - 40.0f, 10);
            std::set<Pid> pids;
            for (const fcmp::probe::FakeWrite& w : r.facade.writes())
                pids.insert(w.pid);
            P.eq("handle.thr_line.port", b(writable ? pids.size() == 1 && *pids.begin() == Pid::thr : pids.empty()), 1);
        }
    }

    // ---- CONTROL PATH over scripted columns --------------------------------------------------------------------------------

    struct Window { int64_t e0 = 0, e1 = 0; double xl = 0, xr = 0; };

    // 02 §9.6's column rule, written here from the spec (not HistoryStore's helper): W = span / columns ms, columns cut at
    // absolute multiples of W, "now" = head at the right edge, offset by the partial column's phase.
    std::vector<Window> windows(uint64_t head, int spanTenths, const L::ControlPathGeom& g)
    {
        std::vector<Window> v;
        const double w = static_cast<double>(spanTenths) * 100.0 / static_cast<double>(g.columns);
        const double colW = static_cast<double>(g.colWidth);
        const double k = std::floor(static_cast<double>(head) / w);
        const double phase = (static_cast<double>(head) - k * w) / w;
        const double right = static_cast<double>(g.plot.right()), left = static_cast<double>(g.plot.x);
        for (int j = g.columns + 1; j >= -1; --j)
        {
            Window c;
            if (j >= 0)
            {
                c.e0 = static_cast<int64_t>(std::ceil((k - 1.0 - j) * w));
                c.e1 = static_cast<int64_t>(std::ceil((k - j) * w));
                c.xr = right - (phase + j) * colW;
                c.xl = c.xr - colW;
            }
            else
            {
                c.e0 = static_cast<int64_t>(std::ceil(k * w));
                c.e1 = static_cast<int64_t>(head);
                c.xl = right - phase * colW;
                c.xr = right;
            }
            if (c.xr <= left || c.e1 <= c.e0 || c.e1 <= 0)
                continue;
            c.xl = std::max(c.xl, left);
            v.push_back(c);
        }
        return v;
    }

    struct Scripted
    {
        FakeFacade& facade;
        const fcdsp::ModeDescriptor& desc;
        const fcdsp::InternalSpec* hist = nullptr;
        int histIndex = -1;
        uint8_t slot = 0;
        fcdsp::UiFrame frame{};
        std::vector<fcdsp::HistoryColumn> cols;                   // every column pushed, by store index

        fcdsp::HistoryColumn column(uint64_t e) const
        {
            fcdsp::HistoryColumn c{};
            const double t = static_cast<double>(e);
            c.inPeakDb = -18.0f;
            c.outPeakDb = -22.0f;
            c.detMaxDb = -18.0f;
            c.grMaxDb = 2.0f + 10.0f * static_cast<float>(std::fmod(t, 700.0) / 700.0);
            c.grMinDb = c.grMaxDb - 0.5f - 0.2f * static_cast<float>(e % 3);
            c.tgtMaxDb = c.grMaxDb + (e % 2 == 0 ? 0.5f : 1.5f);
            if (hist != nullptr)
                c.internal0 = hist->lo + (hist->hi - hist->lo) * (0.1f + 0.8f * static_cast<float>(std::fmod(t, 1300.0) / 1300.0));
            uint32_t bits = static_cast<uint32_t>((e / 97) % 4);
            if ((e / 400) % 3 == 0) bits |= 1u << 2;
            if ((e / 250) % 4 == 1) bits |= 1u << 3;
            if ((e / 333) % 5 == 2) bits |= 1u << 4;
            if (e % 1000 < 30)      bits |= 1u << 6;
            c.bits = bits | (static_cast<uint32_t>(slot) << 8);
            return c;
        }

        void push(int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const fcdsp::HistoryColumn c = column(static_cast<uint64_t>(cols.size()));
                cols.push_back(c);
                facade.pushColumn(c);
            }
        }
        void publish() { facade.publish(frame); }
    };

    struct Expected
    {
        double xl = 0, xr = 0;
        float gr = 0, grMin = 0, tgt = 0, internal = 0;
        uint32_t phase = 0, events = 0;
        bool data = false;
    };

    std::vector<Expected> expectedColumns(const std::vector<fcdsp::HistoryColumn>& store, uint64_t head, int spanTenths)
    {
        std::vector<Expected> v;
        for (const Window& w : windows(head, spanTenths, L::kControlPath))
        {
            Expected c;
            c.xl = w.xl;
            c.xr = w.xr;
            for (int64_t e = std::max<int64_t>(w.e0, 0); e < w.e1 && e < static_cast<int64_t>(store.size()); ++e)
            {
                const fcdsp::HistoryColumn& s = store[static_cast<std::size_t>(e)];
                if (!c.data || s.grMaxDb > c.gr)
                {
                    c.gr = s.grMaxDb;
                    c.phase = s.bits & 3u;
                }
                c.grMin = c.data ? std::min(c.grMin, s.grMinDb) : s.grMinDb;
                c.tgt = c.data ? std::max(c.tgt, s.tgtMaxDb) : s.tgtMaxDb;
                c.events |= s.bits;
                c.internal = s.internal0;
                c.data = true;
            }
            if (c.data)
                v.push_back(c);
        }
        return v;
    }

    // Sample points (x, y) of an AREA strip's edge (top or bottom), one per segment end.
    std::vector<std::pair<float, float>> edgePoints(const std::vector<const funkgui::Prim*>& prims, bool bottom,
                                                    bool clearFill)
    {
        std::vector<std::pair<float, float>> v;
        for (const funkgui::Prim* p : prims)
        {
            if (kindOf(*p) != funkgui::PrimKind::area || ((p->c0 >> 24) == 0) != clearFill)
                continue;
            const Col c = colOf(*p);
            v.emplace_back(c.x0, bottom ? c.bot0 : c.top0);
            v.emplace_back(c.x1, bottom ? c.bot1 : c.top1);
        }
        return v;
    }

    int breaks(const std::vector<const funkgui::Prim*>& prims)
    {
        std::vector<Col> v;
        for (const funkgui::Prim* p : prims)
            if (kindOf(*p) == funkgui::PrimKind::area)
                v.push_back(colOf(*p));
        std::sort(v.begin(), v.end(), [](const Col& a, const Col& c) { return a.x0 < c.x0; });
        int n = 0;
        for (std::size_t i = 1; i < v.size(); ++i)
            if (v[i].x0 > v[i - 1].x1 + 0.01f)
                ++n;
        return n;
    }

    std::set<float> segmentEdges(const std::vector<const funkgui::Prim*>& prims)
    {
        std::set<float> s;
        for (const funkgui::Prim* p : prims)
            if (kindOf(*p) == funkgui::PrimKind::area)
            {
                s.insert(std::round(p->x0 * 1000.0f) / 1000.0f);
                s.insert(std::round(p->x1 * 1000.0f) / 1000.0f);
            }
        return s;
    }

    void controlPath(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeDescriptor& desc = *entry.desc;
        Rig r(desc.key, "chars.sidechain");
        Scripted s{ r.facade, desc, nullptr, -1, 0, {}, {} };
        for (std::size_t i = 0; i < desc.internals.size(); ++i)
            if (desc.internals[i].history)
            {
                s.hist = &desc.internals[i];
                s.histIndex = static_cast<int>(i);
            }
        s.slot = r.slot();
        s.frame = FakeFacade::quietFrame(s.slot, r.ctx().frame.res.eng);
        s.frame.flags |= fcdsp::kUiLive;
        std::printf("NOTE     ui.charscreen: history internal %s\n", s.hist != nullptr ? s.hist->name : "(none)");

        for (int k = 0; k < 330; ++k)
        {
            s.push(kColumnsPerFrame);
            if (s.hist != nullptr)
                s.frame.internals[s.histIndex] = s.cols.back().internal0;
            s.publish();
            r.host.tick(1, kDt);
        }
        r.host.tick(16, kDt);                                     // the title regenerates (<= 4 Hz)
        const ui::PanelContext& ctx = r.ctx();
        const funkgui::PrimList& pl = r.host.draw();
        const funkgui::AxisRec* ax = axisOf(pl, ui::tag::cpAxis);
        if (ax == nullptr || !ax->hasX || !ax->hasY)
        {
            P.harnessError("ui.charscreen: no CP_AXIS on the Characteristics screen");
            return;
        }
        P.eq("cp.store.count", static_cast<int64_t>(ctx.history.count()), static_cast<int64_t>(s.cols.size()));
        const std::vector<Expected> want = expectedColumns(s.cols, ctx.history.count(), ctx.historySpanTenths);

        // x-aligned with HISTORY: the same segment edges as HIST_GR, and the spec's columns.
        const std::vector<const funkgui::Prim*> tgt = tagged(pl, ui::tag::cpTarget);
        const std::vector<const funkgui::Prim*> app = tagged(pl, ui::tag::cpApplied);
        const std::set<float> cpEdges = segmentEdges(tgt), histEdges = segmentEdges(tagged(pl, ui::tag::histGr));
        P.eq("cp.columns.hist_aligned", b(!cpEdges.empty() && cpEdges == histEdges), 1);
        std::set<float> specEdges;
        for (const Expected& c : want)
            specEdges.insert(std::round(static_cast<float>(0.5 * (c.xl + c.xr)) * 1000.0f) / 1000.0f);
        int centresFound = 0;
        for (const float e : specEdges)
            centresFound += cpEdges.count(e) > 0 ? 1 : 0;
        P.eq("cp.columns.spec_centres", centresFound, static_cast<int64_t>(specEdges.size()));

        // Values at every column centre, through CP_AXIS.
        const auto dbAt = [&](float py) { return toValue(ax->y, py); };
        const auto check = [&](const char* row, const std::vector<std::pair<float, float>>& pts, auto field) {
            double worst = 0.0;
            int n = 0;
            for (const Expected& c : want)
            {
                const auto xc = static_cast<float>(0.5 * (c.xl + c.xr));
                for (const auto& [x, y] : pts)
                    if (std::fabs(x - xc) < 1e-3f)
                    {
                        worst = std::max(worst, static_cast<double>(std::fabs(dbAt(y) - field(c))));
                        ++n;
                        break;
                    }
            }
            P.le(std::string("cp.") + row + "_db", n > 0 ? worst : 99.0, 0.01);
            P.eq(std::string("cp.") + row + "_centres", n, static_cast<int64_t>(want.size()));
        };
        check("applied", edgePoints(app, true, false), [](const Expected& c) { return c.gr; });
        check("min", edgePoints(app, true, true), [](const Expected& c) { return c.grMin; });
        check("target", edgePoints(tgt, true, true), [](const Expected& c) { return c.tgt; });

        // The internal lane: internal0 through the declared lo…hi.
        if (s.hist != nullptr)
        {
            const funkgui::Rect& il = L::kControlPath.internalLane;
            const std::vector<std::pair<float, float>> pts = edgePoints(tagged(pl, ui::tag::cpInternal), false, true);
            double worst = 0.0;
            int n = 0;
            for (const Expected& c : want)
            {
                const auto xc = static_cast<float>(0.5 * (c.xl + c.xr));
                const float v = std::clamp((c.internal - s.hist->lo) / (s.hist->hi - s.hist->lo), 0.0f, 1.0f);
                for (const auto& [x, y] : pts)
                    if (std::fabs(x - xc) < 1e-3f)
                    {
                        worst = std::max(worst, static_cast<double>(std::fabs(y - (il.bottom() - v * il.h))));
                        ++n;
                        break;
                    }
            }
            P.le("cp.internal_px", n > 0 ? worst : 99.0, 0.5);
            P.eq("cp.internal_centres", n, static_cast<int64_t>(want.size()));
            // The newest point equals the frame's internal (the lane's live value), through the declared range.
            const float fv = std::clamp((s.frame.internals[s.histIndex] - s.hist->lo) / (s.hist->hi - s.hist->lo), 0.0f, 1.0f);
            float newestY = -1.0f, newestX = -1.0f;
            for (const auto& [x, y] : pts)
                if (x > newestX)
                {
                    newestX = x;
                    newestY = y;
                }
            P.le("cp.internal.newest_px", newestY < 0.0f ? 99.0 : std::fabs(newestY - (il.bottom() - fv * il.h)), 0.5);
        }

        // Phase lane: the merged runs (newest first, <= 40) and their inks.
        {
            const funkgui::Theme th = funkgui::Theme::graphite();
            struct Run { double x0, x1; uint32_t ph; };
            std::vector<Run> runs;
            for (auto it = want.rbegin(); it != want.rend() && static_cast<int>(runs.size()) < L::band::kStateLaneMaxRuns; ++it)
            {
                if (it->phase == 0)
                    continue;
                if (!runs.empty() && runs.back().ph == it->phase && std::fabs(runs.back().x0 - it->xr) < 1e-3)
                    runs.back().x0 = it->xl;
                else
                    runs.push_back({ it->xl, it->xr, it->phase });
            }
            const std::vector<const funkgui::Prim*> drawn = tagged(pl, ui::tag::cpPhase);
            bool ok = drawn.size() == runs.size();
            for (std::size_t i = 0; ok && i < drawn.size(); ++i)
            {
                const Box bx = boxOf(*drawn[i]);
                const funkgui::Col ink = runs[i].ph == 1 ? th.ink70 : runs[i].ph == 2 ? th.ink100 : th.ink32;
                ok = std::fabs(bx.x - static_cast<float>(runs[i].x0)) < 0.01f
                  && std::fabs(bx.x + bx.w - static_cast<float>(runs[i].x1)) < 0.01f
                  && drawn[i]->c0 == pack(funkgui::premix(th.ground, ink, 1.0f));
            }
            P.eq("cp.phase.runs", b(ok), 1);
        }

        // Event rows: each row's stripes are the runs of columns with its bit.
        const std::vector<const funkgui::Prim*> ev = tagged(pl, ui::tag::cpEvents);
        for (std::size_t r0 = 0; r0 < L::controlPath::kEvents.size(); ++r0)
        {
            const L::controlPath::EventRow& row = L::controlPath::kEvents[r0];
            const float y = L::kControlPath.eventsLane.y + L::kControlPath.eventRowPitch * static_cast<float>(r0);
            std::vector<std::pair<double, double>> runs;
            for (const Expected& c : want)
            {
                if ((c.events & row.bit) == 0)
                    continue;
                if (!runs.empty() && std::fabs(runs.back().second - c.xl) < 1e-3)
                    runs.back().second = c.xr;
                else
                    runs.emplace_back(c.xl, c.xr);
            }
            std::vector<std::pair<double, double>> got;
            for (const funkgui::Prim* p : ev)
                if (const Box bx = boxOf(*p); std::fabs(bx.y - y) < 0.01f)
                    got.emplace_back(bx.x, bx.x + bx.w);
            bool ok = got.size() == runs.size();
            for (std::size_t i = 0; ok && i < got.size(); ++i)
                ok = std::fabs(got[i].first - runs[i].first) < 0.01 && std::fabs(got[i].second - runs[i].second) < 0.01;
            std::string name = row.label;
            std::replace(name.begin(), name.end(), ' ', '_');
            std::transform(name.begin(), name.end(), name.begin(), [](char ch) {
                return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            });
            P.eq("cp.events." + name, b(ok && !runs.empty()), 1);
        }

        // The image title names the target, the applied GR and the internal with its live value.
        {
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* img = byId(items, ui::plotIdBase(ui::ViewIndex::charScreen, 4) + 1);
            bool ok = img != nullptr && img->title.find("Control path: target ") == 0;
            if (ok && s.hist != nullptr)
            {
                char v[40];
                const int dp = std::clamp(static_cast<int>(s.hist->decimals), 0, 6);
                if (funkgui::fmt::db(s.frame.internals[s.histIndex], dp, v, sizeof v) < 0)
                    v[0] = '\0';
                std::string want0 = std::string(s.hist->name) + " " + v;
                if (s.hist->unit != nullptr && s.hist->unit[0] != '\0')
                    want0 += std::string(" ") + s.hist->unit;
                ok = img->title.find(want0) != std::string::npos;
                if (!ok)
                    std::printf("NOTE     ui.charscreen: CONTROL PATH title '%s', wanted '%s'\n", img->title.c_str(),
                                want0.c_str());
            }
            P.eq("cp.title", b(ok), 1);
        }

        // Press and hold on HISTORY: both strips hold, and the freeze cursor crosses both plots at one x.
        {
            const L::HistoryGeom& hg = L::kCharsHistory;
            float fx = hg.plot.x + 200.0f, fy = hg.plot.y + 30.0f;
            for (; fy < hg.plot.bottom() - 10.0f; fy += 12.0f)
            {
                r.host.move(fx, fy);
                if (r.panel.cursor() == funkgui::Cursor::normal)
                    break;
            }
            const std::set<float> before = segmentEdges(tagged(r.host.draw(), ui::tag::cpTarget));
            funkgui::PointerEvent e;
            e.x = fx;
            e.y = fy;
            r.panel.pointerDown(e);
            s.push(kColumnsPerFrame);
            s.publish();
            r.host.tick(1, kDt);
            s.push(kColumnsPerFrame);
            s.publish();
            r.host.tick(1, kDt);
            const funkgui::PrimList& fl = r.host.draw();
            const std::set<float> during = segmentEdges(tagged(fl, ui::tag::cpTarget));
            std::vector<float> xs;
            for (const funkgui::Prim* p : tagged(fl, ui::tag::freezeCursor))
                xs.push_back(boxOf(*p).x);
            P.eq("cp.freeze.holds", b(ctx.freeze.active && !before.empty() && before == during), 1);
            P.eq("cp.freeze.cursor_both", b(xs.size() == 2 && std::fabs(xs[0] - xs[1]) <= 0.5f), 1);
            r.panel.pointerUp(e);
            s.publish();
            r.host.tick(1, kDt);
            P.eq("cp.freeze.released", b(!ctx.freeze.active
                                         && segmentEdges(tagged(r.host.draw(), ui::tag::cpTarget)) != during), 1);
        }

        // Stale: no publish for more than 0.5 s; arriving columns do not scroll the strip.
        {
            r.host.tick(40, kDt);
            const std::set<float> before = segmentEdges(tagged(r.host.draw(), ui::tag::cpTarget));
            s.push(300);
            r.host.tick(1, kDt);
            const std::set<float> after = segmentEdges(tagged(r.host.draw(), ui::tag::cpTarget));
            P.eq("cp.stale.holds", b(!ctx.frame.live && !before.empty() && before == after), 1);
        }

        // A lap (5000 columns between two drains): one gap, every CONTROL PATH trace breaks there.
        {
            s.push(5000);
            s.publish();
            r.host.tick(1, kDt);
            for (int k = 0; k < 12; ++k)
            {
                s.push(kColumnsPerFrame);
                s.publish();
                r.host.tick(1, kDt);
            }
            const funkgui::PrimList& gl = r.host.draw();
            int gapsInCp = 0;
            for (const funkgui::Prim* p : tagged(gl, ui::tag::gap))
                gapsInCp += p->y0 >= L::kControlPath.plot.y && p->y1 <= L::kControlPath.plot.bottom() ? 1 : 0;
            P.eq("cp.gap.drawn", b(gapsInCp > 0), 1);
            P.eq("cp.gap.target_broken", b(breaks(tagged(gl, ui::tag::cpTarget)) >= 1), 1);
            P.eq("cp.gap.applied_broken", b(breaks(tagged(gl, ui::tag::cpApplied)) >= 1), 1);
            if (s.hist != nullptr)
                P.eq("cp.gap.internal_broken", b(breaks(tagged(gl, ui::tag::cpInternal)) >= 1), 1);
        }
    }

    // ---- READOUTS over scripted frames ---------------------------------------------------------------------------------------

    std::string num(float v, int dp, bool sign = false)
    {
        if (std::isfinite(v) && v <= -199.0f)
            return kDash;
        char s[32];
        if (funkgui::fmt::db(v, dp, s, sizeof s) < 0)
            return kDash;
        std::string t = s;
        if (sign && std::isfinite(v) && std::round(static_cast<double>(v) * std::pow(10.0, dp)) > 0.0)
            t = "+" + t;
        return t;
    }

    std::string ratio(float r)
    {
        if (!std::isfinite(r) || std::fabs(r) >= 1.0e4f)
            return "\xE2\x88\x9E:1";
        return num(r, std::fabs(r) < 9.95f ? 1 : 0) + ":1";
    }

    std::string timeText(float ms)
    {
        char s[32];
        const char* unit = "";
        if (!(ms > 0.0f) || funkgui::fmt::seconds(ms * 1.0e-3f, s, sizeof s, &unit) < 0)
            return kDash;
        return std::string(s) + " " + unit;
    }

    struct Rows
    {
        std::vector<std::string> names, values;
        std::string title;
    };

    Rows readRows(const Rig& r)
    {
        Rows out;
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        const uint32_t base = ui::plotIdBase(ui::ViewIndex::charScreen, 3);
        if (const funkgui::A11yItem* img = byId(items, base + 1))
            out.title = img->title;
        for (uint32_t i = 0; i < 18; ++i)
            if (const funkgui::A11yItem* it = byId(items, base + 2 + i);
                it != nullptr && it->role == funkgui::A11yRole::staticText && it->visible)
            {
                out.names.push_back(it->title);
                out.values.push_back(it->value);
            }
        return out;
    }

    // The values 02 §7.3 prescribes for frame u, computed here from the frame, the store and resolve() + overlaySmoothed.
    std::vector<std::string> expectedValues(const Rig& r, const fcdsp::ModeEntry& entry, const fcdsp::UiFrame& u,
                                            bool live, std::string& pairs)
    {
        const fcdsp::ModeDescriptor& d = *entry.desc;
        std::vector<std::string> v;
        pairs.clear();
        const std::size_t n = 10 + std::min<std::size_t>(d.internals.size(), 8);
        if (!live)
            return std::vector<std::string>(n, kDash);
        fcdsp::Resolution res;
        fcdsp::resolve(entry, r.facade.currentRaw(), res);
        fcdsp::EngineParams eng = res.eng;
        fcdsp::overlaySmoothed(u, eng);
        const int lane = u.appliedGrDb[1] > u.appliedGrDb[0] ? 1 : 0;
        const auto ul = static_cast<std::size_t>(lane);
        float cx = u.curveXDb[ul], tgt = u.targetGrDb[ul];
        const ui::HistoryStore& h = r.ctx().history;
        for (uint64_t e = h.count() >= 10 ? h.count() - 10 : 0; e < h.count(); ++e)
            if (!ui::HistoryStore::isGap(h.at(e)))
            {
                cx = std::max(cx, h.at(e).detMaxDb);
                tgt = std::max(tgt, h.at(e).tgtMaxDb);
            }
        const bool detPair = eng.link < 1.0f && std::fabs(u.curveXDb[0] - u.curveXDb[1]) > 0.1f;
        const bool grPair = eng.link < 1.0f && std::fabs(u.appliedGrDb[0] - u.appliedGrDb[1]) > 0.1f;
        if (detPair || grPair)
            pairs = (u.flags & fcdsp::kUiMidSide) != 0 ? "M/S" : "L/R";
        v.push_back(detPair ? num(u.curveXDb[0], 1) + "/" + num(u.curveXDb[1], 1) : num(cx, 1));
        v.push_back(num(cx - fcdsp::analysis::inputThresholdDb(eng), 1, true));
        v.push_back(num(std::max(tgt, 0.0f), 1));
        v.push_back(grPair ? num(u.appliedGrDb[0], 1) + "/" + num(u.appliedGrDb[1], 1) : num(u.appliedGrDb[ul], 1));
        v.push_back(d.stage2 == fcdsp::Stage2Kind::none ? std::string(kDash) : num(u.s2GrDb[ul], 1));
        v.push_back(ratio(fcdsp::analysis::localRatio(entry, eng, cx)));
        const auto law = [&](Pid p) {
            const fcdsp::ParamSpec* sp = res.view.spec[fcdsp::idx(p)];
            return fcdsp::lawFactor(sp != nullptr ? sp->law : fcdsp::TimeLaw::expDb);
        };
        v.push_back(timeText(u.attackNowMs[ul] * law(Pid::atk)));
        v.push_back(timeText(u.releaseNowMs[ul] * law(Pid::rel)));
        v.push_back(num(u.crestDb[ul], 1));
        v.push_back(kPhaseNames[(u.flags >> (16u + 2u * static_cast<uint32_t>(lane))) & 3u]);
        for (std::size_t i = 0; i < d.internals.size() && i < 8; ++i)
        {
            std::string t = num(u.internals[i], std::clamp(static_cast<int>(d.internals[i].decimals), 0, 6));
            if (d.internals[i].unit != nullptr && d.internals[i].unit[0] != '\0')
                t += std::string(" ") + d.internals[i].unit;
            v.push_back(t);
        }
        return v;
    }

    std::size_t glyphs(const std::string& s)
    {
        std::size_t n = 0;
        for (const char c : s)
            n += ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u && c != ' ') ? 1 : 0;
        return n;
    }

    void readouts(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeDescriptor& desc = *entry.desc;
        Rig r(desc.key, "chars.sidechain");
        const uint8_t slot = r.slot();
        const fcdsp::UiFrame quiet = FakeFacade::quietFrame(slot, r.ctx().frame.res.eng);

        // Names: rows 1–10, then the declared internals.
        {
            const Rows got = readRows(r);
            std::vector<std::string> want(L::readouts::kNames.begin(), L::readouts::kNames.end());
            for (std::size_t i = 0; i < desc.internals.size() && i < 8; ++i)
                want.emplace_back(desc.internals[i].name);
            P.eq("readouts.names", b(got.names == want), 1);
        }

        struct Case { const char* name; fcdsp::UiFrame f; bool live; const char* pairs; int envelope; };
        fcdsp::UiFrame f = quiet;
        f.flags |= fcdsp::kUiLive | (1u << 16);                   // lane 0 in ATTACK
        f.curveXDb[0] = -14.23f;
        f.curveXDb[1] = -14.26f;
        f.targetGrDb[0] = 5.12f;
        f.targetGrDb[1] = 5.01f;
        f.appliedGrDb[0] = 3.81f;
        f.appliedGrDb[1] = 3.76f;
        f.s2GrDb[0] = f.s2GrDb[1] = 1.24f;
        f.attackNowMs[0] = f.attackNowMs[1] = 0.31f;
        f.releaseNowMs[0] = f.releaseNowMs[1] = 1250.0f;
        f.crestDb[0] = f.crestDb[1] = 12.34f;
        for (std::size_t i = 0; i < desc.internals.size() && i < 8; ++i)
            f.internals[i] = desc.internals[i].lo + (desc.internals[i].hi - desc.internals[i].lo) * (0.4237f + 0.05f * static_cast<float>(i));

        fcdsp::UiFrame lr = f;                                    // unlinked, lanes apart: L/R pairs
        lr.link = 0.5f;
        lr.curveXDb[1] = -15.04f;
        lr.appliedGrDb[1] = 4.13f;
        lr.flags = (lr.flags & ~(3u << 16)) | (3u << 18);         // lane 1 (the larger GR) in RELEASE
        fcdsp::UiFrame ms = lr;
        ms.flags |= fcdsp::kUiMidSide;
        fcdsp::UiFrame linked = lr;                               // linked: no pairs, whatever the lanes say
        linked.link = 1.0f;
        fcdsp::UiFrame silent = f;                                // not live
        silent.flags &= ~static_cast<uint32_t>(fcdsp::kUiLive);
        fcdsp::UiFrame other = f;                                 // the audio runs another Mode
        other.modeSlot = static_cast<uint16_t>(slot == 0 ? 1 : 0);

        const std::array<Case, 7> cases { {
            { "single", f, true, "", 0 },
            { "envelope", f, true, "", 1 },
            { "lr", lr, true, "L/R", 0 },
            { "ms", ms, true, "M/S", 0 },
            { "linked", linked, true, "", 0 },
            { "not_live", silent, false, "", 0 },
            { "other_mode", other, false, "", 0 },
        } };
        for (const Case& c : cases)
        {
            if (c.envelope != 0)
            {
                // 11 columns: the oldest (DET +6 dB) is outside the 10 ms envelope, the rest (+2 dB, target +1) inside.
                for (int i = 0; i < 11; ++i)
                {
                    fcdsp::HistoryColumn col{};
                    col.inPeakDb = col.outPeakDb = -18.0f;
                    col.detMaxDb = c.f.curveXDb[0] + (i == 0 ? 6.0f : 2.0f);
                    col.tgtMaxDb = c.f.targetGrDb[0] + 1.0f;
                    col.grMaxDb = col.grMinDb = c.f.appliedGrDb[0];
                    col.bits = static_cast<uint32_t>(slot) << 8;
                    r.facade.pushColumn(col);
                }
            }
            r.facade.publish(c.f);
            r.host.tick(1, kDt);
            std::string pairs;
            const std::vector<std::string> want = expectedValues(r, entry, c.f, c.live, pairs);
            const Rows got = readRows(r);
            const std::string k = std::string("readouts.") + c.name;
            const bool ok = got.values == want;
            P.eq(k + ".values", b(ok), 1);
            if (!ok)
                for (std::size_t i = 0; i < std::max(want.size(), got.values.size()); ++i)
                    std::printf("NOTE     ui.charscreen: %s row %zu: got '%s' want '%s'\n", k.c_str(), i + 1,
                                i < got.values.size() ? got.values[i].c_str() : "<none>",
                                i < want.size() ? want[i].c_str() : "<none>");
            P.eq(k + ".pairs", b(pairs == c.pairs && (pairs.empty() ? got.title == "Readouts"
                                                                    : got.title == "Readouts, pairs " + pairs)), 1);
            if (c.envelope != 0)
                P.eq(k + ".det_held", b(!got.values.empty() && got.values[0] == num(c.f.curveXDb[0] + 2.0f, 1)), 1);

            // Every row fits (name, 6 px, value within the area) and the drawn value has the text's glyphs.
            const funkgui::PrimList& pl = r.host.draw();
            bool fits = true, drawn = true;
            for (std::size_t i = 0; i < got.values.size(); ++i)
            {
                const float w = funkgui::text::width(atlas(), got.names[i].c_str(), funkgui::type::kMicro) + 6.0f
                              + funkgui::text::width(atlas(), got.values[i].c_str(), funkgui::type::kMicro);
                fits = fits && w <= L::kReadouts.area.w;
                const float y0 = L::kReadouts.area.y + L::kReadouts.rowPitch * static_cast<float>(i);
                std::size_t n = 0;
                for (const funkgui::Prim* p : tagged(pl, ui::tag::readoutValue))
                    n += (0.5f * (p->y0 + p->y1) >= y0 && 0.5f * (p->y0 + p->y1) < y0 + L::kReadouts.rowPitch) ? 1 : 0;
                drawn = drawn && n == glyphs(got.values[i]);
            }
            P.eq(k + ".fit", b(fits), 1);
            P.eq(k + ".drawn_glyphs", b(drawn), 1);
        }
        P.eq("readouts.nothing_written", b(nothingWritten(r.facade)), 1);
    }

    // ---- METERS: labels and the SC meter -------------------------------------------------------------------------------------

    void meters(Probe& P, std::string_view key)
    {
        const L::MeterGeom& g = L::kCharsMeters;
        struct Ext { float l, r, centre, bar; };
        std::vector<Ext> ex;
        for (int i = 0; i < g.nLabels; ++i)
        {
            const L::MeterGeom::Label& l = g.labels[static_cast<std::size_t>(i)];
            const float w = funkgui::text::width(atlas(), l.text, funkgui::type::kMicro);
            ex.push_back({ l.centreX - 0.5f * w, l.centreX + 0.5f * w, l.centreX, g.bars[static_cast<std::size_t>(i)].r.centreX() });
            std::printf("NOTE     ui.charscreen: meter label %-3s width %.3f px, centre %.2f (bar %.2f)\n", l.text,
                        static_cast<double>(w), static_cast<double>(l.centreX),
                        static_cast<double>(g.bars[static_cast<std::size_t>(i)].r.centreX()));
        }
        float gap = 99.0f, off = 0.0f;
        for (std::size_t i = 0; i < ex.size(); ++i)
        {
            off = std::max(off, std::fabs(ex[i].centre - ex[i].bar));
            if (i > 0)
                gap = std::min(gap, ex[i].l - ex[i - 1].r);
        }
        P.ge("meters.labels.gap_px", gap, kMinLabelGap);
        P.le("meters.labels.bar_px", off, kLabelBarPx);

        // Never touching TRANSFER's labels either: "0 DB <LAW>" starts on the 0 dB tick and can run into the meters'
        // column (RMS, TUBE; further at S = 72). Where a glyph quad of each row overlaps horizontally, the quads (which
        // include the AA apron) must not meet vertically. At S = 48 and 72.
        for (const int scale : { L::kDefaultScaleDb, 72 })
        {
            funkgui::UiPreferences::get().setInt("meterScaleDb", scale);
            Rig t(key, "chars.sidechain");
            const funkgui::PrimList& tl = t.host.draw();
            std::vector<Box> meterQ, transferQ;
            for (const funkgui::Prim* p : tagged(tl, ui::tag::axisLabel))
            {
                if (p->x1 < L::kCharsTransfer.plot.x || p->x0 > g.area.right() + 8.0f)
                    continue;
                if (p->y0 >= g.labelY && p->y0 < g.labelY + 3.0f)
                    meterQ.push_back({ p->x0, p->y0, p->x1 - p->x0, p->y1 - p->y0 });
                else if (p->y0 >= L::kCharsTransfer.labelY && p->y0 < L::kCharsTransfer.labelY + 3.0f)
                    transferQ.push_back({ p->x0, p->y0, p->x1 - p->x0, p->y1 - p->y0 });
            }
            float clear = 99.0f, xClear = 99.0f;
            for (const Box& m : meterQ)
                for (const Box& q : transferQ)
                {
                    xClear = std::min(xClear, std::max(q.x - (m.x + m.w), m.x - (q.x + q.w)));
                    if (q.x < m.x + m.w && m.x < q.x + q.w)
                        clear = std::min(clear, q.y - (m.y + m.h));
                }
            std::printf("NOTE     ui.charscreen: S = %d: TRANSFER labels end %.2f px %s the meter labels\n", scale,
                        static_cast<double>(std::fabs(xClear)), xClear >= 0.0f ? "before" : "under");
            P.eq("meters.labels.quads.s" + std::to_string(scale), b(meterQ.size() == 9 && !transferQ.empty()), 1);
            P.ge("meters.labels.transfer_clear_px.s" + std::to_string(scale), clear, 0.25);
            P.eq("meters.labels.scale.s" + std::to_string(scale), t.ctx().meterScaleDb, scale);
        }
        funkgui::UiPreferences::get().setInt("meterScaleDb", L::kDefaultScaleDb);

        Rig r(key, "chars.sidechain");
        const funkgui::PrimList& pl = r.host.draw();

        // SC: "–" without an external key; the bar with one.
        const auto scText = [](const funkgui::PrimList& l) {
            int t = 0;
            for (const funkgui::Prim* p : tagged(l, ui::tag::meterSc))
                t += kindOf(*p) == funkgui::PrimKind::text ? 1 : 0;
            return t;
        };
        const auto scBar = [](const funkgui::PrimList& l) {
            int t = 0;
            for (const funkgui::Prim* p : tagged(l, ui::tag::meterSc))
                t += kindOf(*p) == funkgui::PrimKind::rrect ? 1 : 0;
            return t;
        };
        P.eq("meters.sc.dash_without_key", b(scText(pl) == 1 && scBar(pl) == 0), 1);
        fcdsp::UiFrame f = FakeFacade::quietFrame(r.slot(), r.ctx().frame.res.eng);
        f.flags |= fcdsp::kUiLive | fcdsp::kUiExtKeyActive;
        f.scPeakDb[0] = f.scPeakDb[1] = -12.0f;
        f.inPeakDb[0] = f.inPeakDb[1] = -12.0f;
        r.facade.publish(f);
        r.host.tick(1, kDt);
        const funkgui::PrimList& on = r.host.draw();
        P.eq("meters.sc.bar_with_key", b(scText(on) == 0 && scBar(on) >= 1), 1);
    }

    // ---- the stage-1 curve (02 §7.3) ---------------------------------------------------------------------------------------

    void stage(Probe& P, const fcdsp::ModeEntry& entry)
    {
        Rig r(entry.desc->key, "chars.sidechain");
        const funkgui::PrimList& pl = r.host.draw();
        const std::vector<const funkgui::Prim*> segs = tagged(pl, ui::tag::stageCurve);
        const bool twoStage = entry.desc->stage2 != fcdsp::Stage2Kind::none;
        P.eq("stage.drawn", b(segs.empty() != twoStage), 1);
        {
            Rig band(entry.desc->key, "panel");
            P.eq("stage.not_on_band", b(tagged(band.host.draw(), ui::tag::stageCurve).empty()), 1);
        }
        if (!twoStage)
            return;
        const funkgui::AxisRec* level = axisOf(pl, ui::tag::level);
        if (level == nullptr)
        {
            P.harnessError("ui.charscreen: no LEVEL axis on the Characteristics screen");
            return;
        }
        const fcdsp::EngineParams& eng = r.ctx().frame.eng;
        const funkgui::Rect& p = L::kCharsTransfer.plot;
        double worst = 0.0;
        int n = 0;
        for (const funkgui::Prim* sp : segs)
        {
            const Seg s = segOf(*sp);
            for (const auto& [px, py] : { std::pair{ s.x0, s.y0 }, std::pair{ s.x1, s.y1 } })
            {
                const float xd = toValue(level->x, px);
                float gain = 0.0f;
                fcdsp::analysis::staticGain(entry, eng, std::span<const float>(&xd, 1), std::span<float>(&gain, 1),
                                            fcdsp::analysis::CurveOpts{ false, false });
                const float yWant = toPx(level->y, xd + gain - eng.preGainDb);
                if (yWant < p.y - 0.5f || yWant > p.bottom() + 0.5f)
                    continue;                                     // a clipped end
                worst = std::max(worst, static_cast<double>(std::fabs(py - yWant)));
                ++n;
            }
        }
        P.le("stage.vertex_px", n > 0 ? worst : 99.0, 0.5);
    }
}

FCMP_PROBE(ui, charscreen)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.charscreen writes UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.charscreen: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    funkgui::UiPreferences::get().setInt("historySpanTenths", L::kDefaultSpanTenths);
    funkgui::UiPreferences::get().setInt("meterScaleDb", L::kDefaultScaleDb);
    chrome(P, C.key);
    toggles(P, C.key);
    fade(P, C.key);
    tabs(P, C.key);
    tabOrder(P, C.key, "chars.sidechain", true);
    tabOrder(P, C.key, "chars.colour", false);
    handles(P, C.key);
    controlPath(P, *entry);
    readouts(P, *entry);
    meters(P, C.key);
    stage(P, *entry);
    return P.finish();
}
