#pragma once

// Transcendentals for everything under core/, engine/ and modes/ (01 §2.2 rule 4, §5.1; E §0.9, §3.2; K2 #14).
// The libm names are banned there because Apple's libm differs across arches and macOS releases; these are written
// with fma only, so arm64 and x86 produce the same bits and print.* hashes cannot move under an OS update.
//
// Frozen at FZ0 (names and signatures). F0 declares; F1 (S1) implements. Contract for the bodies:
//   - every scalar form is bit-identical to lane 0 of its vector form;
//   - log2: minimax polynomial, |err| <= 4e-6 log2 units (refit target 4e-7);
//   - exp2: input clamped to [-126, 126], relative error <= 9e-5 (refit target 1e-5);
//   - callers floor before log2: max(|x|, kLinFloor) and max(ms, kMsFloor) (01 §5.7);
//   - every function is FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata): an out-of-line definition repeats the macro.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"

namespace fcdsp {

inline constexpr float kDbPerLog2 = 6.02059991f, kLog2PerDb = 0.166096404f;   // 20*log10(2) and its inverse
inline constexpr float kLinFloor = 1e-12f /*-240 dB*/, kMsFloor = 1e-24f;     // floors before log2 (01 §5.7)

simd::f32x4 log2(simd::f32x4) noexcept FCDSP_NONBLOCKING;     // minimax poly; |err| <= 4e-6 (refit target 4e-7)
simd::f32x4 exp2(simd::f32x4) noexcept FCDSP_NONBLOCKING;     // input clamped to [-126, 126]; rel err <= 9e-5
float log2(float) noexcept FCDSP_NONBLOCKING;                 // == lane 0 of the vector form, bit-identical
float exp2(float) noexcept FCDSP_NONBLOCKING;

// libm replacements for colour stages and per-tick filter design (targets fixed by the F1 spike, asserted by dsp.simd)
simd::f32x4 tanh(simd::f32x4) noexcept FCDSP_NONBLOCKING;     // 1 - 2/(exp2(2x*log2e) + 1); odd minimax for |x| < 0.125
simd::f32x4 logCosh(simd::f32x4) noexcept FCDSP_NONBLOCKING;  // |x| + ln2*log2(1 + exp2(-2|x|*log2e)) - ln 2 (ADAA-1
                                                              // antiderivative of tanh)
float tanh(float) noexcept FCDSP_NONBLOCKING;
float logCosh(float) noexcept FCDSP_NONBLOCKING;
float tanPi(float x) noexcept FCDSP_NONBLOCKING;              // tangent of pi*x, x in [0, 0.499]: SVF g = tanPi(fc/fs)
float sinPi(float x) noexcept FCDSP_NONBLOCKING;              // sine of pi*x, x in [-1, 1]
float cosPi(float x) noexcept FCDSP_NONBLOCKING;              // cosine of pi*x, x in [-1, 1]

inline simd::f32x4 dbFromLin(simd::f32x4 absx) noexcept FCDSP_NONBLOCKING;   // kDbPerLog2 * log2(max(|x|, kLinFloor))
inline simd::f32x4 dbFromMs (simd::f32x4 ms)   noexcept FCDSP_NONBLOCKING;   // 0.5*kDbPerLog2 * log2(max(ms, kMsFloor))
inline simd::f32x4 linFromDb(simd::f32x4 db)   noexcept FCDSP_NONBLOCKING;   // exp2(db * kLog2PerDb)

} // namespace fcdsp

