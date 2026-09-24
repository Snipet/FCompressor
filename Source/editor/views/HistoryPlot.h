// Source/editor/views/HistoryPlot.h — the HistoryPlot plot (02 §6.5, §7.3, §9.2, §9.6). Class declaration frozen at FZ4; U2 (S7) completes it:
// IN area, OUT line, hanging GR, DET (always on the Characteristics screen, else only when it
// can differ from IN), the grid and 0 DBFS label, the state lane, the threshold line into the TRANSFER handle (a
// vertical drag writes THRESHOLD through its SlotModel), press-and-hold freeze (PanelContext::freeze), Mode ticks
// and gaps, dimming when stale; the span cells (a preference). Band: kBandHistory; CharScreen: kCharsHistory.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U2 (S7) — how it draws (HistoryPlot.cpp):
// - Columns. geom.columns columns of span / columns ms each, cut at ABSOLUTE multiples of that width over the
//   HistoryStore's 1 ms entries (entry e counts in the column whose window [a, a + W) holds e, W fractional on the
//   Characteristics screen), so a column's content never changes once complete and only the newest (partial) one
//   grows. The whole strip is offset by the partial column's phase, which is the smooth scroll of 02 §6.5; "now" is
//   the plot's right edge. A column is the max of its entries (IN, OUT, DET, GR) and the phase of its max-GR entry.
// - Gaps. A column holding a gap entry (bits.b5: a lap marker of the store, or the first column after an attach) or no
//   entry at all is empty: every trace breaks there (one areaStrip per run of columns, never interpolated across) and
//   a GAP-tagged dotted ink16 line marks it on the floor.
// - Traces are seam-free areaStrips through the column centres, each run flat to its outer column edges: HIST_IN (floor
//   to IN, ink16 fill, 1 px ink32 top), HIST_OUT (stroke only, ink70), HIST_DET (stroke only, ink52; only when it can
//   differ from IN, unless alwaysDet), HIST_GR (hanging from the plot top by GR · px/dB, premix(ground, signal, 0.18)
//   fill, 1.5 px signal bottom). Every colour is pre-mixed over the ground (a stale dim is a premix toward it).
// - Audio time. The strip advances only while the feed is live (fresh and kUiLive): stale or silent, the head holds
//   and the traces dim to 50 % over 0.4 s; a press-and-hold freezes it too.
// - Threshold line (THRESHOLD_MARK): a hairline at y(T_in) from the plot's left edge to the TRANSFER threshold handle
//   of the TransferGeom on the same level map; accent while THRESHOLD is under the hand. A vertical drag within ±4 px
//   writes THRESHOLD: absolute through SlotModel::plotToHost01 (T_in has slope 1 in thr), keeping the grab offset; a
//   stepped THRESHOLD moves one detent per clamp(240/(n−1), 24, 64) px after half a pitch + 6 px (RuleSlider's rule).
// - Press and hold elsewhere in the plot: PanelContext::freeze holds the column under the pointer (FREEZE_CURSOR,
//   ink52), the display row reads it; release resumes.
// - Span cells: funkgui::PrefCells over the "historySpanTenths" preference, mirrored into PanelContext each tick. One
//   Tab stop (the group); the image and the group are its a11y items.
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

        // ---- U2 additions (S7): the SubView input the plot takes; no FZ4 declaration above changed --------------------
        ~HistoryPlot() override;
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;
        uint32_t a11yRevision() const override;

    private:
        struct State;                                            // HistoryPlot.cpp: cells, columns, drag, freeze

        PanelContext&         ctx_;
        const layout::HistoryGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
