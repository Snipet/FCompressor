// Source/editor/views/EditControls.h — UNDO, REDO and the A/B compare on the preset strip's second line (v1.2, ADR-91;
// ProcessorFacade::edits(), EditHistory.h). Owned and routed by PresetStrip, drawn under SAVE, right of the sub-line.
//
// - UNDO and REDO: arrows drawn as polylines (a hook to the left, and its mirror), ink52 (ink100 and accent under the
//   hand), ink16 when there is nothing to take back; a release inside fires. Their footer line names the step
//   ("UNDO THRESHOLD   CMD-Z"; CTRL-Z off macOS, ADR-92), their a11y buttons are disabled when there is nothing.
// - A | B: two letters, the active one ink100 on a 1 px rule, the other ink32 (ink70 under the hand); a click on the
//   other one switches (EditAccess::selectSlot, an undo step). A popup click, or a11y showMenu, asks the host for a
//   menu with "Copy A to B" (or B to A) (HostServices::showMenu, anchored on the two letters in the Panel's own px, in
//   the product's Theme; web Sprint C, ADR-93). A11y: a radioGroup "Compare" of two radioButtons.
// - Keys (focused, 02 §8.9): Return / Space on UNDO or REDO; on the group, ← → Home End choose a slot and Return / Space
//   switch to the other. Cmd-Z and Shift-Cmd-Z anywhere on the panel are the Panel's (Panel::key).
#pragma once

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace funkgui
{
    class Canvas;
    struct Theme;
}

namespace fcmp
{
    class EditAccess;
}

namespace fcmp::ui
{
    struct PanelContext;

    class EditControls
    {
    public:
        enum class Part : uint8_t { none, undo, redo, a, b };

        static constexpr uint32_t kUndoLocal = 5;               // a11yId(ViewIndex::presetStrip, local)
        static constexpr uint32_t kRedoLocal = 6;
        static constexpr uint32_t kGroupLocal = 7;              // the radioGroup; A and B are 8 and 9

        // The edit history, once an open wheel burst has become its entry: a burst is one gesture, open until 0.5 s
        // after its last notch, and the history refuses to undo, redo or switch while a gesture is open. A drag stays
        // open (and the history refuses).
        static EditAccess& edits(PanelContext&);

        // The platform's command key (ADR-92), as the host says it is (HostServices::commandKeyIsMeta(); web Sprint C,
        // ADR-93: a compile-time platform test would be wrong in a browser): Cmd where it is Meta (macOS); elsewhere
        // the command modifier is Ctrl, so a key event carries mods.cmd and mods.ctrl together. commandOnly: that key
        // held with no other modifier but Shift.
        static bool commandOnly(const funkgui::Mods&, bool commandKeyIsMeta) noexcept;
        static const char* commandKeyName(bool commandKeyIsMeta) noexcept;   // "CMD" or "CTRL", as the footer writes it

        explicit EditControls(PanelContext&);
        ~EditControls();

        EditControls(const EditControls&) = delete;
        EditControls& operator=(const EditControls&) = delete;

        bool contains(funkgui::Point) const noexcept;
        Part partAt(funkgui::Point) const noexcept;
        bool owns(uint32_t id) const noexcept;
        bool settled() const noexcept;

        void tick(float dt, funkgui::Point pointer, bool pointerOver);
        void draw(funkgui::Canvas&, const funkgui::Theme&) const;

        void pointerDown(const funkgui::PointerEvent&);
        void pointerDrag(const funkgui::PointerEvent&);
        void pointerUp(const funkgui::PointerEvent&);
        bool key(const funkgui::KeyEvent&);                      // the focused part's keys
        funkgui::Cursor cursor(funkgui::Point) const;
        void accessibility(std::vector<funkgui::A11yItem>&) const;
        int  focusOrder(std::span<uint32_t> out) const;          // UNDO, REDO, the group
        void a11yAction(uint32_t id, funkgui::A11yAction);
        const char* specOf(Part) const noexcept;                 // the footer line of a part
        uint32_t revision() const noexcept { return revision_; } // bumps when an item's state or text changes

    private:
        void fire(Part);
        void showMenu();                                         // Copy A to B (HostServices::showMenu)
        Part focusedPart() const noexcept;

        PanelContext& ctx_;
        Part   hover_ = Part::none;
        Part   pressed_ = Part::none;
        bool   armed_ = false;
        std::array<float, 4> hoverAmt_{};                        // undo, redo, a, b
        // What EditAccess said at the last tick (draw and a11y read these).
        bool     canUndo_ = false, canRedo_ = false, usedB_ = false;
        int      slot_ = 0;
        uint32_t revision_ = 0;
        char     undoSpec_[96]{};
        char     redoSpec_[96]{};
        std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // a menu callback checks it
    };
}
