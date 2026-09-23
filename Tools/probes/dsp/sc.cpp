// FCMP_PROBE layer=dsp name=sc scope=global timeout=120
//
// dsp.sc (F5, S4; 03 §3.4 "dsp.sc"; E §8, §9.2 row 8; K2 #14): the host side-chain filters of engine/host/ScFilter.h,
// the 2-pole Butterworth TPT SVF high-pass and the six-section `sce` tilt, measured through the real component: an
// impulse runs through ScFilter::process (per-sample, coefficients designed on control ticks, all four lanes), and the
// magnitude of its impulse response (65536 samples, double-precision Goertzel with Signals.h's deterministic cosine) is
// read at 161 log-spaced frequencies from 20 Hz to 20 kHz, or at any frequency a row needs.
//
// Rows (spec unless noted):
//   HPF, fc in {20, 50, 80, 100, 200, 350, 500} Hz:
//     hpf.fs<fs>.fc<fc>.fc_err_pct   the measured -3.0103 dB point against fc, at 44.1, 48, 96 and 192 kHz: <= 1 %
//     hpf.fc<fc>.impl_max_err_db     48 kHz, the measured response against the design's exact transfer function (the
//                                    bilinear Butterworth with the prewarped cutoff, double): <= 0.01 dB
//     hpf.fc<fc>.analysis_max_err_db ScFilter::responseDb (what analysis::scResponse draws) against the measurement
//     hpf.off.mismatches             OFF (0, 19.99 Hz, NaN) is an exact bypass: output bits == input bits, four lanes
//     (NOTE) the deviation from the analog Butterworth, the bilinear warp near Nyquist
//   Tilt, sigma in {-6, -3.01, -1.5, +1.5, +3.01, +6} dB/oct (48 kHz):
//     tilt.s<sigma>.impl_max_err_db      the measured response against the design (six prewarped first-order sections,
//                                        E §8's placement, pivot-normalised): <= 0.01 dB
//     tilt.s<sigma>.analysis_max_err_db  ScFilter::responseDb against the measurement: <= 0.01 dB
//     tilt.s<sigma>.pivot_db             |H(1 kHz)|: 0 dB within 0.01
//     tilt.s<sigma>.ripple_db            the measured response against the target line sigma log2(f / 1 kHz) over the
//                                        core band kRippleLoHz-kRippleHiHz: <= 0.1 dB (03 §3.4 "tilt ripple <= 0.1 dB";
//                                        the band is where six sections at two per decade hold E §8's +/- 0.1 dB)
//     tilt.s<sigma>.mirror_max_db        |dB(sigma) + dB(-sigma)| over the 161 points: <= 0.01 (poles and zeros swap)
//     (NOTE) the deviation from the line over 40 Hz-10 kHz and over 20 Hz-20 kHz, at 44.1, 48 and 96 kHz
//     tilt.off.mismatches                sigma = 0 (and NaN) is an exact bypass, bit for bit
//     tilt.engage.<case>.overshoot_db    a running 100 Hz tone while the target moves (0 -> +6, 0 -> -6, +6 -> -6): the
//                                        output's peak over the move never exceeds the larger of the settled peaks
//                                        before and after by more than 1 dB (the slewed sigma, ScFilter.h); and
//                                        tilt.engage.snap_control_db: the same 0 -> +6 applied from rest at full sigma
//                                        (a snap with cleared states) must read >= +10 dB, so the metric sees a burst
//     tilt.engage.slew_ms                the slewed sigma lands on the target 20 ms after the move (within a tick)
//   Combined and structural:
//     sc.combined.impl_max_err_db        HPF 80 Hz + tilt +3.01: the product of the two designs, <= 0.01 dB
//     sc.lanes.mismatches                four lanes of the same input give four identical outputs, bit for bit
//     sc.blocksize.mismatches            noise with the targets moving at sample 69632 (a multiple of every block
//                                        size) and a slewing tilt, at block sizes {1, 17, 64, 128, 512, 4096}:
//                                        bit-identical to block size 64 (coefficients change on ABSOLUTE ticks)
//     sc.reset.nonzero                   after reset(), silence in gives exactly 0 out
//     sc.finite                          finite() on the filters of this run
//     sc.allocs                          allocations inside prepare/setTarget/snap/process/reset: 0
//   Golden (abs:0.01): sc.<cfg>.f<hz>.db, the measured magnitude at 16 frequencies for five configurations.
#include "ProbeRegistry.h"

