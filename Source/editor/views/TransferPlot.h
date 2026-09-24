// Source/editor/views/TransferPlot.h — the TransferPlot plot (02 §6.5, §7.3, §8.7). Class declaration frozen at FZ4; U2 (S7) completes it:
// unity, grid, the static pre-makeup curve (analysis::staticGain), GR wedge, knee marks,
// net curve, ghosts of the other detents and of the previous Mode, target and operating dots, needle and trail,
// the threshold / knee / ratio / range handles proxying their SlotModels, the stage-1 curve (Characteristics), the
// scale cells (a preference) and the LEVEL axis. Band: kBandTransfer; CharScreen: kCharsTransfer.
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
    class TransferPlot final : public SubView
    {
    public:
        TransferPlot(PanelContext&, const layout::TransferGeom&, uint32_t idBase);

        TransferPlot(const TransferPlot&) = delete;
        TransferPlot& operator=(const TransferPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext&         ctx_;
        const layout::TransferGeom geom_;
        const uint32_t        idBase_;
    };
}
