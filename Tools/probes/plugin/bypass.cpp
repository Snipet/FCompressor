// FCMP_PROBE layer=proc name=bypass scope=mode timeout=60
//
// proc.bypass.<key> (P1, S7; 03 §3.5, §3.7; B §1.6, §7.4.5; 01 §5.4 step 2i; HR B §1.6): the two ways into bypass at
// plugin level, the `bypass` parameter (what JUCE's wrappers map the host's bypass to, getBypassParameter) and
// processBlockBypassed, against the latency-aligned dry path, at both ends and at the edges.
//
// This Mode at its defaults with the threshold at -14 dBFS, 48 kHz, 512-sample blocks split at the edges. Program:
// 1.5 s of a 110 Hz tone at -6 dBFS (C §5.6's click level; right = 0.8 x left). Edges at waveform peaks of the OUTPUT
// (input peak + latency), near 0.5 s (bypass on) and 1.0 s (bypass off). Controls: the processed run (never bypassed)
// and the bypassed path delay(x, L), L = getLatencySamples().
//
// Rows (spec), <cfg> = eco, std, hq, std.la5; <how> = param (the parameter written between blocks) or host (the blocks
// between the edges go through processBlockBypassed):
//   bypass.<cfg>.<how>.before.max_err   before the first edge: |output - processed control|: 0
//   bypass.<cfg>.<how>.on.max_err       from 20 ms (+ one chunk) after the on edge to the off edge: |output -
//                                       delay(x, L)|: 0 (bit-exact, latency-aligned; the processing runs on beneath)
//   bypass.<cfg>.<how>.off.max_err      from 20 ms (+ one chunk) after the off edge on: |output - processed control|:
//                                       0 (bit-exact: the engine never stopped)
//   bypass.<cfg>.<how>.<on|off>.hf_ratio_db  the energy above 8 kHz within +-2 ms of the edge against the louder of the
//                                       two controls (C §5.0): <= +3 dB (the 20 ms smootherstep ramp)
//   bypass.<cfg>.gr_db                  the processed control's level drop against the input (NOTE; the edges are
//                                       measured where GR is running)
//   bypass.<cfg>.nonfinite              non-finite output samples: 0
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"

#include "plugin/Processor.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"
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
    namespace measure = fcmp::probe::measure;
    using Channels = std::vector<std::vector<float>>;

    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;
    constexpr std::size_t kLen = 72000;
    constexpr double kToneHz = 110.0;
    constexpr std::size_t kLand = 960 + 64;                      // the 20 ms ramp at 48 kHz, plus one chunk

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    std::unique_ptr<fcmp::Processor> makeProcessor(const fcdsp::ModeEntry& en, Quality q, LookaheadBudget b)
    {
        auto proc = std::make_unique<fcmp::Processor>();
        const fcdsp::RawParams raw = fcmp::probe::modeRaw(en, b);
        proc->beginBatch();
        setPlain(*proc, Pid::mode, static_cast<float>(fcdsp::slotOf(en)));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            setPlain(*proc, static_cast<Pid>(i), raw.v[i]);
        setPlain(*proc, Pid::thr, -14.0f);
        setPlain(*proc, Pid::quality, static_cast<float>(q));
        setPlain(*proc, Pid::labudget, static_cast<float>(b));
        proc->endBatch();
        proc->setRateAndBufferSizeDetails(kFs, kBlock);
        proc->prepareToPlay(kFs, kBlock);
        return proc;
    }

    enum class How : std::uint8_t { none, param, host };

    // The program through the processor; between `on` and `off` the processor is bypassed the `how` way.
    Channels run(fcmp::Processor& proc, const Channels& x, How how, std::size_t on, std::size_t off)
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        Channels y(2, std::vector<float>(kLen, 0.0f));
        for (std::size_t at = 0; at < kLen;)
        {
            std::size_t n = std::min<std::size_t>(kBlock, kLen - at);
            if (how != How::none)
            {
                if (at < on)
                    n = std::min(n, on - at);
                else if (at < off)
                    n = std::min(n, off - at);
            }
            const bool bypassed = how != How::none && at >= on && at < off;
            if (how == How::param)
                setPlain(proc, Pid::bypass, bypassed ? 1.0f : 0.0f);
            buf.setSize(2, static_cast<int>(n), false, false, true);
            for (int c = 0; c < 2; ++c)
                buf.copyFrom(c, 0, x[static_cast<std::size_t>(c)].data() + at, static_cast<int>(n));
            if (how == How::host && bypassed)
                proc.processBlockBypassed(buf, midi);
            else
                proc.processBlock(buf, midi);
            for (int c = 0; c < 2; ++c)
                std::copy_n(buf.getReadPointer(c), n, y[static_cast<std::size_t>(c)].data() + at);
            at += n;
        }
        return y;
    }

    double maxErr(const Channels& a, const Channels& b, std::size_t from, std::size_t to)
    {
        double m = 0.0;
        for (std::size_t c = 0; c < a.size(); ++c)
            for (std::size_t i = from; i < to && i < a[c].size(); ++i)
                m = std::max(m, std::fabs(static_cast<double>(a[c][i]) - static_cast<double>(b[c][i])));
        return m;
    }

    std::int64_t nonfinite(const Channels& a)
    {
        std::int64_t m = 0;
        for (const auto& ch : a)
            for (const float v : ch)
                m += std::isfinite(v) ? 0 : 1;
        return m;
    }

    double levelDb(const std::vector<float>& x, std::size_t from)
    {
        double e = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return 10.0 * std::log10(std::max(e / static_cast<double>(x.size() - from), 1e-30));
    }

    // The output sample nearest a positive peak of the tone near `seconds` (input peak + latency).
    std::size_t peakNear(double seconds, int latency)
    {
        const double period = kFs / kToneHz;
        const double k = std::floor(seconds * kToneHz);
        return static_cast<std::size_t>(std::llround((k + 0.25) * period)) + static_cast<std::size_t>(latency);
    }
} // namespace

