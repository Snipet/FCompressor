#pragma once

// ADAA-1 residual shapers (E §2.9; 01 §5.2 colour/, §5.6: at ECO the colour stage runs these at base rate; K1 #35;
// K2 #14). F6 (S2). A building block for the colour policies, not a policy itself.
//
// First-order antiderivative anti-aliasing (Parker, Zavalishin & Le Bivic, DAFx-16) replaces a static shaper f by the
// mean of f over the segment between two consecutive inputs:  (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),  F' = f.
// The rectangular kernel delays what it processes by half a sample, which at 50 % mix combs against the undelayed dry
// path (-2.0 dB at 20 kHz at 48 kHz, E §2.9). So only the RESIDUAL r(x) = f(x) - x goes through ADAA; the linear part
// x stays undelayed:
//     y[n] = x[n] + ADAA(r) = Q + (x[n] - x[n-1]) / 2,   Q = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
// (ADAA of the identity is the mean (x[n-1] + x[n]) / 2, so the x^2/2 part of r's antiderivative is subtracted
// analytically, never by cancellation). Where |x[n] - x[n-1]| < S::kEps the quotient is ill-conditioned and Q is
// replaced by f at the midpoint (error O(dx^2)); a constant input gives exactly f(x), silence gives exactly 0, and the
// identity would give exactly x. NaN in gives NaN out (the poison check sees it, 01 §5.8).
//
// Shapers: f(0) = 0, f'(0) = 1 (so the residual is small), F(0) = 0, both lane-wise over f32x4 with fcdsp::tanh /
// fcdsp::logCosh (FastMath.h: fma only, bit-identical across arches; no libm, K2 #14):
//   Tanh      f = tanh x;  F = logCosh x
//   AsymTanh  f = (tanh(x + b) - tanh b) / (1 - tanh^2 b): even harmonics, limits (+-1 - tanh b) / (1 - tanh^2 b),
//             meant for |b| <= 1;  F = (logCosh(x + b) - logCosh b - x tanh b) / (1 - tanh^2 b)
//   SoftClip  f = x - 4x^3/27 for |x| <= 3/2, +-1 beyond (a C1 cubic);  F = x^2/2 - x^4/27, beyond |x| - 9/16
//   HardClip  f = clamp(x, -1, 1);  F = x^2/2, beyond |x| - 1/2
// kEps balances the quotient's rounding error (about ulp(F) / dx; FastMath's logCosh adds 2.8e-7 / dx) against the
// midpoint fallback's (|f''| dx^2 / 24; a corner for HardClip): dsp.os measures both against a long-double reference.
//
// Two drivers with the same arithmetic per sample, so their outputs are bit-identical:
//   tick(s, st, x)          one sample for four independent streams (lanes = channels)
//   process(s, ch, x, n)    one channel along time, in place, four samples per vector op
// Drive, its level compensation and any GR dependence belong to the colour policy (01 §5.4 e); a drive that moves at
// control rate breaks the ADAA assumption only to second order (E §2.9).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"

