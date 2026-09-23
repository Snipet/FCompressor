// Source/editor/views/SlotGrid.h — the SlotGrid sub-view (02 §6.1, §6.4, §8.1–§8.4, §8.7). Class declaration frozen at FZ4; U1s (S6) completes
// it: the 21 slots in 3 × 7 at layout::kSlots, each a RuleSlider over
// PanelContext::slot(pid), with the AUTO, EXT and LISTEN words (WordModel), the Mode-switch landing (carets
// ease, changed labels flash, 02 §8.7) and the spec line of the hovered, focused or dragged slot
// (PanelContext::offerHand). Every slot, word included, is a Tab stop (02 §8.9).
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
    class SlotGrid final : public SubView
    {
    public:
        explicit SlotGrid(PanelContext&);

        SlotGrid(const SlotGrid&) = delete;
        SlotGrid& operator=(const SlotGrid&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
