#pragma once

// Stage policy concepts (01 §5.2; E §3.3; K2 #1, #5). Every stage is a POD `State` of f32x4 members, a `Coeffs`
// struct and `static` functions; engines own no resources. Policies live one per header under engine/stages/<slot>/
// (no umbrella headers), in namespace fcdsp::stage. Sprint-frozen.
//
// Two rates (FZ0 errata, R-F0 #2):
//   - design(c, p, ctx) runs on control ticks only (every kTickSamples) and fills Coeffs from EngineParams. It never
//     bakes a LevelCtl quantity (thrDb, slope, kneeDb, s2ThrDb) into Coeffs;
//   - those four arrive PER SAMPLE as a LevelCtl, smoothed by ModeEngine (lvl_, lvl2_; ModeEngine.h says how it is
//     built). The gain computer and stage 2 read them from `l`, so threshold, ratio, knee and stage-2 threshold moves
//     never zipper and never force a per-sample design().
//
// Per sample i, inside ModeEngine::control (K2 #1, #5):
//     l = the LevelCtl of sample i (ModeEngine.h);
//     v = SH::tick(shc_, sh_, sc[i]);                         // Mode-internal SC shaping, linear in -> linear out
//     x = D::tick(dc_, det_, v);                              // detector-law dB of the (pre-gained) input
//   FB:
//     auto solve = [&](FbAffine a) noexcept { return G::solveFb(gc_, x, l, a); };
//     r = B::solveFb(bc_, bal_, solve);                       // per lane; several branches -> max of roots
//     r = L::apply(r, p_.link);                               // link AFTER the per-lane solve (lanes 0-1)
//     B::commitFb(bc_, bal_, r);                              // the linked value becomes the next state
//   FF:
//     r^ = G::target(gc_, x, l); r^ = L::apply(r^, p_.link); r = B::tick(bc_, bal_, r^);
//   Both then: r = S2::combine(s2c_, s2_, r, x, l); r = min(r, range); r *= offAmt_.
// Seeding on a Mode/kernel switch (01 §5.5): D::seed(det_, carry.detDb), B::seed(bal_, carry.grDb),
// S2::seed(s2_, carry.s2GrDb).
//
// Real time: every policy function is called from ModeEngine's FCDSP_NONBLOCKING members. Policies are header-inline,
// so -Wfunction-effects infers them; they may (and should) carry FCDSP_NONBLOCKING explicitly (core/Rt.h).
//
// S10 interface revision (X10; additive, every existing policy compiles and runs unchanged):
//   - FbAffine::base (default 0): the GR a solve is measured from, so a ballistics policy can carry a slow release's
//     sub-ulp steps in feedback (FbAffine below; SmoothBranching.h "FB sub-ulp carry");
//   - optional, detected (not in the concepts): Stage2 S2::combineStatic (the settled stage 2, for the analysis curves;
//     HasCombineStatic), gain computer G::rhatFb (the FB curve r^_fb(y); HasRhatFb), ballistics B::commitFb(c, s, r,
//     rhat) (the commit with r^_fb at the committed GR; HasCommitFbRhat) and B::fbFalls(s) (the value's FB fall
//     verdict, for Hold; HasFbFalls). ModeEngine.h and the combinators use each when present.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/params/EngineParams.h"
#include <concepts>
#include <cstdint>
#include <span>
#include <utility>

