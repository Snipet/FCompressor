#pragma once

// stage::MultiStage3: a three-stage, program-dependent release, the Fairchild 670's time-constant network at TC5 and
// TC6 (01 §5.2 ballistics/ catalogue; 01 §10.7 Mu 67 "TC5/TC6 as MultiStage3 branches, solved as the max of roots";
// E §2.5d-e, §2.7 "TC5 and TC6 use MultiStage with history-dependent weights"; D §2.3 TC5 "2 s for individual peaks,
// 10 s for multiple peaks", TC6 "0.3 s peaks, 10 s multiple peaks, 25 s for consistently high program"; K2 #1).
// M4 (S10).
//
// The network's main capacitor charges from the side-chain rectifier; further capacitors charge from it through
// resistors, so they fill only when the program keeps the first one charged, and then hold the control voltage up as
// they discharge. That is a serial chain of one-poles in the GR domain (DualRelease.h's two-path form, one path
// longer), per lane, with t the (linked) target GR:
//
//     stage 1   r1 <- smooth-branching(t)          attack tau_A (EngineParams::atkTauMs), release tau_R1
//     stage 2   r2 <- one-pole toward r1           tau_C2 while r1 > r2 (charging), else tau_R2
//     stage 3   r3 <- one-pole toward r2           tau_C3 while r2 > r3, else tau_R3
//     out       r   = max(r1, r2, r3)
//
// A short peak charges stage 1 and leaves a small charge in stage 2, so its GR recovers at tau_R1; repeated peaks fill
// stage 2, which then holds the GR and lets it go at tau_R2; a sustained programme fills stage 3 too (tau_R3). Each
// stage is a SmoothBranching one-pole (its Coeffs and State reused): times smoothed per control tick in the log domain
// (01 §5.1), a value-initialised Coeffs landing on its targets (the ModeEngine convention), and SmoothBranching's
// rounding carry, so a multi-second stage follows its exponential (SmoothBranching.h "float precision").
//
// The times come from EngineParams::m[] at the slots the template names (MultiStage3 = MultiStage3T<2, 3, 4, 5, 6>:
// m[2] tau_R1, m[3] tau_C2, m[4] tau_R2, m[5] tau_C3, m[6] tau_R3, in ms; the slots after a ProgressiveKnee's onset
// law, Mu67.h). A two-stage network (TC5) sets tau_C3 = tau_R3 = 0: stage 3 then follows stage 2 exactly (a rate of
// 1) and is not solved (Coeffs::three, from the smoothed rates, so a switch between TC5 and TC6 glides with the times
// and never steps the GR).
//
// Feedback (K2 #1; 01 §5.2-5.3; E §2.6). Given the state, every stage is affine in the gain computer's output
// r^ = r^_fb(x - r) (a serial chain composes affinely), with c the branch rates (1 - alpha):
//
//     stage 1   {A1, B1} = {(1 - k1) r1, k1}                          k1 = c_A or c_R1 (E's predictor, SmoothBranching)
//     stage 2   {A2, B2} = {(1 - c2) r2 + c2 A1, c2 B1}               c2 = c_C2 where root1 > r2, else c_R2
//     stage 3   {A3, B3} = {(1 - c3) r3 + c3 A2, c3 B2}               c3 = c_C3 where v2 > r3, else c_R3
//
// (v2: stage 2's value at the larger of roots 1 and 2, i.e. its follow value (1 - c2) r2 + c2 root1 where it charges,
// else root2), and the applied GR is the max of the three roots, THE root of r = max(g1(r), g2(r), g3(r)) because each
// g is non-increasing in r (01 §5.3 "max of roots is exact"). A charging stage never wins (its value is a convex
// combination of its previous value and the stage before it, both below that stage's root; DualRelease.h), so a stage
// is solved only in lanes where it releases: one to three solves, plus the carry's. A tie between the float roots goes
// to the later (releasing) stage: a later stage releasing lags the one before it, so its value is the larger one when
// both roots round to the same float, and handing the win to the earlier stage would cap the later one at that stage's
// value and throw away its sub-ulp lead (M4: 7.6e-5 dB lost by TC6's slowest stage while its first stages tied).
//
// FB sub-ulp carry (S10 interface revision, X10; lead revision 3 at the S10 base): the winner's value takes its step
// through a based solve, SmoothBranching::solveFb's pattern: where the FF gate holds for the winning stage (its own
// rate below SmoothBranching::kSlowRate, or its absolute root within kStallGuard of a stall), the winner's map in the
// frame of its float, with every value v_i = r_i + lo_i,
//     stage 1   {lo1 - k1 v1, k1, base r1}
//     stage 2   {lo2 - c2 v2 + c2 A1v, c2 k1, base r2},      A1v = (1 - k1) v1
//     stage 3   {lo3 - c3 v3 + c3 A2v, c3 c2 k1, base r3},   A2v = (1 - c2) v2 + c2 A1v
// returns the value's step, split into the float r' and the new remainder as SmoothBranching splits it. Each carried
// sample costs one more solve (the ns budget: docs/modes/mu-67.md).
//
// commitFb receives the LINKED r (K2 #5b) and the exact r^ at it (S10, Stage.h HasCommitFbRhat; ModeEngine passes it
// when the computer has rhatFb, as ProgressiveKnee does), and advances every stage along its map (the verdicts of
// solveFb travel in `mutable` scratch words, as in Hold.h and DualRelease.h):
//   - the winning stage takes r, with its carried remainder where r is its own solved value (a lane the link raised
//     takes the float, remainder 0);
//   - a stage before the winner takes its map's value at r, which is its one-pole step toward the stage before it (for
//     stage 1: toward r^, its target) at the branch rate the maps used, SmoothBranching::advance with its FF carry;
//   - a stage after the winner takes its one-pole step toward the new value of the stage before it.
//   Every value is capped at r (a float step may round above it). commitFb(c, s, r) without r^ (the concept's form)
//   gives stage 1, where it lost, its own-loop root (an upper bound of its map's value, DualRelease.h's fallback).
//
// Seeding (01 §5.5, Carry::grDb): every stage takes the carried GR (the network charged), so a hand-over into TC5/TC6
// (a Mode switch, or TcSelector's switch from TC1-4) keeps the GR and releases it no faster than the network would.
// Telemetry: attackNowMs is stage 1's attack; releaseNowMs the release tau of the stage that holds the GR (the largest,
// the earliest on a tie). status(): attack while stage 1 attacks and holds the GR, release while any GR remains, idle
// at 0; b2 ("auto-slow", ControlIo::bits) while stage 2 or 3 holds it.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::stage {

