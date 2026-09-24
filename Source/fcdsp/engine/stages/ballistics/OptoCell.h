#pragma once

// stage::OptoCell: the T4 cell's ballistics, a two-stage release with memory (01 §5.2 ballistics/ catalogue, §10.6 Opto
// 2A; E §2.5e, §2.7; D §2.2 "about 10 ms on average ... about 60 ms to 50 %, then 1-15 s for the rest, depending on the
// program's history"). Per lane, in the GR domain, with u the light (the curve's target GR, OptoCellCurve.h):
//
//     fast   r_f <- smooth-branching(u)           attack tau_on = EngineParams::atkTauMs, release tau_f = relTauMs
//     slow   s   <- one-pole toward beta u        charge tau_m (u beta above s: the memory builds), release tau_s
//     out    r    = max(r_f, s)                   the fast path never falls below the slow part (it is held there)
//
// The slow part is the cell's memory: it charges only while the light is on and only at tau_m (seconds), so its level
// after a passage is the program's history. When the light goes off, the fast path releases from the GR it held to
// the slow part's level, which then lets go at tau_s. The slow part never charges above beta u <= u / 2, so the GR
// passes 50 % on the fast path: the release is "60 ms to 50 %" whatever the history (tau_f is fitted to that), and
// the rest takes from a fraction of a second (a short passage leaves little memory) to about 13 s (a long, deep one
// leaves beta = half of the GR) (docs/modes/opto-2a.md fits and measures the constants). The attack moves the fast
// path only, so the static curve settles at the attack's speed and the memory never makes the GR creep.
//
// E §2.7's model keeps a third state per channel (a memory term m that sets the slow release, tau_s = 0.3 + 3.2 m s).
// This one folds the memory into the slow part's charge, so a channel's state is (r, s): the frozen Carry (01 §5.3)
// hands a ballistics policy four lanes (grDb), and grDb() packs {r_0, r_1, s_0, s_1}: channel 0-1's applied GR in
// lanes 0-1 as for every Mode, and their slow parts (a GR in dB, <= r) in the aux lanes, so a seeded engine continues
// the old one's cell exactly (dsp.time's carry row; a hand-over from another Mode, whose aux lanes hold its own GR,
// starts with the slow part charged: the GR is kept and let go no faster than the memory would, as DualRelease does).
//
// Feedback (K2 #1; 01 §5.3; E §2.6-2.7): the fast path is SmoothBranching's pair of affine maps {alpha r_1, 1 - alpha},
// solved by the Mode's computer (FeedbackDelayed<OptoCellCurve>: r = alpha r_1 + (1 - alpha) r^_fb(x - r_1), the naive
// loop E §2.7 finds exact enough for the cell's 10 ms, or FeedbackZdf's root when the guard trips); the branch is
// SmoothBranching's predictor (the attack root where it exceeds r_1). The slow part needs the light u itself, which the
// frozen interface hands out only through a solve: a third one, of the map {r_1 / 2, 1 / 2}, whose delayed root is
// (r_1 + u) / 2 at the same sense point x - r_1 (its fixed point is r_1 exactly), so u = 2 (root - r_1 / 2) to about an
// ulp of r_1 + u. (Recovering u from the fast root, (r - A) / B, divides the root's rounding by B: 4e-4 dB of error on
// a 0.08 dB light in the tail, which the slow part's slow average turns into a 1e-4 dB bias.) Where the guard trips,
// FeedbackZdf solves that map too, and u is the light at its own root (a mid-point between r_1 and the fast root).
// The slow part is this policy's own recurrence, not a solve, so it carries its rounding remainder
// (SmoothBranching::advance: a 5 s one-pole lands on its target at any rate, no FB sub-ulp stall); the fast path's root
// is absolute and cannot (ADR-66, the FbAffine base-GR item; docs/modes/opto-2a.md bounds the residual). The verdicts of solveFb travel to commitFb in `mutable` scratch words (Hold.h, DualRelease.h:
// the frozen concept hands solveFb a const State and ModeEngine calls commitFb right after it for the same sample).
// commitFb takes the LINKED r (K2 #5b): r becomes the fast path's state, and s never exceeds it.
//
// The times come from EngineParams (Mode-private constants in m[], 01 §4.2) at the slots the template names: kShareSlot
// = beta (0 ... 1/2), kChargeSlot = tau_m (ms), kSlowSlot = tau_s (ms). Opto 2A: m[2..4] (Opto2ADesc.cpp). Every time
// is smoothed per control tick in the log domain (SmoothBranching::Coeffs; a value-initialised Coeffs lands on its
// targets, the ModeEngine convention).
//
// Telemetry (the EFF readouts of the locked, program-dependent ATTACK and RELEASE slots, 02 §8.1): attackNowMs is the
// closed loop's attack at the operating point, tau_on / (1 + k L), with k = S / (1 - S) the loop gain and L = 1 -
// 10^(-u / 20) the light's share of it (the curve's local slope is k g / (1 + g) = k L): about 11 ms at 14 dB of GR,
// tau_on where the light is off (the loop is open; an external key's FF kernel is reported the same way). releaseNowMs
// is tau_s where the slow part holds the GR, else tau_f. status(): attack (1) while the fast path attacks and holds the
// GR, release (3) while any GR remains, idle (0); b2 while the slow part holds it (the memory's tail).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::stage {

