// Source/editor/views/HistoryPlot.h — the HistoryPlot plot (02 §6.5, §7.3, §9.2, §9.6). Class declaration frozen at FZ4; U2 (S7) completes it:
// IN area, OUT line, hanging GR, DET (always on the Characteristics screen, else only when it
// can differ from IN), the grid and 0 DBFS label, the state lane, the threshold line into the TRANSFER handle (a
// vertical drag writes THRESHOLD through its SlotModel), press-and-hold freeze (PanelContext::freeze), Mode ticks
// and gaps, dimming when stale; the span cells (a preference). Band: kBandHistory; CharScreen: kCharsHistory.
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
    class HistoryPlot final : public SubView
    {
    public:
        HistoryPlot(PanelContext&, const layout::HistoryGeom&, uint32_t idBase);

        HistoryPlot(const HistoryPlot&) = delete;
        HistoryPlot& operator=(const HistoryPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

    private:
        PanelContext&         ctx_;
        const layout::HistoryGeom geom_;
        const uint32_t        idBase_;
    };
}
