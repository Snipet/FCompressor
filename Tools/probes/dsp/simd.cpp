// FCMP_PROBE layer=dsp name=simd scope=global timeout=60
//
// dsp.simd (F1, S1; 03 §3.4; E §0.9, §3.2, §9.2 row 1; K2 #14, #15): the fcdsp::simd op set and FastMath.
//
//   1. ops      every op on an edge set (+-0, +-inf, NaNs, denormals under FTZ, FLT_MIN/MAX, rounding cases) and on
//               seeded random normals: IEEE semantics pinned as spec rows (fused fma and its operand order, correct
//               rounding of div/sqrt, exact floor/abs/neg, FTZ); the min/max NaN and signed-zero policy pinned per
//               backend (Simd.h: the only op results the backends do not share); xarch.simd.ops.hash over the rest.
//   2. estimate arm64 only: the portable FRSQRTE table function and FRSQRTS composition (the x86 bodies) against the
//               instructions, FZ on and off, so the SSE code runs on a Mac without Rosetta (K2 #15).
//   3. log2     every mantissa of exponents -40..40 (E §9.2 row 1). Error against libm in double (the reference is
//               per mantissa, since log2(m 2^e) = e + log2(m)); exact powers of two; monotone; scalar == lanes.
//   4. exp2     every float with |x| in [2^-24, 1/2] (the reduced argument f's whole range) against a running
//               product of exact powers; every float in [1/2, 126] checked to be exp2(f) scaled by 2^n bit for bit
//               (so the relative error there is f's); a strided pass below 2^-24; clamping; results always normal.
//   5. tanh, logCosh, tanPi, sinPi, cosPi over their domains (strided, dense around the branch points) against libm
//               in double: errors, exact symmetries and special values, monotonicity.
// Spec bounds are the measured errors with headroom (the "targets fixed by the F1 spike", 03 §3.4); the E §0.9 bounds
// (log2 2.3e-5 dB, exp2 7.4e-4 dB) and FastMath.h's refit targets (4e-7, 1e-5) are asserted as well.
//
// Golden rows: xarch.simd.{ops,log2,exp2,tanh,logcosh,tanpi,sinpi,cospi}.hash, FNV-1a over every result of the
// sweeps (NaN canonicalised: payloads are unspecified). They must be identical on arm64 and x86_64 (03 §3.3) and in
// every build configuration: the F1 determinism spike compares build-agent's (RelWithDebInfo) with build-lead's
// (Release + LTO). tanPi/sinPi/cosPi are out of line in fcdsp (FastMath.cpp), so their rows come from the LTO-compiled
// archive in the lead build. --quick runs reduced grids and emits no golden rows.
#include "ProbeRegistry.h"
#include "Signals.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Simd.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace
{
    namespace simd = fcdsp::simd;
    using simd::f32x4;
    using funkgui::test::Probe;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kFltMin = std::numeric_limits<float>::min();
    constexpr float kFltMax = std::numeric_limits<float>::max();
    constexpr double kPi = std::numbers::pi;

    constexpr uint32_t bitsOf(float f) noexcept { return std::bit_cast<uint32_t>(f); }
    constexpr float fromBits(uint32_t b) noexcept { return std::bit_cast<float>(b); }
    constexpr bool isNanBits(uint32_t b) noexcept { return (b & 0x7fffffffu) > 0x7f800000u; }
    bool isNan(float f) noexcept { return isNanBits(bitsOf(f)); }

    // FNV-1a over 32-bit words in four streams (word i of a run goes to stream i mod 4, so a vector's lanes are the four
    // streams), 32-bit prime per stream, the four states folded by 64-bit FNV-1a at the end. Every NaN hashes as
    // 0x7fc00000. The arm64 path updates the four streams with one vector multiply; the portable path is the same
    // integer arithmetic, so the value is the same on every arch.
    class Hash
    {
    public:
        void add(float v) noexcept
        {
            const uint32_t b = bitsOf(v);
            s_[n_] = (s_[n_] ^ (isNanBits(b) ? 0x7fc00000u : b)) * kPrime;
            n_ = (n_ + 1u) & 3u;
        }
        void add(f32x4 v) noexcept
        {
            if (n_ == 0)
            {
#if defined(FCDSP_SIMD_NEON)
                const uint32x4_t b = vbslq_u32(vceqq_f32(v, v), vreinterpretq_u32_f32(v), vdupq_n_u32(0x7fc00000u));
                vst1q_u32(s_, vmulq_u32(veorq_u32(vld1q_u32(s_), b), vdupq_n_u32(kPrime)));
                return;
#endif
            }
            alignas(16) float t[4];
            simd::store(t, v);
            for (const float x : t)
                add(x);
        }
        uint64_t value() const noexcept { return funkgui::test::fnv1a(s_, sizeof s_); }

    private:
        static constexpr uint32_t kOffset = 2166136261u, kPrime = 16777619u;
        alignas(16) uint32_t s_[4] = { kOffset, kOffset, kOffset, kOffset };
        unsigned n_ = 0;
    };

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

    float mask01(simd::m32x4 m) noexcept           // a mask as 1/0 per lane, read from lane 0 (x86 masks are NaNs)
    {
        return simd::lane<0>(simd::sel(m, simd::set1(1.0f), simd::set1(0.0f)));
    }

    // The value, hidden from the optimiser: clang folds constant FP expressions in the default FP environment (no FTZ),
    // so a flush-to-zero check on constants would test the compiler, not the hardware mode.
    float opaque(float x) noexcept
    {
        volatile float v = x;
        return v;
    }

    double ulpOf(float y) noexcept                  // the gap above |y| (y finite)
    {
        const float a = std::fabs(y);
        return static_cast<double>(fromBits(bitsOf(a) + 1u)) - static_cast<double>(a);
    }

    struct Timer                                    // wall and process CPU time per section (the budget is CPU, 03 §3.9)
    {
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        std::clock_t c0 = std::clock();
        void note(const char* what)
        {
            const auto t1 = std::chrono::steady_clock::now();
            const std::clock_t c1 = std::clock();
            std::printf("NOTE     dsp.simd %-10s %6.0f ms wall %6.0f ms cpu\n", what,
                        std::chrono::duration<double, std::milli>(t1 - t0).count(),
                        1000.0 * static_cast<double>(c1 - c0) / CLOCKS_PER_SEC);
            t0 = t1;
            c0 = c1;
        }
    };

    // Keeps the maximum of a metric and where it occurred.
    struct Max
    {
        double v = 0, at = 0;
        void put(double e, double x) noexcept
        {
            if (e > v)
            {
                v = e;
                at = x;
            }
        }
    };

    // ---- 1. ops ---------------------------------------------------------------------------------------------------------
    // Laundered through opaque(): every op must run on the hardware, in the FP mode, never be folded by the compiler.
    std::vector<float> edgeValues()
    {
        std::vector<float> v = { fromBits(0x00000000u), fromBits(0x80000000u), kInf, -kInf,
                                 fromBits(0x7fc00000u), fromBits(0xffc00001u), fromBits(0x7fa00000u),  // NaNs, sNaN
                                 fromBits(0x00000001u), fromBits(0x80000001u), fromBits(0x007fffffu),  // denormals
                                 kFltMin, -kFltMin, 0x1p-125f, 1.0f, -1.0f, 0.5f, -0.5f, 1.5f, -1.5f, -2.0f, 2.5f,
                                 3.0f, 7.0f, 0.1f, -0.1f, 1e-20f, -1e20f, 1e6f, -1e6f, kFltMax, -kFltMax,
                                 std::numbers::pi_v<float>, fromBits(0x3f7fffffu), fromBits(0x3f800001u), -8388609.0f,
                                 8388608.0f };
        for (float& x : v)
            x = opaque(x);
        return v;
    }

    // Random normals with exponents in [-30, 30]: far from overflow and from the underflow threshold.
    std::vector<float> randomNormals(std::size_t n)
    {
        fcmp::probe::sig::Pcg32 rng(0x5eed51bdu, 1u);
        std::vector<float> v(n);
        for (float& x : v)
        {
            const uint32_t sign = rng.next() & 0x80000000u;
            const uint32_t e = 97u + rng.bounded(61u);                                  // biased 97..157
            x = opaque(fromBits(sign | (e << 23) | (rng.next() & 0x007fffffu)));
        }
        return v;
    }

    // x86 flushes a tiny result after rounding, arm64 before (FZ; Arm ARM FPRound): an exact result just below
    // FLT_MIN that rounds up to it is FLT_MIN on one and 0 on the other. Results within 2^-22 of FLT_MIN (from a
    // product or quotient that is not exact) are left out of the hash; the inputs decide, so both arches skip the same.
    bool nearUnderflow(double r) noexcept
    {
        const double a = std::fabs(r), m = static_cast<double>(kFltMin);
        return a >= m * (1.0 - 0x1p-22) && a <= m * (1.0 + 0x1p-22);
    }

    void opsProbe(Probe& P, bool emitGolden)
    {
        const std::vector<float> E = edgeValues();
        const std::vector<float> R = randomNormals(P.quick() ? 256 : 4096);
        Hash h;
        int skippedMinMax = 0, skippedUnderflow = 0;

        const auto un = [&](auto op)
        {
            for (const float a : E)
                h.add(op(simd::set1(a)));
            for (std::size_t i = 0; i + 4 <= R.size(); i += 4)
                h.add(op(simd::load(&R[i])));
        };
        const auto bin = [&](auto op, auto skip)
        {
            for (const float a : E)
                for (const float b : E)
                {
                    if (skip(a, b))
                        continue;
                    h.add(op(simd::set1(a), simd::set1(b)));
                }
            for (std::size_t i = 0; i + 8 <= R.size(); i += 8)
                h.add(op(simd::load(&R[i]), simd::load(&R[i + 4])));
        };
        const auto none = [](float, float) { return false; };

        // memory, construction and exact ops
        un([](f32x4 v) { alignas(16) float t[4]; simd::store(t, v); return simd::load(t); });
        for (const float a : E)
            h.add(simd::set1(a));
        un([](f32x4 v) { return simd::abs(v); });
        un([](f32x4 v) { return simd::neg(v); });
        un([](f32x4 v) { return simd::sqrt(v); });
        un([](f32x4 v) { return simd::floor(v); });
        un([](f32x4 v) { return simd::rsqrte(v); });
        un([](f32x4 v) { return simd::rsqrte(simd::abs(v)); });
        // arithmetic
        bin([](f32x4 a, f32x4 b) { return simd::add(a, b); }, none);
        bin([](f32x4 a, f32x4 b) { return simd::sub(a, b); }, none);
        const auto skipIf = [&](bool s)
        {
            skippedUnderflow += s ? 1 : 0;
            return s;
        };
        bin([](f32x4 a, f32x4 b) { return simd::mul(a, b); },
            [&](float a, float b) { return skipIf(nearUnderflow(static_cast<double>(a) * b)); });
        bin([](f32x4 a, f32x4 b) { return simd::div(a, b); },
            [&](float a, float b) { return skipIf(b != 0.0f && nearUnderflow(static_cast<double>(a) / b)); });
        bin([](f32x4 a, f32x4 b) { return simd::rsqrts(a, b); }, none);
        const auto minMaxSkip = [&](float a, float b)
        {
            const bool s = isNan(a) || isNan(b) || (a == 0.0f && b == 0.0f && bitsOf(a) != bitsOf(b));
            skippedMinMax += s ? 1 : 0;
            return s;
        };
        bin([](f32x4 a, f32x4 b) { return simd::min(a, b); }, minMaxSkip);
        bin([](f32x4 a, f32x4 b) { return simd::max(a, b); }, minMaxSkip);
        // comparisons and masks, read through sel
        const auto one = simd::set1(1.0f), zero = simd::set1(0.0f);
        bin([&](f32x4 a, f32x4 b) { return simd::sel(simd::gt(a, b), one, zero); }, none);
        bin([&](f32x4 a, f32x4 b) { return simd::sel(simd::ge(a, b), one, zero); }, none);
        bin([&](f32x4 a, f32x4 b) { return simd::sel(simd::gt(a, b), a, b); }, none);
        bin([&](f32x4 a, f32x4 b) { return simd::sel(simd::band(simd::ge(a, zero), simd::gt(b, a)), a, b); }, none);
        bin([&](f32x4 a, f32x4 b) { return simd::sel(simd::bor(simd::gt(a, one), simd::ge(zero, b)), b, a); }, none);
        // fused multiply-add, all edge triples and random ones
        for (int which = 0; which < 2; ++which)
        {
            for (const float a : E)
                for (const float b : E)
                    for (const float c : E)
                    {
                        const double bc = static_cast<double>(b) * c;
                        if (bc != 0.0 && nearUnderflow(static_cast<double>(a) + (which == 0 ? bc : -bc)))
                        {
                            ++skippedUnderflow;
                            continue;
                        }
                        const f32x4 va = simd::set1(a), vb = simd::set1(b), vc = simd::set1(c);
                        h.add(which == 0 ? simd::fma(va, vb, vc) : simd::fms(va, vb, vc));
                    }
            for (std::size_t i = 0; i + 12 <= R.size(); i += 12)
            {
                const f32x4 va = simd::load(&R[i]), vb = simd::load(&R[i + 4]), vc = simd::load(&R[i + 8]);
                h.add(which == 0 ? simd::fma(va, vb, vc) : simd::fms(va, vb, vc));
            }
        }
        // lanes
        for (std::size_t i = 0; i + 4 <= R.size(); i += 4)
        {
            const f32x4 v = simd::load(&R[i]);
            h.add(simd::lane<0>(v));
            h.add(simd::lane<1>(v));
            h.add(simd::lane<2>(v));
            h.add(simd::lane<3>(v));
            const float s = R[(i + 7) % R.size()];
            h.add(simd::withLane<0>(v, s));
            h.add(simd::withLane<1>(v, s));
            h.add(simd::withLane<2>(v, s));
            h.add(simd::withLane<3>(v, s));
        }
        std::printf("NOTE     dsp.simd ops hash leaves out %d min/max pairs (NaN or +-0 pairs, pinned below) and %d "
                    "results at the underflow threshold (FTZ tininess differs)\n", skippedMinMax, skippedUnderflow);
        if (emitGolden)
            P.hash("xarch.simd.ops.hash", h.value());

        // IEEE semantics: identical on both backends.
        const auto s1 = [](float x) { return simd::set1(x); };
        const float b = 1.0f + 0x1p-12f;                                // b*b = 1 + 2^-11 + 2^-24: the 2^-24 needs fusing
        P.eq("ops.fma.fused", bitsOf(simd::lane<0>(simd::fma(s1(-(1.0f + 0x1p-11f)), s1(b), s1(b)))), bitsOf(0x1p-24f));
        P.eq("ops.fms.fused", bitsOf(simd::lane<0>(simd::fms(s1(1.0f + 0x1p-11f), s1(b), s1(b)))), bitsOf(-0x1p-24f));
        P.eq("ops.fma.operand_order", bitsOf(simd::lane<0>(simd::fma(s1(1), s1(2), s1(3)))), bitsOf(7.0f));
        P.eq("ops.fms.operand_order", bitsOf(simd::lane<0>(simd::fms(s1(10), s1(2), s1(3)))), bitsOf(4.0f));
        P.eq("ops.div.correctly_rounded", bitsOf(simd::lane<0>(simd::div(s1(1), s1(3)))), 0x3eaaaaabu);
        P.eq("ops.sqrt.correctly_rounded", bitsOf(simd::lane<0>(simd::sqrt(s1(2)))), 0x3fb504f3u);
        P.eq("ops.floor.values",
             bitsOf(simd::lane<0>(simd::floor(s1(-0.5f)))) == bitsOf(-1.0f)
                 && bitsOf(simd::lane<0>(simd::floor(s1(-0.0f)))) == 0x80000000u
                 && simd::lane<0>(simd::floor(s1(2.5f))) == 2.0f
                 && simd::lane<0>(simd::floor(s1(0.99999994f))) == 0.0f
                 && simd::lane<0>(simd::floor(s1(-8388609.0f))) == -8388609.0f
                 && simd::lane<0>(simd::floor(s1(1e30f))) == 1e30f,
             1);
        P.eq("ops.abs_neg.sign_bit_only",
             bitsOf(simd::lane<0>(simd::abs(s1(-0.0f)))) == 0u && bitsOf(simd::lane<0>(simd::neg(s1(0.0f)))) == 0x80000000u
                 && bitsOf(simd::lane<0>(simd::abs(s1(fromBits(0x80000001u))))) == 1u
                 && bitsOf(simd::lane<0>(simd::neg(s1(fromBits(0x00000001u))))) == 0x80000001u,
             1);
        const float qnan = fromBits(0x7fc00000u);
        P.eq("ops.compare.nan_false",
             mask01(simd::gt(s1(qnan), s1(0))) + mask01(simd::gt(s1(0), s1(qnan)))
                     + mask01(simd::ge(s1(qnan), s1(qnan))) == 0.0f,
             1);
        P.eq("ops.compare.signed_zero",
             mask01(simd::ge(s1(-0.0f), s1(0.0f))) == 1.0f && mask01(simd::gt(s1(0.0f), s1(-0.0f))) == 0.0f, 1);
        {
            const f32x4 t = vec(1, 2, 3, 4), f = vec(5, 6, 7, 8);
            const Lanes r(simd::sel(simd::gt(vec(1, 0, 1, 0), simd::set1(0.5f)), t, f));
            P.eq("ops.sel.per_lane", r.v[0] == 1 && r.v[1] == 6 && r.v[2] == 3 && r.v[3] == 8, 1);
            const Lanes w(simd::withLane<2>(t, 9.0f));
            P.eq("ops.lane.round_trip", simd::lane<0>(t) == 1 && simd::lane<1>(t) == 2 && simd::lane<2>(t) == 3
                                            && simd::lane<3>(t) == 4 && w.v[2] == 9 && w.v[0] == 1 && w.v[3] == 4, 1);
        }
        P.eq("ops.rsqrte.one", bitsOf(simd::lane<0>(simd::rsqrte(s1(1.0f)))), 0x3f7f8000u);   // FRSQRTE: 0.998046875
        P.eq("ops.rsqrte.two", bitsOf(simd::lane<0>(simd::rsqrte(s1(2.0f)))), 0x3f348000u);
        P.eq("ops.rsqrte.specials",
             bitsOf(simd::lane<0>(simd::rsqrte(s1(0.0f)))) == 0x7f800000u
                 && bitsOf(simd::lane<0>(simd::rsqrte(s1(-0.0f)))) == 0xff800000u
                 && isNan(simd::lane<0>(simd::rsqrte(s1(-1.0f)))) && simd::lane<0>(simd::rsqrte(s1(kInf))) == 0.0f
                 && bitsOf(simd::lane<0>(simd::rsqrte(s1(opaque(fromBits(0x00000001u)))))) == 0x7f800000u,   // FTZ
             1);
        P.eq("ops.rsqrts.values", simd::lane<0>(simd::rsqrts(s1(2.0f), s1(0.5f))) == 1.0f
                                      && simd::lane<0>(simd::rsqrts(s1(0.0f), s1(kInf))) == 1.5f
                                      && simd::lane<0>(simd::rsqrts(s1(-kInf), s1(-0.0f))) == 1.5f
                                      && simd::lane<0>(simd::rsqrts(s1(-kFltMax), s1(2.0f))) == kFltMax,   // (3 + 2 MAX)/2
             1);
        {
            // One Newton step from the 8-bit estimate: about 16 correct bits.
            double worst = 0;
            for (float x = 0.01f; x < 100.0f; x *= 1.01f)
            {
                const f32x4 v = simd::set1(x), r0 = simd::rsqrte(v);
                const f32x4 r1 = simd::mul(r0, simd::rsqrts(simd::mul(r0, v), r0));
                const double got = static_cast<double>(simd::lane<0>(r1));
                worst = std::max(worst, std::fabs(got * std::sqrt(static_cast<double>(x)) - 1.0));
            }
            P.le("ops.rsqrt_newton.max_rel_err", worst, 5e-5);
        }
        // FTZ (ProbeMain runs the body under ScopedFtz): a tiny result and a denormal input are flushed.
        P.eq("ops.ftz.tiny_result_flushed", bitsOf(simd::lane<0>(simd::mul(s1(opaque(kFltMin)), s1(0.5f)))), 0u);
        P.eq("ops.ftz.denormal_input_flushed",
             bitsOf(simd::lane<0>(simd::add(s1(opaque(fromBits(0x00000001u))), s1(0.0f)))), 0u);
        P.eq("ops.ftz.denormal_moves_untouched", bitsOf(simd::lane<0>(simd::set1(opaque(fromBits(0x00000001u))))), 1u);

        // min/max: the policy each backend has (Simd.h). The idiom fcdsp relies on is the same on both: the operand
        // that may be NaN goes second, so max(set1(floor), x) is NaN when x is.
        const auto nanOf = [](f32x4 v) { return isNan(simd::lane<0>(v)) ? 1 : 0; };
        const auto signOf = [](f32x4 v) { return (bitsOf(simd::lane<0>(v)) >> 31) != 0 ? 1 : 0; };
        P.eq("ops.minmax.nan_second_propagates", nanOf(simd::min(s1(1), s1(qnan))) + nanOf(simd::max(s1(1), s1(qnan))), 2);
#if defined(FCDSP_SIMD_NEON)
        constexpr int kNanFirst = 1, kMinNegPos = 1, kMinPosNeg = 1, kMaxNegPos = 0, kMaxPosNeg = 0;   // FMIN/FMAX
#else
        constexpr int kNanFirst = 0, kMinNegPos = 0, kMinPosNeg = 1, kMaxNegPos = 0, kMaxPosNeg = 1;   // minps/maxps
#endif
        P.eq("ops.minmax.nan_first_propagates", nanOf(simd::min(s1(qnan), s1(1))) + nanOf(simd::max(s1(qnan), s1(1))),
             2 * kNanFirst);
        P.eq("ops.min.neg0_pos0.sign", signOf(simd::min(s1(-0.0f), s1(0.0f))), kMinNegPos);
        P.eq("ops.min.pos0_neg0.sign", signOf(simd::min(s1(0.0f), s1(-0.0f))), kMinPosNeg);
        P.eq("ops.max.neg0_pos0.sign", signOf(simd::max(s1(-0.0f), s1(0.0f))), kMaxNegPos);
        P.eq("ops.max.pos0_neg0.sign", signOf(simd::max(s1(0.0f), s1(-0.0f))), kMaxPosNeg);
    }

    // ---- 2. the portable FRSQRTE / FRSQRTS (x86 bodies) against the arm64 instructions ----------------------------------
#if defined(FCDSP_SIMD_NEON)
    struct ScopedFz                                 // FPCR.FZ set or cleared for a scope (the probe body runs with it set)
    {
        explicit ScopedFz(bool on) noexcept
        {
            __asm__ volatile("mrs %0, fpcr" : "=r"(saved_) : : "memory");
            const uint64_t v = on ? (saved_ | (uint64_t{1} << 24)) : (saved_ & ~(uint64_t{1} << 24));
            __asm__ volatile("msr fpcr, %0" : : "r"(v) : "memory");
        }
        ~ScopedFz() { __asm__ volatile("msr fpcr, %0" : : "r"(saved_) : "memory"); }
        ScopedFz(const ScopedFz&) = delete;
        ScopedFz& operator=(const ScopedFz&) = delete;
        uint64_t saved_ = 0;
    };

    uint32_t hwRsqrte(uint32_t x) noexcept
    {
        return vgetq_lane_u32(vreinterpretq_u32_f32(vrsqrteq_f32(vreinterpretq_f32_u32(vdupq_n_u32(x)))), 0);
    }

    bool sameResult(uint32_t a, uint32_t b) noexcept { return a == b || (isNanBits(a) && isNanBits(b)); }

    void estimateProbe(Probe& P)
    {
        fcmp::probe::sig::Pcg32 rng(0xfe57u, 3u);
        for (int fz = 1; fz >= 0; --fz)
        {
            const ScopedFz mode(fz != 0);
            int64_t n = 0, bad = 0, nanPayload = 0;
            const auto check = [&](uint32_t x)
            {
                const uint32_t hw = hwRsqrte(x), emu = fcdsp::simd::detail::frsqrteBits(x, fz != 0);
                ++n;
                if (hw != emu)
                {
                    if (sameResult(hw, emu))
                        ++nanPayload;
                    else if (++bad <= 5)
                        std::printf("NOTE     frsqrte(0x%08x) fz=%d: hardware 0x%08x, portable 0x%08x\n", x, fz, hw, emu);
                }
            };
            // every exponent x every table-selecting fraction x low-bit patterns, both signs
            for (uint32_t e = 0; e < 256; ++e)
                for (uint32_t t = 0; t < 128; ++t)
                    for (const uint32_t low : { 0x0000u, 0x8000u, 0xffffu, rng.next() & 0xffffu })
                        for (const uint32_t s : { 0u, 0x80000000u })
                            check(s | (e << 23) | (t << 16) | low);
            // every fraction of the denormal exponent and of the five exponents around 1.0 (HR's check)
            for (const uint32_t e : { 0u, 125u, 126u, 127u, 128u, 129u })
                for (uint32_t f = 0; f < (1u << 23); f += (P.quick() ? 97u : 1u))
                    check((e << 23) | f);
            const std::string k = fz != 0 ? "estimate.frsqrte.fz" : "estimate.frsqrte.nofz";
            P.eq(k + ".mismatches", bad, 0);
            P.ge(k + ".checked", static_cast<double>(n), P.quick() ? 3e5 : 5e7);
            if (nanPayload > 0)
                std::printf("NOTE     frsqrte fz=%d: %lld results differ only in the NaN payload\n", fz,
                            static_cast<long long>(nanPayload));
        }

        // FRSQRTS: edge pairs, random pairs, and the ranges where the composition switches formulas or could overflow.
        const std::vector<float> E = edgeValues();
        std::vector<float> ps, qs;
        for (const float a : E)
            for (const float b : E)
            {
                ps.push_back(a);
                qs.push_back(b);
            }
        for (int i = 0; i < (P.quick() ? 20000 : 2000000); ++i)
        {
            const uint32_t pa = rng.next(), qa = rng.next();
            const uint32_t kind = rng.bounded(4u);
            float p = fromBits(pa), q = fromBits(qa);
            if (kind == 1)                                                   // tiny p (formula switch at 2^-125)
                p = fromBits((pa & 0x80000000u) | (rng.bounded(4u) << 23) | (pa & 0x007fffffu));
            else if (kind == 2)                                              // |p q| near FLT_MAX: overflow of 3 - pq
            {
                p = fromBits((pa & 0x80000000u) | (254u << 23) | (pa & 0x007fffffu));
                q = fromBits((qa & 0x80000000u) | ((127u + rng.bounded(2u)) << 23) | (qa & 0x007fffffu));
            }
            else if (kind == 3)                                              // p q near 3: cancellation
            {
                q = fromBits((qa & 0x007fffffu) | (127u << 23));
                const float nudge = 1.0f + static_cast<float>(rng.bounded(64u)) * 0x1p-23f;
                p = static_cast<float>(3.0 / static_cast<double>(q)) * nudge;
            }
            ps.push_back(p);
            qs.push_back(q);
        }
        for (int fz = 1; fz >= 0; --fz)
        {
            const ScopedFz mode(fz != 0);
            int64_t bad = 0;
            for (std::size_t i = 0; i < ps.size(); ++i)
            {
                const f32x4 p = simd::set1(ps[i]), q = simd::set1(qs[i]);
                const uint32_t hw = bitsOf(simd::lane<0>(vrsqrtsq_f32(p, q)));
                const uint32_t emu = bitsOf(simd::lane<0>(fcdsp::simd::detail::rsqrtsPortable(p, q)));
                if (!sameResult(hw, emu) && ++bad <= 5)
                    std::printf("NOTE     frsqrts(%a, %a) fz=%d: hardware %a, portable %a\n", static_cast<double>(ps[i]),
                                static_cast<double>(qs[i]), fz, static_cast<double>(fromBits(hw)),
                                static_cast<double>(fromBits(emu)));
            }
            P.eq(fz != 0 ? "estimate.frsqrts.fz.mismatches" : "estimate.frsqrts.nofz.mismatches", bad, 0);
        }
    }
#endif

    // ---- 3. log2 ---------------------------------------------------------------------------------------------------------
    // Every value is evaluated, hashed and checked for monotonicity; the error against libm is taken on every value of
    // the core binades [1/2, 2) and on every 16th elsewhere, where only the final rounding differs (log2(m 2^e) =
    // e + log2(m): t and q(t) are the same for every e, only the last fused step k + t*q rounds at |result| ~ |e|).
    void log2Probe(Probe& P, bool emitGolden)
    {
        const int eLo = P.quick() ? -2 : -40, eHi = P.quick() ? 1 : 40;
        const uint32_t mStep = P.quick() ? 16u : 1u;
        constexpr uint32_t kBlock = 4096, kSample = 16;
        Hash h;
        Max err, errCore, excess;
        int64_t n = 0, laneMismatch = 0, nonMonotone = 0;
        std::vector<double> ref(kBlock);
        std::vector<float> in(kBlock), out(kBlock);
        std::vector<float> prevLast(static_cast<std::size_t>(eHi - eLo + 1), -kInf);
        uint32_t block = 0;
        for (uint32_t m0 = 0; m0 < (1u << 23); m0 += kBlock * mStep, ++block)
        {
            for (uint32_t j = 0; j < kBlock; ++j)
                ref[j] = std::log2(1.0 + static_cast<double>(m0 + j * mStep) * 0x1p-23);
            for (int e = eLo; e <= eHi; ++e)
            {
                const uint32_t eb = static_cast<uint32_t>(e + 127) << 23;
                for (uint32_t j = 0; j < kBlock; ++j)
                    in[j] = fromBits(eb | (m0 + j * mStep));
                for (uint32_t j = 0; j < kBlock; j += 4)
                {
                    const f32x4 r = fcdsp::log2(simd::load(&in[j]));
                    simd::store(&out[j], r);
                    h.add(r);
                }
                if ((block & 15u) == 0)
                    for (uint32_t j = 0; j < 64; ++j)
                        laneMismatch += bitsOf(fcdsp::log2(in[j])) != bitsOf(out[j]);
                float& last = prevLast[static_cast<std::size_t>(e - eLo)];
                nonMonotone += out[0] < last;
                for (uint32_t j = 1; j < kBlock; ++j)
                    nonMonotone += out[j] < out[j - 1];
                last = out[kBlock - 1];
                const bool core = e == -1 || e == 0;
                for (uint32_t j = 0; j < kBlock; j += core ? 1u : kSample)
                {
                    const double d = std::fabs(static_cast<double>(out[j]) - (e + ref[j]));
                    err.put(d, static_cast<double>(in[j]));
                    if (core)
                        errCore.put(d, static_cast<double>(in[j]));
                    excess.put(d - 0.5 * ulpOf(out[j]), static_cast<double>(in[j]));
                }
                n += kBlock;
            }
        }
        // exact powers of two, and the monotone step across each binade boundary
        int powMismatch = 0;
        for (int k = -126; k <= 127; ++k)
            powMismatch += fcdsp::log2(std::ldexp(1.0f, k)) != static_cast<float>(k);
        for (int e = eLo; e < eHi; ++e)
            nonMonotone += fcdsp::log2(fromBits(static_cast<uint32_t>(e + 128) << 23))
                           < fcdsp::log2(fromBits((static_cast<uint32_t>(e + 128) << 23) - 1u));
        std::printf("NOTE     log2: %lld values, max |err| %.3g at %a (core %.3g at %a; beyond the result's rounding %.3g "
                    "at %a)\n", static_cast<long long>(n), err.v, err.at, errCore.v, errCore.at, excess.v, excess.at);
        P.le("log2.core.max_abs_err", errCore.v, 1.3e-7);                     // |x| in [1/2, 2); refit target 4e-7
        P.le("log2.max_abs_err_db", err.v * 6.0205999132796239, 2.3e-5);      // E §0.9, exponents -40..40
        P.le("log2.max_err_beyond_final_rounding", excess.v, 1e-7);           // |err| <= 1/2 ulp(result) + 1e-7
        P.eq("log2.powers_of_two.mismatches", powMismatch, 0);
        P.eq("log2.nonmonotone", nonMonotone, 0);
        P.eq("log2.scalar_vs_lanes.mismatches", laneMismatch, 0);
        P.eq("log2.one_is_zero", bitsOf(fcdsp::log2(1.0f)), 0u);
        P.eq("log2.floored_zero", fcdsp::log2(0.0f) >= -127.0f && fcdsp::log2(0.0f) <= -126.0f
                                      && fcdsp::log2(fromBits(1u)) >= -127.0f, 1);
        P.eq("log2.nan_and_inf_give_nan", isNan(fcdsp::log2(fromBits(0x7fc00000u))) && isNan(fcdsp::log2(kInf))
                                              && isNan(fcdsp::log2(-kInf)), 1);
        // dB helpers: the floors (NaN kept), and the formulas they document
        {
            const Lanes d(fcdsp::dbFromLin(vec(0.0f, -1.0f, fromBits(0x7fc00000u), 1e-30f)));
            P.eq("db.from_lin.floor", d.v[0] == d.v[3] && d.v[0] == fcdsp::kDbPerLog2 * fcdsp::log2(fcdsp::kLinFloor)
                                          && d.v[1] == 0.0f && isNan(d.v[2]), 1);
            const Lanes m(fcdsp::dbFromMs(vec(0.0f, 1.0f, fromBits(0x7fc00000u), 4.0f)));
            P.eq("db.from_ms.floor", m.v[0] == 0.5f * fcdsp::kDbPerLog2 * fcdsp::log2(fcdsp::kMsFloor) && m.v[1] == 0.0f
                                         && isNan(m.v[2]) && m.v[3] == fcdsp::kDbPerLog2, 1);
            P.eq("db.lin_from_db.zero", bitsOf(simd::lane<0>(fcdsp::linFromDb(simd::set1(0.0f)))), bitsOf(1.0f));
        }
        if (emitGolden)
            P.hash("xarch.simd.log2.hash", h.value());
    }

    // ---- 4. exp2 ---------------------------------------------------------------------------------------------------------
    void exp2Probe(Probe& P, bool emitGolden)
    {
        const uint32_t mStep = P.quick() ? 64u : 1u;
        constexpr uint32_t kBlock = 4096, kSample = 4;
        Hash h;
        Max rel, backstep;
        int64_t n = 0, laneMismatch = 0, scaleMismatch = 0, notNormal = 0;
        std::vector<float> in(kBlock), out(kBlock), fr(kBlock), outF(kBlock);
        std::vector<int32_t> nInt(kBlock);
        uint32_t block = 0;
        // A step against the direction of x (exp2 rises), relative to the value: rounding allows one-ulp steps back
        // wherever consecutive inputs change 2^x by less than an ulp; the bound keeps them within the error bound.
        const auto stepsBack = [&](const std::vector<float>& x, const std::vector<float>& y, uint32_t cnt, float& last,
                                   bool falling)
        {
            for (uint32_t j = 0; j < cnt; ++j)
            {
                const float prev = j == 0 ? last : y[j - 1];
                if (falling ? y[j] > prev : y[j] < prev)
                {
                    const double yj = static_cast<double>(y[j]);
                    backstep.put(std::fabs(yj - static_cast<double>(prev)) / yj, static_cast<double>(x[j]));
                }
            }
            if (cnt > 0)
                last = y[cnt - 1];
        };

        // (a) |x| in [2^-24, 1/2): the reduced argument's whole range. Every value is evaluated and hashed; every 4th is
        //     compared with 2^x, a running product of 2^(4 delta) re-anchored on libm every 256 values.
        for (uint32_t eb = 103; eb <= 125; ++eb)
            for (const uint32_t sign : { 0u, 0x80000000u })
            {
                const double delta = std::ldexp(static_cast<double>(mStep * kSample), static_cast<int>(eb) - 150);
                const double stepFactor = std::exp2(sign != 0 ? -delta : delta);
                double r = 0;
                float last = sign != 0 ? kInf : 0.0f;
                for (uint32_t m0 = 0; m0 < (1u << 23); m0 += kBlock * mStep, ++block)
                {
                    for (uint32_t j = 0; j < kBlock; ++j)
                        in[j] = fromBits(sign | (eb << 23) | (m0 + j * mStep));
                    for (uint32_t j = 0; j < kBlock; j += 4)
                    {
                        const f32x4 v = fcdsp::exp2(simd::load(&in[j]));
                        simd::store(&out[j], v);
                        h.add(v);
                    }
                    if ((block & 15u) == 0)
                        for (uint32_t j = 0; j < 64; ++j)
                            laneMismatch += bitsOf(fcdsp::exp2(in[j])) != bitsOf(out[j]);
                    stepsBack(in, out, kBlock, last, sign != 0);
                    for (uint32_t j = 0; j < kBlock; j += kSample)
                    {
                        r = (j & 255u) == 0 ? std::exp2(static_cast<double>(in[j])) : r * stepFactor;
                        rel.put(std::fabs(static_cast<double>(out[j]) - r) / r, static_cast<double>(in[j]));
                    }
                    n += kBlock;
                }
            }
        for (const float x : { 0.5f, -0.5f })
        {
            const double r = std::exp2(static_cast<double>(x));
            rel.put(std::fabs(static_cast<double>(fcdsp::exp2(x)) - r) / r, static_cast<double>(x));
        }

        // (b) every float with |x| in [1/2, 126]: exp2(x) must be exp2(f) with n added to its exponent field, n the
        //     nearest integer (halves up) and f = x - n (both exact), so its error is (a)'s at f.
        for (uint32_t eb = 126; eb <= 133; ++eb)
            for (const uint32_t sign : { 0u, 0x80000000u })
            {
                float last = sign != 0 ? kInf : 0.0f;
                const uint32_t mEnd = eb == 133 ? 0x7c0001u : (1u << 23);      // |x| <= 126 = 1.96875 * 2^6
                for (uint32_t m0 = 0; m0 < mEnd; m0 += kBlock * mStep)
                {
                    const uint32_t cnt = std::min(kBlock, (mEnd - m0 + mStep - 1) / mStep) & ~3u;
                    for (uint32_t j = 0; j < cnt; ++j)
                    {
                        const float x = fromBits(sign | (eb << 23) | (m0 + j * mStep));
                        in[j] = x;
                        const double nn = std::floor(static_cast<double>(x) + 0.5);
                        nInt[j] = static_cast<int32_t>(nn);
                        fr[j] = static_cast<float>(static_cast<double>(x) - nn);        // exact
                    }
                    for (uint32_t j = 0; j < cnt; j += 4)
                    {
                        const f32x4 v = fcdsp::exp2(simd::load(&in[j]));
                        simd::store(&out[j], v);
                        simd::store(&outF[j], fcdsp::exp2(simd::load(&fr[j])));
                        h.add(v);
                    }
                    for (uint32_t j = 0; j < cnt; ++j)
                    {
                        scaleMismatch += bitsOf(out[j]) != bitsOf(outF[j]) + (static_cast<uint32_t>(nInt[j]) << 23);
                        notNormal += !(out[j] >= kFltMin);
                    }
                    stepsBack(in, out, cnt, last, sign != 0);
                    n += cnt;
                }
            }

        // (c) 2^-40 <= |x| < 2^-24, every 64th mantissa, against libm
        for (uint32_t eb = 87; eb <= 102; ++eb)
            for (const uint32_t sign : { 0u, 0x80000000u })
                for (uint32_t m = 0; m < (1u << 23); m += 64u * mStep)
                {
                    const float x = fromBits(sign | (eb << 23) | m);
                    const float y = fcdsp::exp2(x);
                    h.add(y);
                    const double r = std::exp2(static_cast<double>(x));
                    rel.put(std::fabs(static_cast<double>(y) - r) / r, static_cast<double>(x));
                    ++n;
                }

        // (d) exact values, clamping, NaN
        int intMismatch = 0;
        for (int k = -126; k <= 126; ++k)
            intMismatch += fcdsp::exp2(static_cast<float>(k)) != std::ldexp(1.0f, k);
        std::printf("NOTE     exp2: %lld values, max relative error %.3g at %a; largest backward step %.3g (relative) at "
                    "%a\n", static_cast<long long>(n), rel.v, rel.at, backstep.v, backstep.at);
        P.le("exp2.max_rel_err", rel.v, 2.2e-7);                                   // refit target 1e-5
        P.le("exp2.max_rel_err_db", 20.0 * std::log10(1.0 + rel.v), 7.4e-4);       // E §0.9
        P.le("exp2.max_backward_step_rel", backstep.v, 2.2e-7);                   // within the error bound
        P.eq("exp2.integers.mismatches", intMismatch, 0);
        P.eq("exp2.scaling.mismatches", scaleMismatch, 0);
        P.eq("exp2.not_normal", notNormal, 0);
        P.eq("exp2.scalar_vs_lanes.mismatches", laneMismatch, 0);
        P.eq("exp2.clamp", fcdsp::exp2(-200.0f) == 0x1p-126f && fcdsp::exp2(200.0f) == 0x1p126f
                               && fcdsp::exp2(-kInf) == 0x1p-126f && fcdsp::exp2(kInf) == 0x1p126f
                               && fcdsp::exp2(-126.0f) == 0x1p-126f && fcdsp::exp2(0.0f) == 1.0f, 1);
        P.eq("exp2.nan", isNan(fcdsp::exp2(fromBits(0x7fc00000u))) ? 1 : 0, 1);
        if (emitGolden)
            P.hash("xarch.simd.exp2.hash", h.value());
    }

    // ---- 5. tanh, logCosh ------------------------------------------------------------------------------------------------
    // Strided sweep over |x| = [2^(ebLo - 127), 2^(ebHi - 126)), both signs, denser in two binades; calls
    // f(x_block of positive x) and f(-x_block).
    template <class F>
    void sweepVec(uint32_t ebLo, uint32_t ebHi, uint32_t denseLo, uint32_t denseHi, uint32_t stride, uint32_t denseStride,
                  F&& f)
    {
        std::vector<float> in;
        in.reserve(4096);
        for (uint32_t eb = ebLo; eb <= ebHi; ++eb)
        {
            const uint32_t st = (eb >= denseLo && eb <= denseHi) ? denseStride : stride;
            for (uint32_t m0 = 0; m0 < (1u << 23); m0 += 4096 * st)
            {
                in.clear();
                for (uint32_t j = 0; j < 4096 && m0 + j * st < (1u << 23); ++j)
                    in.push_back(fromBits((eb << 23) | (m0 + j * st)));
                f(in);
            }
        }
    }

    void tanhProbe(Probe& P, bool emitGolden)
    {
        const uint32_t k = P.quick() ? 16u : 1u;
        Hash h;
        Max absErr, excess, relSmall, rel, backstep;
        int64_t n = 0, laneMismatch = 0, oddMismatch = 0, overOne = 0;
        float last = 0;
        std::vector<float> pos(4096), neg(4096);
        sweepVec(87, 132, 123, 124, 32 * k, 8 * k, [&](const std::vector<float>& in)
        {
            const std::size_t cnt = in.size() & ~std::size_t{3};
            for (std::size_t j = 0; j < cnt; j += 4)
            {
                const f32x4 x = simd::load(&in[j]);
                const f32x4 yp = fcdsp::tanh(x), yn = fcdsp::tanh(simd::neg(x));
                simd::store(&pos[j], yp);
                simd::store(&neg[j], yn);
                h.add(yp);
                h.add(yn);
                if ((j & 63u) == 0)
                    for (std::size_t q = 0; q < 4; ++q)
                        laneMismatch += bitsOf(fcdsp::tanh(in[j + q])) != bitsOf(pos[j + q]);
            }
            for (std::size_t j = 0; j < cnt; ++j)
            {
                const double x = static_cast<double>(in[j]), r = std::tanh(x), y = static_cast<double>(pos[j]);
                const double d = std::fabs(y - r);
                absErr.put(d, x);
                excess.put(d - 0.5 * ulpOf(pos[j]), x);
                (x < 0.125 ? relSmall : rel).put(d / r, x);
                oddMismatch += bitsOf(neg[j]) != (bitsOf(pos[j]) ^ 0x80000000u);
                overOne += std::fabs(pos[j]) > 1.0f;
                backstep.put(static_cast<double>(last) - y, x);
                last = pos[j];
            }
            n += static_cast<int64_t>(cnt);
        });
        std::printf("NOTE     tanh: %lld values, max |err| %.3g at %a (beyond the result's rounding %.3g at %a); relative "
                    "%.3g at %a (|x| < 1/8), %.3g at %a (|x| >= 1/8); largest backward step %.3g at %a\n",
                    static_cast<long long>(n), absErr.v, absErr.at, excess.v, excess.at, relSmall.v, relSmall.at, rel.v,
                    rel.at, backstep.v, backstep.at);
        P.le("tanh.max_abs_err", absErr.v, 1.9e-7);
        P.le("tanh.max_err_beyond_final_rounding", excess.v, 1.8e-7);
        P.le("tanh.small.max_rel_err", relSmall.v, 1e-7);
        P.le("tanh.max_rel_err", rel.v, 1.3e-6);
        P.eq("tanh.odd.mismatches", oddMismatch, 0);
        P.eq("tanh.over_one", overOne, 0);
        P.le("tanh.max_backward_step", backstep.v, 1.8e-7);
        P.eq("tanh.scalar_vs_lanes.mismatches", laneMismatch, 0);
        P.eq("tanh.specials", bitsOf(fcdsp::tanh(0.0f)) == 0u && bitsOf(fcdsp::tanh(-0.0f)) == 0x80000000u
                                  && fcdsp::tanh(kInf) == 1.0f && fcdsp::tanh(-kInf) == -1.0f && fcdsp::tanh(20.0f) == 1.0f
                                  && isNan(fcdsp::tanh(fromBits(0x7fc00000u))), 1);
        if (emitGolden)
            P.hash("xarch.simd.tanh.hash", h.value());
    }

    double logCoshRef(double x) noexcept
    {
        const double a = std::fabs(x);
        if (a < 1.0)
        {
            const double s = std::sinh(0.5 * a);
            return std::log1p(2.0 * s * s);                       // cosh a - 1 = 2 sinh^2(a/2): no cancellation
        }
        return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
    }

    void logCoshProbe(Probe& P, bool emitGolden)
    {
        const uint32_t k = P.quick() ? 16u : 1u;
        Hash h;
        Max absErr, excess, relSmall, rel, backstep;
        int64_t n = 0, laneMismatch = 0, evenMismatch = 0, negative = 0;
        float last = 0;
        std::vector<float> pos(4096), neg(4096);
        sweepVec(97, 132, 123, 124, 32 * k, 8 * k, [&](const std::vector<float>& in)
        {
            const std::size_t cnt = in.size() & ~std::size_t{3};
            for (std::size_t j = 0; j < cnt; j += 4)
            {
                const f32x4 x = simd::load(&in[j]);
                const f32x4 yp = fcdsp::logCosh(x), yn = fcdsp::logCosh(simd::neg(x));
                simd::store(&pos[j], yp);
                simd::store(&neg[j], yn);
                h.add(yp);
                h.add(yn);
                if ((j & 63u) == 0)
                    for (std::size_t q = 0; q < 4; ++q)
                        laneMismatch += bitsOf(fcdsp::logCosh(in[j + q])) != bitsOf(pos[j + q]);
            }
            for (std::size_t j = 0; j < cnt; ++j)
            {
                const double x = static_cast<double>(in[j]), r = logCoshRef(x), y = static_cast<double>(pos[j]);
                const double d = std::fabs(y - r);
                absErr.put(d, x);
                excess.put(d - 0.5 * ulpOf(pos[j]), x);
                (x < 0.125 ? relSmall : rel).put(d / r, x);
                evenMismatch += bitsOf(neg[j]) != bitsOf(pos[j]);
                negative += pos[j] < 0.0f;
                backstep.put(static_cast<double>(last) - y, x);
                last = pos[j];
            }
            n += static_cast<int64_t>(cnt);
        });
        std::printf("NOTE     logCosh: %lld values, max |err| %.3g at %a (beyond the result's rounding %.3g at %a); "
                    "relative %.3g at %a (|x| < 1/8), %.3g at %a (|x| >= 1/8); largest backward step %.3g at %a\n",
                    static_cast<long long>(n), absErr.v, absErr.at, excess.v, excess.at, relSmall.v, relSmall.at, rel.v,
                    rel.at, backstep.v, backstep.at);
        P.le("logcosh.max_err_beyond_final_rounding", excess.v, 1.7e-7);
        P.le("logcosh.small.max_rel_err", relSmall.v, 1.5e-7);
        P.le("logcosh.max_rel_err", rel.v, 1.5e-5);
        P.eq("logcosh.even.mismatches", evenMismatch, 0);
        P.eq("logcosh.negative", negative, 0);
        P.le("logcosh.max_backward_step", backstep.v, 1.7e-7);
        P.eq("logcosh.scalar_vs_lanes.mismatches", laneMismatch, 0);
        P.eq("logcosh.specials", bitsOf(fcdsp::logCosh(0.0f)) == 0u && bitsOf(fcdsp::logCosh(-0.0f)) == 0u
                                     && fcdsp::logCosh(kInf) == kInf && fcdsp::logCosh(-kInf) == kInf
                                     && fcdsp::logCosh(1e6f) == 1e6f - std::numbers::ln2_v<float>
                                     && isNan(fcdsp::logCosh(fromBits(0x7fc00000u))), 1);
        if (emitGolden)
            P.hash("xarch.simd.logcosh.hash", h.value());
    }

    // ---- 6. tanPi, sinPi, cosPi (scalar, out of line in fcdsp) -------------------------------------------------------------
    // Reference with the same exact reflections in double, so libm's sin/cos only ever see |pi y| <= pi/4.
    double sinPiRef(double x) noexcept
    {
        const double a = std::fabs(x), b = a > 0.5 ? 1.0 - a : a;
        const double r = b > 0.25 ? std::cos(kPi * (0.5 - b)) : std::sin(kPi * b);
        return x < 0 ? -r : r;
    }
    double cosPiRef(double x) noexcept
    {
        const double a = std::fabs(x), b = a > 0.5 ? 1.0 - a : a;
        const double r = b > 0.25 ? std::sin(kPi * (0.5 - b)) : std::cos(kPi * b);
        return a > 0.5 ? -r : r;
    }
    double tanPiRef(double x) noexcept
    {
        return x > 0.25 ? 1.0 / std::tan(kPi * (0.5 - x)) : std::tan(kPi * x);
    }

    template <class F>
    void sweepScalar(uint32_t ebLo, uint32_t ebHi, uint32_t denseLo, uint32_t stride, uint32_t denseStride, float xMax,
                     F&& f)
    {
        for (uint32_t eb = ebLo; eb <= ebHi; ++eb)
        {
            const uint32_t st = eb >= denseLo ? denseStride : stride;
            for (uint32_t m = 0; m < (1u << 23); m += st)
            {
                const float x = fromBits((eb << 23) | m);
                if (x > xMax)
                    return;
                f(x);
            }
        }
        if (xMax >= 1.0f)
            f(1.0f);
    }

    void trigProbe(Probe& P, bool emitGolden)
    {
        const uint32_t k = P.quick() ? 16u : 1u;
        {
            Hash h;
            Max rel;
            int64_t n = 0, nonMonotone = 0;
            float last = 0;
            h.add(fcdsp::tanPi(0.0f));
            sweepScalar(87, 125, 124, 64 * k, 16 * k, 0.499f, [&](float x)
            {
                const float y = fcdsp::tanPi(x);
                h.add(y);
                const double r = tanPiRef(static_cast<double>(x));
                rel.put(std::fabs(static_cast<double>(y) - r) / r, static_cast<double>(x));
                nonMonotone += y < last;
                last = y;
                ++n;
            });
            std::printf("NOTE     tanPi: %lld values, max relative error %.3g at %a\n", static_cast<long long>(n), rel.v,
                        rel.at);
            P.le("tanpi.max_rel_err", rel.v, 2.6e-7);
            P.eq("tanpi.nonmonotone", nonMonotone, 0);
            P.eq("tanpi.zero", bitsOf(fcdsp::tanPi(0.0f)), 0u);
            P.near("tanpi.quarter", static_cast<double>(fcdsp::tanPi(0.25f)), 1.0, 1.2e-7);
            P.ge("tanpi.near_half", static_cast<double>(fcdsp::tanPi(0.499f)), 318.0);
            if (emitGolden)
                P.hash("xarch.simd.tanpi.hash", h.value());
        }
        for (int which = 0; which < 2; ++which)
        {
            const bool isSin = which == 0;
            Hash h;
            Max absErr, rel;
            int64_t n = 0, symMismatch = 0, nonMonotone = 0, overOne = 0;
            float last = isSin ? 0.0f : 1.0f;
            sweepScalar(87, 126, 124, 64 * k, 16 * k, 1.0f, [&](float x)
            {
                const float y = isSin ? fcdsp::sinPi(x) : fcdsp::cosPi(x);
                const float yn = isSin ? fcdsp::sinPi(-x) : fcdsp::cosPi(-x);
                h.add(y);
                h.add(yn);
                const double r = isSin ? sinPiRef(static_cast<double>(x)) : cosPiRef(static_cast<double>(x));
                const double d = std::fabs(static_cast<double>(y) - r);
                absErr.put(d, static_cast<double>(x));
                if (r != 0.0)
                    rel.put(d / std::fabs(r), static_cast<double>(x));
                symMismatch += bitsOf(yn) != (isSin ? bitsOf(y) ^ 0x80000000u : bitsOf(y));
                overOne += std::fabs(y) > 1.0f;
                // sinPi rises on [0, 1/2] and falls on [1/2, 1]; cosPi falls on [0, 1]
                nonMonotone += (isSin && x <= 0.5f) ? y < last : y > last;
                last = y;
                ++n;
            });
            const char* name = isSin ? "sinpi" : "cospi";
            std::printf("NOTE     %s: %lld values, max |err| %.3g at %a, max relative error %.3g at %a\n", name,
                        static_cast<long long>(n), absErr.v, absErr.at, rel.v, rel.at);
            const std::string key = name;
            P.le(key + ".max_abs_err", absErr.v, 1e-7);
            P.le(key + ".max_rel_err", rel.v, 1.6e-7);
            P.eq(key + (isSin ? ".odd.mismatches" : ".even.mismatches"), symMismatch, 0);
            P.eq(key + ".over_one", overOne, 0);
            P.eq(key + ".nonmonotone", nonMonotone, 0);
            if (emitGolden)
                P.hash("xarch.simd." + key + ".hash", h.value());
        }
        P.eq("sinpi.exact", fcdsp::sinPi(0.0f) == 0.0f && fcdsp::sinPi(1.0f) == 0.0f && fcdsp::sinPi(-1.0f) == 0.0f
                                && fcdsp::sinPi(0.5f) == 1.0f && fcdsp::sinPi(-0.5f) == -1.0f, 1);
        P.eq("cospi.exact", fcdsp::cosPi(0.0f) == 1.0f && fcdsp::cosPi(0.5f) == 0.0f && fcdsp::cosPi(-0.5f) == 0.0f
                                && fcdsp::cosPi(1.0f) == -1.0f && fcdsp::cosPi(-1.0f) == -1.0f, 1);
    }
} // namespace

FCMP_PROBE(dsp, simd)
{
    const bool golden = !P.quick();                    // hashes of reduced grids are not the golden rows
    Timer t;
    opsProbe(P, golden);
    t.note("ops");
#if defined(FCDSP_SIMD_NEON)
    estimateProbe(P);
    t.note("estimate");
#endif
    log2Probe(P, golden);
    t.note("log2");
    exp2Probe(P, golden);
    t.note("exp2");
    tanhProbe(P, golden);
    t.note("tanh");
    logCoshProbe(P, golden);
    t.note("logCosh");
    trigProbe(P, golden);
    t.note("trig");
    return P.finish();
}
