// Source/editor/views/TransferPlot.h — the TransferPlot plot (02 §6.5, §7.3, §8.7). Class declaration frozen at FZ4; U2 (S7) completes it:
// unity, grid, the static pre-makeup curve (analysis::staticGain), GR wedge, knee marks,
// net curve, ghosts of the other detents and of the previous Mode, target and operating dots, needle and trail,
// the threshold / knee / ratio / range handles proxying their SlotModels, the stage-1 curve (Characteristics), the
// scale cells (a preference) and the LEVEL axis. Band: kBandTransfer; CharScreen: kCharsTransfer.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U2 (S7) — what it draws and from what (TransferPlot.cpp):
// - Every curve comes from 01 §7 on the frame's EngineParams: resolve() of the raw values, with the smoothed UiFrame
//   fields overlaid while live (overlaySmoothed; FrameState::eng, K1 #7, K2 #24), never from UiFrame alone.
//   TRANSFER_CURVE is y = x + staticGain(x) − preGainDb (GR only), sampled 64 uniform + 32 across [T_in ± W/2] + 16
//   around the range break (120 uniform for custom curves and feedback topologies), then refined where a chord misses
//   the curve by more than 0.1 px (so C's G2 chord error <= 0.25 px holds for any curve), clipped to the plot.
//   Recomputed only when FrameState::engHash or the scale changes. NET_CURVE (x + netGainDb(gain, makeup, mix)) only
//   where it differs by > 0.05 dB; STAGE_CURVE (stage2 = false) on the Characteristics screen for two-stage Modes.
// - GHOST_CURVE: the other detents of a stepped RATIO or KNEE, resolve()d with that detent's plain value in a copy of
//   the raw values (ink16; the detent label under the pointer in the slot grid previews its curve in ink32); after a
//   Mode switch the old curve eases into the new one over 160 ms and stays in ink16 for 0.9 s (02 §8.7).
// - Live only: TARGET_DOT, OP_DOT (hollow while kUiFading), GR_NEEDLE from unity to the dot, OP_TRAIL (the last 320 ms
//   of the store every 10 ms, ink70 → ink16), all at the operating point of views/Telemetry.h: cx = curveXDb of the
//   lane with the larger applied GR, peak-held over the store's last 10 ms. UF1a (ADR-69): when the feed stops being
//   live they stay where they were and fade out over layout::live::kFadeS (full rate while they fade); the curves
//   never dim.
// - Handles (shown while the pointer is over the plot or dragging, 90/160 ms; always under always-chrome and on the
//   Characteristics screen's focus): threshold (7×7 on unity at T_in; absolute: thr += ΔT_in), knee (two 5×5 at
//   T_in ± W/2), ratio (5×5 at min(T_in + 12, +3)), range (5×5 at the break). Knee and range drag absolutely only when
//   SlotModel::plotToHost01 allows it (kFlagPlotIsPlain + identity map), else relatively at 240 px per track; stepped
//   parameters move one detent per clamp(240/(n−1), 24, 64) px after half a pitch + 6 px. Locked/derived: a 1 px ink32
//   cross that refuses; n/a: nothing. Wheel, double-click, keys and a11y go through a RuleSlider bound to the same
//   SlotModel (never drawn), so a handle behaves and speaks exactly as its slot (02 §7.4, §7.5).
// - Scale cells: funkgui::PrefCells over the "meterScaleDb" preference, mirrored into PanelContext each tick.
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

        // ---- U2 additions (S7): the SubView input the plot takes; no FZ4 declaration above changed --------------------
        ~TransferPlot() override;
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        void doubleClick(const funkgui::PointerEvent&) override;
        bool wheel(const funkgui::WheelEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;
        uint32_t a11yRevision() const override;

    private:
        struct State;                                            // TransferPlot.cpp: curves, handles, drag, cells

        PanelContext&         ctx_;
        const layout::TransferGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
