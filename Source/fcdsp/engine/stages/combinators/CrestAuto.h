#pragma once

// stage::CrestAuto<Inner>: program-dependent release from the side chain's crest factor (01 §5.2 combinators; 01 §10.3
// Clean TIME MODE AUTO, kEngAutoRelease; E §2.5a after Giannoulis, Massberg & Reiss, JAES 2013, and CTAGDRC):
//
//     peak^2 <- max(v^2, peak^2 + c (v^2 - peak^2))      instant attack, 200 ms release       (crest detectors, per
//     ms     <- ms + c (v^2 - ms)                        200 ms mean square                    lane, on the shaped SC)
//     C^2    = max(1, peak^2 / ms)
//     tauR,eff = max(2 tauR / C^2 - tauA, tauR / 8)      tauR, tauA: the Inner's published (smoothed) times
//
// A sine (C^2 = 2) releases at tauR - tauA, a square or a sustained tone (C^2 -> 1) up to 2 tauR - tauA, transients
// (drums: C^2 ~ 10-30) much faster; the floor tauR / 8 [H] keeps a long attack from driving the release to zero
// (docs/modes/clean.md). The release rate is recomputed per sample from the slowly moving crest (the 200 ms detectors),
// so it never steps. With AUTO off the Inner runs with its own coefficients, bit for bit, and the crest detectors still
// run for the CREST readout (EngineTelemetry::crestDb, Clean's CREST internal).
//
// The side chain reaches the ballistics through sense(c, s, v): ModeEngine calls it once per sample with the shaped
// linear SC (after ScShape, before the detector) for every ballistics policy that declares it (an optional hook,
// ModeEngine.h "Bodies"; the frozen BallisticsPolicy concept only passes GR values). sense() also computes the sample's
// release rate, which tick / solveFb / commitFb then use; a state that was never sensed (rate 0) falls back to the
// Inner's own release rate.
//
// Inner: SmoothBranching-shaped (Coeffs::cR, the release rate 1 - alpha, and Coeffs::tauAMs / tauRMs, the smoothed
// published times). releaseNowMs reports the effective release (Clean's REL EFF); status() adds b2 "auto-slow" on the
// louder lane while the automatic release is slower than the published one (low crest: sustained material).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::stage {

template <class Inner>
struct CrestAuto {
    static_assert(BallisticsPolicy<Inner>, "CrestAuto: Inner must be a BallisticsPolicy");
    static constexpr float kCrestTauMs = 200.0f;                // E §2.5a: both crest detectors over ~200 ms
    static constexpr float kMinReleaseFraction = 0.125f;        // [H] the automatic release never drops below tauR / 8
    static constexpr float kMinTauMs = 1e-6f;
    static constexpr uint8_t kAutoSlowBit = 1u << 2;            // ControlIo::bits b2

    struct Coeffs {
        typename Inner::Coeffs inner{};
        simd::f32x4 cCrest{};                   // 1 - alpha of the 200 ms crest detectors
        float fs = 0;
        bool autoRelease = false;               // kEngAutoRelease (Clean: TIME MODE AUTO)
    };
    struct State {
        typename Inner::State inner{};
        simd::f32x4 pk2{};                      // crest peak detector (squared)
        simd::f32x4 ms2{};                      // crest mean square
        simd::f32x4 cR{};                       // this sample's release rate (AUTO), 0 before the first sense()
        simd::f32x4 relMs{};                    // this sample's effective release time (ms), 0 before sense()
        simd::f32x4 slow{};                     // 1 where the automatic release is slower than the published one
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        Inner::design(c.inner, p, x);
        c.cCrest = simd::set1(fcdsp::oneMinusAlpha(kCrestTauMs, x.fs));
        c.fs = x.fs;
        c.autoRelease = (p.flags & kEngAutoRelease) != 0;
    }

