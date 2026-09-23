// Source/editor/views/DisplayRow.h — the DisplayRow sub-view (02 §6.3, §6.6). Class declaration frozen at FZ4; U1b (S6) completes
// it: the big readout (GAIN REDUCTION, else the dragged, hovered or touched item with a 0.9 s
// dwell, or the HISTORY freeze column), its sub-readout, the QUALITY and LOOKAHEAD cells over the global
// ports, and the DELTA, BYPASS and CHARACTERISTICS latches (the last toggles the screen through
// Panel::setView, keeping the tab).
//
// U1b (S6) behaviour, where 02 is silent (U1b handoff):
// - Readout precedence: the HISTORY freeze column; the dragged slot or handle; the hovered one; else the most recent
//   of the last dragged/hovered item (held 0.9 s after the hand leaves it, HR) and PanelContext::touched (0.9 s); else
//   GAIN REDUCTION. Only the 22 Mode-filtered parameters take the readout; the chrome's own items do not.
// - GAIN REDUCTION: the larger lane's appliedGrDb, negated, in `signal` while live and above 0.05 dB (live-tagged),
//   "0.0" ink32 at zero, "–" ink16 when not live; sub "IN −8.1 · OUT −11.9" (the louder channel's peaks), dashes when
//   not live. A slot: its Mode label as the caption, its value text and unit in the slot's value ink, and a sub of
//   CLAMPED FROM …, else the universal name and value when the Mode renames it, else STORED … for a locked or n/a slot
//   whose raw value differs. A value too wide for x 40…216 at kDisplay drops to kValueP (then an ellipsis).
// - QUALITY / LOOKAHEAD are ParamCells over the `quality` / `labudget` ports (CellStyle::text); the UI writes only
//   those ports and never touches latency (K2 #6: SetupWatcher applies them). Their a11y help is formatted from
//   fcdsp::kOs and budgetMs() (K1 #29).
// - DELTA / BYPASS are ParamToggles; CHARACTERISTICS is a ToggleModel over the target screen (Panel::setView, eased,
//   UiState::charExpanded follows). While the bypass ramp runs (a fresh frame with 0 < bypassAmt < 1) BYPASS is drawn
//   with an ink70 fill over bypassAmt of its width (02 §9.1).
// - Every item offers its footer spec line on hover and on focus; DELTA's names it a monitoring latch that a session
//   load turns off (K2 #25c).
//
// The members below the FZ4 declarations are additions (private state and SubView overrides with defaults); no FZ4
// declaration changed.
#pragma once

#include "editor/SubView.h"

#include "fcdsp/params/Pid.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/widgets/LatchToggle.h>
#include <funkgui/widgets/SegmentedSelector.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fcmp::ui
{
    class DisplayRow final : public SubView
    {
    public:
        explicit DisplayRow(PanelContext&);

        DisplayRow(const DisplayRow&) = delete;
        DisplayRow& operator=(const DisplayRow&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U1b additions: the SubView input the cells and latches take ------------------------------------------
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;

    private:
        // The CHARACTERISTICS latch: on = the target screen is CHARACTERISTICS; set() goes through Panel::setView.
        class CharsModel final : public funkgui::ToggleModel
        {
        public:
            explicit CharsModel(PanelContext& ctx) noexcept : ctx_(ctx) {}
            bool on() const override;
            void set(bool, funkgui::GestureController&) override;

        private:
            PanelContext& ctx_;
        };

        enum Widget : int { wQuality, wBudget, wDelta, wBypass, wChars, kWidgets };

        struct Readout                                           // what the big readout shows this frame
        {
            char caption[64] {};
            char value[64] {};
            char unit[16] {};
            char sub[96] {};
            char spoken[96] {};
            funkgui::Col valueInk {};
            funkgui::Col subInk {};
            bool live = false;                                   // live-tagged (telemetry)
        };

        int  widgetAt(funkgui::Point) const noexcept;            // -1: none
        int  widgetOf(uint32_t id) const noexcept;               // the widget owning an a11y id; -1: none
        fcdsp::Pid shownPid() const noexcept;                    // the slot the readout shows; kNoPid: GR / freeze
        void readout(const funkgui::Theme&, Readout&) const;
        void drawBypassRamp(funkgui::Canvas&, const funkgui::Theme&, float amount) const;
        funkgui::Point pointerForWidgets() const noexcept;

        PanelContext& ctx_;
        std::array<std::string, 3> qualityHelp_;                 // a11y help per cell, formatted from fcdsp::kOs
        std::array<std::string, 3> budgetHelp_;
        std::string   qualitySpec_;                              // footer spec lines
        std::string   budgetSpec_;
        funkgui::ParamCells  qualityModel_;
        funkgui::ParamCells  budgetModel_;
        funkgui::ParamToggle deltaModel_;
        funkgui::ParamToggle bypassModel_;
        CharsModel           charsModel_;
        funkgui::SegmentedSelector quality_;
        funkgui::SegmentedSelector budget_;
        funkgui::LatchToggle delta_;
        funkgui::LatchToggle bypass_;
        funkgui::LatchToggle chars_;
        int        captured_ = -1;                               // the widget a pointer down landed on
        bool       pointerOver_ = false;
        fcdsp::Pid recentPid_ = fcdsp::kNoPid;                   // the last dragged or hovered Mode parameter …
        double     recentAt_ = -1.0;                             // … at this panel time
    };
}
