// FCMP_PROBE layer=dsp name=hostile scope=mode timeout=60
//
// dsp.hostile.<key> (F4, S3; D10: 03 §3.4 Host rows, C §5.8; 01 §5.8; K2 #13): hostile input through
// fcdsp::EngineHost at ECO, each case next to a control instance fed the clean program. The editor is attached, so
// every block's UiFrame is read back (single-threaded here: the read always succeeds).
//
// Program: 1.5 s at 48 kHz, blocks of 512: L = 110 Hz at -6 dBFS, R = 220 Hz at -9 dBFS, at the Mode's defaults
// (about 9 dB of GR on Clean). Injections start at 0.5 s (sample 24000, inside block 46).
//
// Rows (spec unless noted; <c> = a case below):
//   hostile.<c>.nonfinite_out      0: sanitize() runs before any delay line, filter or meter (K2 #13)
//   hostile.<c>.flagged            1: kUiPoisonReset is set in the injection's block (sanitize replaced or clamped)
//   hostile.<c>.recovery_blocks    <= 1: blocks after the injected ones that still flag kUiPoisonReset (here 0)
//   hostile.<c>.tail_err_db        <= 1 dB: |RMS ratio| to the control over 100-200 ms after the injection; also a
//                                  golden row (abs:1). Not a spec row for big.*: 1e30 is clamped to 1e6 (+120 dBFS)
//                                  and compressed as the (legal) input it became, so the GR it builds takes the Mode's
//                                  release to leave (NOTE). A whole block always does; a single sample does in a
//                                  fast-attack Mode (DW, S4: FET 76's 0.2 ms, Mu 67's TC attacks, Brickwall's attack
//                                  inside a budget-OFF lookahead catch one sample at +120 dBFS)
//   cases: {nan,inf,big}.{sample,block} on the main input (one sample of L; a whole block, both channels) and
//          key.{nan,inf,big}.{sample,block} on an active external key (extKey on, a 2-channel key bus)
//   hostile.poison.<what>.*        the poison fallback (01 §5.8), forced by a non-finite EngineParams value for one
//                                  block (thrDb: the engine's state; makeupDb: the host's gain smoother):
//                                  .dry_mismatches 0 (that block outputs the sanitised, latency-aligned dry input bit
//                                  for bit),
//                                  .flagged 1, .recovery_blocks 0 (the next block is processed and finite again),
//                                  .nonfinite_out 0
//   hostile.dc.gr_err_db           DC 0.5 for 2 s: the settled GR against the Mode's static curve at -6.02 dBFS
//                                  (fidelity: a NOTE while the Mode is provisional; skipped with a NOTE when the Mode's
//                                  SC HPF is on: the host SC filter is F5's)
//   hostile.plus40.nonfinite_out, hostile.plus40.flagged  a +40 dBFS 110 Hz sine (below the clamp: not flagged)
//   hostile.silence.nonzero        silence after a 0.5 s noise burst gives exactly 0 (Modes without an active colour
//                                  stage: !hasColour, or drive n/a)
//   hostile.silence.tail_cost_ratio  per-sample cost of the post-burst silent tail against the burst, with the probe's
//                                  own flush-to-zero turned OFF (the host must set it itself): < 2 (same-machine ratio,
//                                  min of 7 repetitions)
//   hostile.unprepared.mismatches  process() before configure(): input copied to output bit for bit (HR B §1.7)
//   hostile.zero_length.mismatches 0-length process() calls between blocks change nothing
//   hostile.oversize.mismatches    configured for 64-sample blocks, fed 1024: identical to 64-sample blocks
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
#endif

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;

    constexpr float kFs = 48000.0f;
    constexpr int kBlock = 512;
    constexpr std::size_t kInject = 24000;

    // Flush-to-zero OFF for a scope (the probe body runs under ProbeMain's ScopedFtz): EngineHost::process must turn
    // it on itself (01 §5.7).
    class ScopedNoFtz
    {
    public:
        ScopedNoFtz() noexcept
        {
#if defined(__aarch64__)
            __asm__ volatile("mrs %0, fpcr" : "=r"(saved_) : : "memory");
            const std::uint64_t off = saved_ & ~(std::uint64_t{ 1 } << 24);
            __asm__ volatile("msr fpcr, %0" : : "r"(off) : "memory");
#else
            saved_ = _mm_getcsr();
            _mm_setcsr(static_cast<unsigned int>(saved_) & ~0x8040u);
#endif
        }
        ~ScopedNoFtz()
        {
#if defined(__aarch64__)
            __asm__ volatile("msr fpcr, %0" : : "r"(saved_) : "memory");
#else
            _mm_setcsr(static_cast<unsigned int>(saved_));
#endif
        }
        ScopedNoFtz(const ScopedNoFtz&) = delete;
        ScopedNoFtz& operator=(const ScopedNoFtz&) = delete;

    private:
        std::uint64_t saved_ = 0;
    };

    // ---- host driver (the same small driver as the other F4 probes; one per file, no shared probe file) -------------
    HostConfig ecoConfig(int keyChans = 0, int maxBlock = kBlock)
    {
        HostConfig c;
        c.fs = kFs;
        c.maxBlock = maxBlock;
        c.quality = Quality::eco;
        c.budget = LookaheadBudget::off;
        c.keyChans = keyChans;
        return c;
    }

    BlockParams blockOf(const ModeEntry& en, const RawParams& raw)
    {
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
        return bp;
    }

    struct Signal
    {
        std::vector<float> l, r, kl, kr;          // kl/kr empty: no key bus
    };

    struct BlockInfo
    {
        std::size_t at = 0;
        std::uint32_t flags = 0;
    };

    struct Run
    {
        std::vector<float> l, r, gr;              // gr: tap lane 0
        std::vector<BlockInfo> blocks;
        int latency = 0;
        std::int64_t nonfinite = 0;
    };

    using ParamsAt = std::function<BlockParams(std::size_t off)>;

    // A fresh host (attached, tapped) over the signal in blocks of bs; paramsAt(off) gives each block's parameters.
    Run render(const HostConfig& cfg, const ParamsAt& paramsAt, const Signal& s, int bs = kBlock,
               bool zeroLengthBetween = false)
    {
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, paramsAt(0));
        host->setUiAttached(true);
        const std::size_t n = s.l.size();
        const bool key = !s.kl.empty();
        Run run;
        run.l.assign(n, 0.0f);
        run.r.assign(n, 0.0f);
        std::vector<simd::f32x4> gr(n, simd::set1(0.0f));
        TestTap tap;
        tap.grDb = gr;
        host->setTap(&tap);
        run.latency = host->latencySamples();
        for (std::size_t off = 0; off < n;)
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(bs), n - off);
            const float* ins[2] = { s.l.data() + off, s.r.data() + off };
            float* outs[2] = { run.l.data() + off, run.r.data() + off };
            const float* keys[2] = { key ? s.kl.data() + off : nullptr, key ? s.kr.data() + off : nullptr };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.key = key ? keys : nullptr;
            io.numKey = key ? 2 : 0;
            io.n = static_cast<int>(len);
            const BlockParams bp = paramsAt(off);
            host->process(io, bp);
            if (zeroLengthBetween)
            {
                ProcessIo none = io;
                none.n = 0;
                host->process(none, bp);
            }
            UiFrame f{};
            (void) host->readUiFrame(f);
            run.blocks.push_back(BlockInfo{ off, f.flags });
            off += len;
        }
        host->setTap(nullptr);
        host->setUiAttached(false);
        run.gr.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            run.gr[i] = simd::lane<0>(gr[i]);
        for (std::size_t i = 0; i < n; ++i)
            run.nonfinite += (std::isfinite(run.l[i]) ? 0 : 1) + (std::isfinite(run.r[i]) ? 0 : 1);
        return run;
    }

    Run render(const HostConfig& cfg, const BlockParams& bp, const Signal& s, int bs = kBlock)
    {
        return render(cfg, [&](std::size_t) { return bp; }, s, bs);
    }

    std::int64_t mismatches(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0,
                            std::size_t to = std::numeric_limits<std::size_t>::max())
    {
        std::int64_t m = 0;
        for (std::size_t i = from; i < a.size() && i < b.size() && i < to; ++i)
            m += a[i] == b[i] ? 0 : 1;
        return m;
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double e = 0.0;
        for (std::size_t i = from; i < to && i < x.size(); ++i)
            e += static_cast<double>(x[i]) * x[i];
        return std::sqrt(e / static_cast<double>(std::max<std::size_t>(to - from, 1)));
    }

    // |20 log10(rms(test) / rms(control))| over 100-200 ms after the injection, both channels.
    double tailErrDb(const Run& t, const Run& c)
    {
        const std::size_t a = kInject + static_cast<std::size_t>(0.1f * kFs);
        const std::size_t b = kInject + static_cast<std::size_t>(0.2f * kFs);
        const auto db = [&](const std::vector<float>& x) { return measure::dbFromAmplitude(rms(x, a, b)); };
        const double el = std::fabs(db(t.l) - db(c.l));
        const double er = std::fabs(db(t.r) - db(c.r));
        return std::max(el, er);
    }

    // The block containing sample s.
    std::size_t blockOfSample(const Run& r, std::size_t s)
    {
        std::size_t b = 0;
        while (b + 1 < r.blocks.size() && r.blocks[b + 1].at <= s)
            ++b;
        return b;
    }

    bool flagged(const Run& r, std::size_t block) { return (r.blocks[block].flags & kUiPoisonReset) != 0; }

    std::int64_t flaggedAfter(const Run& r, std::size_t block)
    {
        std::int64_t k = 0;
        for (std::size_t b = block + 1; b < r.blocks.size(); ++b)
            k += flagged(r, b) ? 1 : 0;
        return k;
    }
} // namespace

