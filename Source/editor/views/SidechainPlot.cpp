// Source/editor/views/SidechainPlot.cpp — U1a stub of the SIDECHAIN pane (see SidechainPlot.h): the plot frame, its
// title inside the top-left corner, and the SC_AXIS record (x: log frequency in Hz, y: dB). U4 replaces it.
#include "editor/views/SidechainPlot.h"

#include "editor/Tags.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include "fcdsp/telemetry/UiFrame.h"

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
    }

    SidechainPlot::SidechainPlot(PanelContext& ctx, const layout::SidechainGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void SidechainPlot::tick(float) {}

    void SidechainPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Rect& p = geom_.plot;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            const bool external = ctx_.frame.live && (ctx_.frame.ui.flags & fcdsp::kUiExtKeyActive) != 0;
            c.text(external ? "EXTERNAL" : "INTERNAL", p.x + 4.0f, p.y + 4.0f, funkgui::type::kMicro, th.ink32);
        }
        const funkgui::AxisMap x { p.x, p.right(), geom_.fMinHz, geom_.fMaxHz, true };
        const funkgui::AxisMap y { p.y, p.bottom(), geom_.dbTop, geom_.dbBottom, false };
        c.axis(tag::scAxis, &x, &y);
    }

    bool SidechainPlot::hit(funkgui::Point p) const { return geom_.plot.contains(p); }

    void SidechainPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = "Side-chain response";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int SidechainPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
