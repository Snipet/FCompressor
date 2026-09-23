// Source/editor/views/PresetStrip.cpp — U1a stub of the preset strip (see PresetStrip.h): its frame and title at
// layout::header::kPresetStrip. U6 replaces it.
#include "editor/views/PresetStrip.h"

#include "editor/Layout.h"
#include "editor/Tags.h"

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
    }

    PresetStrip::PresetStrip(PanelContext& ctx) : ctx_(ctx) {}

    void PresetStrip::tick(float) {}

    void PresetStrip::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Rect& r = layout::kPresetStrip;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, r, th.ink16);
        }
        const funkgui::Canvas::Scope scope(c, tag::presetStrip, false);
        c.text(ctx_.facade.presets().count() > 0 ? "PRESETS" : "NO PRESETS", r.centreX(),
               c.capCentreTop(r.centreY(), funkgui::type::kLabel), funkgui::type::kLabel, th.ink32,
               funkgui::Align::centre);
    }

    bool PresetStrip::hit(funkgui::Point p) const { return layout::kPresetStrip.contains(p); }

    void PresetStrip::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::presetStrip, 1);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = layout::kPresetStrip;
        it.title = "Presets";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int PresetStrip::focusOrder(std::span<uint32_t>) const { return 0; }
}
