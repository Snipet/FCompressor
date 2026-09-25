// FCMP_PROBE layer=dsp name=slidingmax scope=global timeout=300
//
// dsp.slidingmax (M7, S11; SPRINTS S11.2; 01 §5.2-5.4, §10.7; E §5.4; D §2.8; K2 #21a-d, #12, #13): the policies Brickwall
// adds, stage::SlidingMaxBox (the lookahead ballistics over PrepareInfo::scratch), stage::TruePeak4x (the 4x true-peak
// side chain) and stage::LoudClip (VOICE LOUD), proven on the policy functions (unit rows, 01 §8.4 step 3), and the
// Mode's owner specs through fcdsp::EngineHost (K2 #21: the ceiling at every Quality, `look` automation, the internals).
// Spec rows only (no golden: the Mode's goldens are dsp.static/time/... of `brickwall`). References are double
// precision; true peaks are measured on a reference 4x reconstruction (RefTp below: a 64-tap-per-phase Kaiser sinc,
// beta 9, at the same four points per sample the detector uses, which every integer latency keeps aligned).
//
// SlidingMaxBox (48 kHz, the host's scratch size, lanes 0-1 driven with different targets):
//   slidingmax.max.mismatches        60,000 samples of random targets while `look` (and the ramp, = look) slews through
//                                    5, 1, 12, 0 and 3 ms and then the ramp alone to 8 ms: the sliding max h against the
//                                    brute-force max over the last max(R, L) + 2 targets, bit for bit (prehistory 0)
//   slidingmax.box.max_err_db        ... the ramp's output a against two brute-force cascaded box means in double over
//                                    the same h (L1 = floor((R + 2) / 2), L2 = R + 1 - L1): <= 1e-5 dB
//   slidingmax.cover.violations      R = L = 240: every target t[n] is met by the applied GR from n + L - 1 to
//                                    n + L + 1 (one base sample either side of its audio): 0 violations (<= 1e-5 dB)
//   slidingmax.ramp.reach_samples    a 10 dB step reaches exactly 10 dB R - 1 samples after it enters: 239
//   slidingmax.ramp.first_step_ratio the S-curve's first increment against 10 / (L1 L2) (a single box would step 10 / R,
//                                    120x larger): <= 1.0001; .monotone: the ramp never falls: 1
//   slidingmax.hold.from, .through   a one-sample 12 dB spike: the GR first reaches exactly 12 dB at n + L - 1 and holds
//                                    it through n + L + 1 (the release then starts below half an ulp, so it may stay a
//                                    sample longer)
//   slidingmax.resum.*               after 20,000 samples of targets in runs of 60 dB and of 1e-20 dB (running sums that
//                                    lose the small ones' bits) and then 0, both box outputs are exactly
//                                    0.0 (.zero: 1) within 4096 + 2 Wmax samples of the last non-zero target
//                                    (.samples_to_zero, the exact re-sum, K2 #21c); .gr_exact_zero: the applied GR is
//                                    exactly 0.0 one second later (the release lands, kLandDb)
//   slidingmax.look.max_step, .between_ticks, .lands   `look` 1 -> 10 ms: L moves by at most one sample per design()
//                                    (a control tick, K2 #21d), never between ticks, and lands on 480
//   slidingmax.jump.under_reads      a value-initialised Coeffs with a longer look mid-stream (a snap): h never under-
//                                    reads the brute-force max; .exact_after: exact again one window later
//   slidingmax.seed.*                seed({5, 7}): grDb = {5, 7, 5, 7} (.lanes), and the carried GR is held exactly for at
//                                    least Wmax samples of 0-dB targets (.held)
//   slidingmax.noscratch.mismatches  no scratch: a rising target is applied at once (no memory, no lookahead)
//   slidingmax.nan.propagates        a NaN target reaches grDb (the host's poison check sees it)
//   slidingmax.release.max_dev_db    L = 0: after a drop from 20 dB the GR follows r0 (1 - c)^n of its release (200 ms)
//                                    until it lands: <= 1e-4 dB
// TruePeak4x:
//   truepeak.coeffs.dc_err, .symmetry_mismatches   each phase sums to 1 (<= 1e-6); phase 3/4 is phase 1/4 reversed,
//                                    phase 1/2 symmetric
//   truepeak.accuracy.f<f>.max_err_db  sines at f / fs in {0.05, 0.15, 0.25, 0.35, 0.40, 0.4167}, 16 phases each: the
//                                    detector's level against max |x| over the analytic points m, m + 1/4, m + 1/2,
//                                    m + 3/4: <= 0.02 dB (the filter's design error is 0.0124 dB at 0.4167 fs)
//   truepeak.phase0.violations       noise: the level is never below |x[m]| (phase 0 is the sample itself); .dc: a
//                                    constant reads exactly dbFromLin(|x|)
//   truepeak.fs4_db                  12 kHz at 45 degrees (samples at -3.01 dB of the peak): reads 0 dB +- 0.02
//   truepeak.over_db                 ... and its TP OVER (held true peak over held sample peak): 3.0103 dB +- 0.02
//   truepeak.delay_samples           an impulse's report arrives kDelay = 12 samples later; .scdelay.tp / .peak: the
//                                    Brickwall engine's scDelaySamples() is 12 at DETECT TP and 0 at PEAK
//   truepeak.seed_err_db             seed(-6 dB) then a constant 10^(-6/20): -6 dB within 1e-5
// LoudClip (T = -18 dB):
//   loudclip.bound.violations        random input up to 30 dB over the corner at 48 kHz (the worst case for ADAA's
//                                    half-step overshoot): no output sample above 1/k (the safety clip)
//   loudclip.small.gain_err_db       a 1 kHz sine 40 dB under T passes within 0.001 dB of unity
//   loudclip.corner.err              a constant 3/(2k) (a held peak's drive) comes out at 1/k (<= 1e-6 relative)
//   loudclip.transfer.max_err        constants through process() against transfer(): <= 1e-6 relative
//   loudclip.silence.nonzero, loudclip.bs.mismatches   silence stays 0; one block against odd splits, bit for bit
//   loudclip.level.lands             T moved by 6 dB: the clip level moves per tick and lands exactly
//   loudclip.headroom_err_db         kHeadroomDb = 20 log10(4/3) - 0.25 (the fitted constant, LoudClip.h)
// Brickwall through EngineHost (48 kHz, THRESHOLD -18, CEILING -1, LOOKAHEAD 5 ms in a 20 ms budget, DETECT TP unless
// noted; two programs 18 dB over the threshold, both band-limited to 20 kHz by a 511-tap linear-phase low-pass: the
// "stress" program (band-limited white-noise bursts, four sines, 12 kHz at 45 degrees, squares, noise hits, a sweep) and
// the "music" program (pink-noise sections, drums, a piano-like chord, bass and hats, a sweep); true peak over the
// ceiling in dB, whole program, from the latency on):
//   bw.<eco|hq>.clean.<prog>.tp_over_db  <= +0.1 (K2 #21b; ECO has no OS path, so it holds like HQ)
//   bw.std.clean.music.tp_over_db        <= +1.0 (01 §10.7: STD overshoot is documented, not clipped)
//   bw.std.clean.<prog>.excess_db        the STD render's true peak against the STD oversampler's own round trip of the
//                                    ECO render (the same GR): <= +0.1: at STD the limiter holds and what remains is the
//                                    IIR round trip's phase dispersion (NOTE: the stress program's STD overshoot and the
//                                    round trip's own gain on the input)
//   bw.<q>.clean.<prog>.gr_active_db >= 10 (the program is limited)
//   bw.eco.peak.<prog>.sample_over_db    DETECT PEAK at ECO: the output sample peak over the ceiling <= +0.001 dB
//   bw.hq.loud.<prog>.tp_over_db         VOICE LOUD at HQ: <= +0.1 (LoudClip.h's fitted margin);
//   bw.eco.loud.<prog>.sample_over_db    <= -1.0 (the safety clip: output samples stay at ceiling - 1.27 dB); ECO / STD
//                                    LOUD true peaks: NOTE
//   bw.loud.gr_mismatches            the tapped GR of LOUD equals CLEAN's bit for bit (colour never feeds the control
//                                    path); bw.loud.louder_db: LOUD is >= 0.5 dB louder (RMS, music, HQ)
//   bw.look.auto.*                   HQ, music, `look` automated 1 -> 10 -> 2 ms at block rate (blocks of 16): the true
//                                    peak stays within +0.25 dB (.tp_over_db: while `look` moves the host's SC read
//                                    position skips or repeats one sample per tick, K2 #21d, which the true-peak
//                                    interpolator sees as a discontinuity; steady `look` is the +0.1 rows), LOOK EFF moves
//                                    by at most one
//                                    sample per 16-sample block (.max_step_samples <= 1) and lands (.lands), the latency
//                                    never moves (.latency_changes 0)
//   bw.internals.*                   a steady 1 kHz square at T + 6 (PEAK): HELD PEAK = T + 6 dBFS +- 0.01, LOOK EFF =
//                                    5 ms +- 1 sample, TP OVER 0 at DETECT PEAK; TP OVER of the 12 kHz / 45 degree sine
//                                    at DETECT TP = 3.0103 +- 0.05 dB
//   bw.soak.tp.gr_nonzero            DETECT TP, STD: 60 s under the threshold after 2 s of the music program: GR exactly
//                                    0.0 from 3 s on (dsp.null's 10-minute row runs the defaults)
//   bw.budgetoff.*                   NOTE: the budget OFF (zero latency) overshoot at each Quality
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/engine/stages/ballistics/SlidingMaxBox.h"
#include "fcdsp/engine/stages/colour/LoudClip.h"
#include "fcdsp/engine/stages/detector/TruePeak4x.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;
    using SMB = stage::SlidingMaxBox;
    using TP4 = stage::TruePeak4x;

    constexpr float kFs = 48000.0f;
    constexpr double kPi = 3.141592653589793;
    constexpr float kThr = -18.0f, kCeiling = -1.0f;

    simd::f32x4 vec(float a, float b, float c, float d)
    {
        alignas(16) const float v[4] = { a, b, c, d };
        return simd::load(v);
    }
    float laneOf(simd::f32x4 v, int ln)
    {
        alignas(16) float t[4];
        simd::store(t, v);
        return t[ln & 3];
    }
    double dbOf(double a) { return measure::dbFromAmplitude(a); }

    double besselI0(double x)
    {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 200; ++k)
        {
            const double q = x / (2.0 * k);
            t *= q * q;
            s += t;
            if (t < 1e-18 * s)
                break;
        }
        return s;
    }

    // The reference 4x reconstruction (file comment): 32 taps either side, beta 9, each phase at unit DC gain.
    struct RefTp
    {
        static constexpr int kN = 32;
        double c[3][2 * kN]{};
        RefTp()
        {
            const double beta = 9.0, i0b = besselI0(beta);
            for (int p = 0; p < 3; ++p)
            {
                const double f = (p + 1) / 4.0;
                double sum = 0.0;
                for (int j = 0; j < 2 * kN; ++j)
                {
                    const double t = f - static_cast<double>(j - (kN - 1));
                    const double r = t / kN;
                    const double w = std::fabs(r) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
                    const double sn = sig::sinTurns(t / 2.0) / (kPi * t);
                    c[p][j] = sn * w;
                    sum += sn * w;
                }
                for (int j = 0; j < 2 * kN; ++j)
                    c[p][j] /= sum;
            }
        }
        // max |y| over the samples and the three points after each, from index `from`
        double peak(const std::vector<float>& y, std::size_t from) const
        {
            double m = 0.0;
            const auto first = std::max<std::size_t>(from, kN);
            for (std::size_t n = first; n + kN < y.size(); ++n)
            {
                m = std::max(m, std::fabs(static_cast<double>(y[n])));
                for (int p = 0; p < 3; ++p)
                {
                    double acc = 0.0;
                    for (int j = 0; j < 2 * kN; ++j)
                        acc += c[p][j] * static_cast<double>(y[n + static_cast<std::size_t>(j) - (kN - 1)]);
                    m = std::max(m, std::fabs(acc));
                }
            }
            return m;
        }
    };

    // ---- the programs ---------------------------------------------------------------------------------------------------
    // A linear-phase low-pass (Kaiser-windowed sinc, 511 taps, beta 10, cutoff 19.4 kHz at 48 kHz: stop band from about
    // 20 kHz), in double, delay removed: the programs carry no content between 20 kHz and Nyquist (TruePeak4x.h: its
    // interpolator is fitted to 20 kHz at 48 kHz).
    void lowpass(std::vector<double>& x)
    {
        constexpr int kHalfTaps = 255;
        const double fc = 19400.0 / static_cast<double>(kFs), beta = 10.0, i0b = besselI0(beta);
        std::vector<double> h(2 * kHalfTaps + 1);
        double sum = 0.0;
        for (int k = -kHalfTaps; k <= kHalfTaps; ++k)
        {
            const double t = static_cast<double>(k), r = t / (kHalfTaps + 1);
            const double w = besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
            const double sn = k == 0 ? 2.0 * fc : sig::sinTurns(fc * t) / (kPi * t);
            h[static_cast<std::size_t>(k + kHalfTaps)] = sn * w;
            sum += sn * w;
        }
        std::vector<double> y(x.size(), 0.0);
        const auto n = static_cast<std::ptrdiff_t>(x.size());
        for (std::ptrdiff_t i = 0; i < n; ++i)
        {
            double acc = 0.0;
            for (int k = -kHalfTaps; k <= kHalfTaps; ++k)
                if (const std::ptrdiff_t j = i + k; j >= 0 && j < n)
                    acc += h[static_cast<std::size_t>(k + kHalfTaps)] * x[static_cast<std::size_t>(j)];
            y[static_cast<std::size_t>(i)] = acc / sum;
        }
        x = std::move(y);
    }

    struct Stereo
    {
        std::vector<float> l, r;
        std::size_t size() const { return l.size(); }
    };

    // Each 1 s segment normalised to a peak of `amp`, then low-passed and converted to float.
    Stereo finish(std::vector<double>& dl, std::vector<double>& dr, std::size_t segments, double amp)
    {
        const auto seg = static_cast<std::size_t>(kFs);
        for (std::size_t s = 0; s < segments; ++s)
        {
            double pk = 1e-30;
            for (std::size_t i = s * seg; i < (s + 1) * seg; ++i)
                pk = std::max({ pk, std::fabs(dl[i]), std::fabs(dr[i]) });
            for (std::size_t i = s * seg; i < (s + 1) * seg; ++i)
            {
                dl[i] *= amp / pk;
                dr[i] *= amp / pk;
            }
        }
        lowpass(dl);
        lowpass(dr);
        Stereo out;
        out.l.resize(dl.size());
        out.r.resize(dr.size());
        for (std::size_t i = 0; i < dl.size(); ++i)
        {
            out.l[i] = static_cast<float>(dl[i]);
            out.r[i] = static_cast<float>(dr[i]);
        }
        return out;
    }

    // The stress program (6 s): band-limited white-noise bursts at random levels, four sines (1, 5, 11, 15 kHz), 12 kHz
    // at 45 degrees and 11,025 Hz, a 1 kHz and a 3 kHz square, noise hits every 125 ms (instant onset), a sweep.
    Stereo stressProgram(double amp)
    {
        const auto seg = static_cast<std::size_t>(kFs);
        const std::size_t n = 6 * seg;
        std::vector<double> l(n, 0.0), r(n, 0.0);
        sig::Pcg32 rng(0x62726b77, 7);
        for (std::size_t i = 0; i < seg; ++i)
        {
            const std::size_t burst = i / 2400;
            const double g = 0.25 + 0.75 * static_cast<double>((burst * 2654435761u) % 1000) / 1000.0;
            l[i] = g * static_cast<double>(rng.bipolar());
            r[i] = g * static_cast<double>(rng.bipolar());
        }
        const double hz[4] = { 1000.0, 5000.0, 11000.0, 15000.0 };
        for (std::size_t i = 0; i < seg; ++i)
        {
            const auto n0 = static_cast<std::int64_t>(seg + i);
            for (int k = 0; k < 4; ++k)
            {
                l[seg + i] += sig::sinTurns(sig::sinePhase(n0, hz[k], kFs, 0.1 * k));
                r[seg + i] += sig::sinTurns(sig::sinePhase(n0, hz[k], kFs, 0.37 + 0.13 * k));
            }
        }
        for (std::size_t i = 0; i < seg; ++i)
        {
            const auto n0 = static_cast<std::int64_t>(2 * seg + i);
            l[2 * seg + i] = sig::sinTurns(sig::sinePhase(n0, 12000.0, kFs, 0.125));
            r[2 * seg + i] = sig::sinTurns(sig::sinePhase(n0, 11025.0, kFs, 0.3));
        }
        for (std::size_t i = 0; i < seg; ++i)
        {
            l[3 * seg + i] = ((3 * seg + i) / 24) % 2 == 0 ? 1.0 : -1.0;
            r[3 * seg + i] = ((3 * seg + i) / 8) % 2 == 0 ? 1.0 : -1.0;
        }
        for (std::size_t i = 0; i < seg; ++i)
        {
            const double env = sig::expDet(-static_cast<double>(i % 6000) / static_cast<double>(kFs) / 0.02);
            l[4 * seg + i] = env * static_cast<double>(rng.bipolar());
            r[4 * seg + i] = env * static_cast<double>(rng.bipolar());
        }
        const sig::LogSweep sw(20.0, 20000.0, kFs, static_cast<std::int64_t>(seg), 1.0);
        for (std::size_t i = 0; i < seg; ++i)
        {
            l[5 * seg + i] = static_cast<double>(sw.at(static_cast<std::int64_t>(i)));
            r[5 * seg + i] = 0.8 * static_cast<double>(sw.at(static_cast<std::int64_t>(i)));
        }
        return finish(l, r, 6, amp);
    }

    // Pink noise (Paul Kellet's refined filter), arithmetic only.
    struct Pink
    {
        double b[7]{};
        double next(double w)
        {
            b[0] = 0.99886 * b[0] + w * 0.0555179;
            b[1] = 0.99332 * b[1] + w * 0.0750759;
            b[2] = 0.96900 * b[2] + w * 0.1538520;
            b[3] = 0.86650 * b[3] + w * 0.3104856;
            b[4] = 0.55000 * b[4] + w * 0.5329522;
            b[5] = -0.7616 * b[5] - w * 0.0168980;
            const double p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362;
            b[6] = w * 0.115926;
            return p * 0.11;
        }
    };

    // The music program (5 s): pink-noise sections (a level per 250 ms, 5 ms ramps), drums (a kick and a pink hit with
    // a 1 ms attack every 250 ms), a piano-like chord (four notes, harmonics 1/n^2 to 12 kHz, retriggered every 500 ms),
    // bass and hats (55 Hz and 4 ms noise ticks with a 0.5 ms attack every 125 ms), a sweep to 19 kHz.
    Stereo musicProgram(double amp)
    {
        const auto seg = static_cast<std::size_t>(kFs);
        const std::size_t n = 5 * seg;
        std::vector<double> l(n, 0.0), r(n, 0.0);
        sig::Pcg32 rng(0x6d757369, 3);
        Pink pl, pr;
        const auto fsd = static_cast<double>(kFs);
        for (std::size_t i = 0; i < seg; ++i)
        {
            const std::size_t k = i / 12000, j = i % 12000;
            const double g0 = 0.3 + 0.7 * static_cast<double>((k * 2654435761u) % 997) / 997.0;
            const double g1 = 0.3 + 0.7 * static_cast<double>(((k + 1) * 2654435761u) % 997) / 997.0;
            const double g = j + 240 >= 12000 ? g0 + (g1 - g0) * static_cast<double>(j + 240 - 12000) / 240.0 : g0;
            l[i] = g * pl.next(static_cast<double>(rng.bipolar()));
            r[i] = g * pr.next(static_cast<double>(rng.bipolar()));
        }
        for (std::size_t i = 0; i < seg; ++i)
        {
            const double t = static_cast<double>(i % 12000) / fsd;
            const double env = (t < 0.001 ? t / 0.001 : 1.0) * sig::expDet(-t / 0.06);
            const double kick = sig::sinTurns(60.0 * t) * sig::expDet(-t / 0.15);
            l[seg + i] = 0.6 * kick + 1.5 * env * pl.next(static_cast<double>(rng.bipolar()));
            r[seg + i] = 0.6 * kick + 1.5 * env * pr.next(static_cast<double>(rng.bipolar()));
        }
        const double f0[4] = { 220.0, 277.18, 329.63, 440.0 };
        for (std::size_t i = 0; i < seg; ++i)
        {
            const double t = static_cast<double>(i % 24000) / fsd, tt = static_cast<double>(2 * seg + i) / fsd;
            for (int q = 0; q < 4; ++q)
                for (int h = 1; h <= 20 && f0[q] * h < 12000.0; ++h)
                {
                    const double a = sig::expDet(-t * h / 0.8) / (h * h);
                    l[2 * seg + i] += a * sig::sinTurns(f0[q] * h * tt + 0.1 * q + 0.05 * h);
                    r[2 * seg + i] += a * sig::sinTurns(f0[q] * h * tt + 0.3 * q + 0.11 * h);
                }
        }
        for (std::size_t i = 0; i < seg; ++i)
        {
            const double tt = static_cast<double>(i) / fsd, th = static_cast<double>(i % 6000) / fsd;
            const double hat = th < 0.004 ? static_cast<double>(rng.bipolar()) * 0.35
                                                * (th < 0.0005 ? th / 0.0005 : 1.0 - (th - 0.0005) / 0.0035)
                                          : 0.0;
            l[3 * seg + i] = 0.7 * sig::sinTurns(55.0 * tt) + hat;
            r[3 * seg + i] = 0.7 * sig::sinTurns(55.0 * tt + 0.25) + hat;
        }
        const sig::LogSweep sw(20.0, 19000.0, kFs, static_cast<std::int64_t>(seg), 1.0);
        for (std::size_t i = 0; i < seg; ++i)
        {
            l[4 * seg + i] = static_cast<double>(sw.at(static_cast<std::int64_t>(i)));
            r[4 * seg + i] = 0.8 * static_cast<double>(sw.at(static_cast<std::int64_t>(i)));
        }
        return finish(l, r, 5, amp);
    }

    // ---- the host ---------------------------------------------------------------------------------------------------------
    BlockParams blockOf(const ModeEntry& en, RawParams raw)
    {
        raw.modeSlot = static_cast<std::uint8_t>(slotOf(en));
        BlockParams bp;
        bp.slot = raw.modeSlot;
        bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
        return bp;
    }

    HostConfig hostConfig(Quality q, LookaheadBudget b)
    {
        HostConfig c;
        c.fs = kFs;
        c.maxBlock = 512;
        c.quality = q;
        c.budget = b;
        return c;
    }

    const char* qualityName(Quality q) { return q == Quality::eco ? "eco" : q == Quality::std ? "std" : "hq"; }

    // Brickwall's raw state for the host rows: THRESHOLD -18, CEILING -1, LOOKAHEAD 5 ms, DETECT / VOICE as given.
    RawParams bwRaw(const ModeEntry& bw, LookaheadBudget b, int det, int voice, float lookMs = 5.0f)
    {
        RawParams raw = fcmp::probe::modeRaw(bw, b);
        raw[Pid::thr] = kThr;
        raw[Pid::makeup] = kCeiling;
        raw[Pid::look] = lookMs;
        raw[Pid::det] = static_cast<float>(det);
        raw[Pid::voice] = static_cast<float>(voice);
        return raw;
    }

    struct Render
    {
        Stereo y;
        std::vector<simd::f32x4> gr;
        int latency = 0;
    };

    Render render(const HostConfig& cfg, const BlockParams& bp, const Stereo& x)
    {
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        Render o;
        const std::size_t n = x.size();
        o.y.l.assign(n, 0.0f);
        o.y.r.assign(n, 0.0f);
        o.gr.assign(n, simd::set1(0.0f));
        TestTap tap;
        tap.grDb = o.gr;
        host->setTap(&tap);
        for (std::size_t off = 0; off < n; off += 512)
        {
            const std::size_t len = std::min<std::size_t>(512, n - off);
            const float* ins[2] = { x.l.data() + off, x.r.data() + off };
            float* outs[2] = { o.y.l.data() + off, o.y.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, bp);
        }
        host->setTap(nullptr);
        o.latency = host->latencySamples();
        return o;
    }

    // The true peak (dB) of a stereo output from sample `from` on.
    double tpDb(const RefTp& ref, const Stereo& y, std::size_t from)
    {
        return dbOf(std::max(ref.peak(y.l, from), ref.peak(y.r, from)));
    }
    double samplePeakDb(const Stereo& y, std::size_t from)
    {
        double m = 0.0;
        for (std::size_t i = from; i < y.size(); ++i)
            m = std::max({ m, std::fabs(static_cast<double>(y.l[i])), std::fabs(static_cast<double>(y.r[i])) });
        return dbOf(m);
    }
    double maxGr(const Render& r)
    {
        double m = 0.0;
        for (const simd::f32x4 g : r.gr)
            m = std::max({ m, static_cast<double>(laneOf(g, 0)), static_cast<double>(laneOf(g, 1)) });
        return m;
    }
    double rmsDb(const Stereo& y, std::size_t from)
    {
        double e = 0.0;
        for (std::size_t i = from; i < y.size(); ++i)
            e += static_cast<double>(y.l[i]) * static_cast<double>(y.l[i])
               + static_cast<double>(y.r[i]) * static_cast<double>(y.r[i]);
        return 10.0 * std::log10(std::max(e / static_cast<double>(2 * (y.size() - from)), 1e-30));
    }

    // The STD oversampler's own round trip of a stereo signal (its latency kStdLatency is kept).
    Stereo stdRoundTrip(const Stereo& x)
    {
        Oversampler os;
        os.configure(Quality::std, 512, 2);
        Stereo y;
        y.l.assign(x.size(), 0.0f);
        y.r.assign(x.size(), 0.0f);
        std::vector<float> ul(1024), ur(1024);
        for (std::size_t off = 0; off < x.size(); off += 512)
        {
            const int m = static_cast<int>(std::min<std::size_t>(512, x.size() - off));
            const float* in[2] = { x.l.data() + off, x.r.data() + off };
            float* up[2] = { ul.data(), ur.data() };
            const int nOs = os.up(in, m, up);
            const float* upc[2] = { ul.data(), ur.data() };
            float* out[2] = { y.l.data() + off, y.r.data() + off };
            os.down(upc, nOs, out);
        }
        return y;
    }

    // ---- the SlidingMaxBox rig: the policy as ModeEngine drives it (design on the ticks, one tick per sample) ------------
    struct BoxRig
    {
        std::vector<float> scratch;
        SMB::Coeffs c{};
        SMB::State s{};
        EngineParams e{};
        StageCtx ctx{};
        std::uint64_t n = 0;
        int designs = 0;

        explicit BoxRig(std::size_t floats) : scratch(floats, 0.0f)
        {
            ctx = StageCtx{ kFs, kFs, 1, std::span<float>(scratch) };
            e.lookMs = 5.0f;
            e.atkTauMs = 5.0f;
            e.relTauMs = 50.0f;
            SMB::design(c, e, ctx);
            SMB::seed(s, simd::set1(0.0f));
        }
        void set(float lookMs, float atkMs)
        {
            e.lookMs = lookMs;
            e.atkTauMs = atkMs;
        }
        simd::f32x4 tick(float g0, float g1)
        {
            if (n % static_cast<std::uint64_t>(kTickSamples) == 0)
            {
                SMB::design(c, e, ctx);
                ++designs;
            }
            ++n;
            return SMB::tick(c, s, vec(g0, g1, g0, g1));
        }
        int wmax() const { return std::max(c.ramp, c.look) + SMB::kHoldExtra; }
    };

    // The host's scratch per slot at 48 kHz (01 §5.3): 4 x nextPow2(ceil(20 ms fs) + kChunk).
    std::size_t hostScratch()
    {
        std::size_t p = 1;
        while (p < static_cast<std::size_t>(lookaheadSamples(LookaheadBudget::ms20, static_cast<double>(kFs))) + kChunk)
            p <<= 1;
        return 4 * p;
    }

    // Random targets: runs of silence, of steady GR and of noise-like GR, lanes different.
    float targetAt(sig::Pcg32& rng, std::size_t i, int lane)
    {
        const std::size_t run = (i / 700 + static_cast<std::size_t>(lane) * 3) % 5;
        if (run == 0)
            return 0.0f;
        if (run == 1)
            return 12.5f + static_cast<float>(lane);
        return 30.0f * rng.uniform() * rng.uniform();
    }

    // Processes x through the host in blocks of `block`, calling each(off, frame) after every block (attached).
    template <class Params, class Each>
    Stereo drive(EngineHost& host, const Stereo& x, std::size_t block, Params&& params, Each&& each)
    {
        Stereo y;
        y.l.assign(x.size(), 0.0f);
        y.r.assign(x.size(), 0.0f);
        for (std::size_t off = 0; off < x.size(); off += block)
        {
            const std::size_t len = std::min(block, x.size() - off);
            const float* ins[2] = { x.l.data() + off, x.r.data() + off };
            float* outs[2] = { y.l.data() + off, y.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host.process(io, params(off));
            UiFrame f{};
            if (host.readUiFrame(f))
                each(off, f);
        }
        return y;
    }

    // ---- the sections -------------------------------------------------------------------------------------------------
    void boxRows(Probe& P)
    {
        {
            // max and boxes against brute force while look and the ramp slew
            BoxRig rig(hostScratch());
            sig::Pcg32 rng(0x736d6178, 1);
            std::vector<float> g[2];
            std::vector<double> h[2], b[2];
            std::int64_t maxMismatch = 0;
            double boxErr = 0.0;
            for (std::size_t i = 0; i < 60000; ++i)
            {
                if (i == 0)
                    rig.set(5.0f, 5.0f);
                if (i == 10000)
                    rig.set(1.0f, 1.0f);
                if (i == 20000)
                    rig.set(12.0f, 12.0f);
                if (i == 35000)
                    rig.set(0.0f, 0.0f);
                if (i == 45000)
                    rig.set(3.0f, 3.0f);
                if (i == 52000)
                    rig.set(3.0f, 8.0f);
                const float t0 = targetAt(rng, i, 0), t1 = targetAt(rng, i, 1);
                g[0].push_back(t0);
                g[1].push_back(t1);
                (void) rig.tick(t0, t1);
                const auto wmax = static_cast<std::size_t>(rig.wmax());
                const int l1 = SMB::box1Length(rig.c.ramp), l2 = SMB::box2Length(rig.c.ramp);
                for (int k = 0; k < 2; ++k)
                {
                    const auto ku = static_cast<std::size_t>(k);
                    float m = 0.0f;
                    for (std::size_t j = i + 1 > wmax ? i + 1 - wmax : 0; j <= i; ++j)
                        m = std::max(m, g[ku][j]);
                    maxMismatch += laneOf(rig.s.held, k) == m ? 0 : 1;
                    h[ku].push_back(static_cast<double>(m));
                    double s1 = 0.0;
                    for (int j = 0; j < l1; ++j)
                        s1 += i >= static_cast<std::size_t>(j) ? h[ku][i - static_cast<std::size_t>(j)] : 0.0;
                    b[ku].push_back(s1 / l1);
                    double s2 = 0.0;
                    for (int j = 0; j < l2; ++j)
                        s2 += i >= static_cast<std::size_t>(j) ? b[ku][i - static_cast<std::size_t>(j)] : 0.0;
                    boxErr = std::max(boxErr, std::fabs(static_cast<double>(laneOf(rig.s.box, k)) - s2 / l2));
                }
            }
            std::printf("NOTE     slidingmax.max: 60000 samples, look 5 / 1 / 12 / 0 / 3 ms, then the ramp 8 ms: %lld "
                        "mismatch(es); box error %.3g dB\n",
                        static_cast<long long>(maxMismatch), boxErr);
            P.eq("slidingmax.max.mismatches", maxMismatch, 0);
            P.le("slidingmax.box.max_err_db", boxErr, 1e-5);
        }
        {
            // coverage: R = L = 240
            BoxRig rig(hostScratch());
            rig.set(5.0f, 5.0f);
            sig::Pcg32 rng(0x636f7665, 2);
            const std::size_t n = 30000;
            const auto la = static_cast<std::size_t>(SMB::lookSamples(5.0f, kFs));
            std::vector<float> g0(n), g1(n), r0(n), r1(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                g0[i] = targetAt(rng, i, 0);
                g1[i] = targetAt(rng, i, 1);
                const simd::f32x4 r = rig.tick(g0[i], g1[i]);
                r0[i] = laneOf(r, 0);
                r1[i] = laneOf(r, 1);
            }
            std::int64_t violations = 0;
            for (std::size_t i = 0; i + la + 1 < n; ++i)
                for (std::size_t t = i + la - 1; t <= i + la + 1; ++t)
                    violations += (r0[t] + 1e-5f < g0[i] ? 1 : 0) + (r1[t] + 1e-5f < g1[i] ? 1 : 0);
            P.eq("slidingmax.cover.violations", violations, 0);
        }
        {
            // the ramp: a 10 dB step; the hold: a one-sample 12 dB spike
            BoxRig rig(hostScratch());
            rig.set(5.0f, 5.0f);
            for (int i = 0; i < 2000; ++i)
                (void) rig.tick(0.0f, 0.0f);
            const int l1 = SMB::box1Length(rig.c.ramp), l2 = SMB::box2Length(rig.c.ramp);
            int reach = -1;
            double firstStep = 0.0, prev = 0.0;
            bool monotone = true;
            for (int i = 0; i < 600; ++i)
            {
                const float r = laneOf(rig.tick(10.0f, 10.0f), 0);
                if (i == 0)
                    firstStep = static_cast<double>(r);
                monotone = monotone && static_cast<double>(r) >= prev;
                prev = static_cast<double>(r);
                if (reach < 0 && r == 10.0f)
                    reach = i;
            }
            std::printf("NOTE     slidingmax.ramp: R %d (L1 %d, L2 %d): 10 dB reached at +%d samples, first step %.6g dB\n",
                        rig.c.ramp, l1, l2, reach, firstStep);
            P.eq("slidingmax.ramp.reach_samples", reach, rig.c.ramp - 1);
            P.le("slidingmax.ramp.first_step_ratio", firstStep / (10.0 / (l1 * l2)), 1.0001);
            P.eq("slidingmax.ramp.monotone", monotone ? 1 : 0, 1);

            BoxRig spike(hostScratch());
            spike.set(5.0f, 5.0f);
            for (int i = 0; i < 2000; ++i)
                (void) spike.tick(0.0f, 0.0f);
            std::vector<float> rs;
            for (int i = 0; i < 1000; ++i)
                rs.push_back(laneOf(spike.tick(i == 0 ? 12.0f : 0.0f, 0.0f), 0));
            const auto la = static_cast<std::size_t>(spike.c.look);
            int first = -1;
            for (std::size_t i = 0; i < rs.size() && first < 0; ++i)
                if (rs[i] == 12.0f)
                    first = static_cast<int>(i);
            P.eq("slidingmax.hold.from", first, spike.c.look - 1);
            P.eq("slidingmax.hold.through", rs[la - 1] == 12.0f && rs[la] == 12.0f && rs[la + 1] == 12.0f ? 1 : 0, 1);
        }
        {
            // the exact re-sum: fractional targets, then zeros
            BoxRig rig(hostScratch());
            rig.set(5.0f, 5.0f);
            sig::Pcg32 rng(0x72657375, 3);
            for (int i = 0; i < 20000; ++i)                     // runs of 60 dB and of 1e-20 dB: sums that lose bits
                (void) rig.tick((i / 400) % 2 == 0 ? 59.37f * rng.uniform() + 0.1f : 1e-20f * rng.uniform(),
                                (i / 300) % 2 == 0 ? 7.77f * rng.uniform() + 0.1f : 3e-25f * rng.uniform());
            const int wmax = rig.wmax();
            int zeroFrom = -1;
            float residual = 0.0f;
            for (int i = 0; i < 20000; ++i)
            {
                (void) rig.tick(0.0f, 0.0f);
                const bool zero = laneOf(rig.s.box, 0) == 0.0f && laneOf(rig.s.box, 1) == 0.0f;
                if (i > 2 * wmax && !zero)
                    residual = std::max({ residual, std::fabs(laneOf(rig.s.box, 0)), std::fabs(laneOf(rig.s.box, 1)) });
                if (zeroFrom < 0 && zero)
                    zeroFrom = i;
                if (!zero)
                    zeroFrom = -1;
            }
            bool grZero = true;
            for (int i = 0; i < 48000; ++i)
            {
                const simd::f32x4 r = rig.tick(0.0f, 0.0f);
                if (i >= 47000)
                    grZero = grZero && laneOf(r, 0) == 0.0f && laneOf(r, 1) == 0.0f;
            }
            std::printf("NOTE     slidingmax.resum: the boxes read exactly 0.0 from +%d samples after the last target (Wmax "
                        "%d); the running sums' residual before the re-sum %.3g dB\n",
                        zeroFrom, wmax, static_cast<double>(residual));
            P.eq("slidingmax.resum.zero", zeroFrom >= 0 ? 1 : 0, 1);
            P.le("slidingmax.resum.samples_to_zero", static_cast<double>(zeroFrom), 4096.0 + 2.0 * wmax);
            P.eq("slidingmax.resum.gr_exact_zero", grZero ? 1 : 0, 1);
        }
        {
            // look slews one sample per tick
            BoxRig rig(hostScratch());
            rig.set(1.0f, 1.0f);
            for (int i = 0; i < 1600; ++i)
                (void) rig.tick(0.0f, 0.0f);
            rig.set(10.0f, 10.0f);
            int maxStep = 0, prevLook = rig.c.look, prevDesigns = rig.designs;
            std::int64_t between = 0;
            for (int i = 0; i < 16 * 600; ++i)
            {
                (void) rig.tick(5.0f, 5.0f);
                const int step = std::abs(rig.c.look - prevLook);
                maxStep = std::max(maxStep, step);
                between += step != 0 && rig.designs == prevDesigns ? 1 : 0;
                prevLook = rig.c.look;
                prevDesigns = rig.designs;
            }
            P.eq("slidingmax.look.max_step", maxStep, 1);
            P.eq("slidingmax.look.between_ticks", between, 0);
            P.eq("slidingmax.look.lands", rig.c.look == 480 ? 1 : 0, 1);
        }
        {
            // a look jump (snapParams: a value-initialised Coeffs lands at once): never an under-read, exact after a
            // window
            BoxRig rig(hostScratch());
            rig.set(2.0f, 2.0f);
            sig::Pcg32 rng(0x6a756d70, 4);
            std::vector<float> g;
            std::int64_t under = 0, inexact = 0;
            for (std::size_t i = 0; i < 20000; ++i)
            {
                if (i == 10000)
                {
                    rig.set(15.0f, 15.0f);
                    rig.c = SMB::Coeffs{};
                    SMB::design(rig.c, rig.e, rig.ctx);
                }
                const float t = targetAt(rng, i, 0);
                g.push_back(t);
                (void) rig.tick(t, t);
                const auto wmax = static_cast<std::size_t>(rig.wmax());
                float m = 0.0f;
                for (std::size_t j = i + 1 > wmax ? i + 1 - wmax : 0; j <= i; ++j)
                    m = std::max(m, g[j]);
                under += laneOf(rig.s.held, 0) < m ? 1 : 0;
                if (i >= 10000 + wmax)
                    inexact += laneOf(rig.s.held, 0) == m ? 0 : 1;
            }
            P.eq("slidingmax.jump.under_reads", under, 0);
            P.eq("slidingmax.jump.exact_after", inexact, 0);
        }
        {
            // seed, no scratch, NaN, release
            BoxRig rig(hostScratch());
            rig.set(5.0f, 5.0f);
            (void) rig.tick(0.0f, 0.0f);
            SMB::seed(rig.s, vec(5.0f, 7.0f, 1.0f, 2.0f));
            const simd::f32x4 gr = SMB::grDb(rig.s);
            P.eq("slidingmax.seed.lanes",
                 laneOf(gr, 0) == 5.0f && laneOf(gr, 1) == 7.0f && laneOf(gr, 2) == 5.0f && laneOf(gr, 3) == 7.0f ? 1 : 0,
                 1);
            int held = 0;
            for (int i = 0; i < 2000; ++i)
            {
                const simd::f32x4 r = rig.tick(0.0f, 0.0f);
                if (laneOf(r, 0) != 5.0f || laneOf(r, 1) != 7.0f)
                    break;
                ++held;
            }
            std::printf("NOTE     slidingmax.seed: the carried GR held for %d samples (Wmax %d)\n", held, rig.wmax());
            P.ge("slidingmax.seed.held", static_cast<double>(held), static_cast<double>(rig.wmax()));

            BoxRig bare(0);
            std::int64_t bareMismatch = 0;
            for (int i = 0; i < 100; ++i)
            {
                const float t = 0.25f * static_cast<float>(i);
                bareMismatch += laneOf(bare.tick(t, t), 0) == t ? 0 : 1;
            }
            P.eq("slidingmax.noscratch.mismatches", bareMismatch, 0);

            BoxRig nan(hostScratch());
            (void) nan.tick(3.0f, 3.0f);
            const simd::f32x4 rn = nan.tick(std::nanf(""), 0.0f);
            P.eq("slidingmax.nan.propagates", std::isnan(laneOf(rn, 0)) ? 1 : 0, 1);

            BoxRig rel(hostScratch());
            rel.set(0.0f, 0.0f);
            rel.e.relTauMs = 200.0f;
            rel.c = SMB::Coeffs{};                                  // primed on 200 ms (no time glide)
            SMB::design(rel.c, rel.e, rel.ctx);
            for (int i = 0; i < 4800; ++i)
                (void) rel.tick(20.0f, 20.0f);
            const auto c = static_cast<double>(oneMinusAlpha(200.0f, kFs));
            const auto r0 = static_cast<double>(laneOf(SMB::grDb(rel.s), 0));
            double dev = 0.0, pw = 1.0;
            bool started = false;
            for (int i = 0; i < 48000 * 3; ++i)
            {
                const auto r = static_cast<double>(laneOf(rel.tick(0.0f, 0.0f), 0));
                if (!started && r < r0)
                    started = true;
                if (!started)
                    continue;
                pw *= 1.0 - c;
                if (r == 0.0)
                    break;
                dev = std::max(dev, std::fabs(r - r0 * pw));
            }
            P.le("slidingmax.release.max_dev_db", dev, 1e-4);
        }
    }

    void truePeakRows(Probe& P, const ModeEntry& bw)
    {
        double dcErr = 0.0;
        std::int64_t sym = 0;
        for (int p = 0; p < 3; ++p)
        {
            double s = 0.0;
            for (int j = 0; j < TP4::kTaps; ++j)
                s += static_cast<double>(TP4::kPhase[p][j]);
            dcErr = std::max(dcErr, std::fabs(s - 1.0));
        }
        for (int j = 0; j < TP4::kTaps; ++j)
            sym += (TP4::kPhase[2][j] == TP4::kPhase[0][TP4::kTaps - 1 - j] ? 0 : 1)
                 + (TP4::kPhase[1][j] == TP4::kPhase[1][TP4::kTaps - 1 - j] ? 0 : 1);
        P.le("truepeak.coeffs.dc_err", dcErr, 1e-6);
        P.eq("truepeak.coeffs.symmetry_mismatches", sym, 0);

        TP4::Coeffs tc{};
        TP4::design(tc, EngineParams{}, StageCtx{ kFs, kFs, 1, {} });
        for (const double rel : { 0.05, 0.15, 0.25, 0.35, 0.40, 0.4167 })
        {
            double worst = 0.0;
            for (int ph = 0; ph < 16; ++ph)
            {
                TP4::State ts{};
                TP4::seed(ts, simd::set1(-240.0f));
                const double phase0 = ph / 16.0;
                for (int n = 0; n < 400; ++n)
                {
                    const double x = 0.5 * sig::sinTurns(rel * n + phase0);
                    const float lvl = laneOf(TP4::tick(tc, ts, simd::set1(static_cast<float>(x))), 0);
                    const int m = n - TP4::kDelay;
                    if (m < TP4::kTaps + 2)
                        continue;
                    double tru = 0.0;
                    for (int q = 0; q < 4; ++q)
                        tru = std::max(tru, std::fabs(0.5 * sig::sinTurns(rel * (m + q / 4.0) + phase0)));
                    worst = std::max(worst, std::fabs(static_cast<double>(lvl) - dbOf(tru)));
                }
            }
            char key[64];
            std::snprintf(key, sizeof key, "truepeak.accuracy.f%04ld.max_err_db", std::lround(rel * 10000.0));
            P.le(key, worst, 0.02);
        }

        TP4::State ns{};
        TP4::seed(ns, simd::set1(-240.0f));
        sig::Pcg32 rng(0x74703478, 5);
        std::vector<float> xs;
        std::int64_t below = 0;
        for (int n = 0; n < 20000; ++n)
        {
            const float x = rng.bipolar();
            xs.push_back(x);
            const float lvl = laneOf(TP4::tick(tc, ns, simd::set1(x)), 0);
            if (n >= TP4::kDelay)
            {
                const float s = std::fabs(xs[static_cast<std::size_t>(n - TP4::kDelay)]);
                below += lvl < laneOf(dbFromLin(simd::set1(s)), 0) ? 1 : 0;
            }
        }
        P.eq("truepeak.phase0.violations", below, 0);
        TP4::State ds{};
        TP4::seed(ds, simd::set1(-240.0f));
        float dcLvl = 0.0f;
        for (int n = 0; n < 100; ++n)
            dcLvl = laneOf(TP4::tick(tc, ds, simd::set1(0.3f)), 0);
        P.eq("truepeak.phase0.dc", dcLvl == laneOf(dbFromLin(simd::set1(0.3f)), 0) ? 1 : 0, 1);

        TP4::State fs4{};
        TP4::seed(fs4, simd::set1(-240.0f));
        float fsLvl = -300.0f;
        for (int n = 0; n < 48000; ++n)                     // 1 s: the onset's ringing has left the 1 s holds
        {
            const float lvl = laneOf(TP4::tick(tc, fs4, simd::set1(sig::sineAt(n, 12000.0, kFs, 1.0, 0.125))), 0);
            if (n > 100)
                fsLvl = std::max(fsLvl, lvl);
        }
        P.near("truepeak.fs4_db", static_cast<double>(fsLvl), 0.0, 0.02);
        P.near("truepeak.over_db", static_cast<double>(laneOf(TP4::overDb(fs4), 0)), 3.0103, 0.02);

        TP4::State is{};
        TP4::seed(is, simd::set1(-240.0f));
        int arrives = -1;
        for (int n = 0; n < 60; ++n)
        {
            const float lvl = laneOf(TP4::tick(tc, is, simd::set1(n == 5 ? 1.0f : 0.0f)), 0);
            if (arrives < 0 && lvl == 0.0f)
                arrives = n - 5;
        }
        P.eq("truepeak.delay_samples", arrives, TP4::kDelay);

        TP4::State ss{};
        TP4::seed(ss, simd::set1(-6.0f));
        const float a6 = laneOf(linFromDb(simd::set1(-6.0f)), 0);
        double seedErr = std::fabs(static_cast<double>(laneOf(TP4::levelDb(ss), 0)) + 6.0);
        for (int n = 0; n < 40; ++n)
            seedErr = std::max(seedErr,
                               std::fabs(static_cast<double>(laneOf(TP4::tick(tc, ss, simd::set1(a6)), 0)) + 6.0));
        P.le("truepeak.seed_err_db", seedErr, 1e-5);

        // the engine's SC delay through the registered entry
        for (const int det : { 1, 0 })
        {
            alignas(64) std::array<std::byte, kArenaBytes> arena{};
            std::vector<float> scratch(hostScratch(), 0.0f);
            IEngine* eng = bw.construct(arena.data());
            eng->prepare(PrepareInfo{ kFs, 1, std::span<float>(scratch) });
            eng->setParams(fcmp::probe::resolveRaw(bw, bwRaw(bw, LookaheadBudget::ms20, det, 0)).eng);
            eng->snapParams();
            P.eq(det == 1 ? "truepeak.scdelay.tp" : "truepeak.scdelay.peak", eng->scDelaySamples(),
                 det == 1 ? TP4::kDelay : 0);
            eng->~IEngine();
        }
    }

    void loudClipRows(Probe& P)
    {
        using LC = stage::LoudClip;
        EngineParams e{};
        e.thrDb = kThr;
        const StageCtx ctx{ kFs, kFs, 1, {} };
        LC::Coeffs c{};
        LC::design(c, e, ctx);
        const float k = c.drive.k, bound = 1.0f / k;

        LC::State s{};
        sig::Pcg32 rng(0x6c6f7564, 6);
        std::vector<float> x(48000);
        for (float& v : x)
            v = rng.bipolar() * bound * 1.5f * 31.6f;                   // up to +30 dB over the corner
        std::vector<float> y = x;
        LC::process(c, s, y.data(), nullptr, static_cast<int>(y.size()), 0);
        std::int64_t over = 0;
        for (const float v : y)
            over += std::fabs(v) > bound ? 1 : 0;
        P.eq("loudclip.bound.violations", over, 0);

        LC::State ss{};
        std::vector<float> small(4800);
        for (std::size_t i = 0; i < small.size(); ++i)
            small[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs,
                                   measure::amplitudeFromDb(static_cast<double>(kThr) - 40.0));
        std::vector<float> ys = small;
        LC::process(c, ss, ys.data(), nullptr, static_cast<int>(ys.size()), 0);
        const measure::SingleBin bin(1000.0, kFs, 4800);
        P.le("loudclip.small.gain_err_db", std::fabs(bin.gainDb(small, ys, 0)), 1e-3);

        double cornerErr = 0.0, transferErr = 0.0;
        for (const float level : { 0.2f, 0.5f, 1.0f, 1.5f, 3.0f })
        {
            LC::State cs{};
            std::vector<float> dc(64, level / k);
            LC::process(c, cs, dc.data(), nullptr, 64, 0);
            const auto want = static_cast<double>(LC::transfer(c, level / k, 0.0f));
            transferErr = std::max(transferErr, std::fabs(static_cast<double>(dc[63]) - want) / std::fabs(want));
            if (level == 1.5f)
                cornerErr = std::fabs(static_cast<double>(dc[63]) - static_cast<double>(bound)) / static_cast<double>(bound);
        }
        P.le("loudclip.corner.err", cornerErr, 1e-6);
        P.le("loudclip.transfer.max_err", transferErr, 1e-6);

        LC::State zs{};
        std::vector<float> zero(256, 0.0f);
        LC::process(c, zs, zero.data(), nullptr, 256, 0);
        std::int64_t nz = 0;
        for (const float v : zero)
            nz += v != 0.0f ? 1 : 0;
        P.eq("loudclip.silence.nonzero", nz, 0);

        LC::State a{}, b{};
        std::vector<float> one(x.begin(), x.begin() + 4096);
        std::vector<float> split = one;
        LC::process(c, a, one.data(), nullptr, 4096, 0);
        int off = 0, step = 1;
        while (off < 4096)
        {
            const int m = std::min(step, 4096 - off);
            LC::process(c, b, split.data() + off, nullptr, m, 0);
            off += m;
            step = step * 2 + 1;
        }
        std::int64_t bsMismatch = 0;
        for (std::size_t i = 0; i < one.size(); ++i)
            bsMismatch += one[i] == split[i] ? 0 : 1;
        P.eq("loudclip.bs.mismatches", bsMismatch, 0);

        LC::Coeffs moved = c;
        e.thrDb = kThr + 6.0f;
        int designs = 0;
        while (moved.levelDb != e.thrDb && designs < 10000)
        {
            LC::design(moved, e, ctx);
            ++designs;
        }
        std::printf("NOTE     loudclip.level: a 6 dB move lands in %d ticks (%.3g ms)\n", designs,
                    designs * static_cast<double>(kTickSamples) * 1000.0 / static_cast<double>(kFs));
        P.eq("loudclip.level.lands", moved.levelDb == e.thrDb && designs > 1 ? 1 : 0, 1);
        P.near("loudclip.headroom_err_db", static_cast<double>(LC::kHeadroomDb), 20.0 * std::log10(4.0 / 3.0) - 0.25,
               1e-6);
    }

    void ceilingRows(Probe& P, const ModeEntry& bw, const RefTp& ref, const Stereo (&progs)[2])
    {
        const char* progName[2] = { "stress", "music" };
        const auto from = [](const Render& r) { return static_cast<std::size_t>(r.latency) + 64; };
        for (int pi = 0; pi < 2; ++pi)
        {
            const std::string pn = progName[pi];
            const Stereo& x = progs[pi];
            Render ecoClean;
            for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
            {
                const std::string qn = qualityName(q);
                const HostConfig cfg = hostConfig(q, LookaheadBudget::ms20);
                Render clean = render(cfg, blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0)), x);
                const double over = tpDb(ref, clean.y, from(clean)) - static_cast<double>(kCeiling);
                std::printf("NOTE     bw.%s.clean.%s: true peak %+.4f dB over the ceiling, sample peak %+.4f dB, GR up to "
                            "%.3g dB\n",
                            qn.c_str(), pn.c_str(), over,
                            samplePeakDb(clean.y, from(clean)) - static_cast<double>(kCeiling), maxGr(clean));
                P.ge("bw." + qn + ".clean." + pn + ".gr_active_db", maxGr(clean), 10.0);
                if (q != Quality::std)
                    P.le("bw." + qn + ".clean." + pn + ".tp_over_db", over, 0.1);
                else
                {
                    if (pi == 1)
                        P.le("bw.std.clean.music.tp_over_db", over, 1.0);
                    // the STD oversampler's own round trip of the ECO render (the same GR), aligned with the STD render
                    // by its latency (the ECO render's latency is the budget alone)
                    const double rtTp = tpDb(ref, stdRoundTrip(ecoClean.y), from(clean)) - static_cast<double>(kCeiling);
                    const double gainRt = tpDb(ref, stdRoundTrip(x), 64 + kStdLatency) - tpDb(ref, x, 64);
                    std::printf("NOTE     bw.std.clean.%s: the STD round trip of the ECO render reads %+.4f dB over the "
                                "ceiling; the round trip alone raises the input's true peak by %+.4f dB (the IIR's "
                                "phase dispersion, not the limiter)\n",
                                pn.c_str(), rtTp, gainRt);
                    P.le("bw.std.clean." + pn + ".excess_db", over - rtTp, 0.1);
                }
                if (q == Quality::eco)
                    ecoClean = std::move(clean);
            }

            // DETECT PEAK at ECO: the sample peaks are exact
            const Render peak = render(hostConfig(Quality::eco, LookaheadBudget::ms20),
                                       blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 0, 0)), x);
            P.le("bw.eco.peak." + pn + ".sample_over_db",
                 samplePeakDb(peak.y, from(peak)) - static_cast<double>(kCeiling), 0.001);

            // VOICE LOUD
            for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
            {
                const std::string qn = qualityName(q);
                const HostConfig cfg = hostConfig(q, LookaheadBudget::ms20);
                const Render loud = render(cfg, blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 1)), x);
                const double over = tpDb(ref, loud.y, from(loud)) - static_cast<double>(kCeiling);
                const double sp = samplePeakDb(loud.y, from(loud)) - static_cast<double>(kCeiling);
                std::printf("NOTE     bw.%s.loud.%s: true peak %+.4f dB over the ceiling, sample peak %+.4f dB\n",
                            qn.c_str(), pn.c_str(), over, sp);
                if (q == Quality::hq)
                    P.le("bw.hq.loud." + pn + ".tp_over_db", over, 0.1);
                if (q == Quality::eco)
                    P.le("bw.eco.loud." + pn + ".sample_over_db", sp, -1.0);
                if (q == Quality::hq && pi == 1)
                {
                    const Render cleanHq = render(cfg, blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0)), x);
                    std::int64_t mismatch = 0;
                    for (std::size_t i = 0; i < loud.gr.size(); ++i)
                        mismatch += laneOf(loud.gr[i], 0) == laneOf(cleanHq.gr[i], 0)
                                            && laneOf(loud.gr[i], 1) == laneOf(cleanHq.gr[i], 1)
                                        ? 0
                                        : 1;
                    P.eq("bw.loud.gr_mismatches", mismatch, 0);
                    const double louder = rmsDb(loud.y, from(loud)) - rmsDb(cleanHq.y, from(cleanHq));
                    std::printf("NOTE     bw.loud.louder_db = %.3f dB (RMS, music, HQ)\n", louder);
                    P.ge("bw.loud.louder_db", louder, 0.5);
                }
            }
        }
    }

    // `look` automated at block rate while limiting (K2 #21d)
    void lookRows(Probe& P, const ModeEntry& bw, const RefTp& ref, const Stereo& x)
    {
        auto host = std::make_unique<EngineHost>();
        host->configure(hostConfig(Quality::hq, LookaheadBudget::ms20),
                        blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0, 1.0f)));
        host->setUiAttached(true);
        const int latency0 = host->latencySamples();
        std::int64_t latencyChanges = 0;
        double maxStep = 0.0;
        float prevLook = -1.0f, lastLook = 0.0f;
        BlockParams bps[3] = { blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0, 1.0f)),
                               blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0, 10.0f)),
                               blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0, 2.0f)) };
        const Stereo y = drive(
            *host, x, 16,
            [&](std::size_t off) {
                const double t = static_cast<double>(off) / static_cast<double>(kFs);
                return bps[t < 1.0 ? 0 : (t < 3.0 ? 1 : 2)];
            },
            [&](std::size_t, const UiFrame& f) {
                lastLook = f.internals[1];
                if (prevLook >= 0.0f)
                    maxStep = std::max(maxStep, std::fabs(static_cast<double>(lastLook - prevLook))
                                                    * static_cast<double>(kFs) / 1000.0);
                prevLook = lastLook;
                latencyChanges += host->latencySamples() == latency0 ? 0 : 1;
            });
        host->setUiAttached(false);
        const double over = tpDb(ref, y, static_cast<std::size_t>(latency0) + 64) - static_cast<double>(kCeiling);
        std::printf("NOTE     bw.look.auto: HQ, look 1 -> 10 -> 2 ms: true peak %+.4f dB over the ceiling; LOOK EFF steps "
                    "<= %.3g samples per 16; ends at %.4g ms\n",
                    over, maxStep, static_cast<double>(lastLook));
        P.le("bw.look.auto.tp_over_db", over, 0.25);
        P.le("bw.look.auto.max_step_samples", maxStep, 1.0 + 1e-3);
        P.eq("bw.look.auto.lands", std::fabs(static_cast<double>(lastLook) - 2.0) < 1e-3 ? 1 : 0, 1);
        P.eq("bw.look.auto.latency_changes", latencyChanges, 0);
    }

    void internalRows(Probe& P, const ModeEntry& bw, double amp)
    {
        const auto frameAfter = [&](const BlockParams& bp, const Stereo& x) {
            auto host = std::make_unique<EngineHost>();
            host->configure(hostConfig(Quality::eco, LookaheadBudget::ms20), bp);
            host->setUiAttached(true);
            UiFrame last{};
            (void) drive(
                *host, x, 512, [&](std::size_t) { return bp; }, [&](std::size_t, const UiFrame& f) { last = f; });
            host->setUiAttached(false);
            return last;
        };
        Stereo sq, fs4;
        const auto a6 = static_cast<float>(measure::amplitudeFromDb(static_cast<double>(kThr) + 6.0));
        for (std::size_t i = 0; i < 48128; ++i)
        {
            const float v = (i / 24) % 2 == 0 ? a6 : -a6;
            sq.l.push_back(v);
            sq.r.push_back(v);
            const float w = sig::sineAt(static_cast<std::int64_t>(i), 12000.0, kFs, amp, 0.125);
            fs4.l.push_back(w);
            fs4.r.push_back(w);
        }
        const UiFrame fp = frameAfter(blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 0, 0)), sq);
        std::printf("NOTE     bw.internals: HELD PEAK %.6g, LOOK EFF %.6g, TP OVER %.6g (PEAK, a 1 kHz square at T + 6)\n",
                    static_cast<double>(fp.internals[0]), static_cast<double>(fp.internals[1]),
                    static_cast<double>(fp.internals[2]));
        P.near("bw.internals.held_peak_db", static_cast<double>(fp.internals[0]), static_cast<double>(kThr) + 6.0, 0.01);
        P.near("bw.internals.look_eff_ms", static_cast<double>(fp.internals[1]), 5.0, 1000.0 / static_cast<double>(kFs));
        P.eq("bw.internals.tp_over_peak_zero", fp.internals[2] == 0.0f ? 1 : 0, 1);
        const UiFrame ft = frameAfter(blockOf(bw, bwRaw(bw, LookaheadBudget::ms20, 1, 0)), fs4);
        std::printf("NOTE     bw.internals: TP OVER %.6g (TP, 12 kHz at 45 degrees)\n", static_cast<double>(ft.internals[2]));
        P.near("bw.internals.tp_over_db", static_cast<double>(ft.internals[2]), 3.0103, 0.05);
    }

    // A true-peak soak (the boxes' exact re-sum with DETECT TP; dsp.null's 10-minute row runs the defaults).
    void soakRows(Probe& P, const ModeEntry& bw, const Stereo& music)
    {
        const BlockParams bp = blockOf(bw, bwRaw(bw, LookaheadBudget::ms5, 1, 0, 4.0f));
        auto host = std::make_unique<EngineHost>();
        host->configure(hostConfig(Quality::std, LookaheadBudget::ms5), bp);
        const auto loud = static_cast<std::size_t>(2.0f * kFs);
        const std::size_t total = loud + static_cast<std::size_t>(60.0f * kFs);
        const double quiet = measure::amplitudeFromDb(static_cast<double>(kThr) - 20.0);
        std::vector<float> il(4096), ir(4096), ol(4096), orr(4096);
        std::vector<simd::f32x4> gr(4096);
        TestTap tap;
        tap.grDb = gr;
        std::int64_t nonzero = 0;
        for (std::size_t off = 0; off < total; off += 4096)
        {
            const std::size_t m = std::min<std::size_t>(4096, total - off);
            for (std::size_t k = 0; k < m; ++k)
            {
                const std::size_t i = off + k;
                il[k] = i < loud ? music.l[i] : sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, quiet);
                ir[k] = i < loud ? music.r[i] : sig::sineAt(static_cast<std::int64_t>(i), 330.0, kFs, quiet);
            }
            tap.firstSample = off;
            tap.written = 0;
            host->setTap(&tap);
            const float* ins[2] = { il.data(), ir.data() };
            float* outs[2] = { ol.data(), orr.data() };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(m);
            host->process(io, bp);
            for (std::size_t k = 0; k < m; ++k)
                if (off + k >= loud + static_cast<std::size_t>(3.0f * kFs))
                    nonzero += laneOf(gr[k], 0) != 0.0f || laneOf(gr[k], 1) != 0.0f ? 1 : 0;
        }
        host->setTap(nullptr);
        P.eq("bw.soak.tp.gr_nonzero", nonzero, 0);
    }
} // namespace

