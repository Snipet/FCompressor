// Source/plugin/State.h: session state save/load (01 §9.1; K2 #10, #23, #25c; K3 #14). The entry points are frozen from
// S7 (SPRINTS §7 D20): P1 wrote them with an APVTS-only body, P2 (S8) wrote the v1 format (stateVersion, modeId, modeRev,
// the migrations table, StateNotice, the <UI> child) and P3 (S12) installs the <PRESET> hooks from Presets.cpp. Neither
// ever edits Processor.cpp: the processor hands both everything they touch through StateContext.
//
// The format (v1-forever once shipped; 01 §0, §9.1):
//
//   <PARAMS stateVersion="1" modeId="fet-76" modeRev="1" product="FCompressor" build="0.1.0">
//     <PARAM id="thr" value="-24"/> ...        one per host parameter (all 29), PLAIN units, the APVTS raw value
//     <PRESET .../>                            only through StateHooks (P3)
//     <UI charExpanded="0" scTab="sidechain"/> per-instance editor state, never parameters (K1 #14)
//   </PARAMS>
//
// The raw value is truth (01 §1.3), so a session restores BIT-EXACTLY: saveState writes every parameter's live raw value
// (the APVTS raw atomic, not the APVTS tree, whose flush skips "approximately equal" changes), and loadState stores each
// saved plain value back into the raw atomic after the host has been told the parameter's normalised value (the host map
// toPlain(toNorm(v)) is not the identity: makeup 0 dB -> 3.6e-7 dB). A non-fresh instance restores exactly like a fresh
// one: every parameter a session lacks takes its table default, again exactly.
//
// Threads: whatever thread the host calls get/setStateInformation on (usually the message thread). loadState brackets
// every parameter write with facade.beginBatch()/endBatch(), so the audio thread keeps the previous BlockParams until
// the whole set is written, and endBatch() raises the engine snap after the last write (K2 #23).
//
// State.cpp owns <PARAMS> and never links FunkPresets (K3 #14): the <PRESET> child goes through StateHooks, which stay
// null until P3 installs them.
#pragma once

#include "plugin/ProcessorFacade.h"

#include "fcdsp/modes/Registry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fcmp
{
    // The optional <PRESET> child of <PARAMS> (01 §9.1 save step 3, load step 7). Installed by Presets.cpp (P3) through
    // PresetContext::stateHooks; null until then, and State.cpp calls each only when it is set.
    struct StateHooks
    {
        std::function<void(juce::ValueTree& params)> writePreset;   // save: add the <PRESET> child to <PARAMS>
        std::function<void(juce::ValueTree& params)> readPreset;    // load: read it (inside the load's batch)
    };

    // Everything session state reads and writes. The processor owns every referent and builds one per call, so a
    // StateContext never outlives the processor.
    struct StateContext
    {
        juce::AudioProcessorValueTreeState& apvts;   // the 29 host parameters; state type "PARAMS" (01 §9.1)
        ProcessorFacade& facade;                     // beginBatch()/endBatch() (K2 #23)
        UiState& ui;                                 // <UI charExpanded scTab/> (P2)
        StateNotice& notice;                         // footer notices after a load (P2); serial bumps on every load
        StateHooks& hooks;                           // <PRESET> hooks (P3)
    };

    // getStateInformation (01 §9.1 "Save"): copyState(); stateVersion, modeId and modeRev of the effective slot, product
    // and build on the root; every PARAM's value = the parameter's raw value; writePreset if installed; the <UI> child;
    // copyXmlToBinary.
    void saveState(const StateContext&, juce::MemoryBlock& destData);

    // setStateInformation (01 §9.1 "Load"): anything that is not a <PARAMS> tree is ignored (fail-safe: nothing changes,
    // StateNotice included). Otherwise: stateVersion (absent = 1; newer -> StateNotice::newerSession, best effort; older
    // -> the migrations below), then inside ONE batch every host parameter is written exactly once: its saved value
    // (sanitised: clamped to its range, rounded for Int/Choice), or its table default when the session lacks it or holds
    // no finite number; listen and delta 0 (K2 #25c); `mode` the slot resolveSessionMode chose. apvts.replaceState gets
    // the same values (the saved tree's PARAM children, unknown ids dropped). Then readPreset if installed, the <UI>
    // child (absent attributes -> UiState defaults), endBatch (the snap after the last write), and StateNotice is
    // replaced by this load's notice with serial + 1.
    void loadState(const StateContext&, const void* data, int sizeInBytes);

    // ---- migrations (01 §9.1 load step 2; plugin/StateMigration.cpp) ------------------------------------------------
    // stateMigrations()[i] rewrites a stateVersion i+1 tree (the whole <PARAMS> tree, <PRESET> and <UI> included) as a
    // stateVersion i+2 tree. Exactly fcdsp::kStateVersion - 1 entries, none null; a session of version v runs entries
    // v-1 .. kStateVersion-2 in order. kStateVersion is bumped only when the meaning of a stored plain value changes or a
    // parameter is renamed or removed; adding a parameter never bumps it (absent means default).
    using StateMigration = void (*)(juce::ValueTree& params);
    std::span<const StateMigration> stateMigrations() noexcept;

    // ---- the session's Mode (01 §9.1 load steps 5-6), as a pure function ----------------------------------------------
    // loadState calls it with the real registry; proc.fixtures also calls it with a synthetic one, so the retired-key
    // and revised-Mode branches are proven before any Mode is retired or revised.
    struct ModeLookup
    {
        const fcdsp::ModeSlot* (*resolveKey)(std::string_view key) noexcept = &fcdsp::resolveKey;
        const fcdsp::ModeSlot& (*resolveSlot)(int rawSlot) noexcept = &fcdsp::resolveSlot;
        std::span<const fcdsp::Retired> (*retired)() noexcept = &fcdsp::retired;
    };

    struct SavedMode                                  // what a session says about its Mode
    {
        std::optional<std::string_view> modeId;       // the root's modeId attribute; nullopt when absent or empty
        std::optional<int> slot;                      // the `mode` PARAM, rounded and clamped to 0..127; nullopt: absent
        std::optional<int> modeRev;                   // the root's modeRev attribute, 0..65535; nullopt: absent/invalid
    };

    struct SessionMode
    {
        const fcdsp::ModeSlot* mode = nullptr;        // the Mode to load; nullptr only when the registry is empty
        // Migrated: the modeId is unknown (-> clean) or retired (-> its successor); or, without a modeId, the `mode` slot
        // is retired (-> its successor) or unassigned (-> clean). fromKey: the saved key, or the retired slot's key, or
        // "slot-<n>" for an unassigned slot. Never set for a key or slot that is registered (C §5.7.5).
        bool migrated = false;
        std::string fromKey;
        // Revised: not migrated, and the saved modeRev (absent = 1: every Mode starts at revision 1, K2 #10) is below
        // the loaded Mode's ModeDescriptor::revision. Old behaviour is not emulated in v1.
        bool revised = false;
        std::uint16_t savedRev = 0, currentRev = 0;
    };

    // modeId wins over the `mode` slot (01 §9.1 step 5); without either, the Mode is the `mode` parameter's default
    // (slot 0) and nothing is migrated.
    SessionMode resolveSessionMode(const SavedMode&, const ModeLookup& = {});
} // namespace fcmp