FCMP_PROBE(dsp, hostile)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    const auto& tol = fcmp::probe::tol::forRigor(desc.rigor);
    fcmp::probe::Fidelity F(P, desc.provisional);
    const HostConfig cfg = ecoConfig();

    const RawParams base = fcmp::probe::modeRaw(en);
    ParamView view;
    resolveView(desc, base, view);
    const BlockParams bp = blockOf(en, base);
    BlockParams bpKey = bp;
    bpKey.extKey = true;

    const std::size_t n = static_cast<std::size_t>(1.5f * kFs);
    Signal clean;
    clean.l.resize(n);
    clean.r.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        clean.l[i] = sig::sineAt(static_cast<std::int64_t>(i), 110.0, kFs, 0.5011872336272722);
        clean.r[i] = sig::sineAt(static_cast<std::int64_t>(i), 220.0, kFs, 0.3548133892335755);
    }
    Signal cleanKey = clean;
    cleanKey.kl = clean.l;
    cleanKey.kr = clean.r;

    const Run control = render(cfg, bp, clean);
    const Run controlKey = render(ecoConfig(2), bpKey, cleanKey);
    std::printf("NOTE     hostile: control GR at the injection point %.4g dB\n",
                static_cast<double>(control.gr[kInject]));

    // ---- non-finite and huge input on the main and key inputs ------------------------------------------------------
    const float kValues[3] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1e30f };
    const char* kNames[3] = { "nan", "inf", "big" };
    for (const bool onKey : { false, true })
        for (int v = 0; v < 3; ++v)
            for (const bool wholeBlock : { false, true })
            {
                Signal s = onKey ? cleanKey : clean;
                std::vector<float>& a = onKey ? s.kl : s.l;
                std::vector<float>& b = onKey ? s.kr : s.r;
                const std::size_t len = wholeBlock ? static_cast<std::size_t>(kBlock) : 1;
                for (std::size_t i = kInject; i < kInject + len; ++i)
                {
                    a[i] = kValues[v];
                    if (wholeBlock)
                        b[i] = kValues[v];
                }
                const std::string k = std::string("hostile.") + (onKey ? "key." : "") + kNames[v]
                                    + (wholeBlock ? ".block" : ".sample");
                const Run t = onKey ? render(ecoConfig(2), bpKey, s) : render(cfg, bp, s);
                const Run& c = onKey ? controlKey : control;
                const std::size_t first = blockOfSample(t, kInject), last = blockOfSample(t, kInject + len - 1);
                P.eq(k + ".nonfinite_out", t.nonfinite, 0);
                P.eq(k + ".flagged", flagged(t, first) ? 1 : 0, 1);
                P.le(k + ".recovery_blocks", static_cast<double>(flaggedAfter(t, last)), 1.0);
                const double tail = tailErrDb(t, c);
                if (v == 2)
                    std::printf("NOTE     %s.tail_err_db = %.4g dB: clamped +120 dBFS input is compressed as input; "
                                "its GR leaves at the Mode's release (not a spec row)\n",
                                k.c_str(), tail);
                else
                    P.le(k + ".tail_err_db", tail, 1.0);
                P.num(k + ".tail_err_db", tail, Tol::abs(1.0));
            }

    // ---- the poison fallback (01 §5.8), forced through a non-finite parameter for one block ------------------------
    {
        const std::size_t poisonAt = static_cast<std::size_t>(kBlock) * (kInject / static_cast<std::size_t>(kBlock));
        for (const int what : { 0, 1 })
        {
            const std::string k = std::string("hostile.poison.") + (what == 0 ? "thr" : "makeup");
            const ParamsAt at = [&](std::size_t off) {
                BlockParams b = bp;
                if (off == poisonAt)
                    (what == 0 ? b.eng.thrDb : b.eng.makeupDb) = std::numeric_limits<float>::quiet_NaN();
                return b;
            };
            const Run t = render(cfg, at, clean);
            const std::size_t blk = blockOfSample(t, poisonAt);
            const std::size_t end = poisonAt + static_cast<std::size_t>(kBlock);
            P.eq(k + ".dry_mismatches",
                 mismatches(t.l, clean.l, poisonAt, end) + mismatches(t.r, clean.r, poisonAt, end), 0);
            P.eq(k + ".flagged", flagged(t, blk) ? 1 : 0, 1);
            P.eq(k + ".recovery_blocks", flaggedAfter(t, blk), 0);
            P.eq(k + ".nonfinite_out", t.nonfinite, 0);
            std::printf("NOTE     %s: after the poisoned block the GR restarts from 0 dB: %.4g dB 10 ms later, "
                        "control %.4g dB\n",
                        k.c_str(), static_cast<double>(t.gr[end + 480]), static_cast<double>(control.gr[end + 480]));
        }
    }

    // ---- DC: the settled GR follows the static curve ----------------------------------------------------------------
    if (bp.eng.scHpfHz > 0.0f)
        std::printf("NOTE     hostile.dc: %s's SC HPF is on (%.4g Hz) at its defaults; the host SC filter is F5's; "
                    "skipped\n",
                    desc.name.data(), static_cast<double>(bp.eng.scHpfHz));
    else
    {
        const std::size_t m = static_cast<std::size_t>(2.0f * kFs);
        Signal dc;
        dc.l.assign(m, 0.5f);
        dc.r.assign(m, 0.5f);
        const Run t = render(cfg, bp, dc);
        const float x = static_cast<float>(measure::dbFromAmplitude(0.5)) + bp.eng.preGainDb;
        float want = 0.0f;
        en.staticGr(bp.eng, &x, &want, 1);
        const float got = t.gr.back();
        std::printf("NOTE     hostile.dc: settled GR %.6g dB, static curve %.6g dB at %.4g dB (detector domain)\n",
                    static_cast<double>(got), static_cast<double>(want), static_cast<double>(x));
        F.near("hostile.dc.gr_err_db", static_cast<double>(got - want), 0.0, tol.curveOutsideKneeDb);
        P.eq("hostile.dc.nonfinite_out", t.nonfinite, 0);
    }

    // ---- +40 dBFS --------------------------------------------------------------------------------------------------
    {
        const std::size_t m = static_cast<std::size_t>(0.5f * kFs);
        Signal loud;
        loud.l.resize(m);
        loud.r.resize(m);
        for (std::size_t i = 0; i < m; ++i)
            loud.l[i] = loud.r[i] = sig::sineAt(static_cast<std::int64_t>(i), 110.0, kFs, 100.0);
        const Run t = render(cfg, bp, loud);
        std::int64_t flags = 0;
        for (std::size_t b = 0; b < t.blocks.size(); ++b)
            flags += flagged(t, b) ? 1 : 0;
        P.eq("hostile.plus40.nonfinite_out", t.nonfinite, 0);
        P.eq("hostile.plus40.flagged", flags, 0);
    }

    // ---- silence after a burst: exactly 0, and no denormal tail cost ------------------------------------------------
    {
        const std::size_t burst = static_cast<std::size_t>(0.5f * kFs), m = static_cast<std::size_t>(1.5f * kFs);
        Signal s;
        s.l.assign(m, 0.0f);
        s.r.assign(m, 0.0f);
        sig::Pcg32 rng(0x686f7374, 7);
        for (std::size_t i = 0; i < burst; ++i)
        {
            s.l[i] = 0.7f * rng.bipolar();
            s.r[i] = 0.7f * rng.bipolar();
        }
        const Run t = render(cfg, bp, s);
        const bool colourActive = desc.hasColour && view[Pid::drive].state != SlotState::na;
        if (!colourActive)
        {
            std::int64_t nz = 0;
            for (std::size_t i = burst; i < m; ++i)
                nz += (t.l[i] != 0.0f ? 1 : 0) + (t.r[i] != 0.0f ? 1 : 0);
            P.eq("hostile.silence.nonzero", nz, 0);
        }
        else
            std::printf("NOTE     hostile.silence: %s runs a colour stage at its defaults; exact silence is not "
                        "required\n",
                        desc.name.data());

        // The tail's per-sample cost against the burst's, flush-to-zero off in the caller (min of 7 runs each).
        const auto cost = [&](std::size_t from, std::size_t to) {
            auto host = std::make_unique<EngineHost>();
            host->configure(cfg, bp);
            std::vector<float> ol(m), orr(m);
            double best = std::numeric_limits<double>::max();
            for (int rep = 0; rep < 7; ++rep)
            {
                host->reset();
                double spent = 0.0;
                for (std::size_t off = 0; off < m; off += static_cast<std::size_t>(kBlock))
                {
                    const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(kBlock), m - off);
                    const float* ins[2] = { s.l.data() + off, s.r.data() + off };
                    float* outs[2] = { ol.data() + off, orr.data() + off };
                    ProcessIo io;
                    io.in = ins;
                    io.numIn = 2;
                    io.out = outs;
                    io.numOut = 2;
                    io.n = static_cast<int>(len);
                    const auto t0 = std::chrono::steady_clock::now();
                    host->process(io, bp);
                    const auto t1 = std::chrono::steady_clock::now();
                    if (off >= from && off + len <= to)
                        spent += std::chrono::duration<double>(t1 - t0).count();
                }
                best = std::min(best, spent / static_cast<double>(to - from));
            }
            return best;
        };
        double ratio = 0.0;
        {
            const ScopedNoFtz noFtz;
            const std::size_t tailFrom = burst + static_cast<std::size_t>(0.25f * kFs);   // well into the release
            const double burstCost = cost(0, burst - (burst % static_cast<std::size_t>(kBlock)));
            const double tailCost = cost(tailFrom - (tailFrom % static_cast<std::size_t>(kBlock)),
                                         m - (m % static_cast<std::size_t>(kBlock)));
            ratio = tailCost / std::max(burstCost, 1e-15);
        }
        std::printf("NOTE     hostile.silence.tail_cost_ratio = %.4g (flush-to-zero off in the caller)\n", ratio);
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define FCMP_HOSTILE_SANITIZED 1
#  endif
#endif
#ifdef FCMP_HOSTILE_SANITIZED   // S4 lead fix: a CPU-cost ratio is meaningless under a sanitizer (03 §3.1: no timing gates)
        std::printf("NOTE     hostile.silence.tail_cost_ratio not judged in a sanitizer build\n");
