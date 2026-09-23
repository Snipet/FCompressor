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
// Float precision (documented limit): r + c*(t - r) (one fused rounding) stops moving once c*|t - r| is below half an
// ulp of r. Toward 0 (every release to no GR) that never binds before FTZ; toward a non-zero target in [8, 16) dB it
// leaves r up to ulp(r) / (2c) above it: 0.005 dB for tau = 200 ms at 48 kHz, 0.11 dB for 5 s at 48 kHz, 0.9 dB for
// 5 s at 384 kHz.
//
// Feedback (K2 #1): solveFb and commitFb are F9's (S3): the affine map {alpha*r1, 1 - alpha} with alpha from E §2.6's
// predictor (attack if r^_fb(x - r1) > r1), then commit of the LINKED r. They are declared here so SmoothBranching
// satisfies BallisticsPolicy now, and deliberately left undefined until then (see QuadKnee.h).

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
        s.r = simd::fma(s.r, simd::sel(up, c.cA, c.cR), simd::sub(t, s.r));
        s.atk = simd::sel(up, simd::set1(1.0f), simd::set1(0.0f));
        return s.r;
    }

    // FB (F9, S3): r from the affine solve(s); then commit of the linked r. Declared only (see above).
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING;
    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING;

    // From Carry::grDb (the outgoing engine's ballistics GR, 01 §5.5); negative values clamp to 0.
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        s.r = simd::max(simd::set1(0.0f), grDb);
        s.atk = simd::set1(0.0f);
    }

    // The current GR of the stage, for Carry::grDb. Not (yet) part of BallisticsPolicy: ModeEngine uses it when a
    // policy provides it (S2 interface-change request: add it to the concept at FZ2).
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
