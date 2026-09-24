#pragma once

// stage::Hold<Inner>: hold the GR for EngineParams::holdMs before the release starts (01 §5.2 combinators; 01 §10.3
// Clean HOLD 0-500 ms; E §3.3 "Hold<Inner> (hold counter before release)"; D §2.8 Pro-C hold). Per lane:
//
//     while the target is at or above the GR (attack or steady), the counter is re-armed to N = round(holdMs fs /
//     1000); once the target falls below the GR, the GR stays exactly where it is for N samples (the Inner is fed its
//     own GR as the target, so its state does not move), then the Inner releases as usual.
//
// holdMs = 0 gives N = 0: the Inner runs unchanged, bit for bit. The hold length is a duration, not a gain, so it is
// taken as designed at each control tick without smoothing (stepping it never steps the gain).
//
// Feedback (K2 #1): during a hold the branch is {r1, 0}, whose root is r1 itself; the hold runs in parallel with the
// Inner's own branches, so the solve is the max of roots (01 §5.2): r = max(Inner root, r1) while holding (an attack
// still wins), the Inner's root otherwise. Whether the Inner's root rises (the FF "target >= GR" test: F(r1) <= 0) is
// solveFb's verdict, which commitFb needs after the link; the frozen concept hands solveFb the state const, so that
// verdict travels in State::fbRearm, a `mutable` scratch word written by solveFb and read by the commitFb of the same
// sample (ModeEngine always calls them in that order). S10 (X10): when the Inner reports its own FB verdict
// (Stage.h HasFbFalls: SmoothBranching, whose carried sub-ulp steps keep the float GR still while the value falls),
// "falling" is that verdict instead of root < GR; with the FZ0 step both are the same.
//
// Optional hooks of the Inner (sense, crestDb; ModeEngine.h "Bodies") are forwarded. status() reports phase 2 (hold) on
// the louder lane while it is held.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"
#include <concepts>
#include <cstdint>

namespace fcdsp::stage {

namespace detail {
template <class B>
concept SensesSideChain = requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 v) {
    { B::sense(c, s, v) } noexcept;
};
template <class B>
concept ReportsCrest = requires (const typename B::State& s) {
    { B::crestDb(s) } noexcept -> std::same_as<simd::f32x4>;
};
} // namespace detail

template <class Inner>
struct Hold {
    static_assert(BallisticsPolicy<Inner>, "Hold: Inner must be a BallisticsPolicy");

    struct Coeffs {
        typename Inner::Coeffs inner{};
        simd::f32x4 n{};                        // hold length, samples (a whole number)
    };
    struct State {
        typename Inner::State inner{};
        simd::f32x4 count{};                    // hold samples left
        simd::f32x4 held{};                     // 1 where the last sample was held (status), else 0
        mutable simd::f32x4 fbRearm{};          // FB scratch: 1 where solveFb found the Inner's root not falling
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        Inner::design(c.inner, p, x);
        const float n = p.holdMs > 0.0f ? p.holdMs * x.fs * 0.001f : 0.0f;
        c.n = simd::set1(static_cast<float>(static_cast<int>(n + 0.5f)));
    }

    static void sense(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
        requires detail::SensesSideChain<Inner>
    {
        Inner::sense(c.inner, s.inner, v);
    }

    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r1 = Inner::grDb(s.inner);
        const simd::m32x4 falling = simd::gt(r1, t);
        const simd::m32x4 holdNow = simd::band(falling, simd::gt(s.count, simd::set1(0.0f)));
        s.count = simd::sel(falling, simd::max(simd::set1(0.0f), simd::sub(s.count, simd::set1(1.0f))), c.n);
        s.held = simd::sel(holdNow, simd::set1(1.0f), simd::set1(0.0f));
        return Inner::tick(c.inner, s.inner, simd::sel(holdNow, r1, t));
    }

    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r1 = Inner::grDb(s.inner);
        const simd::f32x4 r = Inner::solveFb(c.inner, s.inner, solve);
        simd::m32x4 falling = simd::gt(r1, r);
        if constexpr (HasFbFalls<Inner>)                         // S10: the Inner's own verdict (header comment)
            falling = simd::gt(Inner::fbFalls(s.inner), simd::set1(0.5f));
        s.fbRearm = simd::sel(falling, simd::set1(0.0f), simd::set1(1.0f));
        const simd::m32x4 holdNow = simd::band(falling, simd::gt(s.count, simd::set1(0.0f)));
        return simd::sel(holdNow, r1, r);
    }

    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        const simd::m32x4 falling = simd::gt(simd::set1(0.5f), s.fbRearm);
        const simd::m32x4 holdNow = simd::band(falling, simd::gt(s.count, simd::set1(0.0f)));
        s.count = simd::sel(falling, simd::max(simd::set1(0.0f), simd::sub(s.count, simd::set1(1.0f))), c.n);
        s.held = simd::sel(holdNow, simd::set1(1.0f), simd::set1(0.0f));
        Inner::commitFb(c.inner, s.inner, r);
    }

    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        Inner::seed(s.inner, grDb);
        s.count = simd::set1(0.0f);
        s.held = simd::set1(0.0f);
    }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return Inner::grDb(s.inner); }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return Inner::attackNowMs(c.inner, s.inner);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return Inner::releaseNowMs(c.inner, s.inner);
    }

    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const uint8_t inner = Inner::status(s.inner);
        const simd::f32x4 r = Inner::grDb(s.inner);
        const float held = simd::lane<0>(r) >= simd::lane<1>(r) ? simd::lane<0>(s.held) : simd::lane<1>(s.held);
        return held > 0.5f ? static_cast<uint8_t>((inner & ~3u) | 2u) : inner;
    }

    static simd::f32x4 crestDb(const State& s) noexcept FCDSP_NONBLOCKING
        requires detail::ReportsCrest<Inner>
    {
        return Inner::crestDb(s.inner);
    }
};

} // namespace fcdsp::stage
