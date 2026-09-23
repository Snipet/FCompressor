// Source/editor/views/CharScreen.h — the Characteristics screen (02 §7): the middle region y 124–600 of the
// CHARACTERISTICS screen; the chrome stays (Q4). Class declaration frozen at FZ4; U3 (S9) owns it.
//
// CharScreen is a composite: it owns its plots at their Layout.h geometries and dispatches every SubView call to them,
// so the plot cards (U2's shared plots, U4's step / side-chain / colour panes, U3's control path and readouts) show up
// without touching this composer. Plot k gets the a11y ids plotIdBase(ViewIndex::charScreen, k) + 1 … + 255:
//   0 HistoryPlot (kCharsHistory)   1 TransferPlot (kCharsTransfer)   2 MeterColumn (kCharsMeters)   3 Readouts
//   4 ControlPathPlot               5 StepPlot (kStepAttack)           6 StepPlot (kStepRelease)
//   7 SidechainPlot                 8 ColourPlot
// Only one of SIDECHAIN and COLOUR is shown, by UiState::scTab through the SC|COLOUR tab cells this composite owns
// (layout::kTabSidechain, kTabColour; a click or ←/→ on the focused group pins the tab through Panel::setView). The
// hidden pane takes no input and its a11y items are invisible.
//
// Tab order (02 §7.5): the chrome stops of HISTORY (span) and TRANSFER (scale), the SC|COLOUR tab group, the chrome
// stops of the remaining plots (meter reset, …), then every plot's handle stops in plot order (THRESHOLD, KNEE, RATIO,
// RANGE, ATTACK, RELEASE, SC HPF while SIDECHAIN is shown).
#pragma once

#include "editor/SubView.h"
#include "editor/views/ColourPlot.h"
#include "editor/views/ControlPathPlot.h"
#include "editor/views/HistoryPlot.h"
#include "editor/views/MeterColumn.h"
#include "editor/views/Readouts.h"
#include "editor/views/SidechainPlot.h"
#include "editor/views/StepPlot.h"
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
    class CharScreen final : public SubView
    {
    public:
        explicit CharScreen(PanelContext&);

        CharScreen(const CharScreen&) = delete;
        CharScreen& operator=(const CharScreen&) = delete;

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

    private:
        static constexpr int kPlots = 9;
        static constexpr uint32_t kTabGroupId = 1;               // local ids of the tab group and its two cells
        static constexpr uint32_t kTabSidechainId = 2;
        static constexpr uint32_t kTabColourId = 3;

        bool plotShown(int k) const noexcept;                    // the pane of the other tab is hidden
        int  plotAt(funkgui::Point) const noexcept;              // -1: none
        int  plotOf(uint32_t id) const noexcept;                 // -1: not a shown plot's id
        int  tabAt(funkgui::Point) const noexcept;               // 0 sidechain, 1 colour, -1 none
        void pinTab(ScTab);

        PanelContext&   ctx_;
        HistoryPlot     history_;
        TransferPlot    transfer_;
        MeterColumn     meters_;
        Readouts        readouts_;
        ControlPathPlot controlPath_;
        StepPlot        attack_;
        StepPlot        release_;
        SidechainPlot   sidechain_;
        ColourPlot      colour_;
        std::array<SubView*, kPlots> plots_;
        int captured_ = -1;                                      // plot index, or kPlots for the tab cells
        int hovered_  = -1;
    };
}
