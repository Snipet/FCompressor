#pragma once

// fcdsp::simd on wasm32: the output half of flush-to-zero, done by the ops themselves (ADR-93; Simd.h's wasm backend).
//
// Natively fcdsp always runs under ScopedFtz (FPCR.FZ on arm64, MXCSR FTZ|DAZ on x86-64), so no state ever holds a
// denormal: a decaying one-pole reaches zero instead of stalling at the smallest denormals, and nothing meets the slow
// path x86 hosts take for denormal operands. wasm has no FP control register. Its backend therefore replaces every
// tiny result of add, sub, mul, div, fma and fms by a zero of the same sign, and "tiny" is x86's definition (MXCSR.FTZ;
// the Linux CI runner is the witness, web.simd): the exact result, rounded to 24 bits as if the exponent had no lower
// bound, is below FLT_MIN in magnitude. That is every exact result below kTinyLimit = FLT_MIN (1 - 2^-25): the limit
// itself is a tie, which rounds to the even neighbour, FLT_MIN. So:
//   - anything the IEEE result shows as a denormal is tiny (it lies below FLT_MIN (1 - 2^-24));
//   - an IEEE result of exactly +-FLT_MIN is tiny when the exact result lies in [FLT_MIN (1 - 2^-24), kTinyLimit): the
//     gradual-underflow rounding (to 2^-149) took it up to FLT_MIN, the 24-bit rounding does not. Only a product, a
//     multiply-add or the one quotient FLT_MIN (1 - 2^-24) can land there; a sum of two floats is exact when tiny.
//   - in [kTinyLimit, FLT_MIN) the result is FLT_MIN on x86 and here, and zero on arm64, whose FZ decides on the exact
//     result (the difference Simd.h documents and dsp.simd leaves out of its hash).
// A result that is exactly +-FLT_MIN, an infinity or a NaN passes through untouched.
//
// The flush is speculative: one test finds whether ANY lane is non-zero and no larger than FLT_MIN, and only then is
// the flush run (for mul, div, fma and fms on the exact result in double: flushTinyExact). Tiny results are rare (a
// decaying state crosses the range once and is zero from then on), so the branch predicts well and the flush adds
// nothing to the latency of a recurrence; done unconditionally it sits on every critical path (four dependent
// operations after each op). The result is the same either way. The cost is four operations and a branch per op, off
// the critical path.
//
// What this does not do: read a denormal OPERAND as zero (DAZ, and arm64's FZ on inputs). The ops cannot make a
// denormal, so one can only arrive from outside them: an input sample (the web engine zeroes those before the engine
// sees them) or the scalar code of a colour stage (the web engine's silence gate bounds how long that can last).
//
// FCDSP_WASM_FLUSH_ADDSUB (default 1): 0 leaves add and sub unflushed, for measuring what the test costs there. A sum
// or difference of normal numbers is tiny only when they are below 2^-102 and nearly cancel; unflushed, it is a
// denormal that the next op reads as it is. Define the macro for every translation unit or for none.
//
// Empty on every other architecture: there the FP mode does this.

#include "fcdsp/core/Rt.h"

#if defined(__wasm_simd128__)

#include <wasm_simd128.h>

#if !defined(FCDSP_WASM_FLUSH_ADDSUB)
  #define FCDSP_WASM_FLUSH_ADDSUB 1
#endif

