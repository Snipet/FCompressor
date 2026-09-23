// Source/editor/views/Footer.cpp — U1a stub of the footer (see Footer.h): the region frame, the spec line of the item
// under the hand (PanelContext::hand, fitted to 740 px), and the THEME cells' frames and labels. U1b replaces it.
#include "editor/views/Footer.h"

#include "editor/Layout.h"
#include "editor/Tags.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/text/TextFit.h>

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

    Footer::Footer(PanelContext& ctx) : ctx_(ctx) {}

    void Footer::tick(float) {}

    void Footer::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace F = layout::footer;
        namespace T = funkgui::type;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, layout::kFooter, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::footerSpec, false);
            char line[sizeof ctx_.hand.spec];
            const char* spec = ctx_.hand.spec[0] != '\0' ? ctx_.hand.spec : "FOOTER";
            funkgui::text::fitEllipsis(ctx_.atlas, spec, T::kLabel, F::kSpecMaxW, line, sizeof line);
            c.text(line, F::kSpec.x, F::kSpec.y, T::kLabel, th.ink32);
        }
        const funkgui::Canvas::Scope scope(c, funkgui::tags::cell, false);
        for (std::size_t i = 0; i < F::kThemeCells.size(); ++i)
        {
            const funkgui::Rect& r = F::kThemeCells[i];
            outline(c, r, th.ink16);
            c.text(funkgui::Theme::name(static_cast<int>(i)), r.centreX(), c.capCentreTop(r.centreY(), T::kCaption),
                   T::kCaption, th.ink32, funkgui::Align::centre);
        }
    }

    bool Footer::hit(funkgui::Point p) const { return layout::kFooter.contains(p); }

    void Footer::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::footer, 1);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = layout::kFooter;
        it.title = "Footer";
        it.value = ctx_.hand.spec;
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int Footer::focusOrder(std::span<uint32_t>) const { return 0; }
}
