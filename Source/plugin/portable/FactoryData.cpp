// Source/plugin/portable/FactoryData.cpp: the factory bank's entry table and its default filling (01 §9.2; K1 #31, K3
// #18; P3, S12). See FactoryData.h for the contract. Written once by P3 as factory/FactoryBank.cpp, which keeps the
// conversion to funkgui::presets::Preset; later banks change only the per-Mode entry files,
// Source/plugin/factory/<key>.inc, each owned by its Mode's card (03 §4.6).
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
#include "plugin/portable/FactoryData.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include "fcmp/FactoryIncludes.h"                          // FCMP_FACTORY_BANK_REVISION

#include <array>
#include <cstddef>
#include <iterator>

namespace fcmp::factory
{
    namespace
    {
        using fcdsp::Pid;

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

        using Rows = std::array<FactoryRow, std::size(kEntries)>;

        // One entry as a complete row: host defaults, then, in kResolveOrder, each parameter the entry states, or else
        // the default of the spec its (already final) drivers select when that spec is live or stepped. An entry whose
        // Mode is not registered keeps the host defaults and names the Mode anyway (proc.presets reports it).
        FactoryRow materialise(const Entry& en)
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

            FactoryRow row;
            row.uuid = en.uuid;
            row.name = en.name;
            row.category = en.category;
            row.notes = en.notes;
            row.modeKey = en.mode;
            row.modeRev = desc != nullptr ? static_cast<int>(desc->revision) : 1;
            row.values = raw.v;
            return row;
        }

        Rows buildRows()
        {
            Rows rows;
            for (std::size_t i = 0; i < rows.size(); ++i)
                rows[i] = materialise(kEntries[i]);
            return rows;
        }
    } // namespace

    std::span<const FactoryRow> factoryRows()
    {
        static const Rows rows = buildRows();                      // constant data; a thread-safe first use
        return rows;
    }

    std::uint32_t factoryBankRevision() noexcept { return FCMP_FACTORY_BANK_REVISION; }
} // namespace fcmp::factory
