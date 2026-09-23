#pragma once

// Control-rate ticks at ABSOLUTE sample-index multiples of kTickSamples (01 §5.1; E §4.6), so coefficient updates
// land on the same samples whatever the host block size (C §5.6.3 block-size invariance).
//
// Frozen at FZ0. F0 declares; F1 (S1) implements. advance() is FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata); it runs per
// sample, so F1 should define it inline in this header (an out-of-line definition repeats the macro).

#include "fcdsp/core/Rt.h"
#include <cstdint>

namespace fcdsp {

struct ControlTicker {                      // true every kTickSamples at ABSOLUTE sample index multiples
    uint64_t next = 0;
    bool advance(uint64_t sampleIndex) noexcept FCDSP_NONBLOCKING;
};

inline constexpr int kTickSamples = 16;

// Body (F1, S1). The first call ticks (a new or reset engine designs its coefficients on its first sample, next = 0);
// after that, ticks fall on the absolute multiples of kTickSamples, whatever the host block size. sampleIndex never
// decreases between resets (ControlTicker{} starts over); a skipped index still ticks at the next call.
inline bool ControlTicker::advance(uint64_t sampleIndex) noexcept FCDSP_NONBLOCKING
{
    constexpr uint64_t k = static_cast<uint64_t>(kTickSamples);
    if (sampleIndex < next)
        return false;
    next = sampleIndex - sampleIndex % k + k;
    return true;
}

} // namespace fcdsp
