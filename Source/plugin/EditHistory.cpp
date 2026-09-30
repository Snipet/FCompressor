// Source/plugin/EditHistory.cpp — undo/redo and the A/B compare (see EditHistory.h).
#include "plugin/EditHistory.h"

#include "fcdsp/params/HostParams.h"

#include <bit>
#include <cctype>
#include <utility>

namespace fcmp
{
    namespace
    {
        using fcdsp::Pid;

        bool sameBits(float a, float b) noexcept { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

        // The entry's name: the one parameter's universal name, else MODE or PRESET when those moved, else a count.
        std::string nameOf(const std::vector<std::pair<Pid, bool>>& changed, bool presetMoved)
        {
            bool mode = false;
            for (const auto& c : changed)
                mode = mode || c.first == Pid::mode;
            if (presetMoved && changed.size() != 1)
                return "PRESET";
            if (mode)
                return "MODE";
            if (changed.size() == 1)
            {
                std::string n = fcdsp::kHostParams[fcdsp::idx(changed.front().first)].name;
                for (char& ch : n)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                return n;
            }
            return changed.empty() ? std::string("PRESET") : std::to_string(changed.size()) + " CHANGES";
        }
    }

    bool EditHistory::tracked(Pid p) noexcept
    {
        return fcdsp::idx(p) < fcdsp::kNumModeParams || p == Pid::mode || p == Pid::extkey || p == Pid::output;
    }

    EditHistory::EditHistory(Host& host) : host_(host), seenLoad_(host.loadSerial())
    {
        entries_.reserve(kCapacity);
    }

    // ---- sessions ---------------------------------------------------------------------------------------------------

    void EditHistory::sync() const
    {
        if (!host_.onMessageThread())
            return;
        const uint32_t load = host_.loadSerial();
        if (load == seenLoad_)
            return;
        seenLoad_ = load;
        entries_.clear();
        cur_ = 0;
        ++revision_;
    }

    EditHistory::Compare EditHistory::compareForSave() const
    {
        const std::lock_guard<std::mutex> lock(compareMutex_);
        Compare c;
        c.active = active_;
        c.used = used_[1];
        if (c.used)
        {
            const Slot& s = slots_[static_cast<std::size_t>(active_ == 0 ? 1 : 0)];
            c.values = s.values;
            c.preset = s.preset;
        }
        return c;
    }

    void EditHistory::setLoadedCompare(const Compare& c)
    {
        const std::lock_guard<std::mutex> lock(compareMutex_);
        active_ = 0;
        used_ = { true, false };
        slots_ = {};
        if (c.used)
        {
            active_ = c.active == 1 ? 1 : 0;
            used_ = { true, true };
            Slot& inactive = slots_[static_cast<std::size_t>(active_ == 0 ? 1 : 0)];
            inactive.values = c.values;
            inactive.preset = c.preset;
        }
    }

    // ---- recording --------------------------------------------------------------------------------------------------

    EditHistory::Values EditHistory::capture() const
    {
        Values v{};
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            if (tracked(static_cast<Pid>(i)))
                v[i] = host_.raw(static_cast<Pid>(i));
        return v;
    }

    void EditHistory::began(bool batch, Pid pid)
    {
        if (!host_.onMessageThread() || applying_)       // the thread first: applying_ is the message thread's
            return;
        sync();
        if (depth_++ == 0)
        {
            before_ = capture();
            presetBefore_ = host_.presetUuid();
            loadAtBegin_ = host_.loadSerial();
            touched_.reset();
            batchSeen_ = false;
        }
        if (batch)
            batchSeen_ = true;
        else if (fcdsp::idx(pid) < fcdsp::kNumParams)
            touched_.set(fcdsp::idx(pid));
    }

    void EditHistory::ended()
    {
        if (!host_.onMessageThread() || applying_ || depth_ == 0)
            return;
        if (--depth_ > 0)
            return;
        if (host_.loadSerial() != loadAtBegin_)
            return;                                      // a load came in while it was open: the history starts over
        const Values after = capture();
        Entry e;
        std::vector<std::pair<Pid, bool>> changed;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const auto p = static_cast<Pid>(i);
            if (!tracked(p) || !(batchSeen_ || touched_.test(i)) || sameBits(before_[i], after[i]))
                continue;
            e.changes.push_back({ p, before_[i], after[i] });
            changed.emplace_back(p, true);
        }
        e.presetBefore = presetBefore_;
        e.presetAfter = host_.presetUuid();
        const bool presetMoved = e.presetBefore != e.presetAfter;
        if (e.changes.empty() && !presetMoved)
            return;
        e.name = nameOf(changed, presetMoved);
        push(std::move(e));
    }

    void EditHistory::gestureBegan(Pid p) { began(false, p); }
    void EditHistory::gestureEnded(Pid) { ended(); }
    void EditHistory::batchBegan() { began(true, fcdsp::kNoPid); }
    void EditHistory::batchEnded() { ended(); }

