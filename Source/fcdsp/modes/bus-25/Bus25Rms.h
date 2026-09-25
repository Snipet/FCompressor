#pragma once

// modes::bus25::RmsCatch: Bus 25's RMS detector (the THAT 2252 of D §2.4 [V S7]), a Mode-local detector policy (M6,
// S11; 01 §10.7; docs/modes/bus-25.md). The mean square of the side chain in the log domain, as stage::RmsLog, with
// two choices of its own:
//
//     ms <- max( ms + c (v^2 - ms),  v^2 - kCatchRatio ms )          level = 10 log10(ms)          (per lane)
//     c   = 1 - alpha of the RMS window tau_w = kWindowPerRelease x the release tau (RELEASE, or the VAR pot)
//
// 1. The window follows the release. The 2252 averages on its timing capacitor, and a log-domain RMS detector's
//    window is also its fall rate (4.34 / tau_w dB/s, E §2.5c). tau_w = tau_R / 50 [H] (1 ms at the 50 ms position,
//    10 ms at the 0.5 s default, 40 ms at 2 s): a level drop reaches the gain computer well inside every release
//    position, so the RELEASE switch, which acts on the GR afterwards (SmoothBranching), is what a release measures
//    (+2.3 tau_w = +4.6 % on dsp.time's 20 dB step), while a steady tone reads its RMS with the ripple of a 10-40 ms
//    window at the default and slow positions (a 1 kHz sine within 0.035 dB at 10 ms, 0.009 dB at 40 ms; 110 Hz
//    +0.3 dB at 10 ms: dsp.bus25topo's rms rows). A fixed
//    20 ms window (RmsLog) measured 0.104 s on the 50 ms position and 4 ms on every attack below 1 ms (DW's trial).
// 2. A jump catch. The mean square never lags more than kCatchRatio (20 dB) below the instantaneous power: a sample
//    whose power exceeds 100 x the running mean square lifts it to v^2 - 100 ms (0.04 dB short of v^2 on a 40 dB jump,
//    then the window closes the rest). Program whose instantaneous power stays within 20 dB of its mean square (any
//    steady tone, noise, a mix: a Gaussian sample is 10 sigma away) never meets it and reads a true RMS; an onset from
//    20 dB or more below (from silence, a drum after a quiet bar, dsp.time's T - 20 -> T + 20 dB square step) reaches
//    the gain computer at once, so the ATTACK switch alone times it (the published .03-30 ms, which a 2252 window in
//    front could not deliver below ~0.2 tau_w). It is the strong end of the RMS detector's own program dependence (the
//    larger the jump, the faster it charges, E §2.5c), with no second state: the detector is ONE recurrence of one
//    state, like RmsLog, so a carry (Carry::detDb, a level) hands it over exactly (dsp.time's carry rows).
// The update is continuous and non-decreasing in v^2 (both branches are, and so is their max), so a louder sample never
// reads lower. A silent lane (ms flushed to 0) takes its first non-zero sample's power at once.
//
// Floating point: v is sanitised by the host (|v| <= 1e6, 01 §5.8), so v^2 <= 1e12 and 100 ms stay finite; dbFromMs
// floors at kMsFloor (01 §5.7). NaN in v or ms stays NaN on both backends (both max operands are NaN then). The
// window is designed on control ticks from the release TARGET (EngineParams::relTauMs): a coefficient step needs no
// ramp, the state is continuous across it (E §4.6).
//
// State: the mean square per lane. Seeded from a level in dB (Carry::detDb): ms = 10^(dB / 10), as RmsLog.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::modes::bus25 {

struct RmsCatch {
    static constexpr float kWindowPerRelease = 1.0f / 50.0f;   // [H] tau_w = tau_R / 50 (header 1.)
    static constexpr float kMinWindowMs = 1.0f;                // the window's floor (the 50 ms position's)
    static constexpr float kCatchRatio = 100.0f;               // [H] 20 dB: the jump catch (header 2.)
    static constexpr float kLog2PerDbPower = 0.332192809f;     // log2(10) / 10: 10^(dB/10) = exp2(dB * this)

    struct Coeffs { simd::f32x4 c{}; };                        // 1 - alpha of the RMS window, every lane
    struct State { simd::f32x4 ms{}; };                        // mean square per lane (linear power)

    // The RMS window (ms) at these parameters: tau_R / 50, floored at kMinWindowMs.
    static float windowMs(const EngineParams& p) noexcept FCDSP_NONBLOCKING
    {
        const float w = p.relTauMs * kWindowPerRelease;
        return w > kMinWindowMs ? w : kMinWindowMs;             // NaN relTauMs -> the floor
    }

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.c = simd::set1(oneMinusAlpha(windowMs(p), x.fs));
    }

    // One sample of the mean square (no dB conversion).
    static void accumulate(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 p = simd::mul(v, v);
        const simd::f32x4 avg = simd::fma(s.ms, c.c, simd::sub(p, s.ms));          // ms + c (v^2 - ms)
        const simd::f32x4 jump = simd::fms(p, simd::set1(kCatchRatio), s.ms);     // v^2 - 100 ms
        s.ms = simd::max(avg, jump);
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

static_assert(DetectorPolicy<RmsCatch>);

} // namespace fcdsp::modes::bus25
