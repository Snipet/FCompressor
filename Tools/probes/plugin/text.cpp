// FCMP_PROBE layer=proc name=text scope=mode timeout=60
//
// proc.text.<key> (P1, S7; 03 §3.5; 01 §3.1 "Unit / host text", §4.6; K2 #25a-b; HR StateProbe :447-488): the host
// value text of all 29 parameters through the processor's JUCE parameters (getText / getValueForText, the calls hosts
// make), while this Mode is active at its defaults (hostDefaults + modeDefaults, written in one batch).
//
// Rows (spec):
//   text.roundtrip.<id>          getText(getValueForText(getText(v))) == getText(v) over v = k/32 plus every step of the
//                                active spec (and, for Int/Choice/Bool, every index); `mode` over the registered and
//                                retired slots (an unassigned slot prints "—", which is not a Mode): 0 mismatches
//   text.detents.mismatches      every step label (and step text) of every stepped/hybrid active spec parses to that
//                                step: 0
//   text.minus.mismatches        every text holding U+2212 parses to the same value with '-' instead: 0
//   text.na.mismatches           an n/a parameter prints U+2013 at every value, and that text parses to nothing (the
//                                parameter keeps its value): 0
//   text.mode.name               the `mode` text at this slot is the Mode's name, and the name and key parse back: 1
//   text.choices.mismatches      quality/labudget print and parse their choices; the four switches print OFF/ON: 0
//   text.mode_change.announced   one Mode change -> SetupWatcher::poll -> exactly one audioProcessorChanged with
//                                parameterInfoChanged (01 §4.6: updateHostDisplay within 50 ms); back again -> one
//   text.mode_change.quiet       a poll without a change announces nothing: 0
//   text.threads.empty           two threads read every parameter's text while this thread switches Modes: no empty
//                                text (and no crash): 0
// Golden: text.defaults.hash = FNV-1a of the 29 default texts in APVTS order (what a host shows for a fresh instance
// of the Mode); the texts are printed as NOTE lines.
#include "ProbeRegistry.h"

#include "EngineRig.h"

#include "plugin/Processor.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using fcdsp::HostParam;
    using fcdsp::Kind;
    using fcdsp::Map;
    using fcdsp::Pid;
    using funkgui::test::Probe;

    juce::String minusSign() { return juce::String::fromUTF8("\xe2\x88\x92"); }   // U+2212
    juce::String enDash() { return juce::String::fromUTF8("\xe2\x80\x93"); }      // U+2013 (n/a)

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    // The Mode at its defaults (hostDefaults + modeDefaults), in one batch, the way a preset or a state load writes.
    void selectMode(fcmp::Processor& proc, const fcdsp::ModeEntry& en)
    {
        const fcdsp::RawParams raw = fcmp::probe::modeRaw(en);
        proc.beginBatch();
        setPlain(proc, Pid::mode, static_cast<float>(fcdsp::slotOf(en)));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            setPlain(proc, static_cast<Pid>(i), raw.v[i]);
        proc.endBatch();
    }

    fcdsp::ParamView viewOf(const fcmp::Processor& proc, const fcdsp::ModeEntry& en)
    {
        fcdsp::ParamView v;
        fcdsp::resolveView(*en.desc, proc.currentRaw(), v);
        return v;
    }

    juce::String str(const char* s) { return juce::String::fromUTF8(s); }

    struct Announcements final : juce::AudioProcessorListener
    {
        int parameterInfo = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override
        {
            parameterInfo += d.parameterInfoChanged ? 1 : 0;
        }
    };

    std::uint64_t hashText(std::uint64_t h, const juce::String& s)
    {
        const char* u = s.toRawUTF8();
        return funkgui::test::fnv1a(u, std::strlen(u) + 1, h);
    }
} // namespace