#else
        P.le("hostile.silence.tail_cost_ratio", ratio, 2.0);
#endif
    }

    // ---- unprepared, zero-length, oversize blocks -------------------------------------------------------------------
    {
        EngineHost raw;                                           // never configured
        std::vector<float> ol(n), orr(n), ml(n);
        const float* ins[2] = { clean.l.data(), clean.r.data() };
        float* outs[2] = { ol.data(), orr.data() };
        ProcessIo io;
        io.in = ins;
        io.numIn = 2;
        io.out = outs;
        io.numOut = 2;
        io.n = static_cast<int>(n);
        raw.process(io, bp);
        const float* monoIn[1] = { clean.l.data() };
        float* monoOut[1] = { ml.data() };
        ProcessIo mono;
        mono.in = monoIn;
        mono.numIn = 1;
        mono.out = monoOut;
        mono.numOut = 1;
        mono.n = static_cast<int>(n);
        raw.process(mono, bp);
        P.eq("hostile.unprepared.mismatches",
             mismatches(ol, clean.l) + mismatches(orr, clean.r) + mismatches(ml, clean.l), 0);

        const Run z = render(cfg, [&](std::size_t) { return bp; }, clean, kBlock, true);
        P.eq("hostile.zero_length.mismatches", mismatches(z.l, control.l) + mismatches(z.r, control.r), 0);

        const Run small = render(ecoConfig(0, 64), bp, clean, 64);
        const Run big = render(ecoConfig(0, 64), bp, clean, 1024);
        P.eq("hostile.oversize.mismatches", mismatches(big.l, small.l) + mismatches(big.r, small.r), 0);
        P.eq("hostile.oversize.nonfinite_out", big.nonfinite, 0);
    }

    F.summary();
    return P.finish();
}
