// Source/editor/views/PresetBrowser.cpp — the preset browser (see PresetBrowser.h): filters, rows, save (P3c: over the
// current user preset), save as and rename (LineEdit), delete, import and export (juce::FileChooser), context menus
// (funkgui::MenuLook) over PresetAccess.
#include "editor/views/PresetBrowser.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/juce/MenuLook.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/text/TextFit.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        using funkgui::Rect;

        // ---- geometry (PresetBrowser.h), logical px ---------------------------------------------------------------------
        constexpr Rect  kGround { layout::kOverlay.x - 8.0f, layout::kOverlay.y - 4.0f, layout::kOverlay.w + 16.0f,
                                  layout::kOverlay.h + 8.0f };   // the Mode browser's ground (ModeBrowser.cpp)
        constexpr float kTopY = layout::browser::kHeadingY;     // 72: headings, the save-as line
        // the filter column
        constexpr float kFilterX = 40.0f;
        constexpr float kFilterRight = 170.0f;                  // counts right-aligned here
        constexpr float kFilterY0 = layout::browser::kRowY0;    // 92
        constexpr float kFilterPitch = 18.0f;
        constexpr float kFilterGap = 6.0f;                      // after USER
        constexpr int   kFixedFilters = 3;                      // ALL, FACTORY, USER
        constexpr int   kFilterRows = 12;                       // the three, then 9 categories
        constexpr float kFilterBarX = 34.0f;                    // the chosen filter's 2×8 bar
        constexpr Rect  kColumn { 32.0f, 88.0f, 144.0f, 224.0f };
        constexpr float kDividerX = 180.0f;
        // the list
        constexpr float kListX = 186.0f;                        // a row's rectangle ...
        constexpr float kListRight = layout::kContentRight;    // ... to x 920
        constexpr float kCurrentBarX = 190.0f;                  // 2×12 at the name's x − 6
        constexpr float kRowX = 196.0f;
        constexpr float kRowY0 = layout::browser::kRowY0;       // 92
        constexpr float kRowPitch = layout::browser::kRowPitch; // 20
        constexpr float kRowHitDy = layout::browser::kRowHitDy; // −4
        constexpr int   kRows = 11;
        constexpr float kNameMaxW = 300.0f;
        constexpr float kCategoryX = 520.0f;
        constexpr float kCategoryMaxW = 108.0f;
        constexpr float kModeX = 640.0f;
        constexpr float kModeMaxW = 150.0f;
        constexpr float kSourceRight = 908.0f;
        constexpr float kThumbX = 916.0f;
        constexpr float kTrackTop = kRowY0 + kRowHitDy;         // 88
        constexpr float kTrackH = kRowPitch * static_cast<float>(kRows);   // 220
        constexpr Rect  kList { kListX, kTrackTop, kListRight - kListX, kTrackH };
        // the bottom bar
        constexpr float kBarRuleY = 316.0f;
        constexpr float kBarTop = 324.0f;
        constexpr float kBarH = 16.0f;
        constexpr float kBarTextY = 328.0f;
        constexpr float kCellPad = 14.0f;                       // the display row's cells: text + 14, even
        constexpr float kCellGap = 4.0f;
        // the save-as line and the rename field
        constexpr float kEntryLabelX = kRowX;
        constexpr float kFieldX = 262.0f;
        constexpr float kFieldW = 340.0f;                       // 40 characters of kLabel (8.2 px each)
        constexpr float kEntryCentreY = 76.0f;
        constexpr float kEntryRuleY = 86.0f;
        constexpr float kEntryCategoryX = 618.0f;
        constexpr float kEntryCategoryMaxW = 180.0f;
        constexpr float kRenameW = 400.0f;                      // the renamed row's name: x 196–596
        constexpr float kRenameRuleDy = 13.0f;

        constexpr int    kMaxName = 40;                         // characters (HR kMaxName)
        constexpr double kMessageS = 3.0;
        constexpr double kArmS = 3.0;
        constexpr double kTypeAheadS = static_cast<double>(layout::browser::kTypeAheadS);
        constexpr float  kWheelRowsDiscrete = 3.0f;             // HR PresetPanel::mouseWheel
        constexpr float  kWheelRowsSmooth = 12.0f;

        constexpr const char* kSep = " \xC2\xB7 ";              // " · "

        float rowY(int visibleRow) noexcept { return kRowY0 + kRowPitch * static_cast<float>(visibleRow); }

        char upperAscii(char ch) noexcept { return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch; }
        char lowerAscii(char ch) noexcept { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

        std::string upper(std::string_view s)
        {
            std::string u(s);
            for (char& ch : u)
                ch = upperAscii(ch);
            return u;
        }

        std::string lower(std::string_view s)
        {
            std::string u(s);
            for (char& ch : u)
                ch = lowerAscii(ch);
            return u;
        }

        std::string_view trimmed(std::string_view s) noexcept
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

        bool sameNoCase(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (lowerAscii(a[i]) != lowerAscii(b[i]))
                    return false;
            return true;
        }

        bool endsWithNoCase(std::string_view s, std::string_view tail) noexcept
        {
            return s.size() >= tail.size() && sameNoCase(s.substr(s.size() - tail.size()), tail);
        }

        // Upper case into a fixed buffer (the edit's text, drawn every frame without allocating).
        void upperInto(std::string_view s, char* out, std::size_t cap) noexcept
        {
            if (cap == 0)
                return;
            const std::size_t n = std::min(s.size(), cap - 1);
            for (std::size_t i = 0; i < n; ++i)
                out[i] = upperAscii(s[i]);
            out[n] = '\0';
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
                            --n;
                        break;
                    }
                    out[n++] = ch;
                }
                out[n] = '\0';
            }
            void add(const char* s) noexcept { add(std::string_view(s != nullptr ? s : "")); }
            void add(int v) noexcept
            {
                char digits[16];
                std::snprintf(digits, sizeof digits, "%d", v);
                add(std::string_view(digits));
            }
        };

        std::string modeName(std::string_view key)
        {
            if (key.empty())
                return {};
            const fcdsp::ModeSlot* s = fcdsp::resolveKey(key);
            if (s == nullptr || s->entry == nullptr || s->entry->desc == nullptr)
                return {};
            return std::string(s->entry->desc->name);
        }

        bool isInit(std::string_view name) noexcept { return sameNoCase(trimmed(name), "init"); }

        std::string fileNameOf(std::string_view path)
        {
            const std::size_t slash = path.rfind('/');
            return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
        }

        uint32_t local(uint32_t id) noexcept { return id & 0xFFFFu; }
    }

    // ---- construction ---------------------------------------------------------------------------------------------------

    PresetBrowser::PresetBrowser(PanelContext& ctx) : ctx_(ctx)
    {
        // The action cells: kCaption text + 14 px, even, 4 px apart, right-aligned to x 920 (the display row's cell rule).
        const auto width = [&](std::initializer_list<const char*> labels) {
            float w = 0.0f;
            for (const char* l : labels)
                w = std::max(w, funkgui::text::width(ctx_.atlas, l, T::kCaption));
            return 2.0f * std::ceil((w + kCellPad) * 0.5f);
        };
        browseCells_ = { { { Action::save, "SAVE", { 0.0f, kBarTop, width({ "SAVE" }), kBarH } },
                           { Action::saveAs, "SAVE AS", { 0.0f, kBarTop, width({ "SAVE AS" }), kBarH } },
                           { Action::rename, "RENAME", { 0.0f, kBarTop, width({ "RENAME" }), kBarH } },
                           { Action::remove, "DELETE", { 0.0f, kBarTop, width({ "DELETE", "CONFIRM" }), kBarH } },
                           { Action::importFiles, "IMPORT", { 0.0f, kBarTop, width({ "IMPORT" }), kBarH } },
                           { Action::exportFile, "EXPORT", { 0.0f, kBarTop, width({ "EXPORT" }), kBarH } } } };
        editCells_ = { { { Action::commit, "SAVE", { 0.0f, kBarTop, width({ "SAVE", "RENAME" }), kBarH } },
                         { Action::cancel, "CANCEL", { 0.0f, kBarTop, width({ "CANCEL" }), kBarH } } } };
        const auto place = [](std::span<Cell> row) {
            float x = kListRight;
            for (std::size_t i = row.size(); i-- > 0;)
            {
                x -= row[i].rect.w;
                row[i].rect.x = x;
                x -= kCellGap;
            }
        };
        place(browseCells_);
        place(editCells_);
        refresh(true);
    }

    PresetBrowser::~PresetBrowser()
    {
        alive_.reset();                                          // a chooser or menu callback still queued does nothing
        if (menuLook_ != nullptr)
            juce::PopupMenu::dismissAllActiveMenus();            // an open menu holds a pointer to its look (HR)
    }

    // ---- the model ------------------------------------------------------------------------------------------------------

    bool PresetBrowser::isOpen() const noexcept { return ctx_.overlay == Overlay::presetBrowser; }

    void PresetBrowser::sync()
    {
        if (needsReset_ && isOpen())
            reset();
        refresh();
    }

    void PresetBrowser::reset()
    {
        // A fresh opening: nothing typed, armed or pressed; the selection on the current preset when the chosen filter
        // shows it. The chosen filter itself stays for the editor's life.
        needsReset_ = false;
        tickedOpen_ = false;
        lastTick_ = ctx_.seconds;                                // the next tick is not a gap
        edit_ = Edit::none;
        line_ = {};
        editUuid_.clear();
        armedUuid_.clear();
        message_.clear();
        messageUntil_ = -1.0;
        hover_ = {};
        pressed_ = {};
        armed_ = false;
        typedLen_ = 0;
        wheelAcc_ = 0.0f;
        refresh(true);
        const int cur = entryOf(currentUuid_);
        select(cur >= 0 && shownOf(cur) >= 0 ? cur : -1, true);
        ++revision_;
    }

    void PresetBrowser::refresh(bool force)
    {
        PresetAccess& pa = ctx_.facade.presets();
        const uint32_t rev = pa.revision();
        if (valid_ && rev == seenRev_ && !force)
            return;
        seenRev_ = rev;
        valid_ = true;

        const int n = std::max(0, pa.count());
        entries_.clear();
        entries_.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const PresetAccess::Row r = pa.row(i);
            Entry e;
            e.index = i;
            e.uuid = r.uuid;
            e.name = funkgui::text::printable(trimmed(r.name));
            e.category = funkgui::text::printable(trimmed(r.category));
            e.shownName = upper(e.name);
            e.shownCategory = upper(e.category);
            e.categoryKey = lower(e.category);
            e.mode = modeName(r.modeKey);
            e.factory = r.factory;
            entries_.push_back(std::move(e));
        }
        const int cur = pa.current();
        current_ = cur >= 0 && cur < n ? cur : -1;
        modified_ = pa.modified();
        currentUuid_ = current_ >= 0 ? entries_[static_cast<std::size_t>(current_)].uuid : std::string();

        // The filters: ALL, FACTORY, USER, then the categories (case-insensitive, sorted by their upper-case spelling).
        filters_.clear();
        int factory = 0;
        for (const Entry& e : entries_)
            factory += e.factory ? 1 : 0;
        filters_.push_back({ Filter::Kind::all, "ALL", {}, n });
        filters_.push_back({ Filter::Kind::factory, "FACTORY", {}, factory });
        filters_.push_back({ Filter::Kind::user, "USER", {}, n - factory });
        std::vector<Filter> cats;
        for (const Entry& e : entries_)
        {
            if (e.categoryKey.empty())
                continue;
            auto it = std::find_if(cats.begin(), cats.end(), [&](const Filter& f) { return f.key == e.categoryKey; });
            if (it == cats.end())
                cats.push_back({ Filter::Kind::category, e.shownCategory, e.categoryKey, 1 });
            else
                ++it->count;
        }
        std::sort(cats.begin(), cats.end(), [](const Filter& a, const Filter& b) { return a.label < b.label; });
        for (Filter& f : cats)
            filters_.push_back(std::move(f));

        // The chosen filter, by identity; a category that emptied falls back to ALL.
        filter_ = 0;
        for (std::size_t f = 0; f < filters_.size(); ++f)
            if (filters_[f].kind == filterKind_ && filters_[f].key == filterKey_)
                filter_ = static_cast<int>(f);
        filterKind_ = filters_[static_cast<std::size_t>(filter_)].kind;
        filterKey_ = filters_[static_cast<std::size_t>(filter_)].key;
        filterScroll_ = std::clamp(filterScroll_, 0,
                                   std::max(0, static_cast<int>(filters_.size()) - kFilterRows));
        rebuildShown();

        selected_ = entryOf(selectedUuid_);
        if (selected_ < 0)
            selectedUuid_.clear();
        if (edit_ == Edit::rename && entryOf(editUuid_) < 0)
        {
            edit_ = Edit::none;                                  // renamed away by another process
            line_ = {};
            editUuid_.clear();
        }
        if (!armedUuid_.empty() && entryOf(armedUuid_) < 0)
            armedUuid_.clear();
        if (edit_ != Edit::none)
            noteEdit();                                          // the names the typed one is checked against moved
        ++revision_;
    }

    bool PresetBrowser::inFilter(const Entry& e, const Filter& f) const noexcept
    {
        switch (f.kind)
        {
            case Filter::Kind::all:      return true;
            case Filter::Kind::factory:  return e.factory;
            case Filter::Kind::user:     return !e.factory;
            case Filter::Kind::category: return e.categoryKey == f.key;
        }
        return true;
    }

    void PresetBrowser::rebuildShown()
    {
        shown_.clear();
        const Filter& f = filters_[static_cast<std::size_t>(filter_)];
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (inFilter(entries_[i], f))
                shown_.push_back(static_cast<int>(i));
        scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(shown_.size()) - kRows));
    }

    int PresetBrowser::entryOf(std::string_view uuid) const noexcept
    {
        if (uuid.empty())
            return -1;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].uuid == uuid)
                return static_cast<int>(i);
        return -1;
    }

    int PresetBrowser::indexOf(std::string_view uuid)
    {
        const std::string key(uuid);                             // refresh() may rebuild what uuid points into
        refresh();
        const int e = entryOf(key);
        return e >= 0 ? entries_[static_cast<std::size_t>(e)].index : -1;
    }

    int PresetBrowser::shownOf(int entry) const noexcept
    {
        for (std::size_t s = 0; s < shown_.size(); ++s)
            if (shown_[s] == entry)
                return static_cast<int>(s);
        return -1;
    }

    const PresetBrowser::Entry* PresetBrowser::selectedEntry() const noexcept
    {
        return selected_ >= 0 && shownOf(selected_) >= 0 ? &entries_[static_cast<std::size_t>(selected_)] : nullptr;
    }

    void PresetBrowser::select(int entry, bool scrollTo)
    {
        selected_ = entry >= 0 && entry < static_cast<int>(entries_.size()) ? entry : -1;
        selectedUuid_ = selected_ >= 0 ? entries_[static_cast<std::size_t>(selected_)].uuid : std::string();
        if (!armedUuid_.empty() && armedUuid_ != selectedUuid_)
            armedUuid_.clear();                                  // the confirmation belongs to the row it armed
        if (scrollTo)
            if (const int s = shownOf(selected_); s >= 0)
            {
                if (s < scroll_)
                    scroll_ = s;
                else if (s >= scroll_ + kRows)
                    scroll_ = s - kRows + 1;
            }
        ++revision_;
    }

    void PresetBrowser::setFilter(int filter)
    {
        filter = std::clamp(filter, 0, static_cast<int>(filters_.size()) - 1);
        if (filter == filter_)
            return;
        filter_ = filter;
        filterKind_ = filters_[static_cast<std::size_t>(filter)].kind;
        filterKey_ = filters_[static_cast<std::size_t>(filter)].key;
        scroll_ = 0;
        rebuildShown();
        if (shownOf(selected_) >= 0)
            select(selected_, true);
        if (filter >= kFixedFilters)                             // keep the chosen category in view
        {
            const int k = filter - kFixedFilters;
            const int rows = kFilterRows - kFixedFilters;
            if (k < filterScroll_)
                filterScroll_ = k;
            else if (k >= filterScroll_ + rows)
                filterScroll_ = k - rows + 1;
        }
        typedLen_ = 0;
        ++revision_;
    }

    void PresetBrowser::scrollBy(int rows)
    {
        const int to = std::clamp(scroll_ + rows, 0, std::max(0, static_cast<int>(shown_.size()) - kRows));
        if (to != scroll_)
        {
            scroll_ = to;
            ++revision_;
        }
    }

    void PresetBrowser::scrollFilters(int rows)
    {
        const int to = std::clamp(filterScroll_ + rows, 0, std::max(0, static_cast<int>(filters_.size()) - kFilterRows));
        if (to != filterScroll_)
        {
            filterScroll_ = to;
            ++revision_;
        }
    }

    // ---- tick -----------------------------------------------------------------------------------------------------------

    void PresetBrowser::tick(float dt)
    {
        // The Panel ticks the browser only while it is shown: a missed frame means it was hidden in between (a probe's
        // instant setView), so this showing is a fresh opening (ModeBrowser's rule).
        const bool gap = lastTick_ < 0.0 || (dt > 0.0f && ctx_.seconds - lastTick_ > 1.5 * static_cast<double>(dt));
        lastTick_ = ctx_.seconds;
        if (!isOpen())
        {
            needsReset_ = true;                                  // fading out: whatever was typed is dropped
            cancelEdit();
            armedUuid_.clear();
            hover_ = {};
            return;
        }
        if (gap)
            needsReset_ = true;
        sync();
        tickedOpen_ = true;
        if (typedLen_ > 0 && ctx_.seconds - typedAt_ > kTypeAheadS)
            typedLen_ = 0;
        if (!armedUuid_.empty() && ctx_.seconds > armedUntil_)
        {
            armedUuid_.clear();
            ++revision_;
        }
        hover_ = ctx_.pointerIn && layout::kOverlay.contains(ctx_.pointer) ? hitAt(ctx_.pointer) : Hit{};

        // The item under the hand, offered as the strongest kind so it outranks the strip or the latch underneath
        // (ModeBrowser's rule): the hovered row, filter or action.
        if (hover_.zone == Zone::row || hover_.zone == Zone::filter || hover_.zone == Zone::action
            || hover_.zone == Zone::field || hover_.zone == Zone::category)
        {
            rebuildSpec(hover_);
            if (spec_[0] != '\0')
            {
                uint32_t id = a11yId(ViewIndex::presetBrowser, kStatusLocal);
                if (hover_.zone == Zone::row)
                    id = a11yId(ViewIndex::presetBrowser,
                                kRowLocal0 + static_cast<uint32_t>(entries_[static_cast<std::size_t>(
                                                 shown_[static_cast<std::size_t>(hover_.index)])].index));
                ctx_.offerHand(fcdsp::kNoPid, HandKind::drag, id, spec_);
            }
        }
    }

    void PresetBrowser::rebuildSpec(Hit h)
    {
        Line l{ spec_, sizeof spec_ };
        const Entry* sel = selectedEntry();
        switch (h.zone)
        {
            case Zone::row:
                l.add("CLICK: LOAD   DOUBLE-CLICK: LOAD AND CLOSE   RIGHT-CLICK: MORE");
                break;
            case Zone::filter:
            {
                const Filter& f = filters_[static_cast<std::size_t>(h.index)];
                switch (f.kind)
                {
                    case Filter::Kind::all:      l.add("SHOW EVERY PRESET"); break;
                    case Filter::Kind::factory:  l.add("SHOW THE FACTORY PRESETS"); break;
                    case Filter::Kind::user:     l.add("SHOW YOUR PRESETS"); break;
                    case Filter::Kind::category:
                        l.add("SHOW THE ");
                        l.add(f.label);
                        l.add(" PRESETS");
                        break;
                }
                break;
            }
            case Zone::action:
                switch (cells()[static_cast<std::size_t>(h.index)].action)
                {
                    case Action::save:
                        if (const int cur = entryOf(currentUuid_);
                            cur >= 0 && !entries_[static_cast<std::size_t>(cur)].factory)
                        {
                            l.add("SAVE OVER '");
                            l.add(entries_[static_cast<std::size_t>(cur)].shownName);
                            l.add("'");
                        }
                        else
                            l.add("SAVE THE CURRENT SOUND AS A NEW PRESET");
                        break;
                    case Action::saveAs:      l.add("SAVE THE CURRENT SOUND AS A NEW PRESET"); break;
                    case Action::rename:
                        if (sel != nullptr && !sel->factory)
                        {
                            l.add("RENAME '");
                            l.add(sel->shownName);
                            l.add("'");
                        }
                        else
                            l.add(sel != nullptr ? "FACTORY PRESETS CANNOT BE RENAMED" : "SELECT ONE OF YOUR PRESETS");
                        break;
                    case Action::remove:
                        if (sel != nullptr && !sel->factory)
                        {
                            l.add("DELETE '");
                            l.add(sel->shownName);
                            l.add("'   CLICK TWICE");
                        }
                        else
                            l.add(sel != nullptr ? "FACTORY PRESETS CANNOT BE DELETED" : "SELECT ONE OF YOUR PRESETS");
                        break;
                    case Action::importFiles: l.add("IMPORT .FCMPPRESET FILES   OR DROP THEM ON THE PLUGIN"); break;
                    case Action::exportFile:
                        if (sel != nullptr)
                        {
                            l.add("EXPORT '");
                            l.add(sel->shownName);
                            l.add("' TO A FILE");
                        }
                        else
                            l.add("SELECT A PRESET TO EXPORT");
                        break;
                    case Action::commit:      l.add(edit_ == Edit::rename ? "RENAME   RETURN" : "SAVE   RETURN"); break;
                    case Action::cancel:      l.add("CANCEL   ESC"); break;
                }
                break;
            case Zone::field:
                l.add(edit_ == Edit::rename ? "TYPE THE NEW NAME   RETURN RENAMES   ESC CANCELS"
                                            : "TYPE A NAME   RETURN SAVES   ESC CANCELS");
                break;
            case Zone::category:
                l.add("THE CATEGORY IT IS FILED UNDER   CLICK TO CHANGE");
                break;
            case Zone::none:
            case Zone::list:
            case Zone::column:
            case Zone::body:
                break;
        }
    }

    // ---- actions --------------------------------------------------------------------------------------------------------

    void PresetBrowser::load(int entry, bool closeAfter)
    {
        if (entry >= 0 && entry < static_cast<int>(entries_.size()))
        {
            const std::string uuid = entries_[static_cast<std::size_t>(entry)].uuid;
            if (uuid != currentUuid_ || modified_)                // current and unmodified: nothing to load
            {
                if (const int index = indexOf(uuid); index >= 0)
                {
                    ctx_.facade.presets().apply(index);          // one call: it brackets its own batch (01 §9.2)
                    refresh();
                }
                else
                {
                    flash("THAT PRESET IS GONE");
                }
            }
            select(entryOf(uuid), true);
        }
        if (closeAfter)
            close();
    }

    void PresetBrowser::moveSelection(int to)
    {
        if (shown_.empty())
            return;
        to = std::clamp(to, 0, static_cast<int>(shown_.size()) - 1);
        if (to == shownOf(selected_))
            return;
        const int entry = shown_[static_cast<std::size_t>(to)];
        select(entry, true);
        load(entry, false);                                      // HR: arrowing auditions
    }

    void PresetBrowser::beginSaveAs()
    {
        cancelEdit();
        armedUuid_.clear();
        typedLen_ = 0;
        edit_ = Edit::saveAs;
        closeAfterSave_ = !tickedOpen_;                          // opened for this save (the strip's SAVE)
        const int cur = entryOf(currentUuid_);
        const Entry* c = cur >= 0 ? &entries_[static_cast<std::size_t>(cur)] : nullptr;
        line_.set(c != nullptr && !isInit(c->name) ? std::string_view(c->name) : std::string_view(), kMaxName);
        const Filter& f = filters_[static_cast<std::size_t>(filter_)];
        saveCategory_.clear();
        if (f.kind == Filter::Kind::category)
        {
            for (const Entry& e : entries_)
                if (e.categoryKey == f.key)
                {
                    saveCategory_ = e.category;                  // the list's own spelling
                    break;
                }
        }
        else if (c != nullptr && !isInit(c->category))
        {
            saveCategory_ = c->category;
        }
        noteEdit();
    }

    void PresetBrowser::saveCurrent()
    {
        // P3c (PresetBrowser.h): over the current user preset, one call and no dialog, whichever row is selected; a
        // factory preset or none is a save as; a refused overwrite says so and starts a save as.
        refresh();
        const int cur = entryOf(currentUuid_);
        if (cur < 0 || entries_[static_cast<std::size_t>(cur)].factory)
        {
            beginSaveAs();
            return;
        }
        cancelEdit();
        armedUuid_.clear();
        typedLen_ = 0;
        const std::string uuid = currentUuid_;
        const std::string shown = entries_[static_cast<std::size_t>(cur)].shownName;
        const int index = indexOf(uuid);
        if (index < 0 || !ctx_.facade.presets().overwrite(index))
        {
            beginSaveAs();                                       // the sound is kept as a new preset instead
            flash("COULD NOT SAVE OVER '" + shown + "'");
            return;
        }
        refresh(true);
        flash("SAVED '" + shown + "'");
    }

    void PresetBrowser::beginRename(int entry)
    {
        if (entry < 0 || entries_[static_cast<std::size_t>(entry)].factory)
            return;
        cancelEdit();
        armedUuid_.clear();
        typedLen_ = 0;
        select(entry, true);
        edit_ = Edit::rename;
        editUuid_ = entries_[static_cast<std::size_t>(entry)].uuid;
        line_.set(entries_[static_cast<std::size_t>(entry)].name, kMaxName);
        noteEdit();
    }

    void PresetBrowser::cancelEdit()
    {
        if (edit_ == Edit::none)
            return;
        edit_ = Edit::none;
        line_ = {};
        editUuid_.clear();
        ++revision_;
    }

    bool PresetBrowser::editValid() const
    {
        return !trimmed(line_.buffer).empty() && !(edit_ == Edit::rename && editTaken_);
    }

    void PresetBrowser::commitEdit()
    {
        const std::string name(trimmed(line_.buffer));
        if (name.empty())
        {
            flash("TYPE A NAME FIRST");
            return;
        }
        PresetAccess& pa = ctx_.facade.presets();
        if (edit_ == Edit::saveAs)
        {
            if (!pa.saveAs(name, saveCategory_))
            {
                flash("COULD NOT SAVE THE PRESET");              // the name stays for another try
                return;
            }
            edit_ = Edit::none;
            line_ = {};
            refresh(true);
            const int cur = entryOf(currentUuid_);
            if (cur >= 0 && shownOf(cur) < 0)
                setFilter(0);                                    // the new preset is shown where it went
            select(cur, true);
            flash("SAVED '" + (cur >= 0 ? entries_[static_cast<std::size_t>(cur)].shownName : upper(name)) + "'");
            if (closeAfterSave_)
                close();
            return;
        }
        if (edit_ == Edit::rename)
        {
            const std::string uuid = editUuid_;
            const int e = entryOf(uuid);
            if (e >= 0 && entries_[static_cast<std::size_t>(e)].name == name)
            {
                cancelEdit();                                    // unchanged
                return;
            }
            if (nameTaken(name, uuid))
            {
                flash("'" + upper(name) + "' IS TAKEN");
                return;
            }
            const int index = indexOf(uuid);
            if (index < 0)
            {
                cancelEdit();
                flash("THAT PRESET IS GONE");
                return;
            }
            if (!pa.rename(index, name))
            {
                flash("COULD NOT RENAME THE PRESET");
                return;
            }
            edit_ = Edit::none;
            line_ = {};
            editUuid_.clear();
            refresh(true);
            select(entryOf(uuid), true);
            flash("RENAMED TO '" + upper(name) + "'");
        }
    }

    void PresetBrowser::armOrRemove(int entry)
    {
        if (entry < 0 || shownOf(entry) < 0)
            return;
        const Entry& e = entries_[static_cast<std::size_t>(entry)];
        if (e.factory)
        {
            flash("FACTORY PRESETS CANNOT BE DELETED");
            return;
        }
        if (armedUuid_ == e.uuid && ctx_.seconds <= armedUntil_)
        {
            removeEntry(entry);
            return;
        }
        armedUuid_ = e.uuid;
        armedUntil_ = ctx_.seconds + kArmS;
        ++revision_;
    }

    void PresetBrowser::removeEntry(int entry)
    {
        if (entry < 0 || entries_[static_cast<std::size_t>(entry)].factory)
            return;
        const std::string uuid = entries_[static_cast<std::size_t>(entry)].uuid;
        const std::string shown = entries_[static_cast<std::size_t>(entry)].shownName;
        const int at = shownOf(entry);
        armedUuid_.clear();
        const int index = indexOf(uuid);
        if (index < 0)
        {
            flash("THAT PRESET IS GONE");
            return;
        }
        if (!ctx_.facade.presets().remove(index))
        {
            flash("COULD NOT DELETE '" + shown + "'");
            return;
        }
        refresh(true);
        if (shown_.empty())
            select(-1, false);
        else
            select(shown_[static_cast<std::size_t>(std::clamp(at, 0, static_cast<int>(shown_.size()) - 1))], true);
        flash("DELETED '" + shown + "'");
    }

    bool PresetBrowser::filesInterest(const std::vector<std::string>& paths) const
    {
        return std::any_of(paths.begin(), paths.end(),
                           [](const std::string& p) { return endsWithNoCase(p, kFileExtension); });
    }

    void PresetBrowser::filesDropped(const std::vector<std::string>& paths) { importFiles(paths); }

    void PresetBrowser::importFiles(const std::vector<std::string>& paths)
    {
        std::vector<std::string> files;
        for (const std::string& p : paths)
            if (endsWithNoCase(p, kFileExtension))
                files.push_back(p);
        if (files.empty())
            return;
        if (!isOpen())                                           // the result is seen in the browser
            ctx_.panel.setView({ nullptr, ctx_.screen, ctx_.scTab, Overlay::presetBrowser }, false);
        sync();                                                  // a fresh opening resets now, before the result
        cancelEdit();

        std::vector<std::string> before;
        before.reserve(entries_.size());
        for (const Entry& e : entries_)
            before.push_back(e.uuid);
        PresetAccess& pa = ctx_.facade.presets();
        int ok = 0, failed = 0;
        std::string failedName;
        for (const std::string& f : files)
        {
            if (pa.importFile(f))
                ++ok;
            else
            {
                ++failed;
                failedName = fileNameOf(f);
            }
        }
        refresh(true);
        int first = -1;
        int fresh = 0;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (std::find(before.begin(), before.end(), entries_[i].uuid) == before.end())
            {
                ++fresh;
                if (first < 0)
                    first = static_cast<int>(i);
            }
        if (first >= 0)
        {
            if (shownOf(first) < 0)
                setFilter(0);
            select(first, true);
            if (files.size() == 1)
                load(first, false);                              // HR: one file is a request to hear it
        }

        char msg[256];
        Line l{ msg, sizeof msg };
        if (ok > 0 && fresh == 0)
            l.add(ok == 1 ? "THAT PRESET IS ALREADY HERE" : "THOSE PRESETS ARE ALREADY HERE");
        else if (fresh == 1 && first >= 0)
        {
            l.add("IMPORTED '");
            l.add(entries_[static_cast<std::size_t>(first)].shownName);
            l.add("'");
        }
        else if (fresh > 1)
        {
            l.add("IMPORTED ");
            l.add(fresh);
            l.add(" PRESETS");
        }
        if (failed > 0)
        {
            if (l.n > 0)
                l.add(kSep);
            if (failed == 1)
            {
                l.add("COULD NOT IMPORT '");
                l.add(upper(funkgui::text::printable(failedName)));
                l.add("'");
            }
            else
            {
                l.add("COULD NOT IMPORT ");
                l.add(failed);
                l.add(" FILES");
            }
        }
        flash(msg);
        if (ctx_.host != nullptr)
            ctx_.host->nudgeFullRate();
    }

    bool PresetBrowser::exportTo(int index, std::string_view path)
    {
        refresh();
        const bool ok = index >= 0 && !path.empty() && ctx_.facade.presets().exportFile(index, path);
        const std::string file = upper(funkgui::text::printable(fileNameOf(path)));
        flash(ok ? "EXPORTED '" + file + "'" : "COULD NOT EXPORT '" + file + "'");
        return ok;
    }

    void PresetBrowser::chooseImport()
    {
        juce::Component* owner = ctx_.host != nullptr ? ctx_.host->ownerComponent() : nullptr;
        if (owner == nullptr)
            return;                                              // headless: no window to parent a chooser
        chooser_ = std::make_unique<juce::FileChooser>(
            "Import presets", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            juce::String("*") + kFileExtension, true, false, owner);
        const std::weak_ptr<int> alive = alive_;
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, alive](const juce::FileChooser& fc) {
                                  if (alive.expired())
                                      return;
                                  std::vector<std::string> paths;
                                  for (const juce::File& f : fc.getResults())
                                      paths.push_back(f.getFullPathName().toStdString());
                                  importFiles(paths);
                              });
    }

    void PresetBrowser::chooseExport(int entry)
    {
        juce::Component* owner = ctx_.host != nullptr ? ctx_.host->ownerComponent() : nullptr;
        if (owner == nullptr || entry < 0)
            return;
        const Entry& e = entries_[static_cast<std::size_t>(entry)];
        const juce::String ext(kFileExtension);
        const juce::File initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                       .getChildFile(juce::File::createLegalFileName(juce::String(e.name)) + ext);
        chooser_ = std::make_unique<juce::FileChooser>("Export preset", initial, "*" + ext, true, false, owner);
        const std::weak_ptr<int> alive = alive_;
        const std::string uuid = e.uuid;
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, alive, uuid, ext](const juce::FileChooser& fc) {
                                  if (alive.expired())
                                      return;
                                  juce::File f = fc.getResult();
                                  if (f == juce::File())
                                      return;                     // cancelled
                                  if (!f.hasFileExtension(ext))
                                      f = f.withFileExtension(ext);
                                  const int index = indexOf(uuid);
                                  if (index < 0)
                                      flash("THAT PRESET IS GONE");
                                  else
                                      exportTo(index, f.getFullPathName().toStdString());
                                  if (ctx_.host != nullptr)
                                      ctx_.host->nudgeFullRate();
                              });
    }

    int PresetBrowser::menu(int index, std::span<MenuItem> out) const
    {
        const Entry* e = nullptr;
        for (const Entry& x : entries_)
            if (x.index == index)
                e = &x;
        std::size_t n = 0;
        const auto add = [&](Command c, const char* label, bool enabled, bool separator) {
            if (n < out.size())
                out[n++] = { c, label, enabled, separator };
        };
        if (e != nullptr)
            add(Command::load, "Load", true, false);
        add(Command::saveAs, "Save As...", true, e != nullptr);
        if (e != nullptr)
        {
            add(Command::rename, "Rename...", !e->factory, false);
            add(Command::exportFile, "Export...", true, false);
        }
        add(Command::importFiles, "Import...", true, false);
        if (e != nullptr)
            add(Command::remove, "Delete", !e->factory, true);
        return static_cast<int>(n);
    }

    void PresetBrowser::run(Command command, int index)
    {
        refresh();
        int entry = -1;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].index == index)
                entry = static_cast<int>(i);
        const auto open = [&] {
            if (!isOpen())
            {
                ctx_.panel.setView({ nullptr, ctx_.screen, ctx_.scTab, Overlay::presetBrowser }, false);
                sync();
            }
        };
        switch (command)
        {
            case Command::load:
                if (entry >= 0)
                    load(entry, false);
                break;
            case Command::saveAs:
                open();
                beginSaveAs();
                break;
            case Command::rename:
                if (entry >= 0 && !entries_[static_cast<std::size_t>(entry)].factory)
                {
                    open();
                    if (shownOf(entry) < 0)
                        setFilter(0);
                    beginRename(entry);
                }
                break;
            case Command::exportFile:
                if (entry >= 0)
                    chooseExport(entry);
                break;
            case Command::importFiles:
                chooseImport();
                break;
            case Command::remove:
                if (entry >= 0)
                    removeEntry(entry);                          // the menu choice is the confirmation (HR)
                break;
        }
    }

    void PresetBrowser::showMenu(int entry, funkgui::Rect anchor)
    {
        juce::Component* owner = ctx_.host != nullptr ? ctx_.host->ownerComponent() : nullptr;
        if (owner == nullptr)
            return;
        if (menuLook_ == nullptr)
            menuLook_ = std::make_unique<funkgui::MenuLook>(funkgui::Theme::byIndex(ctx_.host->themeIndex()));
        menuLook_->setTheme(funkgui::Theme::byIndex(ctx_.host->themeIndex()));

        std::array<MenuItem, 8> items{};
        const int index = entry >= 0 ? entries_[static_cast<std::size_t>(entry)].index : -1;
        const int n = menu(index, items);
        juce::PopupMenu m;
        m.setLookAndFeel(menuLook_.get());
        for (int i = 0; i < n; ++i)
        {
            const MenuItem& it = items[static_cast<std::size_t>(i)];
            if (it.separatorBefore)
                m.addSeparator();
            m.addItem(static_cast<int>(it.command), it.label, it.enabled);
        }
        const float s = static_cast<float>(owner->getWidth()) / static_cast<float>(layout::kWidth);   // the UI zoom
        const juce::Rectangle<int> area = owner->localAreaToGlobal(
            juce::Rectangle<float>(anchor.x * s, anchor.y * s, anchor.w * s, anchor.h * s).toNearestInt());
        const std::weak_ptr<int> alive = alive_;
        const std::string uuid = entry >= 0 ? entries_[static_cast<std::size_t>(entry)].uuid : std::string();
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(owner).withTargetScreenArea(area),
                        [this, alive, uuid](int id) {
                            if (alive.expired() || id <= 0)
                                return;
                            const int at = uuid.empty() ? -1 : indexOf(uuid);
                            if (!uuid.empty() && at < 0)
                                return;                          // the row went while the menu was open
                            run(static_cast<Command>(id), at);
                            if (ctx_.host != nullptr)
                                ctx_.host->nudgeFullRate();
                        });
    }

    void PresetBrowser::showCategoryMenu()
    {
        juce::Component* owner = ctx_.host != nullptr ? ctx_.host->ownerComponent() : nullptr;
        if (owner == nullptr || edit_ != Edit::saveAs)
            return;
        if (menuLook_ == nullptr)
            menuLook_ = std::make_unique<funkgui::MenuLook>(funkgui::Theme::byIndex(ctx_.host->themeIndex()));
        menuLook_->setTheme(funkgui::Theme::byIndex(ctx_.host->themeIndex()));

        std::vector<std::string> names;                          // the list's own spellings, in filter order
        for (const Filter& f : filters_)
            if (f.kind == Filter::Kind::category)
                for (const Entry& e : entries_)
                    if (e.categoryKey == f.key)
                    {
                        names.push_back(e.category);
                        break;
                    }
        juce::PopupMenu m;
        m.setLookAndFeel(menuLook_.get());
        m.addItem(1, "No Category", true, saveCategory_.empty());
        if (!names.empty())
            m.addSeparator();
        for (std::size_t i = 0; i < names.size(); ++i)
            m.addItem(100 + static_cast<int>(i), juce::String(names[i]), true, sameNoCase(names[i], saveCategory_));
        const float s = static_cast<float>(owner->getWidth()) / static_cast<float>(layout::kWidth);
        const Rect r { kEntryCategoryX, kEntryCentreY - 9.0f, kEntryCategoryMaxW, 18.0f };
        const juce::Rectangle<int> area =
            owner->localAreaToGlobal(juce::Rectangle<float>(r.x * s, r.y * s, r.w * s, r.h * s).toNearestInt());
        const std::weak_ptr<int> alive = alive_;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(owner).withTargetScreenArea(area),
                        [this, alive, names](int id) {
                            if (alive.expired() || id <= 0 || edit_ != Edit::saveAs)
                                return;
                            if (id == 1)
                                saveCategory_.clear();
                            else if (id >= 100 && static_cast<std::size_t>(id - 100) < names.size())
                                saveCategory_ = names[static_cast<std::size_t>(id - 100)];
                            ++revision_;
                            if (ctx_.host != nullptr)
                                ctx_.host->nudgeFullRate();
                        });
    }

    bool PresetBrowser::actionEnabled(Action a) const
    {
        const Entry* sel = selectedEntry();
        switch (a)
        {
            case Action::save:
            case Action::saveAs:
            case Action::importFiles:
            case Action::cancel:      return true;
            case Action::rename:
            case Action::remove:      return sel != nullptr && !sel->factory;
            case Action::exportFile:  return sel != nullptr;
            case Action::commit:      return editValid();
        }
        return false;
    }

    void PresetBrowser::runAction(Action a)
    {
        if (!actionEnabled(a))
            return;
        switch (a)
        {
            case Action::save:        saveCurrent(); break;
            case Action::saveAs:      beginSaveAs(); break;
            case Action::rename:      beginRename(selected_); break;
            case Action::remove:      armOrRemove(selected_); break;
            case Action::importFiles: chooseImport(); break;
            case Action::exportFile:  chooseExport(selected_); break;
            case Action::commit:      commitEdit(); break;
            case Action::cancel:      cancelEdit(); break;
        }
    }

    void PresetBrowser::close()
    {
        cancelEdit();
        armedUuid_.clear();
        needsReset_ = true;
        pressed_ = {};
        armed_ = false;
        ctx_.panel.setView({ nullptr, ctx_.screen, ctx_.scTab, Overlay::none }, false);
    }

    void PresetBrowser::flash(std::string_view message)
    {
        message_ = std::string(message);
        messageUntil_ = ctx_.seconds + kMessageS;
        ++revision_;
    }

    void PresetBrowser::typeChar(char32_t ch)
    {
        if (typedLen_ > 0 && ctx_.seconds - typedAt_ > kTypeAheadS)
            typedLen_ = 0;
        typedAt_ = ctx_.seconds;
        if (typedLen_ + 1 < typed_.size())
            typed_[typedLen_++] = upperAscii(static_cast<char>(ch));
        // One character (or the same one repeated) cycles through the rows it starts; more is a prefix that keeps the
        // selection while it still matches (ModeBrowser's type-ahead).
        bool same = true;
        for (std::size_t i = 1; i < typedLen_; ++i)
            same = same && typed_[i] == typed_[0];
        const std::string_view prefix(typed_.data(), same ? std::size_t{ 1 } : typedLen_);
        const int n = static_cast<int>(shown_.size());
        const int at = shownOf(selected_);
        const int from = same ? (at + 1) % std::max(1, n) : std::max(at, 0);
        for (int k = 0; k < n; ++k)
        {
            const int s = (from + k) % n;
            const std::string& name = entries_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(s)])].shownName;
            if (name.size() >= prefix.size() && sameNoCase(std::string_view(name).substr(0, prefix.size()), prefix))
            {
                select(shown_[static_cast<std::size_t>(s)], true);
                return;
            }
        }
    }

    // ---- names ----------------------------------------------------------------------------------------------------------

    bool PresetBrowser::nameTaken(std::string_view name, std::string_view ignoreUuid) const
    {
        const std::string_view n = trimmed(name);
        return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) {
            return e.uuid != ignoreUuid && sameNoCase(e.name, n);
        });
    }

    // PresetStore::uniqueName: "Name", else "Name 2", "Name 3", … against every row, case-insensitively; a taken "Name 2"
    // continues at "Name 3".
    std::string PresetBrowser::uniqueName(std::string_view wanted) const
    {
        const std::string base(trimmed(wanted).empty() ? std::string_view("Untitled") : trimmed(wanted));
        if (!nameTaken(base, {}))
            return base;
        std::string stem = base;
        long n = 2;
        if (const std::size_t sp = base.rfind(' '); sp != std::string::npos && sp > 0)
        {
            const std::string tail = base.substr(sp + 1);
            if (!tail.empty() && tail.size() <= 6 && tail.find_first_not_of("0123456789") == std::string::npos)
            {
                stem = std::string(trimmed(std::string_view(base).substr(0, sp)));
                n = std::max(2L, std::strtol(tail.c_str(), nullptr, 10) + 1);
            }
        }
        for (int guard = 0; guard < 100000; ++guard, ++n)
            if (std::string c = stem + " " + std::to_string(n); !nameTaken(c, {}))
                return c;
        return base;
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    std::span<const PresetBrowser::Cell> PresetBrowser::cells() const noexcept
    {
        if (edit_ != Edit::none)
            return editCells_;
        return browseCells_;
    }

    funkgui::Rect PresetBrowser::rowRect(int shownIndex) const noexcept
    {
        const int v = shownIndex - scroll_;
        if (v < 0 || v >= kRows)
            return {};
        return { kListX, rowY(v) + kRowHitDy, kListRight - kListX, kRowPitch };
    }

    funkgui::Rect PresetBrowser::filterRect(int filter) const noexcept
    {
        int slot = filter;
        if (filter >= kFixedFilters)
        {
            slot = filter - filterScroll_;
            if (slot < kFixedFilters || slot >= kFilterRows)
                return {};
        }
        const float y = kFilterY0 + kFilterPitch * static_cast<float>(slot) + (slot >= kFixedFilters ? kFilterGap : 0.0f);
        return { kColumn.x, y - 4.0f, kColumn.w, kFilterPitch };
    }

    float PresetBrowser::caretX(float x0) const
    {
        char pre[kMaxName * 4 + 8];
        upperInto(std::string_view(line_.buffer).substr(0, static_cast<std::size_t>(std::clamp(
                                                               line_.caret, 0, static_cast<int>(line_.buffer.size())))),
                  pre, sizeof pre);
        return x0 + funkgui::text::width(ctx_.atlas, pre, T::kLabel);
    }

    PresetBrowser::Hit PresetBrowser::hitAt(funkgui::Point p) const noexcept
    {
        if (edit_ == Edit::saveAs)
        {
            if (Rect{ kFieldX - 4.0f, kTopY - 6.0f, kFieldW + 8.0f, 20.0f }.contains(p))
                return { Zone::field, -1 };
            if (Rect{ kEntryCategoryX - 4.0f, kTopY - 6.0f, kEntryCategoryMaxW + 8.0f, 20.0f }.contains(p))
                return { Zone::category, -1 };
        }
        const std::span<const Cell> cs = cells();
        for (std::size_t i = 0; i < cs.size(); ++i)
            if (Rect{ cs[i].rect.x, cs[i].rect.y - 2.0f, cs[i].rect.w, cs[i].rect.h + 4.0f }.contains(p))
                return { Zone::action, static_cast<int>(i) };
        for (int f = 0; f < static_cast<int>(filters_.size()); ++f)
            if (const Rect r = filterRect(f); !r.isEmpty() && r.contains(p))
                return { Zone::filter, f };
        for (int v = 0; v < kRows; ++v)
        {
            const int s = scroll_ + v;
            if (s >= static_cast<int>(shown_.size()))
                break;
            if (rowRect(s).contains(p))
            {
                const Entry& e = entries_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(s)])];
                if (edit_ == Edit::rename && e.uuid == editUuid_ && p.x < kRowX + kRenameW)
                    return { Zone::field, s };
                return { Zone::row, s };
            }
        }
        if (kList.contains(p))
            return { Zone::list, -1 };
        if (kColumn.contains(p))
            return { Zone::column, -1 };
        return { Zone::body, -1 };
    }

    void PresetBrowser::noteEdit()
    {
        const std::string_view name = trimmed(line_.buffer);
        editUnique_ = edit_ == Edit::saveAs && !name.empty() ? uniqueName(name) : std::string();
        editTaken_ = edit_ == Edit::rename && !name.empty() && nameTaken(name, editUuid_);
        ++revision_;                                             // the typed name is the edit item's value
    }

    void PresetBrowser::statusText(char* out, std::size_t cap, int& kind) const
    {
        Line l{ out, cap };
        kind = 0;
        const int armed = armedUuid_.empty() ? -1 : entryOf(armedUuid_);
        const std::string_view name = trimmed(line_.buffer);
        if (armed >= 0)
        {
            l.add("PRESS DELETE AGAIN TO DELETE '");
            l.add(entries_[static_cast<std::size_t>(armed)].shownName);
            l.add("'");
            kind = 2;
        }
        else if (!message_.empty() && ctx_.seconds <= messageUntil_)
        {
            l.add(message_);
            kind = 1;
        }
        else if (edit_ == Edit::saveAs)
        {
            if (name.empty())
            {
                l.add("TYPE A NAME   RETURN SAVES   ESC CANCELS");
                kind = 3;
            }
            else if (editUnique_ != name)
            {
                char shown[kMaxName * 4 + 16];
                upperInto(editUnique_, shown, sizeof shown);
                l.add("TAKEN: IT WILL BE SAVED AS '");
                l.add(shown);
                l.add("'");
                kind = 1;
            }
            else
            {
                l.add("RETURN SAVES   ESC CANCELS");
                kind = 3;
            }
        }
        else if (edit_ == Edit::rename)
        {
            if (editTaken_)
            {
                char shown[kMaxName * 4 + 16];
                upperInto(name, shown, sizeof shown);
                l.add("'");
                l.add(shown);
                l.add("' IS TAKEN");
                kind = 1;
            }
            else
            {
                l.add("RETURN RENAMES   ESC CANCELS");
                kind = 3;
            }
        }
        else
        {
            const int all = static_cast<int>(entries_.size());
            l.add(all);
            l.add(all == 1 ? " PRESET" : " PRESETS");
            if (filters_[static_cast<std::size_t>(filter_)].kind != Filter::Kind::all)
            {
                l.add(kSep);
                l.add(static_cast<int>(shown_.size()));
                l.add(" SHOWN");
            }
        }
    }

    void PresetBrowser::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const Rect& o = layout::kOverlay;
        {
            const funkgui::Canvas::Scope scope(c, tag::browserBg, false);
            c.rrect(kGround.x, kGround.y, kGround.w, kGround.h, 0.0f, th.ground);
            c.hairlineH(o.x, o.y, o.w, th.ink16);
            c.hairlineH(o.x, o.bottom() - 1.0f, o.w, th.ink16);
            c.hairlineV(kDividerX, kTrackTop, kTrackH + 4.0f, th.ink16);
            c.hairlineH(o.x, kBarRuleY, o.w, th.ink16);
        }

        // Headings (the save-as line takes the list's).
        {
            const funkgui::Canvas::Scope scope(c, tag::browserHeading, false);
            c.text("SHOW", kFilterX, kTopY, T::kCaption, th.ink52);
            if (edit_ != Edit::saveAs)
            {
                c.text("NAME", kRowX, kTopY, T::kCaption, th.ink32);
                c.text("CATEGORY", kCategoryX, kTopY, T::kCaption, th.ink32);
                c.text("MODE", kModeX, kTopY, T::kCaption, th.ink32);
            }
        }

        // The filter column.
        {
            const funkgui::Canvas::Scope scope(c, tag::browserHeading, false);
            char fitted[96];
            char count[16];
            for (int f = 0; f < static_cast<int>(filters_.size()); ++f)
            {
                const Rect r = filterRect(f);
                if (r.isEmpty())
                    continue;
                const Filter& fl = filters_[static_cast<std::size_t>(f)];
                const float y = r.y + 4.0f;
                const bool chosen = f == filter_;
                const bool over = hover_.zone == Zone::filter && hover_.index == f;
                funkgui::text::fitEllipsis(ctx_.atlas, fl.label.c_str(), T::kCaption, kFilterRight - kFilterX - 30.0f,
                                           fitted, sizeof fitted);
                c.text(fitted, kFilterX, y, T::kCaption, chosen ? th.ink100 : over ? th.ink70 : th.ink52);
                std::snprintf(count, sizeof count, "%d", fl.count);
                c.text(count, kFilterRight, c.sharedBaselineTop(y, T::kCaption, T::kMicro), T::kMicro,
                       chosen ? th.ink52 : th.ink32, funkgui::Align::right);
                if (chosen)
                    c.rrect(kFilterBarX, y + 1.0f, 2.0f, 8.0f, 0.0f, th.ink100);
            }
        }

        // The rows.
        const float rowCap = -c.capCentreTop(0.0f, T::kLabel);  // cap centre below a kLabel line's top
        char fitted[256];
        for (int v = 0; v < kRows; ++v)
        {
            const int s = scroll_ + v;
            if (s >= static_cast<int>(shown_.size()))
                break;
            const int ei = shown_[static_cast<std::size_t>(s)];
            const Entry& e = entries_[static_cast<std::size_t>(ei)];
            const float y = rowY(v);
            const bool isSelected = ei == selected_;
            const bool isCurrent = e.uuid == currentUuid_;
            const bool over = hover_.zone == Zone::row && hover_.index == s;
            const bool renaming = edit_ == Edit::rename && e.uuid == editUuid_;
            if (isSelected)
            {
                const funkgui::Canvas::Scope scope(c, tag::browserRow, false);
                c.rrect(kListX, y + kRowHitDy, kListRight - kListX, kRowPitch, 0.0f, th.ink16);
            }
            if (isCurrent)
            {
                const funkgui::Canvas::Scope scope(c, tag::browserCurrent, false);
                c.rrect(kCurrentBarX, y, layout::browser::kCurrentBarW, layout::browser::kCurrentBarH, 0.0f,
                        th.ink100);
            }
            const funkgui::Canvas::Scope scope(c, tag::browserRow, false);
            if (renaming)
            {
                char text[kMaxName * 4 + 8];
                upperInto(line_.buffer, text, sizeof text);
                const float capC = y + rowCap;
                if (line_.allSelected && text[0] != '\0')
                    c.rrect(kRowX - 2.0f, capC - 7.0f, c.textWidth(text, T::kLabel) + 4.0f, 14.0f, 1.0f, th.accentDim);
                c.text(text, kRowX, y, T::kLabel, th.ink100);
                c.rrect(caretX(kRowX) + 0.5f, capC - 6.0f, 1.5f, 12.0f, 0.0f, th.accent);
                c.hairlineH(kRowX, y + kRenameRuleDy, kRenameW, th.accent);
            }
            else
            {
                const bool pressedHere = armed_ && pressed_.zone == Zone::row && pressed_.index == s;
                funkgui::text::fitEllipsis(ctx_.atlas, e.shownName.c_str(), T::kLabel, kNameMaxW, fitted, sizeof fitted);
                c.text(fitted, kRowX, y, T::kLabel,
                       pressedHere ? th.accent : (isCurrent || isSelected || over) ? th.ink100 : th.ink52);
                funkgui::text::fitEllipsis(ctx_.atlas, e.shownCategory.c_str(), T::kMicro, kCategoryMaxW, fitted,
                                           sizeof fitted);
                c.text(fitted, kCategoryX, c.sharedBaselineTop(y, T::kLabel, T::kMicro), T::kMicro,
                       isSelected ? th.ink70 : th.ink52);
            }
            funkgui::text::fitEllipsis(ctx_.atlas, e.mode.c_str(), T::kMicro, kModeMaxW, fitted, sizeof fitted);
            c.text(fitted, kModeX, c.sharedBaselineTop(y, T::kLabel, T::kMicro), T::kMicro,
                   isSelected ? th.ink70 : th.ink52);
            c.text(e.factory ? "FACTORY" : "USER", kSourceRight, c.sharedBaselineTop(y, T::kLabel, T::kMicro), T::kMicro,
                   isSelected ? th.ink52 : th.ink32, funkgui::Align::right);
        }
        if (shown_.empty())
        {
            const funkgui::Canvas::Scope scope(c, tag::browserRow, false);
            const Filter& f = filters_[static_cast<std::size_t>(filter_)];
            const char* text = entries_.empty()                ? "NO PRESETS"
                             : f.kind == Filter::Kind::user     ? "NO PRESETS OF YOUR OWN YET   SAVE AS KEEPS THE SOUND"
                                                                : "NONE";
            c.text(text, kList.centreX(), c.capCentreTop(kList.y + 60.0f, T::kLabel), T::kLabel, th.ink32,
                   funkgui::Align::centre);
        }
        if (static_cast<int>(shown_.size()) > kRows)
        {
            const funkgui::Canvas::Scope scope(c, tag::browserPager, false);
            const float n = static_cast<float>(shown_.size());
            const float h = std::max(12.0f, kTrackH * static_cast<float>(kRows) / n);
            const float y = kTrackTop + (kTrackH - h) * static_cast<float>(scroll_) / (n - static_cast<float>(kRows));
            c.rrect(kThumbX, y, 2.0f, h, 1.0f, th.ink32);
        }

        // The save-as line: SAVE AS [name] IN <CATEGORY>.
        if (edit_ == Edit::saveAs)
        {
            const funkgui::Canvas::Scope scope(c, tag::browserRow, false);
            c.text("SAVE AS", kEntryLabelX, kTopY, T::kCaption, th.ink100);
            char text[kMaxName * 4 + 8];
            upperInto(line_.buffer, text, sizeof text);
            const float top = c.capCentreTop(kEntryCentreY, T::kLabel);
            if (line_.allSelected && text[0] != '\0')
                c.rrect(kFieldX - 2.0f, kEntryCentreY - 7.0f, c.textWidth(text, T::kLabel) + 4.0f, 14.0f, 1.0f,
                        th.accentDim);
            c.text(text, kFieldX, top, T::kLabel, th.ink100);
            c.rrect(caretX(kFieldX) + 0.5f, kEntryCentreY - 6.0f, 1.5f, 12.0f, 0.0f, th.accent);
            c.hairlineH(kFieldX, kEntryRuleY, kFieldW, th.accent);
            char cat[96];
            Line l{ cat, sizeof cat };
            l.add("IN ");
            char upperCat[80];
            upperInto(saveCategory_, upperCat, sizeof upperCat);
            l.add(saveCategory_.empty() ? "NO CATEGORY" : upperCat);
            funkgui::text::fitEllipsis(ctx_.atlas, cat, T::kCaption, kEntryCategoryMaxW, fitted, sizeof fitted);
            c.text(fitted, kEntryCategoryX, kTopY, T::kCaption, hover_.zone == Zone::category ? th.ink100 : th.ink52);
        }

        // The bottom bar: the status line, then the action cells.
        {
            char status[256];
            int kind = 0;
            statusText(status, sizeof status, kind);
            const std::span<const Cell> cs = cells();
            const float statusMaxW = cs.front().rect.x - 16.0f - kFilterX;
            funkgui::text::fitEllipsis(ctx_.atlas, status, T::kCaption, statusMaxW, fitted, sizeof fitted);
            const funkgui::Canvas::Scope scope(c, kind == 1 || kind == 2 ? tag::notice : tag::caption, false);
            c.text(fitted, kFilterX, kBarTextY, T::kCaption,
                   kind == 2 ? th.ink100 : kind == 1 ? th.ink70 : th.ink32);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            const std::span<const Cell> cs = cells();
            const bool armedSel = !armedUuid_.empty() && armedUuid_ == selectedUuid_;
            for (std::size_t i = 0; i < cs.size(); ++i)
            {
                const Cell& cell = cs[i];
                const bool enabled = actionEnabled(cell.action);
                const bool over = hover_.zone == Zone::action && hover_.index == static_cast<int>(i);
                const bool pressedHere = armed_ && pressed_.zone == Zone::action && pressed_.index == static_cast<int>(i);
                const bool confirm = cell.action == Action::remove && armedSel;
                const char* label = cell.action == Action::commit && edit_ == Edit::rename ? "RENAME"
                                  : confirm                                                ? "CONFIRM"
                                                                                           : cell.label;
                const funkgui::Col ink = !enabled                  ? th.ink16
                                       : pressedHere || confirm    ? th.accent
                                       : over                      ? th.ink100
                                                                   : th.ink52;
                c.text(label, cell.rect.centreX(), kBarTextY, T::kCaption, ink, funkgui::Align::centre);
            }
        }
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool PresetBrowser::hit(funkgui::Point p) const { return layout::kOverlay.contains(p); }

    void PresetBrowser::pointerMove(const funkgui::PointerEvent& e)
    {
        sync();
        hover_ = hitAt({ e.x, e.y });
    }

    void PresetBrowser::pointerExit() { hover_ = {}; }

    void PresetBrowser::pointerDown(const funkgui::PointerEvent& e)
    {
        sync();
        pressed_ = {};
        armed_ = false;
        typedLen_ = 0;
        const funkgui::Point p { e.x, e.y };
        const Hit h = hitAt(p);
        hover_ = h;

        if (edit_ != Edit::none)
        {
            if (h.zone == Zone::field)
            {
                // A click in the field keeps editing and puts the caret at the nearest character boundary.
                const float x0 = edit_ == Edit::rename ? kRowX : kFieldX;
                char text[kMaxName * 4 + 8];
                upperInto(line_.buffer, text, sizeof text);
                int best = 0;
                float bestD = std::fabs(p.x - x0);
                char pre[kMaxName * 4 + 8];
                for (std::size_t k = 1; k <= line_.buffer.size() && k < sizeof pre; ++k)
                {
                    std::memcpy(pre, text, k);
                    pre[k] = '\0';
                    const float d = std::fabs(p.x - (x0 + funkgui::text::width(ctx_.atlas, pre, T::kLabel)));
                    if (d < bestD)
                    {
                        bestD = d;
                        best = static_cast<int>(k);
                    }
                }
                line_.caret = best;
                line_.allSelected = false;
                return;
            }
            const bool editCell = h.zone == Zone::action || (h.zone == Zone::category && edit_ == Edit::saveAs);
            if (!editCell || e.popup)
            {
                // Anywhere else: a rename is confirmed (Finder), a save as cancelled (nothing is created without
                // Return). The click does nothing else.
                if (edit_ == Edit::rename)
                    commitEdit();
                else
                    cancelEdit();
                return;
            }
        }

        if (e.popup)
        {
            if (h.zone == Zone::row)
            {
                const int entry = shown_[static_cast<std::size_t>(h.index)];
                select(entry, false);
                showMenu(entry, rowRect(h.index));
            }
            else if (h.zone == Zone::list || h.zone == Zone::body || h.zone == Zone::column)
            {
                showMenu(-1, { p.x, p.y, 1.0f, 1.0f });
            }
            return;
        }
        switch (h.zone)
        {
            case Zone::row:
                select(shown_[static_cast<std::size_t>(h.index)], false);
                if (e.clicks <= 1)                               // the second press of a double-click never loads
                {
                    pressed_ = h;
                    armed_ = true;
                }
                break;
            case Zone::filter:
                setFilter(h.index);                              // on the press, as the Mode browser's pager
                break;
            case Zone::action:
                // The second press of a double-click is not a second press of the cell: a double-clicked DELETE must
                // not confirm itself, nor IMPORT open two choosers.
                if (e.clicks <= 1 && actionEnabled(cells()[static_cast<std::size_t>(h.index)].action))
                {
                    pressed_ = h;
                    armed_ = true;
                }
                break;
            case Zone::category:
                pressed_ = h;
                armed_ = true;
                break;
            case Zone::none:
            case Zone::field:
            case Zone::list:
            case Zone::column:
            case Zone::body:
                break;
        }
    }

    void PresetBrowser::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (!armed_)
            return;
        const Hit h = hitAt({ e.x, e.y });
        if (h.zone != pressed_.zone || h.index != pressed_.index)
            armed_ = false;                                      // dragging off cancels
    }

    void PresetBrowser::pointerUp(const funkgui::PointerEvent& e)
    {
        const Hit p = pressed_;
        bool fire = armed_ && isOpen();
        if (fire)
        {
            const Hit h = hitAt({ e.x, e.y });
            fire = h.zone == p.zone && h.index == p.index;
        }
        pressed_ = {};
        armed_ = false;
        if (!fire)
            return;
        sync();
        switch (p.zone)
        {
            case Zone::row:
                if (p.index >= 0 && p.index < static_cast<int>(shown_.size()))
                    load(shown_[static_cast<std::size_t>(p.index)], false);   // a click keeps the browser open (HR)
                break;
            case Zone::action:
                if (p.index >= 0 && p.index < static_cast<int>(cells().size()))
                    runAction(cells()[static_cast<std::size_t>(p.index)].action);
                break;
            case Zone::category:
                showCategoryMenu();
                break;
            case Zone::none:
            case Zone::filter:
            case Zone::field:
            case Zone::list:
            case Zone::column:
            case Zone::body:
                break;
        }
    }

    void PresetBrowser::doubleClick(const funkgui::PointerEvent& e)
    {
        sync();
        pressed_ = {};
        armed_ = false;
        if (e.popup || !isOpen() || edit_ != Edit::none)
            return;
        const Hit h = hitAt({ e.x, e.y });
        if (h.zone == Zone::row)
            load(shown_[static_cast<std::size_t>(h.index)], true);   // already loaded by the first click: just closes
    }

    bool PresetBrowser::wheel(const funkgui::WheelEvent& e)
    {
        sync();
        if (edit_ == Edit::rename)
            return true;                                         // the renamed row stays where it is
        const float d = (e.reversed ? -1.0f : 1.0f) * (e.dy != 0.0f ? e.dy : e.dx);
        if (!std::isfinite(d))
            return true;
        int rows = 0;
        if (e.smooth)
        {
            wheelAcc_ = std::clamp(wheelAcc_ - d * kWheelRowsSmooth, -64.0f, 64.0f);
            rows = static_cast<int>(wheelAcc_);
            wheelAcc_ -= static_cast<float>(rows);
        }
        else
        {
            rows = d < 0.0f ? static_cast<int>(kWheelRowsDiscrete) : d > 0.0f ? -static_cast<int>(kWheelRowsDiscrete) : 0;
        }
        const Hit h = hitAt({ e.x, e.y });
        if (h.zone == Zone::filter || h.zone == Zone::column)
            scrollFilters(rows);
        else
            scrollBy(rows);
        return true;                                             // over the overlay nothing underneath scrolls
    }

    bool PresetBrowser::key(const funkgui::KeyEvent& e)
    {
        if (!isOpen())
            return false;
        sync();
        if (edit_ != Edit::none)
        {
            switch (e.key)
            {
                case funkgui::Key::enter:  commitEdit(); return true;
                case funkgui::Key::escape: cancelEdit(); return true;
                case funkgui::Key::tab:    return false;
                case funkgui::Key::character:
                case funkgui::Key::up:
                case funkgui::Key::down:
                case funkgui::Key::left:
                case funkgui::Key::right:
                case funkgui::Key::pageUp:
                case funkgui::Key::pageDown:
                case funkgui::Key::home:
                case funkgui::Key::end:
                case funkgui::Key::backspace:
                case funkgui::Key::del:
                case funkgui::Key::space:
                    break;
            }
            if (line_.key(e, kMaxName))
            {
                noteEdit();
                return true;
            }
            // An edit owns the keyboard (HR), except the host's Cmd and Ctrl chords.
            return !(e.mods.cmd || e.mods.ctrl);
        }

        const int at = shownOf(selected_);
        const int last = static_cast<int>(shown_.size()) - 1;
        const bool typing = typedLen_ > 0 && ctx_.seconds - typedAt_ <= kTypeAheadS;
        switch (e.key)
        {
            case funkgui::Key::up:       typedLen_ = 0; moveSelection(at < 0 ? last : at - 1); return true;
            case funkgui::Key::down:     typedLen_ = 0; moveSelection(at < 0 ? 0 : at + 1); return true;
            case funkgui::Key::pageUp:   typedLen_ = 0; moveSelection(at < 0 ? 0 : at - kRows); return true;
            case funkgui::Key::pageDown: typedLen_ = 0; moveSelection(at < 0 ? 0 : at + kRows); return true;
            case funkgui::Key::home:     typedLen_ = 0; moveSelection(0); return true;
            case funkgui::Key::end:      typedLen_ = 0; moveSelection(last); return true;
            case funkgui::Key::left:     setFilter(filter_ - 1); return true;
            case funkgui::Key::right:    setFilter(filter_ + 1); return true;
            case funkgui::Key::enter:
                load(selectedEntry() != nullptr ? selected_ : -1, true);
                return true;
            case funkgui::Key::space:
                if (typing)
                    typeChar(U' ');                              // "MIX BUS": a space inside a pending buffer
                else
                    load(selectedEntry() != nullptr ? selected_ : -1, true);
                return true;
            case funkgui::Key::character:
                if (e.mods.cmd || e.mods.ctrl || e.ch < U' ' || e.ch > U'~')
                    return false;                                // host shortcuts; nothing a name holds
                typeChar(e.ch);
                return true;
            case funkgui::Key::backspace:
                if (e.mods.cmd)                                  // Cmd-Backspace: delete, as Finder
                {
                    armOrRemove(selected_);
                    return true;
                }
                if (typedLen_ > 0)
                    --typedLen_;
                return true;                                     // never a slot under the overlay
            case funkgui::Key::del:
                armOrRemove(selected_);
                return true;
            case funkgui::Key::escape:
                if (!armedUuid_.empty())
                {
                    armedUuid_.clear();                          // Esc takes back an armed delete first
                    ++revision_;
                    return true;
                }
                return false;                                    // the Panel closes the browser
            case funkgui::Key::tab:
                return false;
        }
        return false;
    }

    funkgui::Cursor PresetBrowser::cursor(funkgui::Point p) const
    {
        const Hit h = hitAt(p);
        switch (h.zone)
        {
            case Zone::row:
            case Zone::filter:
            case Zone::category:
                return funkgui::Cursor::pointingHand;
            case Zone::action:
                return actionEnabled(cells()[static_cast<std::size_t>(h.index)].action) ? funkgui::Cursor::pointingHand
                                                                                          : funkgui::Cursor::normal;
            case Zone::none:
            case Zone::field:
            case Zone::list:
            case Zone::column:
            case Zone::body:
                break;
        }
        return funkgui::Cursor::normal;
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void PresetBrowser::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        if (edit_ != Edit::none)
        {
            funkgui::A11yItem it;
            it.id = a11yId(ViewIndex::presetBrowser, kEditLocal);
            it.role = funkgui::A11yRole::staticText;
            const int s = edit_ == Edit::rename ? shownOf(entryOf(editUuid_)) : -1;
            it.bounds = edit_ == Edit::saveAs ? Rect{ kFieldX, kTopY - 6.0f, kFieldW, 20.0f }
                      : s >= 0               ? Rect{ kRowX, rowRect(s).y, kRenameW, kRowPitch }
                                             : Rect{};
            it.title = edit_ == Edit::saveAs ? "New preset name" : "New name";
            it.value = line_.buffer;
            it.help = edit_ == Edit::saveAs ? "Type a name. Return saves, Escape cancels"
                                            : "Type the new name. Return renames, Escape cancels";
            out.push_back(std::move(it));
            if (edit_ == Edit::saveAs)
            {
                funkgui::A11yItem cat;
                cat.id = a11yId(ViewIndex::presetBrowser, kCategoryLocal);
                cat.role = funkgui::A11yRole::button;
                cat.bounds = { kEntryCategoryX, kTopY - 6.0f, kEntryCategoryMaxW, 20.0f };
                cat.title = "Category";
                cat.value = saveCategory_.empty() ? std::string("None") : saveCategory_;
                cat.help = "Chooses the category the preset is filed under";
                out.push_back(std::move(cat));
            }
        }

        {
            funkgui::A11yItem st;
            st.id = a11yId(ViewIndex::presetBrowser, kStatusLocal);
            st.role = funkgui::A11yRole::staticText;
            st.bounds = { kFilterX, kBarTop, cells().front().rect.x - 16.0f - kFilterX, kBarH };
            st.title = "Status";
            char status[256];
            int kind = 0;
            statusText(status, sizeof status, kind);
            st.value = status;
            st.readOnly = true;
            out.push_back(std::move(st));
        }

        {
            funkgui::A11yItem group;
            group.id = a11yId(ViewIndex::presetBrowser, kFiltersLocal);
            group.role = funkgui::A11yRole::radioGroup;
            group.bounds = kColumn;
            group.title = "Show";
            group.value = filters_[static_cast<std::size_t>(filter_)].label;
            out.push_back(std::move(group));
            constexpr int kMaxFilterItems = 0x80;                // locals 0x40–0xBF
            for (int f = 0; f < static_cast<int>(filters_.size()) && f < kMaxFilterItems; ++f)
            {
                const Filter& fl = filters_[static_cast<std::size_t>(f)];
                funkgui::A11yItem it;
                it.id = a11yId(ViewIndex::presetBrowser, kFilterLocal0 + static_cast<uint32_t>(f));
                it.parent = group.id;
                it.role = funkgui::A11yRole::radioButton;
                it.bounds = filterRect(f);
                it.visible = !it.bounds.isEmpty();
                switch (fl.kind)
                {
                    case Filter::Kind::all:      it.title = "All"; break;
                    case Filter::Kind::factory:  it.title = "Factory"; break;
                    case Filter::Kind::user:     it.title = "User"; break;
                    case Filter::Kind::category:
                        for (const Entry& e : entries_)
                            if (e.categoryKey == fl.key)
                            {
                                it.title = e.category;
                                break;
                            }
                        break;
                }
                it.description = std::to_string(fl.count) + (fl.count == 1 ? " preset" : " presets");
                it.checkable = true;
                it.checked = f == filter_;
                out.push_back(std::move(it));
            }
        }

        for (int v = 0; v < kRows; ++v)
        {
            const int s = scroll_ + v;
            if (s >= static_cast<int>(shown_.size()))
                break;
            const Entry& e = entries_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(s)])];
            if (e.index < 0 || static_cast<uint32_t>(e.index) > 0xFFFFu - kRowLocal0)
                continue;
            funkgui::A11yItem it;
            it.id = a11yId(ViewIndex::presetBrowser, kRowLocal0 + static_cast<uint32_t>(e.index));
            it.role = funkgui::A11yRole::listItem;
            it.bounds = rowRect(s);
            it.title = e.name;
            std::string d = e.category.empty() ? std::string() : e.category + ", ";
            if (!e.mode.empty())
                d += e.mode + ", ";
            d += e.factory ? "factory" : "user";
            it.description = std::move(d);
            it.help = "Click loads it, double-click loads it and closes";
            it.checkable = true;
            it.checked = e.uuid == currentUuid_;
            out.push_back(std::move(it));
        }

        const std::span<const Cell> cs = cells();
        const bool armedSel = !armedUuid_.empty() && armedUuid_ == selectedUuid_;
        for (const Cell& cell : cs)
        {
            funkgui::A11yItem it;
            it.role = funkgui::A11yRole::button;
            it.bounds = cell.rect;
            it.enabled = actionEnabled(cell.action);
            uint32_t loc = kSaveAsLocal;
            switch (cell.action)
            {
                case Action::save:
                {
                    loc = kSaveLocal;
                    it.title = "Save";
                    const int cur = entryOf(currentUuid_);
                    it.help = cur >= 0 && !entries_[static_cast<std::size_t>(cur)].factory
                                  ? "Saves over " + entries_[static_cast<std::size_t>(cur)].name
                                  : std::string("Saves the current sound as a new preset");
                    break;
                }
                case Action::saveAs:      loc = kSaveAsLocal; it.title = "Save as"; break;
                case Action::rename:      loc = kRenameLocal; it.title = "Rename"; break;
                case Action::remove:      loc = kDeleteLocal; it.title = armedSel ? "Confirm delete" : "Delete"; break;
                case Action::importFiles: loc = kImportLocal; it.title = "Import"; break;
                case Action::exportFile:  loc = kExportLocal; it.title = "Export"; break;
                case Action::commit:      loc = kCommitLocal; it.title = edit_ == Edit::rename ? "Rename" : "Save"; break;
                case Action::cancel:      loc = kCancelLocal; it.title = "Cancel"; break;
            }
            it.id = a11yId(ViewIndex::presetBrowser, loc);
            out.push_back(std::move(it));
        }
    }

    int PresetBrowser::focusOrder(std::span<uint32_t> out) const
    {
        std::size_t n = 0;
        for (int v = 0; v < kRows && n < out.size(); ++v)
        {
            const int s = scroll_ + v;
            if (s >= static_cast<int>(shown_.size()))
                break;
            const Entry& e = entries_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(s)])];
            out[n++] = a11yId(ViewIndex::presetBrowser, kRowLocal0 + static_cast<uint32_t>(e.index));
        }
        return static_cast<int>(n);
    }

    uint32_t PresetBrowser::a11yRevision() const { return revision_; }

    void PresetBrowser::a11yAction(uint32_t id, funkgui::A11yAction a, double)
    {
        if (!isOpen() || viewIndexOf(id) != static_cast<int>(ViewIndex::presetBrowser))
            return;
        sync();
        const uint32_t loc = local(id);
        const bool press = a == funkgui::A11yAction::press || a == funkgui::A11yAction::toggle;
        if (loc >= kRowLocal0)
        {
            const int index = static_cast<int>(loc - kRowLocal0);
            int entry = -1;
            for (std::size_t i = 0; i < entries_.size(); ++i)
                if (entries_[i].index == index)
                    entry = static_cast<int>(i);
            if (entry < 0)
                return;
            switch (a)
            {
                case funkgui::A11yAction::press:
                case funkgui::A11yAction::toggle:
                    select(entry, true);
                    load(entry, false);
                    break;
                case funkgui::A11yAction::focus:
                    select(entry, true);
                    break;
                case funkgui::A11yAction::showMenu:
                    select(entry, true);
                    showMenu(entry, rowRect(shownOf(entry)));
                    break;
                case funkgui::A11yAction::setValue:
                case funkgui::A11yAction::increment:
                case funkgui::A11yAction::decrement:
                    break;
            }
            return;
        }
        if (loc >= kFilterLocal0 && loc < kFilterLocal0 + 0x80u)
        {
            if (press)
                setFilter(static_cast<int>(loc - kFilterLocal0));
            return;
        }
        if (loc == kFiltersLocal)
        {
            if (a == funkgui::A11yAction::increment)
                setFilter(filter_ + 1);
            else if (a == funkgui::A11yAction::decrement)
                setFilter(filter_ - 1);
            return;
        }
        if (!press)
            return;
        switch (loc)
        {
            case kSaveAsLocal:
                if (edit_ != Edit::saveAs)
                    beginSaveAs();                               // the strip's save as comes this way
                break;
            case kSaveLocal:     if (edit_ == Edit::none) runAction(Action::save); break;
            case kRenameLocal:   runAction(Action::rename); break;
            case kDeleteLocal:   runAction(Action::remove); break;
            case kImportLocal:   runAction(Action::importFiles); break;
            case kExportLocal:   runAction(Action::exportFile); break;
            case kCommitLocal:   if (edit_ != Edit::none) runAction(Action::commit); break;
            case kCancelLocal:   if (edit_ != Edit::none) runAction(Action::cancel); break;
            case kCategoryLocal: showCategoryMenu(); break;
            default:             break;
        }
    }
}
