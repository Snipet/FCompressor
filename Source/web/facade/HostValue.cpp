// Source/web/facade/HostValue.cpp: see HostValue.h. Every function names the JUCE 8.0.4 code it restates (paths under
// modules/); proc.webnull holds the whole of it to a real Processor's raw values.
#include "web/facade/HostValue.h"

#include "fcdsp/params/HostParams.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace fcmp::web
{
    namespace
    {
        using fcdsp::HostParam;
        using fcdsp::Map;
        using fcdsp::Pid;

        static_assert(std::endian::native == std::endian::little, "roundToInt reads the low word of a double");

        const HostParam& hostParam(Pid p) noexcept
        {
            return fcdsp::kHostParams[fcdsp::idx(p) < fcdsp::kNumParams ? fcdsp::idx(p) : 0];
        }

        bool sameBits(float a, float b) noexcept
        {
            return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
        }

        // juce::jlimit (juce_core/maths/juce_MathsFunctions.h:519-529). A NaN passes through.
        float jlimit(float lo, float hi, float v) noexcept { return v < lo ? lo : (hi < v ? hi : v); }

        // NormalisableRange::clampTo0To1 (juce_core/maths/juce_NormalisableRange.h:259-268).
        float clamp01(float v) noexcept { return jlimit(0.0f, 1.0f, v); }

        // juce::roundToInt (juce_MathsFunctions.h:596-611): the low word of value + 1.5 * 2^52, so a tie goes to the
        // even neighbour. Spelled as JUCE spells it (not std::nearbyint), so every input gives JUCE's answer.
        float roundToInt(float v) noexcept
        {
            const double biased = static_cast<double>(v) + 6755399441055744.0;
            std::int32_t low = 0;
            std::memcpy(&low, &biased, sizeof low);
            return static_cast<float>(low);
        }

        // ---- NormalisableRange<float> of each parameter type (HostValue.h names where each is built) ----------------
        // convertFrom0to1 (juce_NormalisableRange.h:160-185).
        float rangeFrom01(Pid p, float proportion) noexcept
        {
            const HostParam& h = hostParam(p);
            proportion = clamp01(proportion);
            switch (hostKind(p))
            {
                case HostKind::continuous: return fcdsp::toPlain(p, proportion);
                case HostKind::integer:    return jlimit(h.lo, h.hi, proportion * (h.hi - h.lo) + h.lo);
                case HostKind::toggle:     break;
            }
            return 0.0f + (1.0f - 0.0f) * proportion;            // :172, no skew: start + (end - start) * proportion
        }

        // convertTo0to1 (:136-155).
        float rangeTo01(Pid p, float v) noexcept
        {
            const HostParam& h = hostParam(p);
            switch (hostKind(p))
            {
                case HostKind::continuous: return clamp01(fcdsp::toNorm(p, v));
                case HostKind::integer:    return clamp01(jlimit(0.0f, 1.0f, (v - h.lo) / (h.hi - h.lo)));
                case HostKind::toggle:     break;
            }
            return clamp01((v - 0.0f) / (1.0f - 0.0f));
        }
    } // namespace

    HostKind hostKind(Pid p) noexcept
    {
        switch (hostParam(p).map)
        {
            case Map::index:   return HostKind::integer;         // AudioParameterInt, or AudioParameterChoice
            case Map::boolean: return HostKind::toggle;          // SwitchParameter
            case Map::linear:
            case Map::log:
            case Map::power:
            case Map::ratio3:  break;
        }
        return HostKind::continuous;                             // AudioParameterFloat
    }

    // snapToLegalValue (juce_NormalisableRange.h:188-197).
    float snapToLegalValue(Pid p, float v) noexcept
    {
        const HostParam& h = hostParam(p);
        switch (hostKind(p))
        {
            case HostKind::continuous: return fcdsp::legal(p, v);
            case HostKind::integer:    return roundToInt(jlimit(h.lo, h.hi, v));
            case HostKind::toggle:     break;
        }
        return v <= 0.0f ? 0.0f : (v >= 1.0f ? 1.0f : v);        // :196, interval 0
    }

    // RangedAudioParameter::convertFrom0to1 and convertTo0to1 (juce_audio_processors/utilities/
    // juce_RangedAudioParameter.cpp:54-58, :48-52).
    float convertFrom0to1(Pid p, float norm) noexcept
    {
        return snapToLegalValue(p, rangeFrom01(p, jlimit(0.0f, 1.0f, norm)));
    }

    float convertTo0to1(Pid p, float plain) noexcept { return rangeTo01(p, snapToLegalValue(p, plain)); }

    // getDefaultValue(): juce_AudioParameterFloat.cpp:99, juce_AudioParameterInt.cpp:52, :73,
    // juce_AudioParameterChoice.cpp:55, :76, juce_AudioParameterBool.cpp:44, :80, with the defaults ParamLayout.cpp
    // passes (:82, :101, :116).
    float defaultValue01(Pid p) noexcept
    {
        const HostParam& h = hostParam(p);
        switch (hostKind(p))
        {
            case HostKind::continuous: return convertTo0to1(p, h.def);
            case HostKind::integer:    return convertTo0to1(p, static_cast<float>(static_cast<int>(h.def)));
            case HostKind::toggle:     break;
        }
        return h.def >= 0.5f ? 1.0f : 0.0f;
    }

    // getNumSteps(): juce_AudioParameterFloat.cpp:100 (AudioProcessor::getDefaultNumParameterSteps(),
    // juce_audio_processors/processors/juce_AudioProcessor.cpp:578-581), juce_AudioParameterInt.cpp:74,
    // juce_AudioParameterChoice.cpp:77, juce_AudioParameterBool.cpp:81.
    int numSteps(Pid p) noexcept
    {
        const HostParam& h = hostParam(p);
        switch (hostKind(p))
        {
            case HostKind::continuous: return 0x7fffffff;
            case HostKind::integer:    return h.choices != nullptr ? h.numSteps : static_cast<int>(h.hi - h.lo) + 1;
            case HostKind::toggle:     break;
        }
        return 2;
    }

    bool approximatelyEqual(float a, float b) noexcept
    {
        if (!(std::isfinite(a) && std::isfinite(b)))
            return a == b;
        const float diff = std::abs(a - b);
        return diff <= FLT_MIN || diff <= FLT_EPSILON * std::max(std::abs(a), std::abs(b));
    }

    // The parameter's constructor (juce_AudioParameterFloat.cpp:45, juce_AudioParameterInt.cpp:51,
    // juce_AudioParameterChoice.cpp:54, juce_AudioParameterBool.cpp:43), then Processor.cpp:142-146 for the raw value.
    HostValue HostValue::fresh(Pid p) noexcept
    {
        const HostParam& h = hostParam(p);
        HostValue v;
        switch (hostKind(p))
        {
            case HostKind::continuous: v.param = h.def; break;
            case HostKind::integer:    v.param = static_cast<float>(static_cast<int>(h.def)); break;
            case HostKind::toggle:     v.param = h.def >= 0.5f ? 1.0f : 0.0f; break;
        }
        v.raw = h.def;
        return v;
    }

    // getValue(): juce_AudioParameterFloat.cpp:97, juce_AudioParameterInt.cpp:71, juce_AudioParameterChoice.cpp:74,
    // juce_AudioParameterBool.cpp:78.
    float HostValue::value01(Pid p) const noexcept
    {
        return hostKind(p) == HostKind::toggle ? param : convertTo0to1(p, param);
    }

    // AudioProcessorParameter::setValueNotifyingHost (juce_AudioProcessor.cpp:1530-1534): setValue (:98, :72, :75 and
    // :79 of the four parameter files), then ParameterAdapter::parameterValueChanged
    // (juce_AudioProcessorValueTreeState.cpp:148-159), which reads the parameter back and not the value it was told.
    bool HostValue::setValueNotifyingHost(Pid p, float norm) noexcept
    {
        param = hostKind(p) == HostKind::toggle ? norm : convertFrom0to1(p, norm);
        const float now = convertFrom0to1(p, value01(p));
        if (heard && approximatelyEqual(raw, now))
            return false;                                        // "equal": the raw value stays as it was
        const bool moved = !sameBits(raw, now);
        raw = now;
        heard = true;
        return moved;
    }

    // Processor::HistoryHost::write (Source/plugin/Processor.cpp:469-480; the gesture around it is the host's).
    bool HostValue::writeExact(Pid p, float plain) noexcept
    {
        const float before = raw;
        if (const float norm = convertTo0to1(p, plain); value01(p) != norm)
            setValueNotifyingHost(p, norm);
        raw = plain;
        return !sameBits(before, raw);
    }
} // namespace fcmp::web
