// Source/editor/views/ColourPlot.h — the ColourPlot plot (02 §7.3). Class declaration frozen at FZ4; U4 (S8) completes it:
// the colour stage's static transfer at the live GR (analysis::colourCurve), the ± input
// peak markers and the harmonics column (analysis::harmonicsDb). Shown while UiState::scTab is colour.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U4 (S8) — what it draws and from what (ColourPlot.cpp):
// - COLOUR_CURVE: analysis::colourCurve on the frame's EngineParams at the GR it is drawn for — the live applied GR
//   (the lane with more), 0 when not live — 128 segments over −1…+1 (linear on both axes, clipped to the plot), ink70,
//   accent while DRIVE or VOICE is under the hand. Recomputed when the EngineParams or the Mode change, when the live GR
//   moves more than 1 dB from the GR drawn, and when telemetry stops (back to 0).
// - HARMONICS: analysis::harmonicsDb at a −6 dBFS sine (amp 10^(−6/20)) at the same GR, rows H2 H3 H4 H5 THD (THD
//   = the power sum of H2…H8): name ink52, whole-dB value ink100 ("–" below −100 dB), and a 3 px bar over −100…0 dB
//   (the HARMONICS axis record), ink70.
// - COLOUR_MARK (live): ±colourInPeakDb (the lane with more) as ink52 hairlines, when inside the axis.
// - Captions: NO COLOUR STAGE centred in ink32 when !hasColour (no curve, no bars); STATIC APPROXIMATION when
//   !colourStatic; "AT GR 3.2 DB" (live) while the curve is drawn for a live GR.
#pragma once

#include "editor/Layout.h"
#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class ColourPlot final : public SubView
    {
    public:
        ColourPlot(PanelContext&, const layout::ColourGeom&, uint32_t idBase);

        ColourPlot(const ColourPlot&) = delete;
        ColourPlot& operator=(const ColourPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U4 additions (S8; no FZ4 declaration above changed) ------------------------------------------------------
        ~ColourPlot() override;

    private:
        struct State;                                            // ColourPlot.cpp: the curve, the harmonics, the title

        PanelContext&         ctx_;
        const layout::ColourGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
