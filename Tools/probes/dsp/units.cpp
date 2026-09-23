// FCMP_PROBE layer=dsp name=units scope=global timeout=60
//
// dsp.units (F1, S1; 03 §3.4; E §2.3, §4.6, §9.2 row 2; K2 #13, #20): the rest of fcdsp/core, spec rows only.
//
//   1. TimeLaw    lawFactor against ln 9 / ln 10 / ln 2; published <-> tau round trips (<= 1e-6 relative).
//   2. alpha      alphaFromTau against e^(-1 / (tau fs)) over tau x fs; the time constant a float alpha implies.
//   3. dB <-> lin dbFromLin / linFromDb / dbFromMs round trips and the constants.
//   4. Smoother4  tau (the 63 % point within 1.5 samples), exact landing at every sample rate (K2 #20: the stall
//                 test), the landing step's bound, lane independence, snap, instant poles.
//   5. LinearRamp exact ends, length, reversal, instant ramps.
//   6. ControlTicker  ticks at absolute multiples of kTickSamples, the first call, skipped indices, reset.
//   7. sanitize   NaN/inf -> 0, clamp to +-1e6, everything else bit for bit, the count, in place (K2 #13).
//   8. ScopedFtz  sets FZ (FTZ|DAZ) for its scope and restores the register exactly, nested too.
// No golden rows: every quantity has a closed-form expectation.
#include "ProbeRegistry.h"
#include "Signals.h"

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Sanitize.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Smoother.h"
#include "fcdsp/core/Units.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
#endif

namespace
{
    namespace simd = fcdsp::simd;
    using simd::f32x4;
    using funkgui::test::Probe;

    constexpr uint32_t bitsOf(float f) noexcept { return std::bit_cast<uint32_t>(f); }
    constexpr float fromBits(uint32_t b) noexcept { return std::bit_cast<float>(b); }

    struct Lanes
    {
        alignas(16) float v[4];
        explicit Lanes(f32x4 x) noexcept { simd::store(v, x); }
    };

    f32x4 vec(float a, float b, float c, float d) noexcept
    {
        alignas(16) const float t[4] = { a, b, c, d };
        return simd::load(t);
    }

    // The FP control register (FPCR on arm64, MXCSR on x86-64) and its flush-to-zero bits.
    uint64_t readFpControl() noexcept
    {
#if defined(__aarch64__)
        uint64_t v = 0;
        __asm__ volatile("mrs %0, fpcr" : "=r"(v) : : "memory");
        return v;
#else
        return _mm_getcsr();
#endif
    }
    void writeFpControl(uint64_t v) noexcept
    {
#if defined(__aarch64__)
        __asm__ volatile("msr fpcr, %0" : : "r"(v) : "memory");
#else
        _mm_setcsr(static_cast<unsigned int>(v));
#endif
    }
#if defined(__aarch64__)
    constexpr uint64_t kFtzBits = uint64_t{1} << 24;          // FPCR.FZ
#else
    constexpr uint64_t kFtzBits = 0x8040u;                    // MXCSR FTZ | DAZ
#endif

    // A product whose exact value is denormal, computed at run time between two memory accesses. Both matter: clang
    // folds constants in the default FP mode, and LLVM's default FP model treats the FP environment as constant, so a
    // product held only in registers may be computed on either side of an FPCR/MXCSR write (observed here: the fmul of
    // a value loaded before a ScopedFtz sank past its msr). Operands loaded and the result stored inside the scope are
    // ordered by the asm's memory clobber, which is the pattern EngineHost::process relies on.
    bool tinyProductSurvives() noexcept
    {
        volatile float a = 1.0e-30f, b = 1.0e-10f;
        volatile float p = a * b;
        return bitsOf(p) != 0u;
    }

