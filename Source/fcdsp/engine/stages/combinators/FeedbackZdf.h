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
// S13 H1b: the loop stops early once every lane sits on a Newton fixed point (fixedPoint(), below), where the remaining
// steps provably return the same bits, so the result is still the kNewtonSteps result, at a lower cost (the static FB
// target, FbAffine{0, 1}, is an FB kernel's per-sample telemetry, ControlIo::tgtDb).
// NaN or inf in x gives NaN (the poison check sees it, 01 §5.8).
//
// FbAffine::base (S10 interface revision, X10; Stage.h): the solve runs in the frame of base, on the increment
// d = r - base with the sense point (x - base) - d, the bracket [A, A + B r^_fb((x - base) - A)] and F(d) = d - A -
// B r^_fb((x - base) - d). Every term then scales with A or B, so a based solve keeps a slow release's sub-ulp steps.
// A based lane widens the bracket's upper end by kBasedSlack (2^-20) of |A| + B r^_fb: with a small B the root lies
// within a rounding of that end, the float end can fall just below it, and the safeguard then rejects Newton's exact
// step and halves (a residue of 2 % of the increment after six steps, dsp.fbsolve's base rows); the absolute FZ0
// solve has the same residue at the scale of a rounding of the GR. With base = 0, x - base is x exactly, the bracket
// is the FZ0 one and the solve
// is the FZ0 solve bit for bit. rhatFb(c, y, l) = curveFb(c, y, QuadKnee::fbLevel(l)) (the optional FB-curve hook,
// Stage.h HasRhatFb).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/params/EngineParams.h"
#include <bit>
#include <concepts>
#include <cstdint>

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
    static constexpr float kBasedSlack = 0x1p-20f;   // S10: a based solve's upper bracket slack, of |A| + B r^_fb

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

    // r^_fb(y) from the engine's LevelCtl (S10 optional hook).
    static simd::f32x4 rhatFb(const Coeffs& c, simd::f32x4 y, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return curveFb(c, y, QuadKnee::fbLevel(l));
    }

    // The root of r = base + A + B r^_fb(x - r), minus base (header comment; base = 0: the FZ0 solve, bit for bit).
    static simd::f32x4 solveFb(const Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING
    {
        const LevelCtl fb = QuadKnee::fbLevel(l);
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f), half = simd::set1(0.5f);
        const simd::f32x4 xb = simd::sub(x, a.base);                          // the sense origin; x when base = 0
        simd::f32x4 lo = a.A;
        const simd::f32x4 top = curveFb(c, simd::sub(xb, a.A), fb);
        simd::f32x4 hi = simd::fma(a.A, a.B, top);
        {
            // A based lane widens hi by 2^-20 of the increment's terms: hi rounds a bound the root can sit within a
            // rounding of (a small B), where the safeguard would reject Newton's exact step and halve instead; at the
            // increment's own scale that residue is visible (S10). A lane with base 0 keeps the FZ0 bracket.
            const simd::m32x4 based = simd::gt(simd::abs(a.base), zero);
            const simd::f32x4 slack = simd::mul(simd::set1(kBasedSlack), simd::fma(simd::abs(a.A), a.B, top));
            hi = simd::sel(based, simd::add(hi, slack), hi);
        }
        simd::f32x4 r = lo;
        for (int i = 0; i < kNewtonSteps; ++i)
        {
            const simd::f32x4 y = simd::sub(xb, r);
            const simd::f32x4 f = simd::sub(simd::sub(r, a.A), simd::mul(a.B, curveFb(c, y, fb)));
            const simd::m32x4 below = simd::ge(zero, f);                      // F(r) <= 0: the root is at or above r
            lo = simd::sel(below, r, lo);
            hi = simd::sel(below, hi, r);
            const simd::f32x4 df = simd::fma(one, a.B, slopeFb(c, y, fb));    // F'(r) = 1 + B r^_fb'(y) >= 1
            const simd::f32x4 next = simd::sub(r, simd::div(f, df));
            const simd::m32x4 inside = simd::band(simd::ge(next, lo), simd::ge(hi, next));   // false for NaN
            if (fixedPoint(inside, next, r))
                break;                                                         // every later step returns r (below)
            r = simd::sel(inside, next, simd::mul(half, simd::add(lo, hi)));
        }
        return simd::fma(r, x, zero);                                          // + x*0: poison stays poison
    }

    // S13 H1b (lead revision 5c): the early exit of solveFb, the same bits as the fixed kNewtonSteps. When every lane's
    // Newton candidate lies in its bracket and IS r (bit for bit), the step keeps r, and every later step recomputes
    // the same F(r), F'(r) and candidate r, re-narrows the bracket to r on the same side (so r stays inside it) and
    // keeps r again: the remaining steps cannot change the result, which depends on r alone. A lane that took the
    // bisection branch, moved, or holds NaN keeps iterating, so all 4 lanes (aux lanes included) must be at their fixed
    // point.
    static bool fixedPoint(simd::m32x4 inside, simd::f32x4 next, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        alignas(16) float in[4], nx[4], rr[4];
        simd::store(in, simd::sel(inside, simd::set1(1.0f), simd::set1(0.0f)));
        simd::store(nx, next);
        simd::store(rr, r);
        for (int k = 0; k < 4; ++k)
            if (in[k] == 0.0f || std::bit_cast<uint32_t>(nx[k]) != std::bit_cast<uint32_t>(rr[k]))
                return false;
        return true;
    }
};

} // namespace fcdsp::stage
