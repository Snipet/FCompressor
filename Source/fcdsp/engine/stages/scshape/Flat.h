#pragma once

// stage::Flat: no Mode-internal side-chain shaping (01 §5.2 catalogue, FZ0 errata R-F0 #3). The linear SC after the
// host filters reaches the detector unchanged, and its magnitude response is 0 dB at every frequency.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct Flat {
    struct Coeffs {};
    struct State {};

    static void design(Coeffs&, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING {}
    static simd::f32x4 tick(const Coeffs&, State&, simd::f32x4 v) noexcept FCDSP_NONBLOCKING { return v; }
    static float magDb(const Coeffs&, float, float) noexcept FCDSP_NONBLOCKING { return 0.0f; }
};

static_assert(ScShapePolicy<Flat>);

} // namespace fcdsp::stage
