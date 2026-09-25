#pragma once

// modes::bus25::CvSumBranching: Bus 25's ballistics, a Mode-local policy (M6, S11; 01 §5.2-5.3, §10.7; K2 #5b;
// ADR-67; docs/modes/bus-25.md). stage::SmoothBranching (the ATTACK and RELEASE switches, in the GR domain) with the
// API 2500's CV-sum link solved INSIDE the feedback loop.
//
// The link of the 2500: "the control voltages are summed together, but both channels are still detecting their own
// control voltages" (D §2.4 [V S6]). Each channel's detector and timing circuit make its own CV c_ch; the VCA of a
// channel follows the summing node r_ch = (1 - w) c_ch + w c_other, w = k / (1 + k) (stage::LinkCvSum, k = LINK). In
// OLD (feedback) each detector senses its own channel's OUTPUT, which the node's r_ch sets, so per sample
//
//     c'_ch = alpha c_ch + (1 - alpha) r^_fb(x_ch - r'_ch),   r'_ch = (1 - w) c'_ch + w c'_other           (*)
//
// (alpha = attack or release by E's predictor, r^_fb the FB curve, x_ch the channel's input level), a coupled
// two-lane system. ModeEngine's FB step solves each lane alone, links the roots and commits the LINKED value as the
// next state (Stage.h): with a CV-sum link that re-links every sample, so a partial link settled as a full one (40 %
// behaved as 99.9 %: ADR-67's known issue for this Mode). Here the state is the channel's own CV, and the link is
// solved in the affine map (the lead's S11 revision 4 suggestion): given the other lane's CV c'_o, the applied GR of a
// lane is the root of ModeEngine's affine solve
//
//     r' = A + B r^_fb(x - r'),   A = (1 - w)(c - k c) + w c'_o,   B = (1 - w) k      (k = 1 - alpha of the branch)
//
// and the lane's CV follows from it exactly, c' = (r' - w c'_o) / (1 - w). solveFb iterates that pair of solves
// (both lanes at once, Jacobi) from c'_o = the other lane's current CV until the CVs stop moving (kTolRel, relative)
// or kMaxIters, each iteration taking both branches' maps and E's predictor per lane (attack where the attack CV
// exceeds the lane's CV, as SmoothBranching). The iteration contracts: d c'_ch / d c'_o = -w B s / (1 + (1 - w) B s)
// (s = r^_fb' <= the loop gain), below w / (1 - w) <= 1 in magnitude, and small wherever the ballistics are slow; the
// system (*) has one solution (its Jacobian is diagonally dominant with positive diagonal). solveFb returns the CVs;
// ModeEngine then applies stage::LinkCvSum to them, which IS the node, so the applied GR is r' (up to the rounding of
// one blend), and commitFb keeps the CVs as the state (the linked value is the applied GR, not the next state).
//
// Choices where the design is silent:
//   - LINK IND (w = 0) runs stage::SmoothBranching's own FB step bit for bit (its FB sub-ulp carry included, S10); a
//     partial or full link uses the coupled solve on absolute roots, without the sub-ulp carry: a release slower
//     than ~2^14 samples may stop up to ulp(r) / (2 c) short of a non-zero target (0.07 dB at 3 s and 48 kHz, as
//     before S10; the FB sub-ulp carry does not extend to a coupled pair).
//   - w is designed on control ticks from EngineParams::link (LinkCvSum::weight), as every coefficient; the engine
//     links with the block's value, so for up to one tick after a LINK change the solve assumes the old node (a
//     stepped switch: one tick of a slightly different loop, no step in any state).
//   - FF (NEW): SmoothBranching's FF step on the linked target, bit for bit (01 §5.2: FF links the targets, E §8).
//   - grDb (the carry, telemetry) is the APPLIED GR in both kernels; seed() takes an applied GR as the CVs (a kernel
//     switch is crossfaded, 01 §5.5, so the incoming loop settles its CVs inside the fade).
//   - Lanes 2-3 (aux, unused by NoStage2) run uncoupled (w = 0 there).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/link/LinkCvSum.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes::bus25 {

struct CvSumBranching {
    using SB = stage::SmoothBranching;
    static constexpr int kMaxIters = 12;            // the coupled solve's iteration cap (header)
    static constexpr float kTolRel = 0x1p-21f;      // converged when no CV moves by more than this x (1 + |c|) dB

    struct Coeffs {
        SB::Coeffs sb{};                            // the ATTACK / RELEASE one-poles (SmoothBranching)
        float w = 0;                                // the CV-sum node's weight of the other channel, lanes 0-1
    };
    struct State {
        SB::State sb{};                             // FF: the applied GR; FB: each channel's own CV (unlinked)
        simd::f32x4 applied{};                      // the applied (linked) GR, both kernels
        mutable simd::f32x4 fbCv{};                 // FB scratch (solveFb -> commitFb): the solved CVs
        mutable simd::f32x4 fbAtk{};                //   and the branch each lane took (1 attack, 0 release)
        mutable int iters = 0;                      //   the iterations of the last coupled solve (internals, probes)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        SB::design(c.sb, p, x);
        c.w = stage::LinkCvSum::weight(p.link);
    }