#include "Measure.h"
#include "Signals.h"

#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/host/ScFilter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kIrLength = 65536;
    constexpr int kPoints = 161;
    constexpr double kImplTolDb = 0.01;
    constexpr double kRippleTolDb = 0.1;
    constexpr double kRippleLoHz = 200.0;           // the tilt's core band (see the header comment)
    constexpr double kRippleHiHz = 2500.0;
    constexpr double kFcTolPct = 1.0;
    constexpr double kLog2Spread = 0.13794007834502475;   // log2(sqrt 10) / (2 * 20 log10 2), ScFilter.h

    std::string label(double v)                     // key-safe: 3.01 -> 3p01, -6 -> m6
    {
        char b[32];
        std::snprintf(b, sizeof b, "%g", std::fabs(v));
        std::string s(b);
        std::replace(s.begin(), s.end(), '.', 'p');
        return (v < 0.0 ? "m" : "") + s;
    }

    std::vector<double> logFrequencies()            // 20 Hz ... 20 kHz, 161 points
    {
        std::vector<double> f(kPoints);
        const double span = sig::logDet(1000.0);
        for (int k = 0; k < kPoints; ++k)
            f[static_cast<std::size_t>(k)] = 20.0 * sig::expDet(span * static_cast<double>(k) / (kPoints - 1));
        return f;
    }

    // ---- the measurement ----------------------------------------------------------------------------------------------

    struct Ir
    {
        std::vector<float> h;                       // lane 0
        std::int64_t laneMismatches = 0;            // lanes 1-3 against lane 0
    };

    // The impulse response of a settled ScFilter (prepare, setTarget, snap), all four lanes fed the same impulse.
    Ir impulseResponse(double fs, float hpfHz, float tilt, std::size_t n = kIrLength)
    {
        host::ScFilter f;
        f.prepare(static_cast<float>(fs));
        f.setTarget(hpfHz, tilt);
        f.snap();
        Ir ir;
        ir.h.resize(n);
        std::array<simd::f32x4, 64> buf{};
        for (std::size_t off = 0; off < n; off += buf.size())
        {
            const std::size_t m = std::min(buf.size(), n - off);
            for (std::size_t i = 0; i < m; ++i)
                buf[i] = simd::set1(off + i == 0 ? 1.0f : 0.0f);
            f.process(buf.data(), buf.data(), static_cast<int>(m), off);
            for (std::size_t i = 0; i < m; ++i)
            {
                alignas(16) float v[4];
                simd::store(v, buf[i]);
                ir.h[off + i] = v[0];
                for (int ln = 1; ln < 4; ++ln)
                    ir.laneMismatches += std::bit_cast<std::uint32_t>(v[ln]) != std::bit_cast<std::uint32_t>(v[0]) ? 1 : 0;
            }
        }
        return ir;
    }

    // |H(hz)| in dB from an impulse response: Goertzel in double (Signals.h's cosine; deterministic).
    double magDb(const std::vector<float>& h, double hz, double fs)
    {
        const double turns = hz / fs;
        const double c = sig::cosTurns(turns), s = sig::sinTurns(turns), k = 2.0 * c;
        double s1 = 0.0, s2 = 0.0;
        for (const float v : h)
        {
            const double s0 = static_cast<double>(v) + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double re = s1 - c * s2, im = s * s2;
        return measure::dbFromAmplitude(std::sqrt(re * re + im * im));
    }

    // ---- the design, independently in double (the "design target" of 03 §3.4) ------------------------------------------

    double warp(double hz, double fs)
    {
        const double x = std::clamp(hz / fs, 0.0, static_cast<double>(host::kScMaxCornerX));
        return std::tan(3.14159265358979323846 * x);
    }

    double hpfPower(double hz, double fs, double fc)
    {
        if (!(fc >= static_cast<double>(host::kScHpfOffHz)))
            return 1.0;
        const double w = std::tan(3.14159265358979323846 * hz / fs), g = warp(fc, fs);
        const double e = g * g - w * w, kgw = static_cast<double>(host::kScSvfK) * g * w;
        return w * w * w * w / (e * e + kgw * kgw);
    }

    double tiltPowerRaw(double hz, double fs, double sigma)
    {
        const double s = std::exp2(sigma * kLog2Spread);
        const double w = std::tan(3.14159265358979323846 * hz / fs);
        double p = 1.0;
        for (const float centre : host::kTiltCentreHz)
        {
            const double gz = warp(static_cast<double>(centre) / s, fs), gp = warp(static_cast<double>(centre) * s, fs);
            p *= (w * w + gz * gz) / (w * w + gp * gp);
        }
        return p;
    }

    double designDb(double hz, double fs, double fc, double sigma)
    {
        double p = hpfPower(hz, fs, fc);
        if (sigma != 0.0)
            p *= tiltPowerRaw(hz, fs, sigma) / tiltPowerRaw(static_cast<double>(host::kTiltPivotHz), fs, sigma);
        return 10.0 * std::log10(p);
    }

    // ---- helpers ------------------------------------------------------------------------------------------------------

    // The -3.0103 dB point of a measured high-pass, by bisection on log f over [fc/2, 2 fc].
    double minus3Hz(const std::vector<float>& h, double fs, double fc)
    {
        const double target = -3.0102999566398120;
        double lo = std::log(0.5 * fc), hi = std::log(2.0 * fc);
        for (int i = 0; i < 48; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (magDb(h, std::exp(mid), fs) < target ? lo : hi) = mid;
        }
        return std::exp(0.5 * (lo + hi));
    }

    // Bits of `a` against `b` over four lanes of every sample.
    std::int64_t mismatches(const std::vector<simd::f32x4>& a, const std::vector<simd::f32x4>& b)
    {
        std::int64_t n = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            alignas(16) float x[4], y[4];
            simd::store(x, a[i]);
            simd::store(y, b[i]);
            for (int ln = 0; ln < 4; ++ln)
                n += std::bit_cast<std::uint32_t>(x[ln]) != std::bit_cast<std::uint32_t>(y[ln]) ? 1 : 0;
        }
        return n;
    }

    std::vector<simd::f32x4> noise4(std::size_t n, std::uint64_t seed)
    {
        sig::Pcg32 rng(seed, 7);
        std::vector<simd::f32x4> v(n);
        for (simd::f32x4& x : v)
        {
            alignas(16) const float s[4] = { 0.5f * rng.bipolar(), 0.5f * rng.bipolar(), 0.5f * rng.bipolar(),
                                             0.5f * rng.bipolar() };
            x = simd::load(s);
        }
        return v;
    }

    // Runs `in` through a filter in blocks of `block`, calling `schedule(filter, absoluteIndex)` at each block start.
    template <class Schedule>
    std::vector<simd::f32x4> run(const std::vector<simd::f32x4>& in, int block, Schedule&& schedule)
    {
        host::ScFilter f;
        f.prepare(static_cast<float>(kFs));
        std::vector<simd::f32x4> out(in.size());
        for (std::size_t off = 0; off < in.size();)
        {
            const std::size_t m = std::min(static_cast<std::size_t>(block), in.size() - off);
            schedule(f, off);
            f.process(in.data() + off, out.data() + off, static_cast<int>(m), off);
            off += m;
        }
        return out;
    }

    // Lane 0 of a 100 Hz tone through a settled filter at `from`, whose target becomes `to` at sample `edge` (slewed),
    // or which is snapped to `to` from rest at `edge` when `snapFromRest`.
    std::vector<float> engage(float from, float to, std::size_t edge, std::size_t n, bool snapFromRest)
    {
        host::ScFilter f;
        f.prepare(static_cast<float>(kFs));
        f.setTarget(0.0f, from);
        f.snap();
        std::vector<float> out(n);
        std::array<simd::f32x4, 64> buf{};
        for (std::size_t off = 0; off < n; off += buf.size())
        {
            if (off == edge)
            {
                f.setTarget(0.0f, to);
                if (snapFromRest)
                {
                    f.reset();
                    f.snap();
                }
            }
            for (std::size_t i = 0; i < buf.size(); ++i)
                buf[i] = simd::set1(sig::sineAt(static_cast<std::int64_t>(off + i), 100.0, kFs, 0.5));
            f.process(buf.data(), buf.data(), static_cast<int>(buf.size()), off);
            for (std::size_t i = 0; i < buf.size(); ++i)
                out[off + i] = simd::lane<0>(buf[i]);
        }
        return out;
    }

    double peakDb(const std::vector<float>& x, std::size_t a, std::size_t b)
    {
        double p = 0.0;
        for (std::size_t i = a; i < b; ++i)
            p = std::max(p, static_cast<double>(std::fabs(x[i])));
        return measure::dbFromAmplitude(p);
    }
} // namespace

