// Source/editor/views/ModeBrowser.h — the ModeBrowser sub-view (02 §8.6). Class declaration frozen at FZ4; U5 (S12) completes
// it: the overlay at layout::kOverlay: one column per Group, the current row marked, the hovered
// row's spec line, click or Return commits `mode` only, Alt commits with the Mode defaults inside one batch,
// Esc or a click outside cancels (the Panel closes it). Drawn while the Panel's overlay fade is > 0.
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
    class ModeBrowser final : public SubView
    {
    public:
        explicit ModeBrowser(PanelContext&);

        ModeBrowser(const ModeBrowser&) = delete;
        ModeBrowser& operator=(const ModeBrowser&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
