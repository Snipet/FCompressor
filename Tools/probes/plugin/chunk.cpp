// FCMP_PROBE layer=proc name=chunk scope=global timeout=60
//
// proc.chunk (P1, S7; 03 §3.5; HR StateProbe :517-566, :705-724; B §1.7): the processor's audio glue against the engine
// it wraps. Clean (slot 0) at its defaults with the threshold at -30 dBFS, 48 kHz, prepared with a DECLARED block size
// of 64; the reference is a fcdsp::EngineHost configured with the processor's own setup (Processor::setup()) and fed,
// on separate arrays in 64-sample blocks, the BlockParams resolve() makes of the processor's currentRaw() (its snapped
// values). One host parameter change (ratio 4:1 -> 8:1) lands at sample 24576 in both.
//
// Layouts <L> (main in x out [.sc<key channels>]): 2x2, 1x1, 1x2, 2x2.sc2, 1x2.sc2 (the side chain's first channel is
// output channel 1: JUCE's in-place buffer), 1x1.sc1 with extkey ON (a quiet main keyed by a loud side chain), and
// 2x2.sc2.off with extkey OFF (the key is present but not used).
//
// Rows (spec):
//   chunk.unprepared.<2x2|1x2>.mismatches  processBlock before prepareToPlay passes the input through (1->2 copies the
//                                          mono input to both outputs): 0 differing samples, and no crash
//   chunk.<L>.bs1024.mismatches            fed 1024-sample blocks (with a 0-length block after each) although 64 was
//                                          declared: bit-identical to the reference
//   chunk.<L>.odd.mismatches               fed blocks cycling {1, 17, 0, 333, 64, 1024, 5}: bit-identical
//   chunk.<L>.level_db                     output minus input level: <= -3 dB where the program compresses (so the
//                                          comparisons are not vacuous); 2x2.sc2.off: NOTE only
//   chunk.nonfinite                        non-finite output samples, every run: 0
// The output hash of the 2x2 run is printed as a NOTE (no golden: the reference is the engine itself).
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "plugin/Processor.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using fcdsp::Pid;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;
    using Channels = std::vector<std::vector<float>>;

    constexpr double kFs = 48000.0;
    constexpr int kDeclared = 64;
    constexpr std::size_t kLen = 48000;
    constexpr std::size_t kChangeAt = 24576;                     // a multiple of 64 and of 1024
    constexpr float kCanary = 1234.5f;                           // output channels that carry no input start here

    struct Layout
    {
        const char* name;
        int ins, outs, keys;
        bool extKey;
    };
    constexpr Layout kLayouts[] = {
        { "2x2", 2, 2, 0, false },         { "1x1", 1, 1, 0, false },         { "1x2", 1, 2, 0, false },
        { "2x2.sc2", 2, 2, 2, true },      { "1x2.sc2", 1, 2, 2, true },      { "1x1.sc1", 1, 1, 1, true },
        { "2x2.sc2.off", 2, 2, 2, false },
    };

    juce::AudioChannelSet setOf(int channels)
    {
        return channels == 0 ? juce::AudioChannelSet::disabled()
             : channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    }

    bool applyLayout(fcmp::Processor& proc, const Layout& l)
    {
        juce::AudioProcessor::BusesLayout b;
        b.inputBuses.add(setOf(l.ins));
        b.inputBuses.add(setOf(l.keys));
        b.outputBuses.add(setOf(l.outs));
        return proc.setBusesLayout(b);
    }

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    // A parameter change at a sample index (applied before the block that starts there).
    struct Change
    {
        std::size_t at;
        Pid pid;
        float plain;
    };

    struct Program
    {
        Channels main, key;
    };

    // Main: noise at about -12 dBFS plus a sine per channel (L != R); key: noise bursts at about -6 dBFS. With a key
    // that drives the gain (extKey), the main is 30 dB quieter so the key alone compresses it.
    Program program(const Layout& l)
    {
        Program p;
        sig::Pcg32 rng(0x5eedu + static_cast<std::uint64_t>(l.ins * 16 + l.keys));
        const float mainAmp = l.extKey ? 0.0063f : 0.2f;
        for (int c = 0; c < 2; ++c)
        {
            std::vector<float> x(kLen), s(kLen);
            sig::noise(x, rng, mainAmp);
            sig::sine(s, c == 0 ? 440.0 : 997.0, kFs, mainAmp);
            for (std::size_t i = 0; i < kLen; ++i)
                x[i] += s[i];
            p.main.push_back(std::move(x));
            std::vector<float> k(kLen);
            sig::noise(k, rng, 0.9f);
            for (std::size_t i = 0; i < kLen; ++i)
                k[i] *= (i / 6000u) % 2u == 0u ? 1.0f : 0.25f;
            p.key.push_back(std::move(k));
        }
        return p;
    }

    // The processor, fed blocks whose sizes cycle through `sizes` (0 = an empty block), split at the changes.
    Channels runProcessor(fcmp::Processor& proc, const Layout& l, const Program& prog, std::span<const int> sizes,
                          std::span<const Change> changes)
    {
        const int total = std::max(l.ins + l.keys, l.outs);
        const int maxSize = *std::max_element(sizes.begin(), sizes.end());
        juce::AudioBuffer<float> buf(total, std::max(1, maxSize));
        juce::MidiBuffer midi;
        Channels out(static_cast<std::size_t>(l.outs), std::vector<float>(kLen, 0.0f));
        std::size_t off = 0, next = 0, ci = 0;
        while (off < kLen)
        {
            while (ci < changes.size() && changes[ci].at <= off)
            {
                setPlain(proc, changes[ci].pid, changes[ci].plain);
                ++ci;
            }
            std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(sizes[next++ % sizes.size()]), kLen - off);
            if (ci < changes.size())
                len = std::min(len, changes[ci].at - off);
            buf.setSize(total, static_cast<int>(len), false, false, true);
            for (int c = 0; c < total; ++c)
                for (std::size_t i = 0; i < len; ++i)
                    buf.setSample(c, static_cast<int>(i), kCanary);
            for (int c = 0; c < l.ins; ++c)
                buf.copyFrom(c, 0, prog.main[static_cast<std::size_t>(c)].data() + off, static_cast<int>(len));
            for (int k = 0; k < l.keys; ++k)
                buf.copyFrom(l.ins + k, 0, prog.key[static_cast<std::size_t>(k)].data() + off, static_cast<int>(len));
            proc.processBlock(buf, midi);
            for (int c = 0; c < l.outs; ++c)
                std::copy_n(buf.getReadPointer(c), len, out[static_cast<std::size_t>(c)].data() + off);
            off += len;
        }
        return out;
    }

    fcdsp::BlockParams blockOf(const fcmp::Processor& proc, bool extKey)
    {
        const fcdsp::RawParams raw = proc.currentRaw();
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
        fcdsp::BlockParams bp;
        bp.slot = ms.slot;
        bp.eng = fcmp::probe::resolveRaw(*ms.entry, raw).eng;
        bp.extKey = extKey;
        return bp;
    }

    // The reference: an EngineHost with the processor's setup, fed separate arrays in blocks of kDeclared, switching
    // to `after` at kChangeAt.
    Channels runReference(const fcdsp::HostConfig& cfg, const fcdsp::BlockParams& before,
                          const fcdsp::BlockParams& after, const Layout& l, const Program& prog)
    {
        auto host = std::make_unique<fcdsp::EngineHost>();
        host->configure(cfg, before);
        Channels out(static_cast<std::size_t>(l.outs), std::vector<float>(kLen, 0.0f));
        for (std::size_t off = 0; off < kLen; off += kDeclared)
        {
            const std::size_t len = std::min<std::size_t>(kDeclared, kLen - off);
            const float* ins[2] = { prog.main[0].data() + off, prog.main[1].data() + off };
            const float* keys[2] = { prog.key[0].data() + off, prog.key[1].data() + off };
            float* outs[2] = { out[0].data() + off, l.outs > 1 ? out[1].data() + off : nullptr };
            fcdsp::ProcessIo io;
            io.in = ins;
            io.numIn = l.ins;
            io.key = l.keys > 0 ? keys : nullptr;
            io.numKey = l.keys;
            io.out = outs;
            io.numOut = l.outs;
            io.n = static_cast<int>(len);
            host->process(io, off < kChangeAt ? before : after);
        }
        return out;
    }

    std::int64_t mismatches(const Channels& a, const Channels& b)
    {
        std::int64_t m = 0;
        for (std::size_t c = 0; c < a.size() && c < b.size(); ++c)
            for (std::size_t i = 0; i < a[c].size(); ++i)
                m += a[c][i] == b[c][i] ? 0 : 1;
        return m + static_cast<std::int64_t>(a.size() != b.size() ? 1 : 0);
    }

    std::int64_t nonfinite(const Channels& a)
    {
        std::int64_t m = 0;
        for (const auto& ch : a)
            for (const float v : ch)
                m += std::isfinite(v) ? 0 : 1;
        return m;
    }

    double levelDb(std::span<const float> x)
    {
        double e = 0.0;
        for (const float v : x)
            e += static_cast<double>(v) * static_cast<double>(v);
        return 10.0 * std::log10(std::max(e / static_cast<double>(x.size()), 1e-30));
    }

    std::unique_ptr<fcmp::Processor> cleanProcessor()
    {
        auto proc = std::make_unique<fcmp::Processor>();
        const fcdsp::ModeEntry& en = *fcdsp::modeSlots().front().entry;
        const fcdsp::RawParams raw = fcmp::probe::modeRaw(en);
        proc->beginBatch();
        setPlain(*proc, Pid::mode, static_cast<float>(fcdsp::slotOf(en)));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            setPlain(*proc, static_cast<Pid>(i), raw.v[i]);
        setPlain(*proc, Pid::thr, -30.0f);
        proc->endBatch();
        return proc;
    }
} // namespace

