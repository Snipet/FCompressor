// Source/editor/views/Footer.h — the Footer sub-view (02 §6.3, §6.6). Class declaration frozen at FZ4; U1b (S6) completes
// it: the spec line (in priority: the first-run hint, state notices, the Mode-switch summary,
// the spec line of the item under the hand, the lookahead hint) and the THEME cells. The last Tab stop.
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
    class Footer final : public SubView
    {
    public:
        explicit Footer(PanelContext&);

        Footer(const Footer&) = delete;
        Footer& operator=(const Footer&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
