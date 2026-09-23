// FCMP_PROBE layer=dsp name=rt scope=mode timeout=60
//
// dsp.rt.<key> (F4, S3; F7, S6; D12: 03 §3.4 Host rows, C §5.8; K2 #18): real-time safety, isolation and determinism
// of fcdsp::EngineHost at every Quality: ECO without lookahead (the S3 keys, rt.*), STD with a 5 ms lookahead budget
// (rt.std.*) and HQ with a 20 ms budget (rt.hq.*).
//
// Script (48 kHz, 2 s, blocks of 512 with a 0-length and a 1-sample block now and then; a 110 Hz + noise program):
// the editor attaches and detaches (twice, the count semantics), the tap is set and cleared, every stepped parameter
// of the Mode walks its detents (det, stmode, voice ... kernel-key changes crossfade two engines: construct into the
// idle arena, prepare, seed, run both, destroy, 01 §5.5; requests inside a fade or its 50 ms gap latch), the slot moves
// to an unassigned slot (resolveSlot maps it) and back, EXT toggles with a 2-channel key bus, continuous parameters
// sweep (`look` too under a budget), bypass, host bypass, delta and SC listen toggle, requestSnap() and reset() are
// called, and one block carries a non-finite parameter (the poison fallback). Around EVERY process() call:
//   rt[.<q>].allocs            0: the allocation counter (replacement operator new, this thread only; ProbeRegistry.h)
//   rt[.<q>].rt_calls          0: malloc/free/mutex/unfair-lock/write/mach_msg calls counted by the RtInterposer when
//                              the rtsan preset built it (fcmp::probe::rt::available()); a NOTE elsewhere
//   rt[.<q>].calls             the number of process() calls counted (> 0 by construction)
// Isolation and determinism (C §5.8 D12):
//   rt[.<q>].isolation.mismatches  two hosts with different parameters, processed interleaved block by block, each
//                              equal bit for bit to the same host run alone (no shared statics, no lazily built tables)
//   rt[.<q>].isolation.differ  ... and the two outputs differ (the isolation row is not vacuous)
//   rt[.<q>].determinism.mismatches  the scripted run twice, fresh hosts: identical output bit for bit
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;

    constexpr float kFs = 48000.0f;
    constexpr int kBlock = 512;

    struct Setup
    {
        const char* prefix;                             // "rt" (ECO, the S3 keys), "rt.std", "rt.hq"
        Quality quality;
        LookaheadBudget budget;
    };

    constexpr Setup kSetups[3] = { { "rt", Quality::eco, LookaheadBudget::off },
                                   { "rt.std", Quality::std, LookaheadBudget::ms5 },
                                   { "rt.hq", Quality::hq, LookaheadBudget::ms20 } };

    HostConfig hostConfig(const Setup& su, int keyChans)
    {
        HostConfig c;
        c.fs = kFs;
        c.maxBlock = kBlock;
        c.quality = su.quality;
        c.budget = su.budget;
        c.keyChans = keyChans;
        return c;
    }

    // The first slot no Mode owns (resolveSlot maps it to clean): a Mode-slot move that must not allocate.
    int unassignedSlot()
    {
        for (int s = 0; s < kModeCapacity; ++s)
            if (bySlot(s) == nullptr)
                return s;
        return 0;
    }

    struct Counts
    {
        std::uint64_t allocs = 0, rtCalls = 0, calls = 0;
    };

    // One process() call with every counter armed around it (and nothing else).
    void countedProcess(EngineHost& h, const ProcessIo& io, const BlockParams& bp, Counts& c)
    {
        const bool rtOn = fcmp::probe::rt::available();
        if (rtOn)
        {
            fcmp::probe::rt::reset();
            fcmp::probe::rt::arm();
        }
        {
            const fcmp::probe::alloc::Scope scope;
            h.process(io, bp);
        }
        if (rtOn)
        {
            fcmp::probe::rt::disarm();
            c.rtCalls += fcmp::probe::rt::counts().total();
        }
        c.allocs += fcmp::probe::alloc::allocations();
        ++c.calls;
    }

    struct Program
    {
        std::vector<float> l, r, kl, kr;
    };

    Program program(std::size_t n)
    {
        Program p;
        p.l.resize(n);
        p.r.resize(n);
        p.kl.resize(n);
        p.kr.resize(n);
        sig::Pcg32 rng(0x72747274, 3);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double env = (i / 12000) % 2 == 0 ? 1.0 : 0.25;
            p.l[i] = sig::sineAt(static_cast<std::int64_t>(i), 110.0, kFs, 0.5 * env) + 0.05f * rng.bipolar();
            p.r[i] = sig::sineAt(static_cast<std::int64_t>(i), 330.0, kFs, 0.4 * env) + 0.05f * rng.bipolar();
            p.kl[i] = sig::sineAt(static_cast<std::int64_t>(i), 55.0, kFs, 0.8);
            p.kr[i] = 0.3f * rng.bipolar();
        }
        return p;
    }

    // The scripted run (see the file comment). Returns the output (L then R) and accumulates the counters.
    std::vector<float> scripted(const ModeEntry& en, const Setup& su, const Program& in, Counts& counts)
    {
        const ModeDescriptor& desc = *en.desc;
        const RawParams base = fcmp::probe::modeRaw(en, su.budget);
        ParamView view;
        resolveView(desc, base, view);

        // parameter states to walk: every detent of every stepped parameter, then continuous sweeps
        std::vector<RawParams> states;
        states.push_back(base);
        for (std::size_t i = 0; i < kNumModeParams; ++i)
        {
            const auto pid = static_cast<Pid>(i);
            const ParamSpec* s = view.spec[i];
            if (s == nullptr)
                continue;
            if (s->kind == Kind::stepped)
                for (const Step& st : s->steps)
                {
                    RawParams r = base;
                    r[pid] = st.plain;
                    states.push_back(r);
                }
            else if (s->kind == Kind::continuous || s->kind == Kind::hybrid)
                for (const float t : { 0.1f, 0.9f })
                {
                    RawParams r = base;
                    r[pid] = toPlain(pid, t * toNorm(pid, s->lo) + (1.0f - t) * toNorm(pid, s->hi));
                    states.push_back(r);
                }
        }

        const std::size_t n = in.l.size();
        std::vector<float> out(2 * n, 0.0f);
        std::vector<simd::f32x4> tapBuf(n, simd::set1(0.0f));
        std::vector<std::uint8_t> tapBits(n, 0);
        TestTap tap;
        tap.grDb = tapBuf;
        tap.bits = tapBits;

        auto host = std::make_unique<EngineHost>();          // constructed and configured outside the counters
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = fcmp::probe::resolveRaw(en, base).eng;
        host->configure(hostConfig(su, 2), bp);
        const int spare = unassignedSlot();

        std::size_t off = 0;
        for (std::size_t block = 0; off < n; ++block)
        {
            const RawParams& raw = states[block % states.size()];
            bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
            bp.slot = static_cast<std::uint8_t>(block % 23 == 11 ? spare : slotOf(en));
            bp.extKey = (block / 7) % 2 == 1;
            bp.bypass = (block / 13) % 3 == 2;
            bp.delta = (block / 5) % 4 == 1;
            bp.listen = (block / 9) % 5 == 2;
            if (block % 31 == 17)
                bp.eng.thrDb = std::numeric_limits<float>::quiet_NaN();     // the poison fallback
            if (block == 20)
                host->setUiAttached(true);
            if (block == 25)
                host->setUiAttached(true);                                  // two editors: a count
            if (block == 40)
                host->setUiAttached(false);
            if (block == 60)
                host->setUiAttached(false);
            if (block == 30)
            {
                tap.firstSample = off;
                host->setTap(&tap);
            }
            if (block == 90)
                host->setTap(nullptr);
            if (block % 17 == 5)
                host->requestSnap();

            std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(kBlock), n - off);
            if (block % 19 == 3)
                len = 1;
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            const float* keys[2] = { in.kl.data() + off, in.kr.data() + off };
            float* outs[2] = { out.data() + off, out.data() + n + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.key = keys;
            io.numKey = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            io.hostBypassed = (block / 11) % 4 == 3;
            countedProcess(*host, io, bp, counts);
            if (block % 9 == 4)
            {
                ProcessIo none = io;
                none.n = 0;
                countedProcess(*host, none, bp, counts);
            }
            if (block == 70)                                                 // reset() is audio-thread too
            {
                {
                    const fcmp::probe::alloc::Scope scope;
                    host->reset();
                }
                counts.allocs += fcmp::probe::alloc::allocations();
                ++counts.calls;
            }
            off += len;
        }
        host->setTap(nullptr);
        return out;
    }

    // One host's plain run over the program (blocks of 512), or two hosts interleaved block by block.
    struct Pair
    {
        std::vector<float> a, b;
    };

    Pair interleaved(const Setup& su, const BlockParams& pa, const BlockParams& pb, const Program& in, bool runA,
                     bool runB)
    {
        const std::size_t n = in.l.size();
        Pair out{ std::vector<float>(2 * n, 0.0f), std::vector<float>(2 * n, 0.0f) };
        auto ha = std::make_unique<EngineHost>();
        auto hb = std::make_unique<EngineHost>();
        ha->configure(hostConfig(su, 0), pa);
        hb->configure(hostConfig(su, 0), pb);
        ha->setUiAttached(true);
        for (std::size_t off = 0; off < n; off += static_cast<std::size_t>(kBlock))
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(kBlock), n - off);
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            if (runA)
            {
                float* outs[2] = { out.a.data() + off, out.a.data() + n + off };
                io.out = outs;
                ha->process(io, pa);
            }
            if (runB)
            {
                float* outs[2] = { out.b.data() + off, out.b.data() + n + off };
                io.out = outs;
                hb->process(io, pb);
            }
        }
        return out;
    }

    std::int64_t mismatches(const std::vector<float>& a, const std::vector<float>& b)
    {
        std::int64_t m = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            m += a[i] == b[i] ? 0 : 1;
        return m + static_cast<std::int64_t>(a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
    }
} // namespace

