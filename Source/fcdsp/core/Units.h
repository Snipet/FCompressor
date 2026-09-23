#pragma once

// Level calibration and time-constant conventions (01 §3.1, §5.1; E §2.3). Implemented here (header-only).
// Frozen at FZ0; kDbuAt0dBFS and the TimeLaw enumerators are v1-forever. lawFactor and alphaFromTau are
// FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include <cstdint>

namespace fcdsp {

inline constexpr float kDbuAt0dBFS = 22.0f;     // fixed calibration: 0 VU = +4 dBu = -18 dBFS (01 §3.1, Q10)

// What a PUBLISHED time means (E §2.3). tau is the one-pole 63 % time constant the engine runs.
enum class TimeLaw : uint8_t {
    expDb,          // tau of the GR change in dB (the engine's own law)
    expLin,         // tau of the change in linear gain
    t10_90,         // 10 -> 90 % time = tau * ln 9
    t0_90,          // 0 -> 90 % time  = tau * ln 10
    t50,            // time to 50 %    = tau * ln 2
    rateDbPerS,     // a slew rate in dB/s, not a time
};

// t_published / tau: 1, 1, ln 9, ln 10, ln 2. A rate is not a time; it maps through unchanged (factor 1), and the
// policy that publishes a rate interprets the value itself.
constexpr float lawFactor(TimeLaw law) noexcept FCDSP_NONBLOCKING {
    switch (law) {
        case TimeLaw::expDb:      return 1.0f;
        case TimeLaw::expLin:     return 1.0f;
        case TimeLaw::t10_90:     return 2.19722458f;    // ln 9
        case TimeLaw::t0_90:      return 2.30258509f;    // ln 10
        case TimeLaw::t50:        return 0.693147181f;   // ln 2
        case TimeLaw::rateDbPerS: return 1.0f;
    }
    return 1.0f;
}

// One-pole coefficient for a time constant in ms at sample rate fs: alpha = 2^(-1000 / (tauMs * fs * ln 2)), i.e.
// e^(-1 / (tau_s * fs)). tauMs <= 0 (or NaN), or fs <= 0, gives 0: an instantaneous pole. Runs per control tick on
// the audio thread, so it uses fcdsp::exp2 (FastMath, fma-only), never libm.
inline float alphaFromTau(float tauMs, float fs) noexcept FCDSP_NONBLOCKING {
    constexpr float kLn2 = 0.693147181f;
    if (!(tauMs > 0.0f) || !(fs > 0.0f))
        return 0.0f;
    return fcdsp::exp2(-1000.0f / (tauMs * fs * kLn2));
}

// S2 lead revision (from F3): the well-conditioned complement, for long time constants where alphaFromTau's
// float alpha near 1 loses resolution (F1: 0.3 % tau error at 1e5 samples). Ballistics carry 1 - alpha.
// 1 - alpha for a one-pole of time constant tauMs at rate fs: 1 - e^(-x), x = 1000 / (tauMs * fs). For x < 1/8 the
// Taylor series x(1 - x/2(1 - x/3(1 - x/4(1 - x/5(1 - x/6))))) (truncation below 1e-9 relative), else
// 1 - exp2(-x log2 e) (1 - alpha >= 0.11 there, so the subtraction costs at most 5e-7 relative). tauMs <= 0 (or NaN),
// or fs <= 0: 1, an instantaneous pole, as alphaFromTau gives 0 there.
inline float oneMinusAlpha(float tauMs, float fs) noexcept FCDSP_NONBLOCKING
{
    if (!(tauMs > 0.0f) || !(fs > 0.0f))
        return 1.0f;
    const float x = 1000.0f / (tauMs * fs);
    if (x < 0.125f)
    {
        float p = 1.0f - x * (1.0f / 6.0f);
        p = 1.0f - x * (1.0f / 5.0f) * p;
        p = 1.0f - x * (1.0f / 4.0f) * p;
        p = 1.0f - x * (1.0f / 3.0f) * p;
        p = 1.0f - x * 0.5f * p;
        return x * p;
    }
    return 1.0f - fcdsp::exp2(-x * 1.44269504f);
}

} // namespace fcdsp
