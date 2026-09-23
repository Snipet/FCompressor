// Tools/probes/common/Measure.h: the measurements the spec probes share (03 §3.4; C §5.0, §5.2, §5.3).
//
//   SingleBin   D1's single-bin DFT: the complex amplitude of a signal at one frequency over a window that holds an
//               integer number of cycles (0.1 s of 1 kHz at every standard rate), so there is no leakage and colour
//               harmonics cannot bias the fundamental's gain (C §5.2).
//   lawSeconds  D2's time extraction from a GR step response by TimeLaw: t63 in dB (expDb) or in linear gain
//               (expLin), 10 -> 90 % (t10_90), 0 -> 90 % (t0_90), 50 % (t50), or a slew rate in dB/s (C §5.3; E §2.3).
//   hfRatioDb   the click metric: energy above 8 kHz within +-2 ms of an edge, test against the louder of two controls,
//               in dB (C §5.0: +42.8 dB for a hard cut, +0.3 dB for a 20 ms ramp on a 110 Hz tone; spec <= +3).
//
// Everything is double precision and deterministic: phases and the filter design come from Signals.h's closed forms,
// logarithms and exponentials from its logDet/expDet, and square roots are IEEE, so a golden row built on these numbers
// does not move with libm.
#pragma once

#include "fcdsp/core/Units.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::probe::measure
{
    double dbFromAmplitude(double a) noexcept;      // 20 log10(a); a <= 0 -> -400
    double amplitudeFromDb(double db) noexcept;     // 10^(db / 20)

    struct Bin
    {
        double re = 0, im = 0;
        double amplitude() const noexcept;          // the sinusoid's peak amplitude (2 |X| / N)
    };

    // The single-bin DFT at `hz` over windows of `n` samples. Requires hz * n / fs to be an integer (the window holds
    // whole cycles; throws std::invalid_argument otherwise): the correlation tables are then periodic in n, and a
    // window starting at any absolute sample index n0 uses the same tables.
    class SingleBin
    {
    public:
        SingleBin(double hz, double fs, std::size_t n);
        std::size_t size() const noexcept { return cos_.size(); }
        // x[i] is the sample at absolute index n0 + i; x.size() must equal size().
        Bin operator()(std::span<const float> x, std::int64_t n0) const;
        // 20 log10(|Y| / |X|) of two windows of the same samples.
        double gainDb(std::span<const float> in, std::span<const float> out, std::int64_t n0) const;

    private:
        std::vector<double> cos_, sin_;
    };

    // Step-response extraction (D2). trace[k] is the GR in dB of the k-th sample after a step; the step happened
    // between the sample before trace[0] (time 0, where the GR is `from`) and trace[0] (time 1/fs), so sample k sits at
    // (k + 1) / fs. crossingSeconds returns the first time the trace reaches from + fraction * (to - from), linearly
    // interpolated, or -1 if it never does.
    double crossingSeconds(std::span<const float> trace, double from, double to, double fraction, double fs) noexcept;
    // The published time of `law` for a step from `from` to `to` dB of GR (rateDbPerS: dB/s over 10 -> 90 %). -1 when
    // a crossing is missing.
    double lawSeconds(std::span<const float> trace, double from, double to, double fs, fcdsp::TimeLaw law) noexcept;

    // Energy of x above hz (a 4th-order Butterworth high-pass run from sample 0) within +-halfWindowS of `edge`.
    double hfEnergy(std::span<const float> x, std::size_t edge, double fs, double hz = 8000.0,
                    double halfWindowS = 0.002);
    // 10 log10(E(test) / max(E(controlA), E(controlB))) around `edge`; equal-length signals.
    double hfRatioDb(std::span<const float> test, std::span<const float> controlA, std::span<const float> controlB,
                     std::size_t edge, double fs);
} // namespace fcmp::probe::measure