    // FF: SmoothBranching on the linked target (header).
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        s.applied = SB::tick(c.sb, s.sb, t);
        return s.applied;
    }

    // The per-lane node weights {w, w, 0, 0} and {1 - w, 1 - w, 1, 1} (lanes 2-3 uncoupled).
    static simd::f32x4 weightOther(float w) noexcept FCDSP_NONBLOCKING
    {
        alignas(16) const float v[4] = { w, w, 0.0f, 0.0f };
        return simd::load(v);
    }
    static simd::f32x4 weightOwn(float w) noexcept FCDSP_NONBLOCKING
    {
        alignas(16) const float v[4] = { 1.0f - w, 1.0f - w, 1.0f, 1.0f };
        return simd::load(v);
    }
    // Lanes 0 and 1 swapped; lanes 2-3 zero (their weight is 0).
    static simd::f32x4 other(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        alignas(16) const float o[4] = { simd::lane<1>(v), simd::lane<0>(v), 0.0f, 0.0f };
        return simd::load(o);
    }

    // FB: the channels' CVs after this sample, from the coupled solve of (*) (header); w = 0: SmoothBranching's step.
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        if (!(c.w > 0.0f))
        {
            s.iters = 0;
            return SB::solveFb(c.sb, s.sb, solve);
        }
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f);
        const simd::f32x4 cv = s.sb.r;                                          // the CVs, per lane
        const simd::f32x4 wo = weightOther(c.w), wn = weightOwn(c.w);
        const simd::f32x4 kA = c.sb.cA, kR = c.sb.cR;
        const simd::f32x4 ownA = simd::mul(wn, simd::fms(cv, kA, cv));        // (1 - w)(c - kA c)
        const simd::f32x4 ownR = simd::mul(wn, simd::fms(cv, kR, cv));        // (1 - w)(c - kR c)
        const simd::f32x4 bA = simd::mul(wn, kA), bR = simd::mul(wn, kR);
        simd::f32x4 est = other(cv);                                            // c'_o, first guess: its CV now
        simd::f32x4 next = cv, atk = zero;
        int it = 0;
        while (it < kMaxIters)
        {
            ++it;
            const simd::f32x4 feed = simd::mul(wo, est);                       // w c'_o
            const simd::f32x4 rA = solve(FbAffine{ simd::add(ownA, feed), bA });
            const simd::f32x4 rR = solve(FbAffine{ simd::add(ownR, feed), bR });
            const simd::f32x4 cA = simd::div(simd::sub(rA, feed), wn);         // c' = (r' - w c'_o) / (1 - w)
            const simd::f32x4 cR = simd::div(simd::sub(rR, feed), wn);
            const simd::m32x4 up = simd::gt(cA, cv);                          // E's predictor, per lane
            const simd::f32x4 cand = simd::max(zero, simd::sel(up, cA, cR));  // NaN stays NaN
            const simd::f32x4 moved = simd::abs(simd::sub(cand, next));
            const simd::f32x4 bound = simd::mul(simd::set1(kTolRel), simd::add(one, simd::abs(cand)));
            next = cand;
            atk = simd::sel(up, one, zero);
            // converged in both channel lanes (a NaN lane stops the iteration: it cannot converge). On the first
            // iteration `moved` is each lane's step from its CV, which bounds the error of the other lane's estimate
            // (its CV now): steps within the tolerance are the fixed point already (a settled or idle pair: 2 solves).
            const bool done = !(simd::lane<0>(moved) > simd::lane<0>(bound))
                           && !(simd::lane<1>(moved) > simd::lane<1>(bound));
            if (done)
                break;
            est = other(next);
        }
        s.iters = it;
        s.fbCv = next;
        s.fbAtk = atk;
        return next;
    }

    // FB: the engine's linked value is the applied GR; the state keeps the solved CVs (w > 0) or takes SmoothBranching's
    // commit (w = 0).
    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        if (!(c.w > 0.0f))
            SB::commitFb(c.sb, s.sb, r);
        else
        {
            s.sb.atk = s.fbAtk;
            s.sb.r = s.fbCv;
            s.sb.lo = simd::set1(0.0f);
            s.sb.fbR = s.fbCv;
            s.sb.fbLo = simd::set1(0.0f);
        }
        s.applied = r;
    }

    // From Carry::grDb (an applied GR): the CVs and the applied GR both take it (header).
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        SB::seed(s.sb, grDb);
        s.applied = s.sb.r;
        s.fbCv = s.sb.r;
        s.fbAtk = simd::set1(0.0f);
        s.iters = 0;
    }

    // The applied GR (Carry::grDb, BallisticsPolicy).
    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.applied; }
    // Each channel's own CV: the FB state, or (FF) the applied GR.
    static simd::f32x4 cvDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.sb.r; }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return SB::attackNowMs(c.sb, s.sb);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return SB::releaseNowMs(c.sb, s.sb);
    }

    // ControlIo::bits b0-1 for the channel with the larger applied GR: 1 attack, 3 release, 0 idle (SmoothBranching's).
    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const float r0 = simd::lane<0>(s.applied), r1 = simd::lane<1>(s.applied);
        const bool first = r0 >= r1;
        const float r = first ? r0 : r1;
        const float atk = first ? simd::lane<0>(s.sb.atk) : simd::lane<1>(s.sb.atk);
        if (atk > 0.5f)
            return 1;
        return r > 0.0f ? uint8_t{3} : uint8_t{0};
    }
};

static_assert(BallisticsPolicy<CvSumBranching>);

} // namespace fcdsp::modes::bus25
