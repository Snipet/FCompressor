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