// ==== Bodies (F1, S1) ================================================================================================
//
// log2, exp2, tanh, logCosh and the three dB helpers are defined inline below: they run per sample, so non-LTO builds
// inline them too. tanPi, sinPi and cosPi run per control tick and are defined out of line in FastMath.cpp.
// Every function is a fixed sequence of fcdsp::simd ops (fused fma, IEEE add/sub/mul/div, exact floor/abs/sel) plus
// exact integer exponent handling, so both backends produce the same bits; every scalar form is lane 0 of its vector
// form by construction (scalar(x) = lane<0>(vector(set1(x)))). Polynomials are minimax fits (Remez exchange, weighted
// for the error measure named at each one), coefficients rounded to float, evaluated by Horner with fused steps.
// Errors measured by dsp.simd over its sweeps (its spec rows hold them with ~20 % headroom):
//   log2      1.1e-7 log2 units for |x| in [1/2, 2) (refit target 4e-7); elsewhere at most half an ulp of the result
//             plus 7.7e-8, i.e. 2.0e-6 at exponent -40 = 1.2e-5 dB (E §0.9: 2.3e-5 dB). Exact on powers of two;
//             monotone over every float of exponents -40..40.
//   exp2      relative 1.7e-7 (refit target 1e-5; E §0.9: 8.5e-5); exact on integers, 2^n scaling exact.
//   tanh      absolute 1.6e-7; relative 6.7e-8 below |x| = 1/8, 1.1e-6 just above it (the 1 - 2/(e+1) form cancels).
//   logCosh   absolute 1.4e-7 beyond the result's rounding; relative 1.2e-7 below |x| = 1/8, 1.3e-5 just above it
//             (|x| - ln2 * |log2(...)| cancels there): an ADAA quotient over dx carries about 2.8e-7 / dx.
// Domains: log2 takes |x| of a positive normal float (callers floor first, 01 §5.7); zero and denormals give values
// in [-127, -126]; NaN and +-inf give NaN. exp2 clamps to [-126, 126] (results are normal) and returns NaN for NaN.
// tanh/logCosh give +-1 and +inf at +-inf and NaN for NaN. NaN in, NaN out everywhere, so poison is never laundered
// into a finite value before the poison check (01 §5.8).

namespace fcdsp {

namespace detail {

// x = 2^k * (1 + t) with 1 + t in [sqrt(1/2), sqrt(2)), from |x|'s bits (the sign bit is ignored). The biased
// exponent is taken relative to sqrt(1/2)'s bits (0x3f3504f3), so one integer subtract and an arithmetic shift give k
// and the mantissa lands on the centred interval where the polynomial is fitted; t = m - 1 is exact (Sterbenz).
struct Log2Split { simd::f32x4 k, t; };

inline Log2Split log2Split(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
#if defined(FCDSP_SIMD_NEON)
    const uint32x4_t ix = vsubq_u32(vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x7fffffffu)),
                                    vdupq_n_u32(0x3f3504f3u));
    const int32x4_t k = vshrq_n_s32(vreinterpretq_s32_u32(ix), 23);
    const float32x4_t m = vreinterpretq_f32_u32(vaddq_u32(vandq_u32(ix, vdupq_n_u32(0x007fffffu)),
                                                          vdupq_n_u32(0x3f3504f3u)));
    return { vcvtq_f32_s32(k), simd::sub(m, simd::set1(1.0f)) };
#elif defined(FCDSP_SIMD_WASM)
    const v128_t ix = wasm_i32x4_sub(wasm_v128_and(simd::detail::asBits(x), wasm_i32x4_const_splat(0x7fffffff)),
                                     wasm_i32x4_const_splat(0x3f3504f3));
    const v128_t k = wasm_i32x4_shr(ix, 23);
    const v128_t m = wasm_i32x4_add(wasm_v128_and(ix, wasm_i32x4_const_splat(0x007fffff)),
                                    wasm_i32x4_const_splat(0x3f3504f3));
    return { simd::detail::asF32(wasm_f32x4_convert_i32x4(k)), simd::sub(simd::detail::asF32(m), simd::set1(1.0f)) };
#else
    const __m128i ix = _mm_sub_epi32(_mm_and_si128(_mm_castps_si128(x), _mm_set1_epi32(0x7fffffff)),
                                     _mm_set1_epi32(0x3f3504f3));
    const __m128i k = _mm_srai_epi32(ix, 23);
    const __m128 m = _mm_castsi128_ps(_mm_add_epi32(_mm_and_si128(ix, _mm_set1_epi32(0x007fffff)),
                                                    _mm_set1_epi32(0x3f3504f3)));
    return { _mm_cvtepi32_ps(k), simd::sub(m, simd::set1(1.0f)) };
#endif
}

