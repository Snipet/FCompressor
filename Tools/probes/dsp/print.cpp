// FCMP_PROBE layer=dsp name=print scope=mode timeout=60
//
// dsp.print.<key> (F7, S6; 03 §3.4 "dsp.print", C §5.12; 01 §0 and K2 #10): the Mode's fingerprint through
// fcdsp::EngineHost at the plugin's default setup (STD, no lookahead, 48 kHz, blocks of 512): fixed program material
// rendered at the Mode's defaults and at three parameter sets, each output channel hashed (Harness hashFloats: FNV-1a
// over the float bit patterns). Golden rows only (exact): an exact refactor-neutrality proof. Once the Mode is listed
// in tests/fixtures/modes-ever.tsv, a change that moves print.default.* requires ModeDescriptor::revision++ and an
// entry in docs/modes/<key>.md (01 §0, §9.1).
//
// Program (4 s, stereo, deterministic: Signals.h's closed-form phases and seeded PCG32): L = a logarithmic sine sweep
// 20 Hz -> 20 kHz at -12 dBFS plus noise bursts (0.25 s on, 0.25 s off) at -18 dBFS peak; R = the sweep a quarter turn
// later plus bursts from another seed stream. The sweep's phase is the exact integral of the log-frequency law, in
// turns, reduced with sinTurns.
//
// Parameter sets (raw, snapped on read by resolve(): the Mode decides what a value means):
//   default  the Mode's defaults
//   lo       every continuous or hybrid parameter at 25 % of its host-normalised range, every stepped one at its first
//            detent
//   hi       ... at 75 %, stepped at the last detent
//   mid      ... at 50 %, stepped at the middle detent, mix 0.5 where mix is live
// Rows: print.<set>.<l|r>.hash (golden, exact); NOTE lines give each render's output RMS.
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;

    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;
    constexpr double kSeconds = 4.0;

    struct Program
    {
        std::vector<float> l, r;
    };

    Program program()
    {
        const auto n = static_cast<std::size_t>(kSeconds * kFs);
        Program p;
        p.l.resize(n);
        p.r.resize(n);
        constexpr double f0 = 20.0, f1 = 20000.0;
        const double k = sig::logDet(f1 / f0);                       // the sweep's log-frequency span
        const double sweepAmp = 0.25118864315095801;                 // -12 dBFS
        const float burstAmp = 0.12589254117941673f;                 // -18 dBFS
        sig::Pcg32 rl(0x7072696e, 1), rr(0x7072696e, 2);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / kFs;
            // phase in turns: integral of f0 exp(k t / T) dt = f0 T / k (exp(k t / T) - 1)
            const double turns = f0 * kSeconds / k * (sig::expDet(k * t / kSeconds) - 1.0);
            const bool burst = (i / 12000) % 2 == 0;
            p.l[i] = static_cast<float>(sweepAmp * sig::sinTurns(turns)) + (burst ? burstAmp * rl.bipolar() : 0.0f);
            p.r[i] = static_cast<float>(sweepAmp * sig::sinTurns(turns + 0.25))
                   + (burst ? burstAmp * rr.bipolar() : 0.0f);
        }
        return p;
    }

    // The raw state of a parameter set: continuous at fraction t of the host-normalised range, stepped at detent `d`
    // (0 first, 1 last, 0.5 middle).
    RawParams setOf(const ModeEntry& en, float t, float d, bool mixHalf)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        ParamView view;
        resolveView(*en.desc, base, view);
        RawParams raw = base;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
        {
            const auto pid = static_cast<Pid>(i);
            const ParamSpec* s = view.spec[i];
            if (s == nullptr || (s->kind == Kind::stepped && s->steps.empty()))
                continue;
            if (s->kind == Kind::stepped)
            {
                const auto last = static_cast<float>(s->steps.size() - 1);
                raw[pid] = s->steps[static_cast<std::size_t>(d * last + 0.5f)].plain;
            }
            else if (s->kind == Kind::continuous || s->kind == Kind::hybrid)
            {
                const float a = toNorm(pid, s->lo), b = toNorm(pid, s->hi);
                raw[pid] = toPlain(pid, a + t * (b - a));
            }
        }
        if (mixHalf && (view[Pid::mix].state == SlotState::live || view[Pid::mix].state == SlotState::stepped))
            raw[Pid::mix] = 0.5f;
        return raw;
    }

    std::pair<std::vector<float>, std::vector<float>> render(const ModeEntry& en, const RawParams& raw,
                                                             const Program& in)
    {
        HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = kBlock;
        cfg.quality = Quality::std;
        cfg.budget = LookaheadBudget::off;
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        const std::size_t n = in.l.size();
        std::vector<float> l(n), r(n);
        for (std::size_t off = 0; off < n; off += static_cast<std::size_t>(kBlock))
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(kBlock), n - off);
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            float* outs[2] = { l.data() + off, r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, bp);
        }
        return { l, r };
    }

    double rmsDb(const std::vector<float>& x)
    {
        double e = 0.0;
        for (const float v : x)
            e += static_cast<double>(v) * static_cast<double>(v);
        return 10.0 * std::log10(std::max(e / static_cast<double>(x.size()), 1e-30));
    }
} // namespace

FCMP_PROBE(dsp, print)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const Program in = program();
    const std::pair<const char*, RawParams> sets[] = {
        { "default", fcmp::probe::modeRaw(en) },
        { "lo", setOf(en, 0.25f, 0.0f, false) },
        { "hi", setOf(en, 0.75f, 1.0f, false) },
        { "mid", setOf(en, 0.5f, 0.5f, true) },
    };
    for (const auto& [name, raw] : sets)
    {
        const auto [l, r] = render(en, raw, in);
        const std::string k = std::string("print.") + name;
        std::printf("NOTE     %s: output RMS %.4f / %.4f dBFS (L/R)\n", k.c_str(), rmsDb(l), rmsDb(r));
        P.hash(k + ".l.hash", funkgui::test::hashFloats(std::span<const float>(l)));
        P.hash(k + ".r.hash", funkgui::test::hashFloats(std::span<const float>(r)));
    }
    return P.finish();
}
