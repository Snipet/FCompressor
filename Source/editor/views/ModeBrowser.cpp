// Source/editor/views/ModeBrowser.cpp — U1a stub of the Mode browser (see ModeBrowser.h): the overlay's opaque ground,
// frame and title at layout::kOverlay. The Panel draws it only while its overlay fade is > 0. U5 replaces it.
#include "editor/views/ModeBrowser.h"

#include "editor/Layout.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <string>
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

    ModeBrowser::ModeBrowser(PanelContext& ctx) : ctx_(ctx) {}

    void ModeBrowser::tick(float) {}

    void ModeBrowser::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Rect& r = layout::kOverlay;
        {
            const funkgui::Canvas::Scope scope(c, tag::browserBg, false);
            c.rrect(r.x, r.y, r.w, r.h, 0.0f, th.ground);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, r, th.ink16);
        }
        const funkgui::Canvas::Scope scope(c, tag::caption, false);
        c.text("MODE BROWSER", layout::browser::columnX(0), layout::browser::kHeadingY, funkgui::type::kCaption,
               th.ink52);
    }

    bool ModeBrowser::hit(funkgui::Point p) const { return layout::kOverlay.contains(p); }

    void ModeBrowser::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::modeBrowser, 1);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = layout::kOverlay;
        it.title = "Mode browser";
        if (ctx_.frame.entry != nullptr && ctx_.frame.entry->desc != nullptr)
            it.value = std::string(ctx_.frame.entry->desc->name);
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int ModeBrowser::focusOrder(std::span<uint32_t>) const { return 0; }
}
