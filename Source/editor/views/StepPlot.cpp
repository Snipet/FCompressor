// Source/editor/views/StepPlot.cpp — U1a stub of one STEP RESPONSE pane (see StepPlot.h): the plot frame, the caption
// (ATTACK or RELEASE), and the STEP_AXIS record (x: log time in seconds from the step, y: the fraction of the
// steady-state GR, 0 at y0 to 1 at y100, hanging). U4 replaces it.
#include "editor/views/StepPlot.h"

#include "editor/PreviewWorker.h"
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

        funkgui::Rect area(const layout::StepGeom& g) noexcept
        {
            const float top = g.caption.y - 4.0f;
            return { g.plot.x, top, g.plot.w, g.labelY + 14.0f - top };
        }
    }

    StepPlot::StepPlot(PanelContext& ctx, const layout::StepGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void StepPlot::tick(float) {}

    void StepPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, geom_.plot, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text(geom_.captionText, geom_.caption.x, geom_.caption.y, funkgui::type::kCaption,
                   ctx_.preview.active() ? th.ink52 : th.ink32);
        }
        const funkgui::AxisMap x { geom_.plot.x, geom_.plot.right(), geom_.tMinS, geom_.tMaxS, true };
        const funkgui::AxisMap y { geom_.y0, geom_.y100, 0.0f, 1.0f, false };
        c.axis(tag::stepAxis, &x, &y);
    }

    bool StepPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void StepPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = geom_.kind == layout::StepKind::attack ? "Attack response" : "Release response";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int StepPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
