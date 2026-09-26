// Source/editor/views/HistoryPlot.h — the HistoryPlot plot (02 §6.5, §7.3, §9.2, §9.6). Class declaration frozen at FZ4; U2 (S7) completes it:
// IN area, OUT line, hanging GR, DET (always on the Characteristics screen, else only when it
// can differ from IN), the grid and 0 DBFS label, the state lane, the threshold line into the TRANSFER handle (a
// vertical drag writes THRESHOLD through its SlotModel), press-and-hold freeze (PanelContext::freeze), Mode ticks
// and gaps; the span cells (a preference). Band: kBandHistory; CharScreen: kCharsHistory.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U2 (S7) — how it draws (HistoryPlot.cpp):
// - Columns. geom.columns columns of span / columns ms each, cut at ABSOLUTE multiples of that width over the timeline
//   (views/Telemetry.h HistoryTimeline: the HistoryStore's 1 ms entries, placed in audio time while the feed is fresh;
//   an entry counts in the column whose window [a, a + W) holds its position, W fractional on the Characteristics
//   screen; HistoryStore::columnWindows' arithmetic), so a column's content never changes once complete and only the
//   newest (partial) one grows. The whole strip is offset by the partial column's phase, which is the smooth scroll of
//   02 §6.5; "now" is the plot's right edge. A column is the max of its entries (IN, OUT, DET, GR) and the phase of its
//   max-GR entry.
// - Gaps. A column holding a gap entry (bits.b5: a lap marker of the store, or the first column after an attach), or
//   timeline time no entry maps to (the audio stopped), or no entry at all is empty: every trace breaks there (one
//   areaStrip per run of columns, never interpolated across); the first two are marked by a GAP-tagged dotted ink16
//   line on the floor.
// - Traces are seam-free areaStrips through the column centres, each run flat to its outer column edges: HIST_IN (floor
//   to IN, ink16 fill, 1 px ink32 top), HIST_OUT (stroke only, ink70), HIST_DET (stroke only, ink52; only when it can
//   differ from IN, unless alwaysDet), HIST_GR (hanging from the plot top by GR · px/dB, premix(ground, signal, 0.18)
//   fill, 1.5 px signal bottom). Every colour is pre-mixed over the ground; nothing ever dims.
// - Time (UF1a, ADR-69, revising 02 §6.5's "audio time" rule for the stopped case): while the feed is fresh (live or
//   silent) the strip advances with the audio, as before; once it is stale (the host stopped calling processBlock) it
//   keeps scrolling at wall-clock rate over a gap, so the picture never freezes; when the audio returns it lands after
//   the gap. A press-and-hold freezes the view. Full rate while ink the head moves is in view (a data column above the
//   floor, a Mode tick, a state-lane run, a gap's inner edge); at rest otherwise.
// - Threshold line (THRESHOLD_MARK): a hairline at y(T_in) from the plot's left edge to the TRANSFER threshold handle
//   of the TransferGeom on the same level map; accent while THRESHOLD is under the hand. A vertical drag within ±4 px
//   writes THRESHOLD: absolute through SlotModel::plotToHost01 (T_in has slope 1 in thr), keeping the grab offset; a
//   stepped THRESHOLD moves one detent per clamp(240/(n−1), 24, 64) px after half a pitch + 6 px (RuleSlider's rule).
// - Press and hold elsewhere in the plot: PanelContext::freeze holds the column under the pointer (FREEZE_CURSOR,
//   ink52), the display row reads it; release resumes.
// - Span cells: funkgui::PrefCells over the "historySpanTenths" preference, mirrored into PanelContext each tick. One
//   Tab stop (the group); the image and the group are its a11y items.
//
// UF2 (S12, ADR-72) — the band's GR view (no declaration above changed; HistoryPlot.cpp's State holds it):
// - On the band only (kBandHistory; the Characteristics screen's HISTORY keeps its "HISTORY" caption), the caption is
//   a two-cell switch HISTORY · VU (layout::vu::kViewCells, CellStyle::text: the span cells' look and hit rule) over
//   the machine-wide UiPreferences int "grView" (0 HISTORY, the default; 1 VU), read every tick like the span. One more
//   Tab stop before the span group (a radio group "GR view"), the same keys and a11y as the span cells.
// - VU shows the GrVuMeter (views/GrVuMeter.h) in the plot rectangle instead of the grid, traces, gaps, Mode ticks and
//   freeze; the span cells, their "S" and the time labels are hidden, not dimmed (and leave the a11y model and the Tab
//   order); the image item gives way to the meter's. The plot frame, the state lane and the threshold line's part
//   outside the plot are unchanged, so nothing outside the plot rectangle, the caption row and the time-label row
//   moves. The columns keep advancing under VU, so HISTORY comes back without a gap; the meter is ticked under
//   HISTORY too, so VU shows the needle where it is. Under VU the plot takes no pointer input (no threshold drag, no
//   freeze); a double-click on it opens CHARACTERISTICS as on any empty band area.
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

        // ---- S13 H1a addition (additive): the clock while the plot is not shown -------------------------------------
        // Its composite calls it each frame the Panel does not tick it (the other screen is shown): the timeline keeps
        // time, so a stale span that starts and ends while the plot is hidden is still a gap when it is shown again
        // (UF1a, ADR-69), and the band's VU needle stays current (it is ticked under HISTORY for the same reason).
        void keepTime(float dt);

    private:
        struct State;                                            // HistoryPlot.cpp: cells, columns, drag, freeze

        PanelContext&         ctx_;
        const layout::HistoryGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
