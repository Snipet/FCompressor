#pragma once

// stage::NoStage2: no second stage (01 §5.2 catalogue, §10.3; SPRINTS §7 D16). combine() passes the main path's GR
// through on lanes 0-1 and reports a stage-2 GR of 0 in the aux lanes (Stage2Policy: "s2 GR in aux lanes"), so a
// Mode without stage 2 never shows stage-2 activity whatever its aux lanes carried. No state, no coefficients.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct NoStage2 {
    struct Coeffs {};
    struct State {};

    static void design(Coeffs&, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING {}

    static simd::f32x4 combine(const Coeffs&, State&, simd::f32x4 r1, simd::f32x4, const LevelCtl&) noexcept
        FCDSP_NONBLOCKING
    {
        return simd::withLane<3>(simd::withLane<2>(r1, 0.0f), 0.0f);
    }

    static void seed(State&, simd::f32x4) noexcept FCDSP_NONBLOCKING {}

    // Stage-2 GR for Carry::s2GrDb: always 0 (the getter convention of SmoothBranching::grDb).
    static simd::f32x4 grDb(const State&) noexcept FCDSP_NONBLOCKING { return simd::set1(0.0f); }
};

static_assert(Stage2Policy<NoStage2>);

} // namespace fcdsp::stage
