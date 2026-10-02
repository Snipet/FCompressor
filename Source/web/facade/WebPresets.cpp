// Source/web/facade/WebPresets.cpp: see WebPresets.h. The models, restated rule for rule: Source/plugin/Presets.cpp
// (the list, the Mode, the identity) and FunkGui's src/presets/PresetManager.cpp (apply, isModified) and
// PresetStore.cpp (names and order); proc.webpresets compares the result with a real Processor's presets().
#include "web/facade/WebPresets.h"

#include "web/facade/HostValue.h"
#include "web/facade/WebFacade.h"

#include "plugin/portable/FactoryData.h"

#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <funkgui/params/ParamPort.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <utility>

namespace fcmp::web
{
    namespace
    {
        using fcdsp::Pid;
        using fcdsp::idx;

        constexpr std::string_view kFallbackKey = "clean";       // a preset without a usable modeId (Presets.cpp:95)

        bool sameBits(float a, float b) noexcept
        {
            return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
        }

        std::span<const factory::FactoryRow> bank() { return factory::factoryRows(); }

        // The Mode a preset loads, and the Mode a baseline stands for (Presets.cpp modeOf): its modeId, registered or
        // retired (the successor); missing or unknown: clean. nullptr only for an empty registry.
        const fcdsp::ModeSlot* modeOf(std::string_view modeId)
        {
            const fcdsp::ModeSlot* ms = fcdsp::resolveKey(modeId);
            return ms != nullptr ? ms : fcdsp::resolveKey(kFallbackKey);
        }

        std::string modeKeyOf(std::string_view modeId)
        {
            const fcdsp::ModeSlot* ms = modeOf(modeId);
            return ms != nullptr ? std::string(ms->key) : std::string();
        }

        // PresetManager's Slot::defaultPlain and halfStep (PresetManager.cpp:95-100): a value that is absent or not
        // finite loads at the first, and a parameter is modified beyond the second. The range's interval is 1 for the
        // integer parameters (juce_AudioParameterInt.cpp:48) and 0 for the others.
        float defaultPlain(Pid p) noexcept { return convertFrom0to1(p, defaultValue01(p)); }

        float halfStep(Pid p) noexcept
        {
            const fcdsp::HostParam& h = fcdsp::kHostParams[idx(p)];
            return hostKind(p) == HostKind::integer ? 0.5f * 1.0f : 1.0e-6f * (h.hi - h.lo);
        }

        // ---- names (PresetStore.cpp; ASCII where the store folds Unicode) -------------------------------------------
        bool isSpace(char c) noexcept { return c == ' ' || (c >= '\t' && c <= '\r'); }

        std::string_view trimmed(std::string_view s) noexcept    // juce::String::trim
        {
            while (!s.empty() && isSpace(s.front()))
                s.remove_prefix(1);
            while (!s.empty() && isSpace(s.back()))
                s.remove_suffix(1);
            return s;
        }

        std::string fold(std::string_view s)                     // the store's name_key: what makes two names the same
        {
            std::string out(s);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return out;
        }

        bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

        // The store's sort_key (PresetStore.cpp sortKey): folded, every run of digits without its leading zeros and
        // left-padded to ten places, so "Hall 2" comes before "Hall 10".
        std::string sortKey(std::string_view name)
        {
            const std::string f = fold(name);
            std::string out;
            for (std::size_t i = 0; i < f.size();)
            {
                if (!isDigit(f[i]))
                {
                    out += f[i++];
                    continue;
                }
                std::size_t j = i;
                while (j < f.size() && isDigit(f[j]))
                    ++j;
                while (i + 1 < j && f[i] == '0')
                    ++i;                                         // "007" is "7"; "000" is "0"
                if (j - i < 10)
                    out.append(10 - (j - i), '0');
                out.append(f, i, j - i);
                i = j;
            }
            return out;
        }
    } // namespace

    // ==== construction ===============================================================================================

