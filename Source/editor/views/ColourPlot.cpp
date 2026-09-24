// Source/editor/views/ColourPlot.cpp — the COLOUR pane (see ColourPlot.h; 02 §7.3, §9.3; 01 §7). tick() recomputes the
// transfer and the harmonics when what they depend on changed; draw() only emits.
#include "editor/views/ColourPlot.h"

#include "editor/Panel.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        using fcdsp::Pid;

        constexpr int   kMaxSegments = 256;                        // >= ColourGeom::segments (128)
        constexpr int   kRows = 5;                                 // H2 H3 H4 H5 THD
        constexpr std::array<const char*, kRows> kRowNames { "H2", "H3", "H4", "H5", "THD" };
        constexpr std::array<const char*, kRows> kSpoken { "second harmonic", "third", "fourth", "fifth",
                                                           "total harmonic distortion" };
        constexpr float kBarFloorDb = -100.0f;                     // the bars' scale: −100 … 0 dB; below: "–"
        constexpr float kBarH = 3.0f;
        constexpr float kBarDy = 16.0f;                            // the bar under the row's name and value
        constexpr float kRowTextDy = 2.0f;
        constexpr float kGrMoveDb = 1.0f;                          // recompute when the live GR moves more (02 §9.3)
        constexpr float kInset = 4.0f;
        constexpr float kTitleS = 0.25f;
        constexpr uint32_t kImageId = 1;

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::ColourGeom& g) noexcept
        {
            return { g.plot.x, g.plot.y, g.harmonics.right() - g.plot.x, g.plot.h };
        }

        // Linear −1…+1 on both axes.
        float xOf(const funkgui::Rect& p, float v) noexcept { return p.x + 0.5f * (v + 1.0f) * p.w; }
        float yOf(const funkgui::Rect& p, float v) noexcept { return p.bottom() - 0.5f * (v + 1.0f) * p.h; }

        // The GR the pane is drawn for: the live applied GR of the lane with more, 0 when not live (02 §7.3).
        float liveGr(const FrameState& f) noexcept
        {
            if (!f.live)
                return 0.0f;
            const float gr = std::max(f.ui.appliedGrDb[0], f.ui.appliedGrDb[1]);
            return gr > 0.0f && std::isfinite(gr) ? gr : 0.0f;
        }

        // Draws the parts of the polyline inside [top, bottom] (x increases along it; TransferPlot's rule).
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
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct ColourPlot::State
    {
        void build(const PanelContext&, const layout::ColourGeom&, float gr);

        uint32_t builtEng = ~0u;
        const fcdsp::ModeEntry* builtEntry = nullptr;
        bool     built = false;
        float    grDb = 0.0f;                                     // the GR the curve and the harmonics are drawn for
        bool     hasColour = false, colourStatic = true;
        int      n = 0;                                           // curve points
        std::array<float, kMaxSegments + 1> x{}, y{};             // px (y unclipped)
        std::array<float, kRows> db{};                            // H2 … H5, THD (dB re the fundamental)

        char     title[200] = "Colour transfer";
        float    titleAge = kTitleS;
    };

    void ColourPlot::State::build(const PanelContext& ctx, const layout::ColourGeom& g, float gr)
    {
        const FrameState& f = ctx.frame;
        builtEng = f.engSerial;
        builtEntry = f.entry;
        built = true;
        grDb = gr;
        n = 0;
        db.fill(-240.0f);
        const fcdsp::ModeDescriptor* d = f.entry != nullptr ? f.entry->desc : nullptr;
        hasColour = d != nullptr && d->hasColour;
        colourStatic = d == nullptr || d->colourStatic;
        if (!hasColour)
            return;

        // COLOUR_CURVE: 128 segments over −1…+1 (01 §7).
        const int segs = std::clamp(g.segments, 1, kMaxSegments);
        n = segs + 1;
        std::array<float, kMaxSegments + 1> xin{}, yout{};
        for (int i = 0; i < n; ++i)
            xin[static_cast<std::size_t>(i)] = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(segs);
        const auto un = static_cast<std::size_t>(n);
        fcdsp::analysis::colourCurve(*f.entry, f.eng, grDb, std::span<const float>(xin.data(), un),
                                     std::span<float>(yout.data(), un));
        for (int i = 0; i < n; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            x[u] = xOf(g.plot, xin[u]);
            y[u] = yOf(g.plot, yout[u]);
        }

        // HARMONICS at a −6 dBFS sine: H2…H5 = h[1..4], THD = the power sum of h[1..7] (01 §7).
        std::array<float, 8> h{};
        const float amp = std::pow(10.0f, g.harmonicsAmpDb / 20.0f);
        fcdsp::analysis::harmonicsDb(*f.entry, f.eng, grDb, amp, std::span<float, 8>(h));
        double power = 0.0;
        for (std::size_t k = 1; k < h.size(); ++k)
            power += std::pow(10.0, static_cast<double>(h[k]) / 10.0);
        for (std::size_t k = 0; k < 4; ++k)
            db[k] = h[k + 1];
        db[4] = static_cast<float>(10.0 * std::log10(std::max(power, 1e-24)));
    }

    // ---- construction ----------------------------------------------------------------------------------------------------

    ColourPlot::ColourPlot(PanelContext& ctx, const layout::ColourGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>())
    {
    }

    ColourPlot::~ColourPlot() = default;

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void ColourPlot::tick(float dt)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        const float gr = liveGr(f);
        const bool grMoved = std::fabs(gr - s.grDb) > kGrMoveDb || (gr == 0.0f && s.grDb != 0.0f);
        const bool rebuilt = !s.built || f.engSerial != s.builtEng || f.entry != s.builtEntry || grMoved;
        if (rebuilt)
            s.build(ctx_, geom_, gr);

        // "Colour transfer at 3.2 dB gain reduction: second harmonic −42 dB, …", regenerated <= 4 Hz.
        s.titleAge += std::max(dt, 0.0f);
        if (rebuilt || s.titleAge >= kTitleS)
        {
            s.titleAge = 0.0f;
            if (f.entry == nullptr)
                std::snprintf(s.title, sizeof s.title, "Colour transfer");
            else if (!s.hasColour)
                std::snprintf(s.title, sizeof s.title, "Colour transfer: no colour stage");
            else
            {
                char grText[16];
                if (funkgui::fmt::db(s.grDb, 1, grText, sizeof grText) < 0)
                    grText[0] = '\0';
                int w = std::snprintf(s.title, sizeof s.title, "Colour transfer at %s dB gain reduction%s", grText,
                                      s.colourStatic ? "" : ", static approximation");
                bool any = false;
                for (int k = 0; k < kRows && w > 0 && static_cast<std::size_t>(w) < sizeof s.title; ++k)
                {
                    const float v = s.db[static_cast<std::size_t>(k)];
                    if (!(v > kBarFloorDb))
                        continue;
                    char vt[16];
                    if (funkgui::fmt::db(v, 0, vt, sizeof vt) < 0)
                        continue;
                    w += std::snprintf(s.title + w, sizeof s.title - static_cast<std::size_t>(w), "%s %s %s dB",
                                       any ? "," : ":", kSpoken[static_cast<std::size_t>(k)], vt);
                    any = true;
                }
                if (!any && w > 0 && static_cast<std::size_t>(w) < sizeof s.title)
                    std::snprintf(s.title + w, sizeof s.title - static_cast<std::size_t>(w), ": no harmonics");
            }
        }
    }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void ColourPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const FrameState& f = ctx_.frame;
        const funkgui::Rect& p = geom_.plot;
        const funkgui::Rect& hr = geom_.harmonics;
        const Pid handPid = ctx_.hand.kind != HandKind::none ? ctx_.hand.pid : fcdsp::kNoPid;
        const bool hot = handPid == Pid::drive || handPid == Pid::voice;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
            outline(c, hr, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            c.hairlineH(p.x + 1.0f, yOf(p, 0.0f), p.w - 2.0f, th.ink16);
            c.hairlineV(xOf(p, 0.0f), p.y + 1.0f, p.h - 2.0f, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::unity, false);
            c.segment(p.x, p.bottom(), p.right(), p.y, 1.0f, th.ink16);
        }
        if (!s.hasColour)
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("NO COLOUR STAGE", p.centreX(), c.capCentreTop(p.centreY(), T::kMicro), T::kMicro, th.ink32,
                   funkgui::Align::centre);
        }
        else
        {
            // ± the colour stage's input peak (live).
            if (f.live)
            {
                const float peak = std::max(f.ui.colourInPeakDb[0], f.ui.colourInPeakDb[1]);
                const float a = std::pow(10.0f, peak / 20.0f);
                if (a > 0.0f && a <= 1.0f)
                {
                    const funkgui::Canvas::Scope scope(c, tag::colourMark, true);
                    c.hairlineV(xOf(p, -a), p.y + 1.0f, p.h - 2.0f, th.ink52);
                    c.hairlineV(xOf(p, a), p.y + 1.0f, p.h - 2.0f, th.ink52);
                }
            }
            if (s.n > 1)
            {
                const funkgui::Canvas::Scope scope(c, tag::colourCurve, false);
                clippedPolyline(c, s.x.data(), s.y.data(), s.n, p.y, p.bottom(), hot ? 1.5f : 1.0f,
                                funkgui::premix(th.ground, hot ? th.accent : th.ink70, 1.0f));
            }
            {
                const funkgui::Canvas::Scope scope(c, tag::caption, false);
                if (!s.colourStatic)
                    c.text("STATIC APPROXIMATION", p.x + kInset, p.y + kInset, T::kMicro, th.ink32);
            }
            if (f.live && s.grDb > 0.0f)
            {
                const funkgui::Canvas::Scope scope(c, tag::caption, true);
                char v[16], text[32];
                if (funkgui::fmt::db(s.grDb, 1, v, sizeof v) >= 0)
                {
                    std::snprintf(text, sizeof text, "AT GR %s DB", v);
                    c.text(text, p.right() - kInset, p.bottom() - kInset - 10.0f, T::kMicro, th.ink32,
                           funkgui::Align::right);
                }
            }

            // Harmonics: name, value, and a bar over −100…0 dB.
            const float pitch = hr.h / static_cast<float>(kRows);
            for (int k = 0; k < kRows; ++k)
            {
                const auto u = static_cast<std::size_t>(k);
                const float top = hr.y + pitch * static_cast<float>(k);
                const float v = s.db[u];
                {
                    const funkgui::Canvas::Scope scope(c, tag::caption, false);
                    c.text(kRowNames[u], hr.x + 2.0f, top + kRowTextDy, T::kMicro, th.ink52);
                    char vt[16] = "–";
                    if (v > kBarFloorDb && funkgui::fmt::db(v, 0, vt, sizeof vt) < 0)
                        std::snprintf(vt, sizeof vt, "–");
                    c.text(vt, hr.right() - 2.0f, top + kRowTextDy, T::kMicro, th.ink100, funkgui::Align::right);
                }
                {
                    const funkgui::Canvas::Scope scope(c, tag::grid, false);
                    c.hairlineH(hr.x + 2.0f, top + kBarDy + kBarH - 1.0f, hr.w - 4.0f, th.ink16);
                }
                const float frac = std::clamp((v - kBarFloorDb) / (0.0f - kBarFloorDb), 0.0f, 1.0f);
                if (frac > 0.0f)
                {
                    const funkgui::Canvas::Scope scope(c, tag::harmonics, false);
                    c.rrect(hr.x + 2.0f, top + kBarDy, frac * (hr.w - 4.0f), kBarH, 0.0f, th.ink70);
                }
            }
        }
        {
            // −1  IN  +1 under the plot; the harmonics' sine level under the column.
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            const float ly = layout::sidechain::kLabelY;
            c.text("−1", p.x, ly, T::kMicro, th.ink32);
            c.text("IN", p.centreX(), ly, T::kMicro, th.ink32, funkgui::Align::centre);
            c.text("+1", p.right(), ly, T::kMicro, th.ink32, funkgui::Align::right);
            char v[16], text[24];
            if (funkgui::fmt::db(geom_.harmonicsAmpDb, 0, v, sizeof v) >= 0)
            {
                std::snprintf(text, sizeof text, "%s DB", v);
                c.text(text, hr.centreX(), ly, T::kMicro, th.ink32, funkgui::Align::centre);
            }
        }
        const funkgui::AxisMap x { p.x, p.right(), -1.0f, 1.0f, false };
        const funkgui::AxisMap y { p.bottom(), p.y, -1.0f, 1.0f, false };
        c.axis(tag::colourAxis, &x, &y);
        const funkgui::AxisMap bars { hr.x + 2.0f, hr.right() - 2.0f, kBarFloorDb, 0.0f, false };
        c.axis(tag::harmonics, &bars, nullptr);
    }

    bool ColourPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void ColourPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = area(geom_);
        it.title = st_->title;
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int ColourPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
