// Source/editor/views/SidechainPlot.h — the SidechainPlot plot (02 §7.3, §7.4). Class declaration frozen at FZ4; U4 (S8) completes it:
// the detector-path response (analysis::scResponse), the SC HPF corner handle proxying the
// schpf SlotModel, and the INTERNAL / EXTERNAL / LISTENING caption. Shown while UiState::scTab is sidechain.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
// U1a stub: it draws its frame and caption at the geometry's positions and records its axes.
#pragma once

#include "editor/Layout.h"
#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>

#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class SidechainPlot final : public SubView
    {
    public:
        SidechainPlot(PanelContext&, const layout::SidechainGeom&, uint32_t idBase);

        SidechainPlot(const SidechainPlot&) = delete;
        SidechainPlot& operator=(const SidechainPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext&         ctx_;
        const layout::SidechainGeom geom_;
        const uint32_t        idBase_;
    };
}
