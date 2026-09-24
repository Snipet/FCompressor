// Source/editor/views/Footer.h — the Footer sub-view (02 §6.3, §6.6). Class declaration frozen at FZ4; U1b (S6) completes
// it: the spec line (in priority: the first-run hint, state notices, the Mode-switch summary,
// the spec line of the item under the hand, the lookahead hint) and the THEME cells. The last Tab stop.
//
// U1b (S6) behaviour, where 02 is silent (U1b handoff). The line at (40, 604), fitted to 740 px, is the first of:
// 1. the first-run hint (HintLine, 6 s; the first pointer move cuts it to 0.4 s; PanelOptions::skipHint: none);
// 2. a notice: AUDIO RESET AFTER A NON-FINITE SAMPLE for 3 s after a fresh frame carries kUiPoisonReset, else the
//    StateNotice texts for 10 s after a load (01 §9.1; K2 #10) — a notice the facade holds when the editor opens is
//    shown then, and a later load (StateNotice::serial moves) shows its own. Several flags are joined by three spaces;
//    a key that is not in the retired list reads IS UNKNOWN instead of IS RETIRED;
// 3. the Mode-switch summary for 3 s after the resolved Mode changes (02 §8.7): "BUS G · 6 STEPPED · 1 DERIVED ·
//    6 EXTENSION · 8 N/A", counted over the 22 Mode-filtered parameters (zero counts omitted; LOCKED counted too);
// 4. the lookahead hint while desc.wantsLookahead && the configured budget is OFF and the pointer is over the band
//    (the middle region on CHARACTERISTICS) or the hand is on LOOKAHEAD (the slot or the budget cells) — keyed on the
//    descriptor, never a Mode name (K1 #35);
// 5. PanelContext::hand's spec line (the hovered, focused or dragged item: slot specs carry their reasons).
// Inks: hint and spec ink32 (02 §8.10), summary and lookahead hint ink52, notices ink70. Tags: HINT, NOTICE (2, 3),
// FOOTER_SPEC (4, 5). The a11y staticText's value is the whole line, before the fit.
//
// The members below the FZ4 declarations are additions (private state and SubView overrides with defaults); no FZ4
// declaration changed.
#pragma once

#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/widgets/HintLine.h>
#include <funkgui/widgets/ThemeCells.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class Footer final : public SubView
    {
    public:
        explicit Footer(PanelContext&);

        Footer(const Footer&) = delete;
        Footer& operator=(const Footer&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U1b additions: the SubView input the THEME cells take ------------------------------------------------
        void pointerDown(const funkgui::PointerEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;

    private:
        enum class LineKind : uint8_t { none, hint, poison, notice, summary, lookahead, spec };

        LineKind line(char* out, std::size_t cap) const;         // the line shown now (unfitted) and its kind
        void rebuildNotice(const StateNotice&);
        void rebuildSummary();
        bool lookaheadHintWanted() const noexcept;

        PanelContext&      ctx_;
        funkgui::HintLine   hint_;
        funkgui::ThemeCells theme_;
        uint32_t movesSeen_ = 0;                                 // PanelContext pointer counters already fed to hint_
        uint32_t downsSeen_ = 0;
        uint32_t noticeSerial_ = 0;                              // the StateNotice::serial shown (or skipped)
        float    noticeLeft_ = 0.0f;                             // seconds
        char     notice_[256]{};
        uint32_t poisonCount_ = 0;                               // the publishCount whose kUiPoisonReset armed poisonLeft_
        float    poisonLeft_ = 0.0f;
        uint32_t modeSerial_ = 0;                                // FrameState::modeSerial seen
        float    summaryLeft_ = 0.0f;
        char     summary_[160]{};
        bool     pointerOver_ = false;
    };
}
