#pragma once

// stage::LinkMax: stereo link by the louder channel, blended by k = EngineParams::link (01 §5.2 catalogue; E §8):
//
//     r_ch <- (1 - k) * r_ch + k * max(r_0, r_1)          on lanes 0-1 only; lanes 2-3 (aux) pass unchanged
//
// Written as the fused (1 - k)*r_ch + k*m (one rounding for the sum), so both ends are exact: k = 0 returns r_ch bit
// for bit (independent) and k = 1 returns max(r_0, r_1) bit for bit (fully linked). The two channel lanes run the same
// operations, so swapping L and R swaps the result bit for bit, and equal channels come back unchanged (D9). FF:
// applied to the gain-computer targets before the ballistics (E §8); FB: after the per-lane solve and before commit
// (K2 #5b).
// `max` is non-expansive, so an FB lane stays a contraction.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"

namespace fcdsp::stage {

struct LinkMax {
    static simd::f32x4 apply(simd::f32x4 r, float link) noexcept FCDSP_NONBLOCKING
    {
        const float r0 = simd::lane<0>(r), r1 = simd::lane<1>(r);
        const float m = r0 > r1 ? r0 : r1;
        const simd::f32x4 linked = simd::fma(simd::mul(simd::set1(1.0f - link), r), simd::set1(link), simd::set1(m));
        return simd::withLane<1>(simd::withLane<0>(r, simd::lane<0>(linked)), simd::lane<1>(linked));
    }
};

static_assert(LinkPolicy<LinkMax>);

} // namespace fcdsp::stage
