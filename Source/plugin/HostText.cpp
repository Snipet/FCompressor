// Source/plugin/HostText.cpp: host value text for the 30 parameters (P1, S7; 01 §3.1 "Unit / host text", §4.6; K2
// #25a-b; S0 review R-F0 #5).
//
// Mode-filtered Pids (idx < kNumModeParams) go through fcdsp::formatHost / parseHost with the processor's currentRaw()
// (relaxed loads + the configured lookahead budget), so a host lane prints exactly what the UI and the engine resolve:
// the active Mode's step texts, display scales, locks ("(10 MS)"), derived values ("(= 0.8 MS)"), "–" for n/a, U+2212
// for minus. Those functions assert on the 7 globals, which are formatted here instead:
//   mode                           the registered Mode's name (desc->name); a retired slot prints its successor's
//                                  name (resolveSlot); an unassigned slot prints "—" (01 §3.1). Parses a Mode name or
//                                  key (any case; a retired key gives its own slot) or a slot number 0..127.
//   quality, labudget              kHostParams choices ("ECO"/"STD"/"HQ", "OFF"/"5 MS"/"20 MS"); parse any case,
//                                  spaces ignored ("5ms"), or the index.
//   extkey, listen, delta, bypass  "OFF"/"ON" (kit::kOffOn's labels, 01 §4.6); parse OFF/ON, 0/1, FALSE/TRUE, and
//                                  EXT for extkey (01 §3.1's "OFF/EXT").
//   output (v1.2, ADR-88)          fcdsp::formatOutput / parseOutput ("−3.0 DB"), the UI's text.
// Thread-safe (JUCE calls value text from any thread; validate.sh's pluginval exercises it): no state, relaxed loads,
// pure fcdsp functions over stack buffers. Not for the audio thread (juce::String allocates).
#include "plugin/Processor.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Text.h"

#include <cmath>
#include <string_view>

namespace fcmp
{
    namespace
    {
        using fcdsp::Pid;

        constexpr int kTextCapacity = 96;                     // formatValue: prefix + 23-byte value + ' ' + unit + ')'
        constexpr const char* kUnassignedUtf8 = "\xe2\x80\x94";   // U+2014 "—": an unassigned Mode slot (01 §3.1)

        int slotOf(float plain) noexcept
        {
            if (!(plain > 0.0f))
                return 0;
            if (plain >= static_cast<float>(fcdsp::kModeCapacity - 1))
                return fcdsp::kModeCapacity - 1;
            return static_cast<int>(plain + 0.5f);
        }

        int choiceOf(const fcdsp::HostParam& h, float plain) noexcept
        {
            const int hi = h.numSteps - 1;
            if (!(plain > 0.0f))
                return 0;
            return plain >= static_cast<float>(hi) ? hi : static_cast<int>(plain + 0.5f);
        }

        // A name for comparison: upper case, without spaces.
        juce::String folded(const juce::String& s) { return s.toUpperCase().removeCharacters(" \t"); }

        // A plain unsigned decimal integer ("0", "17"), or -1.
        int integerOf(const juce::String& s)
        {
            if (s.isEmpty() || s.length() > 4 || !s.containsOnly("0123456789"))
                return -1;
            return s.getIntValue();
        }

        juce::String modeText(float plain)
        {
            const int slot = slotOf(plain);
            if (const fcdsp::ModeEntry* e = fcdsp::bySlot(slot); e != nullptr && e->desc != nullptr)
                return juce::String::fromUTF8(e->desc->name.data(), static_cast<int>(e->desc->name.size()));
            for (const fcdsp::Retired& r : fcdsp::retired())
                if (r.slot == slot)
                {
                    const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(slot);
                    if (ms.entry != nullptr && ms.entry->desc != nullptr)
                        return juce::String::fromUTF8(ms.entry->desc->name.data(),
                                                      static_cast<int>(ms.entry->desc->name.size()));
                }
            return juce::String::fromUTF8(kUnassignedUtf8);
        }

