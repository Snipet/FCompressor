#pragma once

// stage::PeakLog: the instantaneous peak detector in the log domain (01 §5.2 catalogue; E §2.4, §3.3). Each sample's
// level is 20*log10|v| (dbFromLin, floored at kLinFloor = -240 dB), with no smoothing of its own: FCompressor's default
// placement puts the ballistics AFTER the gain computer, in the GR domain (E §2.4 placement 3), where attack and
// release act on GR changes independently of threshold and level. A sine therefore reads its peak (DetectorLaw::peak).
//
// State: the last level per lane (Carry::detDb, seed, telemetry). No coefficients.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct PeakLog {
    struct Coeffs {};
    struct State { simd::f32x4 lvl{}; };        // detector-law dB of the last sample, per lane

    static void design(Coeffs&, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING {}

    // Linear SC in, peak level in dB out.
    static simd::f32x4 tick(const Coeffs&, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        s.lvl = fcdsp::dbFromLin(v);
        return s.lvl;
    }

    // Carry::detDb is already in this detector's domain (peak dB); a cold engine seeds the floor (silence).
    static void seed(State& s, simd::f32x4 levelDb) noexcept FCDSP_NONBLOCKING { s.lvl = levelDb; }

    static simd::f32x4 levelDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.lvl; }
};

static_assert(DetectorPolicy<PeakLog>);

} // namespace fcdsp::stage