    void EditHistory::push(Entry&& e)
    {
        entries_.resize(cur_);                           // a new edit drops the redo branch
        if (entries_.size() == kCapacity)
            entries_.erase(entries_.begin());
        entries_.push_back(std::move(e));
        cur_ = entries_.size();
        ++revision_;
    }

    // ---- undo and redo ----------------------------------------------------------------------------------------------

    bool EditHistory::canUndo() const
    {
        sync();
        return cur_ > 0;
    }

    bool EditHistory::canRedo() const
    {
        sync();
        return cur_ < entries_.size();
    }

    const char* EditHistory::undoName() const
    {
        sync();
        return cur_ > 0 ? entries_[cur_ - 1].name.c_str() : "";
    }

    const char* EditHistory::redoName() const
    {
        sync();
        return cur_ < entries_.size() ? entries_[cur_].name.c_str() : "";
    }

    void EditHistory::applyChanges(const Entry& e, bool undo)
    {
        host_.beginBatch();
        for (const Change& c : e.changes)
            host_.write(c.pid, undo ? c.before : c.after);
        host_.endBatch();
        host_.restorePreset(undo ? e.presetBefore : e.presetAfter);
    }

    // The identity the history last left may have been renamed since, by what it does not record: Save As makes the
    // live sound a new preset (a save over another preset likewise). The sound between the steps either side of the
    // current position is that preset now, so redo comes back to it (and undo from it knows it).
    void EditHistory::adoptIdentity()
    {
        const std::string now = host_.presetUuid();
        if (cur_ > 0 && !entries_[cur_ - 1].abSwitch)
            entries_[cur_ - 1].presetAfter = now;
        if (cur_ < entries_.size() && !entries_[cur_].abSwitch)
            entries_[cur_].presetBefore = now;
    }

    bool EditHistory::undo()
    {
        sync();
        if (depth_ > 0 || cur_ == 0)
            return false;
        adoptIdentity();
        const Entry& e = entries_[cur_ - 1];
        applying_ = true;
        if (e.abSwitch)
            switchTo(e.abFrom);
        else
            applyChanges(e, true);
        applying_ = false;
        --cur_;
        ++revision_;
        return true;
    }

    bool EditHistory::redo()
    {
        sync();
        if (depth_ > 0 || cur_ >= entries_.size())
            return false;
        adoptIdentity();
        const Entry& e = entries_[cur_];
        applying_ = true;
        if (e.abSwitch)
            switchTo(e.abTo);
        else
            applyChanges(e, false);
        applying_ = false;
        ++cur_;
        ++revision_;
        return true;
    }

    // ---- A/B --------------------------------------------------------------------------------------------------------

    int EditHistory::compareSlot() const
    {
        sync();
        const std::lock_guard<std::mutex> lock(compareMutex_);
        return active_;
    }

    bool EditHistory::slotUsed(int slot) const
    {
        sync();
        const std::lock_guard<std::mutex> lock(compareMutex_);
        return slot >= 0 && slot < 2 && used_[static_cast<std::size_t>(slot)];
    }

    void EditHistory::applyValues(const Values& v, const std::string& preset)
    {
        host_.beginBatch();
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const auto p = static_cast<Pid>(i);
            if (tracked(p) && !sameBits(host_.raw(p), v[i]))
                host_.write(p, v[i]);
        }
        host_.endBatch();
        host_.restorePreset(preset);
    }

    void EditHistory::switchTo(int slot)
    {
        Slot now{ capture(), host_.presetUuid() };
        Slot next;
        {
            const std::lock_guard<std::mutex> lock(compareMutex_);
            slots_[static_cast<std::size_t>(active_)] = now;
            if (!used_[static_cast<std::size_t>(slot)])
            {
                slots_[static_cast<std::size_t>(slot)] = now;    // B starts as a copy of A
                used_[static_cast<std::size_t>(slot)] = true;
            }
            active_ = slot;
            next = slots_[static_cast<std::size_t>(slot)];
        }
        applyValues(next.values, next.preset);
    }

    void EditHistory::selectSlot(int slot)
    {
        const int from = compareSlot();                  // syncs
        if (slot < 0 || slot > 1 || slot == from || depth_ > 0)
            return;
        Entry e;
        e.abSwitch = true;
        e.abFrom = from;
        e.abTo = slot;
        e.name = "A/B";
        applying_ = true;
        switchTo(slot);
        applying_ = false;
        push(std::move(e));
    }

    void EditHistory::copySlot()
    {
        sync();
        if (depth_ > 0)
            return;
        {
            const std::lock_guard<std::mutex> lock(compareMutex_);
            const auto other = static_cast<std::size_t>(active_ == 0 ? 1 : 0);
            slots_[other] = Slot{ capture(), host_.presetUuid() };
            used_[other] = true;
        }
        ++revision_;
    }

    uint32_t EditHistory::revision() const
    {
        sync();
        return revision_;
    }
}
