#pragma once

// stage::LinkMean: stereo link toward the mean of the two channels' gain reduction, blended by k = EngineParams::link
// (01 §5.2 catalogue; E §8: r_ch += k (combine(r_0, r_1) - r_ch), combine = mean):
//
//     m    = (r_0 + r_1) / 2
//     r_ch <- (1 - k) * r_ch + k * m                       on lanes 0-1 only; lanes 2-3 (aux) pass unchanged
//
// The Pro-C style "link %": at k the channel moves the fraction k of the way to the average, so its weight on the other
// channel is k / 2 (LinkCvSum's cross-feed law differs, see there). Written as LinkMax is, as the fused
// (1 - k) * r_ch + k * m (one rounding for the sum), so both ends are exact: k = 0 returns r_ch bit for bit
// (independent) and k = 1 returns m bit for bit on both lanes (fully linked: the two lanes are identical). m is formed
// from r_0 + r_1, which is commutative, and both lanes run the same operations, so swapping L and R swaps the result
// bit for bit (D9). k is clamped to [0, 1] (NaN reads as 0).
//
// FF: applied to the gain-computer targets before the ballistics (E §8). FB: after the per-lane solve and before the
// ballistics commit (K2 #5b). Each output is a convex combination of the two inputs, so the map is non-expansive in the
// max norm and an FB lane stays a contraction; the linked value is what the ballistics commit as their next state.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"

namespace fcdsp::stage {

struct LinkMean {
    static float amount(float link) noexcept FCDSP_NONBLOCKING
    {
        return link > 0.0f ? (link < 1.0f ? link : 1.0f) : 0.0f;
    }

    static simd::f32x4 apply(simd::f32x4 r, float link) noexcept FCDSP_NONBLOCKING
    {
        const float k = amount(link);
        const float m = 0.5f * (simd::lane<0>(r) + simd::lane<1>(r));
        const simd::f32x4 linked = simd::fma(simd::mul(simd::set1(1.0f - k), r), simd::set1(k), simd::set1(m));
        return simd::withLane<1>(simd::withLane<0>(r, simd::lane<0>(linked)), simd::lane<1>(linked));
    }
};

static_assert(LinkPolicy<LinkMean>);

} // namespace fcdsp::stage
