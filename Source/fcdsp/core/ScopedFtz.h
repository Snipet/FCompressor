#pragma once

// Flush-to-zero for the lifetime of a scope (01 §5.1, §5.7; K2 #24): HardwareReverb's ScopedFtz (Tools/Harness.h),
// made JUCE-free. arm64: FPCR |= 1 << 24 (FZ). x86-64: MXCSR |= 0x8040 (FTZ + DAZ). The destructor restores the
// saved register. EngineHost::process opens one, and so does every analysis:: entry point, so probes, the plugin and
// the PreviewWorker run in the same FP mode whoever calls them.
//
// Frozen at FZ0. F0 declares; F1 (S1) implements (inline asm / _mm_getcsr, allocation- and syscall-free). The
// constructor and destructor are FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata), because EngineHost::process opens one;
// F1 may define them inline in this header or out of line (repeating the macro).

#include "fcdsp/core/Rt.h"
#include <cstdint>

namespace fcdsp {

struct ScopedFtz {
    ScopedFtz() noexcept FCDSP_NONBLOCKING;     // saves the FP control register, then sets flush-to-zero
    ~ScopedFtz() noexcept FCDSP_NONBLOCKING;    // restores the saved register
    ScopedFtz(const ScopedFtz&) = delete;
    ScopedFtz& operator=(const ScopedFtz&) = delete;

private:
    uint64_t saved_ = 0;                    // FPCR (arm64) or MXCSR (x86-64, low 32 bits)
};

} // namespace fcdsp