        bool parseMode(const juce::String& text, float& plainOut)
        {
            const juce::String t = folded(text.trim());
            if (t.isEmpty())
                return false;
            for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
            {
                const juce::String key = juce::String::fromUTF8(s.key.data(), static_cast<int>(s.key.size()));
                juce::String name;
                if (s.entry != nullptr && s.entry->desc != nullptr)
                    name = juce::String::fromUTF8(s.entry->desc->name.data(),
                                                  static_cast<int>(s.entry->desc->name.size()));
                if (t == folded(key) || (name.isNotEmpty() && t == folded(name)))
                {
                    plainOut = static_cast<float>(s.slot);
                    return true;
                }
            }
            for (const fcdsp::Retired& r : fcdsp::retired())
                if (t == folded(juce::String::fromUTF8(r.key.data(), static_cast<int>(r.key.size()))))
                {
                    plainOut = static_cast<float>(r.slot);
                    return true;
                }
            const int n = integerOf(t);
            if (n >= 0 && n < fcdsp::kModeCapacity)
            {
                plainOut = static_cast<float>(n);
                return true;
            }
            return false;
        }

        bool parseChoice(const fcdsp::HostParam& h, const juce::String& text, float& plainOut)
        {
            const juce::String t = folded(text.trim());
            for (int i = 0; i < h.numSteps; ++i)
                if (t == folded(juce::String(h.choices[i])))
                {
                    plainOut = static_cast<float>(i);
                    return true;
                }
            const int n = integerOf(t);
            if (n >= 0 && n < h.numSteps)
            {
                plainOut = static_cast<float>(n);
                return true;
            }
            return false;
        }

        bool parseSwitch(Pid pid, const juce::String& text, float& plainOut)
        {
            const juce::String t = folded(text.trim());
            if (t == "ON" || t == "1" || t == "TRUE" || (pid == Pid::extkey && t == "EXT"))
            {
                plainOut = 1.0f;
                return true;
            }
            if (t == "OFF" || t == "0" || t == "FALSE")
            {
                plainOut = 0.0f;
                return true;
            }
            return false;
        }

        juce::String globalText(Pid pid, float plain)
        {
            const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
            if (pid == Pid::mode)
                return modeText(plain);
            if (h.choices != nullptr)
                return juce::String(h.choices[choiceOf(h, plain)]);
            if (pid == Pid::output)
            {
                char buf[kTextCapacity];
                fcdsp::formatOutput(plain, buf, kTextCapacity);
                return juce::String::fromUTF8(buf);
            }
            return plain >= 0.5f ? "ON" : "OFF";                  // extkey, listen, delta, bypass
        }
    } // namespace

    juce::String hostText(const Processor& proc, Pid pid, float plain, int maximumLength)
    {
        juce::String text;
        const std::size_t i = fcdsp::idx(pid);
        if (i < fcdsp::kNumModeParams)
        {
            const fcdsp::RawParams raw = proc.currentRaw();
            const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
            if (ms.entry != nullptr)
            {
                char buf[kTextCapacity];
                fcdsp::formatHost(*ms.entry, raw, pid, plain, buf, kTextCapacity);
                text = juce::String::fromUTF8(buf);
            }
            else
                text = juce::String(plain, 2);                    // an empty registry (never shipped)
        }
        else if (i < fcdsp::kNumParams)
            text = globalText(pid, plain);
        if (maximumLength > 0 && text.length() > maximumLength)
            text = text.substring(0, maximumLength);
        return text;
    }

    bool parseHostText(const Processor& proc, Pid pid, const juce::String& text, float& plainOut)
    {
        const std::size_t i = fcdsp::idx(pid);
        if (i < fcdsp::kNumModeParams)
        {
            const fcdsp::RawParams raw = proc.currentRaw();
            const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
            if (ms.entry == nullptr)
                return false;
            const char* utf8 = text.toRawUTF8();                  // valid while `text` lives
            return fcdsp::parseHost(*ms.entry, raw, pid, std::string_view(utf8), plainOut);
        }
        if (i >= fcdsp::kNumParams)
            return false;
        if (pid == Pid::mode)
            return parseMode(text, plainOut);
        const fcdsp::HostParam& h = fcdsp::kHostParams[i];
        if (h.choices != nullptr)
            return parseChoice(h, text, plainOut);
        if (pid == Pid::output)
            return fcdsp::parseOutput(std::string_view(text.toRawUTF8()), plainOut);
        return parseSwitch(pid, text, plainOut);
    }
} // namespace fcmp
