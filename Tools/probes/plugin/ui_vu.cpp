// FCMP_PROBE layer=ui name=vu scope=mode timeout=300
//
// ui.vu.<key> (S12.6 UF2; ADR-72): the band's HISTORY · VU switch and the GR VU meter, driven through the Panel API and
// HeadlessHost input over a FakeFacade with scripted UiFrames and HistoryColumns (Panel{skipHint, syncPreview}, dpi 2).
// Everything is measured on what the Panel draws (the needle's GR_NEEDLE segment, the scale's GRID ticks and AXIS_LABEL
// glyphs) against the law recomputed here, never read back from the view. Spec rows only.
//
//   toggle.*    the switch: two text cells on the span cells' row with their height and width rule (w(label) in kCaption
//               + 14, rounded to an even number; 4 px apart), the HISTORY label on the old caption's x 40 (<= 0.25 px);
//               the default is HISTORY; a click on VU (and on HISTORY at its left edge, x 34, left of the band region)
//               writes only the "grView" preference, no parameter and no batch; under VU the span cells, "S", the time
//               labels and the HIST_AXIS record are gone and a click where the span cells were does nothing; Tab reaches
//               the "GR view" group right before the span group (under VU: before TRANSFER's scale group, the span group
//               gone); →/←/End/Home on it switch; a11y: a radioGroup "GR view" of radioButtons "History" and "VU meter"
//               (checked follows the preference), setValue and press switch it, the history image gives way to a
//               progressBar "Gain reduction VU meter" reading "0.0 dB" at rest, and the a11y revision moves; hovering
//               a cell puts the switch's spec line on the footer.
//   scale.*     the law: every tick lies on θ(dB) = −E + 2E · (g(dB) − g(−20)) / (g(+3) − g(−20)), g = 10^(dB/20),
//               E = layout::vu::kEndDeg (<= 0.01°), 16 ticks (8 labelled, +1…+3, 5 minor); every label's angle (the
//               centre of its glyphs, seen from the pivot) matches its dB within 0.25°; the pivot on the plot's centre x
//               and the ends symmetric; the needle at rest on 70.8 % of full scale (±0.2 %); labels >= 2 px apart; every
//               primitive of the meter inside the plot (clear of the state lane and the time labels); the meter's box
//               centred in the plot (top and bottom margins within 1.5 px).
//   step.*      the ballistics, on the needle's drawn angle, 1 ms per frame (one column per frame), timed from the
//               needle's first motion (it is drawn layout::vu::kShowLagMs behind the audio: that lag, ±1 ms): 0 → 12 dB
//               GR reaches 99 % of its travel in 300 ms ± 10 % with 1–1.5 % overshoot, and settles on the law's angle;
//               12 → 0 (release) the same; the same step at 60 Hz frames (17/16 columns each) draws states of the 1 ms
//               run's trajectory within 3 ms of the same audio time (<= 1e-3° off it): frame-rate invariant; and audio in
//               23.2 ms host blocks (1024 samples at 44.1 kHz) at 60 Hz frames moves the needle every frame of the swing,
//               smoothly (the change of the per-frame travel <= 25 % of the largest).
//   rest.*      before any frame, and after 0 dB GR columns, the needle is exactly on 0 dB.
//   stale.*     (ADR-69) from 12 dB GR held, the audio stops: the needle holds while the feed is fresh (0.5 s), then
//               falls back to rest from the first stale frame (drawn at no lag) in 300 ms ± 10 % with 1–1.5 % overshoot,
//               lands exactly on 0 dB, and the panel settles to the idle rate; nothing dims (the needle's and the labels'
//               colours are the live ones).
//   history.*   HISTORY's columns keep advancing under VU: a panel that shows VU for 2 s of audio and switches back draws
//               HISTORY exactly as a twin that never left it (every HIST_*, GAP, MODE_TICK and STATE_LANE primitive), with
//               no GAP and its newest column at "now".
//   outside.*   nothing outside the plot rectangle (+1 px for its edges' anti-aliasing), the caption row and the time-label
//               row changes between HISTORY and VU (rasterised at dpi 2, pixel for pixel), and switching back restores
//               the whole frame exactly.
//
// The probe writes UiPreferences ("grView", the span): it refuses to run without FCMP_PREFS_DIR (CTest sets a sandbox).
//
// Review pictures (not a test): `fcmp_probe_plugin ui.vu --mode <key> -- --png-dir <dir>` writes vu-<key>-<theme>-
// {history,rest,3db,12db}.png (GRAPHITE and PAPER) and the 0 → 12 dB GR step every 50 ms from the needle's first
// motion to 600 ms, vu-<key>-step-<ms>.png (GRAPHITE), at dpi 2.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/SoftRaster.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    namespace V = fcmp::ui::layout::vu;
    using fcmp::probe::FakeFacade;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kMs = 0.001f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    constexpr const char* kViewKey = "grView";
    constexpr const char* kViewSpec = "GR VIEW   HISTORY \xC2\xB7 VU   CLICK A VIEW (EVERY WINDOW)";

    int b(bool v) { return v ? 1 : 0; }

    void setView(int v) { funkgui::UiPreferences::get().setInt(kViewKey, v); }
    int  prefView() { return funkgui::UiPreferences::get().getInt(kViewKey, -1, -1, 99); }

    // ---- the law, recomputed here (ADR-72) --------------------------------------------------------------------------

    double gain(double db) { return std::pow(10.0, db / 20.0); }

    double lawDeg(double db)
    {
        const double e = static_cast<double>(V::kEndDeg);
        const double lo = gain(static_cast<double>(V::kLowDb)), hi = gain(static_cast<double>(V::kHighDb));
        return -e + 2.0 * e * (gain(db) - lo) / (hi - lo);
    }

    // The angle of point (x, y) seen from the pivot, degrees from vertical, + to the right.
    double angleOf(double x, double y)
    {
        return std::atan2(x - static_cast<double>(V::kPivot.x), static_cast<double>(V::kPivot.y) - y) * 180.0
             / std::numbers::pi;
    }

    // ---- primitives -------------------------------------------------------------------------------------------------

    struct Seg { double x0, y0, x1, y1; uint32_t c0; };

    bool isKind(const funkgui::Prim& p, funkgui::PrimKind k) { return static_cast<int>(p.d2[2] + 0.5f) == static_cast<int>(k); }
    bool isLive(const funkgui::Prim& p) { return (static_cast<uint32_t>(p.d2[3] + 0.5f) & funkgui::pflag::live) != 0; }

    Seg segOf(const funkgui::Prim& p)
    {
        const double cx = 0.5 * (static_cast<double>(p.x0) + static_cast<double>(p.x1));
        const double cy = 0.5 * (static_cast<double>(p.y0) + static_cast<double>(p.y1));
        return { cx + static_cast<double>(p.d1[0]), cy + static_cast<double>(p.d1[1]), cx + static_cast<double>(p.d1[2]),
                 cy + static_cast<double>(p.d1[3]), p.c0 };
    }

    bool inRect(const funkgui::Prim& p, const funkgui::Rect& r)
    {
        return p.x0 >= r.x && p.x1 <= r.right() && p.y0 >= r.y && p.y1 <= r.bottom();
    }

    bool centreIn(const funkgui::Prim& p, const funkgui::Rect& r)
    {
        return r.contains({ 0.5f * (p.x0 + p.x1), 0.5f * (p.y0 + p.y1) });
    }

    std::vector<const funkgui::Prim*> tagged(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        std::vector<const funkgui::Prim*> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == t)
                v.push_back(&p);
        return v;
    }

    // The VU needle: the live GR_NEEDLE segment inside HISTORY's plot (TRANSFER's GR_NEEDLE is an rrect elsewhere).
    std::optional<Seg> needleOf(const funkgui::PrimList& pl)
    {
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == ui::tag::grNeedle && isKind(p, funkgui::PrimKind::segment) && isLive(p)
                && centreIn(p, L::kBandHistory.plot))
                return segOf(p);
        return std::nullopt;
    }

    // The needle's angle: of its end farther from the pivot. NaN when there is none.
    double needleDeg(const funkgui::PrimList& pl)
    {
        const std::optional<Seg> s = needleOf(pl);
        if (!s)
            return std::nan("");
        const auto d = [](double x, double y) {
            return std::hypot(x - static_cast<double>(V::kPivot.x), y - static_cast<double>(V::kPivot.y));
        };
        return d(s->x0, s->y0) > d(s->x1, s->y1) ? angleOf(s->x0, s->y0) : angleOf(s->x1, s->y1);
    }

    // ---- the rig ----------------------------------------------------------------------------------------------------

    struct Rig
    {
        explicit Rig(std::string_view key, int theme = 0)
            : facade(key), panel(facade, kProbeOptions), host(panel, theme, 2.0f)
        {
            settled = host.settle(kMaxSettle, kDt);
            facade.resetCounts();
            base = FakeFacade::quietFrame(ctx().frame.res.view.slot, ctx().frame.res.eng);
            base.flags |= fcdsp::kUiLive;
        }

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        const ui::PanelContext& ctx() const { return panel.context(); }

        // n columns of `grDb` (phase bits `phase`), then a live frame.
        void feed(int n, float grDb, uint32_t phase = 0)
        {
            const auto slot = static_cast<uint32_t>(ctx().frame.res.view.slot);
            for (int i = 0; i < n; ++i)
            {
                fcdsp::HistoryColumn c{};
                c.inPeakDb = -12.0f + static_cast<float>(i % 5);
                c.outPeakDb = c.inPeakDb - grDb;
                c.detMaxDb = c.inPeakDb;
                c.grMaxDb = grDb;
                c.grMinDb = grDb * 0.5f;
                c.tgtMaxDb = grDb;
                c.bits = (phase & 3u) | (slot << 8);
                facade.pushColumn(c);
            }
            fcdsp::UiFrame f = base;
            f.appliedGrDb[0] = f.appliedGrDb[1] = grDb;
            f.blockMaxGrDb[0] = f.blockMaxGrDb[1] = grDb;
            f.inPeakDb[0] = f.inPeakDb[1] = -12.0f;
            f.outPeakDb[0] = f.outPeakDb[1] = -12.0f - grDb;
            facade.publish(f);
        }

        // One 60 Hz frame of audio: 17 or 16 columns (1000 / 60 ms), as the timeline's clock would see them.
        void frame60(float grDb, uint32_t phase = 0)
        {
            ++frames;
            const int n = static_cast<int>((frames * 1000) / 60 - ((frames - 1) * 1000) / 60);
            feed(n, grDb, phase);
            host.tick(1, kDt);
        }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
        fcdsp::UiFrame        base{};
        int                   settled = 0;
        long                  frames = 0;
    };

    const funkgui::A11yItem* findItem(const std::vector<funkgui::A11yItem>& items, funkgui::A11yRole role,
                                      std::string_view title)
    {
        for (const funkgui::A11yItem& it : items)
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::band) && it.role == role && it.title == title)
                return &it;
        return nullptr;
    }

    bool hasHistAxis(const funkgui::PrimList& pl)
    {
        return std::any_of(pl.axes.begin(), pl.axes.end(), [](const funkgui::AxisRec& x) { return x.tag == ui::tag::histAxis; });
    }

    // HISTORY's image item (its bounds are the plot).
    bool plotImage(const std::vector<funkgui::A11yItem>& items)
    {
        const funkgui::Rect& p = L::kBandHistory.plot;
        return std::any_of(items.begin(), items.end(), [&](const funkgui::A11yItem& it) {
            return it.role == funkgui::A11yRole::image && ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::band)
                && it.bounds.x == p.x && it.bounds.y == p.y && it.bounds.w == p.w && it.bounds.h == p.h;
        });
    }

    std::vector<uint32_t> tabStops(Rig& r)
    {
        std::vector<uint32_t> stops;
        for (int i = 0; i < 1024; ++i)
        {
            r.host.keys("tab");
            const uint32_t f = r.ctx().focus;
            if (!stops.empty() && f == stops.front())
                break;
            stops.push_back(f);
        }
        return stops;
    }

    int indexOf(const std::vector<uint32_t>& v, uint32_t x)
    {
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i] == x)
                return static_cast<int>(i);
        return -1;
    }

    // ---- toggle.* ---------------------------------------------------------------------------------------------------

    void toggleRows(Probe& P, std::string_view key)
    {
        const funkgui::FontAtlasSdf& atlas = funkgui::FontService::get().atlas();
        const auto cellW = [&](const char* label) {
            const float w = funkgui::text::width(atlas, label, funkgui::type::kCaption) + 14.0f;
            return 2.0f * std::round(w * 0.5f);
        };
        const funkgui::Rect& h = V::kViewCells[0];
        const funkgui::Rect& v = V::kViewCells[1];
        const funkgui::Rect& s0 = L::kBandHistory.spanCells[0];
        const float wHist = funkgui::text::width(atlas, "HISTORY", funkgui::type::kCaption);
        std::printf("NOTE     ui.vu: w(HISTORY) %.3f, w(VU) %.3f in kCaption -> cells %.0f, %.0f\n",
                    static_cast<double>(wHist), static_cast<double>(funkgui::text::width(atlas, "VU", funkgui::type::kCaption)),
                    static_cast<double>(cellW("HISTORY")), static_cast<double>(cellW("VU")));
        P.eq("toggle.cells.row", b(h.y == s0.y && h.h == s0.h && v.y == s0.y && v.h == s0.h), 1);
        P.eq("toggle.cells.width_rule", b(h.w == cellW("HISTORY") && v.w == cellW("VU")), 1);
        P.near("toggle.cells.gap_px", static_cast<double>(v.x - h.right()), 4.0, 1e-6);
        P.near("toggle.label_x_px", static_cast<double>(h.centreX() - 0.5f * wHist), static_cast<double>(L::kBandHistory.caption.x),
               0.25);

        Rig r(key);
        {
            const funkgui::PrimList& pl = r.host.draw();
            P.eq("toggle.history", b(prefView() == V::kHistory && !needleOf(pl) && hasHistAxis(pl)), 1);
        }
        const std::vector<funkgui::A11yItem> items0 = r.host.accessibility();
        const uint32_t rev0 = r.panel.a11yRevision();
        const funkgui::A11yItem* group = findItem(items0, funkgui::A11yRole::radioGroup, "GR view");
        const funkgui::A11yItem* spanGroup = findItem(items0, funkgui::A11yRole::radioGroup, "History span");
        const funkgui::A11yItem* scaleGroup = nullptr;
        for (const funkgui::A11yItem& it : items0)
            if (it.role == funkgui::A11yRole::radioGroup && ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::band)
                && group != nullptr && it.id != group->id && spanGroup != nullptr && it.id != spanGroup->id)
                scaleGroup = &it;
        {
            const funkgui::A11yItem* hb = findItem(items0, funkgui::A11yRole::radioButton, "History");
            const funkgui::A11yItem* vb = findItem(items0, funkgui::A11yRole::radioButton, "VU meter");
            P.eq("toggle.a11y.group", b(group != nullptr && group->value == "History"), 1);
            P.eq("toggle.a11y.cells", b(hb != nullptr && vb != nullptr && group != nullptr && hb->parent == group->id
                                        && vb->parent == group->id && hb->checked && !vb->checked
                                        && hb->bounds.x == h.x && vb->bounds.x == v.x), 1);
            P.eq("toggle.a11y.history_image", b(plotImage(items0)), 1);
        }

        // Tab: the GR view group right before the span group.
        const std::vector<uint32_t> stops = tabStops(r);
        const int gi = group != nullptr ? indexOf(stops, group->id) : -1;
        const int si = spanGroup != nullptr ? indexOf(stops, spanGroup->id) : -1;
        P.eq("toggle.tab.history", b(gi >= 0 && si == gi + 1), 1);

        // Hover: the spec line.
        r.host.move(v.centreX(), v.centreY());
        r.host.tick(1, kDt);
        P.eq("toggle.spec_line", b(std::string_view(r.ctx().hand.spec) == kViewSpec), 1);

        // Click VU: the preference only.
        r.facade.resetCounts();
        const int batches0 = r.host.log.batches;
        r.host.click(v.centreX(), v.centreY());
        r.host.move(700.0f, 500.0f);
        r.host.tick(1, kDt);
        const funkgui::PrimList& vl = r.host.draw();
        P.eq("toggle.click_vu", b(prefView() == V::kVu && needleOf(vl).has_value()), 1);
        P.eq("toggle.click_vu.no_param", b(r.facade.writes().empty() && r.facade.batches() == 0
                                           && r.host.log.batches == batches0), 1);
        {
            int spanText = 0, timeLabels = 0;
            for (const funkgui::Prim& p : vl.prims)
            {
                if (!isKind(p, funkgui::PrimKind::text))
                    continue;
                for (const funkgui::Rect& c : L::kBandHistory.spanCells)
                    if (centreIn(p, c))
                        ++spanText;
                if (p.y0 >= L::kBandHistory.timeLabelY - 2.0f && p.y1 <= L::kBandHistory.timeLabelY + 16.0f
                    && p.x0 >= L::kBandHistory.plot.x - 2.0f && p.x1 <= L::kBandHistory.plot.right() + 2.0f)
                    ++timeLabels;
            }
            P.eq("toggle.vu.span_hidden", spanText, 0);
            int unit = 0;
            for (const funkgui::Prim* q : tagged(vl, ui::tag::unitWord))
                if (q->x1 <= L::kBandHistory.unitRight + 2.0f)
                    ++unit;
            P.eq("toggle.vu.unit_hidden", unit, 0);
            P.eq("toggle.vu.time_labels_hidden", timeLabels, 0);
            P.eq("toggle.vu.no_hist_axis", b(!hasHistAxis(vl)), 1);
        }
        const std::vector<funkgui::A11yItem> items1 = r.host.accessibility();
        {
            const funkgui::A11yItem* meter = findItem(items1, funkgui::A11yRole::progressBar, "Gain reduction VU meter");
            const funkgui::A11yItem* g1 = findItem(items1, funkgui::A11yRole::radioGroup, "GR view");
            const funkgui::A11yItem* vb = findItem(items1, funkgui::A11yRole::radioButton, "VU meter");
            P.eq("toggle.a11y.vu", b(meter != nullptr && meter->value == "0.0 dB" && meter->readOnly && g1 != nullptr
                                     && g1->value == "VU meter" && vb != nullptr && vb->checked), 1);
            P.eq("toggle.a11y.vu_no_span", b(findItem(items1, funkgui::A11yRole::radioGroup, "History span") == nullptr), 1);
            P.eq("toggle.a11y.vu_no_image", b(!plotImage(items1)), 1);
            P.eq("toggle.a11y.revision", b(r.panel.a11yRevision() != rev0), 1);
        }

        // Under VU a click where the span cells were does nothing.
        const int span0 = r.ctx().historySpanTenths;
        r.host.click(L::kBandHistory.spanCells[3].centreX(), L::kBandHistory.spanCells[3].centreY());
        r.host.tick(1, kDt);
        P.eq("toggle.vu.span_inert", b(r.ctx().historySpanTenths == span0 && prefView() == V::kVu
                                       && funkgui::UiPreferences::get().getInt("historySpanTenths", 0, 0, 1000) == span0), 1);

        // Tab under VU: the group, then TRANSFER's scale group; no span group.
        {
            Rig t(key);
            const std::vector<uint32_t> vs = tabStops(t);
            const int g2 = group != nullptr ? indexOf(vs, group->id) : -1;
            P.eq("toggle.tab.vu", b(g2 >= 0 && (spanGroup == nullptr || indexOf(vs, spanGroup->id) < 0)
                                    && scaleGroup != nullptr && indexOf(vs, scaleGroup->id) == g2 + 1), 1);
        }

        // HISTORY again by a click at x 34 (left of the band region x 40).
        r.host.click(h.x + 1.0f, h.centreY());
        r.host.tick(1, kDt);
        P.eq("toggle.click_history_left_edge", b(prefView() == V::kHistory && !needleOf(r.host.draw())), 1);

        // Keys on the focused group.
        {
            Rig k(key);
            for (int i = 0; i < 64 && (group == nullptr || k.ctx().focus != group->id); ++i)
                k.host.keys("tab");
            k.host.keys("right");
            const bool right = prefView() == V::kVu;
            k.host.keys("left");
            const bool left = prefView() == V::kHistory;
            k.host.keys("end");
            const bool end = prefView() == V::kVu;
            k.host.keys("home");
            const bool home = prefView() == V::kHistory;
            P.eq("toggle.keys", b(group != nullptr && k.ctx().focus == group->id && right && left && end && home), 1);
            // a11y: setValue(1) on the group, press on cell 0.
            if (group != nullptr)
            {
                k.panel.a11yAction(group->id, funkgui::A11yAction::setValue, 1.0);
                const bool set = prefView() == V::kVu;
                k.panel.a11yAction(group->id + 1, funkgui::A11yAction::press, 0.0);
                P.eq("toggle.a11y.actions", b(set && prefView() == V::kHistory), 1);
            }
            else
            {
                P.eq("toggle.a11y.actions", 0, 1);
            }
        }
        setView(V::kHistory);
    }

    // ---- scale.* ----------------------------------------------------------------------------------------------------

    void scaleRows(Probe& P, std::string_view key)
    {
        setView(V::kVu);
        Rig r(key);
        const funkgui::PrimList& pl = r.host.draw();
        const funkgui::Rect& plot = L::kBandHistory.plot;

        // Ticks: GRID segments inside the plot longer than an arc chord (2.6 px).
        std::vector<double> ticks;
        for (const funkgui::Prim& p : pl.prims)
        {
            if (p.tag != ui::tag::grid || !isKind(p, funkgui::PrimKind::segment) || !centreIn(p, plot))
                continue;
            const Seg s = segOf(p);
            if (std::hypot(s.x1 - s.x0, s.y1 - s.y0) > 3.5)
                ticks.push_back(angleOf(0.5 * (s.x0 + s.x1), 0.5 * (s.y0 + s.y1)));
        }
        std::vector<double> want;
        for (const L::AxisLabel& l : V::kLabels)
            want.push_back(lawDeg(static_cast<double>(l.value)));
        for (const float db : V::kOverDb)
            want.push_back(lawDeg(static_cast<double>(db)));
        for (const float db : V::kMinorDb)
            want.push_back(lawDeg(static_cast<double>(db)));
        std::sort(ticks.begin(), ticks.end());
        std::sort(want.begin(), want.end());
        P.eq("scale.ticks.count", static_cast<int64_t>(ticks.size()), 16);
        double worstTick = ticks.size() == want.size() ? 0.0 : 99.0;
        for (std::size_t i = 0; i < ticks.size() && i < want.size(); ++i)
            worstTick = std::max(worstTick, std::fabs(ticks[i] - want[i]));
        P.le("scale.ticks.deg", worstTick, 0.01);

        // Labels: AXIS_LABEL glyphs inside the plot, in drawing order, one run of glyphs per label.
        std::vector<const funkgui::Prim*> glyphs;
        for (const funkgui::Prim* p : tagged(pl, ui::tag::axisLabel))
            if (isKind(*p, funkgui::PrimKind::text) && centreIn(*p, plot))
                glyphs.push_back(p);
        std::size_t at = 0;
        double worstLabel = 0.0;
        std::vector<std::array<double, 4>> boxes;
        for (const L::AxisLabel& l : V::kLabels)
        {
            int n = 0;
            for (const char* q = l.text; *q != '\0';)
            {
                (void)funkgui::text::decodeUtf8(q);
                ++n;
            }
            if (at + static_cast<std::size_t>(n) > glyphs.size())
            {
                worstLabel = 99.0;
                break;
            }
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
            for (int k = 0; k < n; ++k)
            {
                const funkgui::Prim& g = *glyphs[at + static_cast<std::size_t>(k)];
                x0 = std::min(x0, static_cast<double>(g.x0));
                y0 = std::min(y0, static_cast<double>(g.y0));
                x1 = std::max(x1, static_cast<double>(g.x1));
                y1 = std::max(y1, static_cast<double>(g.y1));
            }
            at += static_cast<std::size_t>(n);
            boxes.push_back({ x0, y0, x1, y1 });
            const double a = angleOf(0.5 * (x0 + x1), 0.5 * (y0 + y1));
            worstLabel = std::max(worstLabel, std::fabs(a - lawDeg(static_cast<double>(l.value))));
            std::printf("NOTE     ui.vu: label %-6s at %8.3f deg, law %8.3f deg\n", l.text, a,
                        lawDeg(static_cast<double>(l.value)));
        }
        P.eq("scale.labels.count", static_cast<int64_t>(glyphs.size()), static_cast<int64_t>(at));
        P.le("scale.labels.deg", worstLabel, 0.25);
        double closest = 99.0;
        for (std::size_t i = 0; i < boxes.size(); ++i)
            for (std::size_t j = i + 1; j < boxes.size(); ++j)
            {
                const double dx = std::max({ 0.0, boxes[j][0] - boxes[i][2], boxes[i][0] - boxes[j][2] });
                const double dy = std::max({ 0.0, boxes[j][1] - boxes[i][3], boxes[i][1] - boxes[j][3] });
                closest = std::min(closest, std::hypot(dx, dy));
            }
        P.ge("scale.labels.apart_px", closest, 2.0);

        P.eq("scale.pivot_centred", b(V::kPivot.x == plot.centreX()), 1);
        P.le("scale.symmetric_deg", std::fabs(lawDeg(static_cast<double>(V::kLowDb)) + lawDeg(static_cast<double>(V::kHighDb))),
             1e-9);
        // The needle at rest, as a fraction of full scale (d = 0 .. +3 dB), from the drawn −20 and +3 ticks.
        const double rest = needleDeg(pl);
        const double lo = gain(static_cast<double>(V::kLowDb) - static_cast<double>(V::kHighDb));
        const double tLo = ticks.empty() ? 0.0 : ticks.front(), tHi = ticks.empty() ? 1.0 : ticks.back();
        const double frac = lo + (rest - tLo) / (tHi - tLo) * (1.0 - lo);
        P.near("scale.zero_fraction", frac, gain(-3.0), 0.002);

        // Everything of the meter inside the plot (inset by its 1 px frame), clear of the state lane and time labels;
        // its box centred.
        const funkgui::Rect inner = plot.reduced(1.0f);
        bool inside = true;
        double top = 1e9, bottom = -1e9;
        for (const funkgui::Prim& p : pl.prims)
        {
            const bool mine = (p.tag == ui::tag::grid || p.tag == ui::tag::axisLabel || p.tag == ui::tag::caption
                               || p.tag == ui::tag::grNeedle) && centreIn(p, inner);
            if (!mine)
                continue;
            inside = inside && inRect(p, inner);
            top = std::min(top, static_cast<double>(p.y0));
            bottom = std::max(bottom, static_cast<double>(p.y1));
        }
        P.eq("scale.inside_plot", b(inside), 1);
        P.le("scale.centred_px", std::fabs((top - static_cast<double>(plot.y)) - (static_cast<double>(plot.bottom()) - bottom)),
             1.5);
        std::printf("NOTE     ui.vu: meter box y %.2f .. %.2f in the plot %.0f .. %.0f\n", top, bottom,
                    static_cast<double>(plot.y), static_cast<double>(plot.bottom()));
        setView(V::kHistory);
    }

    // ---- step.*, rest.*, stale.* ------------------------------------------------------------------------------------

    struct Response
    {
        double t99Ms = -1.0;                                     // first 99 % crossing (interpolated)
        double overshoot = 0.0;                                  // max beyond the target, fraction of the travel
        double finalDeg = 0.0;
    };

    // The index of the last sample before the needle moves (angles[0] is at rest before the change), or 0. The needle
    // is drawn kShowLagMs behind the audio, so the step is timed from its motion, 1 ms per sample.
    std::size_t onsetOf(const std::vector<double>& angles)
    {
        for (std::size_t i = 1; i < angles.size(); ++i)
            if (std::fabs(angles[i] - angles[0]) > 1e-7)
                return i - 1;
        return 0;
    }

    // angles[i] is the needle 1 ms apart; t = 0 at the onset (onsetOf).
    Response measure(const std::vector<double>& angles, double from, double to)
    {
        Response r;
        const double travel = to - from;
        const std::size_t i0 = onsetOf(angles);
        double peak = 0.0;
        for (std::size_t i = i0 + 1; i < angles.size(); ++i)
        {
            const double f0 = (angles[i - 1] - from) / travel, f1 = (angles[i] - from) / travel;
            if (r.t99Ms < 0.0 && f1 >= 0.99)
                r.t99Ms = static_cast<double>(i - 1 - i0) + (0.99 - f0) / (f1 - f0);
            peak = std::max(peak, f1);
        }
        r.overshoot = peak - 1.0;
        r.finalDeg = angles.empty() ? 0.0 : angles.back();
        return r;
    }

    // The distance from angle `deg` to the polyline through a[] (1 ms apart) over sample positions [x0, x1].
    double offTrajectory(const std::vector<double>& a, double deg, long x0, long x1)
    {
        double best = 1e9;
        for (long x = std::max(0L, x0); x < x1 && x + 1 < static_cast<long>(a.size()); ++x)
        {
            const double p = a[static_cast<std::size_t>(x)], q = a[static_cast<std::size_t>(x + 1)];
            const double lo = std::min(p, q), hi = std::max(p, q);
            best = std::min(best, deg < lo ? lo - deg : deg > hi ? deg - hi : 0.0);
        }
        return best;
    }

    void stepRows(Probe& P, std::string_view key, std::vector<double>* stepAngles)
    {
        setView(V::kVu);
        Rig r(key);
        const double at0 = lawDeg(0.0), at12 = lawDeg(-12.0);
        P.near("rest.no_frame_deg", needleDeg(r.host.draw()), at0, 1e-5);

        // 400 ms of 0 dB, one column per 1 ms frame.
        for (int i = 0; i < 400; ++i)
        {
            r.feed(1, 0.0f);
            r.host.tick(1, kMs);
        }
        P.near("rest.zero_gr_deg", needleDeg(r.host.draw()), at0, 1e-5);

        // The attack step: 0 -> 12 dB for 900 ms (the needle moves kShowLagMs later).
        std::vector<double> a { needleDeg(r.host.draw()) };
        for (int i = 0; i < 900; ++i)
        {
            r.feed(1, 12.0f);
            r.host.tick(1, kMs);
            a.push_back(needleDeg(r.host.draw()));
        }
        const Response up = measure(a, at0, at12);
        for (int i = 0; i < 1100; ++i)                            // 2 s after the step: settled on the law
        {
            r.feed(1, 12.0f);
            r.host.tick(1, kMs);
        }
        const double settledUp = needleDeg(r.host.draw());
        std::printf("NOTE     ui.vu: attack 0 -> 12 dB: moves %zu ms after the step, t99 %.2f ms, overshoot %.3f %%\n",
                    onsetOf(a), up.t99Ms, 100.0 * up.overshoot);
        P.near("step.display_lag_ms", static_cast<double>(onsetOf(a)), V::kShowLagMs, 1.0);
        P.near("step.attack.t99_ms", up.t99Ms, 300.0, 30.0);
        P.in("step.attack.overshoot_pct", 100.0 * up.overshoot, 1.0, 1.5);
        P.near("step.attack.settled_deg", settledUp, at12, 1e-5);
        if (stepAngles != nullptr)
            *stepAngles = a;

        // The release: 12 -> 0 dB, fresh.
        std::vector<double> d { needleDeg(r.host.draw()) };
        for (int i = 0; i < 900; ++i)
        {
            r.feed(1, 0.0f);
            r.host.tick(1, kMs);
            d.push_back(needleDeg(r.host.draw()));
        }
        const Response down = measure(d, at12, at0);
        std::printf("NOTE     ui.vu: release 12 -> 0 dB: t99 %.2f ms, overshoot %.3f %%\n", down.t99Ms,
                    100.0 * down.overshoot);
        P.near("step.release.t99_ms", down.t99Ms, 300.0, 30.0);
        P.in("step.release.overshoot_pct", 100.0 * down.overshoot, 1.0, 1.5);

        // The same attack at 60 Hz frames (17/16 columns each): every frame draws a state of the 1 ms run's trajectory
        // (between two of its ms) within 3 ms of the same audio time, so neither the block nor the frame rate changes
        // the ballistics.
        {
            Rig f(key);
            for (int i = 0; i < 24; ++i)                          // 400 ms of 0 dB
                f.frame60(0.0f);
            const long stepAt = (f.frames * 1000) / 60;
            double worst = 0.0;
            for (int i = 0; i < 54; ++i)                          // 900 ms of 12 dB
            {
                f.frame60(12.0f);
                const long x = (f.frames * 1000) / 60 - stepAt;
                worst = std::max(worst, offTrajectory(a, needleDeg(f.host.draw()), x - 3, x + 3));
            }
            P.le("step.frame_rate_invariant_deg", worst, 1e-3);
        }

        // Host blocks of 1024 samples at 44.1 kHz (23.2 ms; Logic's process buffer), each delivered at the first 60 Hz
        // frame after the audio clock passes its end: the drawn needle still moves every frame of the swing (33–250 ms
        // after its first motion), and smoothly — the frame-to-frame change of its per-frame travel stays under 25 % of
        // the largest (drawn at the newest column instead, it would stop in every third frame).
        {
            Rig g(key);
            const double blockMs = 1024.0 / 44.1;
            double wall = 0.0, sent = 0.0;
            long cols = 0;
            const auto run = [&](float gr, int frames, std::vector<double>* out) {
                for (int k = 0; k < frames; ++k)
                {
                    wall += 1000.0 / 60.0;
                    int n = 0;
                    while (sent + blockMs <= wall)
                    {
                        sent += blockMs;
                        const auto c = static_cast<long>(std::floor(sent));
                        n += static_cast<int>(c - cols);
                        cols = c;
                    }
                    if (n > 0)
                        g.feed(n, gr);
                    g.host.tick(1, kDt);
                    if (out != nullptr)
                        out->push_back(needleDeg(g.host.draw()));
                }
            };
            run(0.0f, 30, nullptr);
            std::vector<double> m;
            run(12.0f, 60, &m);
            const std::size_t i0 = onsetOf(m);
            double largest = 0.0, smallest = 1e9, jerk = 0.0;
            for (std::size_t k = i0 + 2; k + 1 < m.size() && k <= i0 + 15; ++k)
            {
                const double travel = std::fabs(m[k + 1] - m[k]);
                largest = std::max(largest, travel);
                smallest = std::min(smallest, travel);
                if (k > i0 + 2)
                    jerk = std::max(jerk, std::fabs(std::fabs(m[k + 1] - m[k]) - std::fabs(m[k] - m[k - 1])));
            }
            std::printf("NOTE     ui.vu: 23.2 ms blocks at 60 Hz: per-frame travel %.3f .. %.3f deg, change <= %.3f deg\n",
                        smallest, largest, jerk);
            P.eq("step.blocks.moves_every_frame", b(smallest > 1e-3 && m.size() > i0 + 16), 1);
            P.le("step.blocks.smooth", largest > 0.0 ? jerk / largest : 99.0, 0.25);
        }
        setView(V::kHistory);
    }

    void staleRows(Probe& P, std::string_view key)
    {
        setView(V::kVu);
        Rig r(key);
        const double at0 = lawDeg(0.0), at12 = lawDeg(-12.0);
        for (int i = 0; i < 90; ++i)                              // 1.5 s at 12 dB: settled
            r.frame60(12.0f);
        const funkgui::PrimList& live = r.host.draw();
        const std::optional<Seg> liveNeedle = needleOf(live);
        uint32_t liveLabel = 0;
        for (const funkgui::Prim* p : tagged(live, ui::tag::axisLabel))
            if (centreIn(*p, L::kBandHistory.plot))
                liveLabel = p->c0;
        const double liveDeg = needleDeg(live);                   // `live` is overwritten by the next draw
        P.near("stale.settled_deg", liveDeg, at12, 1e-5);

        // The audio stops: 1 ms frames, no frame, no column.
        std::vector<double> angles;
        std::vector<bool> fresh;
        for (int i = 0; i < 2000; ++i)
        {
            r.host.tick(1, kMs);
            angles.push_back(needleDeg(r.host.draw()));
            fresh.push_back(r.ctx().frame.fresh);
        }
        std::size_t first = fresh.size();
        for (std::size_t i = 0; i < fresh.size(); ++i)
            if (!fresh[i])
            {
                first = i;
                break;
            }
        double held = 0.0;
        for (std::size_t i = 0; i < first; ++i)
            held = std::max(held, std::fabs(angles[i] - liveDeg));
        P.le("stale.holds_while_fresh_deg", held, 0.0);
        P.near("stale.onset_s", static_cast<double>(first) * 1e-3, static_cast<double>(L::band::kStaleS), 0.0025);
        std::vector<double> fall { first > 0 ? angles[first - 1] : at12 };
        for (std::size_t i = first; i < angles.size(); ++i)
            fall.push_back(angles[i]);
        const Response down = measure(fall, at12, at0);
        P.le("stale.falls_at_once_ms", static_cast<double>(onsetOf(fall)), 1.0);   // drawn at no lag once stale
        std::printf("NOTE     ui.vu: stale fall 12 -> 0 dB: t99 %.2f ms, overshoot %.3f %%\n", down.t99Ms,
                    100.0 * down.overshoot);
        P.near("stale.t99_ms", down.t99Ms, 300.0, 30.0);
        P.in("stale.overshoot_pct", 100.0 * down.overshoot, 1.0, 1.5);
        P.near("stale.rest_deg", angles.back(), at0, 1e-5);

        const funkgui::PrimList& sl = r.host.draw();
        const std::optional<Seg> staleNeedle = needleOf(sl);
        uint32_t staleLabel = 0;
        for (const funkgui::Prim* p : tagged(sl, ui::tag::axisLabel))
            if (centreIn(*p, L::kBandHistory.plot))
                staleLabel = p->c0;
        P.eq("stale.no_dim", b(liveNeedle && staleNeedle && liveNeedle->c0 == staleNeedle->c0 && liveLabel == staleLabel
                               && liveLabel != 0), 1);
        r.host.move(900.0f, 630.0f);
        const int frames = r.host.settle(kMaxSettle, kDt);
        P.eq("stale.settles_idle", b(frames < kMaxSettle && !r.panel.wantsFullRate()), 1);
        setView(V::kHistory);
    }

    // ---- history.* --------------------------------------------------------------------------------------------------

    std::vector<std::array<float, 5>> historyPrims(const funkgui::PrimList& pl)
    {
        std::vector<std::array<float, 5>> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == ui::tag::histIn || p.tag == ui::tag::histOut || p.tag == ui::tag::histGr
                || p.tag == ui::tag::histDet || p.tag == ui::tag::gap || p.tag == ui::tag::modeTick
                || p.tag == ui::tag::stateLane)
                v.push_back({ static_cast<float>(p.tag), p.x0, p.y0, p.x1, p.y1 });
        return v;
    }

    void historyRows(Probe& P, std::string_view key)
    {
        setView(V::kHistory);
        Rig a(key), c(key);                                       // a stays on HISTORY; c goes to VU and back
        const auto both = [&](float gr, uint32_t phase, int cView) {
            setView(V::kHistory);
            a.frame60(gr, phase);
            setView(cView);
            c.frame60(gr, phase);
        };
        for (int i = 0; i < 60; ++i)
            both(3.0f + static_cast<float>(i % 7), 1u + static_cast<uint32_t>(i / 20), V::kHistory);
        for (int i = 0; i < 120; ++i)
            both(6.0f + static_cast<float>(i % 5), 1u + static_cast<uint32_t>((i / 15) % 3), V::kVu);
        const bool wasVu = needleOf(c.host.draw()).has_value();
        setView(V::kHistory);
        c.host.tick(1, 0.0f);                                     // back to HISTORY, no time passes
        const funkgui::PrimList& al = a.host.draw();
        const std::vector<std::array<float, 5>> ha = historyPrims(al);
        const std::vector<std::array<float, 5>> hc = historyPrims(c.host.draw());
        float worst = ha.size() == hc.size() ? 0.0f : 99.0f;
        for (std::size_t i = 0; i < ha.size() && i < hc.size(); ++i)
            for (std::size_t k = 0; k < 5; ++k)
                worst = std::max(worst, std::fabs(ha[i][k] - hc[i][k]));
        P.eq("history.was_vu", b(wasVu), 1);
        P.le("history.same_as_twin_px", static_cast<double>(worst), 1e-4);
        P.eq("history.no_gap", static_cast<int64_t>(tagged(c.host.draw(), ui::tag::gap).size()), 0);
        float newest = -1.0f;
        for (const funkgui::Prim* p : tagged(c.host.draw(), ui::tag::histIn))
            newest = std::max(newest, p->x1);
        P.le("history.newest_at_now_px", std::fabs(static_cast<double>(newest) - static_cast<double>(L::kBandHistory.plot.right()))
             , 2.0);
    }

    // ---- outside.* --------------------------------------------------------------------------------------------------

    void outsideRows(Probe& P, std::string_view key)
    {
        setView(V::kHistory);
        Rig r(key);
        for (int i = 0; i < 90; ++i)
            r.frame60(4.0f + static_cast<float>(i % 9), 1u + static_cast<uint32_t>((i / 10) % 3));
        r.host.tick(1, 0.0f);
        const funkgui::FontAtlasSdf& atlas = funkgui::FontService::get().atlas();
        const funkgui::Image hist = funkgui::rasterise(r.host.draw(), atlas, 2);
        setView(V::kVu);
        r.host.tick(1, 0.0f);
        const funkgui::Image vu = funkgui::rasterise(r.host.draw(), atlas, 2);
        setView(V::kHistory);
        r.host.tick(1, 0.0f);
        const funkgui::Image back = funkgui::rasterise(r.host.draw(), atlas, 2);

        const float dpi = 2.0f;
        const funkgui::Rect& p = L::kBandHistory.plot;
        const std::array<funkgui::Rect, 3> masks { {
            p.expanded(1.0f),
            { V::kViewCells[0].x - 3.0f, V::kViewCells[0].y - 3.0f, p.right() + 3.0f - (V::kViewCells[0].x - 3.0f),
              V::kViewCells[0].h + 6.0f },                         // the caption row (cells, focus ring)
            { p.x - 2.0f, L::kBandHistory.timeLabelY - 2.0f, p.w + 4.0f, 18.0f },   // the time labels
        } };
        long differ = 0, differAll = 0;
        const bool sized = hist.w == vu.w && hist.h == vu.h && hist.w == back.w && hist.h == back.h && hist.w > 0;
        for (int y = 0; sized && y < hist.h; ++y)
            for (int x = 0; x < hist.w; ++x)
            {
                const auto o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(hist.w) + static_cast<std::size_t>(x)) * 4;
                bool same = true, sameBack = true;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    same = same && hist.rgba[o + k] == vu.rgba[o + k];
                    sameBack = sameBack && hist.rgba[o + k] == back.rgba[o + k];
                }
                if (!sameBack)
                    ++differAll;
                if (same)
                    continue;
                const funkgui::Point q { (static_cast<float>(x) + 0.5f) / dpi, (static_cast<float>(y) + 0.5f) / dpi };
                if (std::none_of(masks.begin(), masks.end(), [&](const funkgui::Rect& m) { return m.contains(q); }))
                    ++differ;
            }
        P.eq("outside.sized", b(sized), 1);
        P.eq("outside.pixels", differ, 0);
        P.eq("outside.back_exact", differAll, 0);
    }

    // ---- review pictures --------------------------------------------------------------------------------------------

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
        int written = 0, wanted = 0;
        const auto save = [&](Rig& r, const std::string& name) {
            ++wanted;
            r.host.draw();
            const std::string path = dir + "/vu-" + std::string(key) + "-" + name + ".png";
            if (r.host.writePng(path.c_str()))
                ++written;
        };
        for (int theme = 0; theme < 2; ++theme)
        {
            const std::string tn = theme == 0 ? "graphite" : "paper";
            {
                setView(V::kHistory);
                Rig r(key, theme);
                for (int i = 0; i < 90; ++i)
                    r.frame60(3.0f + static_cast<float>((i / 12) % 4) * 3.0f, 1u + static_cast<uint32_t>((i / 10) % 3));
                save(r, tn + "-history");
            }
            setView(V::kVu);
            {
                Rig r(key, theme);
                save(r, tn + "-rest");
            }
            for (const float gr : { 3.0f, 12.0f })
            {
                Rig r(key, theme);
                for (int i = 0; i < 120; ++i)
                    r.frame60(gr, 1u);
                save(r, tn + (gr < 5.0f ? "-3db" : "-12db"));
            }
        }
        {   // the 0 -> 12 dB step, every 50 ms from the needle's first motion to 600 ms (1 ms frames)
            setView(V::kVu);
            Rig r(key, 0);
            for (int i = 0; i < 400; ++i)
            {
                r.feed(1, 0.0f);
                r.host.tick(1, kMs);
            }
            const double rest = needleDeg(r.host.draw());
            save(r, "step-000");                                  // t = 0: still at rest
            long onset = -1;
            for (long ms = 1; ms <= 1000 && (onset < 0 || ms - onset <= 600); ++ms)
            {
                r.feed(1, 12.0f);
                r.host.tick(1, kMs);
                if (onset < 0 && std::fabs(needleDeg(r.host.draw()) - rest) > 1e-7)
                    onset = ms - 1;
                if (onset >= 0 && (ms - onset) % 50 == 0)
                {
                    char n[32];
                    std::snprintf(n, sizeof n, "step-%03ld", ms - onset);
                    save(r, n);
                }
            }
        }
        setView(V::kHistory);
        std::printf("NOTE     ui.vu: %d of %d pictures written to %s\n", written, wanted, dir.c_str());
        P.note("vu_pictures", std::to_string(written));
    }
}

FCMP_PROBE(ui, vu)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.vu writes UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.vu: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    funkgui::UiPreferences::get().setInt("historySpanTenths", L::kDefaultSpanTenths);
    funkgui::UiPreferences::get().setInt("meterScaleDb", L::kDefaultScaleDb);
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    // The default: a sandbox without the key (ProbeMain empties it before each CTest run) shows HISTORY.
    if (prefView() == -1)
    {
        Rig r(C.key);
        const bool noNeedle = !needleOf(r.host.draw()).has_value();
        P.eq("toggle.default_history", b(noNeedle && hasHistAxis(r.host.draw())), 1);
    }
    else
    {
        std::printf("NOTE     ui.vu: grView is already set in this sandbox; toggle.default_history not judged\n");
    }
    setView(V::kHistory);
    toggleRows(P, C.key);
    scaleRows(P, C.key);
    std::vector<double> step;
    stepRows(P, C.key, &step);
    staleRows(P, C.key);
    historyRows(P, C.key);
    outsideRows(P, C.key);
    if (const std::string dir = pngDir(); !dir.empty())
        pictures(P, C.key, dir);
    setView(V::kHistory);
    return P.finish();
}
