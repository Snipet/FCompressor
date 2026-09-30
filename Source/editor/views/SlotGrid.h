// Source/editor/views/SlotGrid.h — the SlotGrid sub-view (02 §6.1, §6.4, §8.1–§8.4, §8.7). Class declaration frozen at FZ4; U1s (S6) completes
// it: the 21 slots in 3 × 7 at layout::kSlots, each a RuleSlider over
// PanelContext::slot(pid), with the AUTO, EXT and LISTEN words (WordModel), the Mode-switch landing (carets
// ease, changed labels flash, 02 §8.7) and the spec line of the hovered, focused or dragged slot
// (PanelContext::offerHand). Every slot, word included, is a Tab stop (02 §8.9).
//
// U1s (S6) completion — additions only (FZ4: the constructor and the overrides above are unchanged): the SubView input,
// cursor, a11y-action and full-rate overrides the grid needs, and its private state.
// - Slots: one funkgui::RuleSlider per layout::kSlots entry at layout::slotGeom, a11y id (slotGrid << 16) | (1 + i).
//   The sliders render the SlotModel views and write only through PanelContext::gestures (02 §5.4, §8.4): stepped
//   writes are the canonical detents, locked / derived / n/a slots refuse every write path.
// - Words: AUTO (automu) on MAKEUP, EXT (extkey) on DETECT, LISTEN (listen) on SC HPF, each a funkgui::AttachedWord over
//   a WordModel (SlotGrid.cpp), a11y id (slotGrid << 16) | (64 + i) of its slot i. AUTO is hidden while automu is n/a
//   and disabled with its reason while locked (K1 #23). EXT stays writable (a global, 02 §6.4) but is drawn disabled,
//   with the reason NO SIDECHAIN BUS CONNECTED on the footer and as a11y help, while fresh telemetry shows the key on
//   and no active key bus (kUiExtKeyActive clear): the only "no bus" signal the facade carries.
// - Live marks (tags DET_TICK, RANGE_BAR; live): THRESHOLD's 1×4 ink52 detector tick under its track at the
//   operating point (the TRANSFER dot's 10 ms peak envelope, views/Telemetry.h; UF1a), RANGE's 1 px signal bar from 0
//   to the applied GR.
// - Landing (02 §8.7): when FrameState::modeSerial moves, every slider's caret eases (τ 90 ms) and a slider whose
//   state, label or tag changed flashes its label 0.6 s. The landing writes nothing (K2 #4).
// - Hand (02 §6.6): a pressed slot or word offers `drag`, the one under the pointer `hover`, the focused one `focus`,
//   with its footer spec line: RuleSlider's for writable slots; for locked / derived / n/a ones "<LABEL> (<AKA>)
//   <TAG>   <REASON>   STORED <raw> (USED BY OTHER MODES)" (no STORED for list and switch parameters, whose raw
//   index means nothing outside its Mode). Words offer their own line and hand pid automu for AUTO, kNoPid for the
//   globals EXT and LISTEN (PanelContext::slot takes Mode-filtered Pids only).
// - A11y: RuleSlider's items (02 §8.9: stepped in index space), with the continuous / locked / derived value
//   interface rewritten to the Mode track in display units (lo, hi, v; step = 1 % of the span) and setValue translated
//   back to the track; each word follows its slot.
// - Typed values (v1.2, ADR-89; views/ValueEntry.h): Return, a digit, '.', '-' or '+' on a focused slot (Tab's ring, or
//   the silent focus a press on a slot gives it) opens a field over its value, holding the value text (Return) or the
//   typed character. Return, Tab or a click elsewhere sets it: the text goes through fcdsp::parseHost with the frame's
//   raw values, as a host's typed text does (the Mode's own numbers, step labels and units, universal units), and the
//   result is one tap on the port; a text that is no value keeps the field open and flashes. Esc cancels. Locked,
//   derived and n/a slots open nothing. A Mode change closes the field without writing.
#pragma once

#include "editor/Layout.h"
#include "editor/SubView.h"
#include "editor/views/ValueEntry.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/widgets/AttachedWord.h>
#include <funkgui/widgets/RuleSlider.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class SlotGrid final : public SubView
    {
    public:
        explicit SlotGrid(PanelContext&);

        SlotGrid(const SlotGrid&) = delete;
        SlotGrid& operator=(const SlotGrid&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U1s additions (S6) ------------------------------------------------------------------------------------
        ~SlotGrid() override;

        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        void doubleClick(const funkgui::PointerEvent&) override;
        bool wheel(const funkgui::WheelEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;
        uint32_t a11yRevision() const override;
        void endTextEntry(bool commit) override;                 // v1.2 (ADR-89)
        bool takesTypedKeys(uint32_t id) const override;

        static constexpr uint32_t kWordIdBase = 64;              // a word's local a11y id: 64 + its slot's index
        static constexpr int kWordCount = 3;                     // AUTO, EXT, LISTEN

    private:
        class Word;                                              // funkgui::WordModel over automu / extkey / listen

        int  sliderAt(funkgui::Point) const noexcept;            // index into sliders_, -1: none (words carved out)
        int  wordAt(funkgui::Point) const noexcept;              // index into words_ (visible ones), -1: none
        int  sliderOf(uint32_t id) const noexcept;               // a11y id -> slider index, -1
        int  wordOf(uint32_t id) const noexcept;                 // a11y id -> word index, -1
        bool focused(uint32_t id) const noexcept;                // the focus ring is on this item
        bool writable(int slider) const noexcept;                // continuous or stepped: writes are allowed
        void land();                                             // a Mode change: carets ease, changed labels flash
        void offerHands();
        void specLine(int slider, char* out, std::size_t n) const;
        void wordSpecLine(int word, char* out, std::size_t n) const;
        void openEntry(int slider, const funkgui::KeyEvent& opener);   // ADR-89
        bool commitEntry();                                      // false: the text is no value (the field stays)
        void closeEntry() noexcept;

        PanelContext& ctx_;

        std::array<std::optional<funkgui::RuleSlider>, layout::kSlotCount> sliders_;
        std::array<std::unique_ptr<Word>, kWordCount>                   wordModels_;
        std::array<std::optional<funkgui::AttachedWord>, kWordCount>    words_;
        std::array<int, kWordCount>                                     wordSlot_ {};   // the slider each word sits on
        std::array<bool, kWordCount>                                    wordShown_ {};  // visibility at the last tick

        int      hovered_ = -1;                                  // slider under the pointer
        int      hoveredWord_ = -1;                              // word under the pointer
        int      captured_ = -1;                                 // slider holding the pointer between down and up
        int      capturedWord_ = -1;                             // word holding the pointer between down and up
        funkgui::Point downAt_{};                                // where the captured slider was pressed
        uint32_t modeSerial_ = 0;                                // FrameState::modeSerial the carets have landed on
        uint32_t revision_ = 0;                                  // bumps when a word appears or disappears
        ValueEntry entry_;                                       // ADR-89: the typed-value field ...
        int        entrySlider_ = -1;                            // ... over this slider
    };
}
