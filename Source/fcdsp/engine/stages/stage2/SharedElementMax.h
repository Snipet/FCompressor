#pragma once

// stage::SharedElementMax<Det, Bal>: a limiter that shares the compressor's gain element (01 §5.2 stage2/ catalogue,
// §10.7 Diode 609; E §3.3 "SharedElementMax (r = max(r1, r2): diode-bridge 33609/2254)"; D §2.5). M5 (S10).
//
// The 33609 and the 2254 have two side chains, a compressor's and a limiter's, and ONE diode-bridge gain element: "the
// outputs of both side-chains are combined before feeding the gain-control element" (D §2.5 [V S11]), so the element
// takes the larger control, and both side chains are feedback designs (they sense the element's OUTPUT). ModeEngine runs
// the compressor (stage 1: the Mode's detector, computer, link and ballistics) and hands this stage its GR r1 and the
// detector level x of the same sample; this stage runs the limiter and returns the element's GR
//
//     r = max(r1, r2)            per channel,
//     r2 = the limiter's GR      its own ballistics (Bal) on the stage-2 times, its own feedback solve.
//
// Lanes (E §3.2: lanes 2-3 carry independent recurrences, "the Stage-2 detector and ballistics"). The host feeds the
// detector's aux lanes a copy of the channels (Router.h: {c0, c1, c0, c1}), so the Mode's detector (Det: it must be the
// Mode's own Detector, e.g. PeakLog, which the traits assert) already produces the limiter's detector level on lanes 2-3
// of x; a Mode-internal SC shaping may leave them unshaped (SlowHp: the compressor's ATTACK SLOW high-pass is not the
// limiter's). The limiter state lives on the aux lanes: combine() reads x's lanes 2-3 and r1's lanes 0-1 into both
// halves of a vector ({x2, x3, x2, x3} and {r1_0, r1_1, r1_0, r1_1}), so its lanes 0-1 run a duplicate of lanes 2-3,
// and returns {max(r1_0, r2_0), max(r1_1, r2_1), r2_0, r2_1}: the element's GR on lanes 0-1 and the stage-2 GR in the
// aux lanes, as Stage2Policy and ModeEngine's s2GrDb output read them.
//
// The limiter's law: QuadKnee in feedback (its FB closed forms, which honour FbAffine::base) at the smoothed stage-2
// threshold l.s2ThrDb, with kSlope = 0.99 (100:1, QuadKnee's loop-gain clamp: D §2.5 gives ">100:1" [U]) and a knee of
// kKneeDb = 0.5 dB [H] at the output (the loop holds the output within about half a dB of the threshold).
//
// The shared sense point (feedback on the element's output). The limiter's side chain senses y = x - r, r = max(r1,
// r2), not x - r2: while the compressor holds the element, the limiter hears the compressed output. With r1 known, each
// affine map g(r2) = base + A + B r^2_fb(x - max(r1, r2)) of the limiter's ballistics is non-increasing in r2, so its
// root is exact in closed form:
//     rs  = the self-sensed root (QuadKnee::solveFb of the map as given);
//     r2* = rs                          where rs >= r1 (the limiter controls the element and senses its own GR),
//         = A + B r^2_fb(x - r1)        else (the compressor holds the sense point: an explicit step),
// which the solve functor hands to Bal::solveFb, so every ballistics branch (SmoothBranching's attack / release,
// DualRelease's fast / slow roots, max of roots; 01 §5.3) and SmoothBranching's FB sub-ulp carry (the based solve)
// apply unchanged. Both branches meet at rs = r1 (continuous). Then the stage-2 link (the Mode's `link`, LinkMax's law
// on the two channels: "both channels always compressed by the same amount", D §2.5), then Bal::commitFb of the linked
// value (with r^2_fb at the committed GR's own sense point, x - max(r1, r2), for a ballistics that takes it:
// DualRelease's exact commit, Stage.h HasCommitFbRhat). So the limiter does not charge while the compressor keeps the
// output below its threshold (and its LIMIT GR internal reads the limiter's own control, <= r1 there), and the element
// GR is the max of the two loops' roots.
//
// The compressor (stage 1) senses x - r1, its own GR (ModeEngine's frozen FB step solves stage 1 before stage 2 is
// known): while the limiter holds the element, the compressor charges as if the limiter were not there. That is the
// documented residual of the stage order (docs/modes/diode-609.md); the applied GR is the same in both models whenever
// the limiter's root is below the compressor's.
//
// Static form (S10 interface revision, X10; Stage.h HasCombineStatic): combineStatic(c, r1, x, l) = max(r1, the
// limiter's static FB root at x), every lane independent. It is what the engine settles on at a constant level in both
// sense models: where the limiter's self root exceeds the compressor's it holds the element; elsewhere its held value
// A + B r^2_fb(x - r1) settles at or below r1. ModeEntry::staticS2 draws it on the transfer curve (lead revision 4).
//
// OFF (s2thr at kS2Off, 01 §5.1: ModeEngine smooths s2ThrDb in [-40, kS2Off] and fades stage 2 with s2On_). While the
// smoothed threshold rests at kS2Off exactly, the limiter is out: its state rests at 0 dB GR (seeded once), and
// combine() returns r1 with a stage-2 GR of 0, NoStage2's arithmetic. So a limiter that is off never charges (not even
// on +40 dBFS program), costs no solve, and starts from 0 dB when it is switched on (the threshold leaves kS2Off at the
// first sample, while s2On_ fades the element's GR in over 20 ms).
//
// An external key: ModeEngine evaluates the compressor FF on the key (E §2.6: the loop is open), but this stage cannot
// see ControlIo::keyExternal, so the limiter keeps its loop on the key level: it senses key - GR (docs: known limit).
//
// Carry (01 §5.5, FZ0 errata R-F0 #3): grDb(s) = {r2_0, r2_1, 0, 0} (Carry::s2GrDb, channel lanes); seed(s, v) seeds the
// limiter's aux lanes (and their duplicate) from v's lanes 0-1.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/params/EngineParams.h"
#include <utility>

