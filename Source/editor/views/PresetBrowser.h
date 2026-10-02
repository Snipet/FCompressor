// Source/editor/views/PresetBrowser.h — the PresetBrowser sub-view (02 §6.3, §8.9). Class declaration frozen at FZ4; U6 (S12) completes
// it: the preset overlay at layout::kOverlay over PresetAccess: factory and user rows,
// categories, save-as (LineEdit), menus and file choosers (the host's: HostServices). Drawn while the Panel's overlay
// fade is > 0.
//
// U6 (S12) behaviour, where 02 is silent (U6 handoff). HR's preset browser (PresetPanel.cpp) in the Mode browser's frame
// (ModeBrowser.h): the same ground (kOverlay grown 8 px left and right, 4 px up and down), hairlines, headings (kCaption
// at y 72), rows (kLabel from y 92 on a 20 px pitch, hit {x, y − 4, w, 20}), current bar (2×12 ink100 at x − 6) and
// inks, so the two overlays read as one design.
// - Left, x 40–170: the filter column "SHOW": ALL, FACTORY, USER, then every category of the list (case-insensitive,
//   sorted), each with its count, kCaption on an 18 px pitch (6 px more after USER); the chosen one ink100 with a 2×8 bar
//   at x 34. It stays chosen while the editor is open. More categories than fit (9) scroll with the wheel over the column,
//   a row per 18 px of trackpad travel or 3 rows a wheel notch.
// - Right, x 186–920 (a hairline at x 180 between): the rows of the chosen filter in PresetAccess order (the factory bank,
//   then the user presets by name: the order ‹ › step through), 11 at a time, with the name (kLabel, ellipsised to
//   300 px), the category and the Mode's name (kMicro ink52 at x 520 and x 640) and FACTORY / USER (kMicro ink32,
//   right-aligned to x 908); a 2 px ink32 scroll thumb at x 916 when the rows do not fit. The list scrolls by the pixel
//   (ADR-84): a trackpad moves it 1:1 with the fingers (at the UI zoom) in the system's direction, natural scrolling
//   included, with the system's momentum; a wheel notch glides it 3 rows (τ 0.05 s); keys and the selection bring a row
//   into view at once. A row cut by the list's edge (y 88–308) is drawn clipped (Canvas::pushClip, FunkGui v0.9.0; the
//   clip's y edges snapped to device px, ADR-93) and listed, hit and focused like the others. The selected row (the
//   one Return and the actions take) has an ink16 fill; the current preset has the bar and its name in ink100; a
//   hovered name is ink100, a pressed one accent.
// - Bottom, y 324–340 (a hairline at y 316 above): the status line at x 40 (the count, "33 PRESETS · 9 SHOWN", ink32; a
//   message for 3 s of panel time after an action, ink70; the delete confirmation, ink100) and the actions, text cells
//   right-aligned to x 920 (kCaption; ink52, hover ink100, accent while pressed, ink16 when not available): SAVE (P3c),
//   SAVE AS, RENAME, DELETE, IMPORT, EXPORT. While a name is typed they are SAVE (or RENAME) and CANCEL.
// - Recall: a click loads the row (one PresetAccess::apply on a release inside it; dragging off cancels) and keeps the
//   browser open, so the sound and the slots are heard and seen landing (HR); a double-click loads once and closes;
//   ↑ ↓ PageUp PageDown Home End move the selection and load it (HR: "the list is for listening"); Return loads the
//   selection and closes; ← → change the filter; typed characters jump to the first row whose name starts with them
//   (1 s buffer, no load). A row that is current and unmodified is never applied again.
// - Save as (SAVE AS, the strip's SAVE, the menus, a11y): the heading line becomes "SAVE AS [name] IN <CATEGORY>", a
//   LineEdit pre-filled with the current preset's name, selected (typing replaces it; Init and "no preset" start empty),
//   at most 40 characters, drawn upper case with a steady accent caret and an accent rule. The category is the chosen
//   filter's when that is a category, else the current preset's (none for Init); the category word opens a menu of
//   the categories (the host's). The status line says what the store will do: "TAKEN: IT WILL BE SAVED AS 'X 2'" when
//   the name is taken (PresetStore's unique-name rule, factory names included). Return (or SAVE) calls
//   PresetAccess::saveAs once; an empty name is refused before the call. A save started from the strip closes the
//   browser after it succeeds; one started here keeps it open on the new row.
// - Save (SAVE, a11y; P3c, S12.5, S12 lead revision 11): the strip's SAVE, here. With a user preset current it saves
//   over it (one PresetAccess::overwrite of the current row, no dialog), whichever row is selected, and says "SAVED
//   'MY BUS'"; the browser stays open and the selection stays. With a factory preset current, or none, it is SAVE AS.
//   An overwrite the processor refuses says "COULD NOT SAVE OVER 'MY BUS'" and starts a save as, so the sound is never
//   lost. SAVE AS stays beside it for a user preset (a copy under a new name).
// - Rename (RENAME, the row menu, a11y; user rows only): the row's name becomes the LineEdit in place. Return (or the
//   RENAME cell) calls PresetAccess::rename once; a name another preset has is refused before the call ("'X' IS TAKEN").
// - An edit owns the keyboard (LineEdit's keys; Cmd and Ctrl chords stay the host's); Esc cancels it and keeps the
//   browser open (Panel offers the open browser Esc first). A click on the field keeps editing; a click anywhere else in
//   the browser confirms a rename (as Finder) and cancels a save as (nothing is created without Return); a click outside
//   the browser closes it (the Panel), which drops the edit. A failed save or rename keeps the edit open with the
//   message.
// - Delete (DELETE, Delete or Backspace, user rows only): the first press arms it for 3 s ("PRESS DELETE AGAIN TO DELETE
//   'X'", DELETE reads CONFIRM in accent); the second calls PresetAccess::remove once. The second press of a double-click
//   never counts (no action cell fires twice from one double-click). The row menu's Delete is the confirmation itself.
//   The selection moves to the next row.
// - Import: IMPORT asks the host for a file chooser (HostServices::chooseFiles: *.fcmppreset, several files); a preset
//   file dropped on the panel imports through Panel::filesDropped → filesDropped(). Each file is one
//   PresetAccess::importFile; the browser opens on the first new row; a single imported file is also loaded (HR: one
//   file is a request to hear it). Export: EXPORT (or the row menu) asks for a save chooser that starts at the
//   preset's name (the host makes it a legal file name and gives the result the extension) and calls
//   PresetAccess::exportFile once. A host that reports no hostservice::fileChooser has IMPORT and EXPORT (the cells and
//   the menu items) disabled; menus are not gated.
// - Menus (a popup click on a row, or on the list's background; a11y showMenu): the host's (HostServices::showMenu; web
//   Sprint C, ADR-93) in the product's Theme, anchored on the row in the Panel's own px: Load, Save As…, Rename…,
//   Export…, Import…, Delete (Rename and Delete on user rows only).
// - PresetAccess is read when revision() moves (once per tick and before acting on input), into rows this view owns, so
//   draw() allocates nothing and a row's index is re-found by its uuid before any call (another process may have
//   changed the list in between).
// - A11y: rows are listItems (title = the name; description = category, Mode, factory or user; checked = current; press
//   = the click's load, focus selects, showMenu the menu), the filters a radioGroup "Show", the actions buttons, the
//   typed name a staticText "New preset name" / "New name" (value = the text), the status a staticText. Only the rows
//   shown are listed; a11yRevision() bumps when the list, the filter, the scroll or the edit changes.
// - Keys the browser does not use (Tab; Esc when there is nothing to cancel) are the Panel's; Cmd and Ctrl chords go to
//   the host.
// - Keyboard focus (S13 H1a): while the browser is open it is the Panel's whole Tab order (Panel::composeFocusOrder):
//   the SHOW filters (one stop), the rows shown, then the action cells that are available; while a name is typed, the
//   category (save as) and the edit's two cells. Opened from the keyboard, it takes the focus onto the selected row;
//   the keys that move the selection (arrows, type-ahead) move the focus with it, and Tab onto a row selects it (as an a11y focus does,
//   without loading). On the filters, ← → ↑ ↓ Home End choose the filter; on an action cell, Return and Space press it;
//   while a name is typed, Return presses the focused CANCEL or category (the typing keeps every other key). A focused
//   stop that goes away (an edit starts or ends, a row scrolls out or is deleted) hands the focus to the selection. The
//   ring is drawn on the focused stop. Closing gives the focus back to its opener (Panel).
//
// The members below the FZ4 declarations are additions (SubView overrides with defaults, the actions shared by pointer,
// keyboard, menus, drops and a11y, and private state; P3c: kSaveLocal and SAVE's private state); no FZ4 declaration
// changed.
#pragma once

