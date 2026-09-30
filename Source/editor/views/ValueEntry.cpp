// Source/editor/views/ValueEntry.cpp — the typed-value field (see ValueEntry.h).
#include "editor/views/ValueEntry.h"

#include "editor/Tags.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>

#include <algorithm>

namespace fcmp::ui
{
    namespace
    {
        constexpr float kPadX = 4.0f;                            // the text starts where the value text did
        constexpr float kCaretPad = 3.0f;                        // the caret reaches this far past the cap height
    }

    bool ValueEntry::opens(const funkgui::KeyEvent& e) noexcept
    {
        if (e.key == funkgui::Key::enter)
            return true;
        if (e.key != funkgui::Key::character || e.mods.cmd || e.mods.ctrl)
            return false;
        const char32_t c = e.ch;
        return (c >= U'0' && c <= U'9') || c == U'.' || c == U'-' || c == U'+';
    }

    std::string ValueEntry::ascii(std::string_view s)
    {
        std::string out;
        for (std::size_t i = 0; i < s.size();)
        {
            const auto b = static_cast<unsigned char>(s[i]);
            if (b < 0x80u)
            {
                out += static_cast<char>(b);
                ++i;
                continue;
            }
            const std::string_view rest = s.substr(i);
            if (rest.starts_with("\xE2\x88\x92"))                // U+2212 minus
                out += '-';
            else if (rest.starts_with("\xC2\xB5") || rest.starts_with("\xCE\xBC"))   // µ, μ
                out += 'u';
            else if (rest.starts_with("\xE2\x88\x9E"))           // ∞
                out += "inf";
            std::size_t n = 1;                                   // skip the whole sequence
            while (i + n < s.size() && (static_cast<unsigned char>(s[i + n]) & 0xC0u) == 0x80u)
                ++n;
            i += n;
        }
        return out;
    }

    void ValueEntry::begin(const funkgui::Rect& box, const funkgui::TextStyle& style, const funkgui::KeyEvent& opener,
                           std::string_view current)
    {
        box_ = box;
        style_ = style;
        flash_ = 0.0f;
        open_ = true;
        if (opener.key == funkgui::Key::enter)
            edit_.set(ascii(current), kMaxLength);               // the current value, selected
        else
        {
            edit_.set("", kMaxLength);
            edit_.key(opener, kMaxLength);                       // the typed character starts the text
        }
    }

    void ValueEntry::end() noexcept
    {
        open_ = false;
        flash_ = 0.0f;
    }

    ValueEntry::Result ValueEntry::key(const funkgui::KeyEvent& e)
    {
        switch (e.key)
        {
            case funkgui::Key::enter:
            case funkgui::Key::tab:    return Result::commit;
            case funkgui::Key::escape: return Result::cancel;
            case funkgui::Key::character: case funkgui::Key::up: case funkgui::Key::down: case funkgui::Key::left:
            case funkgui::Key::right: case funkgui::Key::pageUp: case funkgui::Key::pageDown: case funkgui::Key::home:
            case funkgui::Key::end: case funkgui::Key::backspace: case funkgui::Key::del: case funkgui::Key::space:
                break;
        }
        edit_.key(e, kMaxLength);                                // anything it does not take is swallowed
        return Result::typing;
    }

    void ValueEntry::refuse() noexcept
    {
        const std::string t = edit_.buffer;
        edit_.set(t, kMaxLength);                                // selected again: the next key replaces it
        flash_ = 1.0f;
    }

    void ValueEntry::tick(float dt) noexcept
    {
        flash_ = std::max(0.0f, flash_ - std::max(dt, 0.0f) / kFlashS);
    }

    void ValueEntry::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        if (!open_)
            return;
        const funkgui::Canvas::Scope scope(c, tag::valueEntry, false);
        c.rrect(box_.x, box_.y, box_.w, box_.h, 2.0f, th.ground, 1.0f, funkgui::mix(th.accent, th.ink100, flash_));
        const float x = box_.x + kPadX;
        const float top = c.capCentreTop(box_.centreY(), style_);
        const char* text = edit_.buffer.c_str();
        const float w = c.textWidth(text, style_);
        if (edit_.allSelected && w > 0.0f)
            c.rrect(x - 1.0f, top - kCaretPad, w + 2.0f, style_.px * 0.7f + 2.0f * kCaretPad, 0.0f, th.accentDim);
        c.text(text, x, top, style_, th.ink100);
        const std::string head = edit_.buffer.substr(0, static_cast<std::size_t>(std::clamp(
            edit_.caret, 0, static_cast<int>(edit_.buffer.size()))));
        const float cx = x + c.textWidth(head.c_str(), style_);
        c.rrect(cx, top - kCaretPad, 1.0f, style_.px * 0.7f + 2.0f * kCaretPad, 0.0f, th.accent);
    }
}
