// Source/editor/views/Settings.h — the settings overlay (v1.2, ADR-85): this instance's audio setup in detail, the
// machine's defaults for new instances, and diagnostics for a support question. The lead's addition to the fixed
// composition (Panel.h): ViewIndex::settings, Overlay::settings, the view "settings"; the header's gear opens it.
//
// Layout (layout::settings), over the whole region between the header and the footer (kArea, y 64–590), with the
// browsers' ground, rules and fade:
// - Left, AUDIO · THIS INSTANCE: QUALITY (the `quality` port: ECO · STD · HQ) over a table of what each costs —
//   OVERSAMPLING, FILTER, LATENCY in samples (fcdsp::kOs) and the rate the engine RUNS AT; LOOKAHEAD (the `labudget`
//   port: OFF · 5 MS · 20 MS) over its latency in samples at the current rate (fcdsp::lookaheadSamples); SIDECHAIN (the
//   `extkey` port: INTERNAL · EXTERNAL) over what the host routes to the key input; then the total LATENCY the host is
//   told. In each table the selected column is ink70, the others ink32. The cells are the display row's (CellStyle::text,
//   16 px tall, w(label) + 14 rounded to an even px, 4 px apart), so a cell here writes exactly what the display row's
//   does: one GestureController tap of that port (SetupWatcher applies QUALITY and LOOKAHEAD, never the UI).
// - Left, under a rule, NEW INSTANCES · THIS COMPUTER: QUALITY and LOOKAHEAD for an instance the host creates
//   (funkgui::PrefCells over kPrefNewQuality / kPrefNewLookahead, ProcessorFacade.h), read by the processor when it is
//   constructed. A session or a state load sets its own; presets never carry these two.
// - Left, under a second rule, INTERFACE · THIS COMPUTER (v1.2, ADR-90): the ANIMATION slider, a stepped RuleSlider over
//   AnimationModel (SLOW · NORMAL · FAST · FASTER · OFF, a machine preference; OFF is no animation), and a note. It is
//   driven as a slot is (labels, drag, wheel, keys, double-click to NORMAL, a11y), and is the Tab stop before COPY
//   REPORT.
// - Right, DIAGNOSTICS: fifteen rows, a key (kMicro ink32) and a value (kMicro ink70, fitted to the column) — version,
//   libraries, format and host, sample rate, block size, channels, oversampling, lookahead, latency, DSP load, overruns,
//   audio (running, stopped, none yet), Mode, presets, display. They come from ProcessorFacade::diagnostics(), the
//   frame state and PanelContext::renderInfo, re-read on opening and every layout::settings::kRefreshS of panel time;
//   DSP LOAD, OVERRUNS, AUDIO and DISPLAY are live-tagged. The preset counts are re-read when PresetAccess::revision()
//   moves, so a refresh allocates nothing.
// - Bottom bar, under a rule: "ESC CLOSES" and COPY REPORT, a text cell right-aligned to x 920: report() on the
//   clipboard (juce::SystemClipboard) in a live editor, then COPIED for 2 s; headless (no owner component) it copies
//   nothing and says nothing.
// - Input as the browsers': first in the hit order, a click outside kArea closes it (the Panel), Esc closes it, it is
//   the whole Tab order while open (the five cell groups, then COPY REPORT) and takes the focus onto QUALITY when it was
//   opened from the keyboard. Every group offers its footer spec line on hover and on focus.
// - A11y: the cell groups are radioGroups ("Quality", "Lookahead budget", "Sidechain", "Quality for new instances",
//   "Lookahead for new instances"; a cell's help says what it costs), the key input, the latency and each DIAGNOSTICS
//   row are staticTexts, COPY REPORT is a button. a11yRevision() bumps when a text changes.
#pragma once

