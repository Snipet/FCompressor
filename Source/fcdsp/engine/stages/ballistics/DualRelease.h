#pragma once

// stage::DualRelease: the two-path, program-dependent release of a VCA bus compressor's AUTO position (01 §5.2
// catalogue; 01 §10.4 Bus G RELEASE AUTO; E §2.5b after the SSL G circuit description: "the smaller capacitor charges
// first ... if the signal stays at high level long enough, then the larger capacitor will also charge"; K2 #1). Per
// lane, in the GR domain, with t the (linked) target GR:
//
//     fast   r_f <- smooth-branching(t)        attack tau_A (EngineParams::atkTauMs), release tau_Rf
//     slow   r_s <- one-pole toward r_f        tau_C while r_f > r_s (charging), else tau_Rs
//     out    r    = max(r_f, r_s)
//
// A short transient charges r_f only, so the GR releases at tau_Rf; a sustained one also charges r_s, which then holds
// the GR and lets it go at tau_Rs. Both paths are SmoothBranching one-poles (their Coeffs and State, reused as is): the
// times are smoothed per control tick in the log domain like every ballistics time (01 §5.1), a value-initialised
// Coeffs lands on its targets (the ModeEngine convention), and the slow path, which runs below 2^-14 per sample at any
// tau_Rs above 341 ms at 48 kHz, carries its rounding remainder (SmoothBranching.h "float precision") so a 1.2 s
// release follows its exponential to the target.
//
// The three times come from EngineParams::m[] (Mode-private constants, 01 §4.2) at the slots the template names, so a
// Mode chooses where its physical() puts them: DualRelease = DualReleaseT<0, 1, 2> reads m[0] = tau_Rf, m[1] = tau_C,
// m[2] = tau_Rs (ms; Bus G, BusGDesc.cpp). A Mode whose m[0..2] carry other meanings (Diode 609: m[0] SC corner, m[1]
// A1/A2) instantiates DualReleaseT with free slots.
//
// Feedback (K2 #1; 01 §5.2-5.3; E §2.6). Every path is affine in the gain computer's output r^ = r^_fb(x - r) given the
// state (r_f1, r_s1) and the branch rates c = 1 - alpha:
//
//     fast   {alpha_f r_f1, 1 - alpha_f}                                     A_f, B_f
//     slow   {alpha_s r_s1 + (1 - alpha_s) alpha_f r_f1, (1 - alpha_s)(1 - alpha_f)}   A_s = alpha_s r_s1 + c_s A_f, B_s = c_s B_f
//
// (the slow path follows the fast one within the same sample: a serial chain composes affinely), and the applied GR
// is the max of the two roots, which is THE root of r = max(g_f(r), g_s(r)) because each g is non-increasing in r
// (01 §5.3 "max of roots is exact"). The branches: the fast path takes SmoothBranching's predictor (its attack root
// where that root exceeds r_f1, else its release root); the slow path charges (c_C) where the fast root exceeds r_s1,
// else releases (c_Rs). Where the slow path charges, the fast root always wins (r_s is a convex combination of r_s1 and
// r_f, both below it), so the slow root matters only while the slow path releases.
//
// commitFb receives the LINKED r (K2 #5b) and advances both paths (the verdicts of solveFb travel in `mutable` scratch
// words, as in Hold.h: the frozen concept hands solveFb a const State, and ModeEngine always calls commitFb right after
// it, for the same sample):
//   - where the fast root won: r_f = r (as SmoothBranching commits), r_s = min(r, alpha_s r_s1 + c_s r_f) (exact);
//   - where the slow root won: r_s = r, and r_f = the fast path's own-loop root (the root of its own affine map).
//     The exact value, A_f + B_f r^_fb(x - r), is not reachable through the solve: recovering it from the slow root,
//     (r - alpha_s r_s1) / c_s, divides a float rounding by c_s (1.7e-5 for 1.2 s at 48 kHz: about 0.03 dB of noise per
//     sample in r_f). The own-loop root is its upper bound (g_f is non-increasing and r >= root_f), off by at most
//     B_f * k * (r - root_f) per sample, and both settle on the same static FB equilibrium once r_s has released onto
//     r_f. The applied GR itself is always the exact max of roots of the documented maps (dsp.dualrelease).
// Both paths drop their rounding remainder in FB (SmoothBranching.h: the solve returns an absolute root).
//
// Seeding (01 §5.5, Carry::grDb): both paths take the carried GR, i.e. the slow path starts charged: a hand-over into
// AUTO (a Mode switch, or AutoSwitch from a manual release) keeps the GR where it is and releases it no faster than
// the slow path would, instead of dropping onto the fast release while compressing.
//
// Telemetry: attackNowMs is the fast path's attack; releaseNowMs is tau_Rs where the slow path holds the GR (r_s > r_f),
// else tau_Rf. status() reports the fast path's attack on the louder lane while it holds the GR, release while any GR
// remains, and b2 ("auto-slow") while the slow path holds it.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::stage {

