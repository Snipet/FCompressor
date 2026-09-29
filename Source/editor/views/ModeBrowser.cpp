// Source/editor/views/ModeBrowser.cpp — the Mode browser (see ModeBrowser.h): the ModeGrid model and the overlay over it.
#include "editor/views/ModeBrowser.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/ProductTheme.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/params/ParamPort.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/FocusRing.h>
#include <funkgui/widgets/RuleSlider.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace B = layout::browser;
        namespace T = funkgui::type;
        using fcdsp::Pid;

        // 01 Group order: the columns (02 §8.6).
        constexpr std::array<const char*, ModeGrid::kGroups> kGroupNames { "VCA", "FET", "OPTO", "VARI-MU", "DIODE",
                                                                           "MODERN", "LIMIT", "OTHER" };
        constexpr const char* kSpecHint = "   ALT-CLICK: + DEFAULTS";

        // Drawing (ModeBrowser.h): the ground grown past kOverlay, the name fit, the pager.
        constexpr funkgui::Rect kGround { layout::kOverlay.x - 8.0f, layout::kOverlay.y - 4.0f,
                                          layout::kOverlay.w + 16.0f, layout::kOverlay.h + 8.0f };
        constexpr float kNameMaxW = B::kColumnW - 12.0f;       // 98: 6 px clear of the next column's current bar
        constexpr funkgui::Rect kPagerPrev { 840.0f, 328.0f, 20.0f, 20.0f };
        constexpr funkgui::Rect kPagerNext { 900.0f, 328.0f, 20.0f, 20.0f };   // right-aligned to x 920
        constexpr float kPagerTextX = 880.0f;                  // "1/2", centred between the chevrons, top kPagerY
        constexpr float kChevronDepth = 3.0f;
        constexpr float kChevronHalf = 4.0f;
        constexpr float kChevronStroke = 1.5f;
        constexpr float kHeadingHitDy = -3.0f;                 // a heading's a11y bounds: {colX, 69, 106, 16}
        constexpr float kHeadingHitH = 16.0f;

        // Local a11y ids (ModeBrowser.h).
        constexpr uint32_t kPagerPrevId = 0x10;
        constexpr uint32_t kPagerTextId = 0x11;
        constexpr uint32_t kPagerNextId = 0x12;
        constexpr uint32_t kHeadingIdBase = 0x20;
        constexpr uint32_t kRowIdBase = 0x100;

        int groupIndex(fcdsp::Group g) noexcept
        {
            const auto i = static_cast<int>(g);
            return i >= 0 && i < ModeGrid::kGroups ? i : ModeGrid::kGroups - 1;
        }

        char upperAscii(char c) noexcept { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

        bool startsWithNoCase(const char* s, std::string_view prefix) noexcept
        {
            for (std::size_t i = 0; i < prefix.size(); ++i)
                if (s[i] == '\0' || upperAscii(s[i]) != upperAscii(prefix[i]))
                    return false;
            return true;
        }

        void chevron(funkgui::Canvas& c, const funkgui::Rect& r, int dir, float capCentre, funkgui::Col col)
        {
            const float cx = r.centreX() - static_cast<float>(dir) * kChevronDepth * 0.5f;
            const float tip = cx + static_cast<float>(dir) * kChevronDepth;
            c.segment(cx, capCentre - kChevronHalf, tip, capCentre, kChevronStroke, col);
            c.segment(tip, capCentre, cx, capCentre + kChevronHalf, kChevronStroke, col);
        }

        uint32_t rowId(uint8_t slot) noexcept { return a11yId(ViewIndex::modeBrowser, kRowIdBase + slot); }

        // The keyboard ring around a row: its hit rectangle from x − 8 to x + 102, so the ring clears the name (which
        // starts at the row's x) and encloses the current bar at x − 6, 3 px short of the next column's bar.
        funkgui::Rect ringRect(const funkgui::Rect& row) noexcept { return { row.x - 8.0f, row.y, row.w + 4.0f, row.h }; }
    }

    // ---- ModeGrid -------------------------------------------------------------------------------------------------------

    ModeGrid::ModeGrid(std::span<const Item> items) noexcept
    {
        // The global order (02 §8.5): Group, then slot; a stable insertion sort (at most 128 items, no allocation).
        const int n = static_cast<int>(std::min<std::size_t>(items.size(), static_cast<std::size_t>(kMaxItems)));
        std::array<int, kMaxItems> order{};
        for (int i = 0; i < n; ++i)
        {
            const Item& it = items[static_cast<std::size_t>(i)];
            int j = i;
            while (j > 0)
            {
                const Item& prev = items[static_cast<std::size_t>(order[static_cast<std::size_t>(j - 1)])];
                const int gp = groupIndex(prev.group), gi = groupIndex(it.group);
                if (gp < gi || (gp == gi && prev.slot <= it.slot))
                    break;
                order[static_cast<std::size_t>(j)] = order[static_cast<std::size_t>(j - 1)];
                --j;
            }
            order[static_cast<std::size_t>(j)] = i;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<std::size_t>(i);
            const Item& it = items[static_cast<std::size_t>(order[k])];
            slot_[k] = it.slot;
            group_[k] = static_cast<fcdsp::Group>(groupIndex(it.group));
            const std::size_t len = std::min(it.name.size(), kNameBytes - 1);
            if (len > 0)
                std::memcpy(name_[k].data(), it.name.data(), len);
            name_[k][len] = '\0';
            spec_[k] = it.spec;
        }
        n_ = n;

        // One column per group (an empty group keeps its column), a group of more than kRows wrapping into more.
        int i = 0;
        int k = 0;
        for (int g = 0; g < kGroups; ++g)
        {
            const int first = i;
            while (i < n && groupIndex(group_[static_cast<std::size_t>(i)]) == g)
                ++i;
            const int count = i - first;
            const int parts = std::max(1, (count + B::kRows - 1) / B::kRows);
            for (int part = 0; part < parts && k < kMaxColumns; ++part, ++k)
            {
                Column& col = columns_[static_cast<std::size_t>(k)];
                col.group = static_cast<fcdsp::Group>(g);
                col.part = part;
                col.first = first + part * B::kRows;
                col.count = std::clamp(count - part * B::kRows, 0, B::kRows);
                col.groupCount = count;
                for (int r = 0; r < col.count; ++r)
                {
                    const auto at = static_cast<std::size_t>(col.first + r);
                    column_[at] = static_cast<int16_t>(k);
                    row_[at] = static_cast<int16_t>(r);
                }
            }
        }
        nColumns_ = k;
    }

    ModeGrid ModeGrid::registered() noexcept
    {
        std::array<Item, kMaxItems> items{};
        std::size_t n = 0;
        for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
            if (s.entry != nullptr && s.entry->desc != nullptr && n < items.size())
            {
                const fcdsp::ModeDescriptor& d = *s.entry->desc;
                items[n++] = { s.slot, d.group, d.name, d.specLine != nullptr ? std::string_view(d.specLine) : "" };
            }
        return ModeGrid(std::span<const Item>(items.data(), n));
    }

    uint8_t ModeGrid::slot(int i) const noexcept { return slot_[static_cast<std::size_t>(i)]; }
    fcdsp::Group ModeGrid::group(int i) const noexcept { return group_[static_cast<std::size_t>(i)]; }
    const char* ModeGrid::name(int i) const noexcept { return name_[static_cast<std::size_t>(i)].data(); }
    std::string_view ModeGrid::spec(int i) const noexcept { return spec_[static_cast<std::size_t>(i)]; }

    int ModeGrid::find(int modeSlot) const noexcept
    {
        for (int i = 0; i < n_; ++i)
            if (slot_[static_cast<std::size_t>(i)] == modeSlot)
                return i;
        return -1;
    }

    const ModeGrid::Column& ModeGrid::column(int k) const noexcept { return columns_[static_cast<std::size_t>(k)]; }
    int ModeGrid::columnOf(int i) const noexcept { return column_[static_cast<std::size_t>(i)]; }
    int ModeGrid::rowOf(int i) const noexcept { return row_[static_cast<std::size_t>(i)]; }

    int ModeGrid::itemAt(int k, int row) const noexcept
    {
        if (k < 0 || k >= nColumns_)
            return -1;
        const Column& col = column(k);
        return row >= 0 && row < col.count ? col.first + row : -1;
    }

    int ModeGrid::pages() const noexcept { return std::max(1, (nColumns_ + B::kColumns - 1) / B::kColumns); }
    int ModeGrid::pageOf(int k) noexcept { return k >= 0 ? k / B::kColumns : 0; }
    int ModeGrid::pageOfItem(int i) const noexcept { return i >= 0 && i < n_ ? pageOf(columnOf(i)) : 0; }

    funkgui::Rect ModeGrid::rowRect(int i) const noexcept
    {
        return B::rowHit(columnOf(i) % B::kColumns, rowOf(i));
    }

    int ModeGrid::hit(int page, funkgui::Point p) const noexcept
    {
        const float top = B::kRowY0 + B::kRowHitDy;
        if (!(p.x >= B::kColumnX0) || !(p.y >= top))
            return -1;
        const int v = static_cast<int>((p.x - B::kColumnX0) / B::kColumnW);
        const int r = static_cast<int>((p.y - top) / B::kRowPitch);
        if (v >= B::kColumns || r >= B::kRows)
            return -1;
        const int i = itemAt(page * B::kColumns + v, r);
        return i >= 0 && B::rowHit(v, r).contains(p) ? i : -1;
    }

    void ModeGrid::heading(int k, char* out, std::size_t cap) const noexcept
    {
        if (out == nullptr || cap == 0)
            return;
        if (k < 0 || k >= nColumns_)
        {
            out[0] = '\0';
            return;
        }
        const Column& col = column(k);
        const char* g = kGroupNames[static_cast<std::size_t>(groupIndex(col.group))];
        if (col.part == 0)
            std::snprintf(out, cap, "%s  %d", g, col.groupCount);
        else
            std::snprintf(out, cap, "%s (%d)", g, col.part + 1);
    }

    int ModeGrid::step(int from, funkgui::Key key) const noexcept
    {
        if (n_ <= 0)
            return -1;
        const bool nav = key == funkgui::Key::up || key == funkgui::Key::down || key == funkgui::Key::left
                      || key == funkgui::Key::right || key == funkgui::Key::home || key == funkgui::Key::end
                      || key == funkgui::Key::pageUp || key == funkgui::Key::pageDown;
        if (from < 0 || from >= n_)
            return nav ? (key == funkgui::Key::end ? n_ - 1 : 0) : from;
        const int k = columnOf(from);
        const int r = rowOf(from);
        const auto across = [&](int dir) {
            for (int c = k + dir; c >= 0 && c < nColumns_; c += dir)
                if (const Column& col = column(c); col.count > 0)
                    return col.first + std::min(r, col.count - 1);
            return from;
        };
        const auto paged = [&](int dir) {
            const int p = pageOf(k) + dir;
            const int to = p >= 0 && p < pages() ? onPage(from, p) : -1;
            return to >= 0 ? to : from;
        };
        switch (key)
        {
            case funkgui::Key::up:       return r > 0 ? from - 1 : from;
            case funkgui::Key::down:     return r + 1 < column(k).count ? from + 1 : from;
            case funkgui::Key::left:     return across(-1);
            case funkgui::Key::right:    return across(1);
            case funkgui::Key::home:     return 0;
            case funkgui::Key::end:      return n_ - 1;
            case funkgui::Key::pageUp:   return paged(-1);
            case funkgui::Key::pageDown: return paged(1);
            case funkgui::Key::character:
            case funkgui::Key::tab:
            case funkgui::Key::escape:
            case funkgui::Key::enter:
            case funkgui::Key::backspace:
            case funkgui::Key::del:
            case funkgui::Key::space:    return from;
        }
        return from;
    }

    int ModeGrid::onPage(int from, int page) const noexcept
    {
        if (n_ <= 0 || page < 0 || page >= pages())
            return -1;
        const bool has = from >= 0 && from < n_;
        const int v = has ? columnOf(from) % B::kColumns : 0;
        const int r = has ? rowOf(from) : 0;
        const int lo = page * B::kColumns;
        const int hi = std::min(lo + B::kColumns, nColumns_);
        const int target = std::min(lo + v, hi - 1);
        for (int d = 0; d < B::kColumns; ++d)
            for (const int c : { target - d, target + d })
                if (c >= lo && c < hi && column(c).count > 0)
                    return column(c).first + std::min(r, column(c).count - 1);
        return -1;
    }

    int ModeGrid::typeAhead(int from, std::string_view prefix) const noexcept
    {
        if (n_ <= 0 || prefix.empty())
            return -1;
        const int start = from >= 0 && from < n_ ? from : 0;
        for (int k = 0; k < n_; ++k)
        {
            const int i = (start + k) % n_;
            if (startsWithNoCase(name(i), prefix))
                return i;
        }
        return -1;
    }

    // ---- ModeBrowser: state ---------------------------------------------------------------------------------------------

    ModeBrowser::ModeBrowser(PanelContext& ctx) : ModeBrowser(ctx, ModeGrid::registered()) {}

    ModeBrowser::ModeBrowser(PanelContext& ctx, const ModeGrid& grid) : ctx_(ctx), grid_(grid) {}

    bool ModeBrowser::isOpen() const noexcept { return ctx_.overlay == Overlay::modeBrowser; }

    int ModeBrowser::currentSlot() const noexcept
    {
        return ctx_.frame.entry != nullptr ? static_cast<int>(ctx_.frame.res.view.slot) : -1;
    }

    void ModeBrowser::reset()
    {
        highlight_ = grid_.find(currentSlot());
        if (highlight_ < 0 && grid_.size() > 0)
            highlight_ = 0;
        const int page = grid_.pageOfItem(highlight_);
        if (page != page_)
        {
            page_ = page;
            ++revision_;
        }
        hover_ = ctx_.pointerIn ? grid_.hit(page_, ctx_.pointer) : -1;
        hoverPager_ = 0;
        pressed_ = -1;
        armed_ = false;
        pressAlt_ = false;
        keyboard_ = ctx_.focusVisible;                           // opened from the keyboard: the ring shows at once ...
        if (keyboard_)
            takeFocus();                                         // ... on the row, not on the latch (S13 H1a)
        else
            focusSeen_ = ctx_.focus;
        typedLen_ = 0;
        wheelAcc_ = 0.0f;
        needsReset_ = false;
    }

    // ---- keyboard focus (S13 H1a; ModeBrowser.h "Keyboard focus") ---------------------------------------------------

    void ModeBrowser::takeFocus()
    {
        if (highlight_ >= 0 && highlight_ < grid_.size())
        {
            ctx_.focus = rowId(grid_.slot(highlight_));
            ctx_.focusVisible = true;
        }
        focusSeen_ = ctx_.focus;
    }

    void ModeBrowser::followFocus()
    {
        if (ctx_.focus == focusSeen_)
            return;
        focusSeen_ = ctx_.focus;
        if (!ctx_.focusVisible || viewIndexOf(ctx_.focus) != static_cast<int>(ViewIndex::modeBrowser))
            return;
        const uint32_t local = ctx_.focus & 0xFFFFu;
        if (local < kRowIdBase || local >= kRowIdBase + static_cast<uint32_t>(fcdsp::kModeCapacity))
            return;                                              // a pager button: the highlight stays
        const int item = grid_.find(static_cast<int>(local - kRowIdBase));
        if (item < 0)
            return;
        highlight_ = item;
        keyboard_ = true;
        hover_ = -1;
        if (const int page = grid_.pageOfItem(item); page != page_)
        {
            page_ = page;
            ++revision_;
        }
    }

    int ModeBrowser::focusedPager() const noexcept
    {
        if (!ctx_.focusVisible || grid_.pages() <= 1)
            return 0;
        if (ctx_.focus == a11yId(ViewIndex::modeBrowser, kPagerPrevId))
            return -1;
        return ctx_.focus == a11yId(ViewIndex::modeBrowser, kPagerNextId) ? 1 : 0;
    }

    void ModeBrowser::sync()
    {
        if (needsReset_ && isOpen())
            reset();
    }

    void ModeBrowser::tick(float dt)
    {
        // The Panel ticks the browser only while it is shown: a missed frame means it was hidden in between (a probe's
        // instant setView), so this showing is a fresh opening.
        const bool gap = lastTick_ < 0.0 || (dt > 0.0f && ctx_.seconds - lastTick_ > 1.5 * static_cast<double>(dt));
        lastTick_ = ctx_.seconds;
        if (!isOpen())
        {
            needsReset_ = true;                                  // fading out
            hover_ = -1;
            return;
        }
        if (gap)
            needsReset_ = true;
        sync();
        followFocus();                                           // Tab, or an a11y focus, may have moved it
        if (typedLen_ > 0 && ctx_.seconds - typedAt_ > static_cast<double>(B::kTypeAheadS))
            typedLen_ = 0;

        // The row under the hand: the hovered one, else the keyboard highlight, offered as the strongest kind so it
        // outranks the Mode latch underneath (hovered or focused: the browser was opened from it; ModeBrowser.h).
        const bool kbd = keyboard_ && highlight_ >= 0 && grid_.pageOfItem(highlight_) == page_;
        const int item = hover_ >= 0 ? hover_ : (kbd ? highlight_ : -1);
        if (item >= 0)
        {
            rebuildSpec(item);
            ctx_.offerHand(Pid::mode, HandKind::drag, rowId(grid_.slot(item)), spec_);
        }
    }

    void ModeBrowser::rebuildSpec(int item)
    {
        if (item == specItem_)
            return;
        specItem_ = item;
        // The spec line, plus the Alt hint when the whole line still fits the footer (never an ellipsised hint).
        const std::string_view s = grid_.spec(item);
        std::size_t n = std::min(s.size(), sizeof spec_ - 1);
        std::memcpy(spec_, s.data(), n);
        const std::size_t hint = std::strlen(kSpecHint);
        if (n + hint < sizeof spec_)
        {
            std::memcpy(spec_ + n, kSpecHint, hint + 1);
            if (!funkgui::text::fits(ctx_.atlas, spec_, T::kLabel, layout::footer::kSpecMaxW))
                spec_[n] = '\0';
            return;
        }
        spec_[n] = '\0';
    }

    int ModeBrowser::pagerAt(funkgui::Point p) const noexcept
    {
        if (grid_.pages() <= 1)
            return 0;
        if (kPagerPrev.contains(p))
            return -1;
        if (kPagerNext.contains(p))
            return 1;
        return 0;
    }

    bool ModeBrowser::pagerEnabled(int dir) const noexcept
    {
        return dir < 0 ? page_ > 0 : (dir > 0 && page_ < grid_.pages() - 1);
    }

    void ModeBrowser::setPage(int page)
    {
        page = std::clamp(page, 0, grid_.pages() - 1);
        if (page == page_)
            return;
        page_ = page;
        ++revision_;
        if (const int to = grid_.onPage(highlight_, page_); to >= 0)
            highlight_ = to;
        hover_ = ctx_.pointerIn ? grid_.hit(page_, ctx_.pointer) : -1;
        // A focused row leaves with its page: the focus follows the highlight onto the new one (a focused pager button
        // keeps the focus, so Return pages again).
        if (ctx_.focusVisible && viewIndexOf(ctx_.focus) == static_cast<int>(ViewIndex::modeBrowser)
            && focusedPager() == 0)
            takeFocus();
    }

    void ModeBrowser::moveHighlight(int item)
    {
        if (item < 0)
            return;
        highlight_ = item;
        keyboard_ = true;
        hover_ = -1;                                             // the keyboard takes over until the pointer moves
        const int page = grid_.pageOfItem(item);
        if (page != page_)
        {
            page_ = page;
            ++revision_;
        }
        takeFocus();                                             // the keyboard highlight is the Panel focus (S13 H1a)
    }

    void ModeBrowser::typeChar(char32_t ch)
    {
        if (typedLen_ > 0 && ctx_.seconds - typedAt_ > static_cast<double>(B::kTypeAheadS))
            typedLen_ = 0;
        typedAt_ = ctx_.seconds;
        if (typedLen_ + 1 < typed_.size())
            typed_[typedLen_++] = upperAscii(static_cast<char>(ch));
        // One character, or the same one repeated, cycles through the Modes it starts; more is a prefix that keeps
        // the highlight while it still matches.
        bool same = true;
        for (std::size_t i = 1; i < typedLen_; ++i)
            same = same && typed_[i] == typed_[0];
        const int n = grid_.size();
        int to = -1;
        if (n > 0 && same)
            to = grid_.typeAhead(highlight_ >= 0 ? (highlight_ + 1) % n : 0, std::string_view(typed_.data(), 1));
        else if (n > 0)
            to = grid_.typeAhead(highlight_, std::string_view(typed_.data(), typedLen_));
        if (to >= 0)
        {
            const std::size_t keep = typedLen_;
            moveHighlight(to);
            typedLen_ = keep;
        }
    }

    // ---- commits --------------------------------------------------------------------------------------------------------

    void ModeBrowser::commit(int item, bool withDefaults, bool closeAfter)
    {
        if (item >= 0 && item < grid_.size() && ctx_.gestures != nullptr)
        {
            const uint8_t slot = grid_.slot(item);
            funkgui::ParamPort& modePort = ctx_.facade.port(Pid::mode);
            const float v = fcdsp::toNorm(Pid::mode, static_cast<float>(slot));
            const fcdsp::ModeEntry* entry = fcdsp::bySlot(slot);
            if (!withDefaults || entry == nullptr || entry->desc == nullptr)
            {
                ctx_.gestures->tap(modePort, v);                 // 02 §8.4.3, K2 #4: `mode` and nothing else
            }
            else
            {
                // 02 §8.4.4, K2 #23: `mode`, then every live or stepped parameter whose Mode default differs from its
                // value, one gesture each inside one batch (tapMany skips an unchanged `mode`).
                const fcdsp::RawParams before = ctx_.facade.currentRaw();
                fcdsp::RawParams raw = before;
                raw.modeSlot = slot;
                fcdsp::modeDefaults(*entry->desc, raw);
                std::array<std::pair<funkgui::ParamPort*, float>, fcdsp::kNumModeParams + 1> writes{};
                std::size_t n = 0;
                writes[n++] = { &modePort, v };
                for (std::size_t k = 0; k < fcdsp::kNumModeParams; ++k)
                    if (!funkgui::ease::sameBits(raw.v[k], before.v[k]))
                    {
                        const auto pid = static_cast<Pid>(k);
                        writes[n++] = { &ctx_.facade.port(pid), fcdsp::toNorm(pid, raw.v[k]) };
                    }
                ctx_.gestures->tapMany(std::span<const std::pair<funkgui::ParamPort*, float>>(writes.data(), n));
            }
            highlight_ = item;
        }
        if (closeAfter)
            close();
    }

    void ModeBrowser::close()
    {
        needsReset_ = true;
        pressed_ = -1;
        armed_ = false;
        ctx_.panel.setView({ nullptr, ctx_.screen, ctx_.scTab, Overlay::none }, false);
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void ModeBrowser::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Rect& r = layout::kOverlay;
        {
            const funkgui::Canvas::Scope scope(c, tag::browserBg, false);
            c.rrect(kGround.x, kGround.y, kGround.w, kGround.h, 0.0f, th.ground);
            c.hairlineH(r.x, r.y, r.w, th.ink16);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, th.ink16);
        }

        const int current = currentSlot();
        const int first = page_ * B::kColumns;
        const int last = std::min(first + B::kColumns, grid_.columns());
        for (int k = first; k < last; ++k)
        {
            const int v = k - first;
            const float x = B::columnX(v);
            const ModeGrid::Column& col = grid_.column(k);
            {
                const funkgui::Canvas::Scope scope(c, tag::browserHeading, false);
                char heading[48];
                grid_.heading(k, heading, sizeof heading);
                char fitted[64];
                funkgui::text::fitEllipsis(ctx_.atlas, heading, T::kCaption, kNameMaxW, fitted, sizeof fitted);
                c.text(fitted, x, B::kHeadingY, T::kCaption, col.groupCount > 0 ? th.ink52 : th.ink32);
            }
            for (int row = 0; row < col.count; ++row)
            {
                const int i = col.first + row;
                const float y = B::rowY(row);
                const bool isCurrent = grid_.slot(i) == current;
                const bool lit = isCurrent || i == hover_ || (keyboard_ && i == highlight_);
                const funkgui::Col ink = armed_ && i == pressed_ ? th.accent : (lit ? th.ink100 : th.ink52);
                {
                    const funkgui::Canvas::Scope scope(c, tag::browserRow, false);
                    char fitted[64];
                    funkgui::text::fitEllipsis(ctx_.atlas, grid_.name(i), T::kLabel, kNameMaxW, fitted, sizeof fitted);
                    c.text(fitted, x, y, T::kLabel, ink);
                    // ADR-75 (v1.1): the Mode's colour, a swatch after its name, so colour and Mode are learnt together.
                    if (const fcdsp::ModeEntry* e = fcdsp::bySlot(grid_.slot(i)); e != nullptr && e->desc != nullptr)
                    {
                        const funkgui::Canvas::Scope sw(c, tag::modeSwatch, false);
                        c.disc(x + c.textWidth(fitted, T::kLabel) + layout::kSwatchInset,
                               y - c.capCentreTop(0.0f, T::kLabel), layout::kSwatchR, modeColour(e->desc->key, th));
                    }
                }
                if (isCurrent)
                {
                    const funkgui::Canvas::Scope scope(c, tag::browserCurrent, false);
                    c.rrect(x + B::kCurrentBarDx, y, B::kCurrentBarW, B::kCurrentBarH, 0.0f, th.ink100);
                }
            }
        }
        // One ring (S13 H1a): on the focused row while the keyboard drives the highlight, or on a focused pager button.
        if (keyboard_ && ctx_.focusVisible && highlight_ >= 0 && grid_.pageOfItem(highlight_) == page_
            && ctx_.focus == rowId(grid_.slot(highlight_)))
            funkgui::drawFocusRing(c, ringRect(grid_.rowRect(highlight_)), th.accent);
        if (const int dir = focusedPager(); dir != 0)
            funkgui::drawFocusRing(c, dir < 0 ? kPagerPrev : kPagerNext, th.accent);

        if (grid_.pages() > 1)
        {
            const funkgui::Canvas::Scope scope(c, tag::browserPager, false);
            const float capCentre = B::kPagerY - c.capCentreTop(0.0f, T::kCaption);
            const auto ink = [&](int dir) {
                return !pagerEnabled(dir) ? th.ink16 : (hoverPager_ == dir ? th.ink100 : th.ink52);
            };
            chevron(c, kPagerPrev, -1, capCentre, ink(-1));
            chevron(c, kPagerNext, 1, capCentre, ink(1));
            char text[16];
            std::snprintf(text, sizeof text, "%d/%d", page_ + 1, grid_.pages());
            c.text(text, kPagerTextX, B::kPagerY, T::kCaption, th.ink52, funkgui::Align::centre);
        }
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool ModeBrowser::hit(funkgui::Point p) const { return layout::kOverlay.contains(p); }

    void ModeBrowser::pointerMove(const funkgui::PointerEvent& e)
    {
        sync();
        const funkgui::Point p { e.x, e.y };
        hoverPager_ = pagerAt(p);
        hover_ = grid_.hit(page_, p);
        if (hover_ >= 0)
        {
            highlight_ = hover_;                                 // Return commits what the pointer shows
            keyboard_ = false;
        }
    }

    void ModeBrowser::pointerExit()
    {
        hover_ = -1;
        hoverPager_ = 0;
    }

    void ModeBrowser::pointerDown(const funkgui::PointerEvent& e)
    {
        sync();
        keyboard_ = false;
        pressed_ = -1;
        armed_ = false;
        if (e.popup)
            return;                                              // no menu, never a write
        const funkgui::Point p { e.x, e.y };
        if (const int dir = pagerAt(p); dir != 0)
        {
            if (pagerEnabled(dir))
                setPage(page_ + dir);
            return;
        }
        pressed_ = grid_.hit(page_, p);
        armed_ = pressed_ >= 0;
        pressAlt_ = e.mods.alt;
        if (pressed_ >= 0)
            highlight_ = pressed_;
    }

    void ModeBrowser::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (pressed_ >= 0 && !grid_.rowRect(pressed_).contains({ e.x, e.y }))
            armed_ = false;                                      // dragging off cancels
    }

    void ModeBrowser::pointerUp(const funkgui::PointerEvent& e)
    {
        const int item = pressed_;
        const bool alt = pressAlt_ || e.mods.alt;
        const bool release = armed_ && item >= 0 && isOpen() && grid_.pageOfItem(item) == page_
                          && grid_.rowRect(item).contains({ e.x, e.y });
        pressed_ = -1;
        armed_ = false;
        if (release)
            commit(item, alt, false);                            // a click keeps the browser open (ModeBrowser.h)
    }

    void ModeBrowser::doubleClick(const funkgui::PointerEvent& e)
    {
        sync();
        pressed_ = -1;
        armed_ = false;
        if (e.popup || !isOpen())
            return;
        if (const int item = grid_.hit(page_, { e.x, e.y }); item >= 0)
            commit(item, e.mods.alt, true);                      // switch (again: no write) and close
    }

    bool ModeBrowser::wheel(const funkgui::WheelEvent& e)
    {
        sync();
        if (grid_.pages() <= 1)
            return true;                                         // over the overlay nothing underneath scrolls
        const double now = ctx_.host != nullptr ? ctx_.host->nowSeconds() : ctx_.seconds;
        if (wheelLast_ < 0.0 || now - wheelLast_ >= funkgui::GestureController::kWheelIdle)
        {
            wheelAcc_ = 0.0f;
            wheelPaged_ = false;
        }
        wheelLast_ = now;
        // ADR-84: pages move with the content, so the delta keeps the system's direction (natural scrolling included);
        // `reversed` is only for value controls. A smooth burst (a swipe and its momentum) turns one page at most.
        const float v = e.dy != 0.0f ? e.dy : e.dx;
        int k = 0;
        if (std::isfinite(v))
        {
            if (!e.smooth)
                k = v > 0.0f ? 1 : (v < 0.0f ? -1 : 0);
            else if (!wheelPaged_)
            {
                constexpr float notch = funkgui::RuleSlider::kWheelNotch;
                wheelAcc_ = std::clamp(wheelAcc_ + v, -64.0f * notch, 64.0f * notch);
                k = static_cast<int>(wheelAcc_ / notch);
                k = std::clamp(k, -1, 1);
                if (k != 0)
                {
                    wheelAcc_ = 0.0f;
                    wheelPaged_ = true;
                }
            }
        }
        if (k != 0)
            setPage(page_ - k);                                  // scrolling down (away) goes forward
        return true;
    }

    bool ModeBrowser::key(const funkgui::KeyEvent& e)
    {
        if (!isOpen())
            return false;
        sync();
        followFocus();
        const bool typing = typedLen_ > 0 && ctx_.seconds - typedAt_ <= static_cast<double>(B::kTypeAheadS);
        if (const int dir = focusedPager(); dir != 0 && (e.key == funkgui::Key::enter || e.key == funkgui::Key::space))
        {
            if (pagerEnabled(dir))
                setPage(page_ + dir);                            // the focused pager button (S13 H1a)
            return true;
        }
        switch (e.key)
        {
            case funkgui::Key::up:
            case funkgui::Key::down:
            case funkgui::Key::left:
            case funkgui::Key::right:
            case funkgui::Key::home:
            case funkgui::Key::end:
            case funkgui::Key::pageUp:
            case funkgui::Key::pageDown:
                typedLen_ = 0;
                moveHighlight(grid_.step(highlight_, e.key));
                return true;
            case funkgui::Key::enter:
                commit(highlight_, e.mods.alt, true);
                return true;
            case funkgui::Key::space:
                if (typing)
                    typeChar(U' ');                              // "BUS 25": a space inside a pending buffer
                else
                    commit(highlight_, e.mods.alt, true);
                return true;
            case funkgui::Key::character:
                if (e.mods.cmd || e.mods.ctrl || e.ch < U' ' || e.ch > U'~')
                    return false;                                // host shortcuts; nothing a Mode name holds
                typeChar(e.ch);
                return true;
            case funkgui::Key::backspace:
            case funkgui::Key::del:
                if (typedLen_ > 0)
                    --typedLen_;
                return true;                                     // never a slot under the overlay
            case funkgui::Key::tab:
            case funkgui::Key::escape:
                return false;                                    // the Panel's
        }
        return false;
    }

    funkgui::Cursor ModeBrowser::cursor(funkgui::Point p) const
    {
        const int dir = pagerAt(p);
        if (grid_.hit(page_, p) >= 0 || (dir != 0 && pagerEnabled(dir)))
            return funkgui::Cursor::pointingHand;
        return funkgui::Cursor::normal;
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void ModeBrowser::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const int current = currentSlot();
        for (int k = 0; k < grid_.columns(); ++k)
        {
            const bool shownPage = ModeGrid::pageOf(k) == page_;
            const float x = B::columnX(k % B::kColumns);
            funkgui::A11yItem h;
            h.id = a11yId(ViewIndex::modeBrowser, kHeadingIdBase + static_cast<uint32_t>(k));
            h.role = funkgui::A11yRole::staticText;
            h.bounds = { x, B::kHeadingY + kHeadingHitDy, B::kRowHitW, kHeadingHitH };
            char heading[48];
            grid_.heading(k, heading, sizeof heading);
            h.title = heading;
            h.readOnly = true;
            h.visible = shownPage;
            out.push_back(std::move(h));

            const ModeGrid::Column& col = grid_.column(k);
            for (int row = 0; row < col.count; ++row)
            {
                const int i = col.first + row;
                funkgui::A11yItem it;
                it.id = rowId(grid_.slot(i));
                it.role = funkgui::A11yRole::listItem;
                it.bounds = grid_.rowRect(i);
                it.title = grid_.name(i);
                it.description = kGroupNames[static_cast<std::size_t>(static_cast<int>(grid_.group(i)))];
                it.help = std::string(grid_.spec(i));
                it.checkable = true;
                it.checked = grid_.slot(i) == current;
                it.visible = shownPage;
                out.push_back(std::move(it));
            }
        }
        if (grid_.pages() > 1)
        {
            funkgui::A11yItem prev;
            prev.id = a11yId(ViewIndex::modeBrowser, kPagerPrevId);
            prev.role = funkgui::A11yRole::button;
            prev.bounds = kPagerPrev;
            prev.title = "Previous page";
            prev.enabled = pagerEnabled(-1);
            out.push_back(std::move(prev));

            funkgui::A11yItem text;
            text.id = a11yId(ViewIndex::modeBrowser, kPagerTextId);
            text.role = funkgui::A11yRole::staticText;
            text.bounds = { kPagerPrev.right(), kPagerPrev.y, kPagerNext.x - kPagerPrev.right(), kPagerPrev.h };
            text.title = "Page";
            text.value = std::to_string(page_ + 1) + " of " + std::to_string(grid_.pages());
            text.readOnly = true;
            out.push_back(std::move(text));

            funkgui::A11yItem next;
            next.id = a11yId(ViewIndex::modeBrowser, kPagerNextId);
            next.role = funkgui::A11yRole::button;
            next.bounds = kPagerNext;
            next.title = "Next page";
            next.enabled = pagerEnabled(1);
            out.push_back(std::move(next));
        }
    }

    int ModeBrowser::focusOrder(std::span<uint32_t> out) const
    {
        std::size_t n = 0;
        const int first = page_ * B::kColumns;
        const int last = std::min(first + B::kColumns, grid_.columns());
        for (int k = first; k < last; ++k)
        {
            const ModeGrid::Column& col = grid_.column(k);
            for (int row = 0; row < col.count && n < out.size(); ++row)
                out[n++] = rowId(grid_.slot(col.first + row));
        }
        for (const int dir : { -1, 1 })
            if (grid_.pages() > 1 && pagerEnabled(dir) && n < out.size())
                out[n++] = a11yId(ViewIndex::modeBrowser, dir < 0 ? kPagerPrevId : kPagerNextId);
        return static_cast<int>(n);
    }

    uint32_t ModeBrowser::a11yRevision() const { return revision_; }

    void ModeBrowser::a11yAction(uint32_t id, funkgui::A11yAction a, double)
    {
        if (!isOpen())
            return;
        sync();
        const uint32_t local = id & 0xFFFFu;
        if (local == kPagerPrevId || local == kPagerNextId)
        {
            const int dir = local == kPagerPrevId ? -1 : 1;
            if ((a == funkgui::A11yAction::press || a == funkgui::A11yAction::toggle) && pagerEnabled(dir))
                setPage(page_ + dir);
            return;
        }
        if (local < kRowIdBase || local >= kRowIdBase + static_cast<uint32_t>(fcdsp::kModeCapacity))
            return;
        const int item = grid_.find(static_cast<int>(local - kRowIdBase));
        if (item < 0)
            return;
        switch (a)
        {
            case funkgui::A11yAction::press:
            case funkgui::A11yAction::toggle:    commit(item, false, false); break;
            case funkgui::A11yAction::focus:     moveHighlight(item); break;
            case funkgui::A11yAction::setValue:
            case funkgui::A11yAction::increment:
            case funkgui::A11yAction::decrement:
            case funkgui::A11yAction::showMenu:  break;
        }
    }
}