    // A fresh instance is the bank's Init, its baseline the values the parameters hold (PresetManager's constructor,
    // PresetManager.cpp:104-108, with Presets.cpp's initialPreset hook).
    WebPresets::WebPresets(WebFacade& facade) : facade_(facade)
    {
        current_.baseline.values = liveValues();
        if (!bank().empty())
        {
            const factory::FactoryRow& init = bank().front();
            current_.uuid = init.uuid;
            current_.name = init.name;
            current_.category = init.category;
            current_.baseline.modeId = init.modeKey;
        }
    }

    // ==== the list ===================================================================================================

    int WebPresets::factoryCount() const noexcept { return static_cast<int>(bank().size()); }

    int WebPresets::count() const { return factoryCount() + static_cast<int>(users_.size()); }

    PresetAccess::Row WebPresets::row(int index) const
    {
        if (index < 0 || index >= count())
            return {};
        Row r;
        if (index < factoryCount())
        {
            const factory::FactoryRow& f = bank()[static_cast<std::size_t>(index)];
            r.uuid = f.uuid;
            r.name = f.name;
            r.category = f.category;
            r.modeKey = modeKeyOf(f.modeKey);
            r.factory = true;
            return r;
        }
        const User& u = users_[static_cast<std::size_t>(index - factoryCount())];
        r.uuid = u.uuid;
        r.name = u.name;
        r.category = u.category;
        r.modeKey = modeKeyOf(u.sound.modeId);
        return r;
    }

    int WebPresets::userIndexOf(std::string_view uuid) const noexcept
    {
        for (std::size_t i = 0; i < users_.size(); ++i)
            if (users_[i].uuid == uuid)
                return static_cast<int>(i);
        return -1;
    }

    WebPresets::User* WebPresets::userAt(int index) noexcept
    {
        if (index < factoryCount() || index >= count())
            return nullptr;
        return &users_[static_cast<std::size_t>(index - factoryCount())];
    }

    int WebPresets::current() const
    {
        if (current_.uuid.empty())
            return -1;                                           // untitled
        for (std::size_t i = 0; i < bank().size(); ++i)
            if (current_.uuid == bank()[i].uuid)
                return static_cast<int>(i);
        const int u = userIndexOf(current_.uuid);
        return u >= 0 ? factoryCount() + u : -1;                 // -1: removed
    }

    std::string WebPresets::currentUuid() const { return current_.uuid; }

    // ==== the live sound against the baseline ========================================================================

    WebPresets::Values WebPresets::liveValues() const
    {
        Values v{};
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = facade_.plain(static_cast<Pid>(i));
        return v;
    }

    // PresetManager::capture with Presets.cpp's captureExtra: the live values, and the effective Mode's key (the
    // identity's own stays when the registry has no Mode to name).
    WebPresets::Sound WebPresets::liveSound() const
    {
        Sound s;
        s.values = liveValues();
        s.modeId = current_.baseline.modeId;
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(facade_.currentRaw().modeSlot);
        if (ms.entry != nullptr && ms.entry->desc != nullptr)
            s.modeId = std::string(ms.key);
        return s;
    }

    // Presets.cpp modified(): PresetManager::isModified (PresetManager.cpp:178-192), or another Mode.
    bool WebPresets::modified() const
    {
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
        {
            const auto pid = static_cast<Pid>(i);
            const float v = current_.baseline.values[i];
            const float want = std::isfinite(v) ? snapToLegalValue(pid, v) : defaultPlain(pid);
            if (std::abs(facade_.plain(pid) - want) > halfStep(pid))
                return true;
        }
        const fcdsp::ModeSlot* base = modeOf(current_.baseline.modeId);
        return base != nullptr && base->slot != facade_.currentRaw().modeSlot;
    }

    uint32_t WebPresets::revision() const
    {
        const bool mod = modified();
        if (changes_ != seenChanges_ || mod != seenModified_)
        {
            seenChanges_ = changes_;
            seenModified_ = mod;
            ++revision_;
        }
        return revision_;
    }

