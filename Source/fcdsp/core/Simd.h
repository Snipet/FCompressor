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

#if defined(__aarch64__) || defined(__ARM_NEON)
  #include <arm_neon.h>
  #define FCDSP_SIMD_NEON 1
#elif defined(__x86_64__) || defined(_M_X64)
  #include <immintrin.h>
  #define FCDSP_SIMD_SSE 1
  #if !defined(__FMA__) && !(defined(_MSC_VER) && !defined(__clang__) && defined(__AVX2__))
    #error "fcdsp's x86-64 build needs FMA: fcmp_flags adds -mavx2 -mfma (03 §2.6)."
  #endif
#else
  #error "fcdsp: no SIMD backend for this architecture (arm64 NEON, or x86-64 SSE4.1/AVX2 with FMA)."
#endif

namespace fcdsp::simd {

#if defined(FCDSP_SIMD_NEON)
using f32x4 = float32x4_t;                  // four float lanes
using m32x4 = uint32x4_t;                   // per-lane mask: all-ones = true
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
