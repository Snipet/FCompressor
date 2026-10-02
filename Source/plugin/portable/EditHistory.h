// Source/plugin/portable/EditHistory.h — undo/redo of the editor's edits and the A/B compare (v1.2, ADR-91): the EditAccess the
// processor (and a probe's FakeFacade) hands the preset strip. Plain C++ over a small Host interface, so the real
// processor and the probes run the same code.
//
// Recording. The processor's ports and its batches bracket every write the editor makes: a gesture (a drag, a tap, a
// wheel burst) and a batch (a preset, a Mode change, a multi-write) each call began/ended; nested brackets merge. At the
// outermost begin the history reads the tracked raw values and the preset's uuid; at the outermost end it reads them
// again, and the parameters that changed become ONE entry (their before and after raw values, bit for bit, and both
// preset uuids). A gesture-only bracket takes only the parameters whose gesture it saw, so host automation of another
// parameter meanwhile is never recorded, and undo never reverts it; a batch takes every tracked parameter. Brackets
// off the message thread (a host loading a session on its own thread) are not recorded. Nothing changed: no entry.
//
// Tracked: the 22 Mode-filtered parameters, `mode`, `extkey` and `output`. Not tracked: `quality` and `labudget` (they
// change latency), `bypass`, `listen` and `delta` (the host's and the monitoring latches').
//
// Undo writes the entry's before values (redo its after values) as exact raw writes, inside one batch, each announced
// to the host as a gesture, then puts the preset identity back (PresetAccess::restoreCurrent); nothing it writes is
// recorded. The processor's Host ends that batch without the engine snap, so the writes ramp like any edit. It is
// refused while a bracket is open (the editor closes an open wheel burst first). Save As is not an entry: it names the
// current sound, so the steps either side of it take the new uuid (undo, then redo, lands on the saved preset). At most kCapacity entries; a new entry drops the redo branch.
//
// A/B. Two slots, each a sound (the tracked values) and a preset uuid; A is active at first and B unused. Selecting the
// other slot stores the current sound into the active one, starts B as a copy of A the first time, writes the selected
// slot's sound like an undo, and pushes an "A/B" entry whose undo and redo switch back and forth (the sound left behind
// is stored first, so no edit is lost). copySlot() puts the current sound into the other slot. The inactive slot is
// saved in the session (<COMPARE>, State.h's hooks) once B has been used, and comes back with it.
//
// Sessions. Every accepted state load hands the history its compare state (setLoadedCompare, any thread, under a
// mutex): the slots are the loaded ones at once, so a save that follows (compareForSave, any thread, the same mutex)
// writes them back even when no editor ever opened. The host's loadSerial() bumps after the load; at its next
// message-thread call the history sees it and drops every entry. A bracket still open across a load (a wheel burst)
// records nothing: the load started a new history.
#pragma once

#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace fcmp
{
    class EditHistory final : public EditAccess
    {
    public:
        struct Host
        {
            virtual ~Host() = default;
            virtual float       raw(fcdsp::Pid) const = 0;
            virtual void        write(fcdsp::Pid, float plain) = 0;   // exact raw value, announced as one gesture
            virtual void        beginBatch() = 0;
            virtual void        endBatch() = 0;
            virtual std::string presetUuid() const = 0;
            virtual void        restorePreset(const std::string& uuid) = 0;
            virtual uint32_t    loadSerial() const = 0;               // bumps on every state load
            virtual bool        onMessageThread() const = 0;
        };

        struct Compare                                   // the inactive slot, for <COMPARE>
        {
            int         active = 0;                      // the slot the live sound is (0 = A)
            bool        used = false;                    // B has been used: write <COMPARE>
            std::array<float, fcdsp::kNumParams> values{};   // the inactive slot's raw values (tracked ones)
            std::string preset;                          // its preset uuid
        };

        static constexpr std::size_t kCapacity = 100;
        static bool tracked(fcdsp::Pid) noexcept;

        explicit EditHistory(Host&);

        // Recording (see the top). Message thread; ignored off it and while undo, redo or a switch writes.
        void gestureBegan(fcdsp::Pid);
        void gestureEnded(fcdsp::Pid);
        void batchBegan();
        void batchEnded();

        // EditAccess
        bool        canUndo() const override;
        bool        canRedo() const override;
        bool        undo() override;
        bool        redo() override;
        const char* undoName() const override;
        const char* redoName() const override;
        int         compareSlot() const override;
        bool        slotUsed(int slot) const override;
        void        selectSlot(int slot) override;
        void        copySlot() override;
        uint32_t    revision() const override;

        // Sessions (any thread).
        Compare compareForSave() const;
        void    setLoadedCompare(const Compare&);        // a load's <COMPARE>, or its absence (used false)

    private:
        using Values = std::array<float, fcdsp::kNumParams>;
        struct Change
        {
            fcdsp::Pid pid;
            float      before, after;
        };
        struct Entry
        {
            std::vector<Change> changes;
            std::string presetBefore, presetAfter;
            std::string name;
            bool abSwitch = false;                       // undo selects abFrom, redo abTo
            int  abFrom = 0, abTo = 1;
        };
        struct Slot
        {
            Values      values{};
            std::string preset;
        };

        void   sync() const;                             // a state load since the last call: drop every entry
        Values capture() const;
        void   began(bool batch, fcdsp::Pid);
        void   ended();
        void   push(Entry&&);
        void   applyValues(const Values&, const std::string& preset);   // exact writes of the differing tracked values
        void   applyChanges(const Entry&, bool undo);
        void   adoptIdentity();                          // a Save As since: the current sound's steps take its uuid
        void   switchTo(int slot);                       // store the current sound, then load `slot` (not recorded)

        Host& host_;
        // What sync() may reset lives in `mutable` members: canUndo() and friends are const.
        mutable std::vector<Entry> entries_;
        mutable std::size_t        cur_ = 0;             // entries_[0, cur_) are undoable, [cur_, size) redoable
        mutable uint32_t           seenLoad_ = 0;
        mutable uint32_t           revision_ = 1;
        // slots_, used_ and active_ only under compareMutex_: a load (setLoadedCompare) or a save (compareForSave) may
        // run on a host thread.
        mutable std::mutex         compareMutex_;
        std::array<Slot, 2>        slots_{};
        std::array<bool, 2>        used_{ true, false };
        int                        active_ = 0;
        int         depth_ = 0;                          // open brackets
        uint32_t    loadAtBegin_ = 0;                    // loadSerial() at the outermost begin
        bool        batchSeen_ = false;
        bool        applying_ = false;
        Values      before_{};
        std::string presetBefore_;
        std::bitset<fcdsp::kNumParams> touched_{};
    };
}