    void WebPresets::setCurrent(Current c)
    {
        current_ = std::move(c);
        ++changes_;
    }

    // ==== apply ======================================================================================================

    // PresetManager::apply inside Presets.cpp's hooks (PresetManager.cpp:140-176; Presets.cpp:383-422), in its order:
    // the batch opens; the Mode; the 22 values in the host's layout order; the baseline and the identity; the batch
    // ends (the snap). The writes are the host's own (no gesture: a preset load is not the user holding a control).
    void WebPresets::applySound(const Sound& sound, std::string_view uuid, std::string_view name,
                                std::string_view category)
    {
        facade_.beginBatch();
        if (const fcdsp::ModeSlot* ms = modeOf(sound.modeId))
        {
            funkgui::ParamPort& mode = facade_.port(Pid::mode);
            const float norm = convertTo0to1(Pid::mode, static_cast<float>(ms->slot));
            if (mode.value01() != norm || facade_.currentRaw().modeSlot != ms->slot)
                mode.setValue01(norm);
        }
        for (const Pid pid : fcdsp::kApvtsOrder)
        {
            if (idx(pid) >= fcdsp::kNumModeParams)
                continue;                                        // not a preset parameter
            const float v = sound.values[idx(pid)];
            const float norm = convertTo0to1(pid, std::isfinite(v) ? v : defaultPlain(pid));
            funkgui::ParamPort& port = facade_.port(pid);
            // Skipped when the parameter already reports this value, bit for bit: the raw value then stays what it
            // was, which need not be the map's round trip of the preset's (Init after an undo keeps the exact default).
            if (!sameBits(norm, port.value01()))
                port.setValue01(norm);
        }
        setCurrent({ std::string(uuid), std::string(name), std::string(category), { liveValues(), sound.modeId } });
        facade_.endBatch();
    }

    void WebPresets::apply(int index)
    {
        if (index < 0 || index >= count())
            return;
        if (index < factoryCount())
        {
            const factory::FactoryRow& f = bank()[static_cast<std::size_t>(index)];
            applySound({ f.values, f.modeKey }, f.uuid, f.name, f.category);
            return;
        }
        const User& u = users_[static_cast<std::size_t>(index - factoryCount())];
        applySound(u.sound, u.uuid, u.name, u.category);
    }

    void WebPresets::step(int delta)
    {
        const int n = count();
        if (n == 0 || delta == 0)
            return;
        const int at = current();
        const int from = at < 0 ? (delta > 0 ? -1 : 0) : at;
        apply(((from + delta) % n + n) % n);
    }

    // Presets.cpp restoreCurrent (:195-225).
    void WebPresets::restoreCurrent(std::string_view uuid)
    {
        if (uuid == current_.uuid)
            return;
        if (!uuid.empty())
        {
            for (const factory::FactoryRow& f : bank())
                if (uuid == f.uuid)
                {
                    setCurrent({ f.uuid, f.name, f.category, { f.values, f.modeKey } });
                    return;
                }
            if (const int u = userIndexOf(uuid); u >= 0)
            {
                const User& p = users_[static_cast<std::size_t>(u)];
                setCurrent({ p.uuid, p.name, p.category, p.sound });
                return;
            }
        }
        setCurrent({ {}, {}, {}, liveSound() });                 // untitled: the live sound is its baseline
    }

    // ==== user presets ===============================================================================================

    bool WebPresets::nameTaken(std::string_view name, std::string_view ignoreUuid) const
    {
        const std::string key = fold(name);
        for (const factory::FactoryRow& f : bank())
            if (ignoreUuid != f.uuid && fold(f.name) == key)
                return true;
        for (const User& u : users_)
            if (ignoreUuid != u.uuid && fold(u.name) == key)
                return true;
        return false;
    }

