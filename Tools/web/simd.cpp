// FCMP_WEB_TEST name=web.simd timeout=300 on=all args=simd
//
// Tools/web/simd.cpp: web.simd (F-W, WEB-A.1; ADR-93). fcdsp::simd's contract (Source/fcdsp/core/Simd.h) checked the
// same way on every backend: arm64 NEON, x86-64 SSE and wasm32 SIMD128 (under node). What passes here on all three is
// what "the wasm build has the plugin's arithmetic" means.
//
//   1. moves     load/store, set1, abs, neg, sel, lane and withLane move bits untouched (denormals, NaN payloads).
//   2. ops       every op against a scalar IEEE reference over dsp.simd's edge set (Tools/probes/dsp/simd.cpp) without
//                its denormals, random normals, random bit patterns and operands chosen to land beside FLT_MIN. No
//                denormal OPERANDS: the native FP mode reads one as zero and wasm reads it as it is (Simd.h), and
//                nothing in fcdsp depends on which. Results are compared bit for bit, any NaN equal to any NaN.
//   3. fma, fms  bit for bit against std::fma over more than 10^7 triples: random bit patterns, operands of nearby
//                scale, cancellation, sums that land on or beside the midpoint of two floats (where a multiply-add in
//                double rounds twice), sums beside FLT_MIN and FLT_MAX, infinities and NaNs, and vectors that mix
//                these lane by lane (the wasm fma has a fast and a slow path per vector). The reference is a scalar
//                round-to-odd form which this check first holds to std::fma itself: natively that is the hardware
//                instruction; on wasm it is musl's software fmaf, compared and reported.
//   4. tiny      the flush rule: a result of add, sub, mul, div, fma or fms below FLT_MIN is a signed zero. arm64
//                decides before rounding, x86-64 and wasm after (Simd.h): the one place the expected value depends on
//                the backend, with min/max of a NaN or of +0 against -0 (wasm has x86's rule).
//   5. fastmath  log2, exp2, tanh, logCosh, tanPi, sinPi, cosPi over fixed sweeps of normal numbers, hashed, against
//                constants recorded from the native arm64 build: the wasm run has to reproduce native bits.
//
// The references run in the default FP mode and apply the flush rule themselves; the ops run under ScopedFtz, as
// fcdsp always does (an empty scope on wasm), with operands loaded and results stored inside the scope.
//
// With FCDSP_WASM_FMA_UNFUSED (cmake -DFCOMPRESSOR_WEB_FMA=unfused) the rows that need one rounding become NOTE lines
// that give the mismatch rate, and the check still passes: that build exists to be measured, not to be right.
#include "web/WebCheck.h"

#include "probes/common/Signals.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
    namespace simd = fcdsp::simd;
    using fcmp::probe::sig::Pcg32;
    using simd::f32x4;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kFltMin = std::numeric_limits<float>::min();
    constexpr float kFltMax = std::numeric_limits<float>::max();
    constexpr double kMinD = static_cast<double>(kFltMin);

    constexpr uint32_t bitsOf(float f) noexcept { return std::bit_cast<uint32_t>(f); }
    constexpr float fromBits(uint32_t b) noexcept { return std::bit_cast<float>(b); }
    constexpr bool isNanBits(uint32_t b) noexcept { return (b & 0x7fffffffu) > 0x7f800000u; }
    constexpr bool isNan(float f) noexcept { return isNanBits(bitsOf(f)); }
    constexpr bool isInf(float f) noexcept { return (bitsOf(f) & 0x7fffffffu) == 0x7f800000u; }
    constexpr bool isDenormalBits(uint32_t b) noexcept { return (b & 0x7f800000u) == 0u && (b & 0x007fffffu) != 0u; }
    constexpr float signedZero(bool negative) noexcept { return fromBits(negative ? 0x80000000u : 0u); }
    // A denormal becomes the zero of its sign: the operand sets hold none.
    constexpr float squash(uint32_t b) noexcept { return fromBits(isDenormalBits(b) ? (b & 0x80000000u) : b); }

    // ---- the backend's documented rules (Simd.h) --------------------------------------------------------------------
#if defined(FCDSP_SIMD_NEON)
    constexpr const char* kBackend = "arm64 NEON";
    constexpr bool kTinyBeforeRounding = true;              // FPCR.FZ decides on the exact result
    constexpr bool kMinMaxNeon = true;                      // FMIN/FMAX: a NaN wins, -0 < +0
    constexpr bool kFlushSums = true;
#elif defined(FCDSP_SIMD_WASM)
    constexpr const char* kBackend = "wasm32 SIMD128";
    constexpr bool kTinyBeforeRounding = false;             // detail::flushTiny decides on the rounded result
    constexpr bool kMinMaxNeon = false;                     // pmin/pmax swapped: the second operand, as minps/maxps
    constexpr bool kFlushSums = FCDSP_WASM_FLUSH_ADDSUB != 0;
#else
    constexpr const char* kBackend = "x86-64 SSE";
    constexpr bool kTinyBeforeRounding = false;             // MXCSR.FTZ decides on the rounded result
    constexpr bool kMinMaxNeon = false;                     // minps/maxps: the second operand
    constexpr bool kFlushSums = true;
#endif
#if defined(FCDSP_WASM_FMA_UNFUSED)
    constexpr bool kFmaExact = false;
#else
    constexpr bool kFmaExact = true;