FCMP_PROBE(dsp, sc)
{
    const std::vector<double> freqs = logFrequencies();
    std::int64_t laneMismatches = 0;

    // ---- HPF ----------------------------------------------------------------------------------------------------------
    const double cutoffs[] = { 20.0, 50.0, 80.0, 100.0, 200.0, 350.0, 500.0 };
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const double fc : cutoffs)
        {
            const Ir ir = impulseResponse(fs, static_cast<float>(fc), 0.0f);
            laneMismatches += ir.laneMismatches;
            const double f3 = minus3Hz(ir.h, fs, fc);
            P.le("hpf.fs" + label(fs / 1000.0) + ".fc" + label(fc) + ".fc_err_pct", 100.0 * std::fabs(f3 / fc - 1.0),
                 kFcTolPct);
        }
    for (const double fc : cutoffs)
    {
        const Ir ir = impulseResponse(kFs, static_cast<float>(fc), 0.0f);
        double impl = 0.0, analysis = 0.0, analog = 0.0;
        for (const double f : freqs)
        {
            const double got = magDb(ir.h, f, kFs);
            impl = std::max(impl, std::fabs(got - designDb(f, kFs, fc, 0.0)));
            analysis = std::max(analysis, std::fabs(got - static_cast<double>(host::ScFilter::responseDb(
                                                              static_cast<float>(fc), 0.0f, static_cast<float>(kFs),
                                                              static_cast<float>(f)))));
            const double r = fc / f;
            analog = std::max(analog, std::fabs(got + 10.0 * std::log10(1.0 + r * r * r * r)));
        }
        const std::string k = "hpf.fc" + label(fc);
        P.le(k + ".impl_max_err_db", impl, kImplTolDb);
        P.le(k + ".analysis_max_err_db", analysis, kImplTolDb);
        std::printf("NOTE     %s: 48 kHz, largest deviation from the analog Butterworth over 20 Hz-20 kHz %.4f dB "
                    "(bilinear warp)\n", k.c_str(), analog);
    }
    {
        const std::vector<simd::f32x4> in = noise4(4096, 11);
        std::int64_t bad = 0;
        for (const float hz : { 0.0f, 19.99f, std::numeric_limits<float>::quiet_NaN() })
        {
            const std::vector<simd::f32x4> out = run(in, 64, [hz](host::ScFilter& f, std::size_t off) {
                if (off == 0)
                {
                    f.setTarget(hz, 0.0f);
                    f.snap();
                }
            });
            bad += mismatches(in, out);
        }
        P.eq("hpf.off.mismatches", bad, 0);
    }

    // ---- tilt ---------------------------------------------------------------------------------------------------------
    const double sigmas[] = { -6.0, -3.01, -1.5, 1.5, 3.01, 6.0 };
    for (const double sigma : sigmas)
    {
        const Ir ir = impulseResponse(kFs, 0.0f, static_cast<float>(sigma));
        const Ir mirror = impulseResponse(kFs, 0.0f, static_cast<float>(-sigma));
        laneMismatches += ir.laneMismatches + mirror.laneMismatches;
        double impl = 0.0, analysis = 0.0, ripple = 0.0, mid = 0.0, full = 0.0, mir = 0.0;
        for (const double f : freqs)
        {
            const double got = magDb(ir.h, f, kFs);
            const double line = sigma * std::log2(f / 1000.0);
            impl = std::max(impl, std::fabs(got - designDb(f, kFs, 0.0, sigma)));
            analysis = std::max(analysis, std::fabs(got - static_cast<double>(host::ScFilter::responseDb(
                                                              0.0f, static_cast<float>(sigma), static_cast<float>(kFs),
                                                              static_cast<float>(f)))));
            const double dev = std::fabs(got - line);
            if (f >= kRippleLoHz * 0.9999 && f <= kRippleHiHz * 1.0001)
                ripple = std::max(ripple, dev);
            if (f >= 40.0 * 0.9999 && f <= 10000.0 * 1.0001)
                mid = std::max(mid, dev);
            full = std::max(full, dev);
            mir = std::max(mir, std::fabs(got + magDb(mirror.h, f, kFs)));
        }
        const std::string k = "tilt.s" + label(sigma);
        P.le(k + ".impl_max_err_db", impl, kImplTolDb);
        P.le(k + ".analysis_max_err_db", analysis, kImplTolDb);
        P.le(k + ".pivot_db", std::fabs(magDb(ir.h, 1000.0, kFs)), kImplTolDb);
        P.le(k + ".ripple_db", ripple, kRippleTolDb);
        P.le(k + ".mirror_max_db", mir, kImplTolDb);
        std::printf("NOTE     %s: 48 kHz, deviation from %+g dB/oct: %.4f dB over %g-%g Hz, %.4f dB over 40 Hz-10 kHz, "
                    "%.4f dB over 20 Hz-20 kHz\n", k.c_str(), sigma, ripple, kRippleLoHz, kRippleHiHz, mid, full);
    }
    for (const double fs : { 44100.0, 96000.0 })
        for (const double sigma : { 3.01, 6.0 })
        {
            const Ir ir = impulseResponse(fs, 0.0f, static_cast<float>(sigma));
            double ripple = 0.0, mid = 0.0, full = 0.0, impl = 0.0;
            for (const double f : freqs)
            {
                const double got = magDb(ir.h, f, fs);
                const double dev = std::fabs(got - sigma * std::log2(f / 1000.0));
                impl = std::max(impl, std::fabs(got - designDb(f, fs, 0.0, sigma)));
                if (f >= kRippleLoHz * 0.9999 && f <= kRippleHiHz * 1.0001)
                    ripple = std::max(ripple, dev);
                if (f >= 40.0 * 0.9999 && f <= 10000.0 * 1.0001)
                    mid = std::max(mid, dev);
                full = std::max(full, dev);
            }
            const std::string k = "tilt.fs" + label(fs / 1000.0) + ".s" + label(sigma);
            P.le(k + ".impl_max_err_db", impl, kImplTolDb);
            std::printf("NOTE     %s: deviation from the line %.4f dB over %g-%g Hz, %.4f dB over 40 Hz-10 kHz, "
                        "%.4f dB over 20 Hz-20 kHz\n", k.c_str(), ripple, kRippleLoHz, kRippleHiHz, mid, full);
        }
    {
        const std::vector<simd::f32x4> in = noise4(4096, 12);
        std::int64_t bad = 0;
        for (const float s : { 0.0f, std::numeric_limits<float>::quiet_NaN() })
            bad += mismatches(in, run(in, 64, [s](host::ScFilter& f, std::size_t off) {
                          if (off == 0)
                          {
                              f.setTarget(0.0f, s);
                              f.snap();
                          }
                      }));
        // A tilt that slews back to 0 is an exact bypass again once it lands (and its state is cleared).
        const std::vector<simd::f32x4> out = run(in, 64, [](host::ScFilter& f, std::size_t off) {
            if (off == 0)
            {
                f.setTarget(0.0f, 3.0f);
                f.snap();
            }
            if (off == 1024)
                f.setTarget(0.0f, 0.0f);
        });
        const std::vector<simd::f32x4> tail(in.begin() + 3072, in.end()), outTail(out.begin() + 3072, out.end());
        bad += mismatches(tail, outTail);
        P.eq("tilt.off.mismatches", bad, 0);
    }

    // ---- the slewed engage (ScFilter.h: from rest at full sigma is a burst) -------------------------------------------
    {
        const std::size_t edge = 12032, n = 36096, settle = 4800;           // multiples of the 64-sample blocks
        struct Case { const char* name; float from, to; };
        const Case cases[] = { { "up", 0.0f, 6.0f }, { "down", 0.0f, -6.0f }, { "flip", 6.0f, -6.0f } };
        for (const Case& c : cases)
        {
            const std::vector<float> y = engage(c.from, c.to, edge, n, false);
            const double before = peakDb(y, edge - settle, edge), after = peakDb(y, n - settle, n);
            const double during = peakDb(y, edge, n - settle);
            P.le(std::string("tilt.engage.") + c.name + ".overshoot_db", during - std::max(before, after), 1.0);
        }
        const std::vector<float> slewed = engage(0.0f, 6.0f, edge, n, false);
        const std::vector<float> burst = engage(0.0f, 6.0f, edge, n, true);
        const double settled = peakDb(slewed, n - settle, n);
        P.ge("tilt.engage.snap_control_db", peakDb(burst, edge, edge + settle) - settled, 10.0);

        host::ScFilter f;
        f.prepare(static_cast<float>(kFs));
        f.setTarget(0.0f, 6.0f);
        std::array<simd::f32x4, 16> buf{};
        std::int64_t landed = -1;
        for (std::size_t off = 0; off < 4800 && landed < 0; off += buf.size())
        {
            f.process(buf.data(), buf.data(), static_cast<int>(buf.size()), off);
            if (f.tiltNow() == 6.0f)
                landed = static_cast<std::int64_t>(off);
        }
        // 6 dB/oct at kTiltSlewDbOctPerS takes 20 ms: 60 ticks at 48 kHz, the first at sample 0, so the last step is
        // taken at sample 944 (960 when the float steps leave a last sliver).
        P.in("tilt.engage.slew_ms", 1000.0 * static_cast<double>(landed) / kFs, 19.5, 20.5);
    }

    // ---- combined, structure ------------------------------------------------------------------------------------------
    {
        const Ir ir = impulseResponse(kFs, 80.0f, 3.01f);
        laneMismatches += ir.laneMismatches;
        double impl = 0.0;
        for (const double f : freqs)
            impl = std::max(impl, std::fabs(magDb(ir.h, f, kFs) - designDb(f, kFs, 80.0, 3.01)));
        P.le("sc.combined.impl_max_err_db", impl, kImplTolDb);
    }
    P.eq("sc.lanes.mismatches", laneMismatches, 0);
    {
        const std::size_t n = 69632 * 2;
        const std::vector<simd::f32x4> in = noise4(n, 13);
        const auto schedule = [](host::ScFilter& f, std::size_t off) {
            if (off == 0)
            {
                f.setTarget(120.0f, 0.0f);
                f.snap();
                f.setTarget(120.0f, 4.5f);                  // slews from sample 0 on
            }
            if (off == 69632)
                f.setTarget(40.0f, -2.0f);
        };
        const std::vector<simd::f32x4> ref = run(in, 64, schedule);
        std::int64_t bad = 0;
        for (const int block : { 1, 17, 128, 512, 4096 })
            bad += mismatches(ref, run(in, block, schedule));
        P.eq("sc.blocksize.mismatches", bad, 0);
    }
    {
        host::ScFilter f;
        std::vector<simd::f32x4> buf = noise4(2048, 14);
        std::int64_t allocs = 0;
        {
            const fcmp::probe::alloc::Scope scope;
            f.prepare(static_cast<float>(kFs));
            f.setTarget(100.0f, 2.0f);
            f.snap();
            f.process(buf.data(), buf.data(), static_cast<int>(buf.size()), 0);
            f.reset();
            allocs = static_cast<std::int64_t>(fcmp::probe::alloc::allocations());
        }
        P.eq("sc.allocs", allocs, 0);
        P.eq("sc.finite", f.finite() ? 1 : 0, 1);
        std::vector<simd::f32x4> zeros(1024, simd::set1(0.0f));
        f.process(zeros.data(), zeros.data(), static_cast<int>(zeros.size()), 4096);
        std::int64_t nonzero = 0;
        for (const simd::f32x4& v : zeros)
            for (const float x : { simd::lane<0>(v), simd::lane<1>(v), simd::lane<2>(v), simd::lane<3>(v) })
                nonzero += x != 0.0f ? 1 : 0;
        P.eq("sc.reset.nonzero", nonzero, 0);
    }

    // ---- golden: curve points ------------------------------------------------------------------------------------------
    {
        struct Cfg { const char* name; float hpf, tilt; };
        const Cfg cfgs[] = { { "hpf80", 80.0f, 0.0f }, { "hpf200", 200.0f, 0.0f }, { "tilt6", 0.0f, 6.0f },
                             { "tiltm3p01", 0.0f, -3.01f }, { "hpf80_tilt3p01", 80.0f, 3.01f } };
        const int points[] = { 20, 32, 50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 12500, 20000 };
        for (const Cfg& c : cfgs)
        {
            const Ir ir = impulseResponse(kFs, c.hpf, c.tilt);
            for (const int hz : points)
                P.num(std::string("sc.") + c.name + ".f" + std::to_string(hz) + ".db",
                      magDb(ir.h, static_cast<double>(hz), kFs), Tol::abs(0.01));
        }
    }

    return P.finish();
}
