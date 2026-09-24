// Source/editor/views/TransferPlot.cpp — TRANSFER (see TransferPlot.h; 02 §6.5, §7.3, §7.4, §8.7; 01 §7; K1 #9, #22;
// K2 #24; ADR-42). tick() computes every curve, handle and mark in logical px (only when what it depends on changed);
// draw() only emits.
#include "editor/views/TransferPlot.h"

#include "editor/HistoryStore.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Text.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/widgets/FocusRing.h>
#include <funkgui/widgets/RuleSlider.h>
#include <funkgui/widgets/SegmentedSelector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        namespace B = layout::band;
        using fcdsp::Pid;

        constexpr const char* kScaleKey = "meterScaleDb";        // UiPreferences (02 §5.9)
        constexpr int   kMaxCurve = 400;                           // points of one curve after refinement
        constexpr int   kMaxBase = 256;                            // points of the base sampling (<= 120 + 32 + 17 + 3)
        constexpr int   kMaxGhosts = 12;                           // per stepped parameter
        constexpr int   kMaxWedge = 64;                            // plot.w / 4 columns (48 band, 60 Characteristics)
        constexpr int   kTrailPoints = 33;                         // 320 ms every 10 ms, both ends
        constexpr int   kRefinePasses = 5;
        constexpr float kRefinePx = 0.1f;                          // a chord missing the curve by more is split
        constexpr float kBreakTolDb = 1e-3f;
        constexpr float kBreakSpanDb = 1.0f;                       // the 16 range-break points cover ±1 dB
        constexpr float kDedupeDb = 1e-4f;
        constexpr float kWedgeMinDb = 0.01f;
        constexpr float kTitleS = 0.25f;                           // a11y title <= 4 Hz (02 §7.5)
        constexpr float kTrackPx = 240.0f;                         // RuleSlider: 240 px per full track (Shift 1200)
        constexpr float kTrackPxFine = 1200.0f;
        constexpr float kPitchMin = 24.0f, kPitchMax = 64.0f;
        constexpr float kHandleHitPad = 3.0f;
        constexpr float kKneeHandleMinPx = 4.0f;                   // knee handles closer to T_in than this are hidden
        constexpr float kCrossHalf = 2.5f;
        constexpr float kLabelGap = 12.0f;                         // the knee label's top above the bracket
        constexpr float kRatioInf = 1000.0f;                       // a11y display units: ∞:1 (SlotGrid's rule)
        constexpr uint32_t kImageId = 1, kGroupId = 2;             // scale radio buttons: kGroupId + 1 + i
        constexpr funkgui::Col kClear { 0, 0, 0, 0 };

        constexpr const char* kScaleSpec = "METER SCALE   12 · 24 · 48 · 72 DB   CLICK A SCALE (EVERY WINDOW)";

        // The four handles, in the Tab order of 02 §7.5.
        enum Handle : int { hThr = 0, hKnee = 1, hRatio = 2, hRange = 3, kHandles = 4 };
        constexpr std::array<Pid, kHandles> kHandlePid { Pid::thr, Pid::knee, Pid::ratio, Pid::range };

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::TransferGeom& g) noexcept
        {
            const float top = g.scaleCells[0].y;
            return { g.plot.x, top, g.plot.w, g.labelY + 14.0f - top };
        }

        int scaleDb(int v) noexcept
        {
            for (const int s : layout::kScalesDb)
                if (s == v)
                    return v;
            return layout::kDefaultScaleDb;
        }

        std::vector<funkgui::CellText> scaleTexts()
        {
            return { { "12", "12 decibels", nullptr }, { "24", "24 decibels", nullptr },
                     { "48", "48 decibels", nullptr }, { "72", "72 decibels", nullptr } };
        }

        bool writable(funkgui::ValueState s) noexcept
        {
            return s == funkgui::ValueState::continuous || s == funkgui::ValueState::stepped;
        }

        bool shapesCurve(Pid p) noexcept { return p == Pid::thr || p == Pid::ratio || p == Pid::knee || p == Pid::range; }
        bool shapesNet(Pid p) noexcept { return p == Pid::makeup || p == Pid::mix || p == Pid::drive; }

        // y = x + staticGain(x) − preGainDb (02 §6.5; K1 #22): the output level of the pre-makeup curve, GR only.
        void evalCurve(const fcdsp::ModeEntry& e, const fcdsp::EngineParams& eng, const float* x, float* y, int n,
                       fcdsp::analysis::CurveOpts opts = {}) noexcept
        {
            if (n <= 0)
                return;
            const auto un = static_cast<std::size_t>(n);
            fcdsp::analysis::staticGain(e, eng, std::span<const float>(x, un), std::span<float>(y, un), opts);
            for (std::size_t i = 0; i < un; ++i)
                y[i] = x[i] + y[i] - eng.preGainDb;
        }

        float evalAt(const fcdsp::ModeEntry& e, const fcdsp::EngineParams& eng, float x) noexcept
        {
            float y = 0.0f;
            evalCurve(e, eng, &x, &y, 1);
            return y;
        }

        // ---- a11y display units for the handles' value interface: SlotGrid's rule (02 §8.9, §7.5 "identical") ----------
        const fcdsp::HostParam& hostParam(Pid p) noexcept { return fcdsp::kHostParams[fcdsp::idx(p)]; }

        struct PlainRange { float a, b; };
        PlainRange trackSpan(Pid pid, const fcdsp::ParamSpec& s) noexcept
        {
            if (s.lo < s.hi)
                return { s.lo, s.hi };
            return { hostParam(pid).lo, hostParam(pid).hi };
        }

        double toDisplayUnits(Pid pid, const fcdsp::ParamSpec& s, float plain) noexcept
        {
            if (s.display.toDisplay != nullptr)
                return static_cast<double>(s.display.toDisplay(plain));
            const fcdsp::HostParam& h = hostParam(pid);
            if (h.map == fcdsp::Map::ratio3)
                return plain >= 0.999f ? static_cast<double>(kRatioInf) : 1.0 / (1.0 - static_cast<double>(plain));
            if (std::strcmp(h.unit, "%") == 0)
                return static_cast<double>(plain) * 100.0;
            return static_cast<double>(plain);
        }

        float fromDisplayUnits(Pid pid, const fcdsp::ParamSpec& s, double d) noexcept
        {
            if (s.display.toPlain != nullptr)
                return s.display.toPlain(static_cast<float>(d));
            const fcdsp::HostParam& h = hostParam(pid);
            if (h.map == fcdsp::Map::ratio3)
                return d >= static_cast<double>(kRatioInf) ? 1.0f
                                                           : (d != 0.0 ? static_cast<float>(1.0 - 1.0 / d) : 0.0f);
            if (std::strcmp(h.unit, "%") == 0)
                return static_cast<float>(d / 100.0);
            return static_cast<float>(d);
        }

        // Draws the parts of the polyline inside [top, bottom] (x lies inside the plot by construction).
        void clippedPolyline(funkgui::Canvas& c, const float* xs, const float* ys, int n, float top, float bottom,
                             float width, funkgui::Col col)
        {
            for (int i = 0; i + 1 < n; ++i)
            {
                float x0 = xs[i], y0 = ys[i], x1 = xs[i + 1], y1 = ys[i + 1];
                if ((y0 < top && y1 < top) || (y0 > bottom && y1 > bottom))
                    continue;
                const auto clip = [](float& xa, float& ya, float xb, float yb, float yc) {
                    const float t = (yc - ya) / (yb - ya);
                    xa += t * (xb - xa);
                    ya = yc;
                };
                if (y0 < top)
                    clip(x0, y0, x1, y1, top);
                else if (y0 > bottom)
                    clip(x0, y0, x1, y1, bottom);
                if (y1 < top)
                    clip(x1, y1, x0, y0, top);
                else if (y1 > bottom)
                    clip(x1, y1, x0, y0, bottom);
                if (x1 > x0 || y1 != y0)
                    c.segment(x0, y0, x1, y1, width, col);
            }
        }

        // One knee-width or dB text: "6", "4.5".
        void dbText(char* out, std::size_t n, float v) noexcept
        {
            const int dp = std::fabs(v - std::round(v)) < 0.05f ? 0 : 1;
            if (funkgui::fmt::db(v, dp, out, n) < 0 && n > 0)
                out[0] = '\0';
        }
    }

    namespace
    {
        // The peak envelope of store entries [e0, e1): max detMaxDb, max tgtMaxDb, max grMaxDb over the data entries.
        struct Envelope
        {
            float x = 0.0f, tgt = 0.0f, gr = 0.0f;
            bool  valid = false;
        };

        Envelope envelope(const HistoryStore& h, uint64_t e0, uint64_t e1) noexcept
        {
            Envelope e;
            for (uint64_t i = std::max(e0, h.oldest()); i < e1 && i < h.count(); ++i)
            {
                const fcdsp::HistoryColumn& c = h.at(i);
                if (HistoryStore::isGap(c))
                    continue;
                e.x = e.valid ? std::max(e.x, c.detMaxDb) : c.detMaxDb;
                e.tgt = e.valid ? std::max(e.tgt, c.tgtMaxDb) : c.tgtMaxDb;
                e.gr = e.valid ? std::max(e.gr, c.grMaxDb) : c.grMaxDb;
                e.valid = true;
            }
            return e;
        }
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct TransferPlot::State
    {
        struct Curve                                             // a curve in logical px
        {
            int n = 0;
            std::array<float, kMaxCurve> x{}, y{};
        };
        struct Ghost
        {
            int detent = -1;                                      // the detent it shows
            int n = 0;
            std::array<float, kMaxBase> x{}, y{};
        };
        struct HandlePos
        {
            bool  shown = false;
            float x = 0.0f, y = 0.0f;
        };

        State(PanelContext& ctx, const layout::TransferGeom& g, uint32_t idBase)
            : scaleModel(kScaleKey, { 12, 24, 48, 72 }, scaleTexts(), layout::kDefaultScaleDb),
              scale(scaleModel, std::vector<funkgui::Rect>(g.scaleCells.begin(), g.scaleCells.end()),
                    funkgui::CellStyle::text, nullptr, funkgui::Point{}, idBase + kGroupId),
              sliders{ { { ctx.slot(Pid::thr), layout::slotGeom(*layout::slotOf(Pid::thr)), idBase + kPlotHandleIds },
                         { ctx.slot(Pid::knee), layout::slotGeom(*layout::slotOf(Pid::knee)), idBase + kPlotHandleIds + 1 },
                         { ctx.slot(Pid::ratio), layout::slotGeom(*layout::slotOf(Pid::ratio)),
                           idBase + kPlotHandleIds + 2 },
                         { ctx.slot(Pid::range), layout::slotGeom(*layout::slotOf(Pid::range)),
                           idBase + kPlotHandleIds + 3 } } }
        {
            scale.setSpokenTitle("Meter scale");
        }

        void build(const PanelContext&, const layout::TransferGeom&);
        void buildGhosts(const PanelContext&, const layout::TransferGeom&);
        int  sample(const fcdsp::ModeEntry&, const fcdsp::EngineParams&, bool refine, float lo, float hi, float tolDb,
                    float* xs, float* ys, int cap, float& breakX) noexcept;
        int  handleAt(funkgui::Point) const noexcept;         // the handle under p (4: the knee's left ring), or -1
        bool handlesOn(const PanelContext&, const layout::TransferGeom&) const noexcept;   // the handles' ease target

        funkgui::PrefCells         scaleModel;
        funkgui::SegmentedSelector scale;
        std::array<funkgui::RuleSlider, kHandles> sliders;       // never drawn: wheel, keys, a11y, spec lines

        // what build() derived from FrameState::eng (dB values, then px)
        uint32_t builtEng = ~0u, builtResolve = ~0u, builtMode = ~0u;
        int      builtScale = 0;
        const fcdsp::ModeEntry* builtEntry = nullptr;
        bool     haveCurve = false;
        std::array<float, kMaxCurve> cx{}, cy{};                  // the static curve, dB
        Curve    curve, net, stage;                              // px
        bool     drawNet = false, drawStage = false;
        float    tIn = 0.0f, kneeW = 0.0f, breakX = std::numeric_limits<float>::quiet_NaN();
        bool     kneeApplies = false;
        int      nWedge = 0;
        std::array<float, kMaxWedge + 1> wx{}, wTop{}, wBot{};   // wedge column edges, px
        std::array<bool, kMaxWedge + 1> wGr{};                    // GR > 0 at that edge
        std::array<HandlePos, kHandles + 1> handles{};            // thr, knee right, ratio, range, knee left (index 4)
        std::array<funkgui::ValueState, kHandles> states{};
        float    kneeYL = 0.0f, kneeYR = 0.0f;
        char     kneeLabel[40] = "";

        // ghosts
        std::array<std::array<Ghost, kMaxGhosts>, 2> ghosts{};   // [0] RATIO, [1] KNEE
        std::array<int, 2> nGhosts{};

        // Mode-switch landing (02 §8.7)
        Curve    prev, shown;                                    // the previous Mode's curve; the eased one drawn
        float    landT = std::numeric_limits<float>::infinity(); // seconds since the switch
        bool     landing = false;

        // pointer, handles, drag
        bool  pointerOver = false;
        funkgui::Point pointer{};
        int   hot = -1;                                           // the handle under the pointer (4 = knee left)
        float handleAmt = 0.0f;
        enum class Mode : uint8_t { none, refused, absolute, relative, stepped, cells };
        Mode  mode = Mode::none;
        int   dragHandle = -1;
        funkgui::Point down{};
        float grab = 0.0f;                                        // absolute: pointer value − handle value at down
        float u0 = 0.0f;                                          // relative: plain-normalised track at down
        bool  invert = false;
        int   detent0 = -1, detent = -1;

        uint32_t revision = 0;                                    // bumps when a handle's a11y item comes or goes
        uint32_t naMask = 0;

        // a11y title
        char     title[200] = "Transfer curve";
        float    titleAge = 0.0f;
        uint32_t titleSerial = ~0u;
    };

    namespace
    {
        void toPx(const layout::TransferGeom& g, float s, const float* xd, const float* yd, int n, float* xp, float* yp)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                xp[u] = layout::transferX(g, xd[u], s);
                yp[u] = g.level.y(yd[u], s);
            }
        }

        // Inserts the sorted, de-duplicated union of a[0..na) and b[0..nb) into out (capacity cap); returns its size.
        int mergeSorted(const float* a, int na, const float* b, int nb, float* out, int cap) noexcept
        {
            int i = 0, j = 0, n = 0;
            while ((i < na || j < nb) && n < cap)
            {
                const float v = j >= nb || (i < na && a[i] <= b[j]) ? a[i++] : b[j++];
                if (n == 0 || v - out[n - 1] > kDedupeDb)
                    out[n++] = v;
            }
            return n;
        }
    }

    // The static curve over [lo, hi] (02 §6.5): 64 uniform points (120 for custom curves and feedback topologies), 32
    // across [T_in ± W/2] with T_in and both knee ends exactly, 16 around the range break with the break exactly; then,
    // with `refine`, chords that miss the curve by more than tolDb at their midpoint are split (<= kRefinePasses).
    int TransferPlot::State::sample(const fcdsp::ModeEntry& e, const fcdsp::EngineParams& eng, bool refine, float lo,
                                    float hi, float tolDb, float* xs, float* ys, int cap, float& brk) noexcept
    {
        std::array<float, kMaxBase> a{}, b{};
        const bool custom = e.desc->family == fcdsp::CurveFamily::custom || eng.topo == fcdsp::kTopoFB;
        const int nu = custom ? B::kCurveCustom : B::kCurveUniform;
        for (int k = 0; k < nu; ++k)
            a[static_cast<std::size_t>(k)] = lo + (hi - lo) * static_cast<float>(k) / static_cast<float>(nu - 1);
        int nb = 0;
        const float t = fcdsp::analysis::inputThresholdDb(eng);
        const float w = std::max(eng.kneeDb, 0.0f);
        if (kneeApplies)
        {
            if (w > 0.0f)
                for (int k = 0; k < B::kCurveKnee; ++k)
                    b[static_cast<std::size_t>(nb++)]
                        = t - 0.5f * w + w * static_cast<float>(k) / static_cast<float>(B::kCurveKnee - 1);
            b[static_cast<std::size_t>(nb++)] = t;
        }
        for (int k = 0; k < nb; ++k)
            b[static_cast<std::size_t>(k)] = std::clamp(b[static_cast<std::size_t>(k)], lo, hi);
        std::sort(b.begin(), b.begin() + nb);
        int n = mergeSorted(a.data(), nu, b.data(), nb, xs, std::min(cap, kMaxBase));
        evalCurve(e, eng, xs, ys, n);

        // The range break: where the GR first reaches rangeDb (bisection on the first bracketing interval).
        brk = std::numeric_limits<float>::quiet_NaN();
        if (eng.rangeDb < fcdsp::kRangeOff - kBreakTolDb)
        {
            const float target = eng.rangeDb - kBreakTolDb;
            for (int i = 1; i < n; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                if (xs[u - 1] - ys[u - 1] < target && xs[u] - ys[u] >= target)
                {
                    float l = xs[u - 1], r = xs[u];
                    for (int it = 0; it < 24; ++it)
                    {
                        const float m = 0.5f * (l + r);
                        (m - evalAt(e, eng, m) >= target ? r : l) = m;
                    }
                    brk = r;
                    break;
                }
            }
            if (!std::isnan(brk))
            {
                int nr = 0;
                for (int k = 0; k < B::kCurveRange; ++k)
                    b[static_cast<std::size_t>(nr++)] = std::clamp(
                        brk - kBreakSpanDb + 2.0f * kBreakSpanDb * static_cast<float>(k) / (B::kCurveRange - 1), lo, hi);
                b[static_cast<std::size_t>(nr++)] = brk;
                std::sort(b.begin(), b.begin() + nr);
                std::copy(xs, xs + n, a.begin());
                n = mergeSorted(a.data(), n, b.data(), nr, xs, std::min(cap, kMaxBase));
                evalCurve(e, eng, xs, ys, n);
            }
        }

        if (!refine)
            return n;
        for (int pass = 0; pass < kRefinePasses; ++pass)
        {
            // Midpoints of every segment, evaluated in one call; a chord whose midpoint misses by > tolDb is split.
            int nm = 0;
            std::array<float, kMaxCurve> mx{}, my{};
            for (int i = 0; i + 1 < n && nm < kMaxCurve; ++i)
                mx[static_cast<std::size_t>(nm++)] = 0.5f * (xs[i] + xs[i + 1]);
            evalCurve(e, eng, mx.data(), my.data(), nm);
            std::array<float, kMaxCurve> nx{}, ny{};
            int out = 0, added = 0;
            for (int i = 0; i < n && out < cap; ++i)
            {
                nx[static_cast<std::size_t>(out)] = xs[i];
                ny[static_cast<std::size_t>(out++)] = ys[i];
                if (i + 1 < n && i < nm && out < cap)
                {
                    const auto u = static_cast<std::size_t>(i);
                    const float chord = 0.5f * (ys[i] + ys[i + 1]);
                    if (std::fabs(my[u] - chord) > tolDb && mx[u] - xs[i] > kDedupeDb && n + added < cap)
                    {
                        nx[static_cast<std::size_t>(out)] = mx[u];
                        ny[static_cast<std::size_t>(out++)] = my[u];
                        ++added;
                    }
                }
            }
            std::copy(nx.begin(), nx.begin() + out, xs);
            std::copy(ny.begin(), ny.begin() + out, ys);
            n = out;
            if (added == 0)
                break;
        }
        return n;
    }

    // Everything the frame's EngineParams decide (02 §6.5): the curve and its variants, the wedge, the knee marks and
    // the handles, in px. Runs when FrameState::engSerial, the entry or the scale changed.
    void TransferPlot::State::build(const PanelContext& ctx, const layout::TransferGeom& g)
    {
        const FrameState& f = ctx.frame;
        builtEng = f.engSerial;
        builtEntry = f.entry;
        builtScale = ctx.meterScaleDb;
        haveCurve = false;
        drawNet = drawStage = false;
        nWedge = 0;
        for (HandlePos& h : handles)
            h = HandlePos{};
        kneeLabel[0] = '\0';
        if (f.entry == nullptr || f.entry->desc == nullptr)
            return;

        const fcdsp::ModeEntry& e = *f.entry;
        const fcdsp::EngineParams& eng = f.eng;
        const float s = static_cast<float>(builtScale);
        const float lo = layout::kLevelTopDb - s, hi = layout::kLevelTopDb;
        const float ppd = g.level.pxPerDb(s);
        const funkgui::Rect& p = g.plot;
        for (std::size_t k = 0; k < kHandles; ++k)
        {
            const fcdsp::SlotState st = f.res.view[kHandlePid[k]].state;
            states[k] = st == fcdsp::SlotState::live      ? funkgui::ValueState::continuous
                      : st == fcdsp::SlotState::stepped   ? funkgui::ValueState::stepped
                      : st == fcdsp::SlotState::locked    ? funkgui::ValueState::locked
                      : st == fcdsp::SlotState::derived   ? funkgui::ValueState::derived
                                                          : funkgui::ValueState::na;
        }
        kneeApplies = states[hKnee] != funkgui::ValueState::na;
        uint32_t mask = 0;
        for (std::size_t k = 0; k < kHandles; ++k)
            mask |= states[k] == funkgui::ValueState::na ? 1u << k : 0u;
        if (mask != naMask)
        {
            naMask = mask;
            ++revision;                                           // a handle's a11y item came or went
        }
        tIn = fcdsp::analysis::inputThresholdDb(eng);
        kneeW = std::max(eng.kneeDb, 0.0f);

        // The static curve, refined to C's G2 chord error.
        curve.n = sample(e, eng, true, lo, hi, kRefinePx / ppd, cx.data(), cy.data(), kMaxCurve, breakX);
        toPx(g, s, cx.data(), cy.data(), curve.n, curve.x.data(), curve.y.data());
        haveCurve = curve.n > 1;

        // NET_CURVE: iff it differs from the static curve by > 0.05 dB anywhere (02 §6.5).
        net.n = curve.n;
        float worst = 0.0f;
        for (int i = 0; i < curve.n; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            const float gain = cy[u] + eng.preGainDb - cx[u];
            const float yn = cx[u] + fcdsp::analysis::netGainDb(gain, eng.makeupDb, eng.mix);
            worst = std::max(worst, std::fabs(yn - cy[u]));
            net.x[u] = curve.x[u];
            net.y[u] = g.level.y(yn, s);
        }
        drawNet = worst > B::kNetCurveDb;

        // STAGE_CURVE (Characteristics, two-stage Modes): stage 2 left out (02 §7.3; D §7).
        if (g.stageCurve && e.desc->stage2 != fcdsp::Stage2Kind::none)
        {
            std::array<float, kMaxCurve> ys{};
            evalCurve(e, eng, cx.data(), ys.data(), curve.n, fcdsp::analysis::CurveOpts{ false, false });
            stage.n = curve.n;
            toPx(g, s, cx.data(), ys.data(), curve.n, stage.x.data(), stage.y.data());
            drawStage = true;
        }

        // GR_WEDGE: 4 px columns between unity and the curve where GR > 0.
        nWedge = std::min(static_cast<int>(p.w / B::kWedgeColumnW), kMaxWedge);
        {
            std::array<float, kMaxWedge + 1> xd{}, yd{};
            for (int i = 0; i <= nWedge; ++i)
                xd[static_cast<std::size_t>(i)] = lo + (hi - lo) * static_cast<float>(i) / static_cast<float>(nWedge);
            evalCurve(e, eng, xd.data(), yd.data(), nWedge + 1);
            for (int i = 0; i <= nWedge; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                wx[u] = p.x + B::kWedgeColumnW * static_cast<float>(i);
                wTop[u] = std::clamp(g.level.y(xd[u], s), p.y, p.bottom());
                wBot[u] = std::clamp(g.level.y(yd[u], s), p.y, p.bottom());
                wGr[u] = xd[u] - yd[u] > kWedgeMinDb;
            }
        }

        // Handles and knee marks: T_in on unity; the knee ends, the ratio point and the break on the curve.
        const float xr = std::min(tIn + B::kRatioHandleDb, B::kRatioHandleMaxDb);
        std::array<float, 4> hx { tIn - 0.5f * kneeW, tIn + 0.5f * kneeW, xr, std::isnan(breakX) ? tIn : breakX };
        std::array<float, 4> hy{};
        evalCurve(e, eng, hx.data(), hy.data(), 4);
        const auto inside = [&](float xd, float yd) {
            return xd >= lo && xd <= hi && yd >= lo && yd <= hi;
        };
        const auto put = [&](int k, float xd, float yd) {
            HandlePos& h = handles[static_cast<std::size_t>(k)];
            h.shown = inside(xd, yd) && states[static_cast<std::size_t>(k == 4 ? hKnee : k)] != funkgui::ValueState::na;
            h.x = layout::transferX(g, xd, s);
            h.y = g.level.y(yd, s);
        };
        put(hThr, tIn, tIn);
        const bool kneeHandles = kneeApplies && 0.5f * kneeW * ppd >= kKneeHandleMinPx;
        put(hKnee, hx[1], hy[1]);
        put(4, hx[0], hy[0]);
        handles[hKnee].shown = handles[hKnee].shown && kneeHandles;
        handles[4].shown = handles[4].shown && kneeHandles;
        put(hRatio, hx[2], hy[2]);
        put(hRange, hx[3], hy[3]);
        handles[hRange].shown = handles[hRange].shown && !std::isnan(breakX);
        kneeYL = std::clamp(g.level.y(hy[0], s), p.y, p.bottom());
        kneeYR = std::clamp(g.level.y(hy[1], s), p.y, p.bottom());

        // The knee label: the spec's tag (FIXED, = RATIO), else KNEE <W>.
        if (kneeApplies)
        {
            const fcdsp::ParamSpec* ks = f.res.view.spec[fcdsp::idx(Pid::knee)];
            if (ks != nullptr && ks->tag != nullptr && ks->tag[0] != '\0')
                std::snprintf(kneeLabel, sizeof kneeLabel, "%s", ks->tag);
            else
            {
                char w[16];
                dbText(w, sizeof w, kneeW);
                std::snprintf(kneeLabel, sizeof kneeLabel, "KNEE %s", w);
            }
        }
    }

    // GHOST_CURVE (02 §6.5): each other detent of a stepped RATIO or KNEE, resolve()d with the detent's plain value.
    void TransferPlot::State::buildGhosts(const PanelContext& ctx, const layout::TransferGeom& g)
    {
        const FrameState& f = ctx.frame;
        builtResolve = f.resolveSerial;
        nGhosts = { 0, 0 };
        if (f.entry == nullptr || f.entry->desc == nullptr)
            return;
        const float s = static_cast<float>(ctx.meterScaleDb);
        const float lo = layout::kLevelTopDb - s, hi = layout::kLevelTopDb;
        const std::array<Pid, 2> pids { Pid::ratio, Pid::knee };
        for (std::size_t k = 0; k < pids.size(); ++k)
        {
            const fcdsp::ResolvedParam& r = f.res.view[pids[k]];
            const fcdsp::ParamSpec* spec = f.res.view.spec[fcdsp::idx(pids[k])];
            if (r.state != fcdsp::SlotState::stepped || spec == nullptr)
                continue;
            const int steps = fcdsp::stepCount(*spec);
            for (int i = 0; i < steps && nGhosts[k] < kMaxGhosts; ++i)
            {
                if (i == r.step)
                    continue;
                fcdsp::RawParams raw = f.raw;
                raw[pids[k]] = fcdsp::stepPlain(*spec, i);
                fcdsp::Resolution res;
                fcdsp::resolve(*f.entry, raw, res);
                Ghost& gh = ghosts[k][static_cast<std::size_t>(nGhosts[k]++)];
                gh.detent = i;
                std::array<float, kMaxBase> xd{}, yd{};
                float brk = 0.0f;
                const bool keep = kneeApplies;
                kneeApplies = res.view[Pid::knee].state != fcdsp::SlotState::na;
                gh.n = sample(*f.entry, res.eng, false, lo, hi, 0.0f, xd.data(), yd.data(), kMaxBase, brk);
                kneeApplies = keep;
                toPx(g, s, xd.data(), yd.data(), gh.n, gh.x.data(), gh.y.data());
            }
        }
    }

    // ---- construction ----------------------------------------------------------------------------------------------------

    TransferPlot::TransferPlot(PanelContext& ctx, const layout::TransferGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>(ctx, geom, idBase))
    {
        ctx_.meterScaleDb = scaleDb(st_->scaleModel.value());
        st_->builtMode = ctx_.frame.modeSerial;
    }

    TransferPlot::~TransferPlot() = default;

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void TransferPlot::tick(float dt)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        ctx_.meterScaleDb = scaleDb(s.scaleModel.value());         // the preference, mirrored (no file access here)
        s.scale.tick(dt, s.pointerOver ? s.pointer : funkgui::Point{ -1.0f, -1.0f });
        for (std::size_t k = 0; k < kHandles; ++k)
        {
            const uint32_t id = s.sliders[k].a11yId();
            s.sliders[k].tick(dt, false, geom_.handleStops && ctx_.focusVisible && ctx_.focus == id, false);
        }

        // A Mode switch (02 §8.7): the drawn curve becomes the previous one, eased from over 160 ms, ghosted 0.9 s.
        const bool modeChanged = f.modeSerial != s.builtMode;
        if (modeChanged)
        {
            s.builtMode = f.modeSerial;
            if (s.haveCurve)
            {
                s.prev = s.landing && s.shown.n > 1 ? s.shown : s.curve;   // mid-ease: from what is on screen
                s.landT = 0.0f;
                s.landing = true;
            }
        }
        else if (s.landing)
        {
            s.landT += std::max(dt, 0.0f);
        }
        if (s.landing && s.landT >= B::kGhostHoldS)
            s.landing = false;

        if (f.engSerial != s.builtEng || f.entry != s.builtEntry || ctx_.meterScaleDb != s.builtScale)
            s.build(ctx_, geom_);
        if (f.resolveSerial != s.builtResolve || ctx_.meterScaleDb != s.builtScale)
            s.buildGhosts(ctx_, geom_);

        // The eased curve while landing: the old curve read at the new x, blended by a smoothstep over 160 ms.
        if (s.landing && s.landT < B::kCurveEaseS && s.prev.n > 1)
        {
            const float a = std::clamp(s.landT / B::kCurveEaseS, 0.0f, 1.0f);
            const float w = a * a * (3.0f - 2.0f * a);
            s.shown.n = s.curve.n;
            int j = 0;
            for (int i = 0; i < s.curve.n; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                const float x = s.curve.x[u];
                while (j + 2 < s.prev.n && s.prev.x[static_cast<std::size_t>(j + 1)] < x)
                    ++j;
                const auto v = static_cast<std::size_t>(j);
                const float x0 = s.prev.x[v], x1 = s.prev.x[v + 1];
                const float t = x1 > x0 ? std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f) : 0.0f;
                const float yo = s.prev.y[v] + t * (s.prev.y[v + 1] - s.prev.y[v]);
                s.shown.x[u] = x;
                s.shown.y[u] = yo + w * (s.curve.y[u] - yo);
            }
        }
        else
        {
            s.shown.n = 0;                                        // draw the curve itself
        }

        // Handles: shown while the pointer is over the plot or dragging (90/160 ms), always under always-chrome, and
        // while one has the keyboard focus on the Characteristics screen.
        s.handleAmt = funkgui::ease::hover(s.handleAmt, s.handlesOn(ctx_, geom_), dt);

        // The item under the hand.
        char line[sizeof ctx_.handNext.spec];
        const auto offer = [&](int h, HandKind kind) {
            const int k = h == 4 ? static_cast<int>(hKnee) : h;
            const funkgui::RuleSlider& sl = s.sliders[static_cast<std::size_t>(k)];
            sl.specLine(line, sizeof line);
            ctx_.offerHand(kHandlePid[static_cast<std::size_t>(k)], kind, sl.a11yId(), line);
        };
        if (s.dragHandle >= 0 && ctx_.pointerPressed)
            offer(s.dragHandle, HandKind::drag);
        else if (s.pointerOver && s.hot >= 0)
            offer(s.hot, HandKind::hover);
        else if (s.pointerOver && s.scale.contains(s.pointer))
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, idBase_ + kGroupId, kScaleSpec);
        if (ctx_.focusVisible)
        {
            if (ctx_.focus == idBase_ + kGroupId)
                ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, idBase_ + kGroupId, kScaleSpec);
            for (int k = 0; k < kHandles && geom_.handleStops; ++k)
                if (ctx_.focus == s.sliders[static_cast<std::size_t>(k)].a11yId())
                    offer(k, HandKind::focus);
        }

        // The image's title (02 §7.5): "Transfer curve: threshold −18 dB, ratio 4 to 1, knee 6 dB, gain reduction 3.2
        // dB", regenerated at <= 4 Hz.
        s.titleAge += std::max(dt, 0.0f);
        if (f.entry != nullptr && (s.titleAge >= kTitleS || s.titleSerial != f.resolveSerial))
        {
            s.titleAge = 0.0f;
            s.titleSerial = f.resolveSerial;
            char thr[24], knee[24] = "", gr[24] = "";
            fcdsp::FormattedValue ratio{};
            fcdsp::formatParts(f.res.view, Pid::ratio, ratio);
            if (funkgui::fmt::db(s.tIn, 1, thr, sizeof thr) < 0)
                thr[0] = '\0';
            if (s.kneeApplies)
                dbText(knee, sizeof knee, s.kneeW);
            const int lane = f.ui.appliedGrDb[1] > f.ui.appliedGrDb[0] ? 1 : 0;
            if (f.live && funkgui::fmt::db(f.ui.appliedGrDb[lane], 1, gr, sizeof gr) < 0)
                gr[0] = '\0';
            std::snprintf(s.title, sizeof s.title, "Transfer curve: threshold %s dB, ratio %s%s%s%s%s%s%s", thr,
                          ratio.spoken[0] != '\0' ? ratio.spoken : ratio.value, s.kneeApplies ? ", knee " : "", knee,
                          s.kneeApplies ? " dB" : "", f.live ? ", gain reduction " : "", gr, f.live ? " dB" : "");
        }
    }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void TransferPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const FrameState& f = ctx_.frame;
        const funkgui::Rect& p = geom_.plot;
        const float scale = static_cast<float>(ctx_.meterScaleDb);
        const float floorDb = layout::kLevelTopDb - scale;
        const layout::LevelMap& lm = geom_.level;
        const auto xOf = [&](float db) { return layout::transferX(geom_, db, scale); };
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            const float step = scale <= 24.0f ? B::kGridDbFine : B::kGridDb;
            for (float db = std::floor(layout::kLevelTopDb / step) * step; db > floorDb; db -= step)
                if (db < layout::kLevelTopDb)
                {
                    c.hairlineH(p.x + 1.0f, lm.y(db, scale), p.w - 2.0f, th.ink16);
                    c.hairlineV(xOf(db), p.y + 1.0f, p.h - 2.0f, th.ink16);
                }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::unity, false);
            c.segment(p.x, p.bottom(), p.right(), p.y, 1.0f, th.ink16);   // square: unity is exactly 45°
        }

        const Pid handPid = ctx_.hand.kind != HandKind::none ? ctx_.hand.pid : fcdsp::kNoPid;
        if (s.haveCurve)
        {
            // Ghosts: the other detents (ink16; the detent label under the pointer in ink32), the previous Mode.
            {
                const funkgui::Canvas::Scope scope(c, tag::ghostCurve, false);
                const std::array<Pid, 2> pids { Pid::ratio, Pid::knee };
                for (std::size_t k = 0; k < 2; ++k)
                {
                    const funkgui::RuleSlider& sl = s.sliders[k == 0 ? static_cast<std::size_t>(hRatio)
                                                                     : static_cast<std::size_t>(hKnee)];
                    const int previewed = ctx_.pointerIn && ctx_.screen == Screen::panel && handPid == pids[k]
                                              ? sl.detentLabelAt(ctx_.pointer) : -1;
                    for (int i = 0; i < s.nGhosts[k]; ++i)
                    {
                        const State::Ghost& g = s.ghosts[k][static_cast<std::size_t>(i)];
                        const bool hot = g.detent == previewed;
                        clippedPolyline(c, g.x.data(), g.y.data(), g.n, p.y, p.bottom(), 1.0f,
                                        funkgui::premix(th.ground, hot ? th.ink32 : th.ink16, 1.0f));
                    }
                }
                if (s.landing && s.prev.n > 1)
                    clippedPolyline(c, s.prev.x.data(), s.prev.y.data(), s.prev.n, p.y, p.bottom(), 1.0f,
                                    funkgui::premix(th.ground, th.ink16, 1.0f));
            }
            {
                const funkgui::Canvas::Scope scope(c, tag::grWedge, false);
                for (int i = 0; i < s.nWedge; ++i)
                {
                    const auto u = static_cast<std::size_t>(i);
                    if (!s.wGr[u] && !s.wGr[u + 1])
                        continue;
                    c.area(s.wx[u], s.wx[u + 1], s.wTop[u], s.wTop[u + 1], s.wBot[u], s.wBot[u + 1],
                           funkgui::premix(th.ground, th.ink16, 1.0f));
                }
            }
            if (s.kneeApplies)
            {
                const funkgui::Canvas::Scope scope(c, tag::kneeMark, false);
                const float xl = xOf(s.tIn - 0.5f * s.kneeW), xr = xOf(s.tIn + 0.5f * s.kneeW);
                const float yb = geom_.kneeBracketY;
                const bool marks = s.kneeW * lm.pxPerDb(scale) >= 1.0f && xl >= p.x && xr <= p.right();
                if (marks)
                {
                    // The hairlines run from the curve down to just above the label, the bracket sits under the label
                    // with its end ticks hanging, so a knee narrower than its label never crosses the text.
                    const float stop = yb - kLabelGap - 2.0f;
                    if (stop > s.kneeYL)
                        c.hairlineV(xl, s.kneeYL, stop - s.kneeYL, th.ink16);
                    if (stop > s.kneeYR)
                        c.hairlineV(xr, s.kneeYR, stop - s.kneeYR, th.ink16);
                    c.hairlineH(xl, yb, xr - xl + 1.0f, th.ink16);
                    c.hairlineV(xl, yb, B::kKneeTickH, th.ink32);
                    c.hairlineV(xr, yb, B::kKneeTickH, th.ink32);
                }
                const float tx = xOf(s.tIn);
                if (s.kneeLabel[0] != '\0' && tx >= p.x && tx <= p.right())
                {
                    const float w = c.textWidth(s.kneeLabel, T::kMicro);
                    const float cxl = std::clamp(tx, p.x + 0.5f * w + 2.0f, p.right() - 0.5f * w - 2.0f);
                    c.text(s.kneeLabel, cxl, yb - kLabelGap, T::kMicro, th.ink32, funkgui::Align::centre);
                }
            }
            if (s.drawStage)
            {
                const funkgui::Canvas::Scope scope(c, tag::stageCurve, false);
                clippedPolyline(c, s.stage.x.data(), s.stage.y.data(), s.stage.n, p.y, p.bottom(), 1.0f,
                                funkgui::premix(th.ground, th.ink52, 1.0f));
            }
            if (s.drawNet)
            {
                const funkgui::Canvas::Scope scope(c, tag::netCurve, false);
                clippedPolyline(c, s.net.x.data(), s.net.y.data(), s.net.n, p.y, p.bottom(), 1.0f,
                                funkgui::premix(th.ground, shapesNet(handPid) ? th.accent : th.ink32, 1.0f));
            }
            {
                const funkgui::Canvas::Scope scope(c, tag::transferCurve, false);
                const bool hot = shapesCurve(handPid);
                const State::Curve& cv = s.shown.n > 1 ? s.shown : s.curve;
                clippedPolyline(c, cv.x.data(), cv.y.data(), cv.n, p.y, p.bottom(), hot ? 1.5f : 1.0f,
                                funkgui::premix(th.ground, hot ? th.accent : th.ink70, 1.0f));
            }
        }

        // Live: target ring, trail, needle, operating dot (02 §6.5). Their x is the plugin-input level on the Mode's
        // detector axis (01 §7: peak law = the sine's PEAK). UiFrame::curveXDb is the detector's value at the block's
        // last sample, which for a peak law swings with the waveform's phase, so the dot takes the peak envelope: the max
        // of the store's detMaxDb over the last 10 ms (the trail's step) and curveXDb; the target likewise (tgtMaxDb and
        // targetGrDb). The GR is the frame's appliedGrDb of the lane with the larger GR: what multiplies the audio.
        if (f.live && f.entry != nullptr)
        {
            const HistoryStore& h = ctx_.history;
            const auto step = static_cast<uint64_t>(std::lround(B::kTrailStepS * 1000.0f));
            const int lane = f.ui.appliedGrDb[1] > f.ui.appliedGrDb[0] ? 1 : 0;
            const auto ul = static_cast<std::size_t>(lane);
            const Envelope now = envelope(h, h.count() >= step ? h.count() - step : 0, h.count());
            const float cx = now.valid ? std::max(now.x, f.ui.curveXDb[ul]) : f.ui.curveXDb[ul];
            const float gr = std::max(f.ui.appliedGrDb[ul], 0.0f);
            const float tgt = std::max(now.valid ? std::max(now.tgt, f.ui.targetGrDb[ul]) : f.ui.targetGrDb[ul], 0.0f);
            const float x = xOf(cx);
            // OP_TRAIL: the last 320 ms of the store in 10 ms windows (max detMaxDb, max grMaxDb), ink70 → ink16.
            {
                const funkgui::Canvas::Scope scope(c, tag::opTrail, true);
                bool have = false;
                float px0 = 0.0f, py0 = 0.0f;
                for (int k = kTrailPoints - 1; k >= 0; --k)
                {
                    const uint64_t back = static_cast<uint64_t>(k) * step;
                    if (h.count() < back + step)
                    {
                        have = false;
                        continue;
                    }
                    const Envelope e = envelope(h, h.count() - back - step, h.count() - back);
                    if (!e.valid || !(e.x > floorDb) || e.x > layout::kLevelTopDb)
                    {
                        have = false;
                        continue;
                    }
                    const float px1 = xOf(e.x);
                    const float py1 = std::clamp(lm.y(e.x - std::max(e.gr, 0.0f), scale), p.y, p.bottom());
                    if (have)
                    {
                        const float age = static_cast<float>(k) / static_cast<float>(kTrailPoints - 1);
                        c.segment(px0, py0, px1, py1, 1.0f,
                                  funkgui::premix(th.ground, funkgui::mix(th.ink70, th.ink16, age), 1.0f));
                    }
                    px0 = px1;
                    py0 = py1;
                    have = true;
                }
            }
            if (cx > floorDb && cx <= layout::kLevelTopDb)
            {
                const float yu = lm.y(cx, scale);
                const float yd = std::min(lm.y(cx - gr, scale), p.bottom());
                {
                    const funkgui::Canvas::Scope scope(c, tag::targetDot, true);
                    c.disc(x, std::min(lm.y(cx - tgt, scale), p.bottom()), B::kTargetDotR, kClear, 1.0f, th.ink52);
                }
                if (yd - yu > 0.0f)
                {
                    const funkgui::Canvas::Scope scope(c, tag::grNeedle, true);
                    c.rrect(x - 0.5f * B::kNeedleW, yu, B::kNeedleW, yd - yu, 0.0f, th.signal);
                }
                {
                    const funkgui::Canvas::Scope scope(c, tag::opDot, true);
                    if ((f.ui.flags & fcdsp::kUiFading) != 0)
                        c.disc(x, yd, B::kOpDotR, kClear, 1.0f, th.ink100);   // hollow while the kernel crossfades
                    else
                        c.disc(x, yd, B::kOpDotR, th.ink100);
                }
            }
        }

        // Handles.
        if (s.haveCurve && s.handleAmt > 0.0f)
        {
            const funkgui::Canvas::Scope scope(c, tag::handle, false);
            for (int h = 0; h < kHandles + 1; ++h)
            {
                const State::HandlePos& hp = s.handles[static_cast<std::size_t>(h)];
                if (!hp.shown)
                    continue;
                const int k = h == 4 ? static_cast<int>(hKnee) : h;
                const funkgui::ValueState st = s.states[static_cast<std::size_t>(k)];
                const float a = s.handleAmt;
                if (!writable(st))
                {
                    c.hairlineH(hp.x - kCrossHalf, hp.y, 2.0f * kCrossHalf, funkgui::fade(th.ink32, a));
                    c.hairlineV(hp.x, hp.y - kCrossHalf, 2.0f * kCrossHalf, funkgui::fade(th.ink32, a));
                    continue;
                }
                const bool hot = s.dragHandle == h || (s.dragHandle < 0 && s.hot == h)
                              || (geom_.handleStops && ctx_.focusVisible
                                  && ctx_.focus == s.sliders[static_cast<std::size_t>(k)].a11yId());
                const float r = 0.5f * (h == hThr ? B::kThresholdHandle : B::kHandle);
                c.disc(hp.x, hp.y, r, funkgui::fade(th.ground, a), 1.0f, funkgui::fade(hot ? th.accent : th.ink70, a));
            }
            for (int k = 0; k < kHandles && geom_.handleStops; ++k)
            {
                const State::HandlePos& hp = s.handles[static_cast<std::size_t>(k)];
                if (hp.shown && ctx_.focusVisible && ctx_.focus == s.sliders[static_cast<std::size_t>(k)].a11yId())
                    funkgui::drawFocusRing(c, { hp.x - 6.0f, hp.y - 6.0f, 12.0f, 12.0f }, th.accent);
            }
        }

        // Chrome: caption, scale cells, unit word, level labels with the detector law.
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("TRANSFER", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        s.scale.draw(c, th, ctx_.focusVisible && ctx_.focus == idBase_ + kGroupId);
        {
            const funkgui::Canvas::Scope scope(c, tag::unitWord, false);
            c.text("DB", geom_.unitRight, geom_.caption.y, T::kCaption, th.ink32, funkgui::Align::right);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            const float step = scale / 4.0f;                      // 3 / 6 / 12 / 18 dB: −12 −24 −36 at S = 48
            for (int k = 3; k >= 1; --k)
            {
                const float v = -step * static_cast<float>(k);
                if (!(v > floorDb))
                    continue;
                char t[16];
                dbText(t, sizeof t, v);
                c.text(t, xOf(v), geom_.labelY, T::kMicro, th.ink32, funkgui::Align::centre);
            }
            // "0 DB PK" with the 0 on its tick (02 §6.5): the law from detectorLaw(eng); custom: the det step's label.
            const char* law = "";
            if (f.entry != nullptr && f.entry->desc->detectorLaw != nullptr)
                switch (f.entry->desc->detectorLaw(f.eng))
                {
                    case fcdsp::DetectorLaw::peak:     law = " PK"; break;
                    case fcdsp::DetectorLaw::rms:      law = " RMS"; break;
                    case fcdsp::DetectorLaw::truePeak: law = " TP"; break;
                    case fcdsp::DetectorLaw::custom:
                    {
                        const fcdsp::ResolvedParam& d = f.res.view[Pid::det];
                        const fcdsp::ParamSpec* ds = f.res.view.spec[fcdsp::idx(Pid::det)];
                        law = ds != nullptr && d.step >= 0 && static_cast<std::size_t>(d.step) < ds->steps.size()
                                  ? ds->steps[static_cast<std::size_t>(d.step)].label : "";
                        break;
                    }
                }
            char zero[40];
            std::snprintf(zero, sizeof zero, "0 DB%s%s", law[0] != '\0' && law[0] != ' ' ? " " : "", law);
            const float x0 = xOf(0.0f) - 0.5f * c.textWidth("0", T::kMicro);
            c.text(zero, x0, geom_.labelY, T::kMicro, th.ink32);
        }
        const funkgui::AxisMap x { p.x, p.right(), floorDb, layout::kLevelTopDb, false };
        const funkgui::AxisMap y { p.bottom(), p.y, floorDb, layout::kLevelTopDb, false };
        c.axis(tag::level, &x, &y);
    }

    // ---- hit testing and input -------------------------------------------------------------------------------------------

    bool TransferPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    int TransferPlot::State::handleAt(funkgui::Point p) const noexcept
    {
        int best = -1;
        float bestD = 0.0f;
        for (int h = 0; h < kHandles + 1; ++h)
        {
            const HandlePos& hp = handles[static_cast<std::size_t>(h)];
            if (!hp.shown)
                continue;
            const float r = 0.5f * (h == hThr ? B::kThresholdHandle : B::kHandle) + kHandleHitPad;
            const float dx = p.x - hp.x, dy = p.y - hp.y;
            const float d = dx * dx + dy * dy;
            if (d <= r * r && (best < 0 || d < bestD))
            {
                best = h;
                bestD = d;
            }
        }
        return best;
    }

    // Handles show while the pointer is over the plot or dragging, always under always-chrome, and while one has the
    // keyboard focus on the Characteristics screen (02 §6.5, §7.5).
    bool TransferPlot::State::handlesOn(const PanelContext& ctx, const layout::TransferGeom& g) const noexcept
    {
        bool focused = false;
        for (const funkgui::RuleSlider& sl : sliders)
            focused = focused || (g.handleStops && ctx.focusVisible && ctx.focus == sl.a11yId());
        return pointerOver || dragHandle >= 0 || ctx.alwaysChrome || focused;
    }

    namespace
    {
        int handleSlot(int h) noexcept { return h == 4 ? static_cast<int>(hKnee) : h; }

        // The handle's value at the pointer, in the plot's own units (02 §6.5; K1 #9): the threshold projects onto unity,
        // the knee width is twice the distance from T_in, the range is the depth below unity.
        float plotValueAt(int k, float xDb, float yDb, float tIn) noexcept
        {
            if (k == hThr)
                return 0.5f * (xDb + yDb);
            if (k == hKnee)
                return 2.0f * std::fabs(xDb - tIn);
            return std::max(0.0f, xDb - yDb);
        }

        // Pointer travel that raises the handle's plain value (px): the threshold along unity (right/up), the knee
        // outwards, ratio and range downwards (a flatter curve, a deeper break).
        float travelOf(int h, funkgui::Point from, funkgui::Point to) noexcept
        {
            const float dx = to.x - from.x, dy = to.y - from.y;
            if (h == hThr)
                return 0.70710678f * (dx - dy);
            if (h == hKnee)
                return dx;
            if (h == 4)
                return -dx;                                       // the knee's left ring
            return dy;
        }
    }

    void TransferPlot::pointerMove(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointerOver = true;
        s.pointer = { e.x, e.y };
        s.hot = s.handleAt(s.pointer);
    }

    void TransferPlot::pointerExit()
    {
        State& s = *st_;
        s.pointerOver = false;
        s.hot = -1;
    }

    void TransferPlot::pointerDown(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        const funkgui::Point p{ e.x, e.y };
        s.pointer = p;
        s.pointerOver = true;
        s.mode = State::Mode::none;
        s.dragHandle = -1;
        if (ctx_.gestures == nullptr || ctx_.host == nullptr)
            return;
        if (s.scale.contains(p))
        {
            s.scale.pointerDown(e, *ctx_.gestures);
            ctx_.meterScaleDb = scaleDb(s.scaleModel.value());
            s.mode = State::Mode::cells;
            return;
        }
        const int h = s.handleAt(p);
        if (h < 0)
            return;
        const int k = handleSlot(h);
        SlotModel& m = ctx_.slot(kHandlePid[static_cast<std::size_t>(k)]);
        funkgui::ParamPort* port = m.port();
        if (e.popup)
        {
            if (port != nullptr)
                ctx_.host->showParamMenu(*port, e.x, e.y);
            return;
        }
        s.dragHandle = h;
        s.down = p;
        const funkgui::ValueView& v = s.sliders[static_cast<std::size_t>(k)].view();
        if (!writable(v.state) || port == nullptr)
        {
            s.mode = State::Mode::refused;                        // the footer shows the reason (hand offer)
            return;
        }
        if (v.state == funkgui::ValueState::stepped && v.detents != nullptr && v.nDetents > 1)
        {
            s.mode = State::Mode::stepped;
            s.detent0 = s.detent = v.detent;
        }
        else
        {
            const float scale = static_cast<float>(ctx_.meterScaleDb);
            const float xDb = layout::kLevelTopDb - scale + (e.x - geom_.plot.x) * scale / geom_.plot.w;
            const float yDb = geom_.level.db(e.y, scale);
            const float tIn = fcdsp::analysis::inputThresholdDb(ctx_.frame.res.eng);
            const float cur = k == hThr ? tIn : m.resolved().plain;
            float probe = 0.0f;
            if (k != hRatio && m.plotToHost01(cur, probe))
            {
                s.mode = State::Mode::absolute;                   // K1 #9: in the plot's own units, grab kept
                s.grab = plotValueAt(k, xDb, yDb, tIn) - cur;
            }
            else
            {
                s.mode = State::Mode::relative;                   // 240 px per full track
                const fcdsp::ParamSpec* sp = m.spec();
                s.invert = sp != nullptr && sp->display.invert;
                s.u0 = s.invert ? 1.0f - v.track : v.track;
            }
        }
        ctx_.gestures->beginDrag(*port);
    }

    void TransferPlot::pointerDrag(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointer = { e.x, e.y };
        if (s.dragHandle < 0 || ctx_.gestures == nullptr || !ctx_.gestures->dragging())
            return;
        const int h = s.dragHandle, k = handleSlot(h);
        SlotModel& m = ctx_.slot(kHandlePid[static_cast<std::size_t>(k)]);
        const float travel = travelOf(h, s.down, { e.x, e.y });
        switch (s.mode)
        {
            case State::Mode::absolute:
            {
                const float scale = static_cast<float>(ctx_.meterScaleDb);
                const float xDb = layout::kLevelTopDb - scale + (e.x - geom_.plot.x) * scale / geom_.plot.w;
                const float yDb = geom_.level.db(e.y, scale);
                const float tIn = fcdsp::analysis::inputThresholdDb(ctx_.frame.res.eng);
                float host01 = 0.0f;
                if (m.plotToHost01(plotValueAt(k, xDb, yDb, tIn) - s.grab, host01))
                    ctx_.gestures->dragTo(host01);
                break;
            }
            case State::Mode::relative:
            {
                const float u = std::clamp(s.u0 + travel / (e.mods.shift ? kTrackPxFine : kTrackPx), 0.0f, 1.0f);
                ctx_.gestures->dragTo(m.host01FromTrack(s.invert ? 1.0f - u : u));
                break;
            }
            case State::Mode::stepped:
            {
                // RuleSlider's stepped rule: one detent per clamp(240/(n−1), 24, 64) px, committed half a pitch + 6 px on.
                const funkgui::ValueView& v = s.sliders[static_cast<std::size_t>(k)].view();
                if (v.detents == nullptr || v.nDetents < 2 || s.detent0 < 0)
                    break;
                const float pitch = std::clamp(kTrackPx / static_cast<float>(v.nDetents - 1), kPitchMin, kPitchMax);
                const float commit = 0.5f * pitch + B::kSnapHysteresisPx;
                int cur = s.detent;
                while (cur < v.nDetents - 1 && travel - static_cast<float>(cur - s.detent0) * pitch > commit)
                    ++cur;
                while (cur > 0 && travel - static_cast<float>(cur - s.detent0) * pitch < -commit)
                    --cur;
                if (cur != s.detent)
                {
                    s.detent = cur;
                    ctx_.gestures->dragTo(v.detents[cur].host01);
                }
                break;
            }
            case State::Mode::none:
            case State::Mode::refused:
            case State::Mode::cells:
                break;
        }
    }

    void TransferPlot::pointerUp(const funkgui::PointerEvent&)
    {
        State& s = *st_;
        const bool writing = s.mode == State::Mode::absolute || s.mode == State::Mode::relative
                          || s.mode == State::Mode::stepped;
        if (writing && ctx_.gestures != nullptr)
            ctx_.gestures->endDrag();
        s.mode = State::Mode::none;
        s.dragHandle = -1;
    }

    void TransferPlot::doubleClick(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        const int h = s.handleAt({ e.x, e.y });
        if (h < 0 || ctx_.gestures == nullptr)
            return;
        const int k = handleSlot(h);
        funkgui::RuleSlider& sl = s.sliders[static_cast<std::size_t>(k)];
        sl.doubleClick(*ctx_.gestures);                           // the Mode default, as the slot (02 §7.4)
        if (writable(sl.view().state))
            ctx_.touch(kHandlePid[static_cast<std::size_t>(k)]);
    }

    bool TransferPlot::wheel(const funkgui::WheelEvent& e)
    {
        State& s = *st_;
        const int h = s.handleAt({ e.x, e.y });
        if (h < 0 || ctx_.gestures == nullptr || ctx_.host == nullptr)
            return false;
        const int k = handleSlot(h);
        funkgui::RuleSlider& sl = s.sliders[static_cast<std::size_t>(k)];
        const bool used = sl.wheel(e, *ctx_.gestures, ctx_.host->nowSeconds());
        if (used && writable(sl.view().state))
            ctx_.touch(kHandlePid[static_cast<std::size_t>(k)]);
        return used;
    }

    bool TransferPlot::key(const funkgui::KeyEvent& e)
    {
        State& s = *st_;
        if (ctx_.gestures == nullptr)
            return false;
        if (ctx_.focus == idBase_ + kGroupId)
        {
            const bool used = s.scale.key(e, *ctx_.gestures);
            ctx_.meterScaleDb = scaleDb(s.scaleModel.value());
            return used;
        }
        for (std::size_t k = 0; k < kHandles && geom_.handleStops; ++k)
            if (ctx_.focus == s.sliders[k].a11yId())
            {
                const bool used = s.sliders[k].key(e, *ctx_.gestures);   // the slot's keys (02 §7.5, §8.9)
                if (used && writable(s.sliders[k].view().state))
                    ctx_.touch(kHandlePid[k]);
                return used;
            }
        return false;
    }

    funkgui::Cursor TransferPlot::cursor(funkgui::Point p) const
    {
        const State& s = *st_;
        const int h = s.dragHandle >= 0 ? s.dragHandle : s.handleAt(p);
        if (h >= 0)
            return writable(s.states[static_cast<std::size_t>(handleSlot(h))]) ? funkgui::Cursor::pointingHand
                                                                                 : funkgui::Cursor::crosshair;
        if (s.scale.contains(p))
            return s.scale.cursorAt(p);
        return funkgui::Cursor::normal;
    }

    bool TransferPlot::wantsFullRate() const
    {
        const State& s = *st_;
        return !s.scale.settled() || !funkgui::ease::sameBits(s.handleAmt, s.handlesOn(ctx_, geom_) ? 1.0f : 0.0f)
            || s.landing;
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------------

    void TransferPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const State& s = *st_;
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = s.title;
        it.readOnly = true;
        out.push_back(std::move(it));
        s.scale.accessibility(out);
        if (!geom_.handleStops)
            return;                                               // on PANEL the slots speak for the handles
        for (std::size_t k = 0; k < kHandles; ++k)
        {
            if (s.states[k] == funkgui::ValueState::na)
                continue;                                         // 02 §7.5: n/a handles are skipped
            funkgui::A11yItem h;
            s.sliders[k].accessibility(h);
            const State::HandlePos& hp = s.handles[k];
            const float r = 0.5f * (k == hThr ? B::kThresholdHandle : B::kHandle) + kHandleHitPad;
            h.bounds = hp.shown ? funkgui::Rect{ hp.x - r, hp.y - r, 2.0f * r, 2.0f * r } : geom_.plot;
            const SlotModel& m = ctx_.slot(kHandlePid[k]);
            const fcdsp::ParamSpec* sp = m.spec();
            if (sp != nullptr && h.role == funkgui::A11yRole::slider && s.states[k] != funkgui::ValueState::stepped)
            {
                // The slot's value interface in display units (SlotGrid's rewrite of RuleSlider's track space).
                const PlainRange span = trackSpan(kHandlePid[k], *sp);
                const double a = toDisplayUnits(kHandlePid[k], *sp, span.a);
                const double b = toDisplayUnits(kHandlePid[k], *sp, span.b);
                h.lo = std::min(a, b);
                h.hi = std::max(a, b);
                h.step = (h.hi - h.lo) * 0.01;
                h.v = std::clamp(toDisplayUnits(kHandlePid[k], *sp, m.resolved().plain), h.lo, h.hi);
            }
            out.push_back(std::move(h));
        }
    }

    int TransferPlot::focusOrder(std::span<uint32_t> out) const
    {
        const State& s = *st_;
        std::size_t n = 0;
        if (n < out.size())
            out[n++] = idBase_ + kGroupId;                        // 02 §8.9 item 5: the scale group
        for (std::size_t k = 0; k < kHandles && geom_.handleStops; ++k)
            if (s.states[k] != funkgui::ValueState::na && n < out.size())
                out[n++] = s.sliders[k].a11yId();                 // 02 §7.5: THRESHOLD, KNEE, RATIO, RANGE
        return static_cast<int>(n);
    }

    void TransferPlot::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        State& s = *st_;
        if (ctx_.gestures == nullptr || a == funkgui::A11yAction::focus)
            return;
        if (s.scale.a11yAction(id, a, value, *ctx_.gestures))
        {
            ctx_.meterScaleDb = scaleDb(s.scaleModel.value());
            return;
        }
        for (std::size_t k = 0; k < kHandles; ++k)
        {
            funkgui::RuleSlider& sl = s.sliders[k];
            if (id != sl.a11yId())
                continue;
            const SlotModel& m = ctx_.slot(kHandlePid[k]);
            double v = value;
            if (a == funkgui::A11yAction::setValue && m.spec() != nullptr && !std::isnan(value)
                && sl.view().state == funkgui::ValueState::continuous)
            {
                // Display units back to the track (accessibility() rewrote the value interface).
                const PlainRange span = trackSpan(kHandlePid[k], *m.spec());
                const float plain = fromDisplayUnits(kHandlePid[k], *m.spec(), value);
                const float lo = std::min(span.a, span.b), hi = std::max(span.a, span.b);
                v = static_cast<double>(std::clamp(m.trackPosition(std::clamp(plain, lo, hi)), 0.0f, 1.0f));
            }
            sl.a11yAction(a, v, *ctx_.gestures);
            const bool write = a == funkgui::A11yAction::setValue || a == funkgui::A11yAction::increment
                            || a == funkgui::A11yAction::decrement;
            if (write && writable(sl.view().state))
                ctx_.touch(kHandlePid[k]);
            return;
        }
    }

    uint32_t TransferPlot::a11yRevision() const { return st_->revision; }
}
