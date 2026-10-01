// Source/plugin/factory/FactoryBank.h: FCompressor's preset product (01 §9.2; P3, S12): the FunkPresets configuration,
// the process-wide preset store, and the factory bank compiled into this build.
//
// - presetConfig(): ProductConfig {"FCompressor", ".fcmppreset", "FCompressorPreset", "FCMP_PRESETS_DB"}, spelled from
//   FcmpProduct.h (product name, kPresetExtension, environment prefix), so a store, an exported file and the database
//   location all name the same product.
// - SharedPresetStore: the one PresetStore per process (funkgui::presets::PresetStore's own advice), held through
//   juce::SharedResourcePointer<SharedPresetStore> by every instance's PresetAccess once its editor first asks for the
//   list, so a plugin instance without an editor never opens the database. Opening it syncs the factory rows
//   (syncFactory with factoryBankRevision(): cheap when the revision matches). MESSAGE THREAD ONLY, as the store.
// - factoryBank(): "Init" at index 0 (every parameter at its default, `clean`), then Source/plugin/factory/<key>.inc
//   in Modes.def slot order (the configure-generated fcmp/FactoryIncludes.h). Every preset carries all 22
//   Mode-filtered parameters (plain host units, kApvtsOrder) and the attributes modeId and modeRev; a parameter an
//   entry does not state holds its Mode's default. Built once, on first use, from constant data; the compiled bank is
//   the authority for a factory preset's values (the database's copy only serves search and tags). The data is
//   portable/FactoryData.h's rows (the entry table and the default filling, without JUCE: web Sprint B, ADR-93); this
//   is their funkgui::presets::Preset form, one preset per row, in the same order.
// - factoryBankRevision() (declared in portable/FactoryData.h): FCMP_FACTORY_BANK_REVISION, the first 32 bits of the
//   SHA-256 of the .inc files, computed at configure time: never a hand-edited counter.
#pragma once

#include "plugin/portable/FactoryData.h"

#include <funkgui/presets/PresetStore.h>
#include <funkgui/presets/PresetTypes.h>

#include <cstdint>
#include <vector>

namespace fcmp::factory
{
    inline constexpr char kModeIdAttr[] = "modeId";      // Preset::attributes: the Mode's key, authoritative (01 §9.2)
    inline constexpr char kModeRevAttr[] = "modeRev";    // the Mode's ModeDescriptor::revision when captured (K2 #10)

    funkgui::presets::ProductConfig presetConfig();

    struct SharedPresetStore final : funkgui::presets::PresetStore
    {
        SharedPresetStore();                             // presetConfig(); then syncFactory(factoryBank(), revision)
    };

    const std::vector<funkgui::presets::Preset>& factoryBank();              // thread-safe; built on first use
    const funkgui::presets::Preset* findFactory(const juce::String& uuid);   // nullptr: not a factory uuid
    int factoryIndexOf(const juce::String& uuid);                            // -1: not a factory uuid
} // namespace fcmp::factory