template <int kRel1Slot = 2, int kCharge2Slot = 3, int kRel2Slot = 4, int kCharge3Slot = 5, int kRel3Slot = 6>
struct MultiStage3T {
    static_assert(kRel1Slot >= 0 && kRel1Slot < 8 && kCharge2Slot >= 0 && kCharge2Slot < 8 && kRel2Slot >= 0
                      && kRel2Slot < 8 && kCharge3Slot >= 0 && kCharge3Slot < 8 && kRel3Slot >= 0 && kRel3Slot < 8,
                  "MultiStage3T: the time slots index EngineParams::m[8]");
    using Path = SmoothBranching;
    static constexpr uint8_t kProgramBit = 1u << 2;             // ControlIo::bits b2: a programme stage holds the GR

    struct Coeffs {
        Path::Coeffs s1{};                      // cA = attack (atkTauMs), cR = tau_R1 (m[kRel1Slot])
        Path::Coeffs s2{};                      // cA = tau_C2 (m[kCharge2Slot]), cR = tau_R2 (m[kRel2Slot])
        Path::Coeffs s3{};                      // cA = tau_C3 (m[kCharge3Slot]), cR = tau_R3 (m[kRel3Slot])
        bool three = false;                     // stage 3 runs: one of its smoothed rates is below 1
    };
    struct State {
        Path::State p1{}, p2{}, p3{};           // r, the attack / charging flag, the carried remainder per stage
        mutable simd::f32x4 fbWin{};            // FB scratch (solveFb -> commitFb): 1, 2 or 3, the winning stage
        mutable simd::f32x4 fbK1{};             //   the branch rates the maps used
        mutable simd::f32x4 fbC2{};
        mutable simd::f32x4 fbC3{};
        mutable simd::f32x4 fbRoot1{};          //   stage 1's own root (commitFb without r^)
        mutable simd::f32x4 fbR{};              //   the solved GR per lane
        mutable simd::f32x4 fbLo{};             //   the winner's carried remainder (0 where not carried)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        EngineParams p1 = p;
        p1.relTauMs = p.m[kRel1Slot];
        Path::design(c.s1, p1, x);
        EngineParams p2 = p;
        p2.atkTauMs = p.m[kCharge2Slot];
        p2.relTauMs = p.m[kRel2Slot];
        Path::design(c.s2, p2, x);
        EngineParams p3 = p;
        p3.atkTauMs = p.m[kCharge3Slot];
        p3.relTauMs = p.m[kRel3Slot];
        Path::design(c.s3, p3, x);
        c.three = simd::lane<0>(c.s3.cA) < 1.0f || simd::lane<0>(c.s3.cR) < 1.0f;
    }

