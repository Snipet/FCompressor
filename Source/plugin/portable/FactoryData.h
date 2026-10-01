// Source/plugin/portable/FactoryData.h: the factory bank compiled into this build, as plain data (01 §9.2; P3, S12;
// split from factory/FactoryBank.cpp by web Sprint B, ADR-93). No JUCE and no FunkPresets (lint plugin.portable): the
// processor's bank (factory/FactoryBank.h) converts these rows to funkgui::presets::Preset, and a facade without JUCE
// (the probes' FakeFacade, the browser demo's) reads them as they are.
//
// - factoryRows(): "Init" at index 0 (every parameter at its default, `clean`), then Source/plugin/factory/<key>.inc
//   in Modes.def slot order (the configure-generated fcmp/FactoryIncludes.h). Every row carries all 22 Mode-filtered
//   parameters in plain host units; a parameter an entry does not state holds its Mode's default (FactoryData.cpp).
//   Built once, on first use, from constant data; the strings are literals, valid for the life of the program. The
//   compiled bank is the authority for a factory preset's values (the database's copy only serves search and tags).
// - factoryBankRevision(): FCMP_FACTORY_BANK_REVISION, the first 32 bits of the SHA-256 of the .inc files, computed at
//   configure time: never a hand-edited counter.
#pragma once

#include "fcdsp/params/Pid.h"

#include <array>
#include <cstdint>
#include <span>

namespace fcmp::factory
{
    struct FactoryRow
    {
        const char* uuid = "";                               // fixed forever: the identity sessions and tags key on
        const char* name = "";                               // printable ASCII, unique in the bank
        const char* category = "";
        const char* notes = "";
        const char* modeKey = "";                            // the Mode's key (the preset's modeId), registered or not
        int         modeRev = 1;                             // its ModeDescriptor::revision (1: the Mode is not registered)
        std::array<float, fcdsp::kNumModeParams> values{};   // plain host units, values[fcdsp::idx(pid)]
    };

    std::span<const FactoryRow> factoryRows();               // thread-safe; built on first use
    std::uint32_t factoryBankRevision() noexcept;
} // namespace fcmp::factory
