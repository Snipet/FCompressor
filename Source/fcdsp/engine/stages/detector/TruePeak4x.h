#pragma once

// stage::TruePeak4x: the true-peak detector of a limiter's side chain, 4x polyphase on the SC only (01 §5.2 detector/
// catalogue, §10.7 Brickwall DETECT TP; E §5.4 "l_tp: true peak, 4x polyphase (ITU-R BS.1770 annex 2 method)"; K2
// #21a). Brickwall selects it with DetSelect<PeakLog, TruePeak4x> (DETECT PEAK / TP, kEngTruePeak).
//
// Per lane, at SC sample n the detector reports the base sample m = n - kDelay and the three points between it and the
// next one, x(m + 1/4), x(m + 1/2), x(m + 3/4), interpolated by a 24-tap polyphase filter (x[m - 11] ... x[m + 12]):
//
//     level(n) = 20 log10 max(|x[m]|, |x(m + 1/4)|, |x(m + 1/2)|, |x(m + 3/4)|)      (dB, floored at kLinFloor)
//
// Phase 0 is the sample itself (a Nyquist interpolator: the 4x grid contains every base sample exactly), so the true
// peak is never below the sample peak, and a signal whose inter-sample points are not higher reads exactly as PeakLog
// reads it, kDelay samples later. The report covers the interval [m, m + 3/4]; the next report starts at m + 1. The
// engine limits every point of it (SlidingMaxBox.h holds the GR one base sample either side of m, which covers the
// OS-rate gain interpolation between m and m + 1 at every Quality).
//
// Delay. The interpolator needs x[m + 12], so it reports m = n - 12: D_tp = kDelay = 12 base samples (0.25 ms at
// 48 kHz), on the side chain only (the audio path never sees this filter, so latency does not change). The engine
// reports it through IEngine::scDelaySamples() and the host subtracts it from the SC delay, L_la - look + D_up - D_tp
// (01 §5.4 c, K2 #21a): with a lookahead budget the detector's output at engine time n describes the audio the gain
// meets `look` samples later, exactly as PeakLog's does. Without enough budget (L_la - look + D_up < D_tp; the host
// floors the SC delay at 0) TP cannot align and the attack comes up to D_tp - D_up samples late: the footer's
// "overshoot possible" (02 §6.6).
//
// Filter [H] (docs/modes/brickwall.md "True peak"): h(t) = sinc(t) w(t / 12), w a Kaiser window with beta = 6.2,
// sampled at t = f - k for the fractions f = 1/4, 1/2, 3/4 and k = -11 ... 12, each phase normalised to unit DC gain
// (double precision, then rounded to float). Against the ideal fractional delay, |H(w) - e^(jwf)| stays within
// 0.0079 dB-equivalent (20 log10(1 + err)) up to 0.40 fs and 0.0124 dB up to 0.4167 fs (20 kHz at 48 kHz), the band the
// beta is fitted to; beyond it the error grows (0.15 dB at 0.43 fs, 0.97 dB at 0.45 fs): content between 20 kHz and
// Nyquist at 48 kHz, or above 18.4 kHz at 44.1 kHz, is under-read. Twenty-four taps per phase, not BS.1770's twelve:
// the twelve-tap Kaiser optimum is 0.31 dB off at 0.4167 fs, the eight-tap-a-side one 0.10 dB, which alone would
// spend the HQ ceiling's +0.1 dB (K2 #21b). BS.1770's own table is not used: its phases sit at 1/8, 3/8, 5/8, 7/8 (no
// phase contains the sample itself), so its TP could read below the sample peak.
//
// State: a double-written ring of the last 24 SC samples per lane (the 24-sample window is always contiguous), the last
// level (Carry::detDb, levelDb) and two peak holds for Brickwall's TP OVER readout (the held true peak and the held
// sample peak, linear, instant attack, a 1 s release: TP OVER = their ratio in dB). No libm: 72 fma, abs, max and one
// log2 (dbFromLin) per sample for four lanes.
//
// seed(level): the ring is filled with the level as a constant, linFromDb(level) (the detector continues at the carried
// level in its own domain, as PeakLog's seed does; the first 24 samples then see a step against it, which can only
// read high). A cold engine seeds kSilenceDb (-240 dB): 1e-12, silence.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct TruePeak4x {
    static constexpr int kHalf = 12;                // taps each side of the interpolated interval
    static constexpr int kTaps = 2 * kHalf;         // 24 taps per phase
    static constexpr int kDelay = kHalf;            // D_tp, base samples (IEngine::scDelaySamples)
    static constexpr float kHoldMs = 1000.0f;       // [H] the TP OVER holds' release (1 s: a meter-like readout)

    // kPhase[p][j] multiplies x[m - 11 + j] to estimate x(m + (p + 1) / 4) (header comment). Phase 2 (3/4) is phase 0
    // reversed and phase 1 (1/2) is symmetric, as the Kaiser sinc is even.
    static constexpr float kPhase[3][kTaps] = {
        {
          -6.320256966e-04f, 1.595064467e-03f, -3.262828920e-03f, 5.921957025e-03f, -9.937105079e-03f,
          1.580335898e-02f, -2.427668742e-02f, 3.671208606e-02f, -5.605276152e-02f, 9.048277482e-02f,
          -1.746255780e-01f, 8.993894182e-01f, 2.968542668e-01f, -1.210733856e-01f, 7.040186757e-02f,
          -4.521340791e-02f, 2.987263692e-02f, -1.964997916e-02f, 1.260013474e-02f, -7.733114346e-03f,
          4.447899604e-03f, -2.324435424e-03f, 1.041000096e-03f, -3.411562067e-04f },
        {
          -6.695253674e-04f, 1.835897319e-03f, -3.910924264e-03f, 7.277976922e-03f, -1.242070210e-02f,
          1.998195047e-02f, -3.091051491e-02f, 4.683508097e-02f, -7.111636989e-02f, 1.124619618e-01f,
          -2.030263946e-01f, 6.336615636e-01f, 6.336615636e-01f, -2.030263946e-01f, 1.124619618e-01f,
          -7.111636989e-02f, 4.683508097e-02f, -3.091051491e-02f, 1.998195047e-02f, -1.242070210e-02f,
          7.277976922e-03f, -3.910924264e-03f, 1.835897319e-03f, -6.695253674e-04f },
        {
          -3.411562067e-04f, 1.041000096e-03f, -2.324435424e-03f, 4.447899604e-03f, -7.733114346e-03f,
          1.260013474e-02f, -1.964997916e-02f, 2.987263692e-02f, -4.521340791e-02f, 7.040186757e-02f,
          -1.210733856e-01f, 2.968542668e-01f, 8.993894182e-01f, -1.746255780e-01f, 9.048277482e-02f,
          -5.605276152e-02f, 3.671208606e-02f, -2.427668742e-02f, 1.580335898e-02f, -9.937105079e-03f,
          5.921957025e-03f, -3.262828920e-03f, 1.595064467e-03f, -6.320256966e-04f },
    };

    struct Coeffs {
        simd::f32x4 holdKeep{};                     // per-sample factor of the TP OVER holds' release (alpha)
    };
    struct State {
        simd::f32x4 hist[2 * kTaps]{};              // x[n - 23] ... x[n] at hist[pos] ... hist[pos + 23]
        simd::f32x4 lvl{};                          // the last level (dB)
        simd::f32x4 tpHeld{}, spHeld{};             // held true peak and held sample peak (linear), TP OVER
        int pos = 0;                                // the oldest sample's index in [0, kTaps)
    };

    static void design(Coeffs& c, const EngineParams&, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.holdKeep = simd::set1(1.0f - fcdsp::oneMinusAlpha(kHoldMs, x.fs));
    }

    // One SC sample in (linear, per lane); the level of the interval [n - kDelay, n - kDelay + 3/4] out (dB).
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        s.hist[s.pos] = v;
        s.hist[s.pos + kTaps] = v;
        s.pos = s.pos + 1 < kTaps ? s.pos + 1 : 0;
        const simd::f32x4* w = s.hist + s.pos;                          // w[j] = x[n - 23 + j] = x[m - 11 + j]
        simd::f32x4 q1 = simd::set1(0.0f), q2 = simd::set1(0.0f), q3 = simd::set1(0.0f);
        for (int j = 0; j < kTaps; ++j)
        {
            q1 = simd::fma(q1, simd::set1(kPhase[0][j]), w[j]);
            q2 = simd::fma(q2, simd::set1(kPhase[1][j]), w[j]);
            q3 = simd::fma(q3, simd::set1(kPhase[2][j]), w[j]);
        }
        const simd::f32x4 sp = simd::abs(w[kHalf - 1]);                 // |x[m]|: phase 0, the sample itself
        const simd::f32x4 tp = simd::max(simd::max(sp, simd::abs(q1)), simd::max(simd::abs(q2), simd::abs(q3)));
        s.tpHeld = simd::max(simd::mul(s.tpHeld, c.holdKeep), tp);
        s.spHeld = simd::max(simd::mul(s.spHeld, c.holdKeep), sp);
        s.lvl = fcdsp::dbFromLin(tp);
        return s.lvl;
    }

    // From Carry::detDb (peak dB, the same domain): the ring holds the level as a constant (header comment).
    static void seed(State& s, simd::f32x4 levelDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 a = fcdsp::linFromDb(levelDb);
        for (simd::f32x4& h : s.hist)
            h = a;
        s.pos = 0;
        s.lvl = levelDb;
        s.tpHeld = a;
        s.spHeld = a;
    }

    static simd::f32x4 levelDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.lvl; }

    // TP OVER (Brickwall's internal): how far the held true peak stands over the held sample peak, dB, >= 0.
    static simd::f32x4 overDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 floor = simd::set1(kLinFloor);
        const simd::f32x4 r = simd::div(simd::max(floor, s.tpHeld), simd::max(floor, s.spHeld));
        return simd::max(simd::set1(0.0f), fcdsp::dbFromLin(r));
    }
};

static_assert(DetectorPolicy<TruePeak4x>);

} // namespace fcdsp::stage
