#pragma once

// Flush-to-zero for the lifetime of a scope (01 §5.1, §5.7; K2 #24): HardwareReverb's ScopedFtz (Tools/Harness.h),
// made JUCE-free. arm64: FPCR |= 1 << 24 (FZ). x86-64: MXCSR |= 0x8040 (FTZ + DAZ). The destructor restores the
// saved register. EngineHost::process opens one, and so does every analysis:: entry point, so probes, the plugin and
// the PreviewWorker run in the same FP mode whoever calls them. wasm32 (ADR-93) has no FP control register: the scope
// is empty there, and fcdsp::simd's wasm backend flushes tiny results itself (core/FlushTiny.h).
//
// Frozen at FZ0. F0 declares; F1 (S1) implements (inline asm / _mm_getcsr, allocation- and syscall-free). The
// constructor and destructor are FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata), because EngineHost::process opens one;
// F1 may define them inline in this header or out of line (repeating the macro).

#include "fcdsp/core/Rt.h"
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
#elif !defined(__aarch64__) && !defined(__wasm__)
  #error "ScopedFtz: no flush-to-zero control for this architecture (arm64 FPCR, x86-64 MXCSR; wasm32 has none)."
#endif

namespace fcdsp {

struct ScopedFtz {
    ScopedFtz() noexcept FCDSP_NONBLOCKING;     // saves the FP control register, then sets flush-to-zero
    ~ScopedFtz() noexcept FCDSP_NONBLOCKING;    // restores the saved register
    ScopedFtz(const ScopedFtz&) = delete;
    ScopedFtz& operator=(const ScopedFtz&) = delete;

private:
    uint64_t saved_ = 0;                    // FPCR (arm64) or MXCSR (x86-64, low 32 bits)
};

// Bodies (F1, S1): inline, two register accesses each. The asm statements are volatile with a "memory" clobber, so the
// compiler neither drops them nor moves a load or store of the protected scope across them. That is all it orders:
// LLVM's default FP model treats the FP environment as constant, so arithmetic on values that stay in registers may
// execute on either side of the register write (dsp.units saw the fmul of a value loaded before the scope sink past
// the msr). The mode therefore applies to arithmetic whose operands are loaded inside the scope and whose results are
// stored inside it: open the scope first thing, as EngineHost::process and the analysis entry points do, and keep
// every flushed quantity in memory across its boundary.
#if defined(__aarch64__)

inline ScopedFtz::ScopedFtz() noexcept FCDSP_NONBLOCKING
{
    uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr) : : "memory");
    saved_ = fpcr;
    fpcr |= uint64_t{1} << 24;                                  // FZ
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr) : "memory");
}

inline ScopedFtz::~ScopedFtz() noexcept FCDSP_NONBLOCKING
{
    __asm__ volatile("msr fpcr, %0" : : "r"(saved_) : "memory");
}

#elif defined(__wasm__)

// Nothing to save and nothing to set. The destructor names the member so that it is not an unused private field.
inline ScopedFtz::ScopedFtz() noexcept FCDSP_NONBLOCKING {}
inline ScopedFtz::~ScopedFtz() noexcept FCDSP_NONBLOCKING { static_cast<void>(saved_); }

#else

inline ScopedFtz::ScopedFtz() noexcept FCDSP_NONBLOCKING
{
    const unsigned int csr = _mm_getcsr();
    saved_ = csr;
    _mm_setcsr(csr | 0x8040u);                                  // FTZ (0x8000) | DAZ (0x0040)
}

inline ScopedFtz::~ScopedFtz() noexcept FCDSP_NONBLOCKING
{
    _mm_setcsr(static_cast<unsigned int>(saved_));
}

#endif

} // namespace fcdsp
