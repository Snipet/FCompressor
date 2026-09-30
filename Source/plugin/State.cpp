// Source/plugin/State.cpp: session state (01 §9.1; K2 #10, #23, #25c; K3 #14; P2, S8). See State.h for the format and
// the contract; the migrations table is StateMigration.cpp.
//
// Save  a fresh <PARAMS> tree: the root attributes stateVersion, modeId, modeRev (the effective slot's Mode), product,
//       build; one <PARAM id value/> per host parameter in kApvtsOrder, value = the APVTS raw atomic (bit-exact: the
//       APVTS tree that copyState() returns is flushed with approximatelyEqual and can lag the raw value by an ulp);
//       writePreset if installed; <UI charExpanded scTab/>; copyXmlToBinary.
// Load  parse and check the root type; stateVersion and the migrations; the 29 target values (saved and sanitised,
//       else the table default; listen/delta 0; `mode` from resolveSessionMode); then ONE batch: replaceState with those
//       values (the saved tree's PARAM children rebuilt in kApvtsOrder: unknown ids, <PRESET> and <UI> never reach the
//       APVTS), every parameter written exactly (the host gets its normalised value, the raw atomic the exact plain
//       value), readPreset, <UI>; endBatch raises the snap after the last write; the new StateNotice.
#include "plugin/State.h"

