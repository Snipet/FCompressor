// FCMP_PROBE layer=dsp name=selftest scope=global timeout=60
//
// dsp.selftest (B0; 03 §2.9): the probe plumbing itself, so a later probe's failure is never the harness's. It checks
// registration and dispatch, the context ProbeMain passes, that the body runs under ScopedFtz, Harness v2's spec rows,
// the deterministic signal generators (Signals.h), the tolerance table (Tolerances.h), the thread-scoped allocation
// counter (AllocCounter.cpp), the RtInterposer wiring and the sandbox. Spec rows only: it has no golden rows, so it
// never produces a candidate (Sprint 0 expects none). FZ0's evidence with lint.headers and fg.harness.self.
#include "ProbeRegistry.h"
#include "Signals.h"
#include "Tolerances.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#ifndef FCMP_RT_INTERPOSER
#error "FCMP_RT_INTERPOSER is defined by cmake/FcmpProbes.cmake"
#endif
#ifndef FCOMPRESSOR_RELEASE
#error "FCOMPRESSOR_RELEASE is defined by cmake/FcmpProbes.cmake"
#endif

namespace
{
    // "<layer>.<name>" with layer dsp|proc|ui and name [a-z0-9_]+ (the FCMP_PROBE line grammar, FcmpProbes.cmake).
    bool validProbeName(std::string_view n)
    {
        const auto dot = n.find('.');
        if (dot == std::string_view::npos)
            return false;
        const std::string_view layer = n.substr(0, dot), name = n.substr(dot + 1);
        if (layer != "dsp" && layer != "proc" && layer != "ui")
            return false;
        return !name.empty()
            && std::all_of(name.begin(), name.end(),
                           [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
    }

    constexpr double kTwoPi = 2.0 * std::numbers::pi;
} // namespace

FCMP_PROBE(dsp, selftest)
{
    namespace sig = fcmp::probe::sig;
    namespace tol = fcmp::probe::tol;
    namespace alloc = fcmp::probe::alloc;
    namespace rt = fcmp::probe::rt;

    // ---- 1. registration, dispatch and the context --------------------------------------------------------------------
    {
        int registered = 0, self = 0, malformed = 0;
        for (const auto* r = fcmp::probe::registrations(); r != nullptr; r = r->next)
        {
            ++registered;
            if (std::string_view(r->name) == "dsp.selftest")
                ++self;
            if (!validProbeName(r->name) || r->fn == nullptr)
                ++malformed;
        }
        P.eq("registry.selftest_registered_once", self, 1);
        P.ge("registry.probes", registered, 1);
        P.eq("registry.malformed", malformed, 0);
        P.eq("ctx.global_key_is_empty", C.key.empty() ? 1 : 0, 1);
    }

    // ---- 2. ProbeMain runs the body under ScopedFtz: a subnormal result is flushed to zero ------------------------------
    {
        volatile float a = 1.0e-30f, b = 1.0e-10f;         // 1e-40 is subnormal in float
        const float product = a * b;
        P.eq("ftz.subnormal_result_flushed", std::fpclassify(product) == FP_ZERO ? 1 : 0, 1);
    }

    // ---- 3. Harness v2 spec rows judge as documented (a failure here would already fail this run) --------------------
    P.near("harness.near_abs", 1.0 + 1e-10, 1.0, 1e-9);
    P.near("harness.near_rel", 1000.0 + 1e-4, 1000.0, 0.0, 1e-6);
    P.le("harness.le", 1.0, 1.0);
    P.ge("harness.ge", 2.0, 1.0);
    P.in("harness.in", 0.5, 0.0, 1.0);

    // ---- 4. PCG32 against the reference sequence -----------------------------------------------------------------------
    {
        sig::Pcg32 rng(42u, 54u);
        constexpr std::uint32_t kRef[] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
        int mismatches = 0;
        for (const std::uint32_t want : kRef)
            if (rng.next() != want)
                ++mismatches;
        P.eq("pcg32.reference_mismatches", mismatches, 0);

        int outOfRange = 0;
        for (int i = 0; i < 100000; ++i)
        {
            const float u = rng.uniform(), v = rng.bipolar();
            const double d = rng.uniformDouble();
            if (!(u >= 0.0f && u < 1.0f) || !(v >= -1.0f && v < 1.0f) || !(d >= 0.0 && d < 1.0) || rng.bounded(7u) >= 7u)
                ++outOfRange;
        }
        P.eq("pcg32.out_of_range", outOfRange, 0);

        sig::Pcg32 a(7u), b(7u), c(8u);
        int same = 1, differs = 0;
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t x = a.next(), y = b.next(), z = c.next();
            same &= x == y ? 1 : 0;
            differs |= x != z ? 1 : 0;
        }
        P.eq("pcg32.same_seed_same_stream", same, 1);
        P.eq("pcg32.other_seed_other_stream", differs, 1);
    }

    // ---- 5. deterministic elementary functions against libm (accuracy; determinism is by construction) -----------------
    {
        double sinErr = 0, cosErr = 0;
        for (int i = -20000; i <= 20000; ++i)
        {
            const double t = i * 1.0e-4 + 3.7e-6;           // turns in about [-2, 2]
            sinErr = std::max(sinErr, std::abs(sig::sinTurns(t) - std::sin(kTwoPi * t)));
            cosErr = std::max(cosErr, std::abs(sig::cosTurns(t) - std::cos(kTwoPi * t)));
        }
        P.le("math.sin_turns.max_abs_err", sinErr, 1e-14);
        P.le("math.cos_turns.max_abs_err", cosErr, 1e-14);
        P.eq("math.sin_turns.exact_quarter", sig::sinTurns(0.25) == 1.0 ? 1 : 0, 1);
        P.eq("math.sin_turns.exact_three_quarters", sig::sinTurns(0.75) == -1.0 ? 1 : 0, 1);
        P.eq("math.sin_turns.exact_zero", sig::sinTurns(0.0) == 0.0 && sig::sinTurns(3.5) == 0.0 ? 1 : 0, 1);

        double expErr = 0, logErr = 0;
        for (int i = -5000; i <= 5000; ++i)
        {
            const double x = i * 0.01 + 1.3e-5;             // [-50, 50]
            expErr = std::max(expErr, std::abs(sig::expDet(x) / std::exp(x) - 1.0));
            const double y = std::exp(x * 0.4);             // [2e-9, 5e8]
            logErr = std::max(logErr, std::abs(sig::logDet(y) - std::log(y)) / std::max(1.0, std::abs(std::log(y))));
        }
        P.le("math.exp.max_rel_err", expErr, 4e-15);
        P.le("math.log.max_err", logErr, 4e-15);
        P.eq("math.exp.exact_zero", sig::expDet(0.0) == 1.0 ? 1 : 0, 1);
        P.eq("math.log.exact_one", sig::logDet(1.0) == 0.0 ? 1 : 0, 1);
    }

    // ---- 6. closed-form sines ----------------------------------------------------------------------------------------------
    {
        constexpr double fs = 48000.0;
        std::vector<float> whole(4800), split(4800);
        sig::sine(whole, 1000.0, fs, 0.5);
        sig::sine(std::span(split).first(1234), 1000.0, fs, 0.5);
        sig::sine(std::span(split).subspan(1234), 1000.0, fs, 0.5, 0.0, 1234);
        P.eq("sine.split_equals_whole", whole == split ? 1 : 0, 1);
        P.eq("sine.quarter_cycle_exact", whole[12] == 0.5f && whole[36] == -0.5f && whole[48] == 0.0f ? 1 : 0, 1);
        double err = 0;
        for (std::size_t n = 0; n < whole.size(); ++n)
            err = std::max(err, std::abs(static_cast<double>(whole[n]) - 0.5 * std::sin(kTwoPi * 1000.0 * n / fs)));
        P.le("sine.max_abs_err", err, 3e-8);                // float rounding of the output (2^-25 at 0.5)
        P.near("sine.at_long_offset", sig::sineAt(48000LL * 3600 + 12, 1000.0, fs), 1.0, 0.0);   // one hour in
    }

    // ---- 7. log sweep ---------------------------------------------------------------------------------------------------
    {
        constexpr double fs = 48000.0, f1 = 20.0, f2 = 20000.0;
        constexpr std::int64_t N = 4 * 48000;
        const sig::LogSweep sweep(f1, f2, fs, N, 0.5);
        P.eq("sweep.starts_at_zero", sweep.at(0) == 0.0f ? 1 : 0, 1);
        P.near("sweep.start_hz", sweep.hzAt(0), f1, 0.0, 1e-12);
        P.near("sweep.end_hz", sweep.hzAt(N), f2, 0.0, 1e-12);
        const double L = std::log(f2 / f1), T = static_cast<double>(N) / fs;
        double err = 0;
        for (std::int64_t n = 0; n < N; n += 97)
        {
            const double t = static_cast<double>(n) / fs;
            const double ref = 0.5 * std::sin(kTwoPi * f1 * T / L * (std::exp(t * L / T) - 1.0));
            err = std::max(err, std::abs(static_cast<double>(sweep.at(n)) - ref));
        }
        P.le("sweep.max_abs_err_vs_libm", err, 1e-7);
        std::vector<float> a(static_cast<std::size_t>(N)), b(static_cast<std::size_t>(N));
        sig::logSweep(a, f1, f2, fs, 0.5);
        sweep.fill(b);
        P.eq("sweep.fill_equals_at", a == b ? 1 : 0, 1);
    }

    // ---- 8. tolerance table ---------------------------------------------------------------------------------------------
    {
        P.eq("tol.rigor_count", tol::kRigorCount, 3);
        P.near("tol.clean.curve_outside_knee_db", tol::forRigor(0).curveOutsideKneeDb, 0.05, 0.0);
        P.near("tol.character.curve_inside_knee_db", tol::forRigor(2).curveInsideKneeDb, 0.75, 0.0);
        P.eq("tol.character.no_textbook_check", tol::forRigor(2).textbookApplies ? 1 : 0, 0);
        P.near("tol.tau_floor_s", tol::tauToleranceSeconds(tol::forRigor(0), 1e-4, 48000.0), 1.5 / 48000.0, 1e-18);
        P.near("tol.tau_rel_s", tol::tauToleranceSeconds(tol::forRigor(1), 0.1, 48000.0), 0.01, 1e-15);
        enum class RigorLike : std::uint8_t { clean, modelled, character };
        P.near("tol.enum_index", tol::forRigor(RigorLike::modelled).ratioRel, 0.03, 0.0);
        bool threw = false;
        try
        {
            (void) tol::forRigor(3);
        }
        catch (const std::out_of_range&)
        {
            threw = true;
        }
        P.eq("tol.out_of_range_throws", threw ? 1 : 0, 1);
    }

    // ---- 9. allocation counter: counts the armed thread only -----------------------------------------------------------
    {
        {
            const alloc::Scope scope;
            int* volatile p = new int(7);                   // volatile: the allocation escapes and cannot be elided
            delete p;
        }
        P.eq("alloc.new_counted", static_cast<std::int64_t>(alloc::allocations()), 1);
        P.eq("alloc.delete_counted", static_cast<std::int64_t>(alloc::deallocations()), 1);
        P.eq("alloc.bytes_counted", static_cast<std::int64_t>(alloc::bytes()), static_cast<std::int64_t>(sizeof(int)));

        {
            const alloc::Scope scope;
            std::vector<float> v(64);
            float* volatile data = v.data();
            (void) data;
        }
        P.eq("alloc.vector_counted", static_cast<std::int64_t>(alloc::allocations()), 1);

        std::atomic<int> phase{0};
        std::thread other([&phase]
        {
            while (phase.load(std::memory_order_acquire) != 1)
                std::this_thread::yield();
            for (int i = 0; i < 100; ++i)
            {
                int* volatile q = new int(i);
                delete q;
            }
            phase.store(2, std::memory_order_release);
        });
        alloc::reset();
        alloc::arm();
        phase.store(1, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 2)
            std::this_thread::yield();
        alloc::disarm();
        const std::uint64_t whileOtherThreadAllocated = alloc::allocations();
        other.join();
        P.eq("alloc.other_threads_not_counted", static_cast<std::int64_t>(whileOtherThreadAllocated), 0);

        alloc::reset();
        {
            int* volatile p = new int(1);                   // disarmed: not counted
            delete p;
        }
        P.eq("alloc.disarmed_not_counted", static_cast<std::int64_t>(alloc::allocations()), 0);
    }

    // ---- 10. real-time call counters: present exactly when the build selected the interposer fallback ------------------
    {
        const bool available = rt::available();
        P.eq("rt.interposer_available", available ? 1 : 0, FCMP_RT_INTERPOSER);
        if (available)
        {
            std::mutex m;
            rt::reset();
            rt::arm();
            void* volatile p = std::malloc(64);
            std::free(p);
            m.lock();
            m.unlock();
            rt::disarm();
            const rt::Counts c = rt::counts();
            P.ge("rt.malloc_counted", static_cast<double>(c.mallocs), 1.0);
            P.ge("rt.free_counted", static_cast<double>(c.frees), 1.0);
            P.ge("rt.mutex_lock_counted", static_cast<double>(c.mutexLocks), 1.0);
        }
        else
            P.eq("rt.counts_zero_without_interposer", static_cast<std::int64_t>(rt::counts().total()), 0);
    }

    // ---- 11. sandbox and build flags --------------------------------------------------------------------------------------
    if (const char* dir = std::getenv("FCMP_PREFS_DIR"); dir != nullptr && *dir != '\0')
    {
        std::error_code ec;
        P.eq("sandbox.prefs_dir_created", std::filesystem::is_directory(dir, ec) ? 1 : 0, 1);
    }
    P.in("build.release_flag", FCOMPRESSOR_RELEASE, 0, 1);

    return P.finish();
}
