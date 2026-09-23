// Source/editor/views/DisplayRow.cpp — U1a stub of the display row (see DisplayRow.h): the region frame, the GAIN
// REDUCTION caption, and the frames and labels of the QUALITY and LOOKAHEAD cells and the three latches at their
// Layout.h rectangles. U1b replaces it.
#include "editor/views/DisplayRow.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"

#include "fcdsp/params/HostParams.h"

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

        void labelled(funkgui::Canvas& c, const funkgui::Rect& r, const char* text, const funkgui::TextStyle& ts,
                      funkgui::Col frame, funkgui::Col ink)
        {
            outline(c, r, frame);
            c.text(text, r.centreX(), c.capCentreTop(r.centreY(), ts), ts, ink, funkgui::Align::centre);
        }
    }

    DisplayRow::DisplayRow(PanelContext& ctx) : ctx_(ctx) {}

    void DisplayRow::tick(float) {}

    void DisplayRow::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace D = layout::display;
        namespace T = funkgui::type;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, layout::kDisplayRow, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::displayCaption, false);
            c.text("GAIN REDUCTION", D::kCaption.x, D::kCaption.y, T::kCaption, th.ink52);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("QUALITY", D::kQualityCaption.x, D::kQualityCaption.y, T::kCaption, th.ink52);
            c.text("LOOKAHEAD", D::kLookaheadCaption.x, D::kLookaheadCaption.y, T::kCaption, th.ink52);
        }
        {
            const funkgui::Canvas::Scope scope(c, funkgui::tags::cell, false);
            const auto& quality = fcdsp::kHostParams[fcdsp::idx(fcdsp::Pid::quality)];
            const auto& budget = fcdsp::kHostParams[fcdsp::idx(fcdsp::Pid::labudget)];
            for (std::size_t i = 0; i < D::kQualityCells.size(); ++i)
                labelled(c, D::kQualityCells[i], quality.choices[i], T::kCaption, th.ink16, th.ink32);
            for (std::size_t i = 0; i < D::kLookaheadCells.size(); ++i)
                labelled(c, D::kLookaheadCells[i], budget.choices[i], T::kCaption, th.ink16, th.ink32);
        }
        const funkgui::Canvas::Scope scope(c, funkgui::tags::latch, false);
        labelled(c, D::kDelta, "DELTA", T::kLatch, th.ink16, th.ink52);
        labelled(c, D::kBypass, "BYPASS", T::kLatch, th.ink16, th.ink52);
        const bool on = ctx_.screen == Screen::characteristics;          // the latch shows the target screen
        if (on)
        {
            const funkgui::Rect& r = D::kCharacteristics;
            c.rrect(r.x, r.y, r.w, r.h, 0.0f, th.ink70);
        }
        labelled(c, D::kCharacteristics, "CHARACTERISTICS", T::kLatch, th.ink16, on ? th.ground : th.ink52);
    }

    bool DisplayRow::hit(funkgui::Point p) const { return layout::kDisplayRow.contains(p); }

    void DisplayRow::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::displayRow, 1);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = layout::kDisplayRow;
        it.title = "Gain reduction";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int DisplayRow::focusOrder(std::span<uint32_t>) const { return 0; }
}
