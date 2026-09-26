// Source/editor/views/ControlPathPlot.h — the ControlPathPlot plot (02 §7.3). Class declaration frozen at FZ4; U3 (S9) completes it:
// target vs applied GR with the intra-millisecond min/max band, the phase lane, the Mode's
// history internal and the event stripes, built from HistoryColumns alone.
//
// A plot is a SubView of its composite (Band or CharScreen), which constructs it at a Layout.h geometry and gives it
// the a11y ids idBase + 1 … idBase + 255 (1..127 its cells and image, 128..255 its handles and markers; SubView.h).
//
// U3 (S9) — what it draws and from what (ControlPathPlot.cpp):
// - Columns. The Characteristics HISTORY's columns exactly (HistoryStore::columnWindows, the rule HistoryPlot uses: 210
//   columns of span / 210 ms at absolute multiples, offset by the partial column's phase; "now" is the right edge, over
//   the same wall-clock timeline, views/Telemetry.h, ticked in the same frames), so CONTROL PATH is x-aligned with
//   HISTORY column for column. A column is the max of its entries' grMaxDb and tgtMaxDb,
//   the min of their grMinDb, the phase of its max-GR entry (as HISTORY's state lane), the OR of their event bits, and
//   the last entry's internal0 (01 §6.3) with that entry's Mode slot. A column holding a gap entry (b5), timeline time
//   no entry maps to (the audio stopped), or no entry is empty: every trace breaks there (never interpolated), and a
//   GAP-tagged dotted ink16 line marks the first two.
// - GR lane (grLane, hanging, 0 at the top, 0…S/2 dB over its height; quarter-scale grid lines, ink16): CP_APPLIED is
//   the area from the top to grMaxDb (premix(ground, signal, 0.18) fill, 1.5 px signal bottom stroke) plus a 1 px ink32
//   stroke-only line at grMinDb, so intra-millisecond GR movement shows as the band between them; CP_TARGET is
//   tgtMaxDb, stroke-only ink52 on its bottom edge. GR beyond S/2 clamps to the lane's bottom.
// - Phase lane: the columns' phases, runs merged (<= 40, newest first): ATTACK ink70, HOLD ink100, RELEASE ink32.
// - Internal lane: internal0 normalised through the lo…hi of the Mode that wrote the column (its InternalSpec with
//   history = true; a column of a Mode without one breaks the trace), stroke-only ink70 (CP_INTERNAL), clamped to the
//   lane. The current Mode's internal name (ink52) and its live value (UiFrame::internals, the declared decimals and
//   unit, ink70) sit in kMicro inside the lane's top-left; held from the last frame once the audio stops, "–" (ink16)
//   only before the first frame or while the audio runs another Mode (ADR-69). A Mode with none shows NO HISTORY
//   INTERNAL in ink32.
// - Events lane: four 5 px stripe rows from the columns' bits, runs merged, each labelled in kMicro ink32 at
//   eventLabelX: AUTO SLOW (b2, ink52), RANGE (b3, ink70), STAGE 2 (b4, ink100), FADE (b6, ink32).
// - Time, as HISTORY (UF1a, ADR-69): audio time while the feed is fresh, the wall clock over a gap once it is stale,
//   held while HISTORY is frozen (press and hold, PanelContext::freeze); nothing ever dims; while frozen the
//   FREEZE_CURSOR line crosses this plot at HISTORY's frozen column. Full rate while ink the head moves is in view.
//   Time labels every timeLabelPitch px, as HISTORY's. The CP_AXIS record: x time in seconds (now = 0), y GR in dB over
//   the GR lane (0 at the top, S/2 at the bottom).
// - Columns and strips are rebuilt in tick() (draw() only emits), when the head, the store, the span or the scale move.
// - A11y: one image whose title is regenerated at <= 4 Hz ("Control path: target 5.1 dB, applied 3.8 dB, opto memory 42
//   %"). No Tab stop: the plot takes no input.
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
    class ControlPathPlot final : public SubView
    {
    public:
        ControlPathPlot(PanelContext&, const layout::ControlPathGeom&, uint32_t idBase);

        ControlPathPlot(const ControlPathPlot&) = delete;
        ControlPathPlot& operator=(const ControlPathPlot&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U3 additions (S9): the plot's state and its full-rate rule; no FZ4 declaration above changed -------------
        ~ControlPathPlot() override;
        bool wantsFullRate() const override;

        // ---- S13 H1a addition (additive): HISTORY's clock while the plot is not shown -------------------------------
        // CharScreen calls it each frame the Panel does not tick it (PANEL is shown): the plot's timeline keeps time,
        // so a stale span that starts and ends while it is hidden is still a gap when it is shown again (UF1a, ADR-69).
        void keepTime(float dt);

    private:
        struct State;                                            // ControlPathPlot.cpp: columns, strips, lanes, title

        PanelContext&         ctx_;
        const layout::ControlPathGeom geom_;
        const uint32_t        idBase_;
        std::unique_ptr<State> st_;
    };
}
