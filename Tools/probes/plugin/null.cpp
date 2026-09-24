// FCMP_PROBE layer=proc name=null scope=mode timeout=60
//
// proc.null.<key> (P1, S7; 03 §3.5, §3.7; 01 §5.6; K1 #2, K2 #12; ADR-17): mix 0 through processBlock, which proves the
// latency-aligned dry path at plugin level (dsp.null proves it at engine level).
//
// The processor runs this Mode at its defaults with the threshold at -30 dBFS (so the wet path compresses) and mix 0,
// 48 kHz, 512-sample blocks. Program: 1 s, a 1 kHz sine at -6 dBFS on the left and seeded noise on the right (L != R).
// References (01 §5.6): ECO delay(x, L) with L = getLatencySamples(); STD/HQ the control Oversampler round trip
// down(up(delay(x, L_la))) at the same Quality (2 channels, 512-sample blocks; dsp.os proves it block-size invariant).
//
// Rows (spec), <cfg> = eco, std, hq, std.la5 (a 5 ms lookahead budget):
//   null.<cfg>.mix0.mismatches        2 in 2 out: output samples != the reference: 0 (bit-exact)
//   null.<cfg>.mix0.mono.mismatches   1 in 1 out: 0
//   null.<cfg>.mix0.1x2.mismatches    1 in 2 out: both outputs are the mono reference: 0
//   null.<cfg>.mix1.max_diff          the same program at mix 1 differs from the reference by >= 1e-3 somewhere (the
//                                     wet path is really running, so the null is not vacuous)
// A Mode whose mix does not resolve to 0 at raw 0 (a locked mix: Brickwall) has no plugin-level mix-0 path; it gets
//   null.<cfg>.locked.mismatches      the processor against an EngineHost fed resolve(currentRaw()): 0 (the lock
//                                     holds through processBlock)
// and a NOTE; its latency-aligned dry path is proc.bypass's.
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "plugin/Processor.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using fcdsp::LookaheadBudget;
    using fcdsp::Pid;
    using fcdsp::Quality;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;
    using Channels = std::vector<std::vector<float>>;

    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;
    constexpr std::size_t kLen = 48000;

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    juce::AudioChannelSet setOf(int channels)
    {
        return channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    }

    std::unique_ptr<fcmp::Processor> makeProcessor(const fcdsp::ModeEntry& en, Quality q, LookaheadBudget b, float mix,
                                                   int ins, int outs)
    {
        auto proc = std::make_unique<fcmp::Processor>();
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(setOf(ins));
        l.inputBuses.add(juce::AudioChannelSet::disabled());
        l.outputBuses.add(setOf(outs));
        proc->setBusesLayout(l);
        const fcdsp::RawParams raw = fcmp::probe::modeRaw(en, b);
        proc->beginBatch();
        setPlain(*proc, Pid::mode, static_cast<float>(fcdsp::slotOf(en)));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            setPlain(*proc, static_cast<Pid>(i), raw.v[i]);
        setPlain(*proc, Pid::thr, -30.0f);
        setPlain(*proc, Pid::mix, mix);
        setPlain(*proc, Pid::quality, static_cast<float>(q));
        setPlain(*proc, Pid::labudget, static_cast<float>(b));
        proc->endBatch();
        proc->setRateAndBufferSizeDetails(kFs, kBlock);
        proc->prepareToPlay(kFs, kBlock);
        return proc;
    }

    Channels program()
    {
        Channels x(2, std::vector<float>(kLen));
        sig::sine(x[0], 1000.0, kFs, 0.5);
        sig::Pcg32 rng(0x6e756c6cu);
        sig::noise(x[1], rng, 0.5f);
        return x;
    }

    Channels run(fcmp::Processor& proc, const Channels& x, int ins, int outs)
    {
        const int total = std::max(ins, outs);
        juce::AudioBuffer<float> buf(total, kBlock);
        juce::MidiBuffer midi;
        Channels y(static_cast<std::size_t>(outs), std::vector<float>(kLen, 0.0f));
        for (std::size_t off = 0; off < kLen; off += kBlock)
        {
            const int n = static_cast<int>(std::min<std::size_t>(kBlock, kLen - off));
            buf.setSize(total, n, false, false, true);
            buf.clear();
            for (int c = 0; c < ins; ++c)
                buf.copyFrom(c, 0, x[static_cast<std::size_t>(c)].data() + off, n);
            proc.processBlock(buf, midi);
            for (int c = 0; c < outs; ++c)
                std::copy_n(buf.getReadPointer(c), n, y[static_cast<std::size_t>(c)].data() + off);
        }
        return y;
    }

    std::vector<float> delayed(const std::vector<float>& x, int latency)
    {
        std::vector<float> y(x.size(), 0.0f);
        const auto l = static_cast<std::size_t>(std::max(0, latency));
        for (std::size_t i = l; i < x.size(); ++i)
            y[i] = x[i - l];
        return y;
    }

    // 01 §5.6: ECO delay(x, L); STD/HQ down(up(delay(x, la))) through a control Oversampler.
    Channels reference(Quality q, int la, int latency, const Channels& x)
    {
        Channels d;
        for (const auto& ch : x)
            d.push_back(delayed(ch, q == Quality::eco ? latency : la));
        if (q == Quality::eco)
            return d;
        fcdsp::Oversampler os;
        os.configure(q, kBlock, 2);
        const int f = fcdsp::kOs[static_cast<int>(q)].factor;
        std::vector<float> ul(static_cast<std::size_t>(kBlock * f)), ur(ul.size());
        for (std::size_t off = 0; off < kLen; off += kBlock)
        {
            const int m = static_cast<int>(std::min<std::size_t>(kBlock, kLen - off));
            const float* in[2] = { d[0].data() + off, d[1].data() + off };
            float* up[2] = { ul.data(), ur.data() };
            const int nOs = os.up(in, m, up);
            float* out[2] = { d[0].data() + off, d[1].data() + off };
            const float* upc[2] = { ul.data(), ur.data() };
            os.down(upc, nOs, out);
        }
        return d;
    }

    std::int64_t mismatches(const std::vector<float>& a, const std::vector<float>& b)
    {
        std::int64_t m = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            m += a[i] == b[i] ? 0 : 1;
        return m;
    }

    std::int64_t mismatches(const Channels& a, const Channels& b)
    {
        std::int64_t m = 0;
        for (std::size_t c = 0; c < a.size() && c < b.size(); ++c)
            m += mismatches(a[c], b[c]);
        return m;
    }

    double maxDiff(const Channels& a, const Channels& b)
    {
        double m = 0.0;
        for (std::size_t c = 0; c < a.size() && c < b.size(); ++c)
            for (std::size_t i = 0; i < a[c].size(); ++i)
                m = std::max(m, std::fabs(static_cast<double>(a[c][i]) - static_cast<double>(b[c][i])));
        return m;
    }

    // The engine the processor wraps, configured with its setup and fed resolve(currentRaw()).
    Channels engineReference(const fcmp::Processor& proc, const Channels& x)
    {
        const fcdsp::RawParams raw = proc.currentRaw();
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
        fcdsp::BlockParams bp;
        bp.slot = ms.slot;
        bp.eng = fcmp::probe::resolveRaw(*ms.entry, raw).eng;
        fcdsp::EngineHost host;
        host.configure(proc.setup(), bp);
        Channels y(2, std::vector<float>(kLen, 0.0f));
        for (std::size_t off = 0; off < kLen; off += kBlock)
        {
            const float* in[2] = { x[0].data() + off, x[1].data() + off };
            float* out[2] = { y[0].data() + off, y[1].data() + off };
            fcdsp::ProcessIo io;
            io.in = in;
            io.numIn = 2;
            io.out = out;
            io.numOut = 2;
            io.n = static_cast<int>(std::min<std::size_t>(kBlock, kLen - off));
            host.process(io, bp);
        }
        return y;
    }
} // namespace

