// Source/editor/views/MeterColumn.h — the MeterColumn plot (02 §8.8). Class declaration frozen at FZ4; U2 (S7) completes it:
// IN/OUT peak fills with RMS bars and hold ticks, the hanging GR bar with its max-hold tick
// (blockMaxGrDb), the readouts and their one-click reset. Band: kBandMeters (IN L/R, GR, OUT L/R); CharScreen:
// kCharsMeters (IN, SC, GR, OUT).
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
    class MeterColumn final : public SubView
    {
    public:
        MeterColumn(PanelContext&, const layout::MeterGeom&, uint32_t idBase);

        MeterColumn(const MeterColumn&) = delete;
        MeterColumn& operator=(const MeterColumn&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext&         ctx_;
        const layout::MeterGeom geom_;
        const uint32_t        idBase_;
    };
}
