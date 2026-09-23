#pragma once

// Control-rate ticks at ABSOLUTE sample-index multiples of kTickSamples (01 §5.1; E §4.6), so coefficient updates
// land on the same samples whatever the host block size (C §5.6.3 block-size invariance).
//
// Frozen at FZ0. F0 declares; F1 (S1) implements.

#include <cstdint>

namespace fcdsp {

struct ControlTicker {                      // true every kTickSamples at ABSOLUTE sample index multiples
    uint64_t next = 0;
    bool advance(uint64_t sampleIndex) noexcept;
};

inline constexpr int kTickSamples = 16;

} // namespace fcdsp
