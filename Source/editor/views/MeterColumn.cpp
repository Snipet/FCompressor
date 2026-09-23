// Source/editor/views/MeterColumn.cpp — U1a stub of METERS (see MeterColumn.h): the bars' frames, their labels, the
// readout frames, the caption where the geometry has one, and the METER_AXIS record (y: the shared level map). U2
// replaces it.
#include "editor/views/MeterColumn.h"

#include "editor/Tags.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <cstddef>
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

    MeterColumn::MeterColumn(PanelContext& ctx, const layout::MeterGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void MeterColumn::tick(float) {}

    void MeterColumn::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace T = funkgui::type;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            for (int i = 0; i < geom_.nBars; ++i)
                outline(c, geom_.bars[static_cast<std::size_t>(i)].r, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::meterReadout, false);
            for (int i = 0; i < geom_.nReadouts; ++i)
                outline(c, geom_.readouts[static_cast<std::size_t>(i)], th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            for (int i = 0; i < geom_.nLabels; ++i)
            {
                const layout::MeterGeom::Label& l = geom_.labels[static_cast<std::size_t>(i)];
                c.text(l.text, l.centreX, geom_.labelY, T::kMicro, th.ink52, funkgui::Align::centre);
            }
        }
        if (geom_.hasCaption)
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("METERS", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        const float top = geom_.level.top;
        const float bottom = geom_.level.top + geom_.level.height;
        const funkgui::AxisMap y { bottom, top, layout::kLevelTopDb - static_cast<float>(ctx_.meterScaleDb),
                                   layout::kLevelTopDb, false };
        c.axis(tag::meterAxis, nullptr, &y);
    }

    bool MeterColumn::hit(funkgui::Point p) const { return geom_.area.contains(p); }

    void MeterColumn::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.area;
        it.title = "Meters";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int MeterColumn::focusOrder(std::span<uint32_t>) const { return 0; }
}