namespace fcdsp::adaa {

struct Tanh {
    static constexpr float kEps = 1.0f / 64;
    simd::f32x4 shape(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING { return fcdsp::tanh(x); }
    simd::f32x4 antiderivative(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING { return fcdsp::logCosh(x); }
};

// make(b) runs per control tick at most (two FastMath calls and a divide).
struct AsymTanh {
    static constexpr float kEps = 1.0f / 64;
    float bias = 0.0f, tb = 0.0f, lcb = 0.0f, gain = 1.0f;     // b, tanh b, logCosh b, 1 / (1 - tanh^2 b)

    static AsymTanh make(float b) noexcept FCDSP_NONBLOCKING
    {
        const float t = fcdsp::tanh(b);
        return { b, t, fcdsp::logCosh(b), 1.0f / (1.0f - t * t) };
    }
    simd::f32x4 shape(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 u = fcdsp::tanh(simd::add(x, simd::set1(bias)));
        return simd::mul(simd::sub(u, simd::set1(tb)), simd::set1(gain));
    }
    simd::f32x4 antiderivative(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING     // F(0) = 0 exactly
    {
        const simd::f32x4 l = simd::sub(fcdsp::logCosh(simd::add(x, simd::set1(bias))), simd::set1(lcb));
        return simd::mul(simd::fms(l, simd::set1(tb), x), simd::set1(gain));
    }
};

struct SoftClip {
    static constexpr float kEps = 1.0f / 64;
    simd::f32x4 shape(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 one = simd::set1(1.0f);
        const simd::f32x4 inner = simd::mul(x, simd::fms(one, simd::set1(4.0f / 27.0f), simd::mul(x, x)));
        const simd::f32x4 outer = simd::sel(simd::gt(x, simd::set1(0.0f)), one, simd::set1(-1.0f));
        return simd::sel(simd::gt(simd::abs(x), simd::set1(1.5f)), outer, inner);
    }
    simd::f32x4 antiderivative(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING    // x^2/2 - x^4/27; |x| - 9/16
    {
        const simd::f32x4 t = simd::mul(x, x), ax = simd::abs(x);
        const simd::f32x4 inner = simd::mul(t, simd::fms(simd::set1(0.5f), simd::set1(1.0f / 27.0f), t));
        return simd::sel(simd::gt(ax, simd::set1(1.5f)), simd::sub(ax, simd::set1(0.5625f)), inner);
    }
};

struct HardClip {
    static constexpr float kEps = 1.0f / 1024;
    simd::f32x4 shape(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING   // x second in max/min: NaN stays NaN (Simd.h)
    {
        return simd::min(simd::set1(1.0f), simd::max(simd::set1(-1.0f), x));
    }
    simd::f32x4 antiderivative(simd::f32x4 x) const noexcept FCDSP_NONBLOCKING    // x^2/2 inside; |x| - 1/2 outside
    {
        const simd::f32x4 ax = simd::abs(x), half = simd::set1(0.5f);
        return simd::sel(simd::gt(ax, simd::set1(1.0f)), simd::sub(ax, half), simd::mul(simd::mul(x, x), half));
    }
};

// The residual step for one sample per lane: x0, F0 the previous input and F(x0); x1, F1 the current ones.
template <class S>
inline simd::f32x4 residualStep(const S& s, simd::f32x4 x0, simd::f32x4 F0, simd::f32x4 x1,
                                simd::f32x4 F1) noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 half = simd::set1(0.5f);
    const simd::f32x4 d = simd::sub(x1, x0);
    const simd::f32x4 q = simd::div(simd::sub(F1, F0), d);           // inf or NaN where d is 0: never selected
    const simd::f32x4 mid = simd::fma(x0, half, d);                  // x0 + d/2
    const simd::m32x4 wide = simd::ge(simd::abs(d), simd::set1(S::kEps));
    return simd::fma(simd::sel(wide, q, s.shape(mid)), half, d);     // Q + d/2
}

// Four independent streams (lanes); the previous input and its antiderivative per lane.
struct State {
    simd::f32x4 x1 = simd::set1(0.0f);
    simd::f32x4 F1 = simd::set1(0.0f);
};

template <class S>
inline simd::f32x4 tick(const S& s, State& st, simd::f32x4 x) noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 F = s.antiderivative(x);
    const simd::f32x4 y = residualStep(s, st.x1, st.F1, x, F);
    st.x1 = x;
    st.F1 = F;
    return y;
}

// One channel's carry for process().
struct Channel {
    float x1 = 0.0f;
    float F1 = 0.0f;
};

template <class S>
inline void process(const S& s, Channel& ch, float* x, int n) noexcept FCDSP_NONBLOCKING
{
    alignas(16) float xs[5], Fs[5];                                  // [0] = the previous sample, [1..4] = this group
    xs[0] = ch.x1;
    Fs[0] = ch.F1;
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const simd::f32x4 xv = simd::load(x + i), Fv = s.antiderivative(xv);
        simd::store(xs + 1, xv);
        simd::store(Fs + 1, Fv);
        simd::store(x + i, residualStep(s, simd::load(xs), simd::load(Fs), xv, Fv));
        xs[0] = xs[4];
        Fs[0] = Fs[4];
    }
    for (; i < n; ++i)                                               // the tail: lane 0 of the same step
    {
        const simd::f32x4 xv = simd::set1(x[i]), Fv = s.antiderivative(xv);
        x[i] = simd::lane<0>(residualStep(s, simd::set1(xs[0]), simd::set1(Fs[0]), xv, Fv));
        xs[0] = simd::lane<0>(xv);
        Fs[0] = simd::lane<0>(Fv);
    }
    ch.x1 = xs[0];
    ch.F1 = Fs[0];
}

// The static curve (the COLOUR view): f(x).
template <class S>
inline float transfer(const S& s, float x) noexcept FCDSP_NONBLOCKING
{
    return simd::lane<0>(s.shape(simd::set1(x)));
}

} // namespace fcdsp::adaa