    // PresetStore's uniqueName (PresetStore.cpp:894-917): "Name", else "Name 2", "Name 3", ...; a taken "Name 2"
    // continues at "Name 3", not "Name 2 2".
    std::string WebPresets::uniqueName(std::string_view wanted) const
    {
        const std::string base(trimmed(wanted).empty() ? std::string_view("Untitled") : trimmed(wanted));
        if (!nameTaken(base, {}))
            return base;
        std::string stem = base;
        int n = 2;
        if (const std::size_t space = base.rfind(' '); space != std::string::npos && space > 0)
        {
            const std::string_view tail = std::string_view(base).substr(space + 1);
            if (!tail.empty() && tail.size() <= 6 && std::all_of(tail.begin(), tail.end(), isDigit))
            {
                int value = 0;
                for (const char c : tail)
                    value = value * 10 + (c - '0');
                std::string_view head = std::string_view(base).substr(0, space);
                while (!head.empty() && isSpace(head.back()))
                    head.remove_suffix(1);
                stem = std::string(head);
                n = std::max(2, value + 1);
            }
        }
        for (;; ++n)
            if (std::string candidate = stem + " " + std::to_string(n); !nameTaken(candidate, {}))
                return candidate;
    }

    void WebPresets::sortUsers()
    {
        std::stable_sort(users_.begin(), users_.end(), [](const User& a, const User& b) {
            const std::string ka = sortKey(a.name), kb = sortKey(b.name);
            if (ka != kb)
                return ka < kb;
            const std::string fa = fold(a.name), fb = fold(b.name);
            return fa != fb ? fa < fb : a.uuid < b.uuid;
        });
    }

    // Presets.cpp saveAs (:270-287) over PresetStore::saveNew (PresetStore.cpp:1244-1270).
    bool WebPresets::saveAs(std::string_view name, std::string_view category)
    {
        const std::string_view n = trimmed(name);
        if (n.empty())
            return false;
        User u;
        // A session uuid: the counter under a prefix no factory preset has (theirs start 00000000-).
        char uuid[40];
        std::snprintf(uuid, sizeof uuid, "5e551000-0000-4000-8000-%012llx",
                      static_cast<unsigned long long>(nextUuid_++));
        u.uuid = uuid;
        u.name = uniqueName(n);
        u.category = std::string(trimmed(category));
        u.sound = liveSound();
        setCurrent({ u.uuid, u.name, u.category, u.sound });     // the saved preset, unmodified
        users_.push_back(std::move(u));
        sortUsers();
        return true;
    }

    bool WebPresets::rename(int index, std::string_view newName)
    {
        User* u = userAt(index);
        const std::string_view n = trimmed(newName);
        if (u == nullptr || n.empty() || nameTaken(n, u->uuid))
            return false;                                        // a factory row, no row, no name, or another's name
        u->name = std::string(n);
        if (current_.uuid == u->uuid)
            current_.name = u->name;
        sortUsers();                                             // u is not valid past here
        ++changes_;
        return true;
    }

    bool WebPresets::remove(int index)
    {
        const User* u = userAt(index);
        if (u == nullptr)
            return false;
        if (current_.uuid == u->uuid)
        {
            current_.uuid.clear();                               // untitled: no identity, the same baseline and Mode
            current_.name.clear();
            current_.category.clear();
        }
        users_.erase(users_.begin() + (index - factoryCount()));
        ++changes_;
        return true;
    }

    // Presets.cpp overwrite (:362-380): the row keeps its uuid, name and category and takes the live sound.
    bool WebPresets::overwrite(int index)
    {
        User* u = userAt(index);
        if (u == nullptr)
            return false;
        const Sound live = liveSound();
        u->sound.values = live.values;
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(facade_.currentRaw().modeSlot);
        if (ms.entry != nullptr && ms.entry->desc != nullptr)
            u->sound.modeId = live.modeId;                       // else the row's own stays (setModeAttributes)
        setCurrent({ u->uuid, u->name, u->category, u->sound });
        return true;
    }
} // namespace fcmp::web
