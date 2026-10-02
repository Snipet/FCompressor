// Source/plugin/factory/FactoryBank.cpp: the factory bank, the preset product configuration and the shared store (01
// §9.2; K1 #31, K3 #18; P3, S12). See FactoryBank.h for the contract. Written once by P3; later banks change only the
// per-Mode entry files, Source/plugin/factory/<key>.inc, each owned by its Mode's card (03 §4.6).
//
// The entry table those files expand into, their format and rules, and the default filling are
// Source/plugin/portable/FactoryData.cpp's (web Sprint B, ADR-93: the bank's data without JUCE). What stays here is
// what needs FunkPresets: each FactoryRow as a funkgui::presets::Preset, the product configuration and the store.
#include "plugin/factory/FactoryBank.h"

#include "FcmpProduct.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <cstddef>

namespace fcmp::factory
{
    namespace
    {
        using fcdsp::Pid;
        using funkgui::presets::Preset;

        // One row as a preset: its 22 values under their host ids, then the Mode's key and revision as attributes.
        Preset toPreset(const FactoryRow& row)
        {
            Preset p;
            p.uuid = row.uuid;
            p.name = row.name;
            p.category = row.category;
            p.notes = row.notes;
            p.isFactory = true;
            for (const Pid pid : fcdsp::kApvtsOrder)          // the processor's layout order: capture()'s order
            {
                const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
                if (fcdsp::idx(pid) < fcdsp::kNumModeParams && h.inPresets)
                    p.params.push_back({ h.id, row.values[fcdsp::idx(pid)] });
            }
            p.setAttr(kModeIdAttr, juce::String(row.modeKey));
            p.setAttr(kModeRevAttr, juce::String(row.modeRev));
            return p;
        }

        std::vector<Preset> buildBank()
        {
            std::vector<Preset> bank;
            bank.reserve(factoryRows().size());
            for (const FactoryRow& row : factoryRows())
                bank.push_back(toPreset(row));
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
} // namespace fcmp::factory
