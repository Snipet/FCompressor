// Source/editor/views/PresetStrip.cpp — the preset strip (see PresetStrip.h): ‹ name ›, the modified marker, SAVE.
#include "editor/views/PresetStrip.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"
#include "editor/views/PresetBrowser.h"

#include "fcdsp/params/Pid.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/text/LineEdit.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/FocusRing.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        using funkgui::Rect;

        // Geometry (PresetStrip.h): the Mode latch's metrics inside the strip's rectangle.
        constexpr Rect  kPrev { 228.0f, 14.0f, 24.0f, 32.0f };
        constexpr Rect  kName { 252.0f, 14.0f, 276.0f, 32.0f };
        constexpr Rect  kNext { 528.0f, 14.0f, 24.0f, 32.0f };
        constexpr Rect  kSave { 552.0f, 14.0f, 48.0f, 32.0f };  // hit
        constexpr Rect  kSaveBox { 556.0f, 21.0f, 44.0f, 18.0f };
        constexpr float kNameTextX = 260.0f;
        constexpr float kCapCentreY = 30.0f;                    // the Mode name's (layout::header::kModeNameCentreY)
        constexpr float kRuleY = 44.0f;
        constexpr float kSubTop = 48.0f;                        // the Mode group line's
        constexpr float kMarkerX = 520.0f;
        constexpr float kMarkerR = 2.5f;
        constexpr float kNameMaxW = kMarkerX - 8.0f - kNameTextX;   // 252
        constexpr float kSubMaxW = kSave.x - 4.0f - kNameTextX;     // 288
        constexpr float kChevronInset = 1.5f;                   // the Mode latch's chevrons (Header.cpp)
        constexpr float kChevronDepth = 3.5f;
        constexpr float kChevronHalf = 5.0f;
        constexpr float kChevronStroke = 1.5f;
        constexpr const char* kSep = " \xC2\xB7 ";              // " · "

        static_assert(kPrev.x == layout::header::kPresetStrip.x && kSave.right() == layout::header::kPresetStrip.right(),
                      "the strip's parts span layout::header::kPresetStrip");

        std::size_t partIndex(int part) noexcept { return static_cast<std::size_t>(part - 1); }

        // A UTF-8 copy into a fixed buffer, never cutting a codepoint.
        void copyText(std::string_view s, char* out, std::size_t cap) noexcept
        {
            if (cap == 0)
                return;
            std::size_t n = std::min(s.size(), cap - 1);
            while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u)
                --n;
            std::memcpy(out, s.data(), n);
            out[n] = '\0';
        }

        std::string upper(std::string_view s)
        {
            std::string u(s);
            for (char& ch : u)
                if (ch >= 'a' && ch <= 'z')
                    ch = static_cast<char>(ch - 'a' + 'A');
            return u;
        }

        std::string_view trimmed(std::string_view s) noexcept
        {
            while (!s.empty() && s.front() == ' ')
                s.remove_prefix(1);
            while (!s.empty() && s.back() == ' ')
                s.remove_suffix(1);
            return s;
        }

        void chevron(funkgui::Canvas& c, const Rect& r, int dir, funkgui::Col col)
        {
            const float cx = r.centreX() + (dir < 0 ? kChevronInset : -kChevronInset);
            const float tip = cx + (dir < 0 ? -kChevronDepth : kChevronDepth);
            c.segment(cx, kCapCentreY - kChevronHalf, tip, kCapCentreY, kChevronStroke, col);
            c.segment(tip, kCapCentreY, cx, kCapCentreY + kChevronHalf, kChevronStroke, col);
        }
    }

    PresetStrip::PresetStrip(PanelContext& ctx) : ctx_(ctx) { refresh(); }

    // ---- state ----------------------------------------------------------------------------------------------------------

    void PresetStrip::refresh()
    {
        PresetAccess& pa = ctx_.facade.presets();
        const uint32_t rev = pa.revision();
        if (valid_ && rev == seenRev_)
            return;
        seenRev_ = rev;
        valid_ = true;

        const int count = pa.count();
        const int current = pa.current();
        const bool modified = pa.modified();
        PresetAccess::Row row;
        if (current >= 0 && current < count)
            row = pa.row(current);
        const std::string name = funkgui::text::printable(trimmed(row.name));
        const std::string category = funkgui::text::printable(trimmed(row.category));

        count_ = count;
        current_ = current >= 0 && current < count ? current : -1;
        modified_ = modified;
        factory_ = current_ >= 0 && row.factory;
        copyText(name, rawName_, sizeof rawName_);
        copyText(category, rawCategory_, sizeof rawCategory_);
        if (current_ >= 0 && !name.empty())
            copyText(upper(name), name_, sizeof name_);
        else
            copyText(count_ == 0 ? "NO PRESETS" : "UNTITLED", name_, sizeof name_);
        copyText(upper(category), category_, sizeof category_);
        ++a11yRev_;
    }

    void PresetStrip::tick(float dt)
    {
        refresh();
        hover_ = pointerOver_ ? partAt(ctx_.pointer) : Part::none;
        for (int p = 1; p <= 4; ++p)
            hoverAmt_[partIndex(p)] = funkgui::ease::hover(hoverAmt_[partIndex(p)], static_cast<int>(hover_) == p, dt);

        if (hover_ != Part::none)
        {
            const uint32_t local = static_cast<uint32_t>(hover_);
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, a11yId(ViewIndex::presetStrip, local), specOf(hover_));
        }
        if (ctx_.focusVisible)
            if (const Part f = partOf(ctx_.focus); f != Part::none)
                ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, ctx_.focus, specOf(f));
    }

    bool PresetStrip::wantsFullRate() const
    {
        for (int p = 1; p <= 4; ++p)
            if (!funkgui::ease::sameBits(hoverAmt_[partIndex(p)], static_cast<int>(hover_) == p ? 1.0f : 0.0f))
                return true;
        return false;
    }

    const char* PresetStrip::specOf(Part p) const noexcept
    {
        switch (p)
        {
            case Part::prev: return "PREVIOUS PRESET";
            case Part::next: return "NEXT PRESET";
            case Part::name: return count_ > 0 ? "PRESET   CLICK: ALL PRESETS   ARROWS: PREVIOUS / NEXT"
                                               : "PRESET   CLICK: THE PRESET BROWSER";
            case Part::save: return "SAVE THE CURRENT SOUND AS A NEW PRESET";
            case Part::none: break;
        }
        return "";
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void PresetStrip::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Canvas::Scope scope(c, tag::presetStrip, false);
        const bool any = count_ > 0;
        const auto hoverInk = [&](Part p, funkgui::Col rest, funkgui::Col over) {
            return pressed_ == p && (armed_ || p == Part::prev || p == Part::next)
                       ? th.accent
                       : funkgui::mix(rest, over, hoverAmt_[partIndex(static_cast<int>(p))]);
        };

        chevron(c, kPrev, -1, any ? hoverInk(Part::prev, th.ink52, th.ink100) : th.ink16);
        chevron(c, kNext, +1, any ? hoverInk(Part::next, th.ink52, th.ink100) : th.ink16);

        // The name, fitted before the marker; the rule under it; the marker.
        char fitted[128];
        funkgui::text::fitEllipsis(ctx_.atlas, name_, T::kLatch, kNameMaxW, fitted, sizeof fitted);
        const funkgui::Col nameInk = pressed_ == Part::name && armed_ ? th.accent
                                   : current_ >= 0                  ? th.ink100
                                   : any                            ? th.ink52
                                                                    : th.ink32;
        c.text(fitted, kNameTextX, c.capCentreTop(kCapCentreY, T::kLatch), T::kLatch, nameInk);
        const bool open = ctx_.overlay == Overlay::presetBrowser;
        c.hairlineH(kName.x, kRuleY, kName.w, open ? th.ink52 : funkgui::mix(th.ink16, th.ink32, hoverAmt_[1]));
        if (modified_)
            c.disc(kMarkerX, kCapCentreY, kMarkerR, th.ink100);

        // The sub-line: category · FACTORY | USER, then · MODIFIED.
        char sub[160];
        std::size_t n = 0;
        const auto add = [&](const char* s) {
            for (; *s != '\0' && n + 1 < sizeof sub; ++s)
                sub[n++] = *s;
            sub[n] = '\0';
        };
        sub[0] = '\0';
        if (current_ >= 0)
        {
            if (category_[0] != '\0')
            {
                add(category_);
                add(kSep);
            }
            add(factory_ ? "FACTORY" : "USER");
        }
        else if (any)
        {
            add("NOT SAVED");
        }
        char subFit[192];
        funkgui::text::fitEllipsis(ctx_.atlas, sub, T::kMicro, kSubMaxW, subFit, sizeof subFit);
        c.text(subFit, kNameTextX, kSubTop, T::kMicro, th.ink32);
        if (modified_)
        {
            float x = kNameTextX;
            if (subFit[0] != '\0')
            {
                x += c.textWidth(subFit, T::kMicro);
                c.text(kSep, x, kSubTop, T::kMicro, th.ink32);
                x += c.textWidth(kSep, T::kMicro);
            }
            if (x + c.textWidth("MODIFIED", T::kMicro) <= kNameTextX + kSubMaxW)
                c.text("MODIFIED", x, kSubTop, T::kMicro, th.ink52);
        }

        // SAVE: an outlined button.
        const funkgui::Col border = pressed_ == Part::save && armed_ ? th.accent
                                                                     : funkgui::mix(th.ink32, th.ink70, hoverAmt_[3]);
        c.rrect(kSaveBox.x, kSaveBox.y, kSaveBox.w, kSaveBox.h, 2.0f, funkgui::fade(th.ground, 0.0f), 1.0f, border);
        c.text("SAVE", kSaveBox.centreX(), c.capCentreTop(kSaveBox.centreY(), T::kCaption), T::kCaption,
               hoverInk(Part::save, th.ink52, th.ink100), funkgui::Align::centre);

        if (ctx_.focusVisible)
            if (const Part f = partOf(ctx_.focus); f != Part::none)
                funkgui::drawFocusRing(c, f == Part::prev ? kPrev : f == Part::name ? kName : f == Part::next ? kNext
                                                                                                             : kSaveBox,
                                       th.accent);
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool PresetStrip::hit(funkgui::Point p) const { return layout::kPresetStrip.contains(p); }

    PresetStrip::Part PresetStrip::partAt(funkgui::Point p) const noexcept
    {
        if (kPrev.contains(p))
            return Part::prev;
        if (kName.contains(p))
            return Part::name;
        if (kNext.contains(p))
            return Part::next;
        if (kSave.contains(p))
            return Part::save;
        return Part::none;
    }

    PresetStrip::Part PresetStrip::partOf(uint32_t id) const noexcept
    {
        if (viewIndexOf(id) != static_cast<int>(ViewIndex::presetStrip))
            return Part::none;
        switch (id & 0xFFFFu)
        {
            case kPrevLocal: return Part::prev;
            case kNameLocal: return Part::name;
            case kNextLocal: return Part::next;
            case kSaveLocal: return Part::save;
            default:         return Part::none;
        }
    }

    void PresetStrip::step(int delta)
    {
        refresh();
        if (count_ <= 0 || delta == 0)
            return;
        ctx_.facade.presets().step(delta);                       // P3: one apply, wrapping at the ends
        refresh();
    }

    void PresetStrip::openBrowser()
    {
        ctx_.panel.setView({ nullptr, ctx_.screen, ctx_.scTab, Overlay::presetBrowser }, false);
    }

    void PresetStrip::save()
    {
        // The browser takes the name (PresetBrowser.h): open it, then press its SAVE AS through the Panel.
        openBrowser();
        ctx_.panel.a11yAction(a11yId(ViewIndex::presetBrowser, PresetBrowser::kSaveAsLocal),
                              funkgui::A11yAction::press, 0.0);
    }

    void PresetStrip::activate(Part p)
    {
        switch (p)
        {
            case Part::prev: step(-1); break;
            case Part::next: step(1); break;
            case Part::name: openBrowser(); break;
            case Part::save: save(); break;
            case Part::none: break;
        }
    }

    void PresetStrip::pointerMove(const funkgui::PointerEvent&) { pointerOver_ = true; }

    void PresetStrip::pointerExit() { pointerOver_ = false; }

    void PresetStrip::pointerDown(const funkgui::PointerEvent& e)
    {
        pointerOver_ = true;
        refresh();
        const Part p = partAt({ e.x, e.y });
        pressed_ = Part::none;
        armed_ = false;
        if (p == Part::none)
            return;
        if (e.popup)
        {
            if (p == Part::name)
                openBrowser();                                   // no host menu for presets: the browser has one
            return;
        }
        pressed_ = p;
        armed_ = p == Part::name || p == Part::save;
        if (p == Part::prev || p == Part::next)
            step(p == Part::prev ? -1 : 1);                      // one step per click, on the press (HR's strip)
    }

    void PresetStrip::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (armed_ && partAt({ e.x, e.y }) != pressed_)
            armed_ = false;                                      // dragging off cancels
    }

    void PresetStrip::pointerUp(const funkgui::PointerEvent& e)
    {
        const Part p = pressed_;
        const bool fire = armed_ && partAt({ e.x, e.y }) == p;
        pressed_ = Part::none;
        armed_ = false;
        if (fire)
            activate(p);
    }

    bool PresetStrip::key(const funkgui::KeyEvent& e)
    {
        const Part f = partOf(ctx_.focus);
        if (f == Part::none)
            return false;
        refresh();
        switch (e.key)
        {
            case funkgui::Key::enter:
            case funkgui::Key::space:
                activate(f);
                return true;
            case funkgui::Key::right:
            case funkgui::Key::up:
                if (f != Part::name)
                    return false;
                step(1);
                return true;
            case funkgui::Key::left:
            case funkgui::Key::down:
                if (f != Part::name)
                    return false;
                step(-1);
                return true;
            case funkgui::Key::character:
            case funkgui::Key::tab:
            case funkgui::Key::pageUp:
            case funkgui::Key::pageDown:
            case funkgui::Key::home:
            case funkgui::Key::end:
            case funkgui::Key::escape:
            case funkgui::Key::backspace:
            case funkgui::Key::del:
                return false;
        }
        return false;
    }

    funkgui::Cursor PresetStrip::cursor(funkgui::Point p) const
    {
        const Part part = partAt(p);
        if (part == Part::none || ((part == Part::prev || part == Part::next) && count_ <= 0))
            return funkgui::Cursor::normal;
        return funkgui::Cursor::pointingHand;
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void PresetStrip::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const bool any = count_ > 0;
        funkgui::A11yItem prevItem;
        prevItem.id = a11yId(ViewIndex::presetStrip, kPrevLocal);
        prevItem.role = funkgui::A11yRole::button;
        prevItem.bounds = kPrev;
        prevItem.title = "Previous preset";
        prevItem.enabled = any;
        out.push_back(std::move(prevItem));

        funkgui::A11yItem nameItem;
        nameItem.id = a11yId(ViewIndex::presetStrip, kNameLocal);
        nameItem.role = funkgui::A11yRole::comboBox;
        nameItem.bounds = kName;
        nameItem.title = "Preset";
        std::string value = current_ >= 0 && rawName_[0] != '\0' ? std::string(rawName_)
                          : any                                   ? std::string("Untitled")
                                                                  : std::string("No presets");
        if (modified_)
            value += ", modified";
        nameItem.value = std::move(value);
        if (current_ >= 0)
            nameItem.description = (rawCategory_[0] != '\0' ? std::string(rawCategory_) + ", " : std::string())
                             + (factory_ ? "factory" : "user");
        nameItem.help = "Opens the preset browser";
        out.push_back(std::move(nameItem));

        funkgui::A11yItem nextItem;
        nextItem.id = a11yId(ViewIndex::presetStrip, kNextLocal);
        nextItem.role = funkgui::A11yRole::button;
        nextItem.bounds = kNext;
        nextItem.title = "Next preset";
        nextItem.enabled = any;
        out.push_back(std::move(nextItem));

        funkgui::A11yItem saveItem;
        saveItem.id = a11yId(ViewIndex::presetStrip, kSaveLocal);
        saveItem.role = funkgui::A11yRole::button;
        saveItem.bounds = kSaveBox;
        saveItem.title = "Save preset";
        saveItem.help = "Saves the current sound as a new preset";
        out.push_back(std::move(saveItem));
    }

    int PresetStrip::focusOrder(std::span<uint32_t> out) const
    {
        constexpr std::array<uint32_t, 4> kLocals { kPrevLocal, kNameLocal, kNextLocal, kSaveLocal };
        const std::size_t n = std::min(out.size(), kLocals.size());
        for (std::size_t i = 0; i < n; ++i)
            out[i] = a11yId(ViewIndex::presetStrip, kLocals[i]);
        return static_cast<int>(n);
    }

    uint32_t PresetStrip::a11yRevision() const { return a11yRev_; }

    void PresetStrip::a11yAction(uint32_t id, funkgui::A11yAction a, double)
    {
        const Part p = partOf(id);
        if (p == Part::none)
            return;
        refresh();
        switch (a)
        {
            case funkgui::A11yAction::press:
            case funkgui::A11yAction::toggle:
                activate(p);
                break;
            case funkgui::A11yAction::increment:
                if (p == Part::name)
                    step(1);
                break;
            case funkgui::A11yAction::decrement:
                if (p == Part::name)
                    step(-1);
                break;
            case funkgui::A11yAction::showMenu:
                if (p == Part::name)
                    openBrowser();
                break;
            case funkgui::A11yAction::setValue:
            case funkgui::A11yAction::focus:
                break;
        }
    }
}
