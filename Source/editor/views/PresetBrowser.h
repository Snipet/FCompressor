// Source/editor/views/PresetBrowser.h — the PresetBrowser sub-view (02 §6.3, §8.9). Class declaration frozen at FZ4; U6 (S12) completes
// it: the preset overlay at layout::kOverlay over PresetAccess: factory and user rows,
// categories, save-as (LineEdit), menus (MenuLook). Drawn while the Panel's overlay fade is > 0.
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
    class PresetBrowser final : public SubView
    {
    public:
        explicit PresetBrowser(PanelContext&);

        PresetBrowser(const PresetBrowser&) = delete;
        PresetBrowser& operator=(const PresetBrowser&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
