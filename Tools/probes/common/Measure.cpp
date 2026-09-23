// Tools/probes/common/Measure.cpp: see Measure.h.
#include "Measure.h"

#include "Signals.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fcmp::probe::measure
{
    namespace
    {
        constexpr double kDbPerLn = 8.6858896380650366;         // 20 / ln 10
        constexpr double kLnPerDb = 0.11512925464970229;        // ln 10 / 20
        constexpr double kOneMinusInvE = 0.63212055882855767;   // 1 - 1/e: the 63 % point of a one-pole

        // One RBJ high-pass biquad (Direct Form I, double).
        struct Biquad
        {
            double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

            Biquad(double hz, double fs, double q) noexcept
            {
                const double turns = hz / fs;                           // w0 / (2 pi)
                const double c = sig::cosTurns(turns), s = sig::sinTurns(turns);
                const double alpha = s / (2.0 * q);
                const double a0 = 1.0 + alpha;
                b0 = (1.0 + c) / (2.0 * a0);
                b1 = -(1.0 + c) / a0;
                b2 = b0;
                a1 = -2.0 * c / a0;
                a2 = (1.0 - alpha) / a0;
            }

            double operator()(double x) noexcept
            {
                const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                return y;
            }
        };
    } // namespace

    double dbFromAmplitude(double a) noexcept { return a > 0.0 ? kDbPerLn * sig::logDet(a) : -400.0; }

    double amplitudeFromDb(double db) noexcept { return sig::expDet(db * kLnPerDb); }

    double Bin::amplitude() const noexcept { return std::sqrt(re * re + im * im); }

    SingleBin::SingleBin(double hz, double fs, std::size_t n)
    {
        const double cycles = hz * static_cast<double>(n) / fs;
        if (n == 0 || !(fs > 0.0) || std::fabs(cycles - std::floor(cycles + 0.5)) > 1e-9)
            throw std::invalid_argument("SingleBin: the window must hold an integer number of cycles");
        cos_.resize(n);
        sin_.resize(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double phase = sig::sinePhase(static_cast<std::int64_t>(i), hz, fs);
            cos_[i] = sig::cosTurns(phase);
            sin_[i] = sig::sinTurns(phase);
        }
    }

    Bin SingleBin::operator()(std::span<const float> x, std::int64_t n0) const
    {
        const std::size_t n = cos_.size();
        if (x.size() != n)
            throw std::invalid_argument("SingleBin: window length differs from the table");
        std::size_t k = static_cast<std::size_t>(((n0 % static_cast<std::int64_t>(n)) + static_cast<std::int64_t>(n))
                                                 % static_cast<std::int64_t>(n));
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double v = static_cast<double>(x[i]);
            re += v * cos_[k];
            im -= v * sin_[k];
            if (++k == n)
                k = 0;
        }
        const double scale = 2.0 / static_cast<double>(n);
        return Bin{ re * scale, im * scale };
    }

    double SingleBin::gainDb(std::span<const float> in, std::span<const float> out, std::int64_t n0) const
    {
        return dbFromAmplitude((*this)(out, n0).amplitude()) - dbFromAmplitude((*this)(in, n0).amplitude());
    }

    double crossingSeconds(std::span<const float> trace, double from, double to, double fraction, double fs) noexcept
    {
        const double span = to - from;
        if (span == 0.0 || !(fs > 0.0))
            return -1.0;
        double prevT = 0.0, prevP = 0.0;
        for (std::size_t k = 0; k < trace.size(); ++k)
        {
            const double p = (static_cast<double>(trace[k]) - from) / span;
            const double t = static_cast<double>(k + 1) / fs;
            if (p >= fraction)
                return p == prevP ? t : prevT + (fraction - prevP) / (p - prevP) * (t - prevT);
            prevT = t;
            prevP = p;
        }
        return -1.0;
    }

    double lawSeconds(std::span<const float> trace, double from, double to, double fs, fcdsp::TimeLaw law) noexcept
    {
        const auto at = [&](double fraction) { return crossingSeconds(trace, from, to, fraction, fs); };
        const auto between = [&](double lo, double hi) {
            const double a = at(lo), b = at(hi);
            return a < 0.0 || b < 0.0 ? -1.0 : b - a;
        };
        switch (law)
        {
            case fcdsp::TimeLaw::expDb:
                return at(kOneMinusInvE);
            case fcdsp::TimeLaw::expLin:
            {
                // 63 % of the change in LINEAR gain, found on the dB trace at the equivalent level (both are monotone
                // in each other, so it is the same event).
                const double gFrom = amplitudeFromDb(-from), gTo = amplitudeFromDb(-to);
                const double level = -dbFromAmplitude(gFrom + kOneMinusInvE * (gTo - gFrom));
                return at((level - from) / (to - from));
            }
            case fcdsp::TimeLaw::t10_90:
                return between(0.1, 0.9);
            case fcdsp::TimeLaw::t0_90:
                return at(0.9);
            case fcdsp::TimeLaw::t50:
                return at(0.5);
            case fcdsp::TimeLaw::rateDbPerS:
            {
                const double t = between(0.1, 0.9);
                return t > 0.0 ? 0.8 * std::fabs(to - from) / t : -1.0;
            }
        }
        return -1.0;
    }

    double hfEnergy(std::span<const float> x, std::size_t edge, double fs, double hz, double halfWindowS)
    {
        // 4th-order Butterworth: two sections with Q = 1 / (2 cos(pi/8)) and 1 / (2 cos(3 pi/8)).
        Biquad s1(hz, fs, 0.54119610014619698), s2(hz, fs, 1.3065629648763766);
        const auto half = static_cast<std::size_t>(halfWindowS * fs + 0.5);
        const std::size_t lo = edge > half ? edge - half : 0;
        const std::size_t hi = std::min(x.size(), edge + half + 1);
        double e = 0.0;
        for (std::size_t i = 0; i < hi; ++i)
        {
            const double y = s2(s1(static_cast<double>(x[i])));
            if (i >= lo)
                e += y * y;
        }
        return e;
    }

    double hfRatioDb(std::span<const float> test, std::span<const float> controlA, std::span<const float> controlB,
                     std::size_t edge, double fs)
    {
        const double et = hfEnergy(test, edge, fs);
        const double ec = std::max({ hfEnergy(controlA, edge, fs), hfEnergy(controlB, edge, fs), 1e-300 });
        return 0.5 * dbFromAmplitude(et / ec);
    }
} // namespace fcmp::probe::measure
