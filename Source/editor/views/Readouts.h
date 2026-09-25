// Source/editor/views/Readouts.h — the Readouts plot (02 §7.3). Class declaration frozen at FZ4; U3 (S9) completes it:
// eighteen rows: DET, OVER, TARGET, APPLIED, S2 GR, EFF RATIO, ATK EFF, REL EFF, CREST, PHASE,
// then the Mode's declared internals (ModeDescriptor::internals), L/R or M/S pairs when the lanes differ.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U3 (S9) — what each row prints and from what (Readouts.cpp; 02 §7.3, §9.1). Rows at rowPitch from the area's top,
// kMicro: the name ink52 at nameX, the value ink100 right-aligned at valueRight; "–" (U+2013, ink16) before the first
// frame (views/Telemetry.h: feed none) or while the audio runs another Mode than the resolved one (UiFrame::modeSlot).
// UF1a (ADR-69): a fresh frame prints as it is, live or silent; once the audio stops the rows print the last frame at
// rest (Telemetry.h atRest: GR 0.0, levels −∞, crest 0, phase IDLE; ATK/REL EFF and the internals hold), in ink100.
// The values are the frame's lane with the larger applied GR (the operating dot's lane):
//    1 DET        the operating point's x: the peak envelope TransferPlot's dot uses (max of curveXDb and the store's
//                 detMaxDb over the last 10 ms; none at rest), dB, 1 decimal; "−∞" at the telemetry floor
//    2 OVER       DET − T_in (analysis::inputThresholdDb of FrameState::eng), signed ("+3.8"); "−∞" at the floor
//    3 TARGET     the target dot's GR: max of targetGrDb and the store's tgtMaxDb over the last 10 ms, >= 0
//    4 APPLIED    appliedGrDb
//    5 S2 GR      s2GrDb; "–" for a Mode without a second stage (Stage2Kind::none)
//    6 EFF RATIO  analysis::localRatio at DET ("3.2:1", whole numbers from 10, "∞:1" beyond 1e4); "1.0:1" at the floor
//    7 ATK EFF    attackNowMs through the ATTACK spec's law (the slot's units, as its EFF sub), fmt::seconds
//    8 REL EFF    releaseNowMs likewise
//    9 CREST      crestDb
//   10 PHASE      flags bits 16–19 of the lane: IDLE, ATTACK, HOLD, RELEASE
//   11–18         UiFrame::internals[0..7]: ModeDescriptor::internals' name, decimals and unit (internalText)
// Pairs: while FrameState::eng.link < 1 and the two lanes differ by more than layout::readouts::kPairDb, DET shows the
// raw curveXDb of both lanes and APPLIED both appliedGrDb ("−14.2/−15.0"), and the caption reads "READOUTS · L/R" (or
// "· M/S" under kUiMidSide) so the order is named once. A name that would meet its value is cut with an ellipsis (a value
// is never cut). A hairline separates the Mode's internals from rows 1–10.
// A11y: the image "Readouts" and one staticText per shown row (title = the row's name, value = the text drawn,
// description = what it measures), so a screen reader, and probe ui.charscreen, read exactly what is drawn.
#pragma once

#include "editor/Layout.h"
#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace fcdsp
{
    struct InternalSpec;
}

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

        // ---- U3 additions (S9): the rows' state, and the internals' text CONTROL PATH shares; no FZ4 declaration above
        //      changed ---------------------------------------------------------------------------------------------------
        ~Readouts() override;

        // The text of a Mode internal's value, as READOUTS rows 11–18 and CONTROL PATH's internal lane print it: the
        // declared decimals (0..6) and, for a finite value, " " + the declared unit ("42 %", "0.42", "−12.3 DB"); NaN
        // prints "–". Returns the bytes written without the NUL; 0 (and "") when it does not fit in n bytes.
        static int internalText(const fcdsp::InternalSpec&, float value, char* out, std::size_t n) noexcept;

    private:
        struct State;                                            // Readouts.cpp: the rows' names and values, the pairs

        PanelContext&         ctx_;
        const layout::ReadoutsGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
