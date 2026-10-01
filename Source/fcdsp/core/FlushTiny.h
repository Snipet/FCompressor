#pragma once

// fcdsp::simd on wasm32: the output half of flush-to-zero, done by the ops themselves (ADR-93; Simd.h's wasm backend).
//
// Natively fcdsp always runs under ScopedFtz (FPCR.FZ on arm64, MXCSR FTZ|DAZ on x86-64), so no state ever holds a
// denormal: a decaying one-pole reaches zero instead of stalling at the smallest denormals, and nothing meets the slow
// path x86 hosts take for denormal operands. wasm has no FP control register. Its backend therefore replaces every
// result of add, sub, mul, div, fma and fms whose magnitude is below FLT_MIN by a zero of the same sign, decided on the
// rounded result (the x86 rule; arm64 decides before rounding, a difference Simd.h documents and dsp.simd leaves out
// of its hash). A result that is exactly +-FLT_MIN, an infinity or a NaN passes through untouched.
//
// The flush is speculative: one test finds whether ANY lane is non-zero and no larger than FLT_MIN, and only then are
// the four flushing operations run. Tiny results are rare (a decaying state crosses the range once and is zero from
// then on), so the branch predicts well and the flush adds nothing to the latency of a recurrence; done
// unconditionally it sits on every critical path (four dependent operations after each op). The result is the same
// either way. The cost is four operations and a branch per op, off the critical path.
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

// Per float lane: all-ones where 0 < |r| <= FLT_MIN (a tiny result, or FLT_MIN itself, which fma's fast path must
// also look at twice). |r| - 1 then has no bit at or above FLT_MIN's (0x00800000); a zero gives -1, and a larger
// number, an infinity or a NaN keeps a high bit.
inline v128_t tinyOrMin(v128_t r) noexcept FCDSP_NONBLOCKING
{
    const v128_t magnitude = wasm_v128_and(r, wasm_i32x4_const_splat(0x7fffffff));
    return wasm_i32x4_eq(wasm_v128_and(wasm_i32x4_sub(magnitude, wasm_i32x4_const_splat(1)),
                                       wasm_i32x4_const_splat(static_cast<int32_t>(0xff800000u))),
                         wasm_i32x4_const_splat(0));
}

// r (four float lanes) with every lane of magnitude below FLT_MIN replaced by a zero of its sign, unconditionally. The
// compare is on the magnitude's bits, so an infinity or a NaN is never "below".
inline v128_t flushTinyNow(v128_t r) noexcept FCDSP_NONBLOCKING
{
    const v128_t magnitude = wasm_v128_and(r, wasm_i32x4_const_splat(0x7fffffff));
    const v128_t tiny = wasm_i32x4_gt(wasm_i32x4_const_splat(0x00800000), magnitude);   // |r| < FLT_MIN (0x00800000)
    return wasm_v128_xor(r, wasm_v128_and(magnitude, tiny));                            // only the sign bit is left
}

// The same value, computed only when some lane needs it.
inline v128_t flushTiny(v128_t r) noexcept FCDSP_NONBLOCKING
{
    if (wasm_v128_any_true(tinyOrMin(r))) [[unlikely]]
        return flushTinyNow(r);
    return r;
}

} // namespace fcdsp::simd::detail

#endif
