// Source/editor/views/PresetStrip.h — the PresetStrip sub-view (02 §6.3). Class declaration frozen at FZ4; U6 (S12) completes
// it: the header strip at layout::header::kPresetStrip over PresetAccess: ‹ name ›, the modified
// marker, save; the name opens the preset browser (Panel::setView). Tab stops 2 of 02 §8.9.
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
    class PresetStrip final : public SubView
    {
    public:
        explicit PresetStrip(PanelContext&);

        PresetStrip(const PresetStrip&) = delete;
        PresetStrip& operator=(const PresetStrip&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext& ctx_;
    };
}
