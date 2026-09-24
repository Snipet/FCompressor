// Source/plugin/ParamLayout.cpp: the APVTS parameter layout (P1, S7; 01 §3.1, §3.3; K2 #9, #14, #25a-b; B §1).
//
// One juce parameter per kHostParams row, added in kApvtsOrder (v1-forever; a v2 parameter is appended there with a
// version hint >= 2 and a neutral default):
//   Map::linear/log/power/ratio3  AudioParameterFloat on the three-lambda NormalisableRange over fcdsp::toPlain,
//                                 toNorm and legal (juce_NormalisableRange.h:104-106): the ONLY normalised<->plain map
//   Map::index with choices       AudioParameterChoice (quality: ECO/STD/HQ, labudget: OFF/5 MS/20 MS)
//   Map::index                    AudioParameterInt lo..hi (tmode, det, stmode, voice: 0..7; mode: 0..127)
//   Map::boolean                  SwitchParameter, an AudioParameterBool whose raw value keeps the host's position
//                                 (automu, extkey, listen, delta, bypass; see the class: pluginval state restoration)
// JUCE's Int/Choice maps are the index map of 01 §3.3 (round(v*(n-1))), so the discrete types keep the host's
// discrete/boolean flags without a second map; the switches apply the boolean map (v >= 0.5) on read. Every
// parameter: ParameterID{id, versionHint}, the
// universal name (K2 #25b), label "" (the unit is part of the value text, K2 #25a), automatable per kHostParams (the
// monitoring and setup parameters are not), and value text from HostText.cpp through lambdas that keep a reference to
// the processor (any thread: currentRaw() is relaxed loads, formatHost/parseHost are pure). A text the parameter does
// not accept parses to the parameter's current value, so a typo never moves it.
#include "plugin/Processor.h"

#include "fcdsp/params/HostParams.h"

#include <cmath>
#include <memory>

namespace fcmp
{
    namespace
    {
        using fcdsp::HostParam;
        using fcdsp::Map;
        using fcdsp::Pid;

        // The host plain value `text` stands for, or the parameter's current value when it does not parse.
        float plainFromText(const Processor& proc, Pid pid, const juce::String& text)
        {
            float plain = 0.0f;
            if (parseHostText(proc, pid, text, plain) && std::isfinite(plain))
                return fcdsp::legal(pid, plain);
            return proc.rawValue(pid);
        }

        int indexFromText(const Processor& proc, Pid pid, const juce::String& text)
        {
            return static_cast<int>(std::lround(plainFromText(proc, pid, text)));
        }

        juce::ParameterID idOf(const HostParam& h) { return juce::ParameterID{ h.id, h.versionHint }; }

        std::unique_ptr<juce::RangedAudioParameter> makeFloat(const Processor& proc, const HostParam& h)
        {
            const Pid pid = h.pid;
            juce::NormalisableRange<float> range(
                h.lo, h.hi,
                [pid](float, float, float norm) { return fcdsp::toPlain(pid, norm); },
                [pid](float, float, float plain) { return fcdsp::toNorm(pid, plain); },
                [pid](float, float, float plain) { return fcdsp::legal(pid, plain); });
            const auto attributes = juce::AudioParameterFloatAttributes()
                                        .withLabel(juce::String())
                                        .withAutomatable(h.automatable)
                                        .withStringFromValueFunction([&proc, pid](float plain, int maximumLength) {
                                            return hostText(proc, pid, plain, maximumLength);
                                        })
                                        .withValueFromStringFunction([&proc, pid](const juce::String& text) {
                                            return plainFromText(proc, pid, text);
                                        });
            return std::make_unique<juce::AudioParameterFloat>(idOf(h), h.name, range, h.def, attributes);
        }

        std::unique_ptr<juce::RangedAudioParameter> makeInt(const Processor& proc, const HostParam& h)
        {
            const Pid pid = h.pid;
            const auto attributes = juce::AudioParameterIntAttributes()
                                        .withLabel(juce::String())
                                        .withAutomatable(h.automatable)
                                        .withStringFromValueFunction([&proc, pid](int value, int maximumLength) {
                                            return hostText(proc, pid, static_cast<float>(value), maximumLength);
                                        })
                                        .withValueFromStringFunction([&proc, pid](const juce::String& text) {
                                            return indexFromText(proc, pid, text);
                                        });
            return std::make_unique<juce::AudioParameterInt>(idOf(h), h.name, static_cast<int>(h.lo),
                                                             static_cast<int>(h.hi), static_cast<int>(h.def),
                                                             attributes);
        }

