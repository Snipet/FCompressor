// Source/editor/views/ModeBrowser.h — the ModeBrowser sub-view (02 §8.6). Class declaration frozen at FZ4; U5 (S12) completes
// it: the overlay at layout::kOverlay: one column per Group, the current row marked, the hovered
// row's spec line, click or Return commits `mode` only, Alt commits with the Mode defaults inside one batch,
// Esc or a click outside cancels (the Panel closes it). Drawn while the Panel's overlay fade is > 0.
//
// U5 (S12) behaviour, where 02 §8.6 is silent (U5 handoff):
// - The grid (ModeGrid below): every registered Mode (retired slots never appear) in the global order — Group, then
//   slot — one column per Group in enum order (VCA FET OPTO VARI-MU DIODE MODERN LIMIT OTHER), 12 rows each. An empty
//   group keeps its column (heading "OTHER  0" in ink32) so a Mode added to it moves nobody else's row; a group with
//   more than 12 Modes wraps into the next column under "VCA (2)"; the columns are paged 8 at a time (‹ 1/2 › at
//   y 334, right-aligned to x 920, only when there is more than one page).
// - Drawing: the opaque ground covers kOverlay grown by 8 px left and right (column 0's current bar sits at x 34 and
//   its focus ring at x 33) and 4 px up and down (HR's browser ground; it hides the QUALITY cells' top at y 62);
//   ink16 hairlines at the overlay's top and bottom edges. Headings kCaption ink52 at y 72; names kLabel at the row
//   top, ellipsised to 98 px so they never reach the next column's bar: ink52, ink100 when current, hovered or
//   keyboard-highlighted, accent while pressed; the current Mode's 2×12 ink100 bar at (x − 6, row top).
// - The highlight: the row Return commits. It starts on the current Mode each time the browser opens (with the focus
//   ring when it was opened from the keyboard), the pointer moves it (hover; a key then drops the hover until the
//   pointer moves again) and the keys move it (↑↓ within a column,
//   stopping at its ends; ←→ to the neighbouring non-empty column at the same row, clamped to that column's last row,
//   crossing to the next page at a page edge; Home / End the first / last Mode; PageUp / PageDown a page; type-ahead
//   with a 1 s buffer of panel time, where a space extends a pending buffer). The keyboard highlight draws the focus
//   ring from x − 8 to x + 102 (it clears the name and encloses the current bar). The hovered row, else the keyboard
//   highlight, offers the hand (its spec line on the footer: ModeDescriptor::specLine, + "   ALT-CLICK: + DEFAULTS"
//   when the whole line fits the footer's 740 px) as the strongest kind, `drag`, so it outranks the Mode latch
//   underneath, which offers its own spec while the pointer rests on it or it holds the focus (the browser was opened
//   from it). Only `mode`, a global, is named: DisplayRow and the plots key on Mode-filtered Pids and ignore it.
// - Commits (HR's preset-browser convention, which 02 §8.6 cites): a click on a row switches and keeps the browser
//   open, so the constraints are seen landing on the slots; a double-click switches and closes; Return switches and
//   closes; the Alt variants (Alt-click, Alt-double-click, Alt-Return) load the Mode's defaults in the same batch.
//   A plain switch is one tap of `mode` and nothing else (02 §8.4.3, K2 #4); a switch with defaults is one
//   GestureController::tapMany of `mode` then every live or stepped parameter whose Mode default (fcdsp::modeDefaults)
//   differs from its value, inside one beginBatch()/endBatch() (02 §8.4.4, K2 #23). A row commits on release inside
//   it (dragging off cancels); a popup click writes nothing. The pager steps on the press; the wheel pages (one notch
//   per page, a burst closes after 0.5 s).
// - Keys the browser does not use (Tab, Esc) are the Panel's; Backspace and Delete edit the type-ahead buffer and never
//   reach a slot under the overlay; characters with Cmd or Ctrl go to the host.
// - Keyboard focus (S13 H1a): while the browser is open it is the Panel's whole Tab order (its rows on the shown
//   page, then the enabled pager buttons; Panel::composeFocusOrder), and the keyboard highlight IS the Panel focus:
//   opened from the keyboard, the browser takes the focus onto the current Mode's row (so the Mode latch underneath no
//   longer draws a second ring), every key that moves the highlight moves the focus with it, and Tab moving the focus
//   onto a row moves the highlight there. The ring is drawn on the focused row (not while the pointer drives the
//   highlight) or on the focused pager button, where Return and Space page. Closing gives the focus back to its opener
//   (Panel).
// - A11y: rows are listItems (title = the Mode name, description = its group, help = its spec line, checkable, checked
//   = current; press = the click's commit; focus moves the highlight), headings staticText, and with more than one
//   page the pager's two buttons and a "Page 1 of 2" text; items off the shown page are not visible. Ids:
//   rows 0x100 + slot (stable when Modes are added), headings 0x20 + column, pager 0x10–0x12.
//
// The members below the FZ4 declarations are additions (SubView overrides with defaults, private state, a constructor
// over a given grid, and the public ModeGrid model the view draws from and ui.browsers checks on synthetic Mode lists);
// no FZ4 declaration changed.
#pragma once

