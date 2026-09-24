#pragma once

// stage::OptoSense: the T4 cell's light sensor, the rectified, emphasis-shaped drive of the electroluminescent panel
// (01 §5.2 detector/ catalogue, §10.6 Opto 2A; E §2.7; D §2.2). It runs on the side chain after the Mode's R37 shelf
// (ScShape R37Shelf: the emphasis is already in its input) and reports the drive's level in dB (DetectorLaw::custom;
// the Characteristics x axis is the drive's peak, like a peak-law Mode's, so a sine, a square and DC of the same peak
// read the same level):
//
//     l = 20 log10 |v|                  full-wave rectified (dbFromLin, floored at -240 dB)
//     d <- l                            where l >= d: a new peak
//     d <- d                            while the hold runs: N = round(kHoldMs fs / 1000) samples after the last peak or
//                                       the last near-peak (a sample within kRefreshDb of d while the hold runs)
//     d <- max(l, d - fall)             after it: the panel's afterglow, fall = 20 log10(e) / (kReleaseMs fs) dB per
//                                       sample (an exponential decay in the linear domain), until it meets l exactly
//
// The hold is the panel's persistence [H]: it bridges the gaps between a waveform's rectified peaks down to
// 1 / (2 kHoldMs) = 100 Hz, so a steady tone's light holds its peak and the cell sees no drive ripple. A near-peak keeps
// a running hold, so the sampled crests of a tone that the rate does not divide (1 kHz at 44.1 kHz: within 0.006 dB of
// each other; at 22.05 kHz, 0.09 dB) hold the largest one instead of dipping between them; a slowly decaying tone is
// over-read by less than kRefreshDb, and a level that has started to fall lands on the drive exactly. Below 100 Hz the light dips between half-waves by up to 8.686 (T/2 - kHoldMs) /
// kReleaseMs dB, the frequency-dependent ripple of a real opto (D §2.2 "program- and frequency-dependent"). When the
// drive stops, the light lags it by the hold and then falls at 8.686 / kReleaseMs dB/ms, a delay Opto 2A's release fit
// accounts for (docs/modes/opto-2a.md).
//
// State: the held level and the samples left in the hold, per lane. Carry::detDb is the held level; seed() re-arms the
// hold (a carried level is a fresh peak). The hold length is a duration, designed at each control tick from the rate.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct OptoSense {
    static constexpr float kHoldMs = 5.0f;          // [H] the panel's persistence: bridges half-waves down to 100 Hz
    static constexpr float kReleaseMs = 5.0f;       // [H] its afterglow, a one-pole in the linear domain
    static constexpr float kRefreshDb = 0.1f;       // [H] a sample this close to the held level re-arms the hold

    struct Coeffs {
        simd::f32x4 hold{};                         // N, samples (a whole number)
        simd::f32x4 fall{};                         // dB per sample once the hold has run out
    };
    struct State {
        simd::f32x4 lvl{};                          // the held drive level, dB
        simd::f32x4 left{};                         // samples left in the hold
    };

    static void design(Coeffs& c, const EngineParams&, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float fs = x.fs > 0.0f ? x.fs : 48000.0f;
        const float n = static_cast<float>(static_cast<int>(kHoldMs * fs / 1000.0f + 0.5f));
        c.hold = simd::set1(n);
        c.fall = simd::set1(kDbPerLog2 * 1.44269504f * 1000.0f / (kReleaseMs * fs));   // 20 log10(e) / (tau fs)
    }

    // Linear SC in, the panel's drive level in dB out.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 l = fcdsp::dbFromLin(v);
        const simd::m32x4 peak = simd::ge(l, s.lvl);
        const simd::m32x4 holding = simd::gt(s.left, zero);
        // a near-peak re-arms only a running hold: a decaying level catches the drive exactly (max below) instead of
        // stopping kRefreshDb above it
        const simd::m32x4 near = simd::band(holding, simd::ge(simd::add(l, simd::set1(kRefreshDb)), s.lvl));
        const simd::f32x4 decayed = simd::max(l, simd::sub(s.lvl, c.fall));
        s.lvl = simd::sel(peak, l, simd::sel(holding, s.lvl, decayed));
        const simd::f32x4 counted = simd::sub(simd::min(c.hold, s.left), simd::set1(1.0f));   // a seeded hold is N
        s.left = simd::sel(simd::bor(peak, near), c.hold, simd::max(zero, counted));
        return s.lvl;
    }

    // From Carry::detDb (the drive level, dB): a fresh peak. The hold length needs the rate, which seed() does not see,
    // so it is re-armed at kSeedHold and the next tick() counts down from N: the same as a peak on the last sample.
    static void seed(State& s, simd::f32x4 levelDb) noexcept FCDSP_NONBLOCKING
    {
        s.lvl = levelDb;
        s.left = simd::set1(kSeedHold);
    }

    static simd::f32x4 levelDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.lvl; }

private:
    static constexpr float kSeedHold = 1.0e9f;      // above every N (tick() clamps it to the designed hold)
};

static_assert(DetectorPolicy<OptoSense>);

} // namespace fcdsp::stage