        std::unique_ptr<juce::RangedAudioParameter> makeChoice(const Processor& proc, const HostParam& h)
        {
            const Pid pid = h.pid;
            juce::StringArray choices;
            for (int i = 0; i < h.numSteps; ++i)
                choices.add(h.choices[i]);
            const auto attributes = juce::AudioParameterChoiceAttributes()
                                        .withLabel(juce::String())
                                        .withAutomatable(h.automatable)
                                        .withStringFromValueFunction([&proc, pid](int index, int maximumLength) {
                                            return hostText(proc, pid, static_cast<float>(index), maximumLength);
                                        })
                                        .withValueFromStringFunction([&proc, pid](const juce::String& text) {
                                            return indexFromText(proc, pid, text);
                                        });
            return std::make_unique<juce::AudioParameterChoice>(idOf(h), h.name, choices, static_cast<int>(h.def),
                                                                attributes);
        }

        // A switch (Map::boolean) whose APVTS raw value keeps the host's normalised position instead of JUCE's rounded
        // 0/1: AudioParameterBool's own range snaps, so the state stores 0 or 1 while the host still holds the
        // position it wrote (JUCE's VST3 host caches it), and restoring a state can then never give the host back the
        // value it set (pluginval's state restoration, strictness >= 6, fails an automatable switch four times in
        // five). Every reader applies the boolean map on read: the resolver snaps automu to its OFF/ON steps, and
        // the processor and HostText read a global switch as ON when raw >= 0.5 (01 §3.3's `v >= 0.5`). The value
        // text resolves the position itself, so host text, UI and engine agree even at the exact midpoint.
        class SwitchParameter final : public juce::AudioParameterBool
        {
        public:
            SwitchParameter(const Processor& proc, const HostParam& h, const juce::AudioParameterBoolAttributes& a)
                : juce::AudioParameterBool(juce::ParameterID{ h.id, h.versionHint }, h.name, h.def >= 0.5f, a),
                  proc_(proc), pid_(h.pid)
            {
            }

            const juce::NormalisableRange<float>& getNormalisableRange() const override { return positions_; }

        private:
            juce::String getText(float position, int maximumLength) const override
            {
                return hostText(proc_, pid_, position, maximumLength);
            }

            const Processor& proc_;
            Pid pid_;
            juce::NormalisableRange<float> positions_{ 0.0f, 1.0f };   // continuous: no snap to 0/1
        };

        std::unique_ptr<juce::RangedAudioParameter> makeBool(const Processor& proc, const HostParam& h)
        {
            const Pid pid = h.pid;
            const auto attributes = juce::AudioParameterBoolAttributes()
                                        .withLabel(juce::String())
                                        .withAutomatable(h.automatable)
                                        .withStringFromValueFunction([&proc, pid](bool value, int maximumLength) {
                                            return hostText(proc, pid, value ? 1.0f : 0.0f, maximumLength);
                                        })
                                        .withValueFromStringFunction([&proc, pid](const juce::String& text) {
                                            return plainFromText(proc, pid, text) >= 0.5f;
                                        });
            return std::make_unique<SwitchParameter>(proc, h, attributes);
        }

        std::unique_ptr<juce::RangedAudioParameter> makeParameter(const Processor& proc, const HostParam& h)
        {
            switch (h.map)
            {
                case Map::index:   return h.choices != nullptr ? makeChoice(proc, h) : makeInt(proc, h);
                case Map::boolean: return makeBool(proc, h);
                case Map::linear:
                case Map::log:
                case Map::power:
                case Map::ratio3:  break;
            }
            return makeFloat(proc, h);
        }
    } // namespace

    juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout(const Processor& proc)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (const Pid pid : fcdsp::kApvtsOrder)
            layout.add(makeParameter(proc, fcdsp::kHostParams[fcdsp::idx(pid)]));
        return layout;
    }
} // namespace fcmp
