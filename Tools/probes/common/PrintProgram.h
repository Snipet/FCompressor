// Tools/probes/common/PrintProgram.h: dsp.print's program material and parameter sets (03 §3.4 "dsp.print", C §5.12),
// shared by the probe (Tools/probes/dsp/print.cpp) and by the web engine check (Tools/web/enginecheck.cpp, ADR-93),
// which renders the same material through the C ABI and compares with the probe's golden rows. Header-only, JUCE-free
// and harness-free: fcdsp and Signals.h alone, so it builds in every configuration, the web one included.
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
// lo, hi and mid pass through the host maps (toNorm, toPlain: libm), default does not.
//
// Moving anything here moves every print.*.hash golden row (01 §0, §9.1): the material is frozen with them.
#pragma once

#include "Signals.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fcmp::probe::printprog
{
    inline constexpr double kFs = 48000.0;
    inline constexpr double kSeconds = 4.0;

    struct Program
    {
        std::vector<float> l, r;
    };

    inline Program program()
    {
        namespace sig = fcmp::probe::sig;
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

    // A fresh instance of the Mode: the 22 Mode-filtered host defaults, the entry's slot, the lookahead budget off,
    // then the Mode's own defaults (what EngineRig's modeRaw(entry) returns; restated here so that this header needs no
    // probe library).
    inline fcdsp::RawParams defaults(const fcdsp::ModeEntry& en)
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = fcdsp::kHostParams[i].def;
        const int slot = fcdsp::slotOf(en);
        raw.modeSlot = static_cast<std::uint8_t>(slot < 0 ? 0 : slot);
        raw.budget = fcdsp::LookaheadBudget::off;
        fcdsp::modeDefaults(*en.desc, raw);
        return raw;
    }

    // The raw state of a parameter set: continuous at fraction t of the host-normalised range, stepped at detent `d`
    // (0 first, 1 last, 0.5 middle).
    inline fcdsp::RawParams setOf(const fcdsp::ModeEntry& en, float t, float d, bool mixHalf)
    {
        using namespace fcdsp;
        const RawParams base = defaults(en);
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

    // The four sets, in the order of the golden rows: print.<name>.<l|r>.hash.
    struct Set
    {
        const char*      name;
        fcdsp::RawParams raw;
    };

    inline std::array<Set, 4> sets(const fcdsp::ModeEntry& en)
    {
        return { { { "default", defaults(en) },
                   { "lo", setOf(en, 0.25f, 0.0f, false) },
                   { "hi", setOf(en, 0.75f, 1.0f, false) },
                   { "mid", setOf(en, 0.5f, 0.5f, true) } } };
    }
} // namespace fcmp::probe::printprog
