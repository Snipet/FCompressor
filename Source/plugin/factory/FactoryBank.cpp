// Source/plugin/factory/FactoryBank.cpp: the factory bank, the preset product configuration and the shared store (01
// §9.2; K1 #31, K3 #18; P3, S12). See FactoryBank.h for the contract. Written once by P3; later banks change only the
// per-Mode entry files, Source/plugin/factory/<key>.inc, each owned by its Mode's card (03 §4.6).
//
// ---- the entry files -------------------------------------------------------------------------------------------------
// cmake/FcmpSources.cmake includes every <key>.inc whose key has an FCMP_MODE line, in Modes.def slot order, through
// the generated fcmp/FactoryIncludes.h, INSIDE kEntries' initialiser below; FCMP_FACTORY_BANK_REVISION hashes their
// contents, so any edit moves the revision and the database re-syncs its factory rows. An entry file is a list of
// Entry initialisers, one per preset, each followed by a comma:
//
//   { "00000000-0000-4000-8000-000000000021", "fet-76", "Vocal Grab", "Vocal",
//     "One line on what it is for.",
//     { { Pid::thr, -26.0f }, { Pid::ratio, 0.75f }, { Pid::atk, 0.4f } } },
//
//   uuid      fixed forever: the identity the database, sessions and tags key on (never reused for another sound;
//             retire a preset by deleting its entry). 00000000-0000-4000-8000-0000000000NN with NN = the Mode's slot
//             in the high hex digit and the entry's 1-based position in the low one (bus-g: 11, 12, ...; slot 0 is
//             01..0f; 00 is Init). A Mode with slot >= 16 continues in the next digits (...0000000001S1).
//   Mode      the Mode's key, the preset's modeId (its slot is looked up, never written here).
//   name      printable ASCII, unique in the bank (the editor font's glyphs; the store's unique-name rule).
//   category  what the preset is for (Vocal, Drums, Bass, Bus, Master, Parallel, Instrument, Limiter).
//   values    host PLAIN units (01 §3.1): dB, ms, the ratio's S = 1 - 1/R, index for list parameters, 0..1 or 0..2
//             for link and mix. A stepped parameter states one of its Mode's steps exactly (the literal the Mode's
//             descriptor spells). Only Mode-filtered parameters: `mode` is the Mode above, and the monitoring and
//             setup parameters never go into presets (01 §3.1 rules).
//
// Every parameter an entry does not state holds its Mode's default (the ParamSpec default of the spec that is active
// once the stated drivers are set; the host default where the Mode shows none), and the bank stores all 22: what
// "load Mode defaults" gives, then the entry's settings. proc.presets checks each entry against its Mode: stated values
// on a step, inside the Mode's range and live (a value stated for a locked or n/a slot does nothing).
#include "plugin/factory/FactoryBank.h"

#include "FcmpProduct.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include "fcmp/FactoryIncludes.h"                          // FCMP_FACTORY_BANK_REVISION

#include <array>
#include <cstddef>
#include <string_view>

namespace fcmp::factory
{
    namespace
    {
        using fcdsp::Pid;
        using funkgui::presets::Preset;

        struct Value
        {
            Pid pid = fcdsp::kNoPid;                        // kNoPid: an unused element (the rest of the array)
            float plain = 0.0f;
        };

        struct Entry
        {
            const char* uuid;
            const char* mode;                               // the Mode's key (modeId)
            const char* name;
            const char* category;
            const char* notes;
            Value values[fcdsp::kNumModeParams];            // the stated settings; the rest are the Mode's defaults
        };

        // The ratio's host plain value for R:1 (S = 1 - 1/R), spelled as the descriptors spell their steps.
        constexpr float ratio(float r) noexcept { return 1.0f - 1.0f / r; }

        constexpr const char* kInitUuid = "00000000-0000-4000-8000-000000000000";

        constexpr Entry kEntries[] = {
            { kInitUuid, "clean", "Init", "Init", "Every control at its default: the state a new instance starts in.",
              {} },
#define FCMP_FACTORY_ENTRIES
#include "fcmp/FactoryIncludes.h"
#undef FCMP_FACTORY_ENTRIES
        };