template <int kFastSlot = 0, int kChargeSlot = 1, int kSlowSlot = 2>
struct DualReleaseT {
    static_assert(kFastSlot >= 0 && kFastSlot < 8 && kChargeSlot >= 0 && kChargeSlot < 8 && kSlowSlot >= 0
                      && kSlowSlot < 8,
                  "DualReleaseT: the time slots index EngineParams::m[8]");
    using Path = SmoothBranching;
    static constexpr uint8_t kAutoSlowBit = 1u << 2;            // ControlIo::bits b2

    struct Coeffs {
        Path::Coeffs fast{};                    // cA = attack (atkTauMs), cR = fast release (m[kFastSlot])
        Path::Coeffs slow{};                    // cA = charge (m[kChargeSlot]), cR = slow release (m[kSlowSlot])
    };
    struct State {
        Path::State f{};                        // fast path: r_f, its attack flag, its rounding remainder
        Path::State s{};                        // slow path: r_s, 1 while charging, its rounding remainder
        mutable simd::f32x4 fbFastRoot{};       // FB scratch (solveFb -> commitFb): the fast path's own root
        mutable simd::f32x4 fbSlowWon{};        //   1 where the slow root won the max
        mutable simd::f32x4 fbCharge{};         //   1 where the slow path charges
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        EngineParams pf = p;
        pf.relTauMs = p.m[kFastSlot];
        Path::design(c.fast, pf, x);
        EngineParams ps = p;
        ps.atkTauMs = p.m[kChargeSlot];
        ps.relTauMs = p.m[kSlowSlot];
        Path::design(c.slow, ps, x);
    }

