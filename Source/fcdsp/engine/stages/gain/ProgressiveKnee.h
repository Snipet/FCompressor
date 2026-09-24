#pragma once

// stage::ProgressiveKnee: the progressive (level-dependent) ratio of a remote-cutoff tube, vari-mu's gain computer
// (01 §5.2 gain/ catalogue; 01 §10.7 Mu 67 "ProgressiveKnee inside FeedbackZdf"; E §2.2 "Progressive
// (level-dependent) ratio", E §2.7 vari-mu). M4 (S10). Per lane, with o = x - T the overshoot over the threshold:
//
//     r^(x) = S * o_c * H(o / o_c),   H(u) = u - (1 - e^-u)  (u > 0; 0 at and below the threshold)
//
// i.e. E's r^(o) = S (o - o_c (1 - e^(-o / o_c))): its local slope S (1 - e^(-o / o_c)) starts at 0 at the threshold
// (C1, soft by construction) and rises monotonically to S, so the local ratio 1 / (1 - slope) rises from 1:1 toward
// 1 / (1 - S) as the level grows. The curve is convex (H'' = e^-u > 0) and monotone (H' >= 0).
//
// The per-sample controls come from the LevelCtl (Stage.h's two-rate rule): T = thrDb, S = slope (the asymptotic
// slope, so the ratio the curve rises TOWARD), and the onset constant o_c from the knee width W = kneeDb through the
// Mode's affine law
//
//     o_c = max(kMinOnsetDb, m[kOnsetSlot] + m[kOnsetPerDbSlot] * W)
//
// (EngineParams::m, the Mode-private constants, read by design() on control ticks; a Mode sets them once, so they never
// move while it runs). T, S and W are smoothed per sample by ModeEngine and nothing is baked into Coeffs, so a moving
// DC THRESH (Mu 67's knee) reshapes the curve without a zipper, and the settled engine and staticGr evaluate the same
// function of the same floats. A value-initialised m (0, 0) gives the floor o_c: a hard knee of slope S.
//
// Feedback (K2 #1, #5a; QuadKnee.h's one FB-curve convention): the FB curve is target() with the loop gain
// k = QuadKnee::loopGain(S) in place of S, r^_fb(y) = k o_c H((y - T) / o_c), the same progressive law at the OUTPUT
// (the loop senses y = x - r): the loop gain rises from 0 at the threshold toward k and the closed loop's local ratio
// 1 + k (1 - e^(-(y - T) / o_c)) from 1:1 toward 1 + k = 1 / (1 - S), the same asymptote as the feed-forward curve.
// r^_fb is non-decreasing for every S >= 0 and every o_c (fb.monotone), so the root of every solve is unique; it is
// convex, so F(d) = d - A - B r^_fb(o' - d) is concave and increasing and Newton from the bracket's lower end rises
// monotonically onto the root. rhatFb(c, y, l) = r^_fb(y) (the optional hook, Stage.h HasRhatFb).
//
// solveFb(c, x, l, {A, B, base}) returns the root's increment d = r - base of r = base + A + B r^_fb(x - r) (Stage.h;
// base 0: the absolute root), solved in the frame of base (o' = x - T - base, so every term of d scales with A or
// B r^_fb and a based solve keeps a slow release's sub-ulp steps, S10 X10). It is FeedbackZdf.h's safeguarded Newton
// (the iterate replaced by the bracket's midpoint where it leaves the bracket, which shrinks with the sign of F), with
// three changes for cost. The bracket is [A, A + B k max(0, o' - A)] (H(u) <= u bounds r^_fb by its no-knee line, so
// its upper end costs no curve evaluation; a based lane's upper end is widened by 2^-20 of |A| + B k max(0, o' - A),
// FeedbackZdf.h's slack). The iteration starts at the root of the map on the curve's linear asymptote k (o - o_c)
// (H(u) >= u - 1, so F there is <= 0: the start lies at or below the root, where Newton rises monotonically; clamped
// to A), a step ahead of the bracket's end. And a lane stops once its step is within 2^-21 of the increment's scale
// |A| + B k max(0, o' - A) (quadratic convergence: the iterate it lands on is off by the square of that) and stays
// frozen there, so its result depends on its own inputs only, never on the other lanes' iterations (analysis::staticGr
// and the engine's tgtDb agree bit for bit whatever the lane packing); at most kMaxSteps. A ballistics map has a small
// B (a one-pole's rate), where F is nearly linear and one to three evaluations land on the root, against FeedbackZdf's
// fixed seven: Mu 67 runs two to five solves per sample (TcSelector's branches and the S10 carry), so this is most of
// its control cost (docs/modes/mu-67.md). FeedbackZdf<ProgressiveKnee> remains a valid wrapper of the same law
// (dsp.progressiveknee holds both against 200-step bisection within 1e-5 dB, K2 #1). One exp2 per evaluation serves the
// curve and its slope.
//
// Precision. H(u) = u - 1 + e^-u cancels for a small u (H ~ u^2 / 2): with FastMath's exp2 (relative 1.7e-7) the
// direct form's absolute error ~2e-7 would dominate H below u ~ 1e-3 and could step the curve down between two nearby
// levels. So below kSeriesBelow = 1/2, H is its alternating Taylor series u^2 (1/2 - u/6 + u^2/24 - ... - u^7/9!),
// truncated where the next term is below 3e-9 of H, evaluated by Horner with fused steps; above it the direct form
// (u - 1 is exact on [1/2, 2], Sterbenz), whose relative error there is below 1.2e-6. The two meet within 1.2e-6 of H
// at the seam, far below the 0.1 dB grid of fb.monotone and the 1e-5 dB of the FB-solve rows (dsp.progressiveknee).
// The slope, S (1 - e^-u), is Newton's derivative (F' = 1 + B r^' >= 1 absorbs its absolute error) and needs no series.
// NaN or inf in x gives NaN (the poison check sees it, 01 §5.8), as QuadKnee does. No libm (01 §2.2): fcdsp::exp2.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/combinators/FeedbackZdf.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <int kOnsetSlot = 0, int kOnsetPerDbSlot = 1>
struct ProgressiveKneeT {
    static_assert(kOnsetSlot >= 0 && kOnsetSlot < 8 && kOnsetPerDbSlot >= 0 && kOnsetPerDbSlot < 8
                      && kOnsetSlot != kOnsetPerDbSlot,
                  "ProgressiveKneeT: the onset law's slots index EngineParams::m[8]");
    static constexpr float kMinOnsetDb = 1e-3f;     // o_c floor: a hard knee of slope S
    static constexpr float kSeriesBelow = 0.5f;     // u below which H is its Taylor series (header comment)
    static constexpr float kLog2E = 1.44269504f;    // log2(e): e^-u = 2^(-u log2 e)
    static constexpr int kMaxSteps = 12;            // solveFb: Newton steps at most (header comment)
    static constexpr float kStopRel = 0x1p-21f;     // solveFb: a lane stops at a step within 2^-21 of its scale
    static constexpr float kBasedSlack = 0x1p-20f;  // solveFb: a based lane's upper bracket slack (FeedbackZdf.h)

