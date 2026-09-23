// Source/editor/views/HistoryPlot.cpp — U1a stub of HISTORY (see HistoryPlot.h): the plot frame, the caption, the span
// cells with the preference's cell active, the unit word, the state lane's frame, and the HIST_AXIS record (x: time in
// seconds, now = 0 at the right edge; y: the shared level map). U2 replaces it.
#include "editor/views/HistoryPlot.h"

#include "editor/Tags.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <cstddef>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        constexpr std::array<const char*, 4> kSpanLabels { "2.5", "5", "10", "20" };

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::HistoryGeom& g) noexcept
        {
            const float top = g.spanCells[0].y;
            return { g.plot.x, top, g.plot.w, g.timeLabelY + 14.0f - top };
        }
    }

    HistoryPlot::HistoryPlot(PanelContext& ctx, const layout::HistoryGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void HistoryPlot::tick(float) {}

    void HistoryPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace T = funkgui::type;
        const funkgui::Rect& p = geom_.plot;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
            outline(c, geom_.stateLane, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("HISTORY", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        {
            const funkgui::Canvas::Scope scope(c, funkgui::tags::cell, false);
            for (std::size_t i = 0; i < geom_.spanCells.size(); ++i)
            {
                const funkgui::Rect& r = geom_.spanCells[i];
                const bool active = layout::kSpansTenths[i] == ctx_.historySpanTenths;
                outline(c, r, th.ink16);
                c.text(kSpanLabels[i], r.centreX(), c.capCentreTop(r.centreY(), T::kCaption), T::kCaption,
                       active ? th.ink70 : th.ink32, funkgui::Align::centre);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::unitWord, false);
            c.text("S", geom_.unitRight, geom_.caption.y, T::kCaption, th.ink32, funkgui::Align::right);
        }
        const float span = static_cast<float>(ctx_.historySpanTenths) / 10.0f;
        const float scale = static_cast<float>(ctx_.meterScaleDb);
        const funkgui::AxisMap x { p.x, p.right(), -span, 0.0f, false };
        const funkgui::AxisMap y { p.y, p.bottom(), layout::kLevelTopDb, layout::kLevelTopDb - scale, false };
        c.axis(tag::histAxis, &x, &y);
    }

    bool HistoryPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void HistoryPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = "History";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int HistoryPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
