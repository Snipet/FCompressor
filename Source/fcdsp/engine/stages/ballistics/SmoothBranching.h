#pragma once

// stage::SmoothBranching: smooth-branching ballistics in the GR domain (01 §5.2 catalogue; E §2.4, §3.5; Giannoulis
// et al. 2012; CTAGDRC). Per lane, with t the (linked) target GR:
//
//     c = t > r ? cA : cR          (attack while GR rises, release otherwise)
//     r <- r + c * (t - r)         c = 1 - alpha, alpha = e^(-1 / (tau fs))
//
// Both branches move toward the target (the "smooth" release branch), so the attack/release switch has no
// discontinuity, and r stays in [min(r, t), max(r, t)]: GR never goes negative.
//
// The rates are carried as 1 - alpha (S2 lead revision #4), computed by oneMinusAlpha() below without the
// cancellation of 1 - alphaFromTau: multi-second releases at 192-384 kHz keep their time constant (alphaFromTau's
// float alpha loses 0.3 % at 1e5 samples and 10 % at 3.8e6; this form stays within ~1e-6).
//
// Times are smoothed on control ticks in the log2 domain (01 §5.1 "per tick: attack/release/hold tau smoothed in the
// log domain"; E §4.6): 20 ms one-poles at fs / kTickSamples with an epsilon landing, after which the rate comes from
// the exact target tau. A value-initialised Coeffs is not primed: its first design() lands on the targets (prepare,
// snapParams). A coefficient step needs no ramp: the state is continuous across it (E §4.6).
//
// Float precision (S3 lead revision 3; F9). r + c*(t - r) in one float rounding stops moving once c*|t - r| is below
// half an ulp of r: toward a non-zero target in [8, 16) dB the plain recurrence stalled up to ulp(r) / (2c) short of it
// (0.005 dB for tau = 200 ms at 48 kHz, 0.11 dB for 5 s at 48 kHz, 0.9 dB for 5 s at 384 kHz), and well before that a
// slow one-pole's steps of a few ulp drop correlated rounding errors that bend its exponential (0.002 dB for 5 s at
// 48 kHz). The FF step therefore carries the rounding error in State::lo (the part of the step the rounding dropped,
// added back on the next step, so r + lo follows the exact exponential and r lands on the target) whenever it matters:
// for a slow one-pole (c < 2^-14, tau * fs > 16384 samples: 341 ms at 48 kHz) always, and for any one-pole within 16
// ulp of a stall (|c*(t - r)| < 2^-20 |r|). dsp.srsweep holds a 5 s release to the exact curve within 0.001 dB at 48
// and 384 kHz. Faster one-poles drop their rounding error while they move, as the plain recurrence did, so lo is
// exactly 0 whenever such a GR is visibly moving and a carry (01 §5.5, Carry::grDb = r) hands it over seamlessly; a
// hand-over from a slow one-pole drops at most half an ulp.
//
// Feedback (K2 #1; E §2.6; F9, S3): the affine map of the branch E's predictor picks, {alpha*r1, 1 - alpha} = {r1 -
// c*r1, c}. The predictor "attack if r^_fb(x - r1) > r1" needs no access to the gain computer: F_A(r1) = c_A (r1 -
// r^_fb(x - r1)), so the attack root exceeds r1 exactly when the predictor says attack; solveFb solves both branches
// and keeps the attack root where it exceeds r1, else the release root (both are r1 at equilibrium). commitFb accepts
// the LINKED r (K2 #5b), sets the attack flag where it rose, and clears lo: in FB the solve returns an absolute float
// root, so the sub-ulp part of a very slow FB release cannot be carried through the frozen FbAffine interface
// (documented limit: the plain recurrence's stall, e.g. 0.07 dB for a 3 s release at 48 kHz; the handoff requests an
// interface change).

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::stage {

// oneMinusAlpha lives in core/Units.h (S2 lead revision: shared by every long-release ballistics policy).
using fcdsp::oneMinusAlpha;

struct SmoothBranching {
    static constexpr float kTimeSmoothMs = 20.0f;   // log-domain time smoothing, per tick (01 §5.1)
    static constexpr float kLogLandEps = 1e-4f;     // log2 units (0.007 % of tau): land on the target below this
    static constexpr float kMinTauMs = 1e-6f;       // log2 floor for tau <= 0 (instantaneous at any rate)
    static constexpr float kStallGuard = 0x1p-20f;  // carry the rounding error while |step| < 2^-20 |r| (16 ulp) ...
    static constexpr float kSlowRate = 0x1p-14f;    // ... or always for a slow one-pole (1 - alpha < 2^-14)

    struct Coeffs {
        simd::f32x4 cA{}, cR{};                 // 1 - alpha, attack and release, every lane
        float tauAMs = 0, tauRMs = 0;           // the time constants the rates come from (ms): telemetry, REL EFF
        float logA = 0, logR = 0;               // their smoothed log2
        float kTick = 0;                        // 1 - alpha of the 20 ms time smoother at fs / kTickSamples
        bool primed = false;                    // false: the next design() lands on the targets
    };
    struct State {
        simd::f32x4 r{};                        // applied GR of this stage (dB, >= 0)
        simd::f32x4 atk{};                      // 1 where the last tick took the attack branch, else 0
        simd::f32x4 lo{};                       // the carried rounding error of r (header: float precision), dB
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float tgtA = log2Ms(p.atkTauMs), tgtR = log2Ms(p.relTauMs);
        if (!c.primed)
        {
            c.kTick = oneMinusAlpha(kTimeSmoothMs, x.fs / static_cast<float>(kTickSamples));
            c.logA = tgtA;
            c.logR = tgtR;
            c.primed = true;
        }
        else
        {
            c.logA = smoothLog(c.logA, tgtA, c.kTick);
            c.logR = smoothLog(c.logR, tgtR, c.kTick);
        }
        c.tauAMs = c.logA == tgtA ? p.atkTauMs : fcdsp::exp2(c.logA);
        c.tauRMs = c.logR == tgtR ? p.relTauMs : fcdsp::exp2(c.logR);
        c.cA = simd::set1(oneMinusAlpha(c.tauAMs, x.fs));
        c.cR = simd::set1(oneMinusAlpha(c.tauRMs, x.fs));
    }

    // FF: target GR -> applied GR.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const simd::m32x4 up = simd::gt(t, s.r);
        advance(s, simd::sel(up, c.cA, c.cR), t);
        s.atk = simd::sel(up, simd::set1(1.0f), simd::set1(0.0f));
        return s.r;
    }

    // One step of the value r + lo toward t at rate k (1 - alpha): r + lo <- r + lo + k*(t - r - lo), with r the float
    // the engine applies and lo the rounding error carried near a stall (header comment). The new r is ONE rounding of
    // r + (lo + k*d), so it moves monotonically with the value and never passes the target.
    static void advance(State& s, simd::f32x4 k, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 d = simd::sub(simd::sub(t, s.r), s.lo);       // t - (r + lo); lo = 0 -> exactly t - r
        const simd::f32x4 step = simd::fma(s.lo, k, d);                   // lo + k*d: the value's increment
        const simd::f32x4 next = simd::add(s.r, step);
        const simd::f32x4 kept = simd::sub(next, s.r);                    // exact when the step is small (Sterbenz)
        const simd::m32x4 carry = simd::bor(simd::gt(simd::set1(kSlowRate), k),
                                            simd::gt(simd::mul(simd::set1(kStallGuard), simd::abs(s.r)),
                                                     simd::abs(step)));
        s.lo = simd::sel(carry, simd::sub(step, kept), simd::set1(0.0f));
        s.r = simd::max(simd::set1(0.0f), next);    // a carried lo may undershoot 0 by a rounding; NaN stays NaN
    }

    // FB: the root of the branch E's predictor picks (header comment); solve(FbAffine) is ModeEngine's affine solve.
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 rA = solve(FbAffine{ simd::fms(s.r, c.cA, s.r), c.cA });
        const simd::f32x4 rR = solve(FbAffine{ simd::fms(s.r, c.cR, s.r), c.cR });
        return simd::sel(simd::gt(rA, s.r), rA, rR);
    }

    // FB: the linked r becomes the state.
    static void commitFb(const Coeffs&, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        s.atk = simd::sel(simd::gt(r, s.r), simd::set1(1.0f), simd::set1(0.0f));
        s.r = r;
        s.lo = simd::set1(0.0f);
    }

    // From Carry::grDb (the outgoing engine's ballistics GR, 01 §5.5); negative values clamp to 0.
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        s.r = simd::max(simd::set1(0.0f), grDb);
        s.atk = simd::set1(0.0f);
        s.lo = simd::set1(0.0f);
    }

    // The current GR of the stage, for Carry::grDb (BallisticsPolicy, S2 lead revision).
    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.r; }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State&) noexcept FCDSP_NONBLOCKING
    {
        return simd::set1(c.tauAMs);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State&) noexcept FCDSP_NONBLOCKING
    {
        return simd::set1(c.tauRMs);
    }

    // ControlIo::bits b0-1 for the lane with the larger GR (lanes 0-1): 1 attack, 3 release (GR above 0 and not
    // rising), 0 idle. No hold (2) and no auto-slow (b2) in this policy.
    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const float r0 = simd::lane<0>(s.r), r1 = simd::lane<1>(s.r);
        const bool first = r0 >= r1;
        const float r = first ? r0 : r1;
        const float atk = first ? simd::lane<0>(s.atk) : simd::lane<1>(s.atk);
        if (atk > 0.5f)
            return 1;
        return r > 0.0f ? uint8_t{3} : uint8_t{0};
    }

private:
    static float log2Ms(float tauMs) noexcept FCDSP_NONBLOCKING
    {
        return fcdsp::log2(tauMs > kMinTauMs ? tauMs : kMinTauMs);
    }

    // One per-tick step of the time smoother, landing exactly on the target when within kLogLandEps or stalled.
    static float smoothLog(float cur, float tgt, float k) noexcept FCDSP_NONBLOCKING
    {
        const float d = tgt - cur;
        const float next = cur + k * d;
        return (d < kLogLandEps && d > -kLogLandEps) || next == cur ? tgt : next;
    }
};

static_assert(BallisticsPolicy<SmoothBranching>);

} // namespace fcdsp::stage
