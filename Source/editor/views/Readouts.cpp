// Source/editor/views/Readouts.cpp — U1a stub of READOUTS (see Readouts.h): the frame, the caption and, so the stub
// shows each Mode's declared internals, the names of rows 11–18 (ModeDescriptor::internals). U3 replaces it.
#include "editor/views/Readouts.h"

#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

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

        constexpr int kFirstInternalRow = layout::readouts::kFirstInternalRow;   // rows 11–18
    }

    Readouts::Readouts(PanelContext& ctx, const layout::ReadoutsGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase)
    {
    }

    void Readouts::tick(float) {}

    void Readouts::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, geom_.area, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("READOUTS", geom_.caption.x, geom_.caption.y, funkgui::type::kCaption, th.ink52);
        }
        const fcdsp::ModeDescriptor* d = ctx_.frame.entry != nullptr ? ctx_.frame.entry->desc : nullptr;
        if (d == nullptr)
            return;
        const funkgui::Canvas::Scope scope(c, tag::readoutName, false);
        const std::size_t n = d->internals.size();
        for (std::size_t i = 0; i < n && static_cast<int>(i) + kFirstInternalRow < geom_.rows; ++i)
        {
            const float y = geom_.area.y + geom_.rowPitch * static_cast<float>(static_cast<int>(i) + kFirstInternalRow);
            c.text(d->internals[i].name, geom_.nameX, y, funkgui::type::kMicro, th.ink52);
        }
    }

    bool Readouts::hit(funkgui::Point p) const { return geom_.area.contains(p); }

    void Readouts::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + 1;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.area;
        it.title = "Readouts";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int Readouts::focusOrder(std::span<uint32_t>) const { return 0; }
}
