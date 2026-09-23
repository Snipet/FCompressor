#pragma once

// stage::FeedbackZdf<G>: the zero-delay feedback solve for a gain law without a closed form (01 §5.2 "adaptors
// FeedbackZdf<G> (Newton)"; E §2.6; K2 #1, #5a). It is a GainComputerPolicy: target() is G's FF curve (an FB kernel
// evaluates FF on an external key, E §2.6), and solveFb(c, x, l, {A, B}) returns the root of
//
//     F(r) = r - A - B * r^_fb(x - r) = 0,     r^_fb(y) = G::target(c, y, l with slope k = QuadKnee::loopGain(S))
//
// (the one FB-curve convention, QuadKnee.h). F is strictly increasing whenever r^_fb is non-decreasing (fb.monotone,
// K2 #5a), so the root is unique and lies in the bracket
//
//     [lo, hi] = [A, A + B * r^_fb(x - A)]        F(lo) = -B r^_fb(x - A) <= 0,  F(hi) >= 0 by monotonicity,
//
// which is law-agnostic and tighter than E's [A, A + B max(0, x - T + W)]. Newton starts at lo, where the loop senses
// the most overshoot: with a convex r^_fb (QuadKnee, ProgressiveKnee) F is concave in r, so the iterates rise
// monotonically to the root and the first step already lands on it when the root lies in a linear region. Every step
// is safeguarded (a candidate outside the current bracket, or NaN, is replaced by the bracket's midpoint, and the
// bracket shrinks with the sign of F), so a law that is not convex still converges.
//
// kNewtonSteps = 6 fixed steps (deterministic: the same count every sample, 01 §7's "<= 6 Newton steps"). E §2.6
// proposed two steps from a predictor; from the bracket's end, over 2e5 random QuadKnee cases at 48 kHz (attack
// 5 us-250 ms, knee 0-24 dB, up to inf:1), the worst error against 200-step bisection is 3 dB after two steps, 0.06 dB
// after four, 5e-4 dB after five and 5e-6 dB (float precision) after six, which dsp.fbsolve holds to 1e-5 dB. The
// derivative is G::slope when G provides one (QuadKnee does), else a central difference over +-kSlopeStepDb.
// NaN or inf in x gives NaN (the poison check sees it, 01 §5.8).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/params/EngineParams.h"
#include <concepts>

namespace fcdsp::stage {

namespace detail {
template <class G>
concept HasSlope = requires (const typename G::Coeffs& c, simd::f32x4 x, const LevelCtl& l) {
    { G::slope(c, x, l) } noexcept -> std::same_as<simd::f32x4>;
};
} // namespace detail

template <class G>
struct FeedbackZdf {
    static_assert(GainComputerPolicy<G>, "FeedbackZdf: G must be a GainComputerPolicy");
    static constexpr int kNewtonSteps = 6;
    static constexpr float kSlopeStepDb = 1e-2f;

    struct Coeffs { typename G::Coeffs inner{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        G::design(c.inner, p, x);
    }

    static simd::f32x4 target(const Coeffs& c, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return G::target(c.inner, x, l);
    }

    // The FB curve r^_fb(y) and its slope (header comment).
    static simd::f32x4 curveFb(const Coeffs& c, simd::f32x4 y, const LevelCtl& fb) noexcept FCDSP_NONBLOCKING
    {
        return G::target(c.inner, y, fb);
    }
    static simd::f32x4 slopeFb(const Coeffs& c, simd::f32x4 y, const LevelCtl& fb) noexcept FCDSP_NONBLOCKING
    {
        if constexpr (detail::HasSlope<G>)
            return G::slope(c.inner, y, fb);
        else
        {
            const simd::f32x4 h = simd::set1(kSlopeStepDb);
            const simd::f32x4 up = G::target(c.inner, simd::add(y, h), fb);
            const simd::f32x4 dn = G::target(c.inner, simd::sub(y, h), fb);
            return simd::div(simd::sub(up, dn), simd::set1(2.0f * kSlopeStepDb));
        }
    }

    static simd::f32x4 solveFb(const Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING
    {
        const LevelCtl fb = QuadKnee::fbLevel(l);
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f), half = simd::set1(0.5f);
        simd::f32x4 lo = a.A;
        simd::f32x4 hi = simd::fma(a.A, a.B, curveFb(c, simd::sub(x, a.A), fb));
        simd::f32x4 r = lo;
        for (int i = 0; i < kNewtonSteps; ++i)
        {
            const simd::f32x4 y = simd::sub(x, r);
            const simd::f32x4 f = simd::sub(simd::sub(r, a.A), simd::mul(a.B, curveFb(c, y, fb)));
            const simd::m32x4 below = simd::ge(zero, f);                      // F(r) <= 0: the root is at or above r
            lo = simd::sel(below, r, lo);
            hi = simd::sel(below, hi, r);
            const simd::f32x4 df = simd::fma(one, a.B, slopeFb(c, y, fb));    // F'(r) = 1 + B r^_fb'(y) >= 1
            const simd::f32x4 next = simd::sub(r, simd::div(f, df));
            const simd::m32x4 inside = simd::band(simd::ge(next, lo), simd::ge(hi, next));   // false for NaN
            r = simd::sel(inside, next, simd::mul(half, simd::add(lo, hi)));
        }
        return simd::fma(r, x, zero);                                          // + x*0: poison stays poison
    }
};

} // namespace fcdsp::stage
