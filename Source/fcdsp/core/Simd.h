#pragma once

// fcdsp::simd: the 4-lane float vector every stage runs on (01 §5.1, E §3.2). It extends HardwareReverb's Simd.h:
// a set of thin inline forwarders over a type ALIAS (not a wrapper class), so on arm64 `f32x4` IS `float32x4_t` and
// each op is one intrinsic. Lanes are {ch0, ch1, aux0, aux1}: lanes 0-1 the channels (L/R, or M/S after encoding),
// lanes 2-3 independent recurrences (crest detectors, the stage-2 detector, a second link detector). Serial chains are
// never packed.
//
// Frozen at FZ0 (names and signatures). S0 (F0) declares the op set; F1 (S1) adds the NEON and SSE4.1/AVX2 bodies,
// with every op bit-identical across the two backends (dsp.simd). Rules the bodies keep:
//   - fma(a, b, c) = a + b*c and fms(a, b, c) = a - b*c, fused (one rounding) on both backends: the accumulator is
//     the FIRST argument (NEON vfmaq_f32 order; x86 _mm_fmadd_ps(b, c, a)).
//   - rsqrte is NEON's FRSQRTE on both backends (x86 computes the table function; HR measured why rsqrtps is not
//     acceptable); rsqrts(p, q) = (3 - p*q) / 2 fused.
//   - min/max NaN semantics differ between NEON and x86. Input sanitisation (Sanitize.h), the log floors and the
//     poison check keep NaN out of the engine; dsp.simd pins the policy.
//   - Comparisons return a lane mask `m32x4` (all-ones or all-zeros per lane) that sel/band/bor consume.
//   - Every op is FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata): the bodies are intrinsics only.
// The x86-64 slice requires FMA: an unfused fallback would be a different arithmetic, not a slower one.

#include "fcdsp/core/Rt.h"
#include <cstdint>
#include <limits>

#if defined(__aarch64__) || defined(__ARM_NEON)
  #include <arm_neon.h>
  #define FCDSP_SIMD_NEON 1
#elif defined(__x86_64__) || defined(_M_X64)
  #include <immintrin.h>
  #define FCDSP_SIMD_SSE 1
  #if !defined(__FMA__) && !(defined(_MSC_VER) && !defined(__clang__) && defined(__AVX2__))
    #error "fcdsp's x86-64 build needs FMA: fcmp_flags adds -mavx2 -mfma (03 §2.6)."
  #endif
#elif defined(__wasm_simd128__)
  #include "fcdsp/core/FlushTiny.h"
  #include <wasm_simd128.h>
  #define FCDSP_SIMD_WASM 1
#else
  #error "fcdsp: no SIMD backend for this architecture (arm64 NEON, x86-64 SSE4.1/AVX2 with FMA, or wasm32 SIMD128)."
#endif

