// Source/editor/views/Readouts.h — the Readouts plot (02 §7.3). Class declaration frozen at FZ4; U3 (S9) completes it:
// eighteen rows: DET, OVER, TARGET, APPLIED, S2 GR, EFF RATIO, ATK EFF, REL EFF, CREST, PHASE,
// then the Mode's declared internals (ModeDescriptor::internals), L/R or M/S pairs when the lanes differ.
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
    class Readouts final : public SubView
    {
    public:
        Readouts(PanelContext&, const layout::ReadoutsGeom&, uint32_t idBase);

        Readouts(const Readouts&) = delete;
        Readouts& operator=(const Readouts&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext&         ctx_;
        const layout::ReadoutsGeom geom_;
        const uint32_t        idBase_;
    };
}