#endif

    // ---- output -----------------------------------------------------------------------------------------------------
    struct Report
    {
        int rows = 0, failed = 0;

        // PASS when got == want.
        void eq(const char* name, long long got, long long want)
        {
            ++rows;
            if (got == want)
                std::printf("PASS  %-44s %lld\n", name, got);
            else
            {
                ++failed;
                std::printf("FAIL  %-44s %lld, expected %lld\n", name, got, want);
            }
        }
        void bits(const char* name, uint64_t got, uint64_t want)
        {
            ++rows;
            if (got == want)
                std::printf("PASS  %-44s 0x%llx\n", name, static_cast<unsigned long long>(got));
            else
            {
                ++failed;
                std::printf("FAIL  %-44s 0x%llx, expected 0x%llx\n", name, static_cast<unsigned long long>(got),
                            static_cast<unsigned long long>(want));
            }
        }
        // A count of mismatches that must be zero, out of `of`. `informative`: a NOTE with the rate instead, and why
        // the row is not judged (the unfused build's rows, by default).
        void mismatches(const char* name, long long bad, long long of, bool informative = false,
                        const char* why = "the unfused fma rounds twice: not judged in this build")
        {
            if (informative)
            {
                std::printf("NOTE  %-44s %lld of %lld differ (%.3g %%): %s\n", name, bad, of,
                            of > 0 ? 100.0 * static_cast<double>(bad) / static_cast<double>(of) : 0.0, why);
                return;
            }
            ++rows;
            if (bad == 0)
                std::printf("PASS  %-44s 0 of %lld differ\n", name, of);
            else
            {
                ++failed;
                std::printf("FAIL  %-44s %lld of %lld differ\n", name, bad, of);
            }
        }
    };

    bool same(float got, float want) noexcept { return bitsOf(got) == bitsOf(want) || (isNan(got) && isNan(want)); }

    // ---- scalar references (default FP mode) ------------------------------------------------------------------------
    // The flush rule. `rounded` is the IEEE result; `exact` is a double with the exact result's sign that lies on the
    // exact result's side of FLT_MIN (the exact value itself, or it rounded to nearest or to odd in double).
    float flushed(float rounded, double exact) noexcept
    {
        if (isNan(rounded) || isInf(rounded))
            return rounded;
        if (kTinyBeforeRounding)
            return (exact != 0.0 && std::fabs(exact) < kMinD) ? signedZero(std::signbit(exact)) : rounded;
        return (rounded != 0.0f && std::fabs(rounded) < kFltMin) ? signedZero(std::signbit(rounded)) : rounded;
    }

    // Sums and differences of normal floats are exact when they are tiny, so their double is the exact value there.
    float refAdd(float a, float b) noexcept
    {
        const float r = a + b;
        return kFlushSums ? flushed(r, static_cast<double>(a) + static_cast<double>(b)) : r;
    }
    float refSub(float a, float b) noexcept
    {
        const float r = a - b;
        return kFlushSums ? flushed(r, static_cast<double>(a) - static_cast<double>(b)) : r;
    }
    float refMul(float a, float b) noexcept { return flushed(a * b, static_cast<double>(a) * static_cast<double>(b)); }
    // A quotient of two floats is FLT_MIN exactly or at least 2^-24 away from it (relative), so the double decides.
    float refDiv(float a, float b) noexcept { return flushed(a / b, static_cast<double>(a) / static_cast<double>(b)); }

    // a + b*c with one rounding, and the exact sum rounded to odd in double (the side of FLT_MIN it lies on). The
    // product is exact in double; TwoSum gives the sum's error; round to odd, then to float (Boldo and Melquiond).
    struct Fma { float rounded; double odd; };
    Fma refFma(float a, float b, float c) noexcept
    {
        const double p = static_cast<double>(b) * static_cast<double>(c);
        const double z = static_cast<double>(a);
        const double s = p + z;
        const double t = s - p;
        const double e = (p - (s - t)) + (z - t);
        uint64_t u = std::bit_cast<uint64_t>(s);
        if (e > 0.0 || e < 0.0)                             // inexact (never for an infinite or NaN sum: e is NaN)
        {
            if ((e > 0.0) == (s < 0.0))
                --u;                                        // the exact sum is nearer zero than s: truncate
            u |= 1u;
        }
        const double odd = std::bit_cast<double>(u);
        return { static_cast<float>(odd), odd };
    }
    float refMin(float a, float b) noexcept
    {
        if (kMinMaxNeon)
        {
            if (isNan(a) || isNan(b))
                return fromBits(0x7fc00000u);
            if (a == b)
                return std::signbit(a) ? a : b;             // -0 < +0
        }
        return a < b ? a : b;
    }
    float refMax(float a, float b) noexcept
    {
        if (kMinMaxNeon)
        {
            if (isNan(a) || isNan(b))
                return fromBits(0x7fc00000u);
            if (a == b)
                return std::signbit(a) ? b : a;
        }
        return a > b ? a : b;
    }
    float refRsqrte(float x) noexcept { return fromBits(simd::detail::frsqrteBits(bitsOf(x), false)); }
    // FRSQRTS: (3 - p*q) / 2 with one rounding; (+-0 x +-inf) in either order is 1.5.
    float refRsqrts(float p, float q) noexcept
    {
        if ((p == 0.0f && isInf(q)) || (isInf(p) && q == 0.0f))
            return 1.5f;
        return static_cast<float>(0.5 * refFma(3.0f, -p, q).odd);     // halving a double is exact and keeps it odd
    }

    // ---- running the ops (under ScopedFtz, operands loaded and results stored inside the scope) ---------------------
    template <class Op>
    [[gnu::noinline]] void run1(Op op, const float* a, float* out, std::size_t n) noexcept
    {
        const fcdsp::ScopedFtz ftz;
        for (std::size_t i = 0; i < n; i += 4)
            simd::store(out + i, op(simd::load(a + i)));
    }
    template <class Op>
    [[gnu::noinline]] void run2(Op op, const float* a, const float* b, float* out, std::size_t n) noexcept
    {
        const fcdsp::ScopedFtz ftz;
        for (std::size_t i = 0; i < n; i += 4)
            simd::store(out + i, op(simd::load(a + i), simd::load(b + i)));
    }
    template <class Op>
    [[gnu::noinline]] void run3(Op op, const float* a, const float* b, const float* c, float* out,
                                std::size_t n) noexcept
    {
        const fcdsp::ScopedFtz ftz;
        for (std::size_t i = 0; i < n; i += 4)
            simd::store(out + i, op(simd::load(a + i), simd::load(b + i), simd::load(c + i)));
    }

    // Operand lists are padded to a multiple of four with 1.0f.
    void pad(std::vector<float>& v)
    {
        while ((v.size() & 3u) != 0u)
            v.push_back(1.0f);
    }

    struct Tally { long long n = 0, bad = 0; };

    void describe(const char* name, const float* a, const float* b, const float* c, std::size_t i, float got,
                  float want)
    {
        std::printf("NOTE  %s:", name);
        for (const float* p : { a, b, c })
            if (p != nullptr)
                std::printf(" %a (0x%08x)", static_cast<double>(p[i]), bitsOf(p[i]));
        std::printf(" -> %a (0x%08x), expected %a (0x%08x)\n", static_cast<double>(got), bitsOf(got),
                    static_cast<double>(want), bitsOf(want));
    }

    // Compares got with want over n results; the first few mismatches are printed unless `quiet` (a row that only
    // reports a rate). exactBits: NaNs must match too.
    void tally(Tally& t, const char* name, const float* a, const float* b, const float* c, const float* got,
               const float* want, std::size_t n, bool exactBits = false, bool quiet = false)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            ++t.n;
            if (exactBits ? bitsOf(got[i]) == bitsOf(want[i]) : same(got[i], want[i]))
                continue;
            if (++t.bad <= 4 && !quiet)
                describe(name, a, b, c, i, got[i], want[i]);
        }
    }

    // ---- operand sets -----------------------------------------------------------------------------------------------
    // dsp.simd's edge set (Tools/probes/dsp/simd.cpp), with or without its three denormals.
    std::vector<float> edgeValues(bool withDenormals)
    {
        std::vector<float> v = { fromBits(0x00000000u), fromBits(0x80000000u), kInf, -kInf,
                                 fromBits(0x7fc00000u), fromBits(0xffc00001u), fromBits(0x7fa00000u),  // NaNs, sNaN
                                 kFltMin, -kFltMin, 0x1p-125f, 1.0f, -1.0f, 0.5f, -0.5f, 1.5f, -1.5f, -2.0f, 2.5f,
                                 3.0f, 7.0f, 0.1f, -0.1f, 1e-20f, -1e20f, 1e6f, -1e6f, kFltMax, -kFltMax,
                                 std::numbers::pi_v<float>, fromBits(0x3f7fffffu), fromBits(0x3f800001u), -8388609.0f,
                                 8388608.0f };
        if (withDenormals)
            for (const uint32_t d : { 0x00000001u, 0x80000001u, 0x007fffffu })
                v.push_back(fromBits(d));
        return v;
    }

    // A random normal float: either sign, exponent in [eLo, eHi], every mantissa.
    float randomNormal(Pcg32& rng, int eLo, int eHi) noexcept
    {
        const uint32_t e = static_cast<uint32_t>(eLo + 127) + rng.bounded(static_cast<uint32_t>(eHi - eLo + 1));
        return fromBits((rng.next() & 0x80000000u) | (e << 23) | (rng.next() & 0x007fffffu));
    }

    // 2^k as a double, without libm (k in [-1022, 1023]).
    constexpr double pow2(int k) noexcept { return std::bit_cast<double>(static_cast<uint64_t>(k + 1023) << 52); }

    // Operand pairs for the binary ops: every pair of edges, normals of moderate exponent, random bit patterns
    // (overflow, underflow, infinities, NaNs).
    void commonPairs(std::vector<float>& a, std::vector<float>& b)
    {
        const std::vector<float> E = edgeValues(false);
        for (const float x : E)
            for (const float y : E)
            {
                a.push_back(x);
                b.push_back(y);
            }
        Pcg32 rng(0x5eed51bdu, 11u);
        for (int i = 0; i < 16384; ++i)
        {
            a.push_back(randomNormal(rng, -30, 30));
            b.push_back(randomNormal(rng, -30, 30));
        }
        for (int i = 0; i < 65536; ++i)
        {
            a.push_back(squash(rng.next()));
            b.push_back(squash(rng.next()));
        }
    }

    // ---- 1. moves ---------------------------------------------------------------------------------------------------
    void moves(Report& r)
    {
        std::vector<float> a = edgeValues(true), b;
        Pcg32 rng(0x30f5u, 5u);
        for (int i = 0; i < 4096; ++i)
            a.push_back(fromBits(rng.next()));                          // every class, denormals and NaN payloads too
        pad(a);
        for (std::size_t i = 0; i < a.size(); ++i)
            b.push_back(a[(i * 7 + 3) % a.size()]);
        const std::size_t n = a.size();
        std::vector<float> got(n), want(n);
        Tally t;

        run1([](f32x4 v) noexcept { alignas(16) float m[4]; simd::store(m, v); return simd::load(m); }, a.data(),
             got.data(), n);
        tally(t, "moves.load_store", a.data(), nullptr, nullptr, got.data(), a.data(), n, true);
        run1([](f32x4 v) noexcept { return simd::abs(v); }, a.data(), got.data(), n);
        for (std::size_t i = 0; i < n; ++i)
            want[i] = fromBits(bitsOf(a[i]) & 0x7fffffffu);
        tally(t, "moves.abs", a.data(), nullptr, nullptr, got.data(), want.data(), n, true);
        run1([](f32x4 v) noexcept { return simd::neg(v); }, a.data(), got.data(), n);
        for (std::size_t i = 0; i < n; ++i)
            want[i] = fromBits(bitsOf(a[i]) ^ 0x80000000u);
        tally(t, "moves.neg", a.data(), nullptr, nullptr, got.data(), want.data(), n, true);
        // sel through a mask that does not depend on the operands: lanes 0 and 2 take the first.
        alignas(16) const float pick[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
        run2([&](f32x4 x, f32x4 y) noexcept { return simd::sel(simd::gt(simd::load(pick), simd::set1(0.5f)), x, y); },
             a.data(), b.data(), got.data(), n);
        for (std::size_t i = 0; i < n; ++i)
            want[i] = (i & 1u) == 0u ? a[i] : b[i];
        tally(t, "moves.sel", a.data(), b.data(), nullptr, got.data(), want.data(), n, true);
        r.mismatches("moves.load_store_abs_neg_sel", t.bad, t.n);

        // set1, lane<I>, withLane<I>
        long long bad = 0, count = 0;
        {
            const fcdsp::ScopedFtz ftz;
            for (std::size_t i = 0; i + 4 <= n; i += 4)
            {
                const f32x4 v = simd::load(&a[i]);
                const float s = b[i];
                alignas(16) float m[4];
                simd::store(m, simd::set1(s));
                for (const float x : m)
                    bad += bitsOf(x) != bitsOf(s);
                bad += bitsOf(simd::lane<0>(v)) != bitsOf(a[i]);
                bad += bitsOf(simd::lane<1>(v)) != bitsOf(a[i + 1]);
                bad += bitsOf(simd::lane<2>(v)) != bitsOf(a[i + 2]);
                bad += bitsOf(simd::lane<3>(v)) != bitsOf(a[i + 3]);
                const f32x4 w[4] = { simd::withLane<0>(v, s), simd::withLane<1>(v, s), simd::withLane<2>(v, s),
                                     simd::withLane<3>(v, s) };
                for (std::size_t k = 0; k < 4; ++k)
                {
                    simd::store(m, w[k]);
                    for (std::size_t j = 0; j < 4; ++j)
                        bad += bitsOf(m[j]) != bitsOf(j == k ? s : a[i + j]);
                }
                count += 24;
            }
        }
        r.mismatches("moves.set1_lane_withlane", bad, count);
    }

    // ---- 2. ops -----------------------------------------------------------------------------------------------------
    void unaryOps(Report& r)
    {
        std::vector<float> a = edgeValues(false);
        Pcg32 rng(0x0b5eu, 7u);
        for (int i = 0; i < 16384; ++i)
            a.push_back(randomNormal(rng, -30, 30));
        for (int i = 0; i < 65536; ++i)
            a.push_back(squash(rng.next()));
        pad(a);
        const std::size_t n = a.size();
        std::vector<float> got(n), want(n);

        const auto check = [&](const char* name, auto op, auto ref)
        {
            Tally t;
            run1(op, a.data(), got.data(), n);
            for (std::size_t i = 0; i < n; ++i)
                want[i] = ref(a[i]);
            tally(t, name, a.data(), nullptr, nullptr, got.data(), want.data(), n);
            r.mismatches(name, t.bad, t.n);
        };
        check("ops.sqrt", [](f32x4 v) noexcept { return simd::sqrt(v); },
              [](float x) noexcept { return std::sqrt(x); });
        check("ops.floor", [](f32x4 v) noexcept { return simd::floor(v); },
              [](float x) noexcept { return std::floor(x); });
        check("ops.rsqrte", [](f32x4 v) noexcept { return simd::rsqrte(v); }, refRsqrte);
    }

    // Pairs whose result lies within a few units in the last place of +-FLT_MIN: the flush rule's boundary.
    void nearMinPairs(char op, std::vector<float>& a, std::vector<float>& b)
    {
        Pcg32 rng(0x7131u, static_cast<uint64_t>(op));
        for (int i = 0; i < 32768; ++i)
        {
            const float sign = (rng.next() & 1u) != 0u ? -1.0f : 1.0f;
            const int j = static_cast<int>(rng.bounded(17u)) - 8;
            if (op == '+' || op == '-')
            {
                // +-FLT_MIN * u against FLT_MIN * v, u and v in [1, 4): exact, and often below FLT_MIN
                const float x = fromBits(((1u + rng.bounded(2u)) << 23) | (rng.next() & 0x007fffffu));
                const float y = fromBits(((1u + rng.bounded(2u)) << 23) | (rng.next() & 0x007fffffu));
                a.push_back(sign * x);
                b.push_back(op == '+' ? -sign * y : sign * y);
            }
            else if (op == '*')
            {
                const float x = randomNormal(rng, -100, -2);
                a.push_back(x);                             // x * y = +-FLT_MIN * (1 + j 2^-23), to y's rounding
                b.push_back(sign * static_cast<float>(kMinD * (1.0 + j * 0x1p-23) / static_cast<double>(x)));
            }
            else
            {
                const float y = randomNormal(rng, 1, 100);
                a.push_back(sign * static_cast<float>(kMinD * (1.0 + j * 0x1p-23) * static_cast<double>(y)));
                b.push_back(y);                             // x / y = +-FLT_MIN * (1 + j 2^-23), to x's rounding
            }
        }
    }

    void binaryOps(Report& r)
    {
        std::vector<float> a0, b0;
        commonPairs(a0, b0);

        const auto check = [&](const char* name, char nearMin, auto op, auto ref, bool informative = false)
        {
            std::vector<float> a = a0, b = b0;
            if (nearMin != 0)
                nearMinPairs(nearMin, a, b);
            pad(a);
            pad(b);
            const std::size_t n = a.size();
            std::vector<float> got(n), want(n);
            Tally t;
            run2(op, a.data(), b.data(), got.data(), n);
            for (std::size_t i = 0; i < n; ++i)
                want[i] = ref(a[i], b[i]);
            tally(t, name, a.data(), b.data(), nullptr, got.data(), want.data(), n, false, informative);
            r.mismatches(name, t.bad, t.n, informative);
        };
        const f32x4 one = simd::set1(1.0f), zero = simd::set1(0.0f);
        check("ops.add", '+', [](f32x4 x, f32x4 y) noexcept { return simd::add(x, y); }, refAdd);
        check("ops.sub", '-', [](f32x4 x, f32x4 y) noexcept { return simd::sub(x, y); }, refSub);
        check("ops.mul", '*', [](f32x4 x, f32x4 y) noexcept { return simd::mul(x, y); }, refMul);
        check("ops.div", '/', [](f32x4 x, f32x4 y) noexcept { return simd::div(x, y); }, refDiv);
        check("ops.min", 0, [](f32x4 x, f32x4 y) noexcept { return simd::min(x, y); }, refMin);
        check("ops.max", 0, [](f32x4 x, f32x4 y) noexcept { return simd::max(x, y); }, refMax);
        // rsqrts is (3 - p*q) / 2 with ONE rounding: it is built on fms
        check("ops.rsqrts", 0, [](f32x4 x, f32x4 y) noexcept { return simd::rsqrts(x, y); }, refRsqrts, !kFmaExact);
        // comparisons and mask logic, read through sel
        check("ops.gt", 0, [&](f32x4 x, f32x4 y) noexcept { return simd::sel(simd::gt(x, y), one, zero); },
              [](float x, float y) noexcept { return x > y ? 1.0f : 0.0f; });
        check("ops.ge", 0, [&](f32x4 x, f32x4 y) noexcept { return simd::sel(simd::ge(x, y), one, zero); },
              [](float x, float y) noexcept { return x >= y ? 1.0f : 0.0f; });
        check("ops.sel_gt", 0, [](f32x4 x, f32x4 y) noexcept { return simd::sel(simd::gt(x, y), x, y); },
              [](float x, float y) noexcept { return x > y ? x : y; });
        check("ops.band", 0,
              [&](f32x4 x, f32x4 y) noexcept { return simd::sel(simd::band(simd::ge(x, zero), simd::gt(y, x)), x, y); },
              [](float x, float y) noexcept { return (x >= 0.0f && y > x) ? x : y; });
        check("ops.bor", 0,
              [&](f32x4 x, f32x4 y) noexcept
              {
                  return simd::sel(simd::bor(simd::gt(x, one), simd::ge(zero, y)), y, x);
              },
              [](float x, float y) noexcept { return (x > 1.0f || 0.0f >= y) ? y : x; });
    }

    // ---- 3. fma, fms ------------------------------------------------------------------------------------------------
    enum class Triples { edges, randomBits, nearbyScale, cancellation, midpoint, nearMin, mixed };

    // One triple of the kind into (a, b, c). No denormal operand.
    void makeTriple(Triples kind, Pcg32& rng, float& a, float& b, float& c) noexcept
    {
        switch (kind)
        {
            case Triples::edges:
                break;                                      // filled by the caller
            case Triples::randomBits:
                a = squash(rng.next());
                b = squash(rng.next());
                c = squash(rng.next());
                break;
            case Triples::nearbyScale:
            {
                // The addend within 2^60 of the product either way: the sum's 72 or more bits round in double.
                const int eb = static_cast<int>(rng.bounded(41u)) - 20, ec = static_cast<int>(rng.bounded(41u)) - 20;
                b = randomNormal(rng, eb, eb);
                c = randomNormal(rng, ec, ec);
                const int ea = eb + ec + static_cast<int>(rng.bounded(121u)) - 60;
                a = randomNormal(rng, ea, ea);
                break;
            }
            case Triples::cancellation:
            {
                b = randomNormal(rng, -20, 20);
                c = randomNormal(rng, -20, 20);
                const uint32_t p = bitsOf(-(b * c));        // the rounded product, a few units in the last place away
                a = fromBits(p + rng.bounded(7u) - 3u);
                break;
            }
            case Triples::midpoint:
            {
                // a = +-2^k (1 + m 2^-23) and b*c = +-t 2^(k-24) (1 + d), |d| <= 2^-24, t in {1/2, 1, 3/2, 3, 5}: the
                // sum lies within 2^-48 (relative) of a midpoint of two floats (t odd; t = 1/2 below a power of two),
                // on either side or on it. k = -126 puts it beside FLT_MIN, k = 127 beside the overflow threshold.
                const uint32_t pickK = rng.bounded(16u);
                const int k = pickK < 2u ? -126 : pickK == 2u ? -125 : pickK == 3u ? 127
                                                                     : static_cast<int>(rng.bounded(241u)) - 120;
                const uint32_t mantissa = (rng.next() & 3u) == 0u ? (rng.next() & 3u) : (rng.next() & 0x007fffffu);
                a = fromBits((rng.next() & 0x80000000u) | (static_cast<uint32_t>(k + 127) << 23) | mantissa);
                constexpr double kT[5] = { 0.5, 1.0, 1.5, 3.0, 5.0 };
                const double target = kT[rng.bounded(5u)] * pow2(k - 24);
                const int eb = (k - 24) / 2 + static_cast<int>(rng.bounded(21u)) - 10;
                b = randomNormal(rng, eb, eb);
                if ((rng.next() & 3u) == 0u)
                    b = fromBits(bitsOf(b) & 0xff800000u);  // a power of two: the product is t 2^(k-24) exactly
                c = static_cast<float>(target / static_cast<double>(b));
                break;
            }
            case Triples::nearMin:
            {
                // a = +-FLT_MIN u, u in [1, 4), and b*c = T - a to c's rounding, T = +-(FLT_MIN + h 2^-150), |h| <= 6:
                // sums on both sides of FLT_MIN, a few half units in the last place away.
                a = fromBits((rng.next() & 0x80000000u) | ((1u + rng.bounded(2u)) << 23) | (rng.next() & 0x007fffffu));
                const double sign = (rng.next() & 1u) != 0u ? -1.0 : 1.0;
                const double target = sign * (kMinD + (static_cast<int>(rng.bounded(13u)) - 6) * 0x1p-150);
                b = randomNormal(rng, -70, -55);
                c = squash(bitsOf(static_cast<float>((target - static_cast<double>(a)) / static_cast<double>(b))));
                break;
            }
            case Triples::mixed:
            {
                // One of the kinds above per lane, mostly ordinary ones: vectors in which some lanes take the exact
                // fma's slow path and their neighbours would not have (wasm computes all four lanes on one path).
                constexpr Triples kKinds[8] = { Triples::nearbyScale, Triples::nearbyScale, Triples::nearbyScale,
                                                Triples::cancellation, Triples::randomBits, Triples::randomBits,
                                                Triples::midpoint, Triples::nearMin };
                makeTriple(kKinds[rng.bounded(8u)], rng, a, b, c);
                break;
            }
        }
    }

    struct FmaCounts
    {
        long long n = 0;
        long long refVsStd = 0;             // the round-to-odd reference against std::fma (IEEE, no flush)
        long long doubleRounds = 0;         // triples where a multiply-add in double rounds to another float
        long long tinyResults = 0;          // results the flush rule turns into zero
        long long special = 0;              // infinite or NaN results
        Tally fma, fms, fmaVsStd, fmsVsStd;
    };

    void fmaChunk(FmaCounts& k, const std::vector<float>& a, const std::vector<float>& b, const std::vector<float>& c,
                  std::vector<float>& got, std::vector<float>& want, std::vector<float>& wantStd)
    {
        const std::size_t n = a.size();
        for (int which = 0; which < 2; ++which)
        {
            const bool minus = which == 1;
            for (std::size_t i = 0; i < n; ++i)
            {
                const float bb = minus ? -b[i] : b[i];
                const Fma f = refFma(a[i], bb, c[i]);
                const float lib = std::fma(bb, c[i], a[i]);
                const float twice = static_cast<float>(static_cast<double>(bb) * static_cast<double>(c[i])
                                                       + static_cast<double>(a[i]));
                want[i] = flushed(f.rounded, f.odd);
                wantStd[i] = flushed(lib, f.odd);
                k.refVsStd += !same(lib, f.rounded);
                k.doubleRounds += !same(twice, f.rounded);
                k.tinyResults += bitsOf(want[i]) != bitsOf(f.rounded) && !isNan(f.rounded);
                k.special += isNan(f.rounded) || isInf(f.rounded);
            }
            const float* pa = a.data();
            const float* pb = b.data();
            const float* pc = c.data();
            if (minus)
                run3([](f32x4 x, f32x4 y, f32x4 z) noexcept { return simd::fms(x, y, z); }, pa, pb, pc, got.data(), n);
            else
                run3([](f32x4 x, f32x4 y, f32x4 z) noexcept { return simd::fma(x, y, z); }, pa, pb, pc, got.data(), n);
            tally(minus ? k.fms : k.fma, minus ? "fms" : "fma", pa, pb, pc, got.data(), want.data(), n, false,
                  !kFmaExact);
            Tally& vsStd = minus ? k.fmsVsStd : k.fmaVsStd;
            for (std::size_t i = 0; i < n; ++i)
            {
                ++vsStd.n;
                vsStd.bad += !same(got[i], wantStd[i]);
            }
            k.n += static_cast<long long>(n);
        }
    }

    void fmaChecks(Report& r)
    {
        constexpr std::size_t kChunk = 16384;
        std::vector<float> a, b, c, got(kChunk), want(kChunk), wantStd(kChunk);
        FmaCounts total;

        const auto section = [&](const char* name, Triples kind, std::size_t count)
        {
            FmaCounts k;
            Pcg32 rng(0xf3a0u + static_cast<uint64_t>(kind), 17u);
            for (std::size_t done = 0; done < count; done += kChunk)
            {
                a.assign(kChunk, 1.0f);
                b.assign(kChunk, 1.0f);
                c.assign(kChunk, 1.0f);
                for (std::size_t i = 0; i < kChunk; ++i)
                    makeTriple(kind, rng, a[i], b[i], c[i]);
                fmaChunk(k, a, b, c, got, want, wantStd);
            }
            std::printf("NOTE  fma %-14s %9lld results (fma and fms): %lld where a double multiply-add rounds twice, "
                        "%lld flushed, %lld infinite or NaN\n", name, k.n, k.doubleRounds, k.tinyResults, k.special);
            total.n += k.n;
            total.refVsStd += k.refVsStd;
            total.doubleRounds += k.doubleRounds;
            constexpr Tally FmaCounts::* kTallies[4] = { &FmaCounts::fma, &FmaCounts::fms, &FmaCounts::fmaVsStd,
                                                         &FmaCounts::fmsVsStd };
            for (const auto m : kTallies)
            {
                (total.*m).n += (k.*m).n;
                (total.*m).bad += (k.*m).bad;
            }
        };

        // every triple of edges
        {
            const std::vector<float> E = edgeValues(false);
            a.clear();
            b.clear();
            c.clear();
            for (const float x : E)
                for (const float y : E)
                    for (const float z : E)
                    {
                        a.push_back(x);
                        b.push_back(y);
                        c.push_back(z);
                    }
            pad(a);
            pad(b);
            pad(c);
            got.resize(a.size());
            want.resize(a.size());
            wantStd.resize(a.size());
            FmaCounts k;
            fmaChunk(k, a, b, c, got, want, wantStd);
            std::printf("NOTE  fma %-14s %9lld results (fma and fms): %lld where a double multiply-add rounds twice, "
                        "%lld flushed, %lld infinite or NaN\n", "edges", k.n, k.doubleRounds, k.tinyResults, k.special);
            total = k;
            got.resize(kChunk);
            want.resize(kChunk);
            wantStd.resize(kChunk);
        }
        section("random bits", Triples::randomBits, 3000000);
        section("nearby scale", Triples::nearbyScale, 2500000);
        section("cancellation", Triples::cancellation, 1500000);
        section("midpoints", Triples::midpoint, 3000000);
        section("near FLT_MIN", Triples::nearMin, 1000000);
        section("mixed lanes", Triples::mixed, 1500000);

        // The reference is std::fma: natively the hardware instruction, which must agree everywhere. On wasm std::fma
        // is musl's software fmaf, which takes its halfway test from the normal range and so rounds some denormal
        // results twice; none of those survives the flush except beside FLT_MIN. Reported there, not judged.
#if defined(FCDSP_SIMD_WASM)
        std::printf("NOTE  %-44s %lld of %lld differ (musl's fmaf against the round-to-odd reference, IEEE results)\n",
                    "fma.reference_vs_std_fma", total.refVsStd, total.n);
        const bool stdIsHardware = false;
#else
        r.mismatches("fma.reference_vs_std_fma", total.refVsStd, total.n);
        const bool stdIsHardware = true;
#endif
        r.eq("fma.triples_at_least_1e7", total.fma.n >= 10000000 ? 1 : 0, 1);
        r.mismatches("fma.exact", total.fma.bad, total.fma.n, !kFmaExact);
        r.mismatches("fms.exact", total.fms.bad, total.fms.n, !kFmaExact);
        const char* why = !kFmaExact ? "the unfused fma rounds twice: not judged in this build"
                                     : "std::fma is musl's fmaf here (see fma.reference_vs_std_fma): not judged";
        r.mismatches("fma.vs_std_fma", total.fmaVsStd.bad, total.fmaVsStd.n, !kFmaExact || !stdIsHardware, why);
        r.mismatches("fms.vs_std_fma", total.fmsVsStd.bad, total.fmsVsStd.n, !kFmaExact || !stdIsHardware, why);
        std::printf("NOTE  fma: %lld of the %lld results are ones a multiply-add in double gets wrong\n",
                    total.doubleRounds, total.n);
    }

    // ---- 4. named rows: IEEE semantics, the flush rule, the min/max policy ------------------------------------------
    enum class Op { add, sub, mul, div, fma, fms, min, max, sqrt, floor, rsqrte, rsqrts };

    [[gnu::noinline]] uint32_t eval(Op op, float a, float b = 0.0f, float c = 0.0f) noexcept
    {
        alignas(16) float in[12], out[4];
        for (int i = 0; i < 4; ++i)
        {
            in[i] = a;
            in[4 + i] = b;
            in[8 + i] = c;
        }
        const float* volatile hidden = in;                  // the operands are not constants to the compiler
        const float* p = hidden;
        {
            const fcdsp::ScopedFtz ftz;
            const f32x4 x = simd::load(p), y = simd::load(p + 4), z = simd::load(p + 8);
            f32x4 v = x;
            switch (op)
            {
                case Op::add:    v = simd::add(x, y); break;
                case Op::sub:    v = simd::sub(x, y); break;
                case Op::mul:    v = simd::mul(x, y); break;
                case Op::div:    v = simd::div(x, y); break;
                case Op::fma:    v = simd::fma(x, y, z); break;
                case Op::fms:    v = simd::fms(x, y, z); break;
                case Op::min:    v = simd::min(x, y); break;
                case Op::max:    v = simd::max(x, y); break;
                case Op::sqrt:   v = simd::sqrt(x); break;
                case Op::floor:  v = simd::floor(x); break;
                case Op::rsqrte: v = simd::rsqrte(x); break;
                case Op::rsqrts: v = simd::rsqrts(x, y); break;
            }
            simd::store(out, v);
        }
        return bitsOf(out[2]);
    }

    void namedRows(Report& r)
    {
        const float qnan = fromBits(0x7fc00000u);
        const float b = 1.0f + 0x1p-12f;                    // b*b = 1 + 2^-11 + 2^-24: the 2^-24 needs one rounding
        if (kFmaExact)
        {
            r.bits("ops.fma.fused", eval(Op::fma, -(1.0f + 0x1p-11f), b, b), bitsOf(0x1p-24f));
            r.bits("ops.fms.fused", eval(Op::fms, 1.0f + 0x1p-11f, b, b), bitsOf(-0x1p-24f));
        }
        else
            std::printf("NOTE  ops.fma.fused: -(1 + 2^-11) + (1 + 2^-12)^2 is %a in this build (0x1p-24 with one "
                        "rounding)\n", static_cast<double>(fromBits(eval(Op::fma, -(1.0f + 0x1p-11f), b, b))));
        r.bits("ops.fma.operand_order", eval(Op::fma, 1.0f, 2.0f, 3.0f), bitsOf(7.0f));
        r.bits("ops.fms.operand_order", eval(Op::fms, 10.0f, 2.0f, 3.0f), bitsOf(4.0f));
        r.bits("ops.div.correctly_rounded", eval(Op::div, 1.0f, 3.0f), 0x3eaaaaabu);
        r.bits("ops.sqrt.correctly_rounded", eval(Op::sqrt, 2.0f), 0x3fb504f3u);
        r.eq("ops.floor.values",
             eval(Op::floor, -0.5f) == bitsOf(-1.0f) && eval(Op::floor, -0.0f) == 0x80000000u
                 && eval(Op::floor, 2.5f) == bitsOf(2.0f) && eval(Op::floor, 0.99999994f) == 0u
                 && eval(Op::floor, -8388609.0f) == bitsOf(-8388609.0f) && eval(Op::floor, 1e30f) == bitsOf(1e30f),
             1);
        r.bits("ops.rsqrte.one", eval(Op::rsqrte, 1.0f), 0x3f7f8000u);                 // FRSQRTE: 0.998046875
        r.bits("ops.rsqrte.two", eval(Op::rsqrte, 2.0f), 0x3f348000u);
        r.eq("ops.rsqrte.specials",
             eval(Op::rsqrte, 0.0f) == 0x7f800000u && eval(Op::rsqrte, -0.0f) == 0xff800000u
                 && isNanBits(eval(Op::rsqrte, -1.0f)) && eval(Op::rsqrte, kInf) == 0u,
             1);
        r.eq("ops.rsqrts.values",
             eval(Op::rsqrts, 2.0f, 0.5f) == bitsOf(1.0f) && eval(Op::rsqrts, 0.0f, kInf) == bitsOf(1.5f)
                 && eval(Op::rsqrts, -kInf, -0.0f) == bitsOf(1.5f)
                 && eval(Op::rsqrts, -kFltMax, 2.0f) == bitsOf(kFltMax),               // (3 + 2 MAX) / 2
             1);

        // The flush rule. FLT_MIN itself and everything above it pass through; a NaN and an infinity are not "tiny".
        r.bits("tiny.mul.flushed", eval(Op::mul, kFltMin, 0.5f), 0u);
        r.bits("tiny.mul.sign_kept", eval(Op::mul, -kFltMin, 0.5f), 0x80000000u);
        r.bits("tiny.div.flushed", eval(Op::div, kFltMin, -2.0f), 0x80000000u);
        if (kFmaExact)                                      // the unfused form flushes the product first: other values
        {
            r.bits("tiny.fma.flushed", eval(Op::fma, kFltMin, -kFltMin, 0.5f), 0u);    // FLT_MIN / 2, exactly
            r.bits("tiny.fms.flushed", eval(Op::fms, -kFltMin, -kFltMin, 0.25f), 0x80000000u);
            r.bits("tiny.fma.inexact_flushed", eval(Op::fma, 0.0f, 0x1.fffffep-64f, 0x1.fffffep-64f), 0u);
            r.bits("tiny.fma.min_kept", eval(Op::fma, -kFltMin, kFltMin, 2.0f), bitsOf(kFltMin));
        }
        r.bits("tiny.add.flushed", eval(Op::add, 1.5f * kFltMin, -kFltMin), kFlushSums ? 0u : 0x00400000u);
        r.bits("tiny.sub.flushed", eval(Op::sub, -1.5f * kFltMin, -kFltMin), kFlushSums ? 0x80000000u : 0x80400000u);
        r.bits("tiny.exact_min_kept", eval(Op::mul, 2.0f * kFltMin, 0.5f), bitsOf(kFltMin));
        r.bits("tiny.above_min_kept", eval(Op::sub, 3.0f * kFltMin, kFltMin), bitsOf(2.0f * kFltMin));
        r.eq("tiny.nan_and_inf_kept",
             isNanBits(eval(Op::mul, qnan, kFltMin)) && eval(Op::mul, kInf, kFltMin) == bitsOf(kInf)
                 && isNanBits(eval(Op::fma, qnan, 1.0f, 1.0f)) && eval(Op::add, -kInf, 1.0f) == bitsOf(-kInf),
             1);
        // FLT_MIN (1 - 2^-24) is exactly halfway below FLT_MIN and rounds up to it: flushed where the mode decides
        // before rounding (arm64), kept where it decides after (x86-64, wasm). Simd.h documents the difference.
        const uint32_t upToMin = kTinyBeforeRounding ? 0u : bitsOf(kFltMin);
        r.bits("tiny.mul.rounds_up_to_min", eval(Op::mul, kFltMin, fromBits(0x3f7fffffu)), upToMin);
        if (kFmaExact)
            r.bits("tiny.fma.rounds_up_to_min", eval(Op::fma, 0.0f, kFltMin, fromBits(0x3f7fffffu)), upToMin);

        // min/max: the operand that may be NaN goes second and propagates on every backend; the rest is the backend's.
        const auto nan01 = [](uint32_t v) { return isNanBits(v) ? 1 : 0; };
        const auto sign01 = [](uint32_t v) { return static_cast<int>(v >> 31); };
        r.eq("minmax.nan_second_propagates", nan01(eval(Op::min, 1.0f, qnan)) + nan01(eval(Op::max, 1.0f, qnan)), 2);
        r.eq("minmax.nan_first_propagates", nan01(eval(Op::min, qnan, 1.0f)) + nan01(eval(Op::max, qnan, 1.0f)),
             kMinMaxNeon ? 2 : 0);
        r.eq("minmax.min.neg0_pos0.sign", sign01(eval(Op::min, -0.0f, 0.0f)), kMinMaxNeon ? 1 : 0);
        r.eq("minmax.min.pos0_neg0.sign", sign01(eval(Op::min, 0.0f, -0.0f)), 1);
        r.eq("minmax.max.neg0_pos0.sign", sign01(eval(Op::max, -0.0f, 0.0f)), 0);
        r.eq("minmax.max.pos0_neg0.sign", sign01(eval(Op::max, 0.0f, -0.0f)), kMinMaxNeon ? 0 : 1);
    }

    // ---- 5. FastMath: fixed sweeps of normal numbers, hashed --------------------------------------------------------
    // Recorded from the native arm64 build (cmake --preset dsp; fcmp_web_check simd). x86-64 and wasm must reproduce
    // them.
    constexpr uint64_t kHashLog2 = 0x32581895e39f32c9ull, kHashExp2 = 0x6d733e5c294b53b8ull;
    constexpr uint64_t kHashTanh = 0xef7e178ae8745fadull, kHashLogCosh = 0x701667a48cdce8c9ull;
    constexpr uint64_t kHashDb = 0xaa89460e8a56e57dull, kHashTanPi = 0x065c8317bc347363ull;
    constexpr uint64_t kHashSinPi = 0xdd3e423eda790bbdull, kHashCosPi = 0xbc5d1b3eaedc1f85ull;

    // FNV-1a (64-bit) over the results' bit patterns, low byte first; any NaN counts as 0x7fc00000.
    uint64_t hashOf(const std::vector<float>& v) noexcept
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (const float x : v)
        {
            const uint32_t b = isNan(x) ? 0x7fc00000u : bitsOf(x);
            for (int k = 0; k < 4; ++k)
                h = (h ^ ((b >> (8 * k)) & 0xffu)) * 0x100000001b3ull;
        }
        return h;
    }

    // `perExponent` random mantissas for each biased exponent in [ebLo, ebHi], both signs when asked, plus each
    // binade's first and last float. Integer arithmetic only: the same inputs everywhere.
    std::vector<float> sweep(uint32_t ebLo, uint32_t ebHi, int perExponent, bool bothSigns, float maxAbs, uint64_t seed)
    {
        std::vector<float> v;
        Pcg32 rng(seed, 23u);
        const auto put = [&](uint32_t bits)
        {
            const float x = fromBits(bits);
            if (!(x > maxAbs))
            {
                v.push_back(x);
                if (bothSigns)
                    v.push_back(-x);
            }
        };
        for (uint32_t eb = ebLo; eb <= ebHi; ++eb)
        {
            put(eb << 23);
            put((eb << 23) | 0x007fffffu);
            for (int i = 0; i < perExponent; ++i)
                put((eb << 23) | (rng.next() & 0x007fffffu));
        }
        pad(v);
        return v;
    }

    template <class F>
    uint64_t hashVector(F f, const std::vector<float>& in)
    {
        std::vector<float> out(in.size());
        run1(f, in.data(), out.data(), in.size());
        return hashOf(out);
    }
    template <class F>
    [[gnu::noinline]] uint64_t hashScalar(F f, const std::vector<float>& in)
    {
        std::vector<float> out(in.size());
        {
            const fcdsp::ScopedFtz ftz;
            for (std::size_t i = 0; i < in.size(); ++i)
                out[i] = f(in[i]);
        }
        return hashOf(out);
    }

    void fastMath(Report& r)
    {
        const auto row = [&](const char* name, uint64_t got, uint64_t want, std::size_t count)
        {
            if (kFmaExact)
                r.bits(name, got, want);
            else
                std::printf("NOTE  %-44s 0x%llx over %zu values; the native value is 0x%llx (%s)\n", name,
                            static_cast<unsigned long long>(got), count, static_cast<unsigned long long>(want),
                            got == want ? "equal" : "differs: the unfused fma rounds twice, not judged in this build");
        };
        // log2: every normal exponent, far beyond what the engine meets (callers floor at 1e-12 and 1e-24), either sign
        const std::vector<float> xl = sweep(1u, 254u, 2048, true, kFltMax, 0x10620u);
        row("fastmath.log2.hash", hashVector([](f32x4 v) noexcept { return fcdsp::log2(v); }, xl), kHashLog2,
            xl.size());
        // exp2: |x| from 2^-40 to beyond the clamp at 126
        const std::vector<float> xe = sweep(87u, 134u, 4096, true, 200.0f, 0xe4b2u);
        row("fastmath.exp2.hash", hashVector([](f32x4 v) noexcept { return fcdsp::exp2(v); }, xe), kHashExp2,
            xe.size());
        const std::vector<float> xt = sweep(87u, 132u, 4096, true, 64.0f, 0x7a2bu);
        row("fastmath.tanh.hash", hashVector([](f32x4 v) noexcept { return fcdsp::tanh(v); }, xt), kHashTanh,
            xt.size());
        const std::vector<float> xc = sweep(97u, 132u, 4096, true, 64.0f, 0x10c05bu);
        row("fastmath.logcosh.hash", hashVector([](f32x4 v) noexcept { return fcdsp::logCosh(v); }, xc), kHashLogCosh,
            xc.size());
        // the dB helpers: log2 and exp2 behind the floors and a multiply
        const std::vector<float> xd = sweep(40u, 150u, 512, true, kFltMax, 0xdbu);
        row("fastmath.db.hash",
            hashVector([](f32x4 v) noexcept
                       {
                           const f32x4 db = simd::add(fcdsp::dbFromLin(v), fcdsp::dbFromMs(simd::abs(v)));
                           return simd::add(db, fcdsp::linFromDb(simd::mul(simd::set1(0.01f), db)));
                       }, xd),
            kHashDb, xd.size());
        const std::vector<float> xp = sweep(87u, 125u, 1024, false, 0.499f, 0x7a21u);
        row("fastmath.tanpi.hash", hashScalar([](float x) noexcept { return fcdsp::tanPi(x); }, xp), kHashTanPi,
            xp.size());
        const std::vector<float> xs = sweep(87u, 127u, 1024, true, 1.0f, 0x51a1u);
        row("fastmath.sinpi.hash", hashScalar([](float x) noexcept { return fcdsp::sinPi(x); }, xs), kHashSinPi,
            xs.size());
        row("fastmath.cospi.hash", hashScalar([](float x) noexcept { return fcdsp::cosPi(x); }, xs), kHashCosPi,
            xs.size());

        // every scalar form is lane 0 of its vector form (FastMath.h)
        long long bad = 0, count = 0;
        {
            const fcdsp::ScopedFtz ftz;
            for (std::size_t i = 0; i + 4 <= xt.size(); i += 64, count += 4)
            {
                const float x = xt[i];
                bad += bitsOf(fcdsp::log2(x)) != bitsOf(simd::lane<0>(fcdsp::log2(simd::set1(x))));
                bad += bitsOf(fcdsp::exp2(x)) != bitsOf(simd::lane<0>(fcdsp::exp2(simd::set1(x))));
                bad += bitsOf(fcdsp::tanh(x)) != bitsOf(simd::lane<0>(fcdsp::tanh(simd::set1(x))));
                bad += bitsOf(fcdsp::logCosh(x)) != bitsOf(simd::lane<0>(fcdsp::logCosh(simd::set1(x))));
            }
        }
        r.mismatches("fastmath.scalar_is_lane0", bad, count);
        r.eq("fastmath.exact_values",
             fcdsp::log2(1.0f) == 0.0f && fcdsp::log2(8.0f) == 3.0f && fcdsp::exp2(0.0f) == 1.0f
                 && fcdsp::exp2(-3.0f) == 0.125f && fcdsp::exp2(200.0f) == 0x1p126f && fcdsp::tanh(20.0f) == 1.0f
                 && fcdsp::sinPi(0.5f) == 1.0f && fcdsp::cosPi(1.0f) == -1.0f && fcdsp::tanPi(0.0f) == 0.0f,
             1);
    }
} // namespace

FCMP_WEB_COMMAND(simd)
{
    Report r;
    std::printf("NOTE  web.simd: %s; fma %s; a tiny result is flushed %s rounding; add and sub flush: %s\n", kBackend,
                kFmaExact ? "exact (one rounding)" : "UNFUSED (two roundings: the measured fallback)",
                kTinyBeforeRounding ? "before" : "after", kFlushSums ? "yes" : "no");
    moves(r);
    unaryOps(r);
    binaryOps(r);
    fmaChecks(r);
    namedRows(r);
    fastMath(r);
    std::printf("%s  web.simd: %d rows, %d failed\n", r.failed == 0 ? "PASS" : "FAIL", r.rows, r.failed);
    return r.failed == 0 ? 0 : 1;
}
