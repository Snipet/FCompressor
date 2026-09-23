#pragma once

// stage::ColourNone: no colour stage (01 §5.2 catalogue; the walking-skeleton Clean, and Clean's VOICE OFF). The wet
// signal passes unchanged, so with voice OFF a below-threshold Clean nulls bit-exactly against its mix-0 reference
// (01 §10.3, rigor clean, D8 (b)); drive does not apply (Clean resolves it n/a while VOICE is OFF). The static
// transfer is the identity.

#include "fcdsp/core/Rt.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct ColourNone {
    struct Coeffs {};
    struct State {};

    static void design(Coeffs&, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING {}
    static void process(const Coeffs&, State&, float*, const float*, int, int) noexcept FCDSP_NONBLOCKING {}
    static float transfer(const Coeffs&, float x, float) noexcept FCDSP_NONBLOCKING { return x; }
    static void reset(State&) noexcept FCDSP_NONBLOCKING {}
};

static_assert(ColourPolicy<ColourNone>);

} // namespace fcdsp::stage