FCMP_PROBE(dsp, slidingmax)
{
    const fcdsp::ScopedFtz ftz;                         // the host opens its own; the policy rows run inside this one
    const ModeEntry& bw = fcmp::probe::modeEntry("brickwall");
    boxRows(P);
    truePeakRows(P, bw);
    loudClipRows(P);

    const RefTp ref;
    const double amp = measure::amplitudeFromDb(static_cast<double>(kThr) + 18.0);
    const Stereo progs[2] = { stressProgram(amp), musicProgram(amp) };
    ceilingRows(P, bw, ref, progs);
    lookRows(P, bw, ref, progs[1]);
    internalRows(P, bw, amp);
    soakRows(P, bw, progs[1]);

    // the budget OFF (zero latency): overshoot is allowed (02 §6.6); printed only
    for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
    {
        const Render r = render(hostConfig(q, LookaheadBudget::off),
                                blockOf(bw, bwRaw(bw, LookaheadBudget::off, 1, 0)), progs[1]);
        const auto from = static_cast<std::size_t>(r.latency) + 64;
        std::printf("NOTE     bw.budgetoff.%s: music, DETECT TP, zero latency: true peak %+.4f dB, sample peak %+.4f dB "
                    "over the ceiling\n",
                    qualityName(q), tpDb(ref, r.y, from) - static_cast<double>(kCeiling),
                    samplePeakDb(r.y, from) - static_cast<double>(kCeiling));
    }
    return P.finish();
}
