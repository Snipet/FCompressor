// Source/web/facade/HostValue.h: one host parameter as the plugin holds it, for a facade with no JUCE under it
// (web Sprint C, ADR-93).
//
// THIS RESTATES JUCE, ON PURPOSE. In the plugin a parameter is a juce::RangedAudioParameter inside an
// AudioProcessorValueTreeState, and what the engine is given is the APVTS's raw value, which is NOT simply
// fcdsp::toPlain of what the editor wrote: JUCE keeps two values per parameter and moves the second only when the
// first has moved "enough". The browser demo must hand the engine the plugin's values bit for bit (its promise is "the
// plugin's exact DSP"), so the rules are written out here, each with the JUCE 8.0.4 source it restates, and
// proc.webnull compares every raw value with a real Processor's after every step of its script: a JUCE upgrade that
// changes a rule fails that probe, and this file is then corrected to match. Nothing else in the facade knows JUCE's
// rules.
//
// The two values (HostValue):
//   param   the parameter object's own value. AudioParameterFloat/Int/Choice hold the PLAIN value
//           (juce_AudioParameterFloat.cpp:98, juce_AudioParameterInt.cpp:72, juce_AudioParameterChoice.cpp:75: setValue
//           stores convertFrom0to1(v)); FCompressor's SwitchParameter (Source/plugin/ParamLayout.cpp:112-132) is an
//           AudioParameterBool, which holds the host's POSITION as written (juce_AudioParameterBool.cpp:79).
//   raw     the APVTS's "unnormalised value" (juce_AudioProcessorValueTreeState.cpp:208): the atomic the processor
//           reads for every block (Processor::rawValue), so the value the engine is given.
//   heard   the APVTS adapter has stored a notification (JUCE's `! listenersNeedCalling`, :209).
// A fresh instance: param = the default as the parameter's constructor stores it, raw = kHostParams' default exactly
// (Processor.cpp:142-146 overwrites the adapter's own start value), heard = false.
//
// The writes:
//   setValueNotifyingHost(v)   juce_AudioProcessor.cpp:1530-1534: setValue(v), then the listeners. The adapter
//                              (juce_AudioProcessorValueTreeState.cpp:148-159) computes convertFrom0to1(getValue())
//                              and keeps the OLD raw value when the two are approximatelyEqual and it has heard a
//                              notification before; else it stores the new one. So a write one float step away from
//                              the current value moves `param` and not `raw`.
//   writeExact(plain)          Processor::HistoryHost::write (Processor.cpp:469-480): what undo, redo and an A/B switch
//                              write. The host hears the normalised value unless the parameter already reports it,
//                              then the raw value is stored exactly.
// A preset's write (FunkGui PresetManager::apply, src/presets/PresetManager.cpp:149-162) is setValueNotifyingHost
// behind a bitwise "the parameter already reports this" test; WebPresets states that rule beside its list code.
//
// The maps are juce::RangedAudioParameter's (juce_RangedAudioParameter.cpp:48-58) over the NormalisableRange each
// parameter type builds:
//   continuous  ParamLayout.cpp:52-56, the three lambdas over fcdsp::toPlain, toNorm and legal
//   integer     juce_AudioParameterInt.cpp:44-48 (and juce_AudioParameterChoice.cpp:47-51, which is the same map with
//               start 0): round-half-to-even through juce::roundToInt (juce_MathsFunctions.h:596-611), in float, so
//               not fcdsp::toPlain's index map (std::round, in double); the two agree on every position the editor
//               writes (k / (n - 1)) and may differ at a tie
//   toggle      ParamLayout.cpp:121, :131, a continuous 0..1 range: the position, clamped
//
// Portable C++ over fcdsp/params alone (lint web.facade).
#pragma once

#include "fcdsp/params/Pid.h"

#include <cstdint>

namespace fcmp::web
{
    enum class HostKind : std::uint8_t { continuous, integer, toggle };   // ParamLayout.cpp makeParameter
    HostKind hostKind(fcdsp::Pid) noexcept;

    // juce::RangedAudioParameter of the parameter `pid`: convertFrom0to1 (normalised -> plain, snapped),
    // convertTo0to1 (plain -> normalised, the plain value snapped first), its range's snapToLegalValue, and the
    // parameter's getDefaultValue() and getNumSteps().
    float convertFrom0to1(fcdsp::Pid, float norm) noexcept;
    float convertTo0to1(fcdsp::Pid, float plain) noexcept;
    float snapToLegalValue(fcdsp::Pid, float plain) noexcept;
    float defaultValue01(fcdsp::Pid) noexcept;
    int   numSteps(fcdsp::Pid) noexcept;

    // juce::approximatelyEqual<float> with its default tolerances (juce_MathsFunctions.h:310-323): within FLT_MIN, or
    // within FLT_EPSILON of the larger magnitude; a non-finite operand compares exactly.
    bool approximatelyEqual(float a, float b) noexcept;

    struct HostValue
    {
        float param = 0.0f;
        float raw = 0.0f;
        bool  heard = false;

        static HostValue fresh(fcdsp::Pid) noexcept;

        float value01(fcdsp::Pid) const noexcept;                    // the parameter's getValue()
        // Each returns whether `raw` now holds other bits than before.
        bool  setValueNotifyingHost(fcdsp::Pid, float norm) noexcept;
        bool  writeExact(fcdsp::Pid, float plain) noexcept;
    };
} // namespace fcmp::web