// p * 2^n for an integer-valued n with p * 2^n normal: n is added to p's exponent field (exact, no rounding).
inline simd::f32x4 scaleByPow2(simd::f32x4 p, simd::f32x4 n) noexcept FCDSP_NONBLOCKING
{
#if defined(FCDSP_SIMD_NEON)
    return vreinterpretq_f32_s32(vaddq_s32(vreinterpretq_s32_f32(p), vshlq_n_s32(vcvtq_s32_f32(n), 23)));
#elif defined(FCDSP_SIMD_WASM)
    // trunc_sat gives 0 for a NaN, as NEON's convert does (x86's 0x80000000 shifts out to 0): a NaN p keeps its bits.
    return simd::detail::asF32(wasm_i32x4_add(simd::detail::asBits(p),
                                              wasm_i32x4_shl(wasm_i32x4_trunc_sat_f32x4(simd::detail::asBits(n)), 23)));
#else
    return _mm_castsi128_ps(_mm_add_epi32(_mm_castps_si128(p), _mm_slli_epi32(_mm_cvttps_epi32(n), 23)));
#endif
}

// r with its sign bit flipped where x's is set: copysign(r, x) for r >= 0 (+-0, inf and NaN included).
inline simd::f32x4 xorSign(simd::f32x4 r, simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
#if defined(FCDSP_SIMD_NEON)
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(r),
                                           vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u))));
#elif defined(FCDSP_SIMD_WASM)
    return simd::detail::asF32(wasm_v128_xor(simd::detail::asBits(r),
                                             wasm_v128_and(simd::detail::asBits(x), wasm_f32x4_const_splat(-0.0f))));
#else
    return _mm_xor_ps(r, _mm_and_ps(x, _mm_set1_ps(-0.0f)));
#endif
}

} // namespace detail

// log2: k + t*q(t), q of degree 7 fitted to log2(1 + t)/t for the absolute error on [sqrt(1/2) - 1, sqrt(2) - 1]
// (4.8e-8). The final fused step k + t*q rounds once; adding x*0 (+-0 for every finite x, so exact) makes a NaN or an
// infinite input NaN instead of the finite value its bits would decode to, so poison reaches the poison check.
inline simd::f32x4 log2(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 poison = simd::mul(x, simd::set1(0.0f));
    const detail::Log2Split s = detail::log2Split(x);
    simd::f32x4 q = simd::set1(-0.145744577f);
    q = simd::fma(simd::set1(0.23689042f), q, s.t);
    q = simd::fma(simd::set1(-0.250069022f), q, s.t);
    q = simd::fma(simd::set1(0.286707461f), q, s.t);
    q = simd::fma(simd::set1(-0.360087216f), q, s.t);
    q = simd::fma(simd::set1(0.480939448f), q, s.t);
    q = simd::fma(simd::set1(-0.721357167f), q, s.t);
    q = simd::fma(simd::set1(1.44269478f), q, s.t);
    return simd::add(simd::fma(s.k, s.t, q), poison);
}

// exp2: x clamped to [-126, 126] (the constants are the FIRST operands, so a NaN stays NaN on x86 as on NEON),
// n = floor(x + 1/2), an integer, and f = x - n, exact, in [-1/2, 1/2] (a hair beyond where x + 1/2 rounds up to an
// integer); 2^f = 1 + f*r(f) with r of degree 4 fitted for the relative error (9.1e-8), then n added to the exponent
// field. n = -126 only with f >= 0, so the result is never denormal.
inline simd::f32x4 exp2(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
    x = simd::min(simd::set1(126.0f), simd::max(simd::set1(-126.0f), x));
    const simd::f32x4 n = simd::floor(simd::add(x, simd::set1(0.5f)));
    const simd::f32x4 f = simd::sub(x, n);
    simd::f32x4 r = simd::set1(0.00132647273f);
    r = simd::fma(simd::set1(0.00967151299f), r, f);
    r = simd::fma(simd::set1(0.0555073358f), r, f);
    r = simd::fma(simd::set1(0.240222424f), r, f);
    r = simd::fma(simd::set1(0.693147004f), r, f);
    return detail::scaleByPow2(simd::fma(simd::set1(1.0f), f, r), n);
}

