// Source/editor/views/PresetStrip.h — the PresetStrip sub-view (02 §6.3). Class declaration frozen at FZ4; U6 (S12) completes
// it: the header strip at layout::header::kPresetStrip over PresetAccess: ‹ name ›, the modified
// marker, save; the name opens the preset browser (Panel::setView). Tab stops 2 of 02 §8.9.
//
// U6 (S12) behaviour, where 02 is silent (U6 handoff). The strip is the Mode latch's twin (Header.h), in HR's strip
// geometry (PresetPanel.h) moved 24 px right and 2 px up, inside {228, 12, 372, 44}:
// - ‹ {228,14,24,32}, the name cell {252,14,276,32} and › {528,14,24,32}: the name in kLatch ink100 at x 260 on the Mode
//   name's cap centre (y 30), fitted with an ellipsis before the modified marker; a hairline rule under it at y 44 (ink16,
//   ink32 under the pointer, ink52 while the preset browser is open); the sub-line at (260, 48) in kMicro: the category
//   and FACTORY or USER in ink32, then MODIFIED in ink52. The modified marker (PresetAccess::modified(): the values
//   moved, or the Mode is not the preset's) is a 2.5 px ink100 disc at the name cell's right end (x 520), 02 §6.2's "☆"
//   place. No current preset: UNTITLED in ink52 (the marker still shows modified()); no presets at all: NO PRESETS in
//   ink32 and the chevrons in ink16, disabled.
// - SAVE: an outlined button {556,21,44,18} (hit {552,14,48,32}): 1 px ink32 border, "SAVE" kCaption ink52; hover ink70 /
//   ink100, accent while pressed. The box keeps it from reading "SAVE MODE" with the Mode latch's caption beside it.
// - ‹ › call PresetAccess::step(∓1) once per click (on the press, as the Mode latch's chevrons) and wrap (P3's step()).
//   The name arms on the press and opens the preset browser on a release inside it (dragging off cancels); a popup
//   click on the name opens it too (there is no host menu for presets). SAVE fires on a release inside it: it opens the
//   browser and presses the browser's SAVE AS (Panel::a11yAction, the path VoiceOver uses), so the name is typed in
//   the browser, over the list it joins; a save started from here closes the browser again (PresetBrowser.h).
// - The strip reads PresetAccess only when revision() moves (count, current, modified and the current row), once per
//   tick and before acting on input, so draw() and accessibility() read its own copy (02 §3.7) and nothing allocates
//   per frame.
// - Keys on a focused stop: ‹ or ›: Return / Space step; the name: ↑ → next, ↓ ← previous, Return / Space open the
//   browser; SAVE: Return / Space. A11y: ‹ and › buttons "Previous preset" / "Next preset", the name a comboBox
//   "Preset" (value "Mix Bus Glue, modified"; description the sub-line; press opens the browser, increment / decrement
//   step), SAVE a button "Save preset". Tab stops ‹, name, ›, SAVE (02 §8.9 item 2), always all four.
// - The footer line: each part's spec under the hand (hover) or on focus.
//
// The members below the FZ4 declarations are additions (private state and SubView overrides with defaults); no FZ4
// declaration changed.
#pragma once

#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class PresetStrip final : public SubView
    {
    public:
        explicit PresetStrip(PanelContext&);

        PresetStrip(const PresetStrip&) = delete;
        PresetStrip& operator=(const PresetStrip&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U6 additions: the SubView input the strip takes, and its a11y locals -----------------------------------
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

        static constexpr uint32_t kPrevLocal = 1;               // a11yId(ViewIndex::presetStrip, local)
        static constexpr uint32_t kNameLocal = 2;
        static constexpr uint32_t kNextLocal = 3;
        static constexpr uint32_t kSaveLocal = 4;

    private:
        enum class Part : uint8_t { none, prev, name, next, save };

        Part partAt(funkgui::Point) const noexcept;
        Part partOf(uint32_t id) const noexcept;                 // the part an a11y id names; none: not the strip's
        void refresh();                                          // re-read PresetAccess when revision() moved
        void step(int delta);
        void openBrowser();
        void save();
        void activate(Part);                                     // Return, Space, a11y press
        const char* specOf(Part) const noexcept;

        PanelContext& ctx_;

        // PresetAccess as of the last revision() read (texts are UTF-8, printable, upper case as drawn).
        bool     valid_ = false;
        uint32_t seenRev_ = 0;
        uint32_t a11yRev_ = 0;                                   // bumps when the texts or the enabled state change
        int      count_ = 0;
        int      current_ = -1;
        bool     modified_ = false;
        bool     factory_ = false;
        char     name_[96]{};                                    // "MIX BUS GLUE", "UNTITLED", "NO PRESETS"
        char     rawName_[96]{};                                 // the row's own spelling (a11y), printable
        char     category_[64]{};                                // "BUS" (upper case, printable)
        char     rawCategory_[64]{};

        Part  hover_ = Part::none;
        Part  pressed_ = Part::none;
        bool  armed_ = false;                                    // the pressed name / SAVE fires on a release inside
        bool  pointerOver_ = false;
        std::array<float, 4> hoverAmt_{};                        // prev, name, next, save (90 ms in, 160 ms out)
    };
}