FCMP_PROBE(proc, text)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    const fcdsp::ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const int slot = fcdsp::slotOf(en);
    auto proc = std::make_unique<fcmp::Processor>();
    selectMode(*proc, en);
    proc->setupWatcher().poll();                                 // absorb this Mode's own announcement

    const juce::Array<juce::AudioProcessorParameter*>& all = proc->getParameters();

    // ---- defaults (golden) ------------------------------------------------------------------------------------------
    std::uint64_t hash = 1469598103934665603ull;
    for (juce::AudioProcessorParameter* ap : all)
    {
        const juce::String t = ap->getCurrentValueAsText();
        hash = hashText(hash, t);
        std::printf("NOTE     text.default %-9s %s\n", dynamic_cast<juce::RangedAudioParameter*>(ap)->getParameterID()
                                                           .toRawUTF8(), t.toRawUTF8());
    }

    // ---- round trips, minus, n/a ------------------------------------------------------------------------------------
    const fcdsp::ParamView view = viewOf(*proc, en);
    const juce::String kMinus = minusSign(), kEnDash = enDash();
    std::int64_t minusCases = 0, minusBad = 0, naBad = 0, naCount = 0;
    for (juce::AudioProcessorParameter* ap : all)
    {
        auto& p = *dynamic_cast<juce::RangedAudioParameter*>(ap);
        const HostParam* h = nullptr;
        for (const HostParam& row : fcdsp::kHostParams)
            if (p.getParameterID() == juce::String(row.id))
                h = &row;
        if (h == nullptr)
            continue;
        const Pid pid = h->pid;
        const bool modeFiltered = fcdsp::idx(pid) < fcdsp::kNumModeParams;

        std::vector<float> grid;
        if (pid == Pid::mode)
        {
            for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
                grid.push_back(p.convertTo0to1(static_cast<float>(s.slot)));
            for (const fcdsp::Retired& r : fcdsp::retired())
                grid.push_back(p.convertTo0to1(static_cast<float>(r.slot)));
        }
        else
        {
            for (int k = 0; k <= 32; ++k)
                grid.push_back(static_cast<float>(k) / 32.0f);
            if (h->numSteps > 1)
                for (int k = 0; k < h->numSteps; ++k)
                    grid.push_back(static_cast<float>(k) / static_cast<float>(h->numSteps - 1));
            if (modeFiltered && view.spec[fcdsp::idx(pid)] != nullptr)
                for (const fcdsp::Step& s : view.spec[fcdsp::idx(pid)]->steps)
                    grid.push_back(p.convertTo0to1(s.plain));
        }

        const bool na = modeFiltered && view[pid].state == fcdsp::SlotState::na;
        naCount += na ? 1 : 0;
        std::int64_t bad = 0;
        for (const float v : grid)
        {
            const juce::String t1 = p.getText(v, 1024);
            const float v2 = p.getValueForText(t1);
            const juce::String t2 = p.getText(v2, 1024);
            if (t1 != t2 || t1.isEmpty())
            {
                if (bad < 3)
                    std::printf("NOTE     text.roundtrip.%s: '%s' -> %.9g -> '%s'\n", h->id, t1.toRawUTF8(),
                                static_cast<double>(v2), t2.toRawUTF8());
                ++bad;
            }
            if (modeFiltered && t1.contains(kMinus))
            {
                ++minusCases;
                const juce::String ascii = t1.replace(kMinus, "-");
                const juce::String t3 = p.getText(p.getValueForText(ascii), 1024);
                if (t3 != t1)
                {
                    std::printf("NOTE     text.minus.%s: '%s' -> '%s'\n", h->id, ascii.toRawUTF8(), t3.toRawUTF8());
                    ++minusBad;
                }
            }
            if (na)
            {
                const float before = p.getValue();
                if (t1 != kEnDash || p.getValueForText(t1) != before)
                    ++naBad;
            }
        }
        P.eq(std::string("text.roundtrip.") + h->id, bad, 0);
    }
    P.eq("text.minus.mismatches", minusBad, 0);
    P.eq("text.na.mismatches", naBad, 0);
    std::printf("NOTE     text: %lld texts with U+2212, %lld n/a parameters\n", static_cast<long long>(minusCases),
                static_cast<long long>(naCount));

    // ---- detent labels ----------------------------------------------------------------------------------------------
    std::int64_t detents = 0, detentBad = 0;
    for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
    {
        const Pid pid = static_cast<Pid>(i);
        const fcdsp::ParamSpec* spec = view.spec[i];
        if (spec == nullptr || (spec->kind != Kind::stepped && spec->kind != Kind::hybrid))
            continue;
        juce::RangedAudioParameter& p = proc->parameter(pid);
        for (std::size_t k = 0; k < spec->steps.size(); ++k)
        {
            const fcdsp::Step& s = spec->steps[k];
            for (const char* text : { s.label, s.text })
            {
                if (text == nullptr)
                    continue;
                ++detents;
                const float plain = p.convertFrom0to1(p.getValueForText(str(text)));
                const fcdsp::Snapped sn = fcdsp::snap(pid, *spec, plain);
                if (sn.step != static_cast<int>(k))
                {
                    std::printf("NOTE     text.detent.%s: '%s' parsed to %.9g (step %d, want %d)\n",
                                fcdsp::kHostParams[i].id, text, static_cast<double>(plain), sn.step,
                                static_cast<int>(k));
                    ++detentBad;
                }
            }
        }
    }
    P.eq("text.detents.mismatches", detentBad, 0);
    std::printf("NOTE     text: %lld detent labels/texts parsed\n", static_cast<long long>(detents));

    // ---- globals ----------------------------------------------------------------------------------------------------
    {
        juce::RangedAudioParameter& m = proc->parameter(Pid::mode);
        const juce::String name = juce::String::fromUTF8(en.desc->name.data(), static_cast<int>(en.desc->name.size()));
        const juce::String key = juce::String::fromUTF8(C.key.data(), static_cast<int>(C.key.size()));
        const float at = m.convertTo0to1(static_cast<float>(slot));
        const bool ok = m.getText(at, 1024) == name
                     && static_cast<int>(m.convertFrom0to1(m.getValueForText(name))) == slot
                     && static_cast<int>(m.convertFrom0to1(m.getValueForText(key))) == slot
                     && static_cast<int>(m.convertFrom0to1(m.getValueForText(name.toLowerCase()))) == slot;
        P.eq("text.mode.name", ok ? 1 : 0, 1);
    }
    {
        std::int64_t bad = 0;
        for (const Pid pid : { Pid::quality, Pid::labudget })
        {
            juce::RangedAudioParameter& p = proc->parameter(pid);
            const HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
            for (int i = 0; i < h.numSteps; ++i)
            {
                const float v = p.convertTo0to1(static_cast<float>(i));
                bad += p.getText(v, 1024) == str(h.choices[i]) ? 0 : 1;
                bad += static_cast<int>(p.convertFrom0to1(p.getValueForText(str(h.choices[i])))) == i ? 0 : 1;
                bad += static_cast<int>(p.convertFrom0to1(p.getValueForText(str(h.choices[i]).toLowerCase()))) == i
                           ? 0 : 1;
            }
        }
        for (const Pid pid : { Pid::extkey, Pid::listen, Pid::delta, Pid::bypass })
        {
            juce::RangedAudioParameter& p = proc->parameter(pid);
            bad += p.getText(0.0f, 1024) == "OFF" ? 0 : 1;
            bad += p.getText(1.0f, 1024) == "ON" ? 0 : 1;
            bad += p.getValueForText("ON") >= 0.5f ? 0 : 1;
            bad += p.getValueForText("off") < 0.5f ? 0 : 1;
        }
        P.eq("text.choices.mismatches", bad, 0);
    }

    // ---- the Mode-change announcement (SetupWatcher) ----------------------------------------------------------------
    {
        Announcements listener;
        proc->addListener(&listener);
        proc->setupWatcher().poll();
        const int quiet = listener.parameterInfo;
        const int other = slot == 0 ? 1 : 0;
        setPlain(*proc, Pid::mode, static_cast<float>(other));
        proc->setupWatcher().poll();
        proc->setupWatcher().poll();
        const int first = listener.parameterInfo - quiet;
        setPlain(*proc, Pid::mode, static_cast<float>(slot));
        proc->setupWatcher().poll();
        const int second = listener.parameterInfo - quiet - first;
        proc->removeListener(&listener);
        P.eq("text.mode_change.quiet", quiet, 0);
        P.eq("text.mode_change.announced", first, 1);
        P.eq("text.mode_change.announced_back", second, 1);
    }

    // ---- value text from other threads while the Mode changes -------------------------------------------------------
    {
        std::atomic<bool> stop{ false };
        std::atomic<std::int64_t> empty{ 0 }, reads{ 0 };
        auto reader = [&] {
            while (!stop.load(std::memory_order_acquire))
                for (juce::AudioProcessorParameter* ap : all)
                {
                    const juce::String t = ap->getText(ap->getValue(), 1024);
                    empty.fetch_add(t.isEmpty() ? 1 : 0, std::memory_order_relaxed);
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
        };
        std::thread a(reader), b(reader);
        const std::span<const fcdsp::ModeSlot> slots = fcdsp::modeSlots();
        for (int i = 0; i < 200; ++i)
            setPlain(*proc, Pid::mode, static_cast<float>(slots[static_cast<std::size_t>(i) % slots.size()].slot));
        while (reads.load(std::memory_order_relaxed) < 20000)
            std::this_thread::yield();
        stop.store(true, std::memory_order_release);
        a.join();
        b.join();
        P.eq("text.threads.empty", empty.load(), 0);
        std::printf("NOTE     text.threads: %lld texts read on two threads during 200 Mode changes\n",
                    static_cast<long long>(reads.load()));
    }

    P.hash("text.defaults.hash", hash);
    return P.finish();
}