FCMP_PROBE(proc, chunk)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    if (fcdsp::modeSlots().empty())
    {
        P.harnessError("proc.chunk: no Mode is registered");
        return P.finish();
    }
    std::int64_t bad = 0;

    // ---- processBlock before prepareToPlay: the host's unprepared passthrough (HR B §1.7) ----------------------------
    for (const Layout& l : { kLayouts[0], kLayouts[2] })
    {
        auto proc = cleanProcessor();
        applyLayout(*proc, l);
        const Program prog = program(l);
        const int sizes[] = { 256 };
        const Channels out = runProcessor(*proc, l, prog, sizes, {});
        Channels want;
        for (int c = 0; c < l.outs; ++c)
            want.push_back(prog.main[static_cast<std::size_t>(std::min(c, l.ins - 1))]);
        P.eq(std::string("chunk.unprepared.") + l.name + ".mismatches", mismatches(out, want), 0);
        bad += nonfinite(out);
    }

    // ---- prepared: declared 64, fed 1024 / odd sizes, against the engine fed the processor's values -----------------
    const int big[] = { 1024, 0 };
    const int odd[] = { 1, 17, 0, 333, 64, 1024, 5 };
    const Change changes[] = { { kChangeAt, Pid::ratio, 0.875f } };   // 4:1 -> 8:1
    for (const Layout& l : kLayouts)
    {
        const Program prog = program(l);
        const std::string k = std::string("chunk.") + l.name;
        Channels ref;
        for (int feed = 0; feed < 2; ++feed)
        {
            auto proc = cleanProcessor();
            if (!applyLayout(*proc, l))
            {
                P.harnessError(k + ": the layout was refused");
                return P.finish();
            }
            setPlain(*proc, Pid::extkey, l.extKey ? 1.0f : 0.0f);
            proc->setRateAndBufferSizeDetails(kFs, kDeclared);
            proc->prepareToPlay(kFs, kDeclared);
            if (feed == 0)
            {
                const fcdsp::BlockParams before = blockOf(*proc, l.extKey);
                setPlain(*proc, Pid::ratio, changes[0].plain);
                const fcdsp::BlockParams after = blockOf(*proc, l.extKey);
                setPlain(*proc, Pid::ratio, fcmp::probe::modeRaw(*fcdsp::modeSlots().front().entry)[Pid::ratio]);
                ref = runReference(proc->setup(), before, after, l, prog);
            }
            const Channels out = runProcessor(*proc, l, prog, feed == 0 ? std::span<const int>(big)
                                                                           : std::span<const int>(odd), changes);
            P.eq(k + (feed == 0 ? ".bs1024" : ".odd") + ".mismatches", mismatches(out, ref), 0);
            bad += nonfinite(out);
            if (feed == 0)
            {
                const double d = levelDb(out[0]) - levelDb(prog.main[0]);
                if (l.extKey || l.keys == 0)
                    P.le(k + ".level_db", d, -3.0);
                std::printf("NOTE     %s: output - input level %.2f dB; output hash %016llx\n", k.c_str(), d,
                            static_cast<unsigned long long>(funkgui::test::hashFloats(out[0])));
            }
        }
    }
    P.eq("chunk.nonfinite", bad, 0);
    return P.finish();
}