FCMP_PROBE(proc, bypass)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    const fcdsp::ModeEntry& en = fcmp::probe::modeEntry(C.key);
    Channels x(2, std::vector<float>(kLen));
    sig::sine(x[0], kToneHz, kFs, 0.5);                          // -6 dBFS
    for (std::size_t i = 0; i < kLen; ++i)
        x[1][i] = 0.8f * x[0][i];

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
        const std::string k = std::string("bypass.") + cfg.name;
        auto control = makeProcessor(en, cfg.q, cfg.b);
        const int latency = control->getLatencySamples();
        const Channels processed = run(*control, x, How::none, 0, 0);
        Channels dry(2, std::vector<float>(kLen, 0.0f));
        for (std::size_t c = 0; c < 2; ++c)
            for (std::size_t i = static_cast<std::size_t>(latency); i < kLen; ++i)
                dry[c][i] = x[c][i - static_cast<std::size_t>(latency)];
        const std::size_t on = peakNear(0.5, latency), off = peakNear(1.0, latency);
        std::int64_t bad = nonfinite(processed);
        std::printf("NOTE     %s: latency %d, edges at %zu and %zu, processed level %.2f dB against the input\n",
                    k.c_str(), latency, on, off, levelDb(processed[0], kLen / 2) - levelDb(x[0], kLen / 2));

        for (const How how : { How::param, How::host })
        {
            const std::string h = k + (how == How::param ? ".param" : ".host");
            auto proc = makeProcessor(en, cfg.q, cfg.b);
            const Channels y = run(*proc, x, how, on, off);
            bad += nonfinite(y);
            P.le(h + ".before.max_err", maxErr(y, processed, 0, on), 0.0);
            P.le(h + ".on.max_err", maxErr(y, dry, on + kLand, off), 0.0);
            P.le(h + ".off.max_err", maxErr(y, processed, off + kLand, kLen), 0.0);
            for (int c = 0; c < 2; ++c)
            {
                const auto ch = static_cast<std::size_t>(c);
                const double hfOn = measure::hfRatioDb(y[ch], processed[ch], dry[ch], on, kFs);
                const double hfOff = measure::hfRatioDb(y[ch], processed[ch], dry[ch], off, kFs);
                P.le(h + ".on.hf_ratio_db." + (c == 0 ? "l" : "r"), hfOn, 3.0);
                P.le(h + ".off.hf_ratio_db." + (c == 0 ? "l" : "r"), hfOff, 3.0);
            }
        }
        P.eq(k + ".nonfinite", bad, 0);
    }
    return P.finish();
}
