// Source/plugin/State.h: session state save/load (01 §9.1; K2 #23, #25c; K3 #14). The entry points are frozen from S7
// (SPRINTS §7 D20): P1 writes them with an APVTS-only body, P2 (S8) replaces State.cpp's bodies (stateVersion, modeId,
// modeRev, migrations, StateNotice, the <UI> child) and P3 (S12) installs the <PRESET> hooks from Presets.cpp. Neither
// ever edits Processor.cpp: the processor hands both everything they touch through StateContext.
//
// Threads: whatever thread the host calls get/setStateInformation on (usually the message thread). loadState brackets
// every parameter write with facade.beginBatch()/endBatch(), so the audio thread keeps the previous BlockParams until
// the whole set is written, and endBatch() raises the engine snap after the last write (K2 #23).
//
// State.cpp owns <PARAMS> and never links FunkPresets (K3 #14): the <PRESET> child goes through StateHooks, which stay
// null until P3 installs them.
#pragma once

#include "plugin/ProcessorFacade.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

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

    // getStateInformation: <PARAMS product build> with one <PARAM id value/> per host parameter (plain units), then
    // copyXmlToBinary.
    void saveState(const StateContext&, juce::MemoryBlock& destData);

    // setStateInformation: anything that is not a <PARAMS> tree is ignored (fail-safe). Inside one batch: replaceState,
    // absent parameters back to their defaults (HR's loop), listen and delta reset to 0 (K2 #25c), then readPreset.
    void loadState(const StateContext&, const void* data, int sizeInBytes);
} // namespace fcmp