FCMP_PROBE(dsp, rt)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const std::size_t n = static_cast<std::size_t>(2.0f * kFs);
    const Program in = program(n);

    for (const Setup& su : kSetups)
    {
        const std::string k = su.prefix;
        Counts counts;
        const std::vector<float> first = scripted(en, su, in, counts);
        std::printf("NOTE     %s: %llu process()/reset() calls counted\n", k.c_str(),
                    static_cast<unsigned long long>(counts.calls));
        P.eq(k + ".allocs", static_cast<std::int64_t>(counts.allocs), 0);
        if (fcmp::probe::rt::available())
            P.eq(k + ".rt_calls", static_cast<std::int64_t>(counts.rtCalls), 0);
        else
            std::printf("NOTE     %s.rt_calls: no RtInterposer in this build (the rtsan preset builds it); not "
                        "counted\n",
                        k.c_str());
        P.ge(k + ".calls", static_cast<double>(counts.calls), 1.0);

        Counts again;
        const std::vector<float> second = scripted(en, su, in, again);
        P.eq(k + ".determinism.mismatches", mismatches(first, second), 0);

        // Isolation: A at the Mode's defaults, B with every parameter moved (a different kernel where the Mode has
        // one).
        const RawParams base = fcmp::probe::modeRaw(en, su.budget);
        ParamView view;
        resolveView(*en.desc, base, view);
        RawParams other = base;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
        {
            const auto pid = static_cast<Pid>(i);
            const ParamSpec* s = view.spec[i];
            if (s == nullptr)
                continue;
            if (s->kind == Kind::stepped && s->steps.size() > 1)
                other[pid] = s->steps.back().plain;
            else if (s->kind == Kind::continuous || s->kind == Kind::hybrid)
                other[pid] = toPlain(pid, 0.3f * toNorm(pid, s->lo) + 0.7f * toNorm(pid, s->hi));
        }
        BlockParams pa, pb;
        pa.slot = pb.slot = static_cast<std::uint8_t>(slotOf(en));
        pa.eng = fcmp::probe::resolveRaw(en, base).eng;
        pb.eng = fcmp::probe::resolveRaw(en, other).eng;
        const Pair both = interleaved(su, pa, pb, in, true, true);
        const Pair aloneA = interleaved(su, pa, pb, in, true, false);
        const Pair aloneB = interleaved(su, pa, pb, in, false, true);
        P.eq(k + ".isolation.mismatches", mismatches(both.a, aloneA.a) + mismatches(both.b, aloneB.b), 0);
        P.eq(k + ".isolation.differ", mismatches(both.a, both.b) > 0 ? 1 : 0, 1);
    }

    return P.finish();
}
