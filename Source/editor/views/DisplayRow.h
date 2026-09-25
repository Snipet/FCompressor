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
// - GAIN REDUCTION (UF1a, ADR-70; replaces U1b's per-frame applied GR, which flickered): the max GR over the last 1.0 s
//   (views/Telemetry.h grHoldDb: the store's newest columns and the newest frame, the stale time counting as silence),
//   negated, its text refreshed every 0.25 s of panel time (≈ 4 Hz, quantised in time, not per frame): in `signal`
//   above 0.05 dB, "0.0" ink32 at zero (live-tagged). Sub "IN −8.1 · OUT −11.9": the louder channel's peaks, the max
//   over the fresh frames since the last refresh ("−∞" when there were none). Beside the value, on its baseline, a thin
//   live GR bar (signal, 2 px, the band's px/dB from x 224, over a 1 px ink16 track) with a 1×6 ink100 tick at the
//   held value. ADR-69: once the audio stops the bar falls at 20 dB/s, the hold empties within 1 s and the readout
//   rests at 0.0, IN · OUT at −∞ — "–" in ink16 (and "IN – · OUT –") only before the first frame ever.
//   A slot: its Mode label as the caption, its value text and unit in the slot's value ink, and a sub of
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
// - The panel's input activity clock (UF1a, ADR-69; here because Panel.h is frozen without a member for it, and this row
//   is shown and ticked on both screens): any change of the pointer, a press, the focus, a touched parameter, the hand
//   or the view keeps wantsFullRate() for layout::live::kActiveS, so hovering, dragging and typing run at full rate
//   even while the audio is stopped; the idle rate returns only when nothing moves.
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

        // ---- UF1a additions (S11; ADR-69, ADR-70): private, no FZ4 declaration above changed ------------------------
        struct Activity                                          // what input changes in the PanelContext
        {
            float    x = 0.0f, y = 0.0f;
            uint32_t moves = 0, downs = 0, focus = 0, handItem = 0;
            double   touchedAt = -1.0;
            HandKind handKind = HandKind::none;
            Screen   screen{};
            Overlay  overlay{};
            ScTab    scTab{};
            bool     in = false, pressed = false, focusVisible = false;
            bool operator==(const Activity&) const = default;
        };
        static Activity activityOf(const PanelContext&) noexcept;
        void tickTelemetry(float dt) noexcept;                   // the GR hold, the IN · OUT sub, the GR bar
        void drawGrBar(funkgui::Canvas&, const funkgui::Theme&) const;

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

        // ---- UF1a additions (S11): private state ---------------------------------------------------------------------
        Activity   activity_{};                                  // the last input state seen ...
        double     activeAt_ = -1.0;                             // ... changed at this panel time (-1: never)
        float      grHold_ = 0.0f;                               // the GAIN REDUCTION readout (dB of GR, >= 0), 4 Hz
        float      inShown_ = -200.0f, outShown_ = -200.0f;      // the IN · OUT sub-readout (dBFS), 4 Hz
        float      inAcc_ = -200.0f, outAcc_ = -200.0f;          // their max over the fresh frames since the refresh
        float      grBar_ = 0.0f;                                // the live GR bar (dB of GR)
        int64_t    refreshSlot_ = -1;                            // floor(panel time / kGrRefreshS) of the last refresh
        bool       seen_ = false;                                // a frame has reached the readout
    };
}
