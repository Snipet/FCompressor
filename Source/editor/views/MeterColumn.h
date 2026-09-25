// Source/editor/views/MeterColumn.h — the MeterColumn plot (02 §8.8). Class declaration frozen at FZ4; U2 (S7) completes it:
// IN/OUT peak fills with RMS bars and hold ticks, the hanging GR bar with its max-hold tick
// (blockMaxGrDb), the readouts and their one-click reset. Band: kBandMeters (IN L/R, GR, OUT L/R); CharScreen:
// kCharsMeters (IN, SC, GR, OUT).
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U2 (S7) — how the bars read the telemetry (MeterColumn.cpp), all on the plot's own level map:
// - While live, a bar is exactly the UiFrame value (the engine's meter envelope: instant attack, 40 ms release), so
//   METER_GR equals the processor's applied GR (the max over the lanes, as the TRANSFER needle and HISTORY GR depth).
//   IN/OUT: the peak fill in ink32 (ink100 above 0 dBFS; no red), the RMS as a 2 px (band) or 4 px (Characteristics)
//   ink70 bar inside it. GR hangs from the top in `signal`. SC (Characteristics) = scPeakDb while kUiExtKeyActive.
// - Holds: the IN/OUT peak-hold tick and the GR max-hold tick (from blockMaxGrDb, so a spike between frames survives)
//   hold 1.5 s, then fall at 20 dB/s of panel time; 1 px ink100.
// - UF1a (ADR-69): a fresh frame, live or silent, is drawn as it is; with none (the audio stopped) bars and holds fall
//   at 20 dB/s to the floor. Nothing dims.
// - Readouts (band): the max IN peak, GR and OUT peak since the last reset (updated by fresh frames); kUiOutOver
//   latches an ink100 frame round the OUT readout. ADR-69: "–" (ink16) only before the first frame; afterwards they
//   hold in ink100, a silent maximum printing "−∞" and no GR "0.0" (a reset too). One click on the readout row (the Characteristics METERS caption cell) or Return on its Tab stop
//   resets the maxima, the latch and every hold.
// - A11y: a progressBar per bar ("−4.2 dB") and the reset button.
#pragma once

#include "editor/Layout.h"
#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <cstdint>
#include <memory>
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

        // ---- U2 additions (S7): the SubView input the plot takes; no FZ4 declaration above changed --------------------
        ~MeterColumn() override;
        void pointerDown(const funkgui::PointerEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;

    private:
        struct State;                                            // MeterColumn.cpp: shown values, holds, maxima

        PanelContext&         ctx_;
        const layout::MeterGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
