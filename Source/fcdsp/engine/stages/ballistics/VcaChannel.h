#pragma once

// stage::VcaChannelT<kAutoSlot, kLinSlot>: the console channel's ballistics (01 §5.2 ballistics/ catalogue; Console E;
// D §2.4 the SSL E/G channel dynamics [V S26, S27]): a program-dependent AUTO attack and a logarithmic or linear
// release, both on SmoothBranching's GR-domain step (its time smoothing, its branch rule and its sub-ulp carry).
// Per lane, with t the (linked) target GR and r + lo the value (SmoothBranching.h):
//
//     attack (t > r)   k_A = cA                                          FAST: ATTACK's own time (1 ms)
//                      k_A = min(c(3 ms), c(30 ms) (1 + (t - r) / D0))    AUTO: "adjustable between 3ms and 30ms"
//     release          k_R = cR                                          LOG: RELEASE is the tau of the GR in dB
//                      k_R = min(1, s / (r + lo - t)),  s = L / (tauR fs)   LIN: a constant L dB per RELEASE
//     r + lo <- r + lo + k (t - r - lo)                                  (SmoothBranching::advance)
//
// AUTO: the rate grows with the overshoot t - r, so a transient's large overshoot is caught in about 3 ms and a slowly
// rising level at 30 ms: the attack follows the program (D §2.4 [V S27]). The rate is c(30 ms) (1 + d / D0) in 1 - alpha,
// which is tau(30 ms) / (1 + d / D0) to within 0.4 % at 48 kHz, capped at c(3 ms). D0 = kAutoKneeDb [H]: a 15 dB step
// reaches 1/e of its GR in about 5 ms, a 3 dB step in about 14 ms (docs/modes/console-e.md).
// LIN: the step is exactly -s dB per sample while the value lies more than s above the target (k < 1), then lands; so
// the GR falls at L / RELEASE dB per second (TimeLaw::rateDbPerS), whatever its depth, where LOG slows as it nears 0.
// L = kLinDb [H]: RELEASE is the time to recover 10 dB.
//
// Switching (m[kAutoSlot], m[kLinSlot]: 0 or 1): each law is blended into the other over kBlendMs by a per-tick
// one-pole on its weight (landing exactly), k = k_0 + w (k_1 - k_0), so a switch changes the rate, never the GR, and
// the rate moves over 20 ms instead of stepping (Bus G's AUTO lesson, bus-g.md: a hard change of slope reads as a click).
// With both weights at 0 the policy is SmoothBranching bit for bit.
//
// FB: the Mode that uses this policy is feed-forward only (kTopologies); solveFb / commitFb are SmoothBranching's
// (FAST attack, LOG release), which the concept requires.

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::stage {

template <int kAutoSlot, int kLinSlot>
struct VcaChannelT {
    static_assert(kAutoSlot >= 0 && kAutoSlot < 8 && kLinSlot >= 0 && kLinSlot < 8 && kAutoSlot != kLinSlot,
                  "VcaChannelT: two distinct EngineParams::m[8] slots");

    static constexpr float kAutoSlowMs = 30.0f;     // AUTO's slowest attack (D §2.4 [V S27])
    static constexpr float kAutoFastMs = 3.0f;      // AUTO's fastest attack (D §2.4 [V S27])
    static constexpr float kAutoKneeDb = 2.0f;      // [H] D0: the overshoot that doubles AUTO's rate
    static constexpr float kLinDb = 10.0f;          // [H] LIN: RELEASE is the time to recover this many dB
    static constexpr float kBlendMs = 20.0f;        // a switch's blend (per tick)
    static constexpr float kLandW = 1e-4f;

    struct Coeffs {
        SmoothBranching::Coeffs sb{};
        simd::f32x4 cAutoSlow{}, cAutoFast{};       // 1 - alpha at kAutoSlowMs and kAutoFastMs
        simd::f32x4 invKnee{};                      // 1 / kAutoKneeDb
        simd::f32x4 linStep{};                      // s = kLinDb / (tauR fs), dB per sample (tauR smoothed)
        simd::f32x4 wAuto{}, wLin{};                // the blends, 0 ... 1
        float autoW = 0, linW = 0;                  // their scalar values
        float kTick = 0;                            // 1 - alpha of the blend at fs / kTickSamples
        float fs = 0;
        bool primed = false;                        // false: the next design() lands on the targets
    };
    struct State {
        SmoothBranching::State sb{};
        simd::f32x4 kA{};                           // this sample's attack rate (attackNowMs)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        SmoothBranching::design(c.sb, p, x);
        const float tgtAuto = p.m[kAutoSlot] > 0.5f ? 1.0f : 0.0f;
        const float tgtLin = p.m[kLinSlot] > 0.5f ? 1.0f : 0.0f;
        if (!c.primed)
        {
            c.kTick = oneMinusAlpha(kBlendMs, x.fs / static_cast<float>(kTickSamples));
            c.autoW = tgtAuto;
            c.linW = tgtLin;
            c.primed = true;
        }
        else
        {
            c.autoW = blend(c.autoW, tgtAuto, c.kTick);
            c.linW = blend(c.linW, tgtLin, c.kTick);
        }
        c.fs = x.fs;
        c.cAutoSlow = simd::set1(oneMinusAlpha(kAutoSlowMs, x.fs));
        c.cAutoFast = simd::set1(oneMinusAlpha(kAutoFastMs, x.fs));
        c.invKnee = simd::set1(1.0f / kAutoKneeDb);
        const float samples = c.sb.tauRMs * 0.001f * x.fs;
        c.linStep = simd::set1(samples > 1.0f ? kLinDb / samples : kLinDb);
        c.wAuto = simd::set1(c.autoW);
        c.wLin = simd::set1(c.linW);
    }

    // FF: target GR -> applied GR.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f);
        const simd::f32x4 value = simd::add(s.sb.r, s.sb.lo);
        const simd::m32x4 up = simd::gt(t, s.sb.r);                        // SmoothBranching's branch rule
        // AUTO: min(c(3 ms), c(30 ms) (1 + overshoot / D0)), blended from ATTACK's own rate
        const simd::f32x4 over = simd::max(simd::sub(t, value), zero);
        const simd::f32x4 kAuto = simd::min(c.cAutoFast, simd::mul(c.cAutoSlow, simd::fma(one, over, c.invKnee)));
        const simd::f32x4 kA = simd::fma(c.sb.cA, c.wAuto, simd::sub(kAuto, c.sb.cA));
        // LIN: the constant step s as a rate on the distance left (1: land), blended from RELEASE's own rate
        const simd::f32x4 dist = simd::max(simd::sub(value, t), simd::set1(1e-30f));
        const simd::f32x4 kLin = simd::min(one, simd::div(c.linStep, dist));
        const simd::f32x4 kR = simd::fma(c.sb.cR, c.wLin, simd::sub(kLin, c.sb.cR));
        SmoothBranching::advance(s.sb, simd::sel(up, kA, kR), t);
        s.sb.atk = simd::sel(up, one, zero);
        s.kA = kA;
        return s.sb.r;
    }

    // FB: SmoothBranching's (header comment: the Mode is feed-forward only).
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        return SmoothBranching::solveFb(c.sb, s.sb, static_cast<Solve&&>(solve));
    }
    static simd::f32x4 fbFalls(const State& s) noexcept FCDSP_NONBLOCKING { return SmoothBranching::fbFalls(s.sb); }
    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        SmoothBranching::commitFb(c.sb, s.sb, r);
    }

    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        SmoothBranching::seed(s.sb, grDb);
        s.kA = simd::set1(0.0f);
    }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.sb.r; }

    // AUTO: the attack time this sample's rate stands for, 1000 / (k fs) ms (ATK EFF); else ATTACK's own.
    static simd::f32x4 attackNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        if (c.autoW < 0.5f || !(c.fs > 0.0f))
            return SmoothBranching::attackNowMs(c.sb, s.sb);
        const simd::f32x4 k = simd::max(s.kA, c.cAutoSlow);
        return simd::div(simd::set1(1000.0f / c.fs), k);
    }
    // RELEASE's own time (LIN: the time to recover kLinDb).
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return SmoothBranching::releaseNowMs(c.sb, s.sb);
    }

    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING { return SmoothBranching::status(s.sb); }

private:
    static float blend(float cur, float tgt, float k) noexcept FCDSP_NONBLOCKING
    {
        const float d = tgt - cur;
        const float next = cur + k * d;
        return (d < kLandW && d > -kLandW) || next == cur ? tgt : next;
    }
};

using VcaChannel = VcaChannelT<0, 1>;

static_assert(BallisticsPolicy<VcaChannel> && HasFbFalls<VcaChannel>);

} // namespace fcdsp::stage