    // FF: target GR -> applied GR (header comment).
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r1 = Path::tick(c.s1, s.p1, t);
        const simd::f32x4 r2 = Path::tick(c.s2, s.p2, r1);         // charges (cA) while r1 > r2, else releases
        if (c.three)
            return simd::max(r1, simd::max(r2, Path::tick(c.s3, s.p3, r2)));
        s.p3 = s.p2;                                                // TC5: stage 3 follows stage 2 exactly
        return simd::max(r1, r2);
    }

    // FB: the max of the stages' roots, the winner's value carried below an ulp (header comment).
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f), one = simd::set1(1.0f), two = simd::set1(2.0f),
                          three = simd::set1(3.0f);
        const simd::f32x4 r1 = s.p1.r, r2 = s.p2.r, r3 = s.p3.r;

        // stage 1: E's predictor over both branches (SmoothBranching::solveFb)
        const simd::f32x4 aAtk = simd::fms(r1, c.s1.cA, r1), aRel = simd::fms(r1, c.s1.cR, r1);
        const simd::f32x4 rootAtk = solve(FbAffine{ aAtk, c.s1.cA });
        const simd::f32x4 rootRel = solve(FbAffine{ aRel, c.s1.cR });
        const simd::m32x4 attack = simd::gt(rootAtk, r1);
        const simd::f32x4 root1 = simd::sel(attack, rootAtk, rootRel);
        const simd::f32x4 k1 = simd::sel(attack, c.s1.cA, c.s1.cR);
        const simd::f32x4 a1 = simd::sel(attack, aAtk, aRel);

        // stage 2: charging where stage 1's root exceeds it (then it cannot win and is not solved)
        const simd::m32x4 ch2 = simd::gt(root1, r2);
        const simd::f32x4 c2 = simd::sel(ch2, c.s2.cA, c.s2.cR);
        const simd::f32x4 keep2 = simd::fms(r2, c2, r2);                             // (1 - c2) r2
        const simd::f32x4 a2 = simd::fma(keep2, c2, a1);
        const simd::f32x4 b2 = simd::mul(c2, k1);
        simd::f32x4 root2 = root1;
        if (!all(ch2))
            root2 = simd::sel(ch2, root1, solve(FbAffine{ a2, b2 }));
        const simd::f32x4 v2 = simd::sel(ch2, simd::fma(keep2, c2, root1), root2);

        // stage 3 (TC6): charging where stage 2's value exceeds it
        simd::f32x4 c3 = one, a3 = a2, b3 = b2, root3 = root1;
        simd::m32x4 rel3 = simd::gt(zero, one);                                       // TC5: stage 3 never wins
        if (c.three)
        {
            const simd::m32x4 ch3 = simd::gt(v2, r3);
            rel3 = simd::ge(r3, v2);                                                  // not charging (NaN: neither)
            c3 = simd::sel(ch3, c.s3.cA, c.s3.cR);
            a3 = simd::fma(simd::fms(r3, c3, r3), c3, a2);
            b3 = simd::mul(c3, b2);
            if (!all(ch3))
                root3 = simd::sel(ch3, root1, solve(FbAffine{ a3, b3 }));
        }

        // the max of the releasing stages' roots (a charging stage never wins); a tie at float resolution goes to the
        // later stage, whose value is then the larger one (header comment)
        const simd::m32x4 w2 = simd::band(simd::ge(r2, root1), simd::ge(root2, root1));   // stage 2 releases, >= root1
        simd::f32x4 root = simd::sel(w2, root2, root1);
        simd::f32x4 win = simd::sel(w2, two, one);
        const simd::m32x4 w3 = simd::band(rel3, simd::ge(root3, root));
        root = simd::sel(w3, root3, root);
        win = simd::sel(w3, three, win);

        s.fbWin = win;
        s.fbK1 = k1;
        s.fbC2 = c2;
        s.fbC3 = c3;
        s.fbRoot1 = root1;
        s.fbR = root;
        s.fbLo = zero;

        // the carry (header comment): the winner's rate, float and remainder per lane
        const simd::f32x4 kw = simd::sel(w3, c3, simd::sel(w2, c2, k1));
        const simd::f32x4 rw = simd::sel(w3, r3, simd::sel(w2, r2, r1));
        const simd::f32x4 lw = simd::sel(w3, s.p3.lo, simd::sel(w2, s.p2.lo, s.p1.lo));
        const simd::m32x4 live = simd::gt(simd::add(simd::add(rw, root), simd::abs(lw)), zero);
        const simd::m32x4 carry = simd::band(live, Path::carries(kw, rw, simd::sub(root, rw)));
        const simd::f32x4 flag = simd::sel(carry, one, zero);
        if (simd::lane<0>(flag) + simd::lane<1>(flag) + simd::lane<2>(flag) + simd::lane<3>(flag) == 0.0f)
            return root;                                            // the absolute root in every lane

        const simd::f32x4 lo1 = s.p1.lo, lo2 = s.p2.lo, lo3 = s.p3.lo;
        const simd::f32x4 move1 = simd::fms(simd::fms(lo1, k1, lo1), k1, r1);          // lo1 - k1 (r1 + lo1)
        const simd::f32x4 a1v = simd::add(a1, simd::fms(lo1, k1, lo1));                // (1 - k1) v1
        const simd::f32x4 move2 = simd::fma(simd::fms(simd::fms(lo2, c2, lo2), c2, r2), c2, a1v);
        const simd::f32x4 a2v = simd::fma(simd::add(keep2, simd::fms(lo2, c2, lo2)), c2, a1v);
        const simd::f32x4 move3 = simd::fma(simd::fms(simd::fms(lo3, c3, lo3), c3, r3), c3, a2v);
        const simd::f32x4 moveA = simd::sel(w3, move3, simd::sel(w2, move2, move1));
        const simd::f32x4 moveB = simd::sel(w3, b3, simd::sel(w2, b2, k1));
        const simd::f32x4 step = solve(FbAffine{ moveA, moveB, rw });
        const simd::f32x4 next = simd::add(rw, step);
        const simd::f32x4 kept = simd::sub(next, rw);                                  // exact for a small step
        s.fbR = simd::sel(carry, simd::max(zero, next), root);                          // NaN stays NaN
        s.fbLo = simd::sel(carry, simd::sub(step, kept), zero);
        return s.fbR;
    }

    // FB with r^_fb at the committed GR (S10; header comment): every stage along its map at r.
    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r, simd::f32x4 rhat) noexcept FCDSP_NONBLOCKING
    {
        Path::State n1 = s.p1;
        Path::advance(n1, s.fbK1, rhat);                            // stage 1's map at r: its step toward r^
        commit(c, s, r, n1);
    }

    // FB without r^ (the concept's form): where stage 1 lost, its own-loop root (header comment).
    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        Path::State n1 = s.p1;
        n1.r = simd::min(s.fbRoot1, r);
        n1.lo = simd::set1(0.0f);
        commit(c, s, r, n1);
    }

    // From Carry::grDb: every stage at the carried GR (the network charged; header comment).
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        Path::seed(s.p1, grDb);
        Path::seed(s.p2, grDb);
        Path::seed(s.p3, grDb);
        const simd::f32x4 zero = simd::set1(0.0f);
        s.fbWin = simd::set1(1.0f);
        s.fbK1 = zero;
        s.fbC2 = zero;
        s.fbC3 = zero;
        s.fbRoot1 = s.p1.r;
        s.fbR = s.p1.r;
        s.fbLo = zero;
    }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::max(s.p1.r, simd::max(s.p2.r, s.p3.r));
    }

    // The stages' GR, for a Mode's internals (Mu 67: TC WEIGHT).
    static simd::f32x4 stage1Db(const State& s) noexcept FCDSP_NONBLOCKING { return s.p1.r; }
    static simd::f32x4 programDb(const State& s) noexcept FCDSP_NONBLOCKING { return simd::max(s.p2.r, s.p3.r); }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State&) noexcept FCDSP_NONBLOCKING
    {
        return simd::set1(c.s1.tauAMs);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::m32x4 h3 = simd::gt(s.p3.r, simd::max(s.p1.r, s.p2.r));
        const simd::m32x4 h2 = simd::gt(s.p2.r, s.p1.r);
        return simd::sel(h3, simd::set1(c.s3.tauRMs), simd::sel(h2, simd::set1(c.s2.tauRMs), simd::set1(c.s1.tauRMs)));
    }

    // ControlIo::bits for the lane with the larger GR (lanes 0-1): 1 while stage 1 attacks and holds the GR, 3 while GR
    // remains, 0 idle; b2 while stage 2 or 3 holds it.
    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = grDb(s), prog = programDb(s);
        const bool first = simd::lane<0>(r) >= simd::lane<1>(r);
        const float rr = first ? simd::lane<0>(r) : simd::lane<1>(r);
        const float r1 = first ? simd::lane<0>(s.p1.r) : simd::lane<1>(s.p1.r);
        const float rp = first ? simd::lane<0>(prog) : simd::lane<1>(prog);
        const float atk = first ? simd::lane<0>(s.p1.atk) : simd::lane<1>(s.p1.atk);
        const bool programHolds = rp > r1;
        const uint8_t phase = atk > 0.5f && !programHolds ? uint8_t{ 1 } : (rr > 0.0f ? uint8_t{ 3 } : uint8_t{ 0 });
        return static_cast<uint8_t>(phase | (programHolds ? kProgramBit : 0u));
    }

