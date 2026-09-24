// Source/editor/views/TransferPlot.cpp — U1a stub of TRANSFER (see TransferPlot.h): the square plot's frame, the
// caption, the scale cells with the preference's cell active, the unit word, unity, and the LEVEL axis record (x input
// level, y output level; 02 §3.8's example: "axis LEVEL x 556 748 -42 6 lin y 332 140 -42 6 lin"). U2 replaces it.
#include "editor/views/TransferPlot.h"

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
        constexpr std::array<const char*, 4> kScaleLabels { "12", "24", "48", "72" };

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
    }

    TransferPlot::TransferPlot(PanelContext& ctx, const layout::TransferGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void TransferPlot::tick(float) {}

    void TransferPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace T = funkgui::type;
        const funkgui::Rect& p = geom_.plot;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("TRANSFER", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        {
            const funkgui::Canvas::Scope scope(c, funkgui::tags::cell, false);
            for (std::size_t i = 0; i < geom_.scaleCells.size(); ++i)
            {
                const funkgui::Rect& r = geom_.scaleCells[i];
                const bool active = layout::kScalesDb[i] == ctx_.meterScaleDb;
                outline(c, r, th.ink16);
                c.text(kScaleLabels[i], r.centreX(), c.capCentreTop(r.centreY(), T::kCaption), T::kCaption,
                       active ? th.ink70 : th.ink32, funkgui::Align::centre);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::unitWord, false);
            c.text("DB", geom_.unitRight, geom_.caption.y, T::kCaption, th.ink32, funkgui::Align::right);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::unity, false);
            c.segment(p.x, p.bottom(), p.right(), p.y, 1.0f, th.ink16);   // square: unity is exactly 45°
        }
        const float floorDb = layout::kLevelTopDb - static_cast<float>(ctx_.meterScaleDb);
        const funkgui::AxisMap x { p.x, p.right(), floorDb, layout::kLevelTopDb, false };
        const funkgui::AxisMap y { p.bottom(), p.y, floorDb, layout::kLevelTopDb, false };
        c.axis(tag::level, &x, &y);
    }

    bool TransferPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void TransferPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = "Transfer curve";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int TransferPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