namespace fcdsp::simd::detail {

// Per float lane: all-ones where 0 < |r| <= FLT_MIN (a tiny result, or FLT_MIN itself, which may stand for an exact
// result below the limit and which fma's fast path must look at twice anyway). |r| - 1 then has no bit at or above
// FLT_MIN's (0x00800000); a zero gives -1, and a larger number, an infinity or a NaN keeps a high bit.
inline v128_t tinyOrMin(v128_t r) noexcept FCDSP_NONBLOCKING
{
    const v128_t magnitude = wasm_v128_and(r, wasm_i32x4_const_splat(0x7fffffff));
    return wasm_i32x4_eq(wasm_v128_and(wasm_i32x4_sub(magnitude, wasm_i32x4_const_splat(1)),
                                       wasm_i32x4_const_splat(static_cast<int32_t>(0xff800000u))),
                         wasm_i32x4_const_splat(0));
}

// r (four float lanes) with every lane of magnitude below FLT_MIN replaced by a zero of its sign, unconditionally: the
// rule for a result that is exact whenever it is tiny (a sum or difference of two floats). The compare is on the
// magnitude's bits, so an infinity or a NaN is never "below".
inline v128_t flushTinyNow(v128_t r) noexcept FCDSP_NONBLOCKING
{
    const v128_t magnitude = wasm_v128_and(r, wasm_i32x4_const_splat(0x7fffffff));
    const v128_t tiny = wasm_i32x4_gt(wasm_i32x4_const_splat(0x00800000), magnitude);   // |r| < FLT_MIN (0x00800000)
    return wasm_v128_xor(r, wasm_v128_and(magnitude, tiny));                            // only the sign bit is left
}

// The same value, computed only when some lane needs it. For a result that is exact whenever it is tiny: add and sub.
inline v128_t flushTiny(v128_t r) noexcept FCDSP_NONBLOCKING
{
    if (wasm_v128_any_true(tinyOrMin(r))) [[unlikely]]
        return flushTinyNow(r);
    return r;
}

// FLT_MIN (1 - 2^-25): an exact result below it is tiny (the header's rule). A double holds it exactly.
inline constexpr double kTinyLimit = 0x1.ffffffp-127;

// Float lanes 2-3 in lanes 0-1, where promote_low reads them.
inline v128_t highPair(v128_t v) noexcept FCDSP_NONBLOCKING { return wasm_i32x4_shuffle(v, v, 2, 3, 2, 3); }

// r (four float lanes) with a zero of its sign in every lane whose exact result is tiny. lo and hi hold the four exact
// results as doubles (lanes 0-1 and 2-3), or doubles on the same side of kTinyLimit as the exact results: the product
// of two floats (exact in double), a quotient of two floats rounded to double (never within a rounding of the limit:
// two 24-bit integers cannot have that ratio), or a sum rounded to odd. A NaN compares false and is kept.
inline v128_t flushTinyExact(v128_t r, v128_t lo, v128_t hi) noexcept FCDSP_NONBLOCKING
{
    const v128_t limit = wasm_f64x2_const_splat(kTinyLimit);
    const v128_t tiny = wasm_i32x4_shuffle(wasm_f64x2_lt(wasm_f64x2_abs(lo), limit),
                                           wasm_f64x2_lt(wasm_f64x2_abs(hi), limit), 0, 2, 4, 6);
    return wasm_v128_andnot(r, wasm_v128_and(tiny, wasm_i32x4_const_splat(0x7fffffff)));    // only the sign bit is left
}

// The slow halves of mul and div: the product or quotient redone in double (a product is exact there), because
// r == +-FLT_MIN does not say which side of the limit the exact result lies on. Out of line: they run for a handful of
// results per decay, and inlined at every multiply they would only make the module larger.
[[gnu::noinline, gnu::cold]] inline v128_t flushTinyProductNow(v128_t r, v128_t a, v128_t b) noexcept FCDSP_NONBLOCKING
{
    return flushTinyExact(r, wasm_f64x2_mul(wasm_f64x2_promote_low_f32x4(a), wasm_f64x2_promote_low_f32x4(b)),
                          wasm_f64x2_mul(wasm_f64x2_promote_low_f32x4(highPair(a)),
                                         wasm_f64x2_promote_low_f32x4(highPair(b))));
}
[[gnu::noinline, gnu::cold]] inline v128_t flushTinyQuotientNow(v128_t r, v128_t a, v128_t b) noexcept FCDSP_NONBLOCKING
{
    return flushTinyExact(r, wasm_f64x2_div(wasm_f64x2_promote_low_f32x4(a), wasm_f64x2_promote_low_f32x4(b)),
                          wasm_f64x2_div(wasm_f64x2_promote_low_f32x4(highPair(a)),
                                         wasm_f64x2_promote_low_f32x4(highPair(b))));
}

// r = a * b and r = a / b in float, flushed. The speculative test is flushTiny's.
inline v128_t flushTinyProduct(v128_t r, v128_t a, v128_t b) noexcept FCDSP_NONBLOCKING
{
    if (wasm_v128_any_true(tinyOrMin(r))) [[unlikely]]
        return flushTinyProductNow(r, a, b);
    return r;
}
inline v128_t flushTinyQuotient(v128_t r, v128_t a, v128_t b) noexcept FCDSP_NONBLOCKING
{
    if (wasm_v128_any_true(tinyOrMin(r))) [[unlikely]]
        return flushTinyQuotientNow(r, a, b);
    return r;
}

} // namespace fcdsp::simd::detail

#endif
