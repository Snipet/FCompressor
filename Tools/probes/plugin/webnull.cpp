// FCMP_PROBE layer=proc name=webnull scope=mode timeout=120
//
// proc.webnull.<key> (web Sprint C, ADR-93): the browser demo's facade against the plugin's processor. A Processor
// (48 kHz, 128-frame blocks, stereo, no side chain) and a WebFacade over a LoopbackLink (the engine module through
// its C ABI: the gate off, 48 kHz, 128-frame quanta) get the same never-silent input (a 1 kHz sine at -6 dBFS on the
// left, seeded noise on the right) and the same script, written once against ProcessorFacade and run on both. What the
// two must agree on, bit for bit: the output, the 30 raw values, the ports, the UiFrame after every quantum and every
// HistoryRing column. Spec rows only (mismatch counts: no golden, so no libm value is ever compared across platforms).
//
// The Processor's audio thread pulls its values at each block start, the engine module takes them when a record
// arrives; the two agree when no block falls between a write outside a batch and the batch that follows it, so every
// step here renders before the next one writes (in the plugin both orders happen, and the web is always one of them).
//
//   harness.*   the harness alone, before any facade: a Processor against an engine fed the Processor's own raw values
//               (the defaults; this Mode with a snap; an edit that ramps). A failure here is the wrapper's or the
//               probe's, not the facade's.
//   <step>.*    out.mismatches, raw.mismatches (the 30 raw values, the effective slot and the budget),
//               port.mismatches (value01 of the 30 ports), frame.mismatches (quanta whose UiFrames differ): all 0;
//               records and snaps: the Params records the step posted and how many carried the snap. The steps:
//     defaults                    nothing written
//     mode.open, mode             a batch as the Mode browser makes it (`mode`, then every parameter whose Mode default
//                                 differs, a gesture each; THRESHOLD -30 so the wet path works), two quanta rendered
//                                 while it is open: nothing posted; its end: one record with the snap
//     gesture.a, gesture          one THRESHOLD gesture, two writes with audio between: two records, no snap
//     ramp, emptybatch            OUTPUT -12 dB, then, while it ramps, an empty beginBatch/endBatch: one record with
//                                 the snap although no value changed (the ramp ends at once on both sides)
//     nest                        a batch inside a batch: nothing at the inner end, one record with the snap at the
//                                 outer
//     preset, step                a factory preset of another Mode, then step(+1): one record with the snap each
//                                 (preset.undo_name: the identity was set before the batch ended)
//     undo1, undo2, redo, undo3   the history's batches: one record each, no snap (the writes ramp)
//     ab.b, ab.edit, ab.a, ab.b2  A/B: B starts as a copy of A (a record although nothing moved), an edit, back, again
//     output, bypass.on, bypass.off
//     hq, la5, look, eco          QUALITY and LOOKAHEAD changes, the Processor's SetupWatcher polled after each write,
//                                 with <step>.latency (the facade's against getLatencySamples())
//     reset                       Processor::reset() against WebFacade::resetEngine()
//   script.*    once: columns.mismatches 0 over every HistoryRing column (count and written() equal), record.values
//               (every Params record carried the facade's 30 raw values), refused 0 (records the engine refused,
//               replies the facade refused), pulls and replies (one each per quantum), diag.mismatches (diagnostics()
//               against the Processor's, the load apart), ports.static (default01, numSteps, id), and moved_steps:
//               the script is not vacuous (the level a step settles at differs from the step before it by 0.5 dB or
//               more at 8 steps at least)
//   corner.*    the cases JUCE's two-value model exists for, on a fresh pair (LINK, MAKEUP and MIX: linear maps, so
//               the cases are the same arithmetic under every C library):
//     first                       a parameter's first notification always lands: LINK one float step below its
//                                 default moves the raw value, although a later write that close would not
//     skip.edit, skip.undo, skip  Init applied after an undo: MAKEUP 6 dB, undone (the raw value is 0 exactly, the
//                                 parameter reports its round trip, 3.6e-7), then Init: the preset write is skipped
//                                 and the raw value stays 0
//     ulp1, ulp2, ulp3            a drag one float step at a time: the second write moves the parameter and not the
//                                 raw value, the third moves both (two records, not three)
//     each with <case>.reached = 1: the Processor really was in the state the case is about
//   link.*      the facade's side of the link, driven by hand over a scripted link: nothing posted at construction; a
//               zero frame and true before any reply; the attach count (Attach on 0 <-> 1 only, never below 0); one
//               outstanding Pull, a new one after kPullPatience calls; a reply at an odd address; replies with a wrong
//               magic, version, kind, size or column count change nothing; a gap flag becomes one marker column; a
//               frame is taken only with its flag; resync() posts the values with the snap and Attach; the sink is
//               given back at destruction; and over the real engine: a Pull after 0.7 s unpulled is followed until
//               nothing waits, and after 4.5 s unpulled (the engine's ring lapped) the mirror gets one marker, then
//               the 4095 columns the engine still had, over two pull() calls (one never laps the mirror itself)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "LoopbackLink.h"
#include "Signals.h"

#include "plugin/Processor.h"
#include "plugin/ProcessorFacade.h"

