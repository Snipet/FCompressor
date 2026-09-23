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
// Feedback (K2 #1): solveFb is F9's (S3): the closed form over FbAffine (E §2.6 with alpha*r1 -> A and 1 - alpha -> B;
// linear region, knee region with the stable root form, below the knee r = A), with k = S / (1 - S) clamped to 99.
// It is declared here so QuadKnee satisfies GainComputerPolicy now, and deliberately left undefined until then:
// ModeEngine instantiates the feedback step only for Traits whose kTopologies include kTopoFB, so an FB Mode built
// before F9 fails to link instead of running an unverified solver.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct QuadKnee {
    static constexpr float kMinKneeDb = 1e-4f;  // W floor: a hard knee (E §2.2)

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

    // FB: THE root of r = A + B*r^_fb(x - r). F9 (S3) defines it (see above).
    static simd::f32x4 solveFb(const Coeffs&, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING;
};

static_assert(GainComputerPolicy<QuadKnee>);

} // namespace fcdsp::stage
