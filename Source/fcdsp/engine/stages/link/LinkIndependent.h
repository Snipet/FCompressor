#pragma once

// stage::LinkIndependent: no stereo link (01 §5.2 catalogue; E §8, "Independent"). Each channel lane keeps its own gain
// computer target (FF) or its own per-lane feedback solve (FB), whatever k = EngineParams::link says:
//
//     r_ch <- r_ch                                         on every lane, bit for bit
//
// For the Modes whose hardware has no link at all, or whose descriptor declares LinkLaw::independent (dual-mono
// units); the Mode's `link` entry is then n/a. In FB the identity is trivially non-expansive, so each lane stays the
// contraction its own solve is (K2 #5b).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"

namespace fcdsp::stage {

struct LinkIndependent {
    static simd::f32x4 apply(simd::f32x4 r, float /*link*/) noexcept FCDSP_NONBLOCKING { return r; }
};

static_assert(LinkPolicy<LinkIndependent>);

} // namespace fcdsp::stage