#include "editor/SubView.h"

#include "FcmpProduct.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/text/LineEdit.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fcmp::ui
{
    class PresetBrowser final : public SubView
    {
    public:
        explicit PresetBrowser(PanelContext&);

        PresetBrowser(const PresetBrowser&) = delete;
        PresetBrowser& operator=(const PresetBrowser&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U6 additions -------------------------------------------------------------------------------------------
        ~PresetBrowser() override;                               // a menu or chooser callback still held does nothing

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
        bool wantsFullRate() const override;                     // ADR-84: while a wheel notch glides

        // A preset file's extension: FunkPresets' ProductConfig of the processor (".fcmppreset"), taken from the
        // generated FcmpProduct.h, the product's one source of constants (S13 H1a; it was a literal here).
        static constexpr const auto& kFileExtension = product::kPresetExtension;

        // Files dropped on the panel (Panel::filesInterest / filesDropped): interested when any path ends in
        // kFileExtension (ASCII case-insensitive); a drop imports those and ignores the rest (see above).
        bool filesInterest(const std::vector<std::string>& paths) const;
        void filesDropped(const std::vector<std::string>& paths);

        // The actions of the menus, the action cells, the keys and a11y, by PresetAccess index (−1: the list's
        // background, for the commands that take no row). A row that is gone does nothing.
        enum class Command : uint8_t { load = 1, saveAs, rename, exportFile, importFiles, remove };
        struct MenuItem
        {
            Command     command = Command::load;
            const char* label = "";
            bool        enabled = true;
            bool        separatorBefore = false;
        };
        int  menu(int index, std::span<MenuItem> out) const;    // the context menu's items; returns how many
        void run(Command, int index);                            // choosing one (exportFile, importFiles: a chooser)
        bool exportTo(int index, std::string_view path);         // the export chooser's result: one exportFile call

        // a11y locals: a11yId(ViewIndex::presetBrowser, local)
        static constexpr uint32_t kEditLocal     = 0x01;        // the typed name (staticText)
        static constexpr uint32_t kStatusLocal   = 0x02;        // the status line (staticText)
        static constexpr uint32_t kCategoryLocal = 0x03;        // the save-as category (button: its menu)
        static constexpr uint32_t kSaveAsLocal   = 0x10;        // action buttons
        static constexpr uint32_t kRenameLocal   = 0x11;
        static constexpr uint32_t kDeleteLocal   = 0x12;
        static constexpr uint32_t kImportLocal   = 0x13;
        static constexpr uint32_t kExportLocal   = 0x14;
        static constexpr uint32_t kSaveLocal     = 0x15;        // P3c: SAVE (over the current user preset)
        static constexpr uint32_t kCommitLocal   = 0x18;        // while typing: SAVE / RENAME
        static constexpr uint32_t kCancelLocal   = 0x19;        //               CANCEL
        static constexpr uint32_t kFiltersLocal  = 0x20;        // the radioGroup "Show"
        static constexpr uint32_t kFilterLocal0  = 0x40;        // its radioButtons: 0x40 + filter (at most 0xBF)
        static constexpr uint32_t kRowLocal0     = 0x1000;      // rows: 0x1000 + PresetAccess index

    private:
        enum class Action : uint8_t { saveAs, rename, remove, importFiles, exportFile, commit, cancel, save };
        enum class Edit : uint8_t { none, saveAs, rename };
        enum class Zone : uint8_t { none, row, filter, action, field, category, list, column, body };
        struct Hit
        {
            Zone zone = Zone::none;
            int  index = -1;                                     // row: into shown_; filter: into filters_; action
        };
        struct Entry                                             // one PresetAccess row, as drawn
        {
            int         index = -1;                              // its PresetAccess index at the last read
            std::string uuid, name, category;                    // printable (a11y, edits, messages)
            std::string shownName, shownCategory, mode;          // upper case (drawn); mode: the Mode's name
            std::string categoryKey;                             // lower case (filters)
            bool        factory = false;
        };
        struct Filter
        {
            enum class Kind : uint8_t { all, factory, user, category };
            Kind        kind = Kind::all;
            std::string label;                                   // "ALL", "FACTORY", "USER", "BUS"
            std::string key;                                     // a category's lower-case key
            int         count = 0;
        };
        struct Cell
        {
            Action        action = Action::saveAs;
            const char*   label = "";
            funkgui::Rect rect{};
        };

        // model
        bool isOpen() const noexcept;                            // the Panel's overlay is this browser
        void sync();                                             // a fresh opening resets; then refresh()
        void reset();
        void refresh(bool force = false);                        // re-read PresetAccess when revision() moved
        void rebuildShown();
        bool inFilter(const Entry&, const Filter&) const noexcept;
        int  entryOf(std::string_view uuid) const noexcept;      // into entries_; -1
        int  indexOf(std::string_view uuid);                     // the PresetAccess index now (re-read); -1
        const Entry* selectedEntry() const noexcept;
        int  shownOf(int entry) const noexcept;                  // into shown_; -1
        void select(int entry, bool scrollTo);                   // scrollTo: brought into view at once
        void setFilter(int filter);
        void scrollFilters(int rows);

        // ADR-84: the list's offset in logical px (0 … maxScroll()); rows s with any part in view are firstShown() …
        // lastShown() (-1 … -2 when none).
        float maxScroll() const noexcept;
        void  scrollToPx(float px, bool glide);                  // glide: eased by tick(); else at once
        int   firstShown() const noexcept;
        int   lastShown() const noexcept;

        // actions
        void load(int entry, bool closeAfter);                   // one apply, unless current and unmodified
        void moveSelection(int to);                              // keys: select and load
        void beginSaveAs();
        void saveCurrent();                                      // SAVE (P3c): over the current user preset
        void beginRename(int entry);
        void commitEdit();
        void cancelEdit();
        bool editValid() const;                                  // the commit is available
        void armOrRemove(int entry);
        void removeEntry(int entry);
        void importFiles(const std::vector<std::string>& paths);
        void chooseImport();
        void chooseExport(int entry);
        void showMenu(int entry, funkgui::Rect anchor);
        void showCategoryMenu();
        void runAction(Action);
        bool actionEnabled(Action) const;
        void close();
        void flash(std::string_view message);
        void typeChar(char32_t);

        // drawing and hit testing
        std::span<const Cell> cells() const noexcept;            // the action cells shown now
        Hit  hitAt(funkgui::Point) const noexcept;
        funkgui::Rect rowRect(int shownIndex) const noexcept;    // into shown_, on screen
        funkgui::Rect filterRect(int filter) const noexcept;     // empty when scrolled out
        void statusText(char* out, std::size_t cap, int& kind) const;   // 0 count, 1 message, 2 confirm, 3 hint
        void noteEdit();                                         // the typed name changed: editUnique_, editTaken_
        std::string uniqueName(std::string_view wanted) const;   // PresetStore's rule over entries_
        bool nameTaken(std::string_view name, std::string_view ignoreUuid) const;
        void rebuildSpec(Hit);                                   // spec_ for the item under the pointer
        float caretX(float x0) const;                            // the caret's x for the edit's text at x0

        PanelContext& ctx_;

        // PresetAccess as of the last revision() read
        bool     valid_ = false;
        uint32_t seenRev_ = 0;
        int      current_ = -1;                                  // PresetAccess index
        bool     modified_ = false;
        std::string currentUuid_;
        std::vector<Entry>  entries_;
        std::vector<Filter> filters_;
        std::vector<int>    shown_;                              // entries_ in the chosen filter, in order

        // browsing
        int  filter_ = 0;                                        // into filters_
        Filter::Kind filterKind_ = Filter::Kind::all;           // ... kept by identity across list changes
        std::string  filterKey_;
        int  filterScroll_ = 0;                                  // the first category shown
        std::string selectedUuid_;
        int  selected_ = -1;                                     // into entries_
        float scrollPx_ = 0.0f;                                  // ADR-84: the list's offset drawn, logical px
        float scrollTo_ = 0.0f;                                  // ... and where a notch glides it (== when at rest)
        Hit  hover_;
        Hit  pressed_;
        bool armed_ = false;                                     // the pressed row or cell fires on a release inside
        float wheelAcc_ = 0.0f;                                  // the filter column's trackpad travel, px
        std::array<char, 32> typed_{};                           // type-ahead (ASCII, upper case)
        std::size_t typedLen_ = 0;
        double typedAt_ = -1.0;

        // editing
        Edit edit_ = Edit::none;
        funkgui::text::LineEdit line_;
        std::string editUuid_;                                   // the renamed row
        std::string saveCategory_;
        bool closeAfterSave_ = false;
        std::string editUnique_;                                 // save as: the name the store will give it
        bool editTaken_ = false;                                 // rename: another preset has the typed name

        // delete confirmation and messages (panel time)
        std::string armedUuid_;
        double armedUntil_ = -1.0;
        std::string message_;
        double messageUntil_ = -1.0;

        // opening and closing
        bool   needsReset_ = true;
        bool   tickedOpen_ = false;                              // ticked since this opening
        double lastTick_ = -1.0;
        uint32_t revision_ = 1;                                  // a11y

        // the action cells (fixed; widths from the atlas at construction)
        std::array<Cell, 6> browseCells_{};                      // P3c: SAVE first
        std::array<Cell, 2> editCells_{};

        // the footer line under the hand (rebuilt every tick while the pointer is on an item: it names the selection)
        char spec_[256]{};

        // the host's menus and file choosers (HostServices) answer later, perhaps after this view
        std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // callbacks check it: this view still exists

        // ---- S13 H1a additions: the browser in the Panel's Tab order (see "Keyboard focus" above) -------------------
        static uint32_t localOf(Action) noexcept;                // an action cell's a11y local
        uint32_t focusLocal() const noexcept;                    // the shown Panel focus's local in this browser; 0
        void takeFocus(uint32_t local);                          // the Panel focus onto one of this browser's stops
        void followFocus();                                      // Tab moved the Panel focus onto a row: select it
        void rehomeFocus();                                      // the focused stop went away: onto the selection
        uint32_t rowLocal(int entry) const noexcept;             // kRowLocal0 + its PresetAccess index; 0: none
        uint32_t focusSeen_ = 0;                                 // the Panel focus as this browser last saw it
    };
}