#include "editor/SubView.h"
#include "editor/views/AnimationModel.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/widgets/RuleSlider.h>
#include <funkgui/widgets/SegmentedSelector.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fcmp::ui
{
    class Settings final : public SubView
    {
    public:
        explicit Settings(PanelContext&);

        Settings(const Settings&) = delete;
        Settings& operator=(const Settings&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;
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
        void doubleClick(const funkgui::PointerEvent&) override;   // v1.2 (ADR-90): ANIMATION to NORMAL
        bool wheel(const funkgui::WheelEvent&) override;           // v1.2: ANIMATION

        // The DIAGNOSTICS as plain text: a title line, then "KEY: value" per row. COPY REPORT's text.
        std::string report() const;
        // COPY REPORT: report() on the system clipboard; false (and nothing copied) without an owner component.
        bool copyReport();

        // a11y locals: a11yId(ViewIndex::settings, local). A radioGroup's cells are its local + 1 + i.
        static constexpr uint32_t kQualityLocal    = 0x10;
        static constexpr uint32_t kBudgetLocal     = 0x20;
        static constexpr uint32_t kKeyLocal        = 0x30;
        static constexpr uint32_t kNewQualityLocal = 0x40;
        static constexpr uint32_t kNewBudgetLocal  = 0x50;
        static constexpr uint32_t kCopyLocal       = 0x60;       // button
        static constexpr uint32_t kAnimationLocal  = 0x70;       // slider (v1.2, ADR-90)
        static constexpr uint32_t kKeyNoteLocal    = 0x61;       // staticText: the key input
        static constexpr uint32_t kLatencyLocal    = 0x62;       // staticText: the total latency
        static constexpr uint32_t kDiagLocal0      = 0x100;      // staticText per DIAGNOSTICS row, in order
        static constexpr int      kDiagRows        = 15;

    private:
        enum Group : int { gQuality, gBudget, gKey, gNewQuality, gNewBudget, kGroups };

        struct Row
        {
            const char* key = "";                                // drawn, upper case
            const char* spoken = "";                             // the staticText's title
            bool        live = false;                            // live-tagged
            char        value[96] {};
        };

        bool isOpen() const noexcept;
        void refresh();                                          // the diagnostics and every text built from them
        int  groupOf(uint32_t id) const noexcept;                // the group owning an a11y id; -1: none
        funkgui::SegmentedSelector& group(int g) noexcept { return *groups_[static_cast<std::size_t>(g)]; }
        const funkgui::SegmentedSelector& group(int g) const noexcept { return *groups_[static_cast<std::size_t>(g)]; }
        funkgui::Point pointerForWidgets() const noexcept;

        PanelContext& ctx_;

        // Texts outlive the models that point at them: declared first.
        std::array<std::string, 3> qualityHelp_;
        std::array<std::string, 3> budgetHelp_;
        std::array<std::string, 2> keyHelp_;
        std::array<std::string, 3> newQualityHelp_;
        std::array<std::string, 3> newBudgetHelp_;
        funkgui::ParamCells qualityModel_;
        funkgui::ParamCells budgetModel_;
        funkgui::ParamCells keyModel_;
        funkgui::PrefCells  newQualityModel_;
        funkgui::PrefCells  newBudgetModel_;
        funkgui::SegmentedSelector quality_;
        funkgui::SegmentedSelector budget_;
        funkgui::SegmentedSelector key_;
        funkgui::SegmentedSelector newQuality_;
        funkgui::SegmentedSelector newBudget_;
        AnimationModel      animationModel_;                     // ADR-90
        funkgui::RuleSlider animation_;
        bool                animationCaptured_ = false;
        std::array<funkgui::SegmentedSelector*, kGroups> groups_{};
        funkgui::Rect copyRect_{};                               // COPY REPORT (its width from the atlas)
        std::array<float, 3> qualityX_{};                        // the tables' column centres (the cells')
        std::array<float, 3> budgetX_{};
        float budgetRight_ = 0.0f;                               // the rate note starts after LOOKAHEAD's cells

        // What refresh() read and built.
        Diagnostics diag_{};
        RenderInfo  render_{};
        double      refreshedAt_ = -1.0;
        uint32_t    presetsRev_ = 0;
        bool        presetsRead_ = false;
        int         factoryPresets_ = 0, userPresets_ = 0;
        std::array<Row, kDiagRows> rows_{};
        std::array<std::array<char, 16>, 3> runsAt_{};           // QUALITY's RUNS AT column texts (kHz)
        std::array<std::array<char, 16>, 3> budgetSamples_{};    // LOOKAHEAD's latency column texts
        char rateNote_[32] {};                                   // "AT 48 KHZ" beside LOOKAHEAD's table
        char keyNote_[96] {};
        char latency_[64] {};

        // Input and opening.
        bool     pointerOver_ = false;
        bool     copyArmed_ = false;
        float    copyHover_ = 0.0f;
        double   copiedUntil_ = -1.0;                            // panel time; COPIED until then
        double   lastTick_ = -1.0;
        uint32_t revision_ = 1;
    };
}
