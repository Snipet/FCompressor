// FCMP_PROBE layer=proc name=diagnostics scope=global timeout=120
//
// proc.diagnostics (v1.2, ADR-85): what the real processor reports to the settings screen, and the machine's defaults for
// new instances. FCMP_PREFS_DIR is the test's sandbox (CTest; run by hand without it, the probe makes a scratch
// directory before anything reads the store, so it never touches the user's real preferences). Spec rows only.
//
//   diag.unprepared.*   a fresh instance: not prepared, rate 0, the product's, FunkGui's and JUCE's versions
//                       (FcmpProduct.h), no format (a probe is no wrapper), no blocks
//   diag.prepared.*     after prepareToPlay(44 100, 256) with HQ and 5 MS set: prepared, the rate, the block, 2 in 2 out,
//                       the configured quality and budget, the latency the host is told
//   diag.load.*         200 blocks of noise: blocks 200, 0 < load < 1, peak >= load, overruns <= blocks (a fast machine:
//                       0), and a second prepareToPlay zeroes them
//   new.*               with kPrefNewQuality = HQ and kPrefNewLookahead = 20 MS in the store, a new instance starts at
//                       HQ / 20 MS and reports that latency before any prepare; a state saved at ECO / OFF loads ECO / OFF
//                       (a session keeps its own); a value that is not a decimal integer, or out of range, is the table's
//                       default; with no store file the defaults hold (the sandbox's first instance)
#include "ProbeRegistry.h"

#include "plugin/Processor.h"
#include "plugin/ProcessorFacade.h"

#include "FcmpProduct.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Setup.h"