namespace fcdsp::stage {

template <class Det, class Bal>
struct SharedElementMax {
    static_assert(DetectorPolicy<Det> && BallisticsPolicy<Bal>,
                  "SharedElementMax: Det must be a DetectorPolicy and Bal a BallisticsPolicy");
    using Detector = Det;                       // the stage-2 detector: the Mode's own, on the aux lanes (header)
    using Ballistics = Bal;
    using Computer = QuadKnee;                  // the limiter's law, in feedback

    static constexpr float kSlope = 0.99f;      // 100:1: QuadKnee's FB loop gain k = 99 (D §2.5: ">100:1" [U])
    static constexpr float kKneeDb = 0.5f;      // [H] the limiter's knee at the output (dB)

    struct Coeffs {
        typename Bal::Coeffs bal{};             // the limiter's ballistics at the stage-2 times
        float link = 1.0f;                      // EngineParams::link, clamped to [0, 1]
    };
    struct State {
        typename Bal::State bal{};              // the limiter's ballistics state: lanes 2-3 (0-1 duplicate them)
        bool rest = false;                      // true: the limiter is out and its state rests at 0 dB
    };

    // The stage-2 times reach the ballistics as its attack and release (EngineParams::s2AtkTauMs, s2RelTauMs); every
    // other field (m[], tags) as is, so a ballistics that reads m[] (DualReleaseT, an AutoSwitch selector) finds the
    // Mode's slots.
    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        EngineParams q = p;
        q.atkTauMs = p.s2AtkTauMs;
        q.relTauMs = p.s2RelTauMs;
        Bal::design(c.bal, q, x);
        c.link = p.link >= 0.0f ? (p.link <= 1.0f ? p.link : 1.0f) : 0.0f;     // NaN: independent
    }

