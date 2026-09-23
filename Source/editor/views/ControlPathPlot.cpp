// Source/editor/views/ControlPathPlot.cpp — U1a stub of CONTROL PATH (see ControlPathPlot.h): the plot and lane frames,
// the caption, and the CP_AXIS record (x: time in seconds, now = 0; y: GR in dB, 0 at the top of the GR lane to S/2 at
// its bottom). U3 replaces it.
#include "editor/views/ControlPathPlot.h"

#include "editor/Tags.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <utility>

namespace fcmp::ui
{
    namespace
    {
        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::ControlPathGeom& g) noexcept
        {
            const float top = g.caption.y - 4.0f;
            return { g.plot.x, top, g.plot.w, g.timeLabelY + 14.0f - top };
        }
    }

    ControlPathPlot::ControlPathPlot(PanelContext& ctx, const layout::ControlPathGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void ControlPathPlot::tick(float) {}

    void ControlPathPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, geom_.plot, th.ink16);
            outline(c, geom_.grLane, th.ink16);
            outline(c, geom_.phaseLane, th.ink16);
            outline(c, geom_.internalLane, th.ink16);
            outline(c, geom_.eventsLane, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("CONTROL PATH \xC2\xB7 GR", geom_.caption.x, geom_.caption.y, funkgui::type::kCaption, th.ink52);
        }
        const funkgui::Rect& gr = geom_.grLane;
        const float span = static_cast<float>(ctx_.historySpanTenths) / 10.0f;
        const funkgui::AxisMap x { geom_.plot.x, geom_.plot.right(), -span, 0.0f, false };
        const funkgui::AxisMap y { gr.y, gr.bottom(), 0.0f, static_cast<float>(ctx_.meterScaleDb) * 0.5f, false };
        c.axis(tag::cpAxis, &x, &y);
    }

    bool ControlPathPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void ControlPathPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = "Control path";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int ControlPathPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