    // The side chain (linear, shaped), once per sample before tick / solveFb.
    static void sense(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 v2 = simd::mul(v, v);
        s.ms2 = simd::fma(s.ms2, c.cCrest, simd::sub(v2, s.ms2));
        s.pk2 = simd::max(v2, simd::fma(s.pk2, c.cCrest, simd::sub(v2, s.pk2)));
        const simd::f32x4 tauR = simd::set1(c.inner.tauRMs);
        if (!c.autoRelease)
        {
            s.cR = c.inner.cR;
            s.relMs = tauR;
            s.slow = simd::set1(0.0f);
            return;
        }
        const simd::f32x4 tauA = simd::set1(c.inner.tauAMs);
        const simd::f32x4 crest2 = crestSquared(s);
        const simd::f32x4 twiceOverC2 = simd::div(simd::add(tauR, tauR), crest2);
        const simd::f32x4 tauEff = simd::max(simd::mul(tauR, simd::set1(kMinReleaseFraction)),
                                             simd::sub(twiceOverC2, tauA));
        s.relMs = tauEff;
        s.cR = oneMinusAlphaV(tauEff, c.fs);
        s.slow = simd::sel(simd::gt(tauEff, tauR), simd::set1(1.0f), simd::set1(0.0f));
    }

    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        if (!c.autoRelease)
            return Inner::tick(c.inner, s.inner, t);
        return Inner::tick(withRelease(c, s), s.inner, t);
    }

    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        if (!c.autoRelease)
            return Inner::solveFb(c.inner, s.inner, solve);
        return Inner::solveFb(withRelease(c, s), s.inner, solve);
    }

    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        if (!c.autoRelease)
            Inner::commitFb(c.inner, s.inner, r);
        else
            Inner::commitFb(withRelease(c, s), s.inner, r);
    }

    // The crest detectors keep running across a hand-over (they describe the program, not the Mode).
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING { Inner::seed(s.inner, grDb); }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return Inner::grDb(s.inner); }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return Inner::attackNowMs(c.inner, s.inner);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 own = Inner::releaseNowMs(c.inner, s.inner);
        if (!c.autoRelease)
            return own;
        return simd::sel(simd::gt(s.relMs, simd::set1(0.0f)), s.relMs, own);
    }

    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = Inner::grDb(s.inner);
        const float slow = simd::lane<0>(r) >= simd::lane<1>(r) ? simd::lane<0>(s.slow) : simd::lane<1>(s.slow);
        return static_cast<uint8_t>(Inner::status(s.inner) | (slow > 0.5f ? kAutoSlowBit : 0u));
    }

    // The crest factor in dB, 10 log10 C^2 (0 dB for a square or silence, 3.01 dB for a sine).
    static simd::f32x4 crestDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::mul(simd::set1(0.5f * kDbPerLog2), fcdsp::log2(crestSquared(s)));
    }

private:
    // C^2 = peak^2 / ms, both floored at kMsFloor (silence: 1), at least 1 (peak^2 >= ms up to rounding).
    static simd::f32x4 crestSquared(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 msFloor = simd::set1(kMsFloor);
        const simd::f32x4 q = simd::div(simd::max(msFloor, s.pk2), simd::max(msFloor, s.ms2));
        return simd::max(simd::set1(1.0f), q);
    }

    // The Inner's coefficients with this sample's release rate (a state never sensed keeps the Inner's own).
    static typename Inner::Coeffs withRelease(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        typename Inner::Coeffs ic = c.inner;
        ic.cR = simd::sel(simd::gt(s.cR, simd::set1(0.0f)), s.cR, c.inner.cR);
        return ic;
    }

    // Units.h's oneMinusAlpha lane by lane, with the same operations in the same order (so each lane equals the scalar
    // function bit for bit): 1 - e^(-x), x = 1000 / (tau fs), by the Taylor series below x = 1/8, else by exp2.
    static simd::f32x4 oneMinusAlphaV(simd::f32x4 tauMs, float fs) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 one = simd::set1(1.0f);
        const simd::f32x4 tau = simd::max(simd::set1(kMinTauMs), tauMs);
        const simd::f32x4 x = simd::div(simd::set1(1000.0f), simd::mul(tau, simd::set1(fs)));
        simd::f32x4 p = simd::sub(one, simd::mul(x, simd::set1(1.0f / 6.0f)));
        p = simd::sub(one, simd::mul(simd::mul(x, simd::set1(1.0f / 5.0f)), p));
        p = simd::sub(one, simd::mul(simd::mul(x, simd::set1(1.0f / 4.0f)), p));
        p = simd::sub(one, simd::mul(simd::mul(x, simd::set1(1.0f / 3.0f)), p));
        p = simd::sub(one, simd::mul(simd::mul(x, simd::set1(0.5f)), p));
        const simd::f32x4 series = simd::mul(x, p);
        const simd::f32x4 direct = simd::sub(one, fcdsp::exp2(simd::mul(simd::neg(x), simd::set1(1.44269504f))));
        return simd::sel(simd::gt(simd::set1(0.125f), x), series, direct);
    }
};

} // namespace fcdsp::stage