    // The element's GR {max(r1, r2) per channel, r2 per channel} (header comment).
    static simd::f32x4 combine(const Coeffs& c, State& s, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l) noexcept
        FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        if (!(simd::lane<0>(l.s2ThrDb) < kS2Off))                      // OFF, landed (header comment)
        {
            if (!s.rest)
            {
                Bal::seed(s.bal, zero);
                s.rest = true;
            }
            return simd::withLane<3>(simd::withLane<2>(r1, 0.0f), 0.0f);
        }
        s.rest = false;
        const simd::f32x4 x = dup23(xDb), rc = dup01(r1);
        const LevelCtl l2 = limiterLevel(l);
        const Computer::Coeffs gc{};
        const simd::f32x4 held = Computer::rhatFb(gc, simd::sub(x, rc), l2);   // r^2_fb at the compressor's sense
        const auto solve = [&x, &rc, &held, &l2, &gc](FbAffine a) noexcept FCDSP_NONBLOCKING {
            const simd::f32x4 inc = Computer::solveFb(gc, x, l2, a);           // self-sensed root, minus base
            return simd::sel(simd::ge(simd::add(a.base, inc), rc), inc, simd::fma(a.A, a.B, held));
        };
        const simd::f32x4 r2 = linkLanes(Bal::solveFb(c.bal, std::as_const(s.bal), solve), c.link);
        if constexpr (HasCommitFbRhat<Bal>)
            Bal::commitFb(c.bal, s.bal, r2, Computer::rhatFb(gc, simd::sub(x, simd::max(rc, r2)), l2));
        else
            Bal::commitFb(c.bal, s.bal, r2);
        const simd::f32x4 r = simd::max(rc, r2);
        return simd::withLane<1>(simd::withLane<0>(r2, simd::lane<0>(r)), simd::lane<1>(r));
    }

    // The settled stage 2 for the analysis curves (header comment): max(r1, the limiter's static FB root), per lane.
    static simd::f32x4 combineStatic(const Coeffs&, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l) noexcept
        FCDSP_NONBLOCKING
    {
        const Computer::Coeffs gc{};
        const simd::f32x4 r2 = Computer::solveFb(gc, xDb, limiterLevel(l),
                                                 FbAffine{ simd::set1(0.0f), simd::set1(1.0f) });
        return simd::max(r1, r2);
    }

    // From Carry::s2GrDb (lanes 0-1: the channels' stage-2 GR): the limiter's aux lanes and their duplicate.
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        Bal::seed(s.bal, dup01(simd::max(simd::set1(0.0f), grDb)));
        s.rest = false;
    }

    // Carry::s2GrDb: the limiter's GR per channel on lanes 0-1, 0 on the aux lanes.
    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::withLane<3>(simd::withLane<2>(dup23(Bal::grDb(s.bal)), 0.0f), 0.0f);
    }

    // The limiter's LevelCtl: the smoothed stage-2 threshold, its fixed slope and knee (header comment).
    static LevelCtl limiterLevel(const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return LevelCtl{ l.s2ThrDb, simd::set1(kSlope), simd::set1(kKneeDb), l.s2ThrDb };
    }

    // {v0, v1, v0, v1} and {v2, v3, v2, v3}.
    static simd::f32x4 dup01(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        return simd::withLane<3>(simd::withLane<2>(v, simd::lane<0>(v)), simd::lane<1>(v));
    }
    static simd::f32x4 dup23(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        return simd::withLane<1>(simd::withLane<0>(v, simd::lane<2>(v)), simd::lane<3>(v));
    }

    // LinkMax's law on the two channels, on a {a, b, a, b} vector: (1 - k) r + k max(a, b), one rounding for the sum,
    // so k = 0 and k = 1 are exact and swapping the channels swaps the result bit for bit.
    static simd::f32x4 linkLanes(simd::f32x4 r, float link) noexcept FCDSP_NONBLOCKING
    {
        const float a = simd::lane<2>(r), b = simd::lane<3>(r);
        const float m = a > b ? a : b;
        return simd::fma(simd::mul(simd::set1(1.0f - link), r), simd::set1(link), simd::set1(m));
    }
};

} // namespace fcdsp::stage