        // One entry as a complete preset: host defaults, then, in kResolveOrder, each parameter the entry states, or
        // else the default of the spec its (already final) drivers select when that spec is live or stepped. An entry
        // whose Mode is not registered keeps the host defaults and names the Mode anyway (proc.presets reports it).
        Preset materialise(const Entry& en)
        {
            std::array<bool, fcdsp::kNumModeParams> stated{};
            fcdsp::RawParams raw;
            for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
                raw.v[i] = fcdsp::kHostParams[i].def;
            for (const Value& v : en.values)
                if (fcdsp::idx(v.pid) < fcdsp::kNumModeParams)
                {
                    raw.v[fcdsp::idx(v.pid)] = v.plain;
                    stated[fcdsp::idx(v.pid)] = true;
                }

            const fcdsp::ModeSlot* ms = fcdsp::resolveKey(en.mode);
            const fcdsp::ModeDescriptor* desc = ms != nullptr && ms->entry != nullptr ? ms->entry->desc : nullptr;
            if (desc != nullptr)
            {
                raw.modeSlot = ms->slot;
                raw.budget = fcdsp::LookaheadBudget::ms20;   // the widest budget: `look` resolves as the Mode shows it
                for (const Pid pid : fcdsp::kResolveOrder)
                {
                    const std::size_t i = fcdsp::idx(pid);
                    if (stated[i])
                        continue;
                    fcdsp::ParamView view;
                    fcdsp::resolveView(*desc, raw, view);
                    const fcdsp::ParamSpec* spec = view.spec[i];
                    if (spec != nullptr
                        && (spec->kind == fcdsp::Kind::continuous || spec->kind == fcdsp::Kind::stepped
                            || spec->kind == fcdsp::Kind::hybrid))
                        raw.v[i] = spec->defaultPlain;
                }
            }

            Preset p;
            p.uuid = en.uuid;
            p.name = en.name;
            p.category = en.category;
            p.notes = en.notes;
            p.isFactory = true;
            for (const Pid pid : fcdsp::kApvtsOrder)          // the processor's layout order: capture()'s order
            {
                const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
                if (fcdsp::idx(pid) < fcdsp::kNumModeParams && h.inPresets)
                    p.params.push_back({ h.id, raw.v[fcdsp::idx(pid)] });
            }
            p.setAttr(kModeIdAttr, juce::String(en.mode));
            p.setAttr(kModeRevAttr, juce::String(desc != nullptr ? static_cast<int>(desc->revision) : 1));
            return p;
        }

        std::vector<Preset> buildBank()
        {
            std::vector<Preset> bank;
            bank.reserve(std::size(kEntries));
            for (const Entry& en : kEntries)
                bank.push_back(materialise(en));
            return bank;
        }
    } // namespace

    funkgui::presets::ProductConfig presetConfig()
    {
        funkgui::presets::ProductConfig c;
        c.productName = product::kName;                            // "FCompressor"
        c.fileExtension = product::kPresetExtension;               // ".fcmppreset" (S13 lead revision 5e)
        c.xmlRoot = juce::String(product::kName) + "Preset";       // "FCompressorPreset"
        c.dbEnvVar = juce::String(product::kEnvPrefix) + "PRESETS_DB";   // "FCMP_PRESETS_DB"
        return c;
    }

    SharedPresetStore::SharedPresetStore() : funkgui::presets::PresetStore(presetConfig())
    {
        syncFactory(factoryBank(), static_cast<int>(factoryBankRevision()));   // the bits, as an int: equality only
    }

    const std::vector<Preset>& factoryBank()
    {
        static const std::vector<Preset> bank = buildBank();       // constant data; a thread-safe first use
        return bank;
    }

    const Preset* findFactory(const juce::String& uuid)
    {
        const int i = factoryIndexOf(uuid);
        return i >= 0 ? &factoryBank()[static_cast<std::size_t>(i)] : nullptr;
    }

    int factoryIndexOf(const juce::String& uuid)
    {
        if (uuid.isEmpty())
            return -1;
        const std::vector<Preset>& bank = factoryBank();
        for (std::size_t i = 0; i < bank.size(); ++i)
            if (bank[i].uuid == uuid)
                return static_cast<int>(i);
        return -1;
    }

    std::uint32_t factoryBankRevision() noexcept { return FCMP_FACTORY_BANK_REVISION; }
} // namespace fcmp::factory
