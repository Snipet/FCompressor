// Source/editor/views/Header.h — the Header sub-view (02 §6.3, §8.5). Class declaration frozen at FZ4; U1b (S6) completes
// it: the wordmark, the topology caption (ModeDescriptor::topologyLine) and the Mode latch: caption
// MODE, ‹ and › stepping through the global order (Group, then slot) with one gesture per click or wheel
// burst on `mode` only (02 §8.4.3), the name cell opening the Mode browser (Panel::setView), and the group
// line "VCA · 2 OF 8". A11y: comboBox. Tab stop 1 of 02 §8.9.
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
    class Header final : public SubView
    {
    public:
        explicit Header(PanelContext&);

        Header(const Header&) = delete;
        Header& operator=(const Header&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