    // ---- 1. TimeLaw ------------------------------------------------------------------------------------------------------
    void timeLawProbe(Probe& P)
    {
        using fcdsp::TimeLaw;
        const struct { TimeLaw law; const char* key; double want; } laws[] = {
            { TimeLaw::expDb, "expdb", 1.0 },      { TimeLaw::expLin, "explin", 1.0 },
            { TimeLaw::t10_90, "t10_90", std::log(9.0) }, { TimeLaw::t0_90, "t0_90", std::log(10.0) },
            { TimeLaw::t50, "t50", std::numbers::ln2 },  { TimeLaw::rateDbPerS, "rate", 1.0 },
        };
        for (const auto& l : laws)
        {
            const float f = fcdsp::lawFactor(l.law);
            P.near(std::string("timelaw.") + l.key + ".factor", static_cast<double>(f), l.want, 0.0, 1e-7);
            double worst = 0;
            for (float tau = 0.01f; tau < 60000.0f; tau *= 1.07f)
            {
                const float published = tau * f, back = published / f;
                worst = std::max(worst, std::fabs(static_cast<double>(back) / tau - 1.0));
            }
            P.le(std::string("timelaw.") + l.key + ".round_trip.max_rel_err", worst, 1e-6);
        }
        static_assert(fcdsp::lawFactor(TimeLaw::t10_90) > 2.197f && fcdsp::lawFactor(TimeLaw::t10_90) < 2.198f);
    }

    // ---- 2. alphaFromTau ---------------------------------------------------------------------------------------------------
    void alphaProbe(Probe& P)
    {
        const double rates[] = { 22050, 44100, 48000, 88200, 96000, 176400, 192000, 384000 };
        double relAlpha = 0, tau1e4 = 0, tau1e5 = 0, tauAll = 0;
        for (const double fs : rates)
            for (double tauMs = 0.01; tauMs <= 10000.0; tauMs *= 1.05)
            {
                const auto tf = static_cast<float>(tauMs), ff = static_cast<float>(fs);
                const double samples = static_cast<double>(tf) * 1e-3 * static_cast<double>(ff);
                const double want = std::exp(-1.0 / samples);
                const double a = static_cast<double>(fcdsp::alphaFromTau(tf, ff));
                if (samples >= 1.0)
                    relAlpha = std::max(relAlpha, std::fabs(a / want - 1.0));
                if (a > 0.0 && a < 1.0)
                {
                    const double implied = -1.0 / std::log(a);             // the time constant, in samples
                    const double e = std::fabs(implied / samples - 1.0);
                    if (samples <= 1e4)
                        tau1e4 = std::max(tau1e4, e);
                    if (samples <= 1e5)
                        tau1e5 = std::max(tau1e5, e);
                    tauAll = std::max(tauAll, e);
                }
            }
        std::printf("NOTE     alphaFromTau: max relative error of alpha %.3g (tau >= 1 sample); implied tau error %.3g "
                    "(tau*fs <= 1e4), %.3g (<= 1e5), %.3g (all, up to 3.8e6 samples): a float alpha near 1 has an "
                    "absolute resolution of ~6e-8\n", relAlpha, tau1e4, tau1e5, tauAll);
        P.le("alpha.max_rel_err", relAlpha, 1e-6);
        P.le("alpha.implied_tau.max_rel_err.upto_1e4_samples", tau1e4, 3e-3);
        P.le("alpha.implied_tau.max_rel_err.upto_1e5_samples", tau1e5, 3e-2);
        P.eq("alpha.instant", fcdsp::alphaFromTau(0.0f, 48000.0f) == 0.0f && fcdsp::alphaFromTau(-1.0f, 48000.0f) == 0.0f
                                  && fcdsp::alphaFromTau(20.0f, 0.0f) == 0.0f
                                  && fcdsp::alphaFromTau(std::numeric_limits<float>::quiet_NaN(), 48000.0f) == 0.0f, 1);
    }

