// Tools/probes/common/RtInterposer.cpp: the rtsan preset's fallback when the compiler has no -fsanitize=realtime
// (Apple clang 21 has none; 03 §2.10, K2 #18). It counts, on one armed thread (the probe's audio thread), calls to
// malloc, free, pthread_mutex_lock, os_unfair_lock_lock, write and mach_msg: what an operator new counter cannot see.
//
// dyld applies __DATA,__interpose tuples only from images other than the main executable, so this file is built twice:
// - FCMP_RT_INTERPOSER_DYLIB=1: the dylib fcmp_rt_interposer (cmake/FcmpProbes.cmake builds and links it into the
//   probes only when FCMP_RTSAN_MODE is "interposer"). It holds the interposing functions, whose own calls reach the
//   real functions (dyld never interposes an image on itself), and exports a small C API.
// - otherwise (every probe executable, through the common glob): fcmp::probe::rt (ProbeRegistry.h), which finds that C
//   API with dlsym. Without the dylib, available() is false, arming does nothing and every count is zero.
// The counters are constant-initialised lock-free atomics (they run before any static constructor) and never allocate.
#if defined(FCMP_RT_INTERPOSER_DYLIB) && FCMP_RT_INTERPOSER_DYLIB

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mach/message.h>
#include <os/lock.h>
#include <pthread.h>
#include <unistd.h>

namespace
{
    enum Counter : int { kMalloc, kFree, kMutexLock, kUnfairLock, kWrite, kMachMsg, kCounters };

    constinit std::atomic<pthread_t> gArmed{nullptr};
    constinit std::atomic<std::uint64_t> gCounts[kCounters]{};

    void note(Counter c) noexcept
    {
        const pthread_t armed = gArmed.load(std::memory_order_relaxed);
        if (armed != nullptr && pthread_equal(armed, pthread_self()) != 0)
            gCounts[c].fetch_add(1, std::memory_order_relaxed);
    }

    void* countedMalloc(std::size_t n)
    {
        note(kMalloc);
        return std::malloc(n);
    }
    void countedFree(void* p)
    {
        note(kFree);
        std::free(p);
    }
    int countedMutexLock(pthread_mutex_t* m)
    {
        note(kMutexLock);
        return pthread_mutex_lock(m);
    }
    void countedUnfairLock(os_unfair_lock_t l)
    {
        note(kUnfairLock);
        os_unfair_lock_lock(l);
    }
    ssize_t countedWrite(int fd, const void* buf, std::size_t n)
    {
        note(kWrite);
        return write(fd, buf, n);
    }
    mach_msg_return_t countedMachMsg(mach_msg_header_t* msg, mach_msg_option_t option, mach_msg_size_t sendSize,
                                     mach_msg_size_t rcvSize, mach_port_name_t rcvName, mach_msg_timeout_t timeout,
                                     mach_port_name_t notify)
    {
        note(kMachMsg);
        return mach_msg(msg, option, sendSize, rcvSize, rcvName, timeout, notify);
    }

    // dyld's tuple layout: { replacement, replacee }. The table is address constants only, so it is emitted statically
    // (dyld reads it before any initialiser runs).
    using AnyFn = void (*)();
    struct Interpose
    {
        AnyFn replacement;
        AnyFn replacee;
    };
#define FCMP_INTERPOSE(replacement, replacee) \
    { reinterpret_cast<AnyFn>(&(replacement)), reinterpret_cast<AnyFn>(&(replacee)) }

    [[gnu::used, gnu::section("__DATA,__interpose")]] const Interpose kInterpose[] = {
        FCMP_INTERPOSE(countedMalloc, std::malloc),
        FCMP_INTERPOSE(countedFree, std::free),
        FCMP_INTERPOSE(countedMutexLock, pthread_mutex_lock),
        FCMP_INTERPOSE(countedUnfairLock, os_unfair_lock_lock),
        FCMP_INTERPOSE(countedWrite, write),
        FCMP_INTERPOSE(countedMachMsg, mach_msg),
    };
#undef FCMP_INTERPOSE
} // namespace

extern "C"
{
    // The exported C API (declared first: the dylib builds with FCMP_WARNING_FLAGS, -Wmissing-prototypes included).
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_arm(void);
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_disarm(void);
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_reset(void);
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_counts(std::uint64_t* out6);

    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_arm(void)
    {
        gArmed.store(pthread_self(), std::memory_order_relaxed);
    }
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_disarm(void)
    {
        gArmed.store(nullptr, std::memory_order_relaxed);
    }
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_reset(void)
    {
        for (auto& c : gCounts)
            c.store(0, std::memory_order_relaxed);
    }
    [[gnu::visibility("default")]] void fcmp_rt_interposer_v1_counts(std::uint64_t* out6)
    {
        for (int i = 0; i < kCounters; ++i)
            out6[i] = gCounts[i].load(std::memory_order_relaxed);
    }
}

#else // the probe-executable half

#include "ProbeRegistry.h"

#include <cstdint>
#include <dlfcn.h>

namespace
{
    struct Api
    {
        void (*arm)() = nullptr;
        void (*disarm)() = nullptr;
        void (*reset)() = nullptr;
        void (*counts)(std::uint64_t*) = nullptr;
        bool ok = false;
    };

    template <class F>
    F lookup(const char* name) noexcept
    {
        return reinterpret_cast<F>(dlsym(RTLD_DEFAULT, name));
    }

    const Api& api() noexcept
    {
        static const Api a = []
        {
            Api r;
            r.arm = lookup<void (*)()>("fcmp_rt_interposer_v1_arm");
            r.disarm = lookup<void (*)()>("fcmp_rt_interposer_v1_disarm");
            r.reset = lookup<void (*)()>("fcmp_rt_interposer_v1_reset");
            r.counts = lookup<void (*)(std::uint64_t*)>("fcmp_rt_interposer_v1_counts");
            r.ok = r.arm != nullptr && r.disarm != nullptr && r.reset != nullptr && r.counts != nullptr;
            return r;
        }();
        return a;
    }
} // namespace

namespace fcmp::probe::rt
{
    bool available() noexcept { return api().ok; }

    void arm() noexcept
    {
        if (api().ok)
            api().arm();
    }

    void disarm() noexcept
    {
        if (api().ok)
            api().disarm();
    }

    void reset() noexcept
    {
        if (api().ok)
            api().reset();
    }

    Counts counts() noexcept
    {
        Counts c;
        if (!api().ok)
            return c;
        std::uint64_t v[6] = {};
        api().counts(v);
        c.mallocs = v[0];
        c.frees = v[1];
        c.mutexLocks = v[2];
        c.unfairLocks = v[3];
        c.writes = v[4];
        c.machMsgs = v[5];
        return c;
    }
} // namespace fcmp::probe::rt

#endif