#include "editor/SubView.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fcmp::ui
{
    // ---- U5 addition: the browser's grid as a pure model (02 §8.6) ------------------------------------------------------
    // The Modes in the global order laid out into Group columns of 12 rows, paged 8 columns at a time, with the
    // highlight's navigation and the hit test. No registry, no Panel: ModeBrowser builds one from the registry
    // (registered()); ui.browsers builds others from synthetic lists to check wrapping, paging and navigation, which
    // eight registered Modes cannot reach. Built once (it copies the names); every query is allocation-free.
    class ModeGrid
    {
    public:
        struct Item                                              // one Mode, as the caller gives it
        {
            uint8_t          slot = 0;
            fcdsp::Group     group = fcdsp::Group::other;        // a value past `other` counts as `other`
            std::string_view name;                               // copied (at most kNameBytes − 1 bytes)
            std::string_view spec;                               // not copied: must outlive the grid (descriptors do)
        };
        struct Column
        {
            fcdsp::Group group = fcdsp::Group::other;
            int part = 0;                                        // 0: the group's first column; k: its (k + 1)th
            int first = 0;                                       // its items: [first, first + count) in global order
            int count = 0;                                       // 0..kRows (0 only for an empty group's column)
            int groupCount = 0;                                  // the group's Modes over all its columns
        };

        static constexpr int kGroups     = 8;                   // fcdsp::Group enumerators
        static constexpr int kMaxItems   = fcdsp::kModeCapacity;
        static constexpr int kMaxColumns = 24;                  // >= 8 + ceil(128 / 12) − 1 (every group non-empty)
        static constexpr std::size_t kNameBytes = 48;

        ModeGrid() noexcept = default;
        explicit ModeGrid(std::span<const Item>) noexcept;      // stable-sorted by (group, slot); past kMaxItems dropped
        static ModeGrid registered() noexcept;                  // fcdsp::modeSlots() with a descriptor (never retired)

        int  size() const noexcept { return n_; }
        uint8_t slot(int i) const noexcept;                      // i in [0, size())
        fcdsp::Group group(int i) const noexcept;
        const char* name(int i) const noexcept;                  // NUL-terminated copy
        std::string_view spec(int i) const noexcept;
        int  find(int modeSlot) const noexcept;                  // the item showing `modeSlot`; -1: not listed

        int  columns() const noexcept { return nColumns_; }
        const Column& column(int k) const noexcept;              // k in [0, columns())
        int  columnOf(int i) const noexcept;
        int  rowOf(int i) const noexcept;
        int  itemAt(int column, int row) const noexcept;         // -1: no item there
        int  pages() const noexcept;                             // ceil(columns / 8), at least 1
        static int pageOf(int column) noexcept;                  // column / 8
        int  pageOfItem(int i) const noexcept;                   // 0 for i < 0

        funkgui::Rect rowRect(int i) const noexcept;             // layout::browser::rowHit on its page: hit and a11y
        int  hit(int page, funkgui::Point) const noexcept;       // the row under p on `page`; -1: none (gutters too)
        void heading(int column, char* out, std::size_t cap) const noexcept;   // "FET  3", "VCA (2)", "OTHER  0"

        // The item a navigation key moves the highlight from `from` to (up, down, left, right, home, end, pageUp,
        // pageDown); any other key, or a move past an end, returns `from`. from < 0 starts at the first item (End: the
        // last). -1 only when the grid is empty.
        int  step(int from, funkgui::Key) const noexcept;
        // The item on `page` nearest `from`'s screen column, at from's row (clamped); -1 when that page has no item.
        int  onPage(int from, int page) const noexcept;
        // The first item at or after `from` (wrapping) whose name starts with `prefix` (ASCII case-insensitive); -1.
        int  typeAhead(int from, std::string_view prefix) const noexcept;

    private:
        int n_ = 0;
        int nColumns_ = 0;
        std::array<uint8_t, kMaxItems> slot_{};
        std::array<fcdsp::Group, kMaxItems> group_{};
        std::array<std::array<char, kNameBytes>, kMaxItems> name_{};
        std::array<std::string_view, kMaxItems> spec_{};
        std::array<int16_t, kMaxItems> column_{};
        std::array<int16_t, kMaxItems> row_{};
        std::array<Column, kMaxColumns> columns_{};
    };

    class ModeBrowser final : public SubView
    {
    public:
        explicit ModeBrowser(PanelContext&);

        ModeBrowser(const ModeBrowser&) = delete;
        ModeBrowser& operator=(const ModeBrowser&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U5 additions ---------------------------------------------------------------------------------------------
        // A browser over a given grid (the constructor above uses ModeGrid::registered()): ui.browsers drives one over a
        // synthetic list to reach the pager, the wheel and the page crossings, which eight registered Modes cannot.
        ModeBrowser(PanelContext&, const ModeGrid&);

        // the SubView input the browser takes
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        void doubleClick(const funkgui::PointerEvent&) override;
        bool wheel(const funkgui::WheelEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;
        uint32_t a11yRevision() const override;

    private:
        bool isOpen() const noexcept;                            // the Panel's overlay is this browser
        void sync();                                             // a fresh opening: the highlight on the current Mode
        void reset();
        int  currentSlot() const noexcept;                       // the resolved Mode this frame
        int  pagerAt(funkgui::Point) const noexcept;             // -1 ‹, +1 ›, 0 none (enabled or not)
        bool pagerEnabled(int dir) const noexcept;
        void setPage(int page);                                  // clamped; the highlight follows onto the page
        void moveHighlight(int item);                            // from the keyboard: ring on, page follows
        void typeChar(char32_t);
        void commit(int item, bool withDefaults, bool close);
        void close();
        void rebuildSpec(int item);                              // spec_ for the footer

        PanelContext& ctx_;
        ModeGrid grid_;
        int  page_ = 0;
        int  highlight_ = -1;                                    // the row Return commits
        int  hover_ = -1;                                        // the row under the pointer
        int  hoverPager_ = 0;
        int  pressed_ = -1;                                      // the row pressed (commits on release inside)
        bool armed_ = false;
        bool pressAlt_ = false;
        bool keyboard_ = false;                                  // the highlight came from the keyboard: focus ring
        bool needsReset_ = true;                                 // closed since the last reset
        double lastTick_ = -1.0;                                 // panel time of the last tick (gap: a fresh showing)
        std::array<char, 32> typed_{};                           // the type-ahead buffer (ASCII, upper case)
        std::size_t typedLen_ = 0;
        double typedAt_ = -1.0;                                  // panel time of the last type-ahead key
        float  wheelAcc_ = 0.0f;
        double wheelLast_ = -1.0;
        uint32_t revision_ = 0;                                  // bumps when the shown page changes
        int  specItem_ = -1;                                     // the item spec_ was built for
        char spec_[256]{};

        // ---- S13 H1a additions: the browser in the Panel's Tab order (see "Keyboard focus" above) -------------------
        void takeFocus();                                        // the Panel focus onto the highlighted row, ring shown
        void followFocus();                                      // Tab moved the Panel focus: the highlight follows it
        int  focusedPager() const noexcept;                      // -1 ‹ or +1 › holds the shown focus; 0 otherwise
        uint32_t focusSeen_ = 0;                                 // the Panel focus as this browser last saw it
    };
}
