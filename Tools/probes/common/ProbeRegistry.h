// Tools/probes/common/ProbeRegistry.h: the probe registration macro and the runtime services every probe executable
// has (03 §2.9; SPRINTS §7 D23). Frozen at FZ0 with ProbeMain.cpp (the probe macro and the command line).
//
// One probe per file. The file's first matching line registers the CTest test(s) (cmake/FcmpProbes.cmake); the macro
// registers the subcommand <layer>.<name> that ProbeMain.cpp dispatches:
//
//   // FCMP_PROBE layer=dsp name=static scope=mode timeout=60
//   #include "ProbeRegistry.h"
//   FCMP_PROBE(dsp, static)                   // int (funkgui::test::Probe& P, const fcmp::probe::Ctx& C)
//   {
//       P.near("static.r4.t-30.out_db", got, want, 0.05);                  // spec row
//       P.num ("static.r4.t-30.k6.x-20.out_db", v, funkgui::test::Tol::abs(0.01));   // golden row
//       return P.finish();
//   }
//
// - ProbeMain constructs P (funkgui::test::Probe, Harness v2, 03 §3.2) from the process's command line, so every harness
//   flag passes through; it opens funkgui::test::ScopedFtz around the body, turns an escaping exception into a harness
//   error (exit 4), and calls P.finish() if the body did not. The exit code is always finish()'s.
// - C.key is the Mode key from --mode ("" for a global probe). Probes resolve it through the registry themselves
//   (fcdsp::byKey), so ProbeMain links nothing from fcdsp (D23).
// - Static registration is legal here: the no-static-initialisation rule (C D12) is fcdsp's, not the probes'.
#pragma once

#include <funkgui/test/Harness.h>

#include <cstdint>
#include <string_view>

namespace fcmp::probe
{
    struct Ctx
    {
        std::string_view key;       // --mode <key>; empty for a global probe
    };

    using ProbeFn = int (*)(funkgui::test::Probe&, const Ctx&);

    // A node of the intrusive list ProbeMain walks. Constructed only by FCMP_PROBE, at static-initialisation time.
    struct Registration
    {
        Registration(const char* layerDotName, ProbeFn fn) noexcept;     // prepends itself (ProbeMain.cpp)
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;

        const char* name;           // "<layer>.<name>"
        ProbeFn fn;
        const Registration* next;
    };

    const Registration* registrations() noexcept;      // the list head (unordered)

    // ---- the command line (ADR-91) -----------------------------------------------------------------------------------
    // main()'s argc and argv, recorded by ProbeMain before any probe runs, for a probe that reads flags of its own
    // (the ones after "--" in ui.dump). Probes read macOS's _NSGetArgc() / _NSGetArgv() before Linux; this is the same
    // pair on every platform.
    int argc() noexcept;
    char** argv() noexcept;

    // ---- allocation counter (AllocCounter.cpp: replacement global operator new/delete) --------------------------------
    // Counts operator new/delete calls made by the ARMED thread only, so other threads (a JUCE message thread, a
    // worker) never pollute a count. One armed thread at a time. The counters are lock-free and never allocate.
    namespace alloc
    {
        void arm() noexcept;                      // count the calling thread from now on
        void disarm() noexcept;                   // stop counting (the totals are kept)
        void reset() noexcept;                    // zero the totals
        std::uint64_t allocations() noexcept;     // operator new calls counted while armed
        std::uint64_t deallocations() noexcept;   // operator delete calls counted while armed
        std::uint64_t bytes() noexcept;           // bytes requested by the counted allocations

        // reset() + arm() for the scope, then disarm(); dsp.rt arms one around every process() call (03 §3.4).
        struct Scope
        {
            Scope() noexcept { reset(); arm(); }
            ~Scope() { disarm(); }
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
        };
    } // namespace alloc

    // ---- real-time call counters (RtInterposer.cpp; the rtsan preset's fallback, K2 #18) -------------------------------
    // available() is true only in a build whose compiler lacks -fsanitize=realtime and that was configured with
    // FCOMPRESSOR_RTSAN=ON (FCMP_RT_INTERPOSER=1): the probes then link an interposing dylib that counts, on the armed
    // thread, calls to malloc, free, pthread_mutex_lock, os_unfair_lock_lock, write and mach_msg. Elsewhere every
    // function is a no-op and counts() is all zeros. Call available() once before arming (it resolves the dylib).
    namespace rt
    {
        struct Counts
        {
            std::uint64_t mallocs = 0, frees = 0, mutexLocks = 0, unfairLocks = 0, writes = 0, machMsgs = 0;
            std::uint64_t total() const noexcept
            {
                return mallocs + frees + mutexLocks + unfairLocks + writes + machMsgs;
            }
        };

        bool available() noexcept;
        void arm() noexcept;                      // count the calling thread
        void disarm() noexcept;
        void reset() noexcept;
        Counts counts() noexcept;
    } // namespace rt
} // namespace fcmp::probe

#define FCMP_PROBE_FN_(layer, name) fcmpProbe_##layer##_##name
#define FCMP_PROBE_REG_(layer, name) fcmpProbeReg_##layer##_##name

// FCMP_PROBE(layer, name) { body }: defines and registers the subcommand "<layer>.<name>". The body sees
// `funkgui::test::Probe& P` and `const fcmp::probe::Ctx& C` and returns P.finish().
#define FCMP_PROBE(layer, name)                                                                                     \
    static int FCMP_PROBE_FN_(layer, name)(::funkgui::test::Probe&, const ::fcmp::probe::Ctx&);                     \
    static const ::fcmp::probe::Registration FCMP_PROBE_REG_(layer, name){#layer "." #name,                         \
                                                                          &FCMP_PROBE_FN_(layer, name)};            \
    static int FCMP_PROBE_FN_(layer, name)([[maybe_unused]] ::funkgui::test::Probe& P,                              \
                                           [[maybe_unused]] const ::fcmp::probe::Ctx& C)
