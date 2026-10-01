// Tools/probes/common/AllocCounter.cpp: replacement global operator new/delete that count the calls made by one armed
// thread (fcmp::probe::alloc, declared in ProbeRegistry.h). Linked into every probe executable through the common glob;
// dsp.rt arms it around each process() call and requires 0 allocations (03 §3.4, §3.7).
//
// Only the armed thread is counted, so a JUCE message thread or any other thread never pollutes a count. The counters
// are lock-free atomics and the bookkeeping never allocates. malloc() from C code bypasses operator new; the rtsan
// preset (RealtimeSanitizer or RtInterposer.cpp) covers that and locks and syscalls (K2 #18).
#include "ProbeRegistry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <pthread.h>

namespace
{
    // No armed thread: a value-initialised pthread_t, null on macOS (a pointer) and 0 on Linux (an unsigned long),
    // which no running thread ever has (ADR-92).
    constexpr pthread_t kNoThread{};

    constinit std::atomic<pthread_t> gArmed{kNoThread};
    constinit std::atomic<std::uint64_t> gAllocations{0};
    constinit std::atomic<std::uint64_t> gDeallocations{0};
    constinit std::atomic<std::uint64_t> gBytes{0};
    static_assert(std::atomic<pthread_t>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);

    bool counting() noexcept
    {
        const pthread_t armed = gArmed.load(std::memory_order_relaxed);
        return armed != kNoThread && pthread_equal(armed, pthread_self()) != 0;
    }

    void noteAllocation(std::size_t n) noexcept
    {
        if (counting())
        {
            gAllocations.fetch_add(1, std::memory_order_relaxed);
            gBytes.fetch_add(n, std::memory_order_relaxed);
        }
    }

    void* allocate(std::size_t n)
    {
        noteAllocation(n);
        const std::size_t size = n != 0 ? n : 1;
        for (;;)
        {
            if (void* p = std::malloc(size))
                return p;
            const std::new_handler handler = std::get_new_handler();
            if (handler == nullptr)
                throw std::bad_alloc();
            handler();
        }
    }

    void* allocateAligned(std::size_t n, std::align_val_t alignment)
    {
        noteAllocation(n);
        std::size_t align = static_cast<std::size_t>(alignment);
        if (align < sizeof(void*))
            align = sizeof(void*);
        const std::size_t size = n != 0 ? n : 1;
        for (;;)
        {
            void* p = nullptr;
            if (posix_memalign(&p, align, size) == 0)
                return p;
            const std::new_handler handler = std::get_new_handler();
            if (handler == nullptr)
                throw std::bad_alloc();
            handler();
        }
    }

    void deallocate(void* p) noexcept
    {
        if (p == nullptr)
            return;
        if (counting())
            gDeallocations.fetch_add(1, std::memory_order_relaxed);
        std::free(p);
    }
} // namespace

namespace fcmp::probe::alloc
{
    void arm() noexcept { gArmed.store(pthread_self(), std::memory_order_relaxed); }
    void disarm() noexcept { gArmed.store(kNoThread, std::memory_order_relaxed); }

    void reset() noexcept
    {
        gAllocations.store(0, std::memory_order_relaxed);
        gDeallocations.store(0, std::memory_order_relaxed);
        gBytes.store(0, std::memory_order_relaxed);
    }

    std::uint64_t allocations() noexcept { return gAllocations.load(std::memory_order_relaxed); }
    std::uint64_t deallocations() noexcept { return gDeallocations.load(std::memory_order_relaxed); }
    std::uint64_t bytes() noexcept { return gBytes.load(std::memory_order_relaxed); }
} // namespace fcmp::probe::alloc

// ---- the replaceable global allocation functions ([new.delete]) ------------------------------------------------------
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, std::align_val_t a) { return allocateAligned(n, a); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocateAligned(n, a); }

void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try { return allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept
{
    try { return allocate(n); } catch (...) { return nullptr; }
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    try { return allocateAligned(n, a); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    try { return allocateAligned(n, a); } catch (...) { return nullptr; }
}

void operator delete(void* p) noexcept { deallocate(p); }
void operator delete[](void* p) noexcept { deallocate(p); }
void operator delete(void* p, std::size_t) noexcept { deallocate(p); }
void operator delete[](void* p, std::size_t) noexcept { deallocate(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { deallocate(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { deallocate(p); }
void operator delete(void* p, std::align_val_t) noexcept { deallocate(p); }
void operator delete[](void* p, std::align_val_t) noexcept { deallocate(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { deallocate(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { deallocate(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { deallocate(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { deallocate(p); }
