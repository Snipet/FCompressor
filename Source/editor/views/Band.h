// Source/editor/views/Band.h — the band (02 §6.5): always visible on PANEL, HISTORY + TRANSFER + METERS on one level
// map (4 px/dB at S = 48), no view switching (02 §0.8). Class declaration frozen at FZ4; U2 (S7) owns it.
//
// Band is a composite: it owns its three plots at their Layout.h geometries (kBandHistory, kBandTransfer, kBandMeters)
// and dispatches every SubView call to them, so the plot cards show up without touching this composer. Plot k gets the
// a11y ids plotIdBase(ViewIndex::band, k) + 1 … + 255. A pointer down is captured by the plot it hit until released;
// a double-click on empty band area (no plot claims it) opens CHARACTERISTICS (02 §6.5, §7.1). Tab stops: each plot's
// chrome stops in plot order (span group, scale group, meter reset; 02 §8.9) — on PANEL the slots, not the handles,
// are the Tab stops.
//
// UF2 (S12, ADR-72): HISTORY's caption is the HISTORY · VU switch, and VU shows a GR needle meter in HISTORY's plot
// (HistoryPlot owns both; no declaration here changed). Its first cell starts 7 px left of kBand, so hit() also takes
// what the HISTORY plot claims.
#pragma once

#include "editor/SubView.h"
#include "editor/views/HistoryPlot.h"
#include "editor/views/MeterColumn.h"
#include "editor/views/TransferPlot.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class Band final : public SubView
    {
    public:
        explicit Band(PanelContext&);

        Band(const Band&) = delete;
        Band& operator=(const Band&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        void doubleClick(const funkgui::PointerEvent&) override;
        bool wheel(const funkgui::WheelEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;
        uint32_t a11yRevision() const override;

        // ---- S13 H1a addition (additive): the Panel calls it each frame the band is not shown (CHARACTERISTICS) -----
        void keepTime(float dt);                                 // HistoryPlot::keepTime

    private:
        static constexpr int kPlots = 3;

        int plotAt(funkgui::Point) const noexcept;               // -1: none
        int plotOf(uint32_t id) const noexcept;                  // -1: not a plot's id

        PanelContext& ctx_;
        HistoryPlot   history_;
        TransferPlot  transfer_;
        MeterColumn   meters_;
        std::array<SubView*, kPlots> plots_;                     // plot k: history, transfer, meters
        int captured_ = -1;
        int hovered_  = -1;
    };
}
