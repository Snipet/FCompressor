// FCMP_PROBE layer=dsp name=latency scope=mode timeout=60
//
// dsp.latency.<key> (F7, S6; D7 at engine level: 03 §3.4, C §5.8; 01 §5.6; E §5.2; K1 #29, K2 #11a; ADR-15): the
// latency fcdsp::EngineHost reports against the latency its audio has, for every Quality x lookahead budget {off, 5 ms,
// 20 ms} at 44.1, 48 and 96 kHz, and its independence of the Mode.
//
// Reported: latencySamples() after configure(). Measured (01 §5.6 FZ2 note: STD by LF phase or group delay, never by
// impulse peak or broadband correlation), from a 1e-3 impulse at sample 256 through three paths of this Mode:
//   dry     mix 0: the OS round trip of the delayed input; the DC group delay is the impulse response's centroid
//           sum(n h[n]) / sum(h[n]) (exact for the linear-phase HQ halfbands, 3.995 + L_la at STD by the Thiran design)
//   wet     mix 1 with GR OFF (kEngGrOff: no gain reduction) at the Mode's defaults otherwise: the group delay at
//           1 kHz from the phase of the impulse response's DFT at 990 and 1010 Hz (a colour stage may block DC, so not
//           the centroid; a memoryless shaper at -60 dBFS adds no delay)
//   bypass  the bypass path: the index of the impulse in the output (an integer delay line)
// Rows, <cfg> = <q>.la<0|5|20>.fs<rate> (spec, exact: 03 §3.7 "Latency: exact"):
//   latency.<cfg>.reported_err      reported - EngineHost::latencyFor(config) (= lookaheadSamples + kOs[q].latency): 0
//   latency.<cfg>.<dry|wet|bypass>.err_samples  round(measured) - reported: 0
//   latency.<cfg>.dry.frac_samples  |measured - reported| of the dry path: <= 0.01 (K2 #11a: STD's Thiran section)
//   latency.mode_independent        configurations where this Mode's host reports another latency than a host
//                                   configured with slot 0's Mode: 0 (E §5.2: latency never depends on the Mode)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;

    constexpr int kBlock = 512;
    constexpr std::size_t kLen = 8192, kAt = 256;
    constexpr float kImpulse = 1e-3f;

    const char* nameOf(Quality q) { return q == Quality::eco ? "eco" : q == Quality::std ? "std" : "hq"; }

    HostConfig config(Quality q, LookaheadBudget b, double fs)
    {
        HostConfig c;
        c.fs = fs;
        c.maxBlock = kBlock;
        c.quality = q;
        c.budget = b;
        return c;
    }

    // The left output of a fresh host fed an impulse (both channels) at kAt.
    std::vector<float> impulseResponse(const HostConfig& cfg, const BlockParams& bp, int& latency)
    {
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        latency = host->latencySamples();
        std::vector<float> x(kLen, 0.0f), l(kLen, 0.0f), r(kLen, 0.0f);
        x[kAt] = kImpulse;
        for (std::size_t off = 0; off < kLen; off += static_cast<std::size_t>(kBlock))
        {
            const float* ins[2] = { x.data() + off, x.data() + off };
            float* outs[2] = { l.data() + off, r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = kBlock;
            host->process(io, bp);
        }
        return l;
    }

    double centroid(const std::vector<float>& h)
    {
        double s = 0.0, sn = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i)
        {
            s += static_cast<double>(h[i]);
            sn += static_cast<double>(i) * static_cast<double>(h[i]);
        }
        return sn / s - static_cast<double>(kAt);
    }

    // Group delay at hz from the phase slope between hz -+ 10 Hz (deterministic DFT: Signals.h's sinTurns).
    double groupDelay(const std::vector<float>& h, double hz, double fs)
    {
        const auto phase = [&](double f) {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < h.size(); ++i)
            {
                const double t = f * (static_cast<double>(i) - static_cast<double>(kAt)) / fs;
                re += static_cast<double>(h[i]) * sig::cosTurns(t);
                im -= static_cast<double>(h[i]) * sig::sinTurns(t);
            }
            return std::atan2(im, re);
        };
        const double f1 = hz - 10.0, f2 = hz + 10.0;
        double d = phase(f2) - phase(f1);
        while (d > 3.141592653589793)
            d -= 2.0 * 3.141592653589793;
        while (d < -3.141592653589793)
            d += 2.0 * 3.141592653589793;
        return -d / (2.0 * 3.141592653589793 * (f2 - f1) / fs);
    }

    std::int64_t peakIndex(const std::vector<float>& h)
    {
        std::size_t best = 0;
        for (std::size_t i = 0; i < h.size(); ++i)
            if (std::fabs(h[i]) > std::fabs(h[best]))
                best = i;
        return static_cast<std::int64_t>(best) - static_cast<std::int64_t>(kAt);
    }
} // namespace

FCMP_PROBE(dsp, latency)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeEntry* first = fcdsp::modeSlots().empty() ? nullptr : fcdsp::modeSlots().front().entry;
    std::int64_t dependent = 0;

    for (const double fs : { 44100.0, 48000.0, 96000.0 })
        for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
            for (const LookaheadBudget b : { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 })
            {
                const HostConfig cfg = config(q, b, fs);
                const std::string k = std::string("latency.") + nameOf(q) + ".la"
                                    + std::to_string(static_cast<int>(budgetMs(b))) + ".fs"
                                    + std::to_string(static_cast<int>(fs));
                const RawParams raw = fcmp::probe::modeRaw(en, b);
                BlockParams dry;
                dry.slot = static_cast<std::uint8_t>(slotOf(en));
                dry.eng = fcmp::probe::resolveRaw(en, raw).eng;
                dry.eng.mix = 0.0f;
                BlockParams wet = dry;
                wet.eng.mix = 1.0f;
                wet.eng.flags = static_cast<uint8_t>(wet.eng.flags | kEngGrOff);
                BlockParams byp = dry;
                byp.bypass = true;

                int reported = 0, l2 = 0, l3 = 0;
                const std::vector<float> hd = impulseResponse(cfg, dry, reported);
                const std::vector<float> hw = impulseResponse(cfg, wet, l2);
                const std::vector<float> hb = impulseResponse(cfg, byp, l3);
                P.eq(k + ".reported_err", reported - EngineHost::latencyFor(cfg), 0);
                const double cd = centroid(hd), gw = groupDelay(hw, 1000.0, fs);
                const std::int64_t pb = peakIndex(hb);
                P.eq(k + ".dry.err_samples", std::llround(cd) - reported, 0);
                P.le(k + ".dry.frac_samples", std::fabs(cd - static_cast<double>(reported)), 0.01);
                P.eq(k + ".wet.err_samples", std::llround(gw) - reported, 0);
                P.eq(k + ".bypass.err_samples", pb - reported, 0);
                if (fs == 48000.0)
                    std::printf("NOTE     %s: reported %d; measured dry %.6f, wet %.6f (1 kHz), bypass %lld\n",
                                k.c_str(), reported, cd, gw, static_cast<long long>(pb));

                if (first != nullptr)
                {
                    BlockParams other;
                    other.slot = static_cast<std::uint8_t>(slotOf(*first));
                    other.eng = fcmp::probe::resolveRaw(*first, fcmp::probe::modeRaw(*first, b)).eng;
                    EngineHost h;
                    h.configure(cfg, other);
                    dependent += h.latencySamples() == reported && l2 == reported && l3 == reported ? 0 : 1;
                }
            }
    P.eq("latency.mode_independent", dependent, 0);
    return P.finish();
}