FCMP_PROBE(proc, null)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    const fcdsp::ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const Channels x = program();

    struct Cfg
    {
        const char* name;
        Quality q;
        LookaheadBudget b;
    };
    const Cfg cfgs[] = { { "eco", Quality::eco, LookaheadBudget::off },
                         { "std", Quality::std, LookaheadBudget::off },
                         { "hq", Quality::hq, LookaheadBudget::off },
                         { "std.la5", Quality::std, LookaheadBudget::ms5 } };
    for (const Cfg& cfg : cfgs)
    {
        const std::string k = std::string("null.") + cfg.name;
        auto stereo = makeProcessor(en, cfg.q, cfg.b, 0.0f, 2, 2);
        fcdsp::ParamView view;
        fcdsp::resolveView(*en.desc, stereo->currentRaw(), view);
        if (view[Pid::mix].plain != 0.0f)
        {
            std::printf("NOTE     %s: mix resolves to %.3f at raw 0 (%s): no plugin-level mix-0 path; the lock is "
                        "checked against the engine instead\n", k.c_str(), static_cast<double>(view[Pid::mix].plain),
                        view[Pid::mix].state == fcdsp::SlotState::locked ? "locked" : "not live");
            P.eq(k + ".locked.mismatches", mismatches(run(*stereo, x, 2, 2), engineReference(*stereo, x)), 0);
            continue;
        }
        const int latency = stereo->getLatencySamples();
        const int la = fcdsp::lookaheadSamples(cfg.b, kFs);
        const Channels ref = reference(cfg.q, la, latency, x);

        P.eq(k + ".mix0.mismatches", mismatches(run(*stereo, x, 2, 2), ref), 0);

        auto mono = makeProcessor(en, cfg.q, cfg.b, 0.0f, 1, 1);
        const Channels monoRef = reference(cfg.q, la, latency, Channels{ x[0], x[0] });
        P.eq(k + ".mix0.mono.mismatches", mismatches(run(*mono, x, 1, 1)[0], monoRef[0]), 0);

        auto upmix = makeProcessor(en, cfg.q, cfg.b, 0.0f, 1, 2);
        const Channels y12 = run(*upmix, x, 1, 2);
        P.eq(k + ".mix0.1x2.mismatches", mismatches(y12[0], monoRef[0]) + mismatches(y12[1], monoRef[0]), 0);

        auto wet = makeProcessor(en, cfg.q, cfg.b, 1.0f, 2, 2);
        P.ge(k + ".mix1.max_diff", maxDiff(run(*wet, x, 2, 2), ref), 1e-3);
    }
    return P.finish();
}