template <int kShareSlot = 2, int kChargeSlot = 3, int kSlowSlot = 4>
struct OptoCellT {
    static_assert(kShareSlot >= 0 && kShareSlot < 8 && kChargeSlot >= 0 && kChargeSlot < 8 && kSlowSlot >= 0
                      && kSlowSlot < 8,
                  "OptoCellT: the constant slots index EngineParams::m[8]");
    using Path = SmoothBranching;
    static constexpr uint8_t kSlowHoldsBit = 1u << 2;          // ControlIo::bits b2
    static constexpr float kMaxShare = 0.5f;                     // beta <= 1/2: the GR passes 50 % on the fast path

    struct Coeffs {
        Path::Coeffs fast{};                    // cA = attack (atkTauMs), cR = fast release (relTauMs)
        Path::Coeffs slow{};                    // cA = memory charge (m[kChargeSlot]), cR = slow release (m[kSlowSlot])
        simd::f32x4 share{};                    // beta
        float loop = 0;                         // the loop gain k = S / (1 - S), for attackNowMs
    };
    struct State {
        Path::State fast{};                     // r = the applied GR; atk; lo (FF only)
        Path::State slow{};                     // r = s, the slow part; atk = 1 while it charges; lo, its remainder
        simd::f32x4 light{};                    // u of the last sample (the LIGHT internal)
        mutable Path::State fbSlow{};           // FB scratch (solveFb -> commitFb): the slow part's next state
        mutable simd::f32x4 fbLight{};          //   and the light the solve used
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        Path::design(c.fast, p, x);
        EngineParams ps = p;
        ps.atkTauMs = p.m[kChargeSlot];
        ps.relTauMs = p.m[kSlowSlot];
        Path::design(c.slow, ps, x);
        const float b = p.m[kShareSlot];
        c.share = simd::set1(b > 0.0f ? (b < kMaxShare ? b : kMaxShare) : 0.0f);   // NaN reads as 0 (no memory)
        c.loop = QuadKnee::loopGain(p.slope);
    }

