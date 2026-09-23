#pragma once

// stage::RmsLog: the RMS detector in the log domain (01 §5.2 catalogue; E §2.4 "RMS (mean square)", placement 2; E
// §2.5c). A mean-square one-pole per lane, reported in dB:
//
//     ms <- ms + c * (v^2 - ms),   c = 1 - alpha = oneMinusAlpha(tau, fs)       level = 10 log10(ms) (dbFromMs)
//
// A sine therefore reads its peak - 3.01 dB (DetectorLaw::rms, the curve's x axis, C §5.2), a square its peak. The GR-
// domain ballistics still act after the gain computer (E §2.4 placement 3), so attack/release keep their meaning and
// the detector's own window adds the RMS integration in front of them.
//
// The time constant is a template parameter in microseconds (RmsLogT<TauUs>): RmsLog = 20 ms, Clean's RMS window
// (docs/modes/clean.md, [H]: the 2 f0 ripple of a 1 kHz sine stays below 0.02 dB, so D1 holds the clean tolerance, and
// the window is short enough to follow a phrase). A Mode that needs another window (dbx 160: ~30 ms, E §2.5c) names its
// own instantiation. The rate is designed on control ticks from the actual fs (not smoothed: it never changes at run
// time). Inputs are sanitised by the host (|v| <= 1e6, 01 §5.8), so v^2 <= 1e12 never overflows (K2 #13); dbFromMs
// floors the mean square at kMsFloor (01 §5.7).
//
// State: the mean square per lane. Seeded from a level in dB (Carry::detDb): ms = 10^(dB / 10).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <int TauUs>
struct RmsLogT {
    static_assert(TauUs > 0, "RmsLogT: the RMS time constant must be positive");
    static constexpr float kTauMs = static_cast<float>(TauUs) / 1000.0f;
    static constexpr float kLog2PerDbPower = 0.332192809f;      // log2(10) / 10: 10^(dB/10) = exp2(dB * this)

    struct Coeffs { simd::f32x4 c{}; };                         // 1 - alpha of the mean-square one-pole
    struct State { simd::f32x4 ms{}; };                         // mean square per lane (linear power)

    static void design(Coeffs& c, const EngineParams&, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.c = simd::set1(oneMinusAlpha(kTauMs, x.fs));
    }

    // One sample of the mean square (no dB conversion): shared with DualDet.
    static void accumulate(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        s.ms = simd::fma(s.ms, c.c, simd::sub(simd::mul(v, v), s.ms));    // ms + c (v^2 - ms), one rounding
    }

    // Linear SC in, RMS level in dB out.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        accumulate(c, s, v);
        return fcdsp::dbFromMs(s.ms);
    }

    // From a level in dB (Carry::detDb; a cold engine seeds the -240 dB floor).
    static void seed(State& s, simd::f32x4 levelDb) noexcept FCDSP_NONBLOCKING
    {
        s.ms = fcdsp::exp2(simd::mul(levelDb, simd::set1(kLog2PerDbPower)));
    }

    static simd::f32x4 levelDb(const State& s) noexcept FCDSP_NONBLOCKING { return fcdsp::dbFromMs(s.ms); }
};

using RmsLog = RmsLogT<20000>;

static_assert(DetectorPolicy<RmsLog>);

} // namespace fcdsp::stage
