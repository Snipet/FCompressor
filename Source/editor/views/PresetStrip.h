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
//   click on the name opens it too (there is no host menu for presets). SAVE fires on a release inside it (below).
// - SAVE (P3c, S12.5; S12 lead revision 11): with a user preset current it saves over it, one click and no dialog
//   (PresetAccess::overwrite(current)): the modified marker and MODIFIED go, and the sub-line reads SAVED (ink52, where
//   MODIFIED was) for 2 s of panel time. With a factory preset current, or none, it saves as, as before: it opens the
//   browser and presses the browser's SAVE AS (Panel::a11yAction, the path VoiceOver uses), so the name is typed in the
//   browser, over the list it joins; a save started from here closes the browser again (PresetBrowser.h). An overwrite
//   the processor refuses (the preset went, the store failed) falls back to that save as, so the sound is never lost.
//   Save as stays one step away for a user preset: SAVE's context menu (a popup click, or a11y showMenu) is the
//   host's (HostServices::showMenu; web Sprint C, ADR-93), anchored on the box in the Panel's own px, in the product's
//   Theme: "Save" (what the click does) and "Save As..." (always the browser's save as); Shift-Return on the focused
//   SAVE is the save as too. The footer line and the a11y help say which save the click is
//   ("SAVE OVER 'MY BUS'   RIGHT-CLICK OR SHIFT-RETURN: SAVE AS"; "Saves over My Bus. Its menu has Save as").
// - The strip reads PresetAccess only when revision() moves (count, current, modified and the current row), once per
//   tick and before acting on input, so draw() and accessibility() read its own copy (02 §3.7) and nothing allocates
//   per frame.
// - Keys on a focused stop: ‹ or ›: Return / Space step; the name: ↑ → next, ↓ ← previous, Return / Space open the
//   browser; SAVE: Return / Space (Shift: save as). A11y: ‹ and › buttons "Previous preset" / "Next preset", the
//   name a comboBox "Preset" (value "Mix Bus Glue, modified"; description the sub-line; press opens the browser,
//   increment / decrement step), SAVE a button "Save preset" (help: over the current user preset, or as a new preset;
//   showMenu: its menu).
//   Tab stops ‹, name, ›, SAVE (02 §8.9 item 2), always all four.
// - The footer line: each part's spec under the hand (hover) or on focus.
//
// The members below the FZ4 declarations are additions (private state and SubView overrides with defaults; P3c: the
// destructor, SAVE's menu API and its state); no FZ4 declaration changed.
#pragma once

#include "editor/views/EditControls.h"

#include "editor/SubView.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <array>
#include <cstdint>
#include <memory>
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

        // ---- P3c additions (S12.5): SAVE over a user preset, and SAVE's context menu --------------------------------
        ~PresetStrip() override;                                 // a menu callback still held does nothing

        // SAVE's context menu: its items (returns how many) and choosing one. save: what a click on SAVE does (over the
        // current user preset, else save as); saveAs: the browser's save as, whatever is current.
        enum class Command : uint8_t { save = 1, saveAs };
        struct MenuItem
        {
            Command     command = Command::save;
            const char* label = "";
            bool        enabled = true;
        };
        int  menu(std::span<MenuItem> out) const;
        void run(Command);

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

        // ---- P3c additions ----------------------------------------------------------------------------------------
        bool savesOver() const noexcept;                         // SAVE is an overwrite: a user preset is current
        void saveAs();                                           // the browser's save as (SAVE before P3c)
        void showMenu();                                         // SAVE's context menu (HostServices::showMenu)
        void rebuildSaveSpec();                                  // saveSpec_ for the current preset

        double savedUntil_ = -1.0;                               // SAVED shows until then (panel time)
        char   saveSpec_[160]{};                                 // SAVE's footer line
        std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // a menu callback checks it: this view still exists

        // ---- v1.2 (ADR-91): UNDO, REDO and A | B on the second line, routed first ---------------------------------------
        EditControls edits_;
        bool         editsPressed_ = false;                      // the press landed on them
    };
}