    // FF (an external key, E §2.6): target GR -> applied GR.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        Path::tick(c.fast, s.fast, t);                                       // r_f: SmoothBranching's FF step
        chargeSlow(c, s.slow, t);
        s.light = t;
        return glue(s);
    }

    // FB: the fast root (SmoothBranching's predictor) and the slow part's step, max of the two (header comment).
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r1 = s.fast.r;
        const simd::f32x4 aA = simd::fms(r1, c.fast.cA, r1);                 // alpha_A r_1 = r_1 - c_A r_1
        const simd::f32x4 aR = simd::fms(r1, c.fast.cR, r1);
        const simd::f32x4 rA = solve(FbAffine{ aA, c.fast.cA });
        const simd::f32x4 rR = solve(FbAffine{ aR, c.fast.cR });
        const simd::m32x4 attack = simd::gt(rA, r1);
        const simd::f32x4 rf = simd::sel(attack, rA, rR);
        const simd::f32x4 half = simd::set1(0.5f);
        const simd::f32x4 hA = simd::mul(half, r1);                          // {r_1 / 2, 1 / 2}: the light (header)
        const simd::f32x4 d = simd::sub(solve(FbAffine{ hA, half }), hA);
        const simd::f32x4 u = simd::max(simd::set1(0.0f), simd::add(d, d));
        Path::State next = s.slow;
        chargeSlow(c, next, u);
        s.fbSlow = next;
        s.fbLight = u;
        return simd::max(rf, next.r);
    }

    // FB: the linked r becomes the applied GR; the slow part takes its step, never above r.
    static void commitFb(const Coeffs&, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        s.fast.atk = simd::sel(simd::gt(r, s.fast.r), simd::set1(1.0f), zero);
        s.fast.r = simd::max(zero, r);
        s.fast.lo = zero;
        s.slow = s.fbSlow;
        clampSlow(s);
        s.light = s.fbLight;
    }

    // From Carry::grDb = {r_0, r_1, s_0, s_1} (grDb() below): the channels' GR and their slow parts; aux lanes copy the
    // channels. A carry from another Mode brings its aux GR there: the slow part starts at min(it, r), charged.
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 g = simd::max(zero, grDb);
        const float r0 = simd::lane<0>(g), r1 = simd::lane<1>(g);
        alignas(16) const float rv[4] = { r0, r1, r0, r1 };
        alignas(16) const float sv[4] = { simd::lane<2>(g), simd::lane<3>(g), simd::lane<2>(g), simd::lane<3>(g) };
        Path::seed(s.fast, simd::load(rv));
        Path::seed(s.slow, simd::load(sv));
        clampSlow(s);
        s.light = zero;
        s.fbSlow = s.slow;
        s.fbLight = zero;
    }

    // Carry::grDb: {r_0, r_1, s_0, s_1} (the aux lanes carry the channels' slow parts; header comment).
    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = s.fast.r;
        return simd::withLane<3>(simd::withLane<2>(r, simd::lane<0>(s.slow.r)), simd::lane<1>(s.slow.r));
    }

    // The parts, for a Mode's internals: the applied GR, the slow part, the light.
    static simd::f32x4 appliedDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.fast.r; }
    static simd::f32x4 slowDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.slow.r; }
    static simd::f32x4 lightDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.light; }
    static float shareOf(const Coeffs& c) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(c.share); }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 one = simd::set1(1.0f);
        const simd::f32x4 dark = fcdsp::linFromDb(simd::neg(simd::max(simd::set1(0.0f), s.light)));   // 1 - L
        return simd::div(simd::set1(c.fast.tauAMs), simd::fma(one, simd::set1(c.loop), simd::sub(one, dark)));
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::sel(slowHolds(s), simd::set1(c.slow.tauRMs), simd::set1(c.fast.tauRMs));
    }

    // ControlIo::bits for the lane with the larger GR (lanes 0-1): 1 while the fast path attacks and holds the GR, 3
    // while GR remains, 0 idle; b2 while the slow part holds it.
    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = s.fast.r;
        const bool first = simd::lane<0>(r) >= simd::lane<1>(r);
        const float rr = first ? simd::lane<0>(r) : simd::lane<1>(r);
        const float sl = first ? simd::lane<0>(s.slow.r) : simd::lane<1>(s.slow.r);
        const float atk = first ? simd::lane<0>(s.fast.atk) : simd::lane<1>(s.fast.atk);
        const bool held = rr > 0.0f && sl >= rr;
        const uint8_t phase = atk > 0.5f && !held ? uint8_t{ 1 } : (rr > 0.0f ? uint8_t{ 3 } : uint8_t{ 0 });
        return static_cast<uint8_t>(phase | (held ? kSlowHoldsBit : 0u));
    }

private:
    // The slow part's step toward beta u: charge (tau_m) while beta u is above it, release (tau_s) otherwise, with its
    // rounding remainder carried (SmoothBranching::advance).
    static void chargeSlow(const Coeffs& c, Path::State& sl, simd::f32x4 u) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 t = simd::mul(c.share, u);
        const simd::m32x4 charge = simd::gt(t, sl.r);
        Path::advance(sl, simd::sel(charge, c.slow.cA, c.slow.cR), t);
        sl.atk = simd::sel(charge, simd::set1(1.0f), simd::set1(0.0f));
    }

    // r = max(r_f, s): where the slow part holds the GR the fast path is held at it (its remainder dropped).
    static simd::f32x4 glue(State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::m32x4 held = simd::gt(s.slow.r, s.fast.r);
        s.fast.r = simd::sel(held, s.slow.r, s.fast.r);
        s.fast.lo = simd::sel(held, simd::set1(0.0f), s.fast.lo);
        return s.fast.r;
    }

    // s <= r: a seed from a foreign carry may bring an aux GR above r (in FB the root is >= s and the link only raises
    // it, so this is a no-op there): clamp it, remainder dropped.
    static void clampSlow(State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::m32x4 over = simd::gt(s.slow.r, s.fast.r);
        s.slow.r = simd::sel(over, s.fast.r, s.slow.r);
        s.slow.lo = simd::sel(over, simd::set1(0.0f), s.slow.lo);
    }

    static simd::m32x4 slowHolds(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::band(simd::gt(s.fast.r, simd::set1(0.0f)), simd::ge(s.slow.r, s.fast.r));
    }
};

using OptoCell = OptoCellT<>;

static_assert(BallisticsPolicy<OptoCell>);

} // namespace fcdsp::stage
