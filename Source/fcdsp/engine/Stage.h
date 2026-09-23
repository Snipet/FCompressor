#pragma once

// Stage policy concepts (01 §5.2; E §3.3; K2 #1, #5). Every stage is a POD `State` of f32x4 members, a `Coeffs`
// struct and `static` functions; engines own no resources. Policies live one per header under engine/stages/<slot>/
// (no umbrella headers), in namespace fcdsp::stage. Sprint-frozen.
//
// The feedback step (FB), per sample, inside ModeEngine::control (K2 #1, #5):
//     x = D::tick(dc_, det_, sc[i]);                          // detector-law dB of the (pre-gained) input
//     auto solve = [&](FbAffine a) noexcept { return G::solveFb(gc_, x, a); };
//     r = B::solveFb(bc_, bal_, solve);                       // per lane; several branches -> max of roots
//     r = L::apply(r, p_.link);                               // link AFTER the per-lane solve (lanes 0-1)
//     B::commitFb(bc_, bal_, r);                              // the linked value becomes the next state
// The feed-forward step (FF): x = D::tick; r^ = G::target(x); r^ = L::apply(r^, link); r = B::tick(r^).
// Both then: r = S2::combine(r, x), r = min(r, rangeSmoothed), r *= offAmt_.

#include "fcdsp/core/Simd.h"
#include "fcdsp/params/EngineParams.h"
#include <concepts>
#include <cstdint>
#include <span>
#include <utility>

namespace fcdsp {

struct StageCtx { float fs; float fsOs; int osFactor; std::span<float> scratch; };

template <class P> concept Designable = requires (typename P::Coeffs& c, const EngineParams& p, const StageCtx& x) {
    { P::design(c, p, x) } noexcept;                       // runs on control ticks only
};

template <class D> concept DetectorPolicy = Designable<D> &&
requires (const typename D::Coeffs& c, typename D::State& s, simd::f32x4 v) {
    { D::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;         // linear SC in -> detector-law dB out
    { D::seed(s, v) } noexcept;                                          // from Carry::detDb
    { D::levelDb(std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
};

// The ballistics' affine map in the feedback loop (K2 #1): given its state, every linear ballistic path maps the
// gain computer's output affinely to the applied GR:  r = A + B*r^(x - r),  per lane A >= 0, 0 <= B <= 1.
struct FbAffine { simd::f32x4 A, B; };

template <class G> concept GainComputerPolicy = Designable<G> &&
requires (const typename G::Coeffs& c, simd::f32x4 x, FbAffine a) {
    { G::target(c, x) } noexcept -> std::same_as<simd::f32x4>;          // FF, pure: r^ >= 0 at detector level x
    { G::solveFb(c, x, a) } noexcept -> std::same_as<simd::f32x4>;      // FB: THE root of r = A + B*r^(x - r);
                                                                         // a = {0, 1} -> the static FB curve
};

template <class L> concept LinkPolicy =
requires (simd::f32x4 r) { { L::apply(r, 0.5f) } noexcept -> std::same_as<simd::f32x4>; };   // lanes 0-1 only

namespace detail { struct FbSolveArchetype { simd::f32x4 operator()(FbAffine) const noexcept; }; }   // declared only

template <class B> concept BallisticsPolicy = Designable<B> &&
requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 v, detail::FbSolveArchetype solve) {
    { B::tick(c, s, v) } noexcept -> std::same_as<simd::f32x4>;                        // FF: r^ -> r
    { B::solveFb(c, std::as_const(s), solve) } noexcept -> std::same_as<simd::f32x4>;  // FB: r from >= 1 affine solves
    { B::commitFb(c, s, v) } noexcept;                                                  // FB: accept the LINKED r,
                                                                                        //     advance every internal state
    { B::seed(s, v) } noexcept;                                                         // from Carry::grDb
    { B::attackNowMs (c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
    { B::releaseNowMs(c, std::as_const(s)) } noexcept -> std::same_as<simd::f32x4>;
    { B::status(std::as_const(s)) } noexcept -> std::same_as<uint8_t>;   // ControlIo::bits b0-1 phase (max lane), b2 auto-slow
};

template <class S2> concept Stage2Policy = Designable<S2> &&
requires (const typename S2::Coeffs& c, typename S2::State& s, simd::f32x4 v) {
    { S2::combine(c, s, v, v) } noexcept -> std::same_as<simd::f32x4>;  // (r1, xDb) -> r; s2 GR in aux lanes
};

template <class C> concept ColourPolicy = Designable<C> &&
requires (const typename C::Coeffs& c, typename C::State& s, float* x, const float* gr, int n) {
    { C::process(c, s, x, gr, n, 0) } noexcept;                          // in place, OS rate, one channel
    { C::transfer(c, 0.f, 0.f) } noexcept -> std::same_as<float>;        // static shape for the COLOUR view
    { C::reset(s) } noexcept;
};

} // namespace fcdsp