private:
    static bool all(simd::m32x4 m) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 f = simd::sel(m, simd::set1(1.0f), simd::set1(0.0f));
        return simd::lane<0>(f) + simd::lane<1>(f) + simd::lane<2>(f) + simd::lane<3>(f) == 4.0f;
    }

    // One stage's commit: the winner takes r (with its carried remainder where r is its own solved value), the others
    // their stepped value `n` capped at r.
    static void take(Path::State& st, const Path::State& n, simd::m32x4 won, simd::m32x4 own, simd::f32x4 lo,
                     simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::m32x4 capped = simd::gt(n.r, r);
        const simd::f32x4 loser = simd::sel(capped, r, n.r);
        st.r = simd::max(zero, simd::sel(won, r, loser));
        st.lo = simd::sel(won, simd::sel(own, lo, zero), simd::sel(capped, zero, n.lo));
    }

    // The commit (header comment), with stage 1's stepped value n1 (its map's value at r, or the fallback).
    static void commit(const Coeffs& c, State& s, simd::f32x4 r, const Path::State& n1) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 one = simd::set1(1.0f), zero = simd::set1(0.0f);
        const simd::m32x4 own = simd::band(simd::ge(r, s.fbR), simd::ge(s.fbR, r));      // r == fbR (false for NaN)
        const simd::m32x4 w1 = simd::gt(simd::set1(1.5f), s.fbWin);
        const simd::m32x4 w3 = simd::gt(s.fbWin, simd::set1(2.5f));
        const simd::m32x4 w2 = simd::band(simd::ge(s.fbWin, simd::set1(1.5f)), simd::ge(simd::set1(2.5f), s.fbWin));

        const simd::f32x4 r1Old = s.p1.r, r2Old = s.p2.r;
        take(s.p1, n1, w1, own, s.fbLo, r);
        s.p1.atk = simd::sel(simd::gt(s.p1.r, r1Old), one, zero);

        Path::State n2 = s.p2;
        Path::advance(n2, s.fbC2, s.p1.r);                          // toward stage 1's new value
        take(s.p2, n2, w2, own, s.fbLo, r);
        s.p2.atk = simd::sel(simd::gt(s.p1.r, r2Old), one, zero);  // charging

        if (c.three)
        {
            const simd::f32x4 r3Old = s.p3.r;
            Path::State n3 = s.p3;
            Path::advance(n3, s.fbC3, s.p2.r);                      // toward stage 2's new value
            take(s.p3, n3, w3, own, s.fbLo, r);
            s.p3.atk = simd::sel(simd::gt(s.p2.r, r3Old), one, zero);
        }
        else
            s.p3 = s.p2;                                            // TC5: stage 3 follows stage 2 exactly
    }
};

using MultiStage3 = MultiStage3T<>;

static_assert(BallisticsPolicy<MultiStage3> && HasCommitFbRhat<MultiStage3>);

} // namespace fcdsp::stage
