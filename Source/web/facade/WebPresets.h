// Source/web/facade/WebPresets.h: the browser demo's PresetAccess (web Sprint C, ADR-93): what the plugin's
// (Source/plugin/Presets.cpp over FunkPresets' PresetManager and PresetStore) does for the preset strip and browser,
// without JUCE, a database or a file. Tools/probes/plugin/webpresets.cpp holds every row and operation to a real
// Processor's presets().
//
// The list: the factory bank compiled into the build (plugin/portable/FactoryData.h) in bank order, Init at 0, then
// the SESSION's user presets, which live in this object and go with the page. User rows are in the store's name order
// (FunkGui src/presets/PresetStore.cpp: ORDER BY sort_key, name_key, uuid): the name case-folded with every run of
// digits padded to ten places ("A 2" before "a 10"), then the folded name, then the uuid. The fold here is ASCII's
// (the store's is Unicode's, through the platform): the editor's font and its name field are ASCII.
// Row::modeKey is the key of the Mode the preset loads: its modeId resolved (a retired Mode's successor; unknown:
// clean).
//
// The current preset is an identity (uuid, name, category) and a baseline: the 22 raw values the parameters held when
// it became current, and its Mode. A fresh facade is Init, unmodified.
//   apply(i)          one batch: the Mode first (Presets.cpp writeMode), then the 22 values in the host's layout order,
//                     each written through the host map unless the parameter already reports that value bit for bit
//                     (PresetManager::apply's skip rule: so a preset's raw value is the map's round trip of the
//                     stored one, or what was there); the baseline is what the parameters then hold; the identity is
//                     set BEFORE the batch ends, because the edit history reads it there; the end raises the snap.
//   modified()        PresetManager::isModified (a raw value further than half a step from the snapped baseline: half
//                     an index for the integer parameters, a millionth of the span for the others), or the live Mode
//                     is not the baseline's.
//   step(d)           apply(current + d), wrapping; from no preset, +1 is the first row and -1 the last.
//   currentUuid(), restoreCurrent(uuid)   the identity undo, redo and A/B carry: a listed preset becomes current with
//                     its STORED values as the baseline and no parameter moves; "" or an unknown uuid leaves an
//                     untitled preset whose baseline is the live values and Mode; the uuid already current: nothing.
//   saveAs(n, c)      the live values and Mode as a new user preset: the trimmed name, refused when empty, made unique
//                     against every row ignoring case ("Name 2", and a taken "Name 2" continues at "Name 3"); it
//                     becomes current, unmodified.
//   rename, remove, overwrite   user rows only, with the store's rules (Presets.cpp's header lists them): a name taken
//                     by another row or empty is refused; removing the current preset leaves it untitled with the
//                     same baseline; overwrite stores the live values and Mode in the row and makes it current.
//   importFile, exportFile      keep PresetAccess's refusing defaults: there are no files (the editor shows the two
//                     cells disabled when the host has no file chooser).
// A refused call changes nothing and holds revision(); every other change moves it, and so does modified() changing
// (seen at the next revision() call, as the plugin's).
//
// Message thread only. Portable C++ (lint web.facade).
#pragma once

#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fcmp::web
{
    class WebFacade;

    class WebPresets final : public PresetAccess
    {
    public:
        explicit WebPresets(WebFacade&);                         // reads the facade's values: construct it after them

        WebPresets(const WebPresets&) = delete;
        WebPresets& operator=(const WebPresets&) = delete;

        int         count() const override;
        Row         row(int index) const override;               // a default Row for an index out of range
        int         current() const override;
        bool        modified() const override;
        uint32_t    revision() const override;
        void        apply(int index) override;
        void        step(int delta) override;
        bool        saveAs(std::string_view name, std::string_view category) override;
        bool        rename(int index, std::string_view newName) override;
        bool        remove(int index) override;
        bool        overwrite(int index) override;
        std::string currentUuid() const override;
        void        restoreCurrent(std::string_view uuid) override;

    private:
        using Values = std::array<float, fcdsp::kNumModeParams>;

        struct Sound                                             // what a preset stores
        {
            Values      values{};                                // plain host units, values[fcdsp::idx(pid)]
            std::string modeId;                                  // the Mode's key as saved ("": none, loads clean)
        };
        struct User
        {
            std::string uuid, name, category;
            Sound       sound;
        };
        struct Current                                           // PresetManager::current()
        {
            std::string uuid, name, category;                    // uuid "": untitled
            Sound       baseline;
        };

        int    factoryCount() const noexcept;
        int    userIndexOf(std::string_view uuid) const noexcept;        // into users_, or -1
        User*  userAt(int index) noexcept;                       // nullptr: a factory row, or no row
        Values liveValues() const;
        Sound  liveSound() const;                                // the live values and the live Mode's key
        bool   nameTaken(std::string_view name, std::string_view ignoreUuid) const;
        std::string uniqueName(std::string_view wanted) const;
        void   sortUsers();
        void   applySound(const Sound&, std::string_view uuid, std::string_view name, std::string_view category);
        void   setCurrent(Current);                              // identity and baseline; no parameter moves

        WebFacade&        facade_;
        std::vector<User> users_;                                // the session's presets, in list order
        Current           current_;
        std::uint64_t     nextUuid_ = 1;
        std::uint64_t     changes_ = 0;                          // every list, identity or baseline change
        mutable std::uint64_t seenChanges_ = 0;
        mutable bool          seenModified_ = false;
        mutable uint32_t      revision_ = 1;
    };
} // namespace fcmp::web
