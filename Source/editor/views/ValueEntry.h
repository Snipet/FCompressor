// Source/editor/views/ValueEntry.h — a typed-value field (v1.2, ADR-89): funkgui::text::LineEdit's state, drawn over a
// value control's value text, owned by the view that opens it (SlotGrid for the slots, OutputTrim for OUTPUT).
//
// - While open the Panel routes every key to the owning view (PanelContext::textEntry), which hands it to key():
//   Return and Tab set the value (Result::commit), Esc cancels, everything else is LineEdit's (printable ASCII, at most
//   kMaxLength characters; Backspace, Delete, ← → Home End, Alt / Cmd deletes). A pointer down outside the box sets the
//   value too (SubView::endTextEntry); inside it, nothing happens.
// - The owner parses the text (fcdsp::parseHost for a slot, parseOutput for OUTPUT): a text that is no value keeps the
//   field open with the text selected, and the border flashes (refuse()), so a typo is never written.
// - Opened by Return the field holds the current value text, selected (typing replaces it); opened by a typed character
//   it starts with that character. The text is ASCII: U+2212 is written '-', µ 'u' and ∞ "inf", which the parsers take.
// - Look: the box (the value text's line, 4 px wider each side) filled with the ground, a 1 px accent border (ink100
//   while refused, easing back over kFlashS), the text in the value's own style, ink100, on an accentDim selection
//   while all is selected, and a 1 px accent caret (steady, as the preset browser's).
#pragma once

#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/text/LineEdit.h>
#include <funkgui/text/TextStyle.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace funkgui
{
    class Canvas;
    struct Theme;
}

namespace fcmp::ui
{
    class ValueEntry
    {
    public:
        enum class Result : uint8_t { typing, commit, cancel };

        static constexpr int   kMaxLength = 16;
        static constexpr float kFlashS = 0.35f;

        // Return, or a character that starts a number: 0-9 . - +
        static bool opens(const funkgui::KeyEvent&) noexcept;
        // The value text as the field holds it: ASCII (U+2212 -> '-', µ -> 'u', ∞ -> "inf"), other non-ASCII dropped.
        static std::string ascii(std::string_view utf8);

        bool open() const noexcept { return open_; }
        // Opens over `box` in `style`: Return pre-fills `current`, selected; a typed character starts the text with it.
        void begin(const funkgui::Rect& box, const funkgui::TextStyle& style, const funkgui::KeyEvent& opener,
                   std::string_view current);
        void end() noexcept;
        Result key(const funkgui::KeyEvent&);
        void refuse() noexcept;                                  // not a value: select the text again, flash
        void tick(float dt) noexcept;
        bool settled() const noexcept { return flash_ <= 0.0f; }
        void draw(funkgui::Canvas&, const funkgui::Theme&) const;

        const std::string&   text() const noexcept { return edit_.buffer; }
        const funkgui::Rect& box() const noexcept { return box_; }

    private:
        funkgui::text::LineEdit edit_;
        funkgui::Rect           box_{};
        funkgui::TextStyle      style_{};
        float                   flash_ = 0.0f;
        bool                    open_ = false;
    };
}