    // ---- 3. dB <-> linear --------------------------------------------------------------------------------------------------
    void dbProbe(Probe& P)
    {
        P.near("db.per_log2", static_cast<double>(fcdsp::kDbPerLog2), 20.0 * std::log10(2.0), 0.0, 1e-7);
        P.near("db.log2_per_db", static_cast<double>(fcdsp::kLog2PerDb), 1.0 / (20.0 * std::log10(2.0)), 0.0, 1e-7);
        double lin40 = 0, lin120 = 0, db120 = 0, ms = 0;
        for (double d = -120.0; d <= 120.0; d += 0.0371)
        {
            const float db = static_cast<float>(d);
            const float x = simd::lane<0>(fcdsp::linFromDb(simd::set1(db)));
            const float back = simd::lane<0>(fcdsp::dbFromLin(simd::set1(x)));
            db120 = std::max(db120, std::fabs(static_cast<double>(back) - db));
            const double lin = std::pow(10.0, static_cast<double>(db) / 20.0);
            const double e = std::fabs(static_cast<double>(x) / lin - 1.0);
            lin120 = std::max(lin120, e);
            if (std::fabs(d) <= 40.0)
                lin40 = std::max(lin40, e);
            // power: dbFromMs(x^2) is dbFromLin(x) in dB
            const float msDb = simd::lane<0>(fcdsp::dbFromMs(simd::set1(static_cast<float>(lin * lin))));
            ms = std::max(ms, std::fabs(static_cast<double>(msDb) - static_cast<double>(db)));
        }
        double rt = 0;
        for (double x = 1e-6; x <= 1e6; x *= 1.013)
        {
            const float xf = static_cast<float>(x);
            const float back = simd::lane<0>(fcdsp::linFromDb(fcdsp::dbFromLin(simd::set1(xf))));
            rt = std::max(rt, std::fabs(static_cast<double>(back) / static_cast<double>(xf) - 1.0));
        }
        std::printf("NOTE     dB: linFromDb relative error %.3g (|dB| <= 40), %.3g (|dB| <= 120); dB round trip %.3g dB; "
                    "dbFromMs %.3g dB; lin round trip %.3g\n", lin40, lin120, db120, ms, rt);
        P.le("db.lin_from_db.max_rel_err.40db", lin40, 1e-6);
        P.le("db.lin_from_db.max_rel_err.120db", lin120, 2e-6);
        P.le("db.round_trip.max_abs_err_db", db120, 2e-5);
        P.le("db.from_ms.max_abs_err_db", ms, 2e-5);
        P.le("db.lin_round_trip.max_rel_err", rt, 2e-6);
    }

    // ---- 4. Smoother4 ------------------------------------------------------------------------------------------------------
    // Runs a smoother from `from` to `to` until every lane has landed; returns the number of ticks (or -1).
    struct LandingRun
    {
        int64_t ticks = -1;
        float maxJump[4] = { 0, 0, 0, 0 };        // the step with which each lane landed
        bool stays = false;                       // landed lanes stay exactly on target afterwards
    };

    LandingRun land(float fs, f32x4 from, f32x4 to, int64_t limit)
    {
        fcdsp::Smoother4 s{};
        s.prepare(fs);
        s.setTarget(from);
        s.snap();
        s.setTarget(to);
        const Lanes tgt(to);
        LandingRun r;
        bool landed[4] = { false, false, false, false };
        Lanes prev(s.cur);
        for (int64_t n = 1; n <= limit; ++n)
        {
            const Lanes c(s.tick());
            bool all = true;
            for (std::size_t k = 0; k < 4; ++k)
            {
                if (!landed[k] && bitsOf(c.v[k]) == bitsOf(tgt.v[k]))
                {
                    landed[k] = true;
                    r.maxJump[k] = std::fabs(c.v[k] - prev.v[k]);
                }
                all = all && landed[k];
            }
            prev = c;
            if (all)
            {
                r.ticks = n;
                bool stays = true;
                for (int i = 0; i < 1000; ++i)
                {
                    const Lanes after(s.tick());
                    for (std::size_t k = 0; k < 4; ++k)
                        stays = stays && bitsOf(after.v[k]) == bitsOf(tgt.v[k]);
                }
                r.stays = stays;
                return r;
            }
        }
        return r;
    }

