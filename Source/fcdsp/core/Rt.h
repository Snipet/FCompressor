#pragma once

// Real-time effect annotation (01 §2.2 rule 6; 03 §2.10 rtsan; ADR-46, K2 #18). FCDSP_NONBLOCKING expands to
// [[clang::nonblocking]] where the compiler supports it, else to nothing. The rtsan preset compiles fcdsp with
// -Wfunction-effects (fcdsp only), which proves at compile time that a nonblocking function calls only functions that
// are nonblocking themselves: annotated, or inline with a visible body the compiler can infer.
//
// Rules (FZ0 errata, review R-F0 #1):
//   - Every declaration the audio thread reaches carries it: EngineHost::process/reset, every IEngine virtual (the
//     destructor included) and its ModeEngine override, ModeEntry::construct (the pointer type) and ModeEngine's
//     construct, the registry lookups, Oversampler::reset/up/down, and the core helpers (Simd, FastMath, Units,
//     ScopedFtz, Sanitize, Smoother4, LinearRamp, ControlTicker). Virtual calls, calls through function pointers and
//     calls to out-of-line definitions cannot be inferred, so those declarations MUST carry it.
//   - Placement: after noexcept and before `override`, `= 0`, `= default` or the body:
//         void process(const ProcessIo&, const BlockParams&) noexcept FCDSP_NONBLOCKING;
//         virtual ~IEngine() FCDSP_NONBLOCKING = default;
//         IEngine* (*construct)(void* arena) noexcept FCDSP_NONBLOCKING;
//   - An out-of-line definition repeats the macro so its effects match the declaration (-Wfunction-effects warns on a
//     mismatch).
//   - Stage policies and Traits hooks are header-inline, so the compiler infers them; annotating them is allowed and
//     recommended. A nonblocking body may not allocate, lock, throw, use thread_local or a function-local static, or
//     call an unannotated out-of-line function (libm, assert's failure path when NDEBUG is off).
//
// Frozen at FZ0 (the macro name and the rules above).

#if defined(__has_cpp_attribute)
  #if __has_cpp_attribute(clang::nonblocking)
    #define FCDSP_NONBLOCKING [[clang::nonblocking]]
  #endif
#endif
#if !defined(FCDSP_NONBLOCKING)
  #define FCDSP_NONBLOCKING
#endif
