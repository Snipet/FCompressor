#pragma once

// Level calibration and time-constant conventions (01 §3.1, §5.1; E §2.3). Implemented here (header-only).
// Frozen at FZ0; kDbuAt0dBFS and the TimeLaw enumerators are v1-forever.

#include "fcdsp/core/FastMath.h"
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
constexpr float lawFactor(TimeLaw law) noexcept {
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
inline float alphaFromTau(float tauMs, float fs) noexcept {
    constexpr float kLn2 = 0.693147181f;
    if (!(tauMs > 0.0f) || !(fs > 0.0f))
        return 0.0f;
    return fcdsp::exp2(-1000.0f / (tauMs * fs * kLn2));
}

} // namespace fcdsp