    void smootherProbe(Probe& P)
    {
        // tau: the step response crosses 1 - 1/e at tau * fs samples
        {
            double worst = 0;
            for (const float fs : { 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f })
            {
                fcdsp::Smoother4 s{};
                s.prepare(fs);
                s.setTarget(simd::set1(0.0f));
                s.snap();
                s.setTarget(simd::set1(1.0f));
                const double want = 0.02 * static_cast<double>(fs), level = 1.0 - std::exp(-1.0);
                double prevV = 0;
                for (int n = 1; n < 100000; ++n)
                {
                    const double v = static_cast<double>(simd::lane<0>(s.tick()));
                    if (v >= level)
                    {
                        const double crossing = (n - 1) + (level - prevV) / (v - prevV);   // linear interpolation
                        worst = std::max(worst, std::fabs(crossing - want));
                        break;
                    }
                    prevV = v;
                }
            }
            P.le("smoother.tau.max_err_samples", worst, 1.5);
        }
        // Exact landing (K2 #20) at every sample rate, on dB-, slope- and amount-like targets. Without the stall test the
        // one-pole parks where (1 - a)|cur - tgt| < ulp/2 and never lands (1.8e-3 dB short of -40 dB at 48 kHz).
        {
            const struct { const char* name; f32x4 from, to; } cases[] = {
                { "db", vec(0.0f, -18.0f, 60.0f, -40.0f), vec(-40.0f, -60.0f, 12.0f, -39.0f) },
                { "slope", vec(0.0f, 0.75f, 0.9f, 1.5f), vec(0.75f, 0.5f, 0.95f, 0.0f) },
                { "amount", vec(0.0f, 1.0f, 0.5f, 0.3f), vec(1.0f, 0.0f, 0.500001f, 0.3000003f) },
                { "wide", vec(-120.0f, 120.0f, 127.0f, -127.0f), vec(120.0f, -120.0f, -127.0f, 126.9f) },
            };
            int notLanded = 0, notStaying = 0, jumpOverBound = 0;
            int64_t slowest = 0;
            double worstJumpDb = 0;
            for (const float fs : { 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f, 384000.0f })
            {
                const float a = fcdsp::alphaFromTau(20.0f, fs);
                const int64_t limit = static_cast<int64_t>(30.0 * 0.02 * static_cast<double>(fs));    // 30 tau
                for (const auto& c : cases)
                {
                    const LandingRun r = land(fs, c.from, c.to, limit);
                    notLanded += r.ticks < 0;
                    notStaying += r.ticks >= 0 && !r.stays;
                    slowest = std::max(slowest, r.ticks);
                    const Lanes tgt(c.to);
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        // bound: max(eps, ulp(|tgt| + jump) / (2 (1 - a))), 25 % slack for the value's binade
                        const float mag = std::fabs(tgt.v[k]) + r.maxJump[k];
                        const float ulp = fromBits(bitsOf(mag) + 1u) - mag;
                        const double stall = static_cast<double>(ulp) / (2.0 * (1.0 - static_cast<double>(a)));
                        const double bound = std::max(1e-5, stall);
                        jumpOverBound += static_cast<double>(r.maxJump[k]) > 1.25 * bound;
                        if (std::string(c.name) == "db")
                            worstJumpDb = std::max(worstJumpDb, static_cast<double>(r.maxJump[k]));
                    }
                }
            }
            if (notLanded == 0)
                std::printf("NOTE     Smoother4: every lane landed within %lld ticks (%.1f tau at 384 kHz); largest landing "
                            "step on dB targets %.3g dB\n", static_cast<long long>(slowest),
                            static_cast<double>(slowest) / (0.02 * 384000.0), worstJumpDb);
            P.eq("smoother.landing.not_landed", notLanded, 0);
            P.eq("smoother.landing.not_staying", notStaying, 0);
            P.eq("smoother.landing.step_over_bound", jumpOverBound, 0);
            P.le("smoother.landing.max_step_db", worstJumpDb, 0.02);
        }
        // Lanes are independent: each lane of a 4-lane run equals a run with that lane alone moving.
        {
            fcdsp::Smoother4 all{}, one[4] = {};
            const f32x4 from = vec(0.0f, -20.0f, 1.0f, 60.0f), to = vec(-30.0f, 6.0f, 0.25f, 10.0f);
            const Lanes f(from), t(to);
            all.prepare(48000.0f);
            all.setTarget(from);
            all.snap();
            all.setTarget(to);
            for (std::size_t k = 0; k < 4; ++k)
            {
                one[k].prepare(48000.0f);
                one[k].setTarget(simd::set1(f.v[k]));
                one[k].snap();
                one[k].setTarget(simd::set1(t.v[k]));
            }
            int mismatches = 0;
            for (int n = 0; n < 20000; ++n)
            {
                const Lanes a(all.tick());
                for (std::size_t k = 0; k < 4; ++k)
                    mismatches += bitsOf(a.v[k]) != bitsOf(simd::lane<0>(one[k].tick()));
            }
            P.eq("smoother.lanes_independent.mismatches", mismatches, 0);
        }
        // snap, a retarget mid-glide (continuous: the first step is (1 - a) of the distance), an instant pole
        {
            fcdsp::Smoother4 s{};
            s.prepare(48000.0f);
            s.setTarget(simd::set1(-12.0f));
            s.snap();
            P.eq("smoother.snap", bitsOf(simd::lane<0>(s.cur)), bitsOf(-12.0f));
            s.setTarget(simd::set1(0.0f));
            for (int n = 0; n < 480; ++n)
                (void) s.tick();
            const float mid = simd::lane<0>(s.cur);
            s.setTarget(simd::set1(-24.0f));
            const float next = simd::lane<0>(s.tick());
            const double a = static_cast<double>(fcdsp::alphaFromTau(20.0f, 48000.0f));
            P.near("smoother.retarget.first_step", static_cast<double>(next - mid),
                   (1.0 - a) * (-24.0 - static_cast<double>(mid)), 1e-5);
            fcdsp::Smoother4 z{};
            z.prepare(48000.0f, 0.0f);
            z.setTarget(simd::set1(3.0f));
            P.eq("smoother.instant", bitsOf(simd::lane<0>(z.tick())), bitsOf(3.0f));
        }
    }

    // ---- 5. LinearRamp -----------------------------------------------------------------------------------------------------
    void rampProbe(Probe& P)
    {
        int badLength = 0, notExact = 0, nonMonotone = 0;
        for (const float fs : { 22050.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f })
            for (const float ms : { 20.0f, 5.0f, 1.0f })
            {
                const double samples = static_cast<double>(ms) * static_cast<double>(fs) / 1000.0;
                for (const float to : { 1.0f, 0.0f })
                {
                    fcdsp::LinearRamp r{};
                    r.prepare(fs, ms);
                    r.cur = r.tgt = 1.0f - to;
                    r.setTarget(to);
                    int n = 0;
                    float prev = r.cur;
                    while (r.moving() && n < 1000000)
                    {
                        const float v = r.tick();
                        nonMonotone += to > prev ? v < prev : v > prev;
                        prev = v;
                        ++n;
                    }
                    badLength += std::fabs(n - samples) > 1.0;
                    notExact += bitsOf(r.cur) != bitsOf(to) || r.tick() != to || r.moving();
                }
            }
        P.eq("ramp.length.off_by_more_than_one", badLength, 0);
        P.eq("ramp.lands_exactly.failures", notExact, 0);
        P.eq("ramp.nonmonotone", nonMonotone, 0);
        {
            fcdsp::LinearRamp r{};
            r.prepare(48000.0f);
            r.setTarget(1.0f);
            for (int n = 0; n < 480; ++n)
                (void) r.tick();
            const float half = r.cur;
            r.setTarget(0.0f);
            int back = 0;
            while (r.moving() && back < 100000)
            {
                (void) r.tick();
                ++back;
            }
            P.near("ramp.reversal.half", static_cast<double>(half), 0.5, 1e-5);
            P.eq("ramp.reversal.returns_exactly", bitsOf(r.cur) == 0u && std::abs(back - 480) <= 1, 1);
            r.setTarget(0.5f);
            while (r.moving())
                (void) r.tick();
            P.eq("ramp.intermediate_target", bitsOf(r.cur), bitsOf(0.5f));
            fcdsp::LinearRamp z{};
            z.prepare(48000.0f, 0.0f);
            z.setTarget(1.0f);
            P.eq("ramp.instant", z.tick() == 1.0f && !z.moving(), 1);
            fcdsp::LinearRamp idle{};
            P.eq("ramp.idle", idle.tick() == 0.0f && !idle.moving(), 1);
        }
    }

    // ---- 6. ControlTicker --------------------------------------------------------------------------------------------------
    void tickerProbe(Probe& P)
    {
        P.eq("ticker.samples", fcdsp::kTickSamples, 16);
        // Block-size invariance: an engine calls advance(start + n) for every sample n of every block; the ticks depend
        // on the absolute index only, whatever the block partition.
        std::vector<uint64_t> reference;
        int partitionsDiffer = 0;
        for (const uint64_t start : { uint64_t{0}, uint64_t{1000}, uint64_t{48000} * 3600 + 7 })
        {
            bool first = true;
            for (const int bs : { 1, 17, 64, 128, 512, 4096 })
            {
                fcdsp::ControlTicker t{};
                std::vector<uint64_t> ticks;
                for (uint64_t block = start; block < start + 20000; block += static_cast<uint64_t>(bs))
                    for (uint64_t i = block; i < std::min(block + static_cast<uint64_t>(bs), start + 20000); ++i)
                        if (t.advance(i))
                            ticks.push_back(i);
                if (first)
                {
                    reference = ticks;
                    first = false;
                    bool aligned = ticks.front() == start;
                    for (std::size_t k = 1; k < ticks.size(); ++k)
                        aligned = aligned && ticks[k] % 16 == 0 && ticks[k] - ticks[k - 1] <= 16;
                    P.eq("ticker.absolute_multiples.start_" + std::to_string(start % 16), aligned ? 1 : 0, 1);
                }
                else
                    partitionsDiffer += ticks != reference;
            }
        }
        P.eq("ticker.block_size_invariance.mismatches", partitionsDiffer, 0);
        fcdsp::ControlTicker t{};
        const bool a = t.advance(5), b = t.advance(6), c = t.advance(40), d = t.advance(47), e = t.advance(48);
        P.eq("ticker.first_call_and_skips", a && !b && c && !d && e, 1);
        t = fcdsp::ControlTicker{};
        P.eq("ticker.reset", t.advance(3) ? 1 : 0, 1);
    }

    // ---- 7. sanitize -------------------------------------------------------------------------------------------------------
    void sanitizeProbe(Probe& P)
    {
        const struct { uint32_t in, out; } cases[] = {
            { 0x7fc00000u, 0u }, { 0xffc00001u, 0u }, { 0x7fa00000u, 0u }, { 0x7f800000u, 0u }, { 0xff800000u, 0u },
            { bitsOf(1e6f), bitsOf(1e6f) }, { bitsOf(-1e6f), bitsOf(-1e6f) },
            { bitsOf(1e6f) + 1u, bitsOf(1e6f) }, { bitsOf(-2e6f), bitsOf(-1e6f) }, { bitsOf(3e38f), bitsOf(1e6f) },
            { 0x7f7fffffu, bitsOf(1e6f) }, { 0xff7fffffu, bitsOf(-1e6f) },
            { 0x00000001u, 0x00000001u }, { 0x807fffffu, 0x807fffffu }, { 0u, 0u }, { 0x80000000u, 0x80000000u },
            { bitsOf(0.5f), bitsOf(0.5f) }, { bitsOf(-999999.94f), bitsOf(-999999.94f) },
        };
        std::vector<float> in, out;
        int wantCount = 0;
        for (const auto& c : cases)
        {
            in.push_back(fromBits(c.in));
            wantCount += c.in != c.out;
        }
        out.assign(in.size(), 1.0f);
        const int count = fcdsp::sanitize(in.data(), out.data(), static_cast<int>(in.size()));
        int wrong = 0;
        for (std::size_t i = 0; i < in.size(); ++i)
            wrong += bitsOf(out[i]) != cases[i].out;
        P.eq("sanitize.edges.wrong", wrong, 0);
        P.eq("sanitize.edges.count", count, wantCount);
        std::vector<float> same = in;
        const int countInPlace = fcdsp::sanitize(same.data(), same.data(), static_cast<int>(same.size()));
        P.eq("sanitize.in_place", countInPlace == count && std::equal(same.begin(), same.end(), out.begin(),
                                  [](float x, float y) { return bitsOf(x) == bitsOf(y); }), 1);
        float untouched = 7.0f;
        P.eq("sanitize.empty", fcdsp::sanitize(&untouched, &untouched, 0) == 0 && untouched == 7.0f, 1);

        // random bit patterns: finite, |out| <= 1e6, unchanged when already legal, the count is the number changed
        fcmp::probe::sig::Pcg32 rng(0x5a17u, 9u);
        std::vector<float> r(1u << 20), o(r.size());
        for (float& x : r)
            x = fromBits(rng.next());
        const int n = fcdsp::sanitize(r.data(), o.data(), static_cast<int>(r.size()));
        int changed = 0, illegal = 0, wrongKeep = 0;
        for (std::size_t i = 0; i < r.size(); ++i)
        {
            const bool legal = std::isfinite(r[i]) && std::fabs(r[i]) <= 1e6f;
            changed += bitsOf(o[i]) != bitsOf(r[i]);
            illegal += !(std::isfinite(o[i]) && std::fabs(o[i]) <= 1e6f);
            wrongKeep += legal && bitsOf(o[i]) != bitsOf(r[i]);
        }
        P.eq("sanitize.random.count", n, changed);
        P.eq("sanitize.random.illegal_out", illegal, 0);
        P.eq("sanitize.random.legal_changed", wrongKeep, 0);
    }

    // ---- 8. ScopedFtz ------------------------------------------------------------------------------------------------------
    void ftzProbe(Probe& P)
    {
        const uint64_t harness = readFpControl();                 // ProbeMain's own ScopedFtz is open
        writeFpControl(harness & ~kFtzBits);
        const uint64_t before = readFpControl();
        const bool offSurvives = tinyProductSurvives();
        bool onFlushes = false, nestedFlushes = false, innerRestored = false;
        {
            const fcdsp::ScopedFtz ftz;
            onFlushes = !tinyProductSurvives() && (readFpControl() & kFtzBits) == kFtzBits;
            {
                const fcdsp::ScopedFtz inner;
                nestedFlushes = !tinyProductSurvives();
            }
            innerRestored = (readFpControl() & kFtzBits) == kFtzBits && !tinyProductSurvives();
        }
        const uint64_t after = readFpControl();
        const bool offAgain = tinyProductSurvives();
        writeFpControl(harness);
        P.eq("ftz.off_keeps_denormals", offSurvives ? 1 : 0, 1);
        P.eq("ftz.scope_flushes", onFlushes ? 1 : 0, 1);
        P.eq("ftz.nested", nestedFlushes && innerRestored ? 1 : 0, 1);
        P.eq("ftz.restores_register", after == before ? 1 : 0, 1);
        P.eq("ftz.off_again_after_scope", offAgain ? 1 : 0, 1);
    }
} // namespace

FCMP_PROBE(dsp, units)
{
    timeLawProbe(P);
    alphaProbe(P);
    dbProbe(P);
    smootherProbe(P);
    rampProbe(P);
    tickerProbe(P);
    sanitizeProbe(P);
    ftzProbe(P);
    return P.finish();
}
