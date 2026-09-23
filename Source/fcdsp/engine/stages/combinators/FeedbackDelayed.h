#pragma once

// stage::FeedbackDelayed<G>: the one-sample-delay feedback loop, for slow loops only (01 §5.2 "FeedbackDelayed<G> (opto
// only)", §5.3 FB bullet; E §2.6, §2.7; K2 #5c). Instead of solving r = A + B r^_fb(x - r), the loop senses the GR it
// already holds:
//
//     r = A + B * r^_fb(x - r~),    r~ = A / (1 - B)
//
// r~ is the fixed point of the branch's affine map, which for a one-pole branch {alpha r1, 1 - alpha} is exactly the
// previous GR r1 (and for a hold {r1, 0} too): this is E §2.6's naive loop r[n] = alpha r1 + (1 - alpha) r^(x - r1),
// with r^_fb the FB curve of QuadKnee.h's convention (G::target with the loop gain k in place of the slope).
//
// Stability guard (K2 #5c: at run time, never a static_assert). Linearised on the attack branch, the loop's pole is
// p = alpha - (1 - alpha) k: it rings (p < 0) as soon as k > alpha / (1 - alpha), and diverges beyond (1 + alpha) /
// (1 - alpha) (E §2.6: a 20 us attack at 48 kHz buzzes at 4:1). design() computes the bound kLimit = alpha / (1 -
// alpha) at the ACTUAL fs from the loop's fastest pole, the attack (EngineParams::atkTauMs; the opto's ~10 ms gives 440
// at 44.1 kHz and 220 at 22.05 kHz). It runs in prepare() (prepare -> reset -> snapParams -> design) and again on every
// control tick, so a new rate or attack is guarded at once. Per sample, the worst-case loop gain of the curve, k(S)
// (QuadKnee-family laws never exceed their linear-region slope k at any level), is compared with the bound: within it
// the delayed loop runs; beyond it the solve falls back to FeedbackZdf<G> (the engine "switches that kernel to
// FeedbackZdf", 01 §5.3). An affine map with B = 1 in any lane (the static FB curve, a = {0, 1}, or an instantaneous
// branch, where kLimit = 0) always takes the ZDF solve: the delayed step is not an equilibrium there.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/combinators/FeedbackZdf.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <class G>
struct FeedbackDelayed {
    using Zdf = FeedbackZdf<G>;

    struct Coeffs {
        typename Zdf::Coeffs zdf{};
        float kLimit = 0;                       // alpha / (1 - alpha) of the attack pole at the actual fs
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        Zdf::design(c.zdf, p, x);
        c.kLimit = guardLimit(p.atkTauMs, x.fs);
    }

    // alpha / (1 - alpha) for a one-pole of tauMs at fs (0 for an instantaneous pole).
    static float guardLimit(float tauMs, float fs) noexcept FCDSP_NONBLOCKING
    {
        const float oneMinus = oneMinusAlpha(tauMs, fs);
        return (1.0f - oneMinus) / oneMinus;
    }

    // Whether the delayed loop runs for this sample's controls (else the ZDF fallback): the guard of the header.
    static bool delayedAt(const Coeffs& c, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING
    {
        const float k = simd::lane<0>(QuadKnee::loopGain(l.slope));    // LevelCtl lanes are broadcasts
        const bool branchesBelowOne = simd::lane<0>(a.B) < 1.0f && simd::lane<1>(a.B) < 1.0f
                                   && simd::lane<2>(a.B) < 1.0f && simd::lane<3>(a.B) < 1.0f;
        return k <= c.kLimit && branchesBelowOne;
    }

    static simd::f32x4 target(const Coeffs& c, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return Zdf::target(c.zdf, x, l);
    }

    static simd::f32x4 solveFb(const Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING
    {
        if (!delayedAt(c, l, a))
            return Zdf::solveFb(c.zdf, x, l, a);
        const simd::f32x4 held = simd::div(a.A, simd::sub(simd::set1(1.0f), a.B));     // r~ = A / (1 - B), B < 1
        const simd::f32x4 u = Zdf::curveFb(c.zdf, simd::sub(x, held), QuadKnee::fbLevel(l));
        return simd::fma(a.A, a.B, u);
    }
};

} // namespace fcdsp::stage
