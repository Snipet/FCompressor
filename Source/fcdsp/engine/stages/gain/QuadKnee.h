#pragma once

// stage::QuadKnee: the quadratic soft-knee gain computer (01 §5.2 catalogue; E §2.2; Giannoulis, Massberg & Reiss
// 2012), in the branchless form E §2.2 verified equal to the textbook one:
//
//     o = x - T,   q = clamp(o + W/2, 0, W),   r^ = S * (q^2 / (2W) + max(0, o - W/2))
//
// with T = thrDb, S = slope = 1 - 1/R and W = kneeDb, all taken PER SAMPLE from the LevelCtl (FZ0 errata, R-F0 #2):
// the engine's smoothed values, or staticGr's unsmoothed targets, so the settled engine and the analysis curve are the
// same function of the same floats. W is floored at kMinKneeDb (a hard knee is W -> 1e-4 dB, E §2.2), so the knee's
// division never sees 0. S in [0, 2] covers 1:1 ... inf:1 ... -1:1 (negative ratios are feed-forward only, E §2.6);
// r^ >= 0 for every S >= 0. Pure: no state, no coefficients (design() bakes nothing, per Stage.h's two-rate rule).
//
// Feedback (K2 #1; E §2.6; F9, S3). The loop senses the OUTPUT level y = x - r, and its gain k applies to the output
// overshoot, so the FB curve is the same knee with the loop gain in place of the slope:
//
//     r^_fb(y) = k * (q^2 / (2W) + max(0, o - W/2)),   o = y - T,   k = loopGain(S) = min(S / (1 - S), kMaxLoopGain)
//
// which gives R_eff = 1 + k = R (E §2.6; S >= 1, inf:1 and the FF-only negative ratios, clamps to k = 99 = 100:1). This
// is THE convention for every FB kernel: FeedbackZdf<G> and FeedbackDelayed<G> evaluate G::target with slope k as
// r^_fb, and dsp.registry's fb.monotone row samples it the same way (staticGr with topo FF and slope k).
//
// solveFb(c, x, l, {A, B}) returns THE root of r = A + B * r^_fb(x - r) (A >= 0, 0 <= B <= 1; F(r) = r - A -
// B r^_fb(x - r) is strictly increasing because r^_fb is non-decreasing, so the root is unique), by E §2.6's closed
// forms with alpha*r1 -> A and (1 - alpha) -> B, evaluated per lane and selected without branches:
//
//     b = x - T + W/2 (the input overshoot at the knee's start), c = b - A:
//     below the knee   c <= 0                       r = A
//     knee region      u = 2c / (1 + sqrt(1 + 4 kappa c)), kappa = B k / (2W),   valid while u <= W:   r = b - u
//     linear region    otherwise                    r = (A + B k (x - T)) / (1 + B k) = o - (o - A) / (1 + B k)
//
// The knee uses the stable root form (no cancellation for small kappa c), and the regions meet continuously (u = W
// exactly where the linear formula leaves the knee). a = {0, 1} gives the static FB curve (01 §7 staticGr, FB). B = 0
// (a hold) and c <= 0 return A exactly. The float result is within a few ulp of the exact root (dsp.fbsolve: <= 1e-5 dB
// against 200-step bisection for GR up to 60 dB, every region and every ballistics branch). NaN or inf in x gives NaN
// (the poison check sees it, 01 §5.8), as in target().

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct QuadKnee {
    static constexpr float kMinKneeDb = 1e-4f;      // W floor: a hard knee (E §2.2)
    static constexpr float kMaxLoopGain = 99.0f;    // FB loop gain clamp: 100:1 (E §2.6)

    struct Coeffs {};

    static void design(Coeffs&, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING {}

    // FF target GR (dB, >= 0) at detector level x (dB), per lane.
    static simd::f32x4 target(const Coeffs&, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 w = simd::max(simd::set1(kMinKneeDb), l.kneeDb);     // the constant first: NaN stays NaN
        const simd::f32x4 halfW = simd::mul(simd::set1(0.5f), w);
        const simd::f32x4 o = simd::sub(x, l.thrDb);
        const simd::f32x4 q = simd::min(w, simd::max(zero, simd::add(o, halfW)));
        const simd::f32x4 over = simd::max(zero, simd::sub(o, halfW));
        const simd::f32x4 knee = simd::div(simd::mul(q, q), simd::add(w, w));
        return simd::mul(l.slope, simd::add(over, knee));
    }

    // d r^ / dx of target(): S * clamp((o + W/2) / W, 0, 1). Continuous; FeedbackZdf's Newton uses it when present.
    static simd::f32x4 slope(const Coeffs&, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 w = simd::max(simd::set1(kMinKneeDb), l.kneeDb);
        const simd::f32x4 t = simd::div(simd::fma(simd::sub(x, l.thrDb), simd::set1(0.5f), w), w);
        return simd::mul(l.slope, simd::min(simd::set1(1.0f), simd::max(simd::set1(0.0f), t)));
    }

    // The FB loop gain k = S / (1 - S), clamped to [0, kMaxLoopGain] (S >= 1 -> kMaxLoopGain). NaN stays NaN.
    static simd::f32x4 loopGain(simd::f32x4 s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 kMax = simd::set1(kMaxLoopGain);
        const simd::f32x4 oneMinus = simd::sub(simd::set1(1.0f), s);
        // k <= kMax  <=>  S <= kMax (1 - S); beyond it (and for S >= 1, where the division is 1/0 or negative) clamp.
        const simd::m32x4 finiteK = simd::gt(simd::mul(kMax, oneMinus), s);
        const simd::f32x4 k = simd::div(s, simd::sel(finiteK, oneMinus, simd::set1(1.0f)));
        const simd::f32x4 clamped = simd::max(simd::set1(0.0f), simd::sel(finiteK, k, kMax));
        return simd::fma(clamped, s, simd::set1(0.0f));                  // + S*0: a NaN slope stays NaN (01 §5.8)
    }
    static float loopGain(float s) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(loopGain(simd::set1(s))); }

    // The LevelCtl of the FB curve: the same controls with the loop gain in place of the slope (header comment).
    static LevelCtl fbLevel(const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        LevelCtl f = l;
        f.slope = loopGain(l.slope);
        return f;
    }

    // FB: THE root of r = A + B * r^_fb(x - r) (header comment).
    static simd::f32x4 solveFb(const Coeffs&, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f);
        const simd::f32x4 w = simd::max(simd::set1(kMinKneeDb), l.kneeDb);
        const simd::f32x4 k = loopGain(l.slope);
        const simd::f32x4 bk = simd::mul(a.B, k);
        const simd::f32x4 o = simd::sub(x, l.thrDb);                              // input overshoot
        const simd::f32x4 b = simd::fma(o, simd::set1(0.5f), w);                  // o + W/2
        const simd::f32x4 c = simd::sub(b, a.A);

        // knee region: kappa u^2 + u - c = 0, u = b - r in [0, W]
        const simd::f32x4 kappa = simd::div(bk, simd::add(w, w));
        const simd::f32x4 cPos = simd::max(zero, c);
        const simd::f32x4 disc = simd::fma(one, simd::mul(simd::set1(4.0f), kappa), cPos);    // 1 + 4 kappa c
        const simd::f32x4 u = simd::div(simd::add(cPos, cPos), simd::add(one, simd::sqrt(disc)));
        const simd::f32x4 rKnee = simd::sub(b, u);

        // linear region: r = o - (o - A) / (1 + B k)
        const simd::f32x4 rLin = simd::sub(o, simd::div(simd::sub(o, a.A), simd::add(one, bk)));

        const simd::f32x4 r = simd::sel(simd::gt(u, w), rLin, rKnee);
        const simd::f32x4 root = simd::sel(simd::band(simd::gt(c, zero), simd::gt(a.B, zero)), r, a.A);
        return simd::fma(root, x, zero);                                 // + x*0: poison in x stays poison (01 §5.8)
    }
};

static_assert(GainComputerPolicy<QuadKnee>);

} // namespace fcdsp::stage
