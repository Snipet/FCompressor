#pragma once

// stage::LinkCvSum: the control-voltage sum bus of the API 2500 / Manley style link (01 §5.2 catalogue, §10.7 Bus 25;
// E §8; D §2.4: "the control voltages are summed together, but both channels are still detecting their own control
// voltages"). Each channel keeps its own detector and gain computer; its control node sums its own CV (conductance 1)
// and the other channel's through the link pot (conductance k = EngineParams::link), a resistive summing node:
//
//     w    = k / (1 + k)
//     r_ch <- (r_ch + k * r_other) / (1 + k) = (1 - w) * r_ch + w * r_other      lanes 0-1; lanes 2-3 pass unchanged
//
// Choice where the design is silent (F5): the node is normalised (the conductances sum to 1), so a mono programme is
// compressed exactly as unlinked and the result is a convex combination of the two lanes. An unnormalised sum
// (r_0 + r_1 on both channels) doubles the GR of a centred source and is expansive (Lipschitz 2): in the FB kernel the
// linked value is committed as the ballistics' next state, and a silent lane would then grow without bound whenever
// the release coefficient exceeds 1/2 (K2 #5b requires a non-expansive link). At k = 1 the node is the mean, as
// LinkMean's; in between, the cross-feed weight k / (1 + k) is larger than LinkMean's k / 2 (50 %: 1/3 against 1/4):
// that is the law's difference, and what dsp.link judges for a Mode declaring LinkLaw::cvSum.
//
// Exactness: written as the fused (1 - w) * r_ch + w * r_other (one rounding for the sum). k = 0 gives w = 0 and returns
// r_ch bit for bit; k = 1 gives w = 1/2 exactly, and both lanes round the same exact sum r_0/2 + r_1/2, so the linked
// lanes are identical. Both lanes run the same operations with their roles swapped, so swapping L and R swaps the
// result bit for bit (D9). k is clamped to [0, 1] (NaN reads as 0).
//
// FF: applied to the gain-computer targets before the ballistics (E §8). FB: after the per-lane solve and before the
// ballistics commit (K2 #5b; Bus 25 OLD links here); non-expansive, so each lane stays a contraction.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"

namespace fcdsp::stage {

struct LinkCvSum {
    // The weight of the other channel's CV at the summing node: k / (1 + k), k clamped to [0, 1].
    static float weight(float link) noexcept FCDSP_NONBLOCKING
    {
        const float k = link > 0.0f ? (link < 1.0f ? link : 1.0f) : 0.0f;
        return k / (1.0f + k);
    }

    static simd::f32x4 apply(simd::f32x4 r, float link) noexcept FCDSP_NONBLOCKING
    {
        const float w = weight(link);
        const float r0 = simd::lane<0>(r), r1 = simd::lane<1>(r);
        const simd::f32x4 other = simd::withLane<1>(simd::withLane<0>(r, r1), r0);
        const simd::f32x4 linked = simd::fma(simd::mul(simd::set1(1.0f - w), r), simd::set1(w), other);
        return simd::withLane<1>(simd::withLane<0>(r, simd::lane<0>(linked)), simd::lane<1>(linked));
    }
};

static_assert(LinkPolicy<LinkCvSum>);

} // namespace fcdsp::stage
