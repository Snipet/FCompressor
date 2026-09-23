// Source/editor/views/Header.cpp — the header (see Header.h): wordmark, fitted topology caption and the Mode latch.
#include "editor/views/Header.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"

#include "FcmpProduct.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/FontAtlasSdf.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/FocusRing.h>
#include <funkgui/widgets/RuleSlider.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcmp::ui
{
    namespace
    {
        namespace H = layout::header;
        namespace T = funkgui::type;

        constexpr uint32_t kLatchLocal = 1;                     // the comboBox (the only Tab stop)

        // ‹ name › as one rectangle: the wheel's hit area, the a11y bounds and the focus ring's.
        constexpr funkgui::Rect kLatchCells { H::kModePrev.x, H::kModePrev.y, H::kModeNext.right() - H::kModePrev.x,
                                              H::kModePrev.h };
        constexpr float kTopologyMaxW = H::kPresetStrip.x - 4.0f - H::kTopology.x;   // 184: 4 px clear of the strip
        constexpr std::array<float, 2> kTopologyTwoLineTops { 33.0f, 45.0f };        // two lines round y 40
        constexpr float kNameMaxW = H::kModeNext.x - 4.0f - H::kModeNameTextX;      // 204: 4 px clear of ›
        constexpr float kNameRuleY = 44.0f;                     // between the name (cap centre 30) and the group (48)
        constexpr float kChevronInset = 1.5f;                   // HR PresetPanel.cpp:502-507
        constexpr float kChevronDepth = 3.5f;
        constexpr float kChevronHalf = 5.0f;
        constexpr float kChevronStroke = 1.5f;
        constexpr float kNameFadeTau = funkgui::kScreenFadeTau; // 0.12 s

        // 01 Group order (the browser's columns, 02 §8.6).
        constexpr std::array<const char*, 8> kGroupNames { "VCA", "FET", "OPTO", "VARI-MU", "DIODE", "MODERN", "LIMIT",
                                                           "OTHER" };
        constexpr const char* kSep = " \xC2\xB7 ";              // " · "

        const char* groupName(fcdsp::Group g) noexcept
        {
            const auto i = static_cast<std::size_t>(g);
            return i < kGroupNames.size() ? kGroupNames[i] : "OTHER";
        }

        // Appends UTF-8 text to a fixed buffer, never cutting inside a codepoint.
        struct Line
        {
            Line(char* o, std::size_t c) noexcept : out(o), cap(c)
            {
                if (cap > 0)
                    out[0] = '\0';
            }

            char*       out;
            std::size_t cap;
            std::size_t n = 0;

            void add(std::string_view s) noexcept
            {
                for (const char ch : s)
                {
                    if (n + 1 >= cap)
                    {
                        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0u) == 0x80u)
                            --n;                                 // do not leave half a codepoint
                        break;
                    }
                    out[n++] = ch;
                }
                out[n] = '\0';
            }
            void add(const char* s) noexcept { add(std::string_view(s != nullptr ? s : "")); }
            void add(int v) noexcept                             // a count (>= 0), without allocating
            {
                char digits[12];
                std::size_t i = sizeof digits;
                auto u = static_cast<unsigned>(v < 0 ? 0 : v);
                do
                {
                    digits[--i] = static_cast<char>('0' + u % 10u);
                    u /= 10u;
                } while (u != 0u && i > 0);
                add(std::string_view(digits + i, sizeof digits - i));
            }
        };

        const fcdsp::ModeDescriptor* descOfSlot(int slot) noexcept
        {
            const fcdsp::ModeEntry* e = slot >= 0 ? fcdsp::bySlot(slot) : nullptr;
            return e != nullptr ? e->desc : nullptr;
        }

        // ---- the topology caption's fit (Header.h) ---------------------------------------------------------------------

        struct Topology
        {
            const funkgui::TextStyle* style = &T::kCaption;
            int  lines = 0;
            char a[128]{};
            char b[128]{};
        };

        void copyRange(std::string_view s, char (&out)[128]) noexcept
        {
            const std::size_t n = std::min(s.size(), sizeof out - 1);
            std::memcpy(out, s.data(), n);
            out[n] = '\0';
        }

        // The best two-line break of `s` in `style`: at a " · " separator (dropped at the break) when one lets both
        // lines fit, else at a space; among the breaks that fit, the one with the narrower wider line. False: none fits.
        bool splitTwo(const funkgui::FontAtlasSdf& atlas, std::string_view s, const funkgui::TextStyle& style,
                      Topology& out)
        {
            const std::string_view sep(kSep);
            for (const bool atSeparator : { true, false })
            {
                float best = 0.0f;
                bool found = false;
                for (std::size_t i = 0; i < s.size(); ++i)
                {
                    const bool here = atSeparator ? s.substr(i, sep.size()) == sep : s[i] == ' ';
                    if (!here)
                        continue;
                    char a[128];
                    char b[128];
                    copyRange(s.substr(0, i), a);
                    copyRange(s.substr(i + (atSeparator ? sep.size() : 1)), b);
                    // A space break never starts the second line with punctuation ("/ OLD …") nor ends the first
                    // with a dangling " ·".
                    const auto alnum = [](char ch) {
                        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
                    };
                    const std::size_t na = std::strlen(a);
                    if (!atSeparator && (!alnum(b[0]) || (na > 0 && static_cast<unsigned char>(a[na - 1]) == 0xB7u)))
                        continue;
                    const float wa = funkgui::text::width(atlas, a, style);
                    const float wb = funkgui::text::width(atlas, b, style);
                    if (a[0] == '\0' || b[0] == '\0' || wa > kTopologyMaxW || wb > kTopologyMaxW)
                        continue;
                    const float worst = std::max(wa, wb);
                    if (!found || worst < best)
                    {
                        found = true;
                        best = worst;
                        std::memcpy(out.a, a, sizeof a);
                        std::memcpy(out.b, b, sizeof b);
                    }
                }
                if (found)
                {
                    out.style = &style;
                    out.lines = 2;
                    return true;
                }
            }
            return false;
        }

        Topology fitTopology(const funkgui::FontAtlasSdf& atlas, const char* line)
        {
            Topology t;
            const std::string_view s(line != nullptr ? line : "");
            if (s.empty())
                return t;
            if (funkgui::text::fits(atlas, line, T::kCaption, kTopologyMaxW))
            {
                t.lines = 1;
                copyRange(s, t.a);
                return t;
            }
            if (splitTwo(atlas, s, T::kCaption, t) || splitTwo(atlas, s, T::kMicro, t))
                return t;
            // Nothing fits: the most balanced space break in kMicro, each line cut with an ellipsis.
            t.style = &T::kMicro;
            t.lines = 2;
            std::size_t cut = s.size() / 2;
            if (const std::size_t sp = s.rfind(' ', cut); sp != std::string_view::npos)
                cut = sp;
            char a[128];
            char b[128];
            copyRange(s.substr(0, cut), a);
            copyRange(cut < s.size() ? s.substr(cut + 1) : std::string_view(), b);
            funkgui::text::fitEllipsis(atlas, a, T::kMicro, kTopologyMaxW, t.a, sizeof t.a);
            funkgui::text::fitEllipsis(atlas, b, T::kMicro, kTopologyMaxW, t.b, sizeof t.b);
            return t;
        }

        void chevron(funkgui::Canvas& c, const funkgui::Rect& r, int dir, float capCentre, funkgui::Col col)
        {
            const float cx = r.centreX() + (dir < 0 ? kChevronInset : -kChevronInset);
            const float tip = cx + (dir < 0 ? -kChevronDepth : kChevronDepth);
            c.segment(cx, capCentre - kChevronHalf, tip, capCentre, kChevronStroke, col);
            c.segment(tip, capCentre, cx, capCentre + kChevronHalf, kChevronStroke, col);
        }
    }

    Header::Header(PanelContext& ctx) : ctx_(ctx), shown_(ctx.frame.res.view.slot, kNameFadeTau)
    {
        // The global order (02 §8.5): Group, then slot; retired and unassigned slots never appear.
        std::array<const fcdsp::ModeSlot*, fcdsp::kModeCapacity> rows{};
        std::size_t n = 0;
        for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
            if (s.entry != nullptr && s.entry->desc != nullptr && n < rows.size())
                rows[n++] = &s;
        std::stable_sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(n),
                         [](const fcdsp::ModeSlot* a, const fcdsp::ModeSlot* b) {
                             const auto ga = static_cast<int>(a->entry->desc->group);
                             const auto gb = static_cast<int>(b->entry->desc->group);
                             return ga != gb ? ga < gb : a->slot < b->slot;
                         });
        for (std::size_t i = 0; i < n; ++i)
            order_[i] = rows[i]->slot;
        nOrder_ = static_cast<int>(n);

        // The wordmark: the product name's first letter, then the rest, upper case (02 §6.3: "F" + "COMPRESSOR").
        const std::string_view name(product::kName);
        std::size_t k = 0;
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            const char ch = name[i] >= 'a' && name[i] <= 'z' ? static_cast<char>(name[i] - 'a' + 'A') : name[i];
            if (i == 0)
                wordFirst_[0] = ch;
            else if (k + 1 < sizeof wordRest_)
                wordRest_[k++] = ch;
        }
        rebuildSpec();
    }

    // ---- state ----------------------------------------------------------------------------------------------------------

    int Header::orderIndex() const noexcept
    {
        // The Mode the `mode` port holds now, not the last tick's resolve: key repeats and wheel notches can arrive
        // faster than frames (12 Hz while idle), and each step must start where the previous one landed.
        const float plain = fcdsp::toPlain(fcdsp::Pid::mode, ctx_.facade.port(fcdsp::Pid::mode).value01());
        const uint8_t slot = fcdsp::resolveSlot(static_cast<int>(std::lround(plain))).slot;
        for (int i = 0; i < nOrder_; ++i)
            if (order_[static_cast<std::size_t>(i)] == slot)
                return i;
        return -1;
    }

    void Header::rebuildSpec()
    {
        const fcdsp::ModeDescriptor* d = descOfSlot(shown_.incoming());
        Line line{ spec_, sizeof spec_ };
        line.add("MODE   ");
        line.add(d != nullptr ? d->specLine : "NO MODE");
        line.add("   ARROWS / WHEEL STEP   CLICK: ALL MODES");
        specSlot_ = shown_.incoming();
    }

    void Header::tick(float dt)
    {
        if (ctx_.frame.entry != nullptr && ctx_.frame.res.view.slot != shown_.pinned())
            shown_.pin(ctx_.frame.res.view.slot);                // crossfade the Mode texts (Header.h "with dwell")
        shown_.tick(dt);
        if (shown_.incoming() != specSlot_)
            rebuildSpec();

        hover_ = pointerOver_ ? partAt(ctx_.pointer) : Part::none;
        const std::array<Part, 3> parts { Part::prev, Part::name, Part::next };
        for (std::size_t i = 0; i < parts.size(); ++i)
            hoverAmt_[i] = funkgui::ease::hover(hoverAmt_[i], hover_ == parts[i], dt);

        const uint32_t id = a11yId(ViewIndex::header, kLatchLocal);
        if (hover_ != Part::none)
            ctx_.offerHand(fcdsp::Pid::mode, HandKind::hover, id, spec_);
        if (ctx_.focusVisible && ctx_.focus == id)
            ctx_.offerHand(fcdsp::Pid::mode, HandKind::focus, id, spec_);
    }

    bool Header::wantsFullRate() const
    {
        if (!shown_.settled())
            return true;
        const std::array<Part, 3> parts { Part::prev, Part::name, Part::next };
        for (std::size_t i = 0; i < parts.size(); ++i)
            if (!funkgui::ease::sameBits(hoverAmt_[i], hover_ == parts[i] ? 1.0f : 0.0f))
                return true;
        return false;
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void Header::drawModeTexts(funkgui::Canvas& c, const funkgui::Theme& th, int slot, float alpha) const
    {
        const fcdsp::ModeDescriptor* d = descOfSlot(slot);
        if (d == nullptr || !(alpha > 0.0f))
            return;
        {
            const funkgui::Canvas::Scope scope(c, tag::topology, false);
            const Topology t = fitTopology(ctx_.atlas, d->topologyLine);
            const funkgui::Col ink = funkgui::fade(th.ink32, alpha);
            if (t.lines == 1)
                c.text(t.a, H::kTopology.x, H::kTopology.y, *t.style, ink);
            else if (t.lines == 2)
            {
                c.text(t.a, H::kTopology.x, kTopologyTwoLineTops[0], *t.style, ink);
                c.text(t.b, H::kTopology.x, kTopologyTwoLineTops[1], *t.style, ink);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::modeName, false);
            char name[64];
            char fitted[64];
            const std::size_t n = std::min(d->name.size(), sizeof name - 1);
            std::memcpy(name, d->name.data(), n);
            name[n] = '\0';
            funkgui::text::fitEllipsis(ctx_.atlas, name, T::kLatch, kNameMaxW, fitted, sizeof fitted);
            const bool pressed = pressed_ == Part::name && armed_;
            c.text(fitted, H::kModeNameTextX, c.capCentreTop(H::kModeNameCentreY, T::kLatch), T::kLatch,
                   funkgui::fade(pressed ? th.accent : th.ink100, alpha));
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::modeGroup, false);
            char group[64];
            Line line{ group, sizeof group };
            int pos = -1;
            for (int i = 0; i < nOrder_; ++i)
                if (order_[static_cast<std::size_t>(i)] == slot)
                    pos = i;
            line.add(groupName(d->group));
            if (pos >= 0)
            {
                line.add(kSep);
                line.add(pos + 1);
                line.add(" OF ");
                line.add(nOrder_);
            }
            c.text(group, H::kModeGroup.x, H::kModeGroup.y, T::kMicro, funkgui::fade(th.ink32, alpha));
        }
    }

    void Header::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        {
            const funkgui::Canvas::Scope scope(c, tag::wordmark, false);
            c.text(wordFirst_, H::kWordmark.x, H::kWordmark.y, T::kWordmark, th.ink100);
            const float x = H::kWordmark.x + c.textWidth(wordFirst_, T::kWordmark) + H::kWordmarkGap;
            c.text(wordRest_, x, H::kWordmark.y, T::kWordmark, th.ink52);
        }

        // The Mode texts: the outgoing Mode at 1 − a, the incoming one at a (only the incoming one once settled).
        const float a = shown_.amount();
        if (shown_.outgoing() != shown_.incoming() && a < 1.0f)
            drawModeTexts(c, th, shown_.outgoing(), 1.0f - a);
        drawModeTexts(c, th, shown_.incoming(), a);

        const funkgui::Canvas::Scope scope(c, tag::modeLatch, false);
        c.text("MODE", H::kModeCaptionRight, H::kModeCaptionTop, T::kCaption, th.ink52, funkgui::Align::right);
        const float capCentre = H::kModeNameCentreY;
        const auto chevronInk = [&](Part p, float h) {
            return pressed_ == p ? th.accent : funkgui::mix(th.ink52, th.ink100, h);
        };
        chevron(c, H::kModePrev, -1, capCentre, chevronInk(Part::prev, hoverAmt_[0]));
        chevron(c, H::kModeNext, +1, capCentre, chevronInk(Part::next, hoverAmt_[2]));
        const bool open = ctx_.overlay == Overlay::modeBrowser;
        c.hairlineH(H::kModeName.x, kNameRuleY, H::kModeName.w,
                    open ? th.ink52 : funkgui::mix(th.ink16, th.ink32, hoverAmt_[1]));
        if (ctx_.focusVisible && ctx_.focus == a11yId(ViewIndex::header, kLatchLocal))
            funkgui::drawFocusRing(c, kLatchCells, th.accent);
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool Header::hit(funkgui::Point p) const { return layout::kHeader.contains(p); }

    Header::Part Header::partAt(funkgui::Point p) const noexcept
    {
        if (H::kModePrev.contains(p))
            return Part::prev;
        if (H::kModeName.contains(p))
            return Part::name;
        if (H::kModeNext.contains(p))
            return Part::next;
        return Part::none;
    }

    void Header::selectOrder(int index)
    {
        if (ctx_.gestures == nullptr || nOrder_ <= 0)
            return;
        index = std::clamp(index, 0, nOrder_ - 1);
        const float slot = static_cast<float>(order_[static_cast<std::size_t>(index)]);
        ctx_.gestures->tap(ctx_.facade.port(fcdsp::Pid::mode), fcdsp::toNorm(fcdsp::Pid::mode, slot));
    }

    void Header::stepMode(int delta, bool wrap)
    {
        if (nOrder_ <= 0 || delta == 0)
            return;
        const int at = orderIndex();
        int to = at < 0 ? (delta > 0 ? 0 : nOrder_ - 1) : at + delta;
        if (wrap)
            to = ((to % nOrder_) + nOrder_) % nOrder_;
        else
            to = std::clamp(to, 0, nOrder_ - 1);
        if (to != at)
            selectOrder(to);                                     // a tap writes nothing when unchanged
    }

    void Header::openBrowser()
    {
        ctx_.panel.setView({ nullptr, ctx_.screen, ctx_.scTab, Overlay::modeBrowser }, false);
    }

    void Header::showMenu(float x, float y)
    {
        if (ctx_.host != nullptr)
            ctx_.host->showParamMenu(ctx_.facade.port(fcdsp::Pid::mode), x, y);
    }

    void Header::pointerMove(const funkgui::PointerEvent&) { pointerOver_ = true; }

    void Header::pointerExit() { pointerOver_ = false; }

    void Header::pointerDown(const funkgui::PointerEvent& e)
    {
        pointerOver_ = true;
        const Part p = partAt({ e.x, e.y });
        if (p == Part::none)
            return;
        if (e.popup)
        {
            showMenu(e.x, e.y);                                  // the host menu, never a write
            return;
        }
        pressed_ = p;
        armed_ = p == Part::name;
        if (p == Part::prev || p == Part::next)
            stepMode(p == Part::prev ? -1 : 1, true);            // one tap per click (HR's strip steps on the click)
    }

    void Header::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (pressed_ == Part::name && !H::kModeName.contains({ e.x, e.y }))
            armed_ = false;                                      // dragging off cancels
    }

    void Header::pointerUp(const funkgui::PointerEvent& e)
    {
        const bool open = pressed_ == Part::name && armed_ && H::kModeName.contains({ e.x, e.y });
        pressed_ = Part::none;
        armed_ = false;
        if (open)
            openBrowser();
    }

    bool Header::wheel(const funkgui::WheelEvent& e)
    {
        if (!kLatchCells.contains({ e.x, e.y }) || ctx_.gestures == nullptr || ctx_.host == nullptr || nOrder_ <= 0)
            return false;
        const double now = ctx_.host->nowSeconds();
        if (wheelLast_ < 0.0 || now - wheelLast_ >= funkgui::GestureController::kWheelIdle)
            wheelAcc_ = 0.0f;                                    // a new burst starts from nothing
        wheelLast_ = now;

        // Whole notches, as RuleSlider counts them: one per discrete event, else smooth deltas in kWheelNotch units.
        const float delta = e.dy != 0.0f ? e.dy : e.dx;          // shift+scroll arrives horizontal (HR :1830)
        const float v = (e.reversed ? -1.0f : 1.0f) * delta;
        int k = 0;
        if (std::isfinite(v))
        {
            if (!e.smooth)
                k = v > 0.0f ? 1 : (v < 0.0f ? -1 : 0);
            else
            {
                constexpr float notch = funkgui::RuleSlider::kWheelNotch;
                wheelAcc_ = std::clamp(wheelAcc_ + v, -64.0f * notch, 64.0f * notch);
                k = static_cast<int>(wheelAcc_ / notch);
                wheelAcc_ -= static_cast<float>(k) * notch;
            }
        }
        const int at = orderIndex();
        if (k != 0)
        {
            const int to = std::clamp(at < 0 ? (k > 0 ? 0 : nOrder_ - 1) : at + k, 0, nOrder_ - 1);
            if (to != at)
            {
                const float slot = static_cast<float>(order_[static_cast<std::size_t>(to)]);
                ctx_.gestures->wheelTo(ctx_.facade.port(fcdsp::Pid::mode), fcdsp::toNorm(fcdsp::Pid::mode, slot), now);
            }
        }
        return true;
    }

    bool Header::key(const funkgui::KeyEvent& e)
    {
        if (ctx_.focus != a11yId(ViewIndex::header, kLatchLocal))
            return false;
        switch (e.key)
        {
            case funkgui::Key::right:
            case funkgui::Key::up:       stepMode(1, false); return true;
            case funkgui::Key::left:
            case funkgui::Key::down:     stepMode(-1, false); return true;
            case funkgui::Key::home:     selectOrder(0); return true;
            case funkgui::Key::end:      selectOrder(nOrder_ - 1); return true;
            case funkgui::Key::enter:
            case funkgui::Key::space:    openBrowser(); return true;
            case funkgui::Key::character:
            case funkgui::Key::tab:
            case funkgui::Key::pageUp:
            case funkgui::Key::pageDown:
            case funkgui::Key::escape:
            case funkgui::Key::backspace:
            case funkgui::Key::del:      return false;
        }
        return false;
    }

    funkgui::Cursor Header::cursor(funkgui::Point p) const
    {
        return partAt(p) != Part::none ? funkgui::Cursor::pointingHand : funkgui::Cursor::normal;
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void Header::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::header, kLatchLocal);
        it.role = funkgui::A11yRole::comboBox;
        it.bounds = kLatchCells;
        it.title = "Mode";
        if (const fcdsp::ModeDescriptor* d = ctx_.frame.entry != nullptr ? ctx_.frame.entry->desc : nullptr)
        {
            it.value = std::string(d->name);
            char group[64];
            Line line{ group, sizeof group };
            line.add(groupName(d->group));
            int pos = -1;
            for (int i = 0; i < nOrder_; ++i)
                if (order_[static_cast<std::size_t>(i)] == ctx_.frame.res.view.slot)
                    pos = i;
            if (pos >= 0)
            {
                line.add(kSep);
                line.add(pos + 1);
                line.add(" OF ");
                line.add(nOrder_);
            }
            it.description = group;                              // the group line, as drawn
            it.help = d->topologyLine != nullptr ? d->topologyLine : "";
        }
        out.push_back(std::move(it));
    }

    int Header::focusOrder(std::span<uint32_t> out) const
    {
        if (out.empty())
            return 0;
        out[0] = a11yId(ViewIndex::header, kLatchLocal);
        return 1;
    }

    void Header::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (id != a11yId(ViewIndex::header, kLatchLocal))
            return;
        switch (a)
        {
            case funkgui::A11yAction::press:
            case funkgui::A11yAction::toggle:    openBrowser(); break;
            case funkgui::A11yAction::increment: stepMode(1, false); break;
            case funkgui::A11yAction::decrement: stepMode(-1, false); break;
            case funkgui::A11yAction::setValue:
                if (std::isfinite(value))
                    selectOrder(static_cast<int>(std::lround(value)));   // the position in the global order
                break;
            case funkgui::A11yAction::showMenu:  showMenu(kLatchCells.centreX(), kLatchCells.centreY()); break;
            case funkgui::A11yAction::focus:     break;
        }
    }
}
