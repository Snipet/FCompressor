#pragma once

// The probes' per-sample view of the engine (01 §5.4; K1 #6, K2 #2). Always compiled, never behind a flag: the tap is
// a runtime pointer that only probes set (EngineHost::setTap); the plugin never calls it. The host holds an
// std::atomic<TestTap*>, loads it once per block and forwards these spans through ControlIo's nullable outputs, so
// ControlIo gains no member. Sprint-frozen.

#include "fcdsp/core/Simd.h"
#include <cstdint>
#include <span>

namespace fcdsp {

struct TestTap {                                       // per base-rate sample of the INCOMING path; empty span = not tapped
    std::span<simd::f32x4> grDb, detDb, tgtDb, s2GrDb; // engine lanes {c0, c1, aux0, aux1}
    std::span<uint8_t>     bits;                       // ControlIo::bits
    uint64_t firstSample = 0;                          // absolute sample index of element 0 (set by the probe)
    uint64_t written = 0;                              // elements written (EngineHost advances; stops at capacity)
};

} // namespace fcdsp