#include <funkgui/prefs/UiPreferences.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace
{
    using fcdsp::Pid;
    using funkgui::test::Probe;

    constexpr const char* kPrefsDirVar = "FCMP_PREFS_DIR";

    int b(bool v) { return v ? 1 : 0; }

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    void prepare(fcmp::Processor& proc, double fs, int block)
    {
        proc.setRateAndBufferSizeDetails(fs, block);
        proc.prepareToPlay(fs, block);
    }

    // The store's file, as UiPreferences opened it; the probe writes its values through the store, as the settings
    // screen does, and removes a key by writing the file's XML without it.
    juce::File storeFile() { return funkgui::UiPreferences::get().file(); }

    void writeRaw(const char* key, const char* value)
    {
        const juce::File f = storeFile();
        std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(f);
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement>("PROPERTIES");
        juce::XmlElement* e = nullptr;
        for (juce::XmlElement* c : xml->getChildIterator())
            if (c->hasTagName("VALUE") && c->getStringAttribute("name") == key)
                e = c;
        if (e == nullptr)
        {
            e = xml->createNewChildElement("VALUE");
            e->setAttribute("name", key);
        }
        e->setAttribute("val", value);
        xml->writeTo(f);
    }

    void unprepared(Probe& P)
    {
        fcmp::Processor proc;
        const fcmp::Diagnostics d = proc.diagnostics();
        P.eq("diag.unprepared.not_prepared", b(!d.prepared && d.sampleRate == 0.0 && d.maxBlock == 0), 1);
        P.eq("diag.unprepared.versions", b(std::strcmp(d.version, fcmp::product::kVersion) == 0
                                           && std::strcmp(d.funkgui, fcmp::product::kFunkGuiVersion) == 0
                                           && d.funkgui[0] != '\0' && std::strcmp(d.juce, "8.0.4") == 0), 1);
        P.eq("diag.unprepared.no_format", b(d.format[0] == '\0'), 1);
        P.eq("diag.unprepared.no_blocks", static_cast<int64_t>(d.blocks), 0);
        P.eq("diag.unprepared.latency", d.latencySamples, proc.getLatencySamples());
    }

    void prepared(Probe& P)
    {
        fcmp::Processor proc;
        setPlain(proc, Pid::quality, 2.0f);
        setPlain(proc, Pid::labudget, 1.0f);
        prepare(proc, 44100.0, 256);
        const fcmp::Diagnostics d = proc.diagnostics();
        P.eq("diag.prepared.prepared", b(d.prepared && d.sampleRate == 44100.0 && d.maxBlock == 256), 1);
        P.eq("diag.prepared.channels", b(d.mainIns == 2 && d.mainOuts == 2), 1);
        P.eq("diag.prepared.setup", b(d.quality == 2 && d.budget == 1), 1);
        P.eq("diag.prepared.latency", d.latencySamples, proc.getLatencySamples());

        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        juce::Random rng(1234);
        for (int k = 0; k < 200; ++k)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    buf.setSample(c, i, 0.25f * (2.0f * rng.nextFloat() - 1.0f));
            proc.processBlock(buf, midi);
        }
        const fcmp::Diagnostics l = proc.diagnostics();
        std::printf("NOTE     diag.load: avg %.4f, peak %.4f, overruns %u of %u\n", static_cast<double>(l.loadAvg),
                    static_cast<double>(l.loadPeak), l.overruns, l.blocks);
        P.eq("diag.load.blocks", static_cast<int64_t>(l.blocks), 200);
        P.eq("diag.load.avg_in_range", b(l.loadAvg > 0.0f && l.loadAvg < 1.0f), 1);
        P.eq("diag.load.peak_over_avg", b(l.loadPeak >= l.loadAvg), 1);
        P.le("diag.load.overruns", static_cast<double>(l.overruns), static_cast<double>(l.blocks));
        prepare(proc, 48000.0, 512);
        const fcmp::Diagnostics z = proc.diagnostics();
        P.eq("diag.load.prepare_zeroes", b(z.blocks == 0 && z.overruns == 0 && z.loadAvg == 0.0f && z.loadPeak == 0.0f),
             1);
    }

    int rawIndex(const fcmp::Processor& proc, Pid pid)
    {
        return static_cast<int>(std::lround(proc.rawValue(pid)));
    }

    void newInstances(Probe& P)
    {
        const int defQuality = static_cast<int>(std::lround(fcdsp::kHostParams[fcdsp::idx(Pid::quality)].def));
        const int defBudget = static_cast<int>(std::lround(fcdsp::kHostParams[fcdsp::idx(Pid::labudget)].def));
        {
            fcmp::Processor fresh;                               // the sandbox has no store values yet
            P.eq("new.no_store_defaults", b(rawIndex(fresh, Pid::quality) == defQuality
                                            && rawIndex(fresh, Pid::labudget) == defBudget), 1);
        }

        funkgui::UiPreferences& prefs = funkgui::UiPreferences::get();
        prefs.setInt(fcmp::kPrefNewQuality, 2);
        prefs.setInt(fcmp::kPrefNewLookahead, 2);
        juce::MemoryBlock ecoState;
        {
            fcmp::Processor proc;
            P.eq("new.quality_from_store", rawIndex(proc, Pid::quality), 2);
            P.eq("new.lookahead_from_store", rawIndex(proc, Pid::labudget), 2);
            P.eq("new.parameter_agrees", b(std::lround(proc.parameter(Pid::quality).convertFrom0to1(
                                               proc.parameter(Pid::quality).getValue())) == 2), 1);
            fcdsp::HostConfig cfg;
            cfg.quality = fcdsp::Quality::hq;
            cfg.budget = fcdsp::LookaheadBudget::ms20;
            P.eq("new.latency_before_prepare", proc.getLatencySamples(), fcdsp::EngineHost::latencyFor(cfg));
            setPlain(proc, Pid::quality, 0.0f);
            setPlain(proc, Pid::labudget, 0.0f);
            proc.getStateInformation(ecoState);
        }
        {
            fcmp::Processor proc;                                // starts at HQ / 20 MS, then a session at ECO / OFF
            proc.setStateInformation(ecoState.getData(), static_cast<int>(ecoState.getSize()));
            P.eq("new.session_keeps_its_own", b(rawIndex(proc, Pid::quality) == 0 && rawIndex(proc, Pid::labudget) == 0),
                 1);
        }

        writeRaw(fcmp::kPrefNewQuality, "2x");
        writeRaw(fcmp::kPrefNewLookahead, "7");
        {
            fcmp::Processor proc;
            P.eq("new.damaged_is_default", rawIndex(proc, Pid::quality), defQuality);
            P.eq("new.out_of_range_is_default", rawIndex(proc, Pid::labudget), defBudget);
        }
        writeRaw(fcmp::kPrefNewQuality, "1");
        writeRaw(fcmp::kPrefNewLookahead, "0");
    }
}

FCMP_PROBE(proc, diagnostics)
{
    (void) C;
    // Run by hand without CTest's sandbox: a scratch store, set before the first get() or processor reads it.
    if (const char* dir = std::getenv(kPrefsDirVar); dir == nullptr || dir[0] == '\0')
    {
        const juce::File tmp = juce::File::createTempFile("fcmp-diagnostics");
        tmp.createDirectory();
        ::setenv(kPrefsDirVar, tmp.getFullPathName().toRawUTF8(), 1);
    }
    const juce::ScopedJuceInitialiser_GUI juceInit;
    unprepared(P);
    prepared(P);
    newInstances(P);
    return P.finish();
}