#include "FcmpProduct.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace fcmp
{
    namespace
    {
        using fcdsp::HostParam;
        using fcdsp::Pid;

        // ---- the XML names (v1-forever) --------------------------------------------------------------------------------
        constexpr const char* kParamType = "PARAM";            // the APVTS child type and its two properties
        constexpr const char* kIdAttr = "id";
        constexpr const char* kValueAttr = "value";
        constexpr const char* kUiType = "UI";
        constexpr const char* kStateVersionAttr = "stateVersion";
        constexpr const char* kModeIdAttr = "modeId";
        constexpr const char* kModeRevAttr = "modeRev";
        constexpr const char* kProductAttr = "product";
        constexpr const char* kBuildAttr = "build";
        constexpr const char* kCharExpandedAttr = "charExpanded";
        constexpr const char* kScTabAttr = "scTab";
        constexpr const char* kScTabSidechain = "sidechain";
        constexpr const char* kScTabColour = "colour";
        constexpr std::string_view kFallbackKey = "clean";      // where an unknown key lands (01 §8.2, §9.1 step 5)

        // The batch of one load (K2 #23): endBatch runs even if a hook throws, so the audio thread is never left
        // reusing the previous BlockParams forever.
        class BatchScope
        {
        public:
            explicit BatchScope(ProcessorFacade& facade) : facade_(facade) { facade_.beginBatch(); }
            ~BatchScope() { facade_.endBatch(); }
            BatchScope(const BatchScope&) = delete;
            BatchScope& operator=(const BatchScope&) = delete;

        private:
            ProcessorFacade& facade_;
        };

        // A finite number, or nothing. XML attributes are strings: the whole string (surrounding whitespace allowed)
        // must be one number, read locale-independently (a host's LC_NUMERIC never turns "0.75" into 0). nan and inf
        // are not values.
        std::optional<double> parseNumber(const juce::var& v)
        {
            double d = 0.0;
            if (v.isString())
            {
                const juce::String s = v.toString();
                const auto start = s.getCharPointer().findEndOfWhitespace();
                auto p = start;
                d = juce::CharacterFunctions::readDoubleValue(p);
                if (p == start || !p.findEndOfWhitespace().isEmpty())
                    return std::nullopt;                            // nothing read, or trailing text
            }
            else if (v.isDouble() || v.isInt() || v.isInt64() || v.isBool())
                d = static_cast<double>(v);
            else
                return std::nullopt;
            if (!std::isfinite(d))
                return std::nullopt;
            return d;
        }

        // An integer in [lo, hi], or nothing (a fractional or out-of-range number is not one).
        std::optional<int> parseInt(const juce::var& v, int lo, int hi)
        {
            const std::optional<double> d = parseNumber(v);
            if (!d.has_value() || *d != std::floor(*d) || *d < static_cast<double>(lo) || *d > static_cast<double>(hi))
                return std::nullopt;
            return static_cast<int>(*d);
        }

        // The legal plain value nearest `plain` for this parameter, through the parameter's own range: legal() for the
        // Float parameters (their range's snap lambda), rounded and clamped for Int/Choice, clamped for the switches
        // (whose raw value keeps the host's position, ParamLayout.cpp).
        float sanitise(const juce::RangedAudioParameter& p, double plain)
        {
            const juce::NormalisableRange<float>& r = p.getNormalisableRange();
            const double clamped = std::clamp(plain, static_cast<double>(r.start), static_cast<double>(r.end));
            return r.snapToLegalValue(static_cast<float>(clamped));
        }

        // Round a `mode` plain value to its slot the way the processor does (0..127).
        int slotOf(float plain) noexcept
        {
            if (!(plain > 0.0f))                                    // also NaN
                return 0;
            if (plain >= static_cast<float>(fcdsp::kModeCapacity - 1))
                return fcdsp::kModeCapacity - 1;
            return static_cast<int>(plain + 0.5f);
        }

        std::atomic<float>& rawOf(juce::AudioProcessorValueTreeState& apvts, const HostParam& h)
        {
            std::atomic<float>* raw = apvts.getRawParameterValue(h.id);
            jassert(raw != nullptr);                                // the layout builds every kHostParams id
            return *raw;
        }

        juce::RangedAudioParameter& parameterOf(juce::AudioProcessorValueTreeState& apvts, const HostParam& h)
        {
            juce::RangedAudioParameter* p = apvts.getParameter(h.id);
            jassert(p != nullptr);
            return *p;
        }

        // One exact write: the host is told the normalised value of `plain` (only when it differs, so an unchanged
        // parameter is not re-announced), then the raw atomic, which the adapter has just set to the host map's
        // round trip toPlain(toNorm(plain)), gets `plain` itself. The raw value is what the processor reads, what the
        // next save writes, and what the resolver snaps (01 §1.3).
        void writeExact(juce::RangedAudioParameter& p, std::atomic<float>& raw, float plain)
        {
            const float norm = p.convertTo0to1(plain);
            if (p.getValue() != norm)
                p.setValueNotifyingHost(norm);
            raw.store(plain, std::memory_order_relaxed);
        }

        // StateNotice keys: at most 24 bytes; anything but [A-Za-z0-9._-] becomes '?', so a foreign or corrupt key can
        // never put a broken UTF-8 sequence into the footer.
        void copyKey(char (&dst)[25], std::string_view key) noexcept
        {
            const std::size_t n = std::min<std::size_t>(key.size(), sizeof dst - 1);
            for (std::size_t i = 0; i < n; ++i)
            {
                const char c = key[i];
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                             || c == '_' || c == '.';
                dst[i] = ok ? c : '?';
            }
            dst[n] = '\0';
        }

        juce::String toJuce(std::string_view s)
        {
            return juce::String::fromUTF8(s.data(), static_cast<int>(s.size()));
        }

        UiState readUi(const juce::ValueTree& ui)
        {
            UiState s{};                                            // absent child or attribute: the default
            if (!ui.isValid())
                return s;
            if (const std::optional<int> e = parseInt(ui.getProperty(kCharExpandedAttr), 0, 1))
                s.charExpanded = *e == 1;
            if (ui.getProperty(kScTabAttr).toString() == kScTabColour)
                s.scTab = ScTab::colour;
            return s;
        }
    } // namespace

    // ==== the session's Mode =========================================================================================

    SessionMode resolveSessionMode(const SavedMode& saved, const ModeLookup& lookup)
    {
        SessionMode out;
        if (saved.modeId.has_value())
        {
            const std::string_view key = *saved.modeId;
            const fcdsp::ModeSlot* ms = lookup.resolveKey(key);     // registered, or retired -> its successor
            out.mode = ms != nullptr ? ms : lookup.resolveKey(kFallbackKey);
            out.migrated = ms == nullptr || ms->key != key;
            if (out.migrated)
                out.fromKey = std::string(key);
        }
        else
        {
            const int slot = std::clamp(saved.slot.value_or(0), 0, fcdsp::kModeCapacity - 1);
            const fcdsp::ModeSlot& ms = lookup.resolveSlot(slot);   // retired -> successor; unassigned -> clean
            out.mode = &ms;
            if (ms.slot != slot)
            {
                out.migrated = true;
                out.fromKey = "slot-" + std::to_string(slot);
                for (const fcdsp::Retired& r : lookup.retired())
                    if (r.slot == slot)
                        out.fromKey = std::string(r.key);
            }
        }
        if (out.mode == nullptr || out.mode->entry == nullptr)
            return SessionMode{};                                   // an empty registry: nothing to load or report

        const fcdsp::ModeDescriptor* desc = out.mode->entry->desc;
        if (!out.migrated && desc != nullptr)
        {
            const int savedRev = saved.modeRev.value_or(1);         // absent: the first revision (K2 #10)
            if (savedRev < static_cast<int>(desc->revision))
            {
                out.revised = true;
                out.savedRev = static_cast<std::uint16_t>(savedRev);
                out.currentRev = desc->revision;
            }
        }
        return out;
    }

    // ==== save =======================================================================================================

    void saveState(const StateContext& ctx, juce::MemoryBlock& destData)
    {
        juce::AudioProcessorValueTreeState& apvts = ctx.apvts;
        juce::ValueTree tree(kStateType);

        // 2. identity: the effective slot's Mode (a retired or unassigned raw slot saves the Mode it plays)
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(slotOf(rawOf(apvts, fcdsp::kHostParams[fcdsp::idx(Pid::mode)])
                                                                  .load(std::memory_order_relaxed)));
        tree.setProperty(kStateVersionAttr, static_cast<int>(fcdsp::kStateVersion), nullptr);
        if (ms.entry != nullptr && ms.entry->desc != nullptr)
        {
            tree.setProperty(kModeIdAttr, toJuce(ms.key), nullptr);
            tree.setProperty(kModeRevAttr, static_cast<int>(ms.entry->desc->revision), nullptr);
        }
        tree.setProperty(kProductAttr, product::kName, nullptr);
        tree.setProperty(kBuildAttr, product::kVersion, nullptr);

        // 1. every host parameter in host order, at its raw value (plain units)
        for (const Pid pid : fcdsp::kApvtsOrder)
        {
            const HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
            juce::ValueTree param(kParamType);
            param.setProperty(kIdAttr, h.id, nullptr);
            param.setProperty(kValueAttr, static_cast<double>(rawOf(apvts, h).load(std::memory_order_relaxed)),
                              nullptr);
            tree.appendChild(param, nullptr);
        }

        // 3. the <PRESET> child (P3), 4. the per-instance editor state
        if (ctx.hooks.writePreset)
            ctx.hooks.writePreset(tree);
        juce::ValueTree ui(kUiType);
        ui.setProperty(kCharExpandedAttr, ctx.ui.charExpanded ? 1 : 0, nullptr);
        ui.setProperty(kScTabAttr, ctx.ui.scTab == ScTab::colour ? kScTabColour : kScTabSidechain, nullptr);
        tree.appendChild(ui, nullptr);
        if (ctx.hooks.writeCompare)                                 // ADR-91: after <UI>, only once B was used
            ctx.hooks.writeCompare(tree);

        // 5.
        if (const std::unique_ptr<juce::XmlElement> xml = tree.createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, destData);
    }

    // ==== load =======================================================================================================

    void loadState(const StateContext& ctx, const void* data, int sizeInBytes)
    {
        juce::AudioProcessorValueTreeState& apvts = ctx.apvts;

        // 1. XML -> tree; not a <PARAMS> tree: ignored, nothing changes
        if (data == nullptr || sizeInBytes <= 0)
            return;
        const std::unique_ptr<juce::XmlElement> xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || !xml->hasTagName(kStateType))
            return;
        juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid())
            return;

        StateNotice notice{};
        notice.serial = ctx.notice.serial + 1u;

        // 2. stateVersion (absent or not a version: 1, the development sessions): newer -> best effort, no migration;
        //    older -> the migrations, in order, on the whole tree
        const int version = parseInt(tree.getProperty(kStateVersionAttr), 1, std::numeric_limits<int>::max()).value_or(1);
        if (version > static_cast<int>(fcdsp::kStateVersion))
            notice.newerSession = true;
        else
        {
            const std::span<const StateMigration> migrations = stateMigrations();
            for (std::size_t i = static_cast<std::size_t>(version - 1); i < migrations.size(); ++i)
                migrations[i](tree);
        }

        // 3-4. the target value of every host parameter: the first saved PARAM of its id holding a number, sanitised;
        //      else its table default; listen and delta always 0 (K2 #25c)
        std::array<float, fcdsp::kNumParams> target{};
        std::array<bool, fcdsp::kNumParams> present{};
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            target[i] = fcdsp::kHostParams[i].def;
        const juce::Identifier paramType(kParamType), idAttr(kIdAttr), valueAttr(kValueAttr);
        for (int c = 0; c < tree.getNumChildren(); ++c)
        {
            const juce::ValueTree child = tree.getChild(c);
            if (!child.hasType(paramType))
                continue;
            const juce::String id = child.getProperty(idAttr).toString();
            for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            {
                const HostParam& h = fcdsp::kHostParams[i];
                if (present[i] || id != h.id)
                    continue;
                if (const std::optional<double> v = parseNumber(child.getProperty(valueAttr)))
                {
                    target[i] = sanitise(parameterOf(apvts, h), *v);
                    present[i] = true;
                }
                break;
            }
        }
        target[fcdsp::idx(Pid::listen)] = 0.0f;
        target[fcdsp::idx(Pid::delta)] = 0.0f;

        // 5-6. the Mode: modeId wins over the `mode` slot; unknown/retired keys and slots migrate; an older modeRev is
        //      reported. The `mode` parameter gets the slot of the Mode that loads.
        const std::string modeId = tree.getProperty(kModeIdAttr).toString().toStdString();
        SavedMode saved;
        if (!modeId.empty())
            saved.modeId = std::string_view(modeId);
        if (const std::size_t m = fcdsp::idx(Pid::mode); present[m])
            saved.slot = slotOf(target[m]);
        saved.modeRev = parseInt(tree.getProperty(kModeRevAttr), 0, std::numeric_limits<std::uint16_t>::max());
        const SessionMode session = resolveSessionMode(saved);
        if (session.mode != nullptr)
        {
            target[fcdsp::idx(Pid::mode)] = static_cast<float>(session.mode->slot);
            if (session.migrated || session.revised)
                copyKey(notice.toKey, session.mode->key);
        }
        if (session.migrated)
        {
            notice.modeMigrated = true;
            copyKey(notice.fromKey, session.fromKey);
        }
        if (session.revised)
        {
            notice.modeRevised = true;
            notice.savedRev = session.savedRev;
            notice.currentRev = session.currentRev;
        }

        // The APVTS tree: exactly the targets, in host order (the saved tree's unknown ids, <PRESET> and <UI> stay out).
        juce::ValueTree params(kStateType);
        for (const Pid pid : fcdsp::kApvtsOrder)
        {
            juce::ValueTree param(paramType);
            param.setProperty(idAttr, fcdsp::kHostParams[fcdsp::idx(pid)].id, nullptr);
            param.setProperty(valueAttr, static_cast<double>(target[fcdsp::idx(pid)]), nullptr);
            params.appendChild(param, nullptr);
        }

        {
            const BatchScope batch(ctx.facade);                    // the audio thread keeps the previous BlockParams
            apvts.replaceState(params);                             // (HR B §1.8)
            for (const Pid pid : fcdsp::kApvtsOrder)
            {
                const HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
                writeExact(parameterOf(apvts, h), rawOf(apvts, h), target[fcdsp::idx(pid)]);
            }
            // 7. the <PRESET> hook (P3), then the per-instance editor state; endBatch raises the snap after them
            if (ctx.hooks.readPreset)
                ctx.hooks.readPreset(tree);
            ctx.ui = readUi(tree.getChildWithName(kUiType));
            if (ctx.hooks.readCompare)                              // ADR-91: every load, the child or its absence
                ctx.hooks.readCompare(tree);
        }
        ctx.notice = notice;
    }
} // namespace fcmp