    // FF: target GR -> applied GR.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 rf = Path::tick(c.fast, s.f, t);
        const simd::f32x4 rs = Path::tick(c.slow, s.s, rf);        // charges (cA) while r_f > r_s, else releases
        return simd::max(rf, rs);
    }

    // FB: the max of the fast and the slow root (header comment).
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f);
        const simd::f32x4 rf1 = s.f.r, rs1 = s.s.r;
        const simd::f32x4 aAtk = simd::fms(rf1, c.fast.cA, rf1);          // alpha_A r_f1 = r_f1 - c_A r_f1
        const simd::f32x4 aRel = simd::fms(rf1, c.fast.cR, rf1);
        const simd::f32x4 rootAtk = solve(FbAffine{ aAtk, c.fast.cA });
        const simd::f32x4 rootRel = solve(FbAffine{ aRel, c.fast.cR });
        const simd::m32x4 attack = simd::gt(rootAtk, rf1);
        const simd::f32x4 rootF = simd::sel(attack, rootAtk, rootRel);
        const simd::f32x4 aF = simd::sel(attack, aAtk, aRel);
        const simd::f32x4 bF = simd::sel(attack, c.fast.cA, c.fast.cR);

        const simd::m32x4 charge = simd::gt(rootF, rs1);
        const simd::f32x4 cS = simd::sel(charge, c.slow.cA, c.slow.cR);
        const simd::f32x4 aS = simd::fma(simd::fms(rs1, cS, rs1), cS, aF);   // alpha_s r_s1 + c_s A_f
        const simd::f32x4 bS = simd::mul(cS, bF);                            // c_s B_f
        const simd::f32x4 rootS = solve(FbAffine{ aS, bS });

        const simd::m32x4 slowWon = simd::gt(rootS, rootF);
        s.fbFastRoot = rootF;
        s.fbSlowWon = simd::sel(slowWon, one, zero);
        s.fbCharge = simd::sel(charge, one, zero);
        return simd::max(rootF, rootS);
    }

    // FB: the linked r becomes the applied GR; both paths advance (header comment).
    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f);
        const simd::m32x4 slowWon = simd::gt(s.fbSlowWon, simd::set1(0.5f));
        const simd::m32x4 charge = simd::gt(s.fbCharge, simd::set1(0.5f));
        const simd::f32x4 rf = simd::sel(slowWon, simd::min(s.fbFastRoot, r), r);
        const simd::f32x4 cS = simd::sel(charge, c.slow.cA, c.slow.cR);
        const simd::f32x4 follow = simd::fma(simd::fms(s.s.r, cS, s.s.r), cS, rf);   // alpha_s r_s1 + c_s r_f
        const simd::f32x4 rs = simd::sel(slowWon, r, simd::min(follow, r));
        s.f.atk = simd::sel(simd::gt(rf, s.f.r), one, zero);
        s.f.r = simd::max(zero, rf);
        s.f.lo = zero;
        s.s.atk = s.fbCharge;
        s.s.r = simd::max(zero, rs);
        s.s.lo = zero;
    }

    // From Carry::grDb: both paths at the carried GR (the slow path charged; header comment).
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        Path::seed(s.f, grDb);
        Path::seed(s.s, grDb);
        s.fbFastRoot = s.f.r;
        s.fbSlowWon = simd::set1(0.0f);
        s.fbCharge = simd::set1(0.0f);
    }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return simd::max(s.f.r, s.s.r); }

    // The paths' GR, for a Mode's internals (Bus G: FAST ENV, SLOW ENV).
    static simd::f32x4 fastDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.f.r; }
    static simd::f32x4 slowDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.s.r; }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State&) noexcept FCDSP_NONBLOCKING
    {
        return simd::set1(c.fast.tauAMs);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::sel(simd::gt(s.s.r, s.f.r), simd::set1(c.slow.tauRMs), simd::set1(c.fast.tauRMs));
    }

    // ControlIo::bits for the lane with the larger GR (lanes 0-1): 1 while the fast path attacks and holds the GR, 3
    // while GR remains, 0 idle; b2 while the slow path holds the GR.
    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = grDb(s);
        const bool first = simd::lane<0>(r) >= simd::lane<1>(r);
        const float rr = first ? simd::lane<0>(r) : simd::lane<1>(r);
        const float rf = first ? simd::lane<0>(s.f.r) : simd::lane<1>(s.f.r);
        const float rs = first ? simd::lane<0>(s.s.r) : simd::lane<1>(s.s.r);
        const float atk = first ? simd::lane<0>(s.f.atk) : simd::lane<1>(s.f.atk);
        const bool slowHolds = rs > rf;
        const uint8_t phase = atk > 0.5f && !slowHolds ? uint8_t{ 1 } : (rr > 0.0f ? uint8_t{ 3 } : uint8_t{ 0 });
        return static_cast<uint8_t>(phase | (slowHolds ? kAutoSlowBit : 0u));
    }
};

using DualRelease = DualReleaseT<>;

static_assert(BallisticsPolicy<DualRelease>);

} // namespace fcdsp::stage
