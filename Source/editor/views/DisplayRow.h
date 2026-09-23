// Source/editor/views/DisplayRow.h — the DisplayRow sub-view (02 §6.3, §6.6). Class declaration frozen at FZ4; U1b (S6) completes
// it: the big readout (GAIN REDUCTION, else the dragged, hovered or touched item with a 0.9 s
// dwell, or the HISTORY freeze column), its sub-readout, the QUALITY and LOOKAHEAD cells over the global
// ports, and the DELTA, BYPASS and CHARACTERISTICS latches (the last toggles the screen through
// Panel::setView, keeping the tab).
//
// U1a stub: it draws its frame and title at the Layout.h positions.
#pragma once

#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>

#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class DisplayRow final : public SubView
    {
    public:
        explicit DisplayRow(PanelContext&);

        DisplayRow(const DisplayRow&) = delete;
        DisplayRow& operator=(const DisplayRow&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