    struct Coeffs {
        float onsetDb = 0;                          // o_c at W = 0 (m[kOnsetSlot]), dB
        float onsetPerDb = 0;                       // o_c per dB of knee width (m[kOnsetPerDbSlot])
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx&) noexcept FCDSP_NONBLOCKING
    {
        c.onsetDb = p.m[kOnsetSlot];
        c.onsetPerDb = p.m[kOnsetPerDbSlot];
    }

    // o_c per lane from the knee width (header comment); the floor first, so a NaN knee stays NaN.
    static simd::f32x4 onset(const Coeffs& c, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return simd::max(simd::set1(kMinOnsetDb), simd::fma(simd::set1(c.onsetDb), simd::set1(c.onsetPerDb), l.kneeDb));
    }

    // H(u) = u - 1 + e^-u for u >= 0 (header comment: the series below kSeriesBelow, the direct form above); e = e^-u.
    static simd::f32x4 shape(simd::f32x4 u, simd::f32x4 e) noexcept FCDSP_NONBLOCKING
    {
        simd::f32x4 p = simd::set1(-1.0f / 362880.0f);
        p = simd::fma(simd::set1(1.0f / 40320.0f), p, u);
        p = simd::fma(simd::set1(-1.0f / 5040.0f), p, u);
        p = simd::fma(simd::set1(1.0f / 720.0f), p, u);
        p = simd::fma(simd::set1(-1.0f / 120.0f), p, u);
        p = simd::fma(simd::set1(1.0f / 24.0f), p, u);
        p = simd::fma(simd::set1(-1.0f / 6.0f), p, u);
        p = simd::fma(simd::set1(0.5f), p, u);
        const simd::f32x4 series = simd::mul(simd::mul(u, u), p);
        const simd::f32x4 direct = simd::add(simd::sub(u, simd::set1(1.0f)), e);
        return simd::sel(simd::gt(simd::set1(kSeriesBelow), u), series, direct);
    }

    // FF target GR (dB, >= 0) at detector level x (dB), per lane (header comment).
    static simd::f32x4 target(const Coeffs& c, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 oc = onset(c, l);
        const simd::f32x4 o = simd::max(simd::set1(0.0f), simd::sub(x, l.thrDb));      // x second: NaN stays NaN
        const simd::f32x4 u = simd::div(o, oc);
        const simd::f32x4 e = fcdsp::exp2(simd::mul(u, simd::set1(-kLog2E)));
        const simd::f32x4 r = simd::mul(simd::mul(l.slope, oc), shape(u, e));
        return simd::fma(r, x, simd::set1(0.0f));                                   // + x*0: poison stays poison
    }

