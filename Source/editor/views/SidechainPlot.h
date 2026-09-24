// Source/editor/views/SidechainPlot.h — the SidechainPlot plot (02 §7.3, §7.4). Class declaration frozen at FZ4; U4 (S8) completes it:
// the detector-path response (analysis::scResponse), the SC HPF corner handle proxying the
// schpf SlotModel, and the INTERNAL / EXTERNAL / LISTENING caption. Shown while UiState::scTab is sidechain.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U4 (S8) — what it draws and from what (SidechainPlot.cpp):
// - SC_CURVE: analysis::scResponse (host SC HPF × `sce` tilt × the Mode's own SC shaping) on the frame's EngineParams
//   (resolve() + overlaySmoothed) at the live sample rate (48 kHz when no UiFrame was read), 161 log-spaced points over
//   20 Hz–20 kHz, 0…−24 dB, clipped to the plot; ink70, accent while SC HPF or SC EMPH is under the hand. Recomputed
//   when the EngineParams, the Mode or the sample rate change.
// - SC_HANDLE: a 5×5 ring at (x(scHpfHz), y(−3 dB)), at the left edge while the HPF is OFF, with the slot's value beside
//   it. It proxies the schpf SlotModel (02 §7.4): a horizontal drag writes Hz absolutely through
//   SlotModel::plotToHost01 (left past 20 Hz = OFF), a stepped SC HPF snaps to its detents with 6 px hysteresis,
//   locked/derived draw an ink32 cross and refuse, n/a has no handle. Wheel, double-click, keys and a11y go through a
//   RuleSlider bound to the same SlotModel (never drawn).
// - Caption inside the plot: INTERNAL, or EXTERNAL · <SC peak> DB PK while fresh telemetry says an external key is
//   active (live text); LISTENING in ink100 while `listen` is on.
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
    class SidechainPlot final : public SubView
    {
    public:
        SidechainPlot(PanelContext&, const layout::SidechainGeom&, uint32_t idBase);

        SidechainPlot(const SidechainPlot&) = delete;
        SidechainPlot& operator=(const SidechainPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U4 additions (S8): the SubView input the pane takes; no FZ4 declaration above changed --------------------
        ~SidechainPlot() override;
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
        struct State;                                            // SidechainPlot.cpp: the curve, the handle, the drag

        PanelContext&         ctx_;
        const layout::SidechainGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
