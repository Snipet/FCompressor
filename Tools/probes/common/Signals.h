// Tools/probes/common/Signals.h: deterministic test signals for the probes (03 §3.4 "Signals"; C §5.2).
//
// FCompressor's own seeded PCG32 and closed-form signals, never std::*_distribution (whose output is not specified
// across library versions) and never libm: every value is computed from +, -, * and / in double precision plus
// operations the standard defines exactly (floor, fmod, ldexp, frexp), and the probes compile with -ffp-contract=off,
// so a signal is bit-identical on arm64 and x86_64 and across macOS updates. That lets print.* hashes and xarch. rows
// depend on the input. Accuracy: sinTurns/cosTurns/expDet/logDet are within a few ulp of the true values.
//
// Closed form: sample n of a sine is computed from n itself (no recursive oscillator), so a sample never depends on how
// many came before it; a signal is the same whatever the block size or starting offset.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace fcmp::probe::sig
{
    // ---- PCG32 (O'Neill 2014: PCG-XSH-RR 64/32, the pcg32_srandom_r / pcg32_random_r reference) -------------------------
    class Pcg32
    {
    public:
        // pcg32_srandom_r(initstate, initseq). Pcg32(42, 54) yields 0xa15c02b7 0x7b47f409 0xba1d3330 ... (the reference
        // demo's sequence; dsp.selftest checks it).
        constexpr explicit Pcg32(std::uint64_t initState = 0x853c49e6748fea9bull,
                                 std::uint64_t initSeq = 0xda3e39cb94b95bdbull) noexcept
            : inc_((initSeq << 1u) | 1u)
        {
            next();
            state_ += initState;
            next();
        }

        constexpr std::uint32_t next() noexcept
        {
            const std::uint64_t old = state_;
            state_ = old * 6364136223846793005ull + inc_;
            const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
            const auto rot = static_cast<std::uint32_t>(old >> 59u);
            return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
        }

        // Uniform in [0, bound), unbiased (the reference pcg32_boundedrand_r). bound > 0.
        constexpr std::uint32_t bounded(std::uint32_t bound) noexcept
        {
            const std::uint32_t threshold = (0u - bound) % bound;
            for (;;)
                if (const std::uint32_t r = next(); r >= threshold)
                    return r % bound;
        }

        // Uniform floats on a 2^-24 grid: [0, 1) and [-1, 1). Exact conversions, so the same bits everywhere.
        constexpr float uniform() noexcept { return static_cast<float>(next() >> 8u) * 0x1p-24f; }
        constexpr float bipolar() noexcept { return static_cast<float>(next() >> 8u) * 0x1p-23f - 1.0f; }

        // Uniform double in [0, 1) on a 2^-53 grid (two draws).
        constexpr double uniformDouble() noexcept
        {
            const std::uint64_t hi = next() >> 5u, lo = next() >> 6u;           // 27 + 26 bits
            return static_cast<double>((hi << 26u) | lo) * 0x1p-53;
        }

    private:
        std::uint64_t state_ = 0;
        std::uint64_t inc_;
    };

    // ---- deterministic elementary functions (double; no libm) ----------------------------------------------------------
    namespace detail
    {
        // sin(pi/2 * f) and cos(pi/2 * f) for f in [0, 0.5], i.e. an argument in [0, pi/4]: Taylor series to z^17 / z^16,
        // truncation below 1e-16.
        inline double sinQuarter(double f) noexcept
        {
            const double z = f * 1.5707963267948966;            // pi/2
            const double z2 = z * z;
            double p = 2.8114572543455206e-15;                  // 1/17!
            p = p * z2 - 7.6471637318198164e-13;                // 1/15!
            p = p * z2 + 1.6059043836821613e-10;                // 1/13!
            p = p * z2 - 2.5052108385441720e-08;                // 1/11!
            p = p * z2 + 2.7557319223985893e-06;                // 1/9!
            p = p * z2 - 1.9841269841269841e-04;                // 1/7!
            p = p * z2 + 8.3333333333333333e-03;                // 1/5!
            p = p * z2 - 1.6666666666666667e-01;                // 1/3!
            return z + z * z2 * p;
        }

        inline double cosQuarter(double f) noexcept
        {
            const double z = f * 1.5707963267948966;
            const double z2 = z * z;
            double p = 4.7794773323873853e-14;                  // 1/16!
            p = p * z2 - 1.1470745597729725e-11;                // 1/14!
            p = p * z2 + 2.0876756987868099e-09;                // 1/12!
            p = p * z2 - 2.7557319223985891e-07;                // 1/10!
            p = p * z2 + 2.4801587301587302e-05;                // 1/8!
            p = p * z2 - 1.3888888888888889e-03;                // 1/6!
            p = p * z2 + 4.1666666666666667e-02;                // 1/4!
            p = p * z2 - 0.5;                                   // 1/2!
            return 1.0 + z2 * p;
        }

        // sin(pi/2 * f), f in [0, 1]: the octant keeps the series argument <= pi/4.
        inline double sinQuadrant(double f) noexcept { return f <= 0.5 ? sinQuarter(f) : cosQuarter(1.0 - f); }
        inline double cosQuadrant(double f) noexcept { return f <= 0.5 ? cosQuarter(f) : sinQuarter(1.0 - f); }

        inline constexpr double kLn2Hi = 6.93147180369123816490e-01;    // fdlibm's split of ln 2
        inline constexpr double kLn2Lo = 1.90821492927058770002e-10;
    } // namespace detail

    // sin(2 pi t) for any finite t (t in turns). Exact at every multiple of 1/4: sinTurns(0.25) == 1, sinTurns(0.5) == 0.
    inline double sinTurns(double t) noexcept
    {
        const double r = t - std::floor(t);                     // [0, 1), exact
        const double q = r * 4.0;                               // exact (power of two)
        const double k = std::floor(q);
        const double f = q - k;                                 // [0, 1), exact
        switch (static_cast<int>(k))
        {
            case 0:  return detail::sinQuadrant(f);
            case 1:  return detail::cosQuadrant(f);
            case 2:  return -detail::sinQuadrant(f);
            default: return -detail::cosQuadrant(f);
        }
    }

    // cos(2 pi t).
    inline double cosTurns(double t) noexcept { return sinTurns(t + 0.25); }

    // e^x for x in [-700, 700].
    inline double expDet(double x) noexcept
    {
        const double k = std::floor(x * 1.4426950408889634 + 0.5);     // nearest integer of x / ln 2
        const double r = (x - k * detail::kLn2Hi) - k * detail::kLn2Lo; // |r| <= ~0.347
        double p = 1.6059043836821613e-10;                              // 1/13!, Taylor series of e^r, error < 1e-17
        p = p * r + 2.0876756987868099e-09;
        p = p * r + 2.5052108385441720e-08;
        p = p * r + 2.7557319223985891e-07;
        p = p * r + 2.7557319223985893e-06;
        p = p * r + 2.4801587301587302e-05;
        p = p * r + 1.9841269841269841e-04;
        p = p * r + 1.3888888888888889e-03;
        p = p * r + 8.3333333333333333e-03;
        p = p * r + 4.1666666666666667e-02;
        p = p * r + 1.6666666666666667e-01;
        p = p * r + 0.5;
        p = p * r + 1.0;
        p = p * r + 1.0;
        return std::ldexp(p, static_cast<int>(k));
    }

    // ln x for finite x > 0.
    inline double logDet(double x) noexcept
    {
        int e = 0;
        double m = std::frexp(x, &e);                           // x = m * 2^e, m in [0.5, 1), exact
        if (m < 0.70710678118654752)
        {
            m *= 2.0;                                           // exact
            e -= 1;
        }
        const double s = (m - 1.0) / (m + 1.0);                 // |s| <= 0.1716
        const double s2 = s * s;
        double p = 1.0 / 21.0;                                  // ln m = 2 atanh(s) = 2 (s + s^3/3 + ... + s^21/21)
        p = p * s2 + 1.0 / 19.0;
        p = p * s2 + 1.0 / 17.0;
        p = p * s2 + 1.0 / 15.0;
        p = p * s2 + 1.0 / 13.0;
        p = p * s2 + 1.0 / 11.0;
        p = p * s2 + 1.0 / 9.0;
        p = p * s2 + 1.0 / 7.0;
        p = p * s2 + 1.0 / 5.0;
        p = p * s2 + 1.0 / 3.0;
        const double lnm = 2.0 * s + 2.0 * s * s2 * p;
        const double de = static_cast<double>(e);
        return de * detail::kLn2Hi + (lnm + de * detail::kLn2Lo);
    }

    // ---- closed-form signals ---------------------------------------------------------------------------------------------
    // The phase of a sine, in turns, at sample n: hz * n / fs reduced to [0, 1). For integral hz and fs the reduction is
    // exact (fmod is exact) before the one division.
    inline double sinePhase(std::int64_t n, double hz, double fs, double phaseTurns = 0.0) noexcept
    {
        const double cycles = std::fmod(hz * static_cast<double>(n), fs) / fs;
        return cycles + phaseTurns;
    }

    // amp * sin(2 pi (hz n / fs + phaseTurns)).
    inline float sineAt(std::int64_t n, double hz, double fs, double amp = 1.0, double phaseTurns = 0.0) noexcept
    {
        return static_cast<float>(amp * sinTurns(sinePhase(n, hz, fs, phaseTurns)));
    }

    // out[i] = sineAt(n0 + i, ...).
    inline void sine(std::span<float> out, double hz, double fs, double amp = 1.0, double phaseTurns = 0.0,
                     std::int64_t n0 = 0) noexcept
    {
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = sineAt(n0 + static_cast<std::int64_t>(i), hz, fs, amp, phaseTurns);
    }

    // Exponential ("log") sine sweep from f1 to f2 Hz over `samples` samples (Farina 2000):
    //   x(t) = amp * sin(2 pi * f1 T / L * (e^(t L / T) - 1)),  L = ln(f2 / f1),  T = samples / fs.
    // The instantaneous frequency is f1 at t = 0 and f2 at t = T; x(0) = 0.
    class LogSweep
    {
    public:
        LogSweep(double f1, double f2, double fs, std::int64_t samples, double amp = 1.0) noexcept
            : fs_(fs), amp_(amp), rate_(logDet(f2 / f1) / static_cast<double>(samples)),
              turnsScale_(f1 / fs / rate_)
        {
        }

        // The phase in turns at sample n (e^(n L / N) - 1 scaled; reduced to [0, 1) by sinTurns).
        double phase(std::int64_t n) const noexcept { return turnsScale_ * (expDet(rate_ * static_cast<double>(n)) - 1.0); }
        float at(std::int64_t n) const noexcept { return static_cast<float>(amp_ * sinTurns(phase(n))); }
        // The instantaneous frequency at sample n, in Hz.
        double hzAt(std::int64_t n) const noexcept { return fs_ * turnsScale_ * rate_ * expDet(rate_ * static_cast<double>(n)); }

        void fill(std::span<float> out, std::int64_t n0 = 0) const noexcept
        {
            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = at(n0 + static_cast<std::int64_t>(i));
        }

    private:
        double fs_, amp_;
        double rate_;           // L / N: the exponent per sample
        double turnsScale_;     // f1 N / (fs L): turns per unit of (e^(...) - 1)
    };

    // The sweep over the whole of `out`.
    inline void logSweep(std::span<float> out, double f1, double f2, double fs, double amp = 1.0) noexcept
    {
        LogSweep(f1, f2, fs, static_cast<std::int64_t>(out.size()), amp).fill(out);
    }

    // Uniform white noise in [-amp, amp) from rng (bursts: call it on sub-spans).
    inline void noise(std::span<float> out, Pcg32& rng, float amp = 1.0f) noexcept
    {
        for (float& x : out)
            x = amp * rng.bipolar();
    }

    // A unit impulse of height amp at index `at` (0 elsewhere); a step from index `at` on.
    inline void impulse(std::span<float> out, std::size_t at, float amp = 1.0f) noexcept
    {
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = i == at ? amp : 0.0f;
    }

    inline void step(std::span<float> out, std::size_t at, float amp = 1.0f) noexcept
    {
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = i >= at ? amp : 0.0f;
    }
} // namespace fcmp::probe::sig