    // d r^ / dx: S (1 - e^-u) above the threshold, 0 at and below it (u = 0 gives e = 1 exactly). FeedbackZdf's Newton
    // uses it (FeedbackZdf.h detail::HasSlope).
    static simd::f32x4 slope(const Coeffs& c, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 oc = onset(c, l);
        const simd::f32x4 o = simd::max(simd::set1(0.0f), simd::sub(x, l.thrDb));
        const simd::f32x4 u = simd::div(o, oc);
        const simd::f32x4 e = fcdsp::exp2(simd::mul(u, simd::set1(-kLog2E)));
        return simd::mul(l.slope, simd::sub(simd::set1(1.0f), e));
    }

    // r^_fb(y): the FB curve at output level y (header comment; S10 optional hook).
    static simd::f32x4 rhatFb(const Coeffs& c, simd::f32x4 y, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return target(c, y, QuadKnee::fbLevel(l));
    }

    // FB: the increment d = r - base of THE root of r = base + A + B r^_fb(x - r) (header comment).
    static simd::f32x4 solveFb(const Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept
        FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f), half = simd::set1(0.5f);
        const simd::f32x4 k = QuadKnee::loopGain(l.slope);
        const simd::f32x4 oc = onset(c, l);
        const simd::f32x4 kOc = simd::mul(k, oc);
        const simd::f32x4 ob = simd::sub(simd::sub(x, l.thrDb), a.base);          // o' = x - T - base
        const Curve fb{ simd::div(one, oc), k, kOc };
        simd::f32x4 lo = a.A, slopeAt;
        // the bracket's upper end: r^_fb(o) = k o_c H(o / o_c) <= k max(0, o) (H(u) <= u), at the lower end's sense
        // point, so no curve evaluation is spent on it (header comment)
        const simd::f32x4 top = simd::mul(k, simd::max(zero, simd::sub(ob, a.A)));
        simd::f32x4 hi = simd::fma(a.A, a.B, top);
        const simd::f32x4 scale = simd::fma(simd::abs(a.A), a.B, top);            // the increment's terms
        {
            const simd::m32x4 based = simd::gt(simd::abs(a.base), zero);           // FeedbackZdf.h's slack
            hi = simd::sel(based, simd::fma(hi, simd::set1(kBasedSlack), scale), hi);
        }
        const simd::f32x4 tol = simd::mul(simd::set1(kStopRel), scale);
        // the start (header comment): the map's root on the curve's asymptote, d (1 + B k) = A + B k (o' - o_c), at or
        // below the root (H(u) >= u - 1), clamped to A. Only a start: the bracket stays [A, hi].
        const simd::f32x4 bk = simd::mul(a.B, k);
        simd::f32x4 d = simd::max(lo, simd::div(simd::fma(a.A, bk, simd::sub(ob, oc)), simd::fma(one, a.B, k)));
        simd::m32x4 done = simd::gt(zero, one);                                    // no lane yet
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const simd::f32x4 f = simd::sub(simd::sub(d, a.A), simd::mul(a.B, fb.at(simd::sub(ob, d), slopeAt)));
            const simd::m32x4 below = simd::ge(zero, f);                           // F(d) <= 0: the root is >= d
            lo = simd::sel(below, d, lo);
            hi = simd::sel(below, hi, d);
            const simd::f32x4 df = simd::fma(one, a.B, slopeAt);                   // F'(d) = 1 + B r^_fb' >= 1
            const simd::f32x4 next = simd::sub(d, simd::div(f, df));
            const simd::m32x4 inside = simd::band(simd::ge(next, lo), simd::ge(hi, next));   // false for NaN
            const simd::f32x4 cand = simd::sel(inside, next, simd::mul(half, simd::add(lo, hi)));
            const simd::m32x4 conv = simd::ge(tol, simd::abs(simd::sub(cand, d)));   // false for NaN
            d = simd::sel(done, d, cand);                                          // a converged lane stays frozen
            done = simd::bor(done, conv);
            const simd::f32x4 flag = simd::sel(done, one, zero);
            if (simd::lane<0>(flag) + simd::lane<1>(flag) + simd::lane<2>(flag) + simd::lane<3>(flag) == 4.0f)
                break;
        }
        return simd::fma(d, x, zero);                                              // + x*0: poison stays poison
    }

private:
    // The FB curve of one solve: r^_fb at an output overshoot o (dB over T) and its slope, sharing one exp2.
    struct Curve {
        simd::f32x4 inv, k, kOc;                    // 1 / o_c, the loop gain and k o_c
        simd::f32x4 at(simd::f32x4 o, simd::f32x4& slopeOut) const noexcept FCDSP_NONBLOCKING
        {
            const simd::f32x4 u = simd::mul(simd::max(simd::set1(0.0f), o), inv); // o second: NaN stays NaN
            const simd::f32x4 e = fcdsp::exp2(simd::mul(u, simd::set1(-kLog2E)));
            slopeOut = simd::mul(k, simd::sub(simd::set1(1.0f), e));
            return simd::mul(kOc, shape(u, e));
        }
    };
};

using ProgressiveKnee = ProgressiveKneeT<>;

static_assert(GainComputerPolicy<ProgressiveKnee> && HasRhatFb<ProgressiveKnee>
              && GainComputerPolicy<FeedbackZdf<ProgressiveKnee>>);

} // namespace fcdsp::stage