#include "web/engine/WebEngine.h"
#include "web/engine/WebProtocol.h"
#include "web/facade/EngineLink.h"
#include "web/facade/HostValue.h"
#include "web/facade/WebFacade.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/params/ParamPort.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace
{
    using fcdsp::Pid;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;
    namespace web = fcmp::web;

    constexpr double kFs = 48000.0;
    constexpr int kQuantum = 128;
    constexpr int kRender = 40;                                  // quanta after each step: 107 ms

    bool sameBits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

    // By hand (no CTest sandbox): the Processor must open neither the user's presets nor read the machine's
    // new-instance preferences, which the facade does not have.
    void sandbox()
    {
        const char* db = std::getenv("FCMP_PRESETS_DB");
        const char* prefs = std::getenv("FCMP_PREFS_DIR");
        if (db != nullptr && *db != '\0' && prefs != nullptr && *prefs != '\0')
            return;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path dir = std::filesystem::temp_directory_path()
                                        / ("fcmp-proc-webnull-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        funkgui::test::setEnv("FCMP_PRESETS_DB", (dir / "presets.db").string().c_str());
        funkgui::test::setEnv("FCMP_PREFS_DIR", dir.string().c_str());
        std::printf("NOTE     no sandbox in the environment: presets and preferences under %s\n", dir.string().c_str());
    }

    void prepare(fcmp::Processor& proc)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(juce::AudioChannelSet::stereo());
        l.inputBuses.add(juce::AudioChannelSet::disabled());
        l.outputBuses.add(juce::AudioChannelSet::stereo());
        proc.setBusesLayout(l);
        proc.setRateAndBufferSizeDetails(kFs, kQuantum);
        proc.prepareToPlay(kFs, kQuantum);
    }

    // The input, the same for whoever asks: closed-form sine, seeded noise.
    struct Source
    {
        void fill(float* l, float* r)
        {
            sig::sine(std::span<float>(l, kQuantum), 1000.0, kFs, 0.5, 0.0, n);
            sig::noise(std::span<float>(r, kQuantum), rng, 0.5f);
            n += kQuantum;
        }

        sig::Pcg32   rng{ 0x7765626eu };
        std::int64_t n = 0;
    };

    std::int64_t differing(const float* a, const float* b, int n)
    {
        std::int64_t bad = 0;
        for (int i = 0; i < n; ++i)
            bad += sameBits(a[i], b[i]) ? 0 : 1;
        return bad;
    }

    // ---- the editor's writes, against ProcessorFacade ---------------------------------------------------------------
    void tap01(fcmp::ProcessorFacade& f, Pid pid, float v01)     // one gesture: begin, set, end
    {
        funkgui::ParamPort& p = f.port(pid);
        p.beginGesture();
        p.setValue01(v01);
        p.endGesture();
    }

    void tap(fcmp::ProcessorFacade& f, Pid pid, float plain) { tap01(f, pid, fcdsp::toNorm(pid, plain)); }

    // The Mode browser's commit with defaults (editor/views/ModeBrowser.cpp), left open: `mode`, then every parameter
    // whose Mode default differs from its value, and THRESHOLD at -30 dBFS.
    void openModeBatch(fcmp::ProcessorFacade& f, const fcdsp::ModeEntry& en)
    {
        const fcdsp::RawParams before = f.currentRaw();
        fcdsp::RawParams raw = before;
        raw.modeSlot = static_cast<std::uint8_t>(fcdsp::slotOf(en));
        fcdsp::modeDefaults(*en.desc, raw);
        f.beginBatch();
        tap(f, Pid::mode, static_cast<float>(raw.modeSlot));
        for (std::size_t k = 0; k < fcdsp::kNumModeParams; ++k)
            if (!sameBits(raw.v[k], before.v[k]))
                tap(f, static_cast<Pid>(k), raw.v[k]);
        tap(f, Pid::thr, -30.0f);
    }

    // ---- a Processor and a WebFacade side by side -------------------------------------------------------------------
    struct Tally
    {
        std::int64_t out = 0;                                    // output samples that differ, both channels
        std::int64_t frames = 0;                                 // quanta after which the UiFrames differ
    };

    struct Pair
    {
        Pair()
        {
            prepare(proc);
            fcmp_web_set_gate(link.engine(), 0);                 // the Processor has no silence gate
            fcmp_web_configure(link.engine(), kFs, kQuantum);
            facade.setEngineSetup(kFs, kQuantum);
            proc.setUiAttached(true);
            facade.setUiAttached(true);
            link.clearLog();
        }

        // The same call on both.
        template <class Fn>
        void both(Fn&& fn)
        {
            fn(static_cast<fcmp::ProcessorFacade&>(proc));
            fn(static_cast<fcmp::ProcessorFacade&>(facade));
        }

        void quantum(Tally& t)
        {
            float inL[kQuantum], inR[kQuantum], outL[kQuantum], outR[kQuantum];
            source.fill(inL, inR);
            buf.setSize(2, kQuantum, false, false, true);
            buf.copyFrom(0, 0, inL, kQuantum);
            buf.copyFrom(1, 0, inR, kQuantum);
            proc.processBlock(buf, midi);
            fcmp_web_process(link.engine(), inL, inR, outL, outR, kQuantum);
            t.out += differing(buf.getReadPointer(0), outL, kQuantum);
            t.out += differing(buf.getReadPointer(1), outR, kQuantum);

            fcdsp::UiFrame a{}, b{};
            const bool gotA = proc.readUiFrame(a);
            facade.pull();                                       // once per frame, as the page's frame loop
            const bool gotB = facade.readUiFrame(b);
            t.frames += gotA == gotB && std::memcmp(&a, &b, sizeof a) == 0 ? 0 : 1;
            drain(proc.history(), procNext, procColumns);
            drain(facade.history(), webNext, webColumns);

            double sum = 0.0;
            for (int i = 0; i < kQuantum; ++i)
            {
                const auto y = static_cast<double>(buf.getReadPointer(0)[i]);
                sum += y * y;
            }
            power = sum / kQuantum;
            ++quanta;
        }

        Tally render(int n)
        {
            Tally t;
            double sum = 0.0;
            for (int q = 0; q < n; ++q)
            {
                quantum(t);
                if (q >= n - 8)
                    sum += power;                                // the level the step settles at: its last 8 quanta
            }
            levelDb = 10.0 * std::log10(std::max(sum / std::min(n, 8), 1.0e-30));
            return t;
        }

        static void drain(const fcdsp::HistoryRing& ring, std::uint64_t& next, std::vector<fcdsp::HistoryColumn>& to)
        {
            fcdsp::HistoryColumn scratch[64];
            for (;;)
            {
                std::uint32_t n = 0;
                const std::uint64_t first = ring.read(next, scratch, n);
                to.insert(to.end(), scratch, scratch + n);
                next = first + n;
                if (n == 0)
                    return;
            }
        }

        std::int64_t rawMismatches() const
        {
            std::int64_t bad = 0;
            for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
                bad += sameBits(proc.rawValue(static_cast<Pid>(i)), facade.plain(static_cast<Pid>(i))) ? 0 : 1;
            const fcdsp::RawParams a = proc.currentRaw(), b = facade.currentRaw();
            for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
                bad += sameBits(a.v[i], b.v[i]) ? 0 : 1;
            return bad + (a.modeSlot == b.modeSlot ? 0 : 1) + (a.budget == b.budget ? 0 : 1);
        }

        std::int64_t portMismatches()
        {
            std::int64_t bad = 0;
            for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            {
                const auto pid = static_cast<Pid>(i);
                bad += sameBits(proc.port(pid).value01(), facade.port(pid).value01()) ? 0 : 1;
            }
            return bad;
        }

        // The rows of one step, after its writes: what it posted, then kRender quanta and the comparisons.
        void step(Probe& P, const std::string& name, int records, int snaps, int n = kRender)
        {
            const fcmp::probe::LoopbackLink::Log& log = link.log();
            P.eq(name + ".records", log.params, records);
            P.eq(name + ".snaps", log.snapped(), snaps);
            if (log.params > 0)
                for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
                    recordValueMismatches += sameBits(log.lastParams[i], facade.plain(static_cast<Pid>(i))) ? 0 : 1;
            const double before = levelDb;
            const Tally t = render(n);
            P.eq(name + ".out.mismatches", t.out, 0);
            P.eq(name + ".raw.mismatches", rawMismatches(), 0);
            P.eq(name + ".port.mismatches", portMismatches(), 0);
            P.eq(name + ".frame.mismatches", t.frames, 0);
            movedSteps += steps > 0 && std::fabs(levelDb - before) >= 0.5 ? 1 : 0;
            ++steps;
            pulls += link.log().pulls;
            replies += link.log().replies;
            resets += link.log().resets;
            refused += link.log().refused;
            link.clearLog();
        }

        void latencyRow(Probe& P, const std::string& name)
        {
            P.eq(name + ".latency", facade.latencySamples(), proc.getLatencySamples());
        }

        fcmp::Processor           proc;
        fcmp::probe::LoopbackLink link;
        web::WebFacade            facade{ link };
        juce::AudioBuffer<float>  buf{ 2, kQuantum };
        juce::MidiBuffer          midi;
        Source                    source;
        std::vector<fcdsp::HistoryColumn> procColumns, webColumns;
        std::uint64_t procNext = 0, webNext = 0;
        std::int64_t  recordValueMismatches = 0;
        std::int64_t  quanta = 0, pulls = 0, replies = 0, resets = 0, refused = 0;
        double power = 0.0, levelDb = 0.0;
        int    steps = 0, movedSteps = 0;
    };

    // ==== the harness alone ==========================================================================================

    void harnessRows(Probe& P, const fcdsp::ModeEntry& en)
    {
        fcmp::Processor proc;
        prepare(proc);
        fcmp::probe::LoopbackLink link;
        fcmp_web_set_gate(link.engine(), 0);
        fcmp_web_configure(link.engine(), kFs, kQuantum);
        P.eq("harness.latency", fcmp_web_latency(link.engine()), proc.getLatencySamples());

        juce::AudioBuffer<float> buf(2, kQuantum);
        juce::MidiBuffer midi;
        Source source;
        const auto render = [&](int quanta) {
            std::int64_t bad = 0;
            for (int q = 0; q < quanta; ++q)
            {
                float inL[kQuantum], inR[kQuantum], outL[kQuantum], outR[kQuantum];
                source.fill(inL, inR);
                buf.copyFrom(0, 0, inL, kQuantum);
                buf.copyFrom(1, 0, inR, kQuantum);
                proc.processBlock(buf, midi);
                fcmp_web_process(link.engine(), inL, inR, outL, outR, kQuantum);
                bad += differing(buf.getReadPointer(0), outL, kQuantum);
                bad += differing(buf.getReadPointer(1), outR, kQuantum);
            }
            return bad;
        };
        const auto postRaw = [&](bool snap) {                    // the Processor's own raw values as one record
            web::ParamsMsg m{};
            m.h = web::header(web::Kind::params, sizeof(web::ParamsMsg));
            for (std::size_t i = 0; i < web::kParamCount; ++i)
                m.plain[i] = proc.rawValue(static_cast<Pid>(i));
            m.snap = snap ? 1u : 0u;
            std::uint8_t bytes[sizeof(web::ParamsMsg)];
            std::memcpy(bytes, &m, sizeof m);
            link.post(bytes);
        };

        P.eq("harness.defaults.out.mismatches", render(kRender), 0);
        openModeBatch(proc, en);
        proc.endBatch();
        postRaw(true);
        P.eq("harness.mode.out.mismatches", render(kRender), 0);
        tap(proc, Pid::thr, -24.0f);
        tap(proc, Pid::output, -3.0f);
        postRaw(false);
        P.eq("harness.edit.out.mismatches", render(kRender), 0);
        P.eq("harness.refused", link.log().refused, 0);
    }

    // ==== the script =================================================================================================

    void scriptRows(Probe& P, const fcdsp::ModeEntry& en)
    {
        Pair r;
        r.latencyRow(P, "defaults");
        r.step(P, "defaults", 0, 0);

        // A Mode change with its defaults, audio running while the batch is open.
        r.both([&](fcmp::ProcessorFacade& f) { openModeBatch(f, en); });
        r.step(P, "mode.open", 0, 0, 2);
        r.both([](fcmp::ProcessorFacade& f) { f.endBatch(); });
        r.step(P, "mode", 1, 1);

        // One gesture, two writes, audio between them.
        r.both([](fcmp::ProcessorFacade& f) {
            f.port(Pid::thr).beginGesture();
            f.port(Pid::thr).setValue01(fcdsp::toNorm(Pid::thr, -24.0f));
        });
        r.step(P, "gesture.a", 1, 0, 3);
        r.both([](fcmp::ProcessorFacade& f) {
            f.port(Pid::thr).setValue01(fcdsp::toNorm(Pid::thr, -36.0f));
            f.port(Pid::thr).endGesture();
        });
        r.step(P, "gesture", 1, 0);

        // An empty batch while OUTPUT ramps (20 ms; two quanta are 5.3 ms): the snap ends the ramp.
        r.both([](fcmp::ProcessorFacade& f) { tap(f, Pid::output, -12.0f); });
        r.step(P, "ramp", 1, 0, 2);
        r.both([](fcmp::ProcessorFacade& f) {
            f.beginBatch();
            f.endBatch();
        });
        r.step(P, "emptybatch", 1, 1);

        // A batch inside a batch: only the outermost end posts.
        r.both([](fcmp::ProcessorFacade& f) {
            f.beginBatch();
            f.beginBatch();
            tap(f, Pid::makeup, 3.0f);
            f.endBatch();
        });
        P.eq("nest.inner.records", r.link.log().params, 0);
        r.both([](fcmp::ProcessorFacade& f) {
            tap(f, Pid::output, -9.0f);
            f.endBatch();
        });
        r.step(P, "nest", 1, 1);

        // A factory preset of another Mode, the next one, and the history over both.
        int other = -1;
        {
            fcmp::PresetAccess& pr = r.proc.presets();
            for (int i = 1; i < pr.count() && other < 0; ++i)
                if (pr.row(i).factory && pr.row(i).modeKey != en.desc->key)
                    other = i;
        }
        P.ge("preset.found_other_mode", other, 1);
        if (other >= 1)
        {
            P.eq("preset.same_row", r.facade.presets().row(other).uuid == r.proc.presets().row(other).uuid ? 1 : 0, 1);
            r.both([other](fcmp::ProcessorFacade& f) { f.presets().apply(other); });
            // The history read the new identity at the batch's end: the entry is the preset on both sides.
            P.eq("preset.undo_name", std::string(r.facade.edits().undoName()) == r.proc.edits().undoName() ? 1 : 0, 1);
            r.step(P, "preset", 1, 1);
            r.both([](fcmp::ProcessorFacade& f) { f.presets().step(1); });
            r.step(P, "step", 1, 1);
            for (const char* name : { "undo1", "undo2" })
            {
                r.both([](fcmp::ProcessorFacade& f) { f.edits().undo(); });
                r.step(P, name, 1, 0);
            }
            r.both([](fcmp::ProcessorFacade& f) { f.edits().redo(); });
            r.step(P, "redo", 1, 0);
            r.both([](fcmp::ProcessorFacade& f) { f.edits().undo(); });
            r.step(P, "undo3", 1, 0);
            P.eq("undo3.same_preset", r.facade.presets().current() == r.proc.presets().current()
                                      && r.facade.presets().modified() == r.proc.presets().modified() ? 1 : 0, 1);
        }

        // A/B.
        r.both([](fcmp::ProcessorFacade& f) { f.edits().selectSlot(1); });
        r.step(P, "ab.b", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { tap(f, Pid::thr, -20.0f); });
        r.step(P, "ab.edit", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { f.edits().selectSlot(0); });
        r.step(P, "ab.a", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { f.edits().selectSlot(1); });
        r.step(P, "ab.b2", 1, 0);

        // OUTPUT and BYPASS.
        r.both([](fcmp::ProcessorFacade& f) { tap(f, Pid::output, -3.0f); });
        r.step(P, "output", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { tap01(f, Pid::bypass, 1.0f); });
        r.step(P, "bypass.on", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { tap01(f, Pid::bypass, 0.0f); });
        r.step(P, "bypass.off", 1, 0);

        // The setup: the engine module reconfigures when the record arrives, the Processor at its watcher's next poll.
        struct Setup
        {
            const char* name;
            Pid   pid;
            float plain;
        };
        for (const Setup& s : { Setup{ "hq", Pid::quality, 2.0f }, Setup{ "la5", Pid::labudget, 1.0f },
                                Setup{ "look", Pid::look, 2.0f }, Setup{ "eco", Pid::quality, 0.0f } })
        {
            r.both([&s](fcmp::ProcessorFacade& f) { tap(f, s.pid, s.plain); });
            r.proc.setupWatcher().poll();
            r.step(P, s.name, 1, 0);
            r.latencyRow(P, s.name);
        }

        r.proc.reset();
        r.facade.resetEngine();
        P.eq("reset.resets", r.link.log().resets, 1);
        r.step(P, "reset", 0, 0);

        // Once, over the whole script.
        std::int64_t columnsBad = r.procColumns.size() == r.webColumns.size() ? 0 : 1;
        for (std::size_t i = 0; i < std::min(r.procColumns.size(), r.webColumns.size()); ++i)
            columnsBad += std::memcmp(&r.procColumns[i], &r.webColumns[i], sizeof(fcdsp::HistoryColumn)) == 0 ? 0 : 1;
        P.eq("script.columns.mismatches", columnsBad, 0);
        P.ge("script.columns.count", static_cast<double>(r.webColumns.size()), 1000.0);
        P.eq("script.columns.written", static_cast<std::int64_t>(r.facade.history().written()),
             static_cast<std::int64_t>(r.proc.history().written()));
        P.eq("script.record.values.mismatches", r.recordValueMismatches, 0);
        P.eq("script.refused", r.refused + static_cast<std::int64_t>(r.facade.repliesRefused()), 0);
        P.eq("script.pulls", r.pulls, r.quanta);
        P.eq("script.replies", static_cast<std::int64_t>(r.facade.replies()), r.quanta);

        const fcmp::Diagnostics a = r.proc.diagnostics(), b = r.facade.diagnostics();
        const std::int64_t diagBad = (a.prepared == b.prepared ? 0 : 1) + (a.sampleRate == b.sampleRate ? 0 : 1)
                                   + (a.maxBlock == b.maxBlock ? 0 : 1) + (a.mainIns == b.mainIns ? 0 : 1)
                                   + (a.mainOuts == b.mainOuts ? 0 : 1) + (a.keyChans == b.keyChans ? 0 : 1)
                                   + (a.quality == b.quality ? 0 : 1) + (a.budget == b.budget ? 0 : 1)
                                   + (a.latencySamples == b.latencySamples ? 0 : 1)
                                   + (std::strcmp(a.version, b.version) == 0 ? 0 : 1)
                                   + (std::strcmp(a.funkgui, b.funkgui) == 0 ? 0 : 1);
        P.eq("script.diag.mismatches", diagBad, 0);
        P.eq("script.diag.no_load", b.blocks == 0u && b.overruns == 0u && b.loadAvg == 0.0f && b.loadPeak == 0.0f
                                    && b.juce[0] == '\0' ? 1 : 0, 1);

        std::int64_t portsBad = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const auto pid = static_cast<Pid>(i);
            funkgui::ParamPort& p = r.proc.port(pid);
            funkgui::ParamPort& w = r.facade.port(pid);
            portsBad += (sameBits(p.default01(), w.default01()) ? 0 : 1) + (p.numSteps() == w.numSteps() ? 0 : 1)
                      + (std::strcmp(p.id(), w.id()) == 0 ? 0 : 1) + (w.native() == nullptr ? 0 : 1);
        }
        P.eq("script.ports.static.mismatches", portsBad, 0);
        P.ge("script.moved_steps", r.movedSteps, 8);
    }

    // ==== the corner cases of the two-value model ====================================================================

    // What the parameter object reports as its plain value: not the raw value in the cases below.
    float reported(fcmp::Processor& proc, Pid pid)
    {
        const juce::RangedAudioParameter& p = proc.parameter(pid);
        return p.convertFrom0to1(p.getValue());
    }

    void cornerRows(Probe& P)
    {
        Pair r;

        // The first notification is stored whatever it says.
        const float linkDefault = r.proc.rawValue(Pid::link);
        const float below = std::nextafter(fcdsp::toNorm(Pid::link, linkDefault), 0.0f);
        r.both([below](fcmp::ProcessorFacade& f) { tap01(f, Pid::link, below); });
        const float landed = r.proc.rawValue(Pid::link);         // "equal" to the default by JUCE's rule, and stored
        P.eq("corner.first.reached",
             !sameBits(landed, linkDefault) && web::approximatelyEqual(landed, linkDefault) ? 1 : 0, 1);
        r.step(P, "corner.first", 1, 0);

        // Init after an undo.
        r.both([](fcmp::ProcessorFacade& f) { tap(f, Pid::makeup, 6.0f); });
        r.step(P, "corner.skip.edit", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { f.edits().undo(); });
        r.step(P, "corner.skip.undo", 1, 0);
        r.both([](fcmp::ProcessorFacade& f) { f.presets().apply(0); });
        P.eq("corner.skip.reached", sameBits(r.proc.rawValue(Pid::makeup), 0.0f)
                                    && !sameBits(reported(r.proc, Pid::makeup), 0.0f) ? 1 : 0, 1);
        r.step(P, "corner.skip", 1, 1);

        // A drag, one float step at a time (MIX: plain = 2 * position, exactly).
        const float n1 = 0.3f, n2 = std::nextafter(n1, 1.0f), n3 = std::nextafter(n2, 1.0f);
        r.both([n1](fcmp::ProcessorFacade& f) {
            f.port(Pid::mix).beginGesture();
            f.port(Pid::mix).setValue01(n1);
        });
        const float held = r.proc.rawValue(Pid::mix);
        r.step(P, "corner.ulp1", 1, 0, 3);
        r.both([n2](fcmp::ProcessorFacade& f) { f.port(Pid::mix).setValue01(n2); });
        P.eq("corner.ulp.reached", sameBits(r.proc.rawValue(Pid::mix), held)
                                   && sameBits(r.proc.port(Pid::mix).value01(), n2) ? 1 : 0, 1);
        r.step(P, "corner.ulp2", 0, 0, 3);
        r.both([n3](fcmp::ProcessorFacade& f) {
            f.port(Pid::mix).setValue01(n3);
            f.port(Pid::mix).endGesture();
        });
        P.eq("corner.ulp.moved", !sameBits(r.proc.rawValue(Pid::mix), held) ? 1 : 0, 1);
        r.step(P, "corner.ulp3", 1, 0);
        P.eq("corner.refused", r.refused + static_cast<std::int64_t>(r.facade.repliesRefused()), 0);
    }

    // ==== the facade's side of the link ==============================================================================

    // A link the probe plays the other end of: it keeps what was posted and answers when told to.
    class ScriptLink final : public web::EngineLink
    {
    public:
        void post(std::span<const std::uint8_t> record) override { posted.emplace_back(record.begin(), record.end()); }
        void setSink(web::ReplySink* s) override { sink = s; }

        int count(web::Kind kind) const
        {
            int n = 0;
            for (const std::vector<std::uint8_t>& r : posted)
                n += r.size() >= sizeof(web::Header) && header(r).kind == static_cast<std::uint16_t>(kind) ? 1 : 0;
            return n;
        }

        static web::Header header(const std::vector<std::uint8_t>& r)
        {
            web::Header h{};
            std::memcpy(&h, r.data(), sizeof h);
            return h;
        }

        std::vector<std::vector<std::uint8_t>> posted;
        web::ReplySink* sink = nullptr;
    };

    // A reply record as the engine lays it out, at an odd address (a port's buffer need not be aligned).
    struct ReplyBytes
    {
        ReplyBytes(std::uint32_t tag, std::uint32_t flags, std::uint32_t latency, const fcdsp::UiFrame& frame,
                   std::span<const fcdsp::HistoryColumn> columns)
            : storage(1 + web::replyBytes(static_cast<std::uint32_t>(columns.size())))
        {
            web::ReplyHead head{};
            head.h = web::header(web::Kind::reply, storage.size() - 1, tag);
            head.flags = flags;
            head.latencySamples = latency;
            head.columnCount = static_cast<std::uint32_t>(columns.size());
            head.frame = frame;
            std::memcpy(storage.data() + 1, &head, sizeof head);
            if (!columns.empty())
                std::memcpy(storage.data() + 1 + web::kReplyFixedBytes, columns.data(), columns.size_bytes());
        }

        std::span<const std::uint8_t> bytes() const { return { storage.data() + 1, storage.size() - 1 }; }
        std::uint8_t* at(std::size_t offset) { return storage.data() + 1 + offset; }

        std::vector<std::uint8_t> storage;
    };

    fcdsp::HistoryColumn column(float seed)
    {
        fcdsp::HistoryColumn c{};
        c.inPeakDb = -seed;
        c.outPeakDb = -seed - 1.0f;
        c.grMaxDb = seed * 0.25f;
        c.bits = static_cast<std::uint32_t>(seed);
        return c;
    }

    void linkRows(Probe& P)
    {
        {
            ScriptLink link;
            std::int64_t fresh = 0, attach = 0, pullRows = 0;
            {
                web::WebFacade f(link);
                fcdsp::UiFrame zero{}, got;
                std::memset(&got, 0xff, sizeof got);
                fresh += link.posted.empty() && link.sink != nullptr ? 0 : 1;
                fresh += f.readUiFrame(got) && std::memcmp(&got, &zero, sizeof got) == 0 ? 0 : 1;
                fresh += !f.diagnostics().prepared && f.history().written() == 0 ? 0 : 1;
                fcmp::Processor unprepared;
                fresh += f.latencySamples() == unprepared.getLatencySamples() ? 0 : 1;
                f.setEnvironment("WEB", std::string(100, 'x'));  // a name longer than Diagnostics::host: cut
                const fcmp::Diagnostics d = f.diagnostics();
                fresh += std::strcmp(d.format, "WEB") == 0 && std::strlen(d.host) == sizeof d.host - 1 ? 0 : 1;
                P.eq("link.fresh.mismatches", fresh, 0);

                // The attach count: an Attach record when it leaves 0 and when it comes back to it.
                f.setUiAttached(false);                          // nothing listens yet: ignored
                f.setUiAttached(true);
                f.setUiAttached(true);
                attach += link.count(web::Kind::attach) == 1 && f.attachCount() == 2 ? 0 : 1;
                f.setUiAttached(false);
                attach += link.count(web::Kind::attach) == 1 ? 0 : 1;
                f.setUiAttached(false);
                attach += link.count(web::Kind::attach) == 2 && f.attachCount() == 0 ? 0 : 1;
                f.setUiAttached(false);
                attach += link.count(web::Kind::attach) == 2 && f.attachCount() == 0 ? 0 : 1;
                std::uint32_t flag = 9;
                if (link.posted.size() == 2)
                    std::memcpy(&flag, link.posted[1].data() + offsetof(web::AttachMsg, attached), sizeof flag);
                attach += flag == 0u ? 0 : 1;
                P.eq("link.attach.mismatches", attach, 0);

                // One outstanding Pull.
                link.posted.clear();
                f.setUiAttached(true);
                for (int i = 0; i < web::WebFacade::kPullPatience; ++i)
                    f.pull();
                pullRows += link.count(web::Kind::pull) == 1 ? 0 : 1;        // unanswered: no second one yet
                f.pull();
                pullRows += link.count(web::Kind::pull) == 2 ? 0 : 1;        // taken for lost: a new one
                const std::uint32_t tag = ScriptLink::header(link.posted.back()).tag;
                fcdsp::UiFrame frame{};
                frame.publishCount = 7;
                frame.thrDb = -21.5f;
                const fcdsp::HistoryColumn cols[3] = { column(1.0f), column(2.0f), column(3.0f) };
                const std::uint32_t flags = web::kReplyFrame | web::kReplyConfigured | web::kReplyAttached;
                const ReplyBytes good(tag, flags, 261, frame, cols);
                link.sink->reply(good.bytes());
                f.pull();
                pullRows += link.count(web::Kind::pull) == 3 ? 0 : 1;        // answered: the next frame's goes out
                P.eq("link.pull.mismatches", pullRows, 0);

                // What the good reply left: the columns in order, the frame, the latency, the engine's facts.
                std::int64_t taken = f.replies() == 1u && f.repliesRefused() == 0u ? 0 : 1;
                fcdsp::HistoryColumn read[8];
                std::uint32_t n = 0;
                const std::uint64_t firstRead = f.history().read(0, read, n);
                taken += firstRead == 0 && n == 3 && std::memcmp(read, cols, sizeof cols) == 0 ? 0 : 1;
                fcdsp::UiFrame got2{};
                taken += f.readUiFrame(got2) && std::memcmp(&got2, &frame, sizeof frame) == 0 ? 0 : 1;
                taken += f.latencySamples() == 261 && f.diagnostics().prepared ? 0 : 1;
                P.eq("link.reply.taken.mismatches", taken, 0);

                // Replies that fail a check change nothing.
                const auto refusedReply = [&](const ReplyBytes& reply, std::size_t size) {
                    const std::uint32_t before = f.repliesRefused();
                    link.sink->reply(reply.bytes().first(size));
                    fcdsp::UiFrame now{};
                    f.readUiFrame(now);
                    return f.repliesRefused() == before + 1u && f.replies() == 1u && f.history().written() == 3
                               && std::memcmp(&now, &frame, sizeof frame) == 0 && f.latencySamples() == 261
                               ? 0 : 1;
                };
                fcdsp::UiFrame other{};
                other.publishCount = 99;
                const auto make = [&] { return ReplyBytes(tag, web::kReplyFrame | web::kReplyGap, 5, other, cols); };
                const std::size_t whole = web::replyBytes(3);
                std::int64_t bad = 0;
                {
                    ReplyBytes b = make();
                    *b.at(offsetof(web::Header, magic)) = 0x00;  // 'F' no more
                    bad += refusedReply(b, whole);
                }
                {
                    ReplyBytes b = make();
                    *b.at(offsetof(web::Header, version)) = 0x02;
                    bad += refusedReply(b, whole);
                }
                {
                    ReplyBytes b = make();
                    const auto kind = static_cast<std::uint16_t>(web::Kind::params);
                    std::memcpy(b.at(offsetof(web::Header, kind)), &kind, sizeof kind);
                    bad += refusedReply(b, whole);
                }
                {
                    ReplyBytes b = make();                       // says four columns, carries three
                    const std::uint32_t four = 4;
                    std::memcpy(b.at(offsetof(web::ReplyHead, columnCount)), &four, sizeof four);
                    bad += refusedReply(b, whole);
                }
                {
                    ReplyBytes b = make();                       // more columns than a reply holds
                    const std::uint32_t many = web::kReplyMaxColumns + 1u;
                    std::memcpy(b.at(offsetof(web::ReplyHead, columnCount)), &many, sizeof many);
                    bad += refusedReply(b, whole);
                }
                bad += refusedReply(make(), whole - 1);          // cut short
                bad += refusedReply(make(), web::kReplyFixedBytes - 1);
                bad += refusedReply(make(), 0);
                P.eq("link.reply.refused.mismatches", bad, 0);

                // A gap becomes one marker column, then the reply's own; without the frame flag the frame stays.
                const ReplyBytes gap(tag + 100u, web::kReplyGap | web::kReplyConfigured, 261, other, cols);
                link.sink->reply(gap.bytes());
                std::int64_t gapRows = f.history().written() == 7 ? 0 : 1;
                n = 0;
                gapRows += f.history().read(3, read, n) == 3 && n == 4 ? 0 : 1;
                gapRows += read[0].bits == (1u << 5) && read[0].inPeakDb == -200.0f && read[0].outPeakDb == -200.0f
                           && read[0].detMaxDb == -200.0f && read[0].grMaxDb == 0.0f && read[0].grMinDb == 0.0f
                           && read[0].tgtMaxDb == 0.0f && read[0].internal0 == 0.0f ? 0 : 1;
                gapRows += std::memcmp(read + 1, cols, sizeof cols) == 0 ? 0 : 1;
                fcdsp::UiFrame kept{};
                gapRows += f.readUiFrame(kept) && std::memcmp(&kept, &frame, sizeof frame) == 0 ? 0 : 1;
                P.eq("link.reply.gap.mismatches", gapRows, 0);

                // A link that has just connected: the values with the snap, and Attach (something listens).
                link.posted.clear();
                tap(f, Pid::thr, -40.0f);
                f.resync();
                std::int64_t resync = link.posted.size() == 3 && link.count(web::Kind::params) == 2
                                          && link.count(web::Kind::attach) == 1 ? 0 : 1;
                if (link.posted.size() == 3 && link.posted[1].size() == sizeof(web::ParamsMsg))
                {
                    web::ParamsMsg m;
                    std::memcpy(static_cast<void*>(&m), link.posted[1].data(), sizeof m);
                    resync += m.snap == 1u && sameBits(m.plain[fcdsp::idx(Pid::thr)], f.plain(Pid::thr)) ? 0 : 1;
                }
                else
                    ++resync;
                f.pull();                                        // the old engine's Pull is forgotten: one goes out
                resync += link.count(web::Kind::pull) == 1 ? 0 : 1;
                P.eq("link.resync.mismatches", resync, 0);
            }
            P.eq("link.sink_returned", link.sink == nullptr ? 1 : 0, 1);
        }

        // Over the real engine: more columns than one reply holds, then more than the engine's ring holds.
        {
            fcmp::probe::LoopbackLink link;
            web::WebFacade f(link);
            fcmp_web_set_gate(link.engine(), 0);
            fcmp_web_configure(link.engine(), kFs, kQuantum);
            f.setUiAttached(true);
            Source source;
            const auto run = [&](int quanta) {
                float inL[kQuantum], inR[kQuantum], outL[kQuantum], outR[kQuantum];
                for (int q = 0; q < quanta; ++q)
                {
                    source.fill(inL, inR);
                    fcmp_web_process(link.engine(), inL, inR, outL, outR, kQuantum);
                }
            };
            run(263);                                            // 0.7 s: 701 columns
            f.pull();
            const std::uint64_t afterBurst = f.history().written();
            P.eq("link.burst.replies", link.log().replies, 2);
            P.eq("link.burst.columns", static_cast<std::int64_t>(afterBurst), 263 * kQuantum / 48);
            f.pull();
            P.eq("link.burst.settled", static_cast<std::int64_t>(f.history().written() - afterBurst), 0);

            run(1688);                                           // 4.5 s: the engine's ring (4096 columns) lapped
            link.clearLog();
            f.pull();
            P.eq("link.lap.burst.replies", link.log().replies, web::WebFacade::kPullBurst);
            fcdsp::HistoryColumn first[2];
            std::uint32_t n = 0;
            const std::uint64_t at = f.history().read(afterBurst, first, n);    // the mirror itself has not lapped
            P.eq("link.lap.marker_first", at == afterBurst && n == 2 && first[0].bits == (1u << 5)
                                          && first[0].inPeakDb == -200.0f && first[1].inPeakDb > -100.0f ? 1 : 0, 1);
            f.pull();
            P.eq("link.lap.columns", static_cast<std::int64_t>(f.history().written() - afterBurst),
                 1 + (fcdsp::HistoryRing::kCapacity - 1));
            P.eq("link.lap.refused", link.log().refused + static_cast<std::int64_t>(f.repliesRefused()), 0);
        }
    }
} // namespace

FCMP_PROBE(proc, webnull)
{
    sandbox();
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    const fcdsp::ModeEntry& en = fcmp::probe::modeEntry(C.key);
    harnessRows(P, en);
    scriptRows(P, en);
    cornerRows(P);
    linkRows(P);
    return P.finish();
}