namespace fcdsp::simd {

#if defined(FCDSP_SIMD_NEON)
using f32x4 = float32x4_t;                  // four float lanes
using m32x4 = uint32x4_t;                   // per-lane mask: all-ones = true
#elif defined(FCDSP_SIMD_WASM)
typedef float f32x4 __attribute__((__vector_size__(16), __aligned__(16)));  // a float vector, as wasm_simd128.h's own
using m32x4 = v128_t;                       // per-lane mask: all-ones = true (an int32 vector: a distinct type)
#else
using f32x4 = __m128;
using m32x4 = __m128;                       // SSE compares produce float-typed masks
#endif

// Memory and construction (unaligned access on both backends).
inline f32x4 load (const float* p) noexcept FCDSP_NONBLOCKING;
inline void  store(float* p, f32x4 v) noexcept FCDSP_NONBLOCKING;
inline f32x4 set1 (float s) noexcept FCDSP_NONBLOCKING;

// Arithmetic. div and sqrt are IEEE correctly rounded, so bit-equal across arches.
inline f32x4 add(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;
inline f32x4 sub(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;
inline f32x4 mul(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;
inline f32x4 div(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING;     // a + b*c, one rounding
inline f32x4 fms(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING;     // a - b*c, one rounding
inline f32x4 min(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;
inline f32x4 max(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;
inline f32x4 abs(f32x4 a) noexcept FCDSP_NONBLOCKING;                       // exact (sign bit cleared)
inline f32x4 neg(f32x4 a) noexcept FCDSP_NONBLOCKING;                       // exact (sign bit flipped)
inline f32x4 sqrt(f32x4 a) noexcept FCDSP_NONBLOCKING;
inline f32x4 floor(f32x4 a) noexcept FCDSP_NONBLOCKING;                     // exact (round toward -inf)

// Newton-Raphson 1/sqrt halves, as the hardware exposes them: r0 = rsqrte(x); r1 = r0 * rsqrts(r0*x, r0).
inline f32x4 rsqrte(f32x4 x) noexcept FCDSP_NONBLOCKING;
inline f32x4 rsqrts(f32x4 p, f32x4 q) noexcept FCDSP_NONBLOCKING;

// Comparison, selection and mask logic.
inline m32x4 gt  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;             // a > b, per lane
inline m32x4 ge  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING;             // a >= b, per lane
inline f32x4 sel (m32x4 mask, f32x4 t, f32x4 f) noexcept FCDSP_NONBLOCKING; // mask ? t : f, per lane
inline m32x4 band(m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING;             // mask AND
inline m32x4 bor (m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING;             // mask OR

// Lane access; I in [0, 3].
template <int I> inline float lane(f32x4 v) noexcept FCDSP_NONBLOCKING;
template <int I> inline f32x4 withLane(f32x4 v, float s) noexcept FCDSP_NONBLOCKING;

} // namespace fcdsp::simd

// ==== Bodies (F1, S1) ================================================================================================
//
// Every op is bit-identical on the two backends for every input, with four documented exceptions (dsp.simd pins the
// first two per backend and leaves all four out of its xarch hash):
//   - min/max with a NaN operand: NEON returns NaN; x86 returns the SECOND operand (minps/maxps). Put the operand that
//     may be NaN second (max(set1(floor), x)) and a NaN propagates on both.
//   - min/max of +0 and -0: NEON orders -0 < +0 (min -> -0, max -> +0); x86 returns the second operand.
//   - NaN payloads and signs are not specified anywhere (compilers commute operands; x86 FMA picks its NaN by
//     encoding), so dsp.simd hashes every NaN as one canonical NaN.
//   - Flush-to-zero decides tininess before rounding on arm64 (FZ, Arm ARM FPRound) and after rounding on x86 (FTZ),
//     where "rounding" is to 24 bits with no lower bound on the exponent: a mul or fma whose exact result lies in
//     [FLT_MIN (1 - 2^-25), FLT_MIN) is 0 on arm64 and FLT_MIN on x86. Below that range both give 0, including the
//     results gradual underflow would round up to FLT_MIN (web.simd holds the rule on each backend).
// Denormals follow the FP mode, which fcdsp always runs under (ScopedFtz: FPCR.FZ on arm64, MXCSR FTZ|DAZ on x86):
// arithmetic flushes denormal inputs and outputs on both; load/store/set1/abs/neg/sel/lane move bits untouched. The
// compiler folds constant expressions in the default mode (no flush), so never rely on FTZ for constant operands.
// The wasm backend (ADR-93; its own comment below) has no FP mode at all: its fma and fms round once like the others,
// its min/max follow x86, its ops flush tiny results themselves by x86's rule, and it reads a denormal operand as the
// value it is.
//
// rsqrte/rsqrts are AArch64 FRSQRTE/FRSQRTS on both backends. x86 computes FRSQRTE's table function per lane
// (detail::frsqrteBits, from the Arm ARM's FPRSqrtEstimate/RecipSqrtEstimate; HR measured why rsqrtps, a different
// 12-bit estimate, is not acceptable) and FRSQRTS as detail::rsqrtsPortable, a composition of the ops above that is
// exact against the instruction including its (0 x inf) -> 1.5 case. Both helpers compile on arm64 too, where
// dsp.simd checks them against the hardware instructions, so the x86 code is exercised on a Mac without Rosetta.

namespace fcdsp::simd {

#if defined(FCDSP_SIMD_NEON)

inline f32x4 load (const float* p) noexcept FCDSP_NONBLOCKING    { return vld1q_f32(p); }
inline void  store(float* p, f32x4 v) noexcept FCDSP_NONBLOCKING { vst1q_f32(p, v); }
inline f32x4 set1 (float s) noexcept FCDSP_NONBLOCKING           { return vdupq_n_f32(s); }

inline f32x4 add(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vaddq_f32(a, b); }
inline f32x4 sub(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vsubq_f32(a, b); }
inline f32x4 mul(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vmulq_f32(a, b); }
inline f32x4 div(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vdivq_f32(a, b); }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return vfmaq_f32(a, b, c); }
inline f32x4 fms(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return vfmsq_f32(a, b, c); }
inline f32x4 min(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vminq_f32(a, b); }
inline f32x4 max(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vmaxq_f32(a, b); }
inline f32x4 abs(f32x4 a) noexcept FCDSP_NONBLOCKING                   { return vabsq_f32(a); }
inline f32x4 neg(f32x4 a) noexcept FCDSP_NONBLOCKING                   { return vnegq_f32(a); }
inline f32x4 sqrt(f32x4 a) noexcept FCDSP_NONBLOCKING                  { return vsqrtq_f32(a); }
inline f32x4 floor(f32x4 a) noexcept FCDSP_NONBLOCKING                 { return vrndmq_f32(a); }

inline f32x4 rsqrte(f32x4 x) noexcept FCDSP_NONBLOCKING          { return vrsqrteq_f32(x); }
inline f32x4 rsqrts(f32x4 p, f32x4 q) noexcept FCDSP_NONBLOCKING { return vrsqrtsq_f32(p, q); }

inline m32x4 gt  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vcgtq_f32(a, b); }
inline m32x4 ge  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING          { return vcgeq_f32(a, b); }
inline f32x4 sel (m32x4 mask, f32x4 t, f32x4 f) noexcept FCDSP_NONBLOCKING { return vbslq_f32(mask, t, f); }
inline m32x4 band(m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING          { return vandq_u32(a, b); }
inline m32x4 bor (m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING          { return vorrq_u32(a, b); }

template <int I> inline float lane(f32x4 v) noexcept FCDSP_NONBLOCKING
{
    static_assert(I >= 0 && I < 4, "lane index out of range");
    return vgetq_lane_f32(v, I);
}
template <int I> inline f32x4 withLane(f32x4 v, float s) noexcept FCDSP_NONBLOCKING
{
    static_assert(I >= 0 && I < 4, "lane index out of range");
    return vsetq_lane_f32(s, v, I);
}

#elif defined(FCDSP_SIMD_WASM) // WASM SIMD128 (ADR-93): fixed-width SIMD only, never relaxed SIMD

// Every op is one wasm instruction, as on NEON, with three exceptions the instruction set forces:
//   - fma/fms. WASM SIMD128 has no fused multiply-add (relaxed SIMD's is implementation-defined), and the contract is
//     ONE rounding. detail::fmaExact computes it from operations wasm has, two lanes at a time in f64x2. The product
//     b*c is exact there (24 x 24 bits in 53), so s = RN(b*c + a) in double has rounded once. Rounding s to float
//     rounds a second time, and that is wrong in one case only: s lies exactly halfway between two floats although
//     the sum was not exact, so the tie breaks on s where it should break on the sum. The fast path is therefore the
//     double multiply-add, the conversion and a test: is any lane's s a float midpoint (the double's low 29 bits are
//     0x10000000), or is any result non-zero and no larger than FLT_MIN (below FLT_MIN the midpoints sit elsewhere,
//     and the flush rule decides)? Only then the slow path runs, for all four lanes: TwoSum gives the sum's rounding
//     error exactly, s is re-rounded to odd (truncated, with a sticky last bit), and rounding THAT to float is the
//     correctly rounded a + b*c for every input (53 >= 24 + 2; Boldo and Melquiond, "Emulation of a FMA and
//     correctly-rounded sums"), which is then flushed. The test is a superset (it also fires on an exact tie), the
//     result does not depend on which path ran, and there is no libm and no scalar loop. 24 SIMD operations and a
//     branch on the fast path, against 2 for a multiply and an add; 33 more on the slow one, which the engine takes
//     for a few per cent of its calls (sums of few-bit values tie exactly). The branch is there for latency: the
//     branch-free form (round to odd always, 46 operations) puts TwoSum on the critical path of every recurrence
//     and Horner chain. Tools/web/simd.cpp (web.simd) holds the result to the hardware instruction's bits.
//     FCDSP_WASM_FMA_UNFUSED (cmake -DFCOMPRESSOR_WEB_FMA=unfused) replaces it with the multiply and the add: two
//     roundings, a different arithmetic, kept only to measure what the exact form costs.
//   - Flush-to-zero. wasm has no FP control register, so ScopedFtz is empty there and the ops do the output half
//     themselves: a tiny result of add, sub, mul, div, fma or fms is a signed zero, and tiny is the x86 rule above,
//     decided on the exact result where the float does not say (detail::flushTinyExact, FlushTiny.h; a few more
//     operations, on the slow path only). There is no denormals-are-zero half: a denormal OPERAND is read as the
//     value it is (it can only come from outside the ops: the web engine zeroes denormal input samples). sqrt and
//     floor of a normal number are never tiny; rsqrte reads its operand as it is.
//   - min/max are pmin/pmax with the operands swapped, which is x86's rule exactly: min(a, b) = a < b ? a : b and
//     max(a, b) = a > b ? a : b, so the second operand is returned for a NaN and for +0 against -0.
// sel is v128.bitselect and needs no fence: nothing here depends on an FP mode. NaN payloads are unspecified, as
// everywhere.

namespace detail {

inline v128_t asBits(f32x4 v) noexcept FCDSP_NONBLOCKING { return (v128_t) v; }     // the same 128 bits
inline f32x4  asF32 (v128_t v) noexcept FCDSP_NONBLOCKING { return (f32x4) v; }

// Two f64x2 (lanes 0-1 and 2-3) rounded to one f32x4.
inline v128_t toFloats(v128_t lo, v128_t hi) noexcept FCDSP_NONBLOCKING
{
    return wasm_i32x4_shuffle(wasm_f32x4_demote_f64x2_zero(lo), wasm_f32x4_demote_f64x2_zero(hi), 0, 1, 4, 5);
}

// s = RN(p + a) in f64x2, re-rounded to odd: s itself where the sum was exact (or is infinite or NaN), else the
// neighbour of the exact sum whose last bit is set.
inline v128_t roundToOdd(v128_t a, v128_t p, v128_t s) noexcept FCDSP_NONBLOCKING
{
    const v128_t t = wasm_f64x2_sub(s, p);                              // TwoSum (Knuth): e = (p + a) - s, exactly
    const v128_t e = wasm_f64x2_add(wasm_f64x2_sub(p, wasm_f64x2_sub(s, t)), wasm_f64x2_sub(a, t));
    const v128_t zero = wasm_f64x2_const_splat(0.0);
    const v128_t up = wasm_f64x2_gt(e, zero), down = wasm_f64x2_lt(e, zero);    // neither: exact, or s is inf or NaN
    // The exact sum lies between s and zero when e and s have opposite signs: one step down on s's bits truncates.
    const v128_t towardZero = wasm_v128_bitselect(up, down, wasm_f64x2_lt(s, zero));
    const v128_t truncated = wasm_i64x2_add(s, towardZero);             // the mask is -1
    return wasm_v128_or(truncated, wasm_u64x2_shr(wasm_v128_or(up, down), 63));  // inexact: the last bit is set
}

// fmaExact's slow path for all four lanes: the sums re-rounded to odd, rounded to float and flushed (odd lies on the
// exact sum's side of the tiny limit). Out of line: 40 operations that a few per cent of the calls need, at every one
// of the engine's multiply-adds, are most of what the exact form would add to the module's size.
[[gnu::noinline]] inline v128_t fmaExactSlow(v128_t aLo, v128_t aHi, v128_t pLo, v128_t pHi, v128_t sLo,
                                             v128_t sHi) noexcept FCDSP_NONBLOCKING
{
    const v128_t oLo = roundToOdd(aLo, pLo, sLo), oHi = roundToOdd(aHi, pHi, sHi);
    return flushTinyExact(toFloats(oLo, oHi), oLo, oHi);
}

inline f32x4 fmaExact(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING
{
    const v128_t av = asBits(a), bv = asBits(b), cv = asBits(c);
    const v128_t aLo = wasm_f64x2_promote_low_f32x4(av), aHi = wasm_f64x2_promote_low_f32x4(highPair(av));
    const v128_t pLo = wasm_f64x2_mul(wasm_f64x2_promote_low_f32x4(bv), wasm_f64x2_promote_low_f32x4(cv));
    const v128_t pHi = wasm_f64x2_mul(wasm_f64x2_promote_low_f32x4(highPair(bv)),
                                      wasm_f64x2_promote_low_f32x4(highPair(cv)));       // exact
    const v128_t sLo = wasm_f64x2_add(pLo, aLo), sHi = wasm_f64x2_add(pHi, aHi);         // RN(p + a)
    const v128_t r = toFloats(sLo, sHi);
    // The four doubles' low words: a float midpoint ends in 0x10000000 (bit 28 set, the 28 bits below it clear).
    const v128_t low = wasm_i32x4_shuffle(sLo, sHi, 0, 2, 4, 6);
    const v128_t midpoint = wasm_i32x4_eq(wasm_v128_and(low, wasm_i32x4_const_splat(0x1fffffff)),
                                          wasm_i32x4_const_splat(0x10000000));
    if (wasm_v128_any_true(wasm_v128_or(midpoint, tinyOrMin(r)))) [[unlikely]]
        return asF32(fmaExactSlow(aLo, aHi, pLo, pHi, sLo, sHi));
    return asF32(r);                                    // no lane is tiny: nothing to flush
}

// The measured fallback: the product rounds, then the sum rounds (each flushed, as mul and add are).
inline f32x4 fmaUnfused(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING
{
    const v128_t p = flushTinyProduct(wasm_f32x4_mul(asBits(b), asBits(c)), asBits(b), asBits(c));
    return asF32(flushTiny(wasm_f32x4_add(asBits(a), p)));
}

// add and sub: a tiny sum needs two tiny-ish operands that nearly cancel, so FCDSP_WASM_FLUSH_ADDSUB=0 may skip the
// flush (FlushTiny.h); the default keeps the rule on all six ops.
inline v128_t flushSum(v128_t r) noexcept FCDSP_NONBLOCKING
{
#if FCDSP_WASM_FLUSH_ADDSUB
    return flushTiny(r);
#else
    return r;
#endif
}

} // namespace detail

inline f32x4 load (const float* p) noexcept FCDSP_NONBLOCKING    { return detail::asF32(wasm_v128_load(p)); }
inline void  store(float* p, f32x4 v) noexcept FCDSP_NONBLOCKING { wasm_v128_store(p, detail::asBits(v)); }
inline f32x4 set1 (float s) noexcept FCDSP_NONBLOCKING           { return detail::asF32(wasm_f32x4_splat(s)); }

inline f32x4 add(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    return detail::asF32(detail::flushSum(wasm_f32x4_add(detail::asBits(a), detail::asBits(b))));
}
inline f32x4 sub(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    return detail::asF32(detail::flushSum(wasm_f32x4_sub(detail::asBits(a), detail::asBits(b))));
}
inline f32x4 mul(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    const v128_t x = detail::asBits(a), y = detail::asBits(b);
    return detail::asF32(detail::flushTinyProduct(wasm_f32x4_mul(x, y), x, y));
}
inline f32x4 div(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    const v128_t x = detail::asBits(a), y = detail::asBits(b);
    return detail::asF32(detail::flushTinyQuotient(wasm_f32x4_div(x, y), x, y));
}
#if defined(FCDSP_WASM_FMA_UNFUSED)
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return detail::fmaUnfused(a, b, c); }
inline f32x4 fms(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return detail::fmaUnfused(a, neg(b), c); }
#else
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return detail::fmaExact(a, b, c); }
inline f32x4 fms(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return detail::fmaExact(a, neg(b), c); }
#endif
// pmin(x, y) = y < x ? y : x and pmax(x, y) = x < y ? y : x: with (b, a) these are minps and maxps.
inline f32x4 min(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    return detail::asF32(wasm_f32x4_pmin(detail::asBits(b), detail::asBits(a)));
}
inline f32x4 max(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    return detail::asF32(wasm_f32x4_pmax(detail::asBits(b), detail::asBits(a)));
}
inline f32x4 abs(f32x4 a) noexcept FCDSP_NONBLOCKING   { return detail::asF32(wasm_f32x4_abs(detail::asBits(a))); }
inline f32x4 neg(f32x4 a) noexcept FCDSP_NONBLOCKING   { return detail::asF32(wasm_f32x4_neg(detail::asBits(a))); }
inline f32x4 sqrt(f32x4 a) noexcept FCDSP_NONBLOCKING  { return detail::asF32(wasm_f32x4_sqrt(detail::asBits(a))); }
inline f32x4 floor(f32x4 a) noexcept FCDSP_NONBLOCKING { return detail::asF32(wasm_f32x4_floor(detail::asBits(a))); }

inline m32x4 gt  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    return wasm_f32x4_gt(detail::asBits(a), detail::asBits(b));
}
inline m32x4 ge  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING
{
    return wasm_f32x4_ge(detail::asBits(a), detail::asBits(b));
}
inline f32x4 sel (m32x4 mask, f32x4 t, f32x4 f) noexcept FCDSP_NONBLOCKING
{
    return detail::asF32(wasm_v128_bitselect(detail::asBits(t), detail::asBits(f), mask));
}
inline m32x4 band(m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING { return wasm_v128_and(a, b); }
inline m32x4 bor (m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING { return wasm_v128_or(a, b); }

template <int I> inline float lane(f32x4 v) noexcept FCDSP_NONBLOCKING
{
    static_assert(I >= 0 && I < 4, "lane index out of range");
    return wasm_f32x4_extract_lane(detail::asBits(v), I);
}
template <int I> inline f32x4 withLane(f32x4 v, float s) noexcept FCDSP_NONBLOCKING
{
    static_assert(I >= 0 && I < 4, "lane index out of range");
    return detail::asF32(wasm_f32x4_replace_lane(detail::asBits(v), I, s));
}

#else // FCDSP_SIMD_SSE: SSE4.1 + FMA3 (the x86-64 slice builds with -mavx2 -mfma; VEX encodings throughout)

// Unaligned on purpose (HR Simd.h): with VEX encodings vmovups on aligned data costs what vmovaps does.
inline f32x4 load (const float* p) noexcept FCDSP_NONBLOCKING    { return _mm_loadu_ps(p); }
inline void  store(float* p, f32x4 v) noexcept FCDSP_NONBLOCKING { _mm_storeu_ps(p, v); }
inline f32x4 set1 (float s) noexcept FCDSP_NONBLOCKING           { return _mm_set1_ps(s); }

inline f32x4 add(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING { return _mm_add_ps(a, b); }
inline f32x4 sub(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING { return _mm_sub_ps(a, b); }
inline f32x4 mul(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING { return _mm_mul_ps(a, b); }
inline f32x4 div(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING { return _mm_div_ps(a, b); }
// The accumulator moves from NEON's first argument to x86's last: _mm_fmadd_ps(x, y, z) = x*y + z and
// _mm_fnmadd_ps(x, y, z) = -(x*y) + z, each with one rounding, so the product b*c is the same exact real on both.
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return _mm_fmadd_ps(b, c, a); }
inline f32x4 fms(f32x4 a, f32x4 b, f32x4 c) noexcept FCDSP_NONBLOCKING { return _mm_fnmadd_ps(b, c, a); }
inline f32x4 min(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING { return _mm_min_ps(a, b); }     // NaN/+-0: see above
inline f32x4 max(f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING { return _mm_max_ps(a, b); }
inline f32x4 abs(f32x4 a) noexcept FCDSP_NONBLOCKING          { return _mm_andnot_ps(_mm_set1_ps(-0.0f), a); }
inline f32x4 neg(f32x4 a) noexcept FCDSP_NONBLOCKING          { return _mm_xor_ps(a, _mm_set1_ps(-0.0f)); }
inline f32x4 sqrt(f32x4 a) noexcept FCDSP_NONBLOCKING         { return _mm_sqrt_ps(a); }
inline f32x4 floor(f32x4 a) noexcept FCDSP_NONBLOCKING        { return _mm_floor_ps(a); }

inline m32x4 gt  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING             { return _mm_cmpgt_ps(a, b); }
inline m32x4 ge  (f32x4 a, f32x4 b) noexcept FCDSP_NONBLOCKING             { return _mm_cmpge_ps(a, b); }
// blendv reads each mask lane's sign bit; an m32x4 is all-ones or all-zeros per lane, so this is NEON's bsl. The empty
// asm hides the mask's origin from the optimiser (ADR-92, found by the first x86 run, dsp.simd): Clang lowers
// sel(gt(a, b), a, b) to maxps (and the like to minps), equal under the default FP mode, but under DAZ maxps returns a
// denormal operand flushed to zero where bsl, and blendv, return its bits. Every spelling of the blend (float or
// integer blendv, and/andnot/or) is matched; the fence emits no instruction.
inline f32x4 sel (m32x4 mask, f32x4 t, f32x4 f) noexcept FCDSP_NONBLOCKING
{
    __asm__("" : "+x"(mask));
    return _mm_blendv_ps(f, t, mask);
}
inline m32x4 band(m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING             { return _mm_and_ps(a, b); }
inline m32x4 bor (m32x4 a, m32x4 b) noexcept FCDSP_NONBLOCKING             { return _mm_or_ps(a, b); }

template <int I> inline float lane(f32x4 v) noexcept FCDSP_NONBLOCKING
{
    static_assert(I >= 0 && I < 4, "lane index out of range");
    return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(I, I, I, I)));
}
template <int I> inline f32x4 withLane(f32x4 v, float s) noexcept FCDSP_NONBLOCKING
{
    static_assert(I >= 0 && I < 4, "lane index out of range");
    return _mm_insert_ps(v, _mm_set_ss(s), I << 4);
}

#endif

// ---- portable FRSQRTE / FRSQRTS (the x86 bodies; checked against the instructions on arm64 by dsp.simd) -------------
namespace detail {

// Arm ARM RecipSqrtEstimate(a) without FEAT_RPRES, 128 <= a < 512: the largest b with a * b^2 < 2^28 (the pseudocode's
// b = 512; while a*(b+1)*(b+1) < 2^28 do b = b+1), then r = (b + 1) DIV 2, in [256, 511].
constexpr uint32_t isqrtFloor(uint32_t n) noexcept FCDSP_NONBLOCKING
{
    uint32_t lo = 0, hi = 65536;                    // invariant: lo*lo <= n < hi*hi
    while (hi - lo > 1)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (static_cast<uint64_t>(mid) * mid <= n)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

constexpr uint32_t recipSqrtEstimate(uint32_t a) noexcept FCDSP_NONBLOCKING
{
    if (a < 256)
        a = a * 2 + 1;                              // 0.25 .. 0.5, in units of 1/512 rounded to nearest
    else
        a = ((a >> 1) << 1) * 2 + 2;                // 0.5 .. 1.0: bottom bit discarded, units of 1/256
    const uint32_t b = isqrtFloor(((1u << 28) - 1) / a);    // the largest b with a * b^2 <= 2^28 - 1
    return ((b < 512 ? 512 : b) + 1) / 2;
}

// FRSQRTE's 8-bit estimate, indexed by (biased exponent odd ? 0 : 128) + fraction<22:16>.
struct FrsqrteTable { uint8_t e[256]; };
constexpr FrsqrteTable makeFrsqrteTable() noexcept
{
    FrsqrteTable t{};
    for (uint32_t i = 0; i < 128; ++i)
    {
        t.e[i]       = static_cast<uint8_t>(recipSqrtEstimate(128 + i) - 256);      // odd exponent: '01':frac<22:16>
        t.e[128 + i] = static_cast<uint8_t>(recipSqrtEstimate(256 + 2 * i) - 256);  // even: '1':frac<22:15>, 15 dropped
    }
    return t;
}
inline constexpr FrsqrteTable kFrsqrte = makeFrsqrteTable();
static_assert(kFrsqrte.e[0] == 255 && kFrsqrte.e[128] == 105,
              "FRSQRTE(1.0f) = 0x3f7f8000 and FRSQRTE(2.0f) = 0x3f348000");

// FPRSqrtEstimate(operand) for binary32 with FPCR.AH = 0. flushDenormals is FPCR.FZ (x86: MXCSR.DAZ, the input half
// of flush-to-zero): a denormal input is then a signed zero. NaN -> the quietened input (FPCR.DN = 0), +-0 -> +-inf,
// a negative non-zero -> the default NaN, +inf -> +0.
constexpr uint32_t frsqrteBits(uint32_t x, bool flushDenormals) noexcept FCDSP_NONBLOCKING
{
    const uint32_t sign = x & 0x80000000u;
    uint32_t exp = (x >> 23) & 0xffu;
    uint32_t frac = x & 0x007fffffu;
    if (exp == 0xffu && frac != 0)
        return x | 0x00400000u;                     // NaN: quietened
    if (exp == 0 && (frac == 0 || flushDenormals))
        return sign | 0x7f800000u;                  // +-0 (or a flushed denormal): +-inf
    if (sign != 0)
        return 0x7fc00000u;                         // negative: default NaN
    if (exp == 0xffu)
        return 0;                                   // +inf: +0
    int e = static_cast<int>(exp);
    if (exp == 0)                                   // denormal, not flushed: normalise (the pseudocode's while loop)
    {
        while ((frac & 0x00400000u) == 0)
        {
            frac <<= 1;
            --e;
        }
        frac = (frac << 1) & 0x007fffffu;
    }
    const uint32_t idx = ((e & 1) != 0 ? 0u : 128u) + (frac >> 16);
    const int resultExp = (380 - e) / 2;            // 380 - e > 0, so / is the pseudocode's DIV
    return (static_cast<uint32_t>(resultExp) << 23) | (static_cast<uint32_t>(kFrsqrte.e[idx]) << 15);
}

// FPRSqrtStepFused(p, q) = (3 - p*q) / 2 with ONE rounding, (+-0 x +-inf) in either order -> +1.5. 1.5 - (p/2)*q is
// that value whenever p/2 is exact (|p| >= 2^-125, or p zero, infinite or NaN), and it cannot overflow where the
// instruction does not. Below 2^-125, |p*q| < 8, so (3 - p*q) rounded and then halved is exact: halving a result of
// magnitude >= 2^-46 (the smallest non-zero 3 - p*q can be there) never rounds. The zero and infinity tests use the FP
// mode, as the instruction does (a denormal compares equal to 0 under FZ / DAZ).
inline f32x4 rsqrtsPortable(f32x4 p, f32x4 q) noexcept FCDSP_NONBLOCKING
{
    const f32x4 zero = set1(0.0f), inf = set1(std::numeric_limits<float>::infinity());
    const f32x4 halfP = fms(set1(1.5f), mul(set1(0.5f), p), q);
    const f32x4 tinyP = mul(set1(0.5f), fms(set1(3.0f), p, q));
    const f32x4 r = sel(gt(set1(0x1p-125f), abs(p)), tinyP, halfP);
    const m32x4 zeroP = band(ge(p, zero), ge(zero, p)), zeroQ = band(ge(q, zero), ge(zero, q));
    const m32x4 infP = ge(abs(p), inf), infQ = ge(abs(q), inf);
    return sel(bor(band(zeroP, infQ), band(infP, zeroQ)), set1(1.5f), r);
}

} // namespace detail

#if defined(FCDSP_SIMD_SSE)

inline f32x4 rsqrte(f32x4 x) noexcept FCDSP_NONBLOCKING
{
    const bool daz = (_mm_getcsr() & 0x0040u) != 0u;
    const __m128i v = _mm_castps_si128(x);
    const int e0 = static_cast<int>(detail::frsqrteBits(static_cast<uint32_t>(_mm_extract_epi32(v, 0)), daz));
    const int e1 = static_cast<int>(detail::frsqrteBits(static_cast<uint32_t>(_mm_extract_epi32(v, 1)), daz));
    const int e2 = static_cast<int>(detail::frsqrteBits(static_cast<uint32_t>(_mm_extract_epi32(v, 2)), daz));
    const int e3 = static_cast<int>(detail::frsqrteBits(static_cast<uint32_t>(_mm_extract_epi32(v, 3)), daz));
    return _mm_castsi128_ps(_mm_setr_epi32(e0, e1, e2, e3));
}
inline f32x4 rsqrts(f32x4 p, f32x4 q) noexcept FCDSP_NONBLOCKING { return detail::rsqrtsPortable(p, q); }

#elif defined(FCDSP_SIMD_WASM)

// The same table function per lane, reading a denormal operand as it is (no FP mode on wasm), and the same composition.
inline f32x4 rsqrte(f32x4 x) noexcept FCDSP_NONBLOCKING
{
    const v128_t v = detail::asBits(x);
    return detail::asF32(wasm_u32x4_make(detail::frsqrteBits(wasm_u32x4_extract_lane(v, 0), false),
                                         detail::frsqrteBits(wasm_u32x4_extract_lane(v, 1), false),
                                         detail::frsqrteBits(wasm_u32x4_extract_lane(v, 2), false),
                                         detail::frsqrteBits(wasm_u32x4_extract_lane(v, 3), false)));
}
inline f32x4 rsqrts(f32x4 p, f32x4 q) noexcept FCDSP_NONBLOCKING { return detail::rsqrtsPortable(p, q); }

#endif

} // namespace fcdsp::simd
