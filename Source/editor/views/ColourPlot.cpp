// Source/editor/views/ColourPlot.cpp — U1a stub of the COLOUR pane (see ColourPlot.h): the plot and harmonics frames,
// the identity, NO COLOUR STAGE for a Mode without one, and the COLOUR_AXIS record (x in, y out, linear −1…+1). U4
// replaces it.
#include "editor/views/ColourPlot.h"

#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

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

        funkgui::Rect area(const layout::ColourGeom& g) noexcept
        {
            return { g.plot.x, g.plot.y, g.harmonics.right() - g.plot.x, g.plot.h };
        }
    }

    ColourPlot::ColourPlot(PanelContext& ctx, const layout::ColourGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void ColourPlot::tick(float) {}

    void ColourPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Rect& p = geom_.plot;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
            outline(c, geom_.harmonics, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::unity, false);
            c.segment(p.x, p.bottom(), p.right(), p.y, 1.0f, th.ink16);
        }
        const fcdsp::ModeDescriptor* d = ctx_.frame.entry != nullptr ? ctx_.frame.entry->desc : nullptr;
        if (d != nullptr && !d->hasColour)
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("NO COLOUR STAGE", p.centreX(), c.capCentreTop(p.centreY(), funkgui::type::kMicro),
                   funkgui::type::kMicro, th.ink32, funkgui::Align::centre);
        }
        const funkgui::AxisMap x { p.x, p.right(), -1.0f, 1.0f, false };
        const funkgui::AxisMap y { p.bottom(), p.y, -1.0f, 1.0f, false };
        c.axis(tag::colourAxis, &x, &y);
    }

    bool ColourPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void ColourPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = area(geom_);
        it.title = "Colour transfer";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int ColourPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
