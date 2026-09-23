// Source/editor/views/StepPlot.h — the StepPlot plot (02 §7.3, §7.4, §9.3). Class declaration frozen at FZ4; U4 (S8) completes it:
// the step responses from the PreviewWorker (attack: three steps; release: two bursts), the
// declared spec marker (a draggable proxy of the atk or rel SlotModel), the measured crossing and the ghosts of
// the other detents. Two instances: kStepAttack and kStepRelease.
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
    class StepPlot final : public SubView
    {
    public:
        StepPlot(PanelContext&, const layout::StepGeom&, uint32_t idBase);

        StepPlot(const StepPlot&) = delete;
        StepPlot& operator=(const StepPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext&         ctx_;
        const layout::StepGeom geom_;
        const uint32_t        idBase_;
    };
}
