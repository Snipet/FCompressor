// Source/editor/views/StepPlot.h — the StepPlot plot (02 §7.3, §7.4, §9.3). Class declaration frozen at FZ4; U4 (S8) completes it:
// the step responses from the PreviewWorker (attack: three steps; release: two bursts), the
// declared spec marker (a draggable proxy of the atk or rel SlotModel), the measured crossing and the ghosts of
// the other detents. Two instances: kStepAttack and kStepRelease.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U4 (S8) — what it draws and from what (StepPlot.cpp):
// - It asks the PreviewWorker for the frame's EngineParams (resolve() + overlaySmoothed, FrameState::eng) at the live
//   sample rate (48 kHz when no UiFrame was read) every tick; the worker drops repeats and runs <= 20 Hz.
// - STEP_CURVE: each run's PreviewWorker::Trace, one segment per column edge (116 columns, 1 px each), plus a vertical
//   min/max bar where the samples inside a column leave its chord by more than 0.5 px. y is the fraction of the
//   steady-state GR, 0 % at y0, 100 % at y100 (hanging): an attack run by its own GR before the step (0 %) and at the
//   end of the burst (100 %); both release runs by the 2 s burst's (the +12 dB steady state, 100 %) and its final GR
//   (0 %), so the 50 ms burst starts where its attack got to. Attack +6 / +12 / +24 in ink32 / 52 / 70; release 50 ms /
//   2 s in ink52 / 70; the measured run (+12, 2 s) in accent while a time control or marker is under the hand. A run
//   whose GR moves less than 0.05 dB draws nothing.
// - STEP_SPEC: the declared spec (ModeDescriptor::attackSpec / releaseSpec on the frame's view) as an ink32 hairline at
//   `seconds` on the log-time axis, under its TimeLaw (a rate law has no time: no marker); a program value's published
//   [lo, hi] as an ink16 band. It proxies the atk / rel SlotModel (02 §7.4): continuous drags absolutely through
//   SlotModel::plotToHost01 (seconds), stepped snaps to the detents' positions with 6 px hysteresis, locked/derived add
//   an ink32 cross and refuse (the footer shows the reason), n/a has no marker. Wheel, double-click, keys and a11y go
//   through a RuleSlider bound to the same SlotModel (never drawn), as TRANSFER's handles do.
// - STEP_MEAS: the measured crossing, analysis::measure of the measured run under the spec's law (Trace::measured), as a
//   5 px ink100 ring on that run's curve at the measured time; a disagreement with STEP_SPEC is shown, not hidden.
// - STEP_GHOST: a stepped time slot's other detents (program detents excepted) as their nominal one-pole curves (the
//   detent's time through its law), ink16.
// - Captions inside the plot, kMicro: ATTACK "TGT 7.5 DB AT +12" (the static GR, analysis::staticGain at T_in + 12)
//   and the legend "+6 +12 +24" in the curves' inks; RELEASE "AFTER 50 MS · 2.0 S" in the curves' inks.
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

        // ---- U4 additions (S8): the SubView input the pane takes; no FZ4 declaration above changed --------------------
        ~StepPlot() override;
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        void doubleClick(const funkgui::PointerEvent&) override;
        bool wheel(const funkgui::WheelEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;
        uint32_t a11yRevision() const override;

    private:
        struct State;                                            // StepPlot.cpp: curves, markers, drag, a11y title

        PanelContext&         ctx_;
        const layout::StepGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