inline float log2(float x) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(fcdsp::log2(simd::set1(x))); }
inline float exp2(float x) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(fcdsp::exp2(simd::set1(x))); }

// tanh on |x|, then x's sign (so tanh(-x) == -tanh(x) exactly, zeros included): |x| < 1/8: |x| + |x|^3 * P(x^2),
// P = c3 + c5 x^2 fitted for the relative error (7.8e-9); otherwise 1 - 2/(exp2(2|x| log2 e) + 1).
inline simd::f32x4 tanh(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 ax = simd::abs(x), one = simd::set1(1.0f);
    const simd::f32x4 e = fcdsp::exp2(simd::mul(ax, simd::set1(2.88539008f)));             // e^(2|x|)
    const simd::f32x4 big = simd::sub(one, simd::div(simd::set1(2.0f), simd::add(e, one)));
    const simd::f32x4 x2 = simd::mul(ax, ax);
    const simd::f32x4 p = simd::fma(simd::set1(-0.333327711f), simd::set1(0.132167369f), x2);
    const simd::f32x4 small = simd::fma(ax, simd::mul(ax, x2), p);
    return detail::xorSign(simd::sel(simd::gt(simd::set1(0.125f), ax), small, big), x);
}

// logCosh: |x| < 1/8: u/2 + u^2 * S(u), u = x^2, S = s0 + s1 u fitted for the relative error (2.0e-9); even by
// construction. Otherwise |x| + ln2 * log2((1 + e^(-2|x|)) / 2), the design's |x| + ln2*log2(1 + e^(-2|x|)) - ln 2
// with the constant folded into the log's argument: (1 + e)/2 = 1/2 + e/2 rounds once and the separate - ln 2
// cancellation disappears. logCosh(-x) == logCosh(x) exactly.
inline simd::f32x4 logCosh(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 ax = simd::abs(x), half = simd::set1(0.5f);
    const simd::f32x4 e = fcdsp::exp2(simd::mul(ax, simd::set1(-2.88539008f)));            // e^(-2|x|)
    const simd::f32x4 big = simd::fma(ax, simd::set1(0.693147182f), fcdsp::log2(simd::fma(half, half, e)));
    const simd::f32x4 u = simd::mul(x, x);
    const simd::f32x4 s = simd::fma(simd::set1(-0.083332628f), simd::set1(0.022076292f), u);
    const simd::f32x4 small = simd::fma(simd::mul(half, u), simd::mul(u, u), s);
    return simd::sel(simd::gt(simd::set1(0.125f), ax), small, big);
}

inline float tanh(float x) noexcept FCDSP_NONBLOCKING    { return simd::lane<0>(fcdsp::tanh(simd::set1(x))); }
inline float logCosh(float x) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(fcdsp::logCosh(simd::set1(x))); }

// The floors come FIRST in max(): a NaN input stays NaN on both backends (x86 maxps returns its second operand).
inline simd::f32x4 dbFromLin(simd::f32x4 absx) noexcept FCDSP_NONBLOCKING
{
    return simd::mul(simd::set1(kDbPerLog2), fcdsp::log2(simd::max(simd::set1(kLinFloor), simd::abs(absx))));
}

inline simd::f32x4 dbFromMs(simd::f32x4 ms) noexcept FCDSP_NONBLOCKING
{
    return simd::mul(simd::set1(0.5f * kDbPerLog2), fcdsp::log2(simd::max(simd::set1(kMsFloor), ms)));
}

inline simd::f32x4 linFromDb(simd::f32x4 db) noexcept FCDSP_NONBLOCKING
{
    return fcdsp::exp2(simd::mul(db, simd::set1(kLog2PerDb)));
}

} // namespace fcdsp