namespace fcdsp {

struct StageCtx { float fs = 0; float fsOs = 0; int osFactor = 1; std::span<float> scratch{}; };

// The per-sample level controls (FZ0 errata, R-F0 #2): smoothed values, each broadcast to all four lanes by ModeEngine
// (ModeEngine.h), or built unsmoothed from EngineParams by the analysis entry points (staticGr).
struct LevelCtl {
    simd::f32x4 thrDb{};      // detector-domain threshold, dB (after preGain; EngineParams::thrDb)
    simd::f32x4 slope{};      // S = 1 - 1/R (EngineParams::slope; > 1 is negative ratio)
    simd::f32x4 kneeDb{};     // knee width W, dB (EngineParams::kneeDb)
    simd::f32x4 s2ThrDb{};    // stage-2 threshold, dB: clamp(EngineParams::s2ThrDb, -40, kS2Off)
};

template <class P> concept Designable = requires (typename P::Coeffs& c, const EngineParams& p, const StageCtx& x) {
    { P::design(c, p, x) } noexcept;                       // control ticks only; never bakes a LevelCtl field
};

template <class D> concept DetectorPolicy = Designable<D> &&
requires (const typename D::Coeffs& c, typename D::State& s, simd::f32x4 v) {
    { D::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;         // linear SC in -> detector-law dB out
    { D::seed(s, v) } noexcept;                                          // from Carry::detDb
    { D::levelDb(std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
};

// The ballistics' affine map in the feedback loop (K2 #1): given its state, every linear ballistic path maps the
// gain computer's output affinely to the applied GR:  r = A + B*r^(x - r),  per lane A >= 0, 0 <= B <= 1.
//
// S10 interface revision (X10): `base`, default 0. The map is r = base + A + B*r^(x - r) and every solve returns
// r - base, the root measured from base. FbAffine{A, B} (base 0) is the FZ0 map and the FZ0 absolute root, bit for bit.
// A ballistics policy whose GR moves by less than an ulp per sample passes base = its current GR and A = its move
// without the gain term (e.g. lo - c (r + lo) for SmoothBranching's value r + lo), which may be negative: the returned
// increment then carries the sub-ulp part the absolute root rounds away (its rounding error scales with |A| + B r^,
// not with the GR). Every gain computer's solveFb honours base (QuadKnee's closed form, FeedbackZdf, FeedbackDelayed
// and everything they wrap); a new computer either does or wraps its law in FeedbackZdf<G>, which honours it for any
// law.
struct FbAffine { simd::f32x4 A, B; simd::f32x4 base{}; };

template <class G> concept GainComputerPolicy = Designable<G> &&
requires (const typename G::Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) {
    { G::target(c, x, l) } noexcept -> std::same_as<simd::f32x4>;       // FF, pure: r^ >= 0 at detector level x
    { G::solveFb(c, x, l, a) } noexcept -> std::same_as<simd::f32x4>;   // FB: THE root of r = base + A + B*r^(x - r),
                                                                         // minus base (S10); a = {0, 1} -> the static
                                                                         // FB curve
};

// Optional (S10, X10; detected, not in the concept): G::rhatFb(c, y, l) = r^_fb(y), the FB curve at OUTPUT level y
// (dB), i.e. G::target with the loop gain in place of the slope (QuadKnee.h's one FB-curve convention), from the
// engine's LevelCtl. ModeEngine uses it to hand a ballistics policy r^_fb at its committed GR (HasCommitFbRhat).
template <class G> concept HasRhatFb =
requires (const typename G::Coeffs& c, simd::f32x4 y, const LevelCtl& l) {
    { G::rhatFb(c, y, l) } noexcept -> std::same_as<simd::f32x4>;
};

template <class L> concept LinkPolicy =
requires (simd::f32x4 r, float link) {
    { L::apply(r, link) } noexcept -> std::same_as<simd::f32x4>;         // apply(r, link): lanes 0-1 only;
};                                                                       //   link = EngineParams::link in [0, 1]

namespace detail { struct FbSolveArchetype { simd::f32x4 operator()(FbAffine) const noexcept; }; }   // declared only

template <class B> concept BallisticsPolicy = Designable<B> &&
requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 v, detail::FbSolveArchetype solve) {
    { B::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;                        // FF: r^ -> r
    { B::solveFb(c, std::as_const(s), solve) } noexcept -> std::same_as<simd::f32x4>;  // FB: r from >= 1 affine solves
    { B::commitFb(c, s, v) } noexcept;                                                  // FB: accept the LINKED r,
                                                                                        //     advance every internal state
    { B::seed(s, v) } noexcept;                                                         // from Carry::grDb
    { B::grDb(std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;               // for Carry::grDb (S2 lead revision)
    { B::attackNowMs (c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
    { B::releaseNowMs(c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
    { B::status(std::as_const(s)) } noexcept -> std::same_as<uint8_t>;   // ControlIo::bits b0-1 phase (max lane), b2 auto-slow
};

// Optional (S10, X10; detected): B::commitFb(c, s, r, rhat), the FB commit with rhat = r^_fb(x - r) per lane, the FB
// curve at the committed (linked) GR's own sense point, exact. ModeEngine calls it instead of commitFb(c, s, r) when
// the Mode's computer has G::rhatFb (HasRhatFb); a policy that recovers a path's value from the loop (DualRelease's
// fast path where the slow root won) needs it, since no solve returns that value.
template <class B> concept HasCommitFbRhat =
requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 r, simd::f32x4 rhat) {
    { B::commitFb(c, s, r, rhat) } noexcept;
};

// Optional (S10, X10; detected): B::fbFalls(s), after solveFb, 1 per lane where the ballistics' value falls (the FB
// analogue of FF's "target below the GR"), else 0. A policy that carries sub-ulp steps in FB (SmoothBranching) has it,
// because its float root no longer tells: a carried fall keeps the float GR for samples, then drops it by one ulp.
// Hold<Inner> reads it for its FB verdict when the Inner has it (else root < GR, the FZ0 verdict).
template <class B> concept HasFbFalls = requires (const typename B::State& s) {
    { B::fbFalls(s) } noexcept -> std::same_as<simd::f32x4>;
};

template <class S2> concept Stage2Policy = Designable<S2> &&
requires (const typename S2::Coeffs& c, typename S2::State& s, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l) {
    { S2::combine(c, s, r1, xDb, l) } noexcept -> std::same_as<simd::f32x4>;   // combine(c, s, r1, xDb, l) -> r;
                                                                               //   s2 GR in aux lanes
    { S2::seed(s, r1) } noexcept;                                              // from Carry::s2GrDb (FZ0 errata)
    { S2::grDb(std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;     // for Carry::s2GrDb (S2 lead revision)
};

// Optional (S10 interface revision, X10; detected, not in the concept, so NoStage2 needs no change): the static stage 2
// the analysis curves draw (CurveOpts::stage2). combineStatic(c, r1, xDb, l) is the GR combine() settles on at a
// constant detector level xDb (dB) with a settled stage-1 GR r1, per lane, EVERY lane an independent abscissa (no
// aux-lane packing: combine()'s stage-2 aux lanes are its own business). Pure, no state: stage 2's ballistics have
// landed. A shared element returns max(r1, r2(xDb)); a policy without it is the identity (NoStage2).
// ModeEngine<T>::staticS2 evaluates it with the unsmoothed LevelCtl and the settled s2On, and ModeEntry::staticS2
// points there (nullptr without it).
template <class S2> concept HasCombineStatic =
requires (const typename S2::Coeffs& c, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l) {
    { S2::combineStatic(c, r1, xDb, l) } noexcept -> std::same_as<simd::f32x4>;
};

// Mode-internal side-chain shaping (FZ0 errata, R-F0 #3): Flat, R37Shelf, SlowHp (Diode 609), Thrust. It runs per
// sample on the linear SC after the host filters (ControlIo::sc), before the detector, and is distinct from the host's
// SC HPF and tilt. magDb is its static magnitude response for scShapeDb / analysis::scResponse.
template <class S> concept ScShapePolicy = Designable<S> &&
requires (const typename S::Coeffs& c, typename S::State& s, simd::f32x4 v, float hz, float fs) {
    { S::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;          // tick(c, s, sc) -> shaped sc, linear
    { S::magDb(c, hz, fs) } noexcept -> std::same_as<float>;             // magDb(c, hz, fs): |H(hz)| in dB at rate fs
};

template <class C> concept ColourPolicy = Designable<C> &&
requires (const typename C::Coeffs& c, typename C::State& s, float* x, const float* grDb, int n, int channel,
          float xIn, float grIn) {
    { C::process(c, s, x, grDb, n, channel) } noexcept;   // process(c, s, x, grDb, n, channel): in place, OS rate, one
                                                          //   channel; channel = 0 or 1 = the wet/grDbOs row and the
                                                          //   col_[] index (never a drive: drive is in Coeffs)
    { C::transfer(c, xIn, grIn) } noexcept -> std::same_as<float>;   // transfer(c, x, grDb) -> y: static shape for
                                                                     //   the COLOUR view
    { C::reset(s) } noexcept;
};

} // namespace fcdsp
