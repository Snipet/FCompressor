#pragma once

// Seqlock<T>: one writer, any number of readers, no locks, no allocation (01 §6.1): HardwareReverb's
// publishUiFrame/readUiFrame pattern (B §3), generalised. T is copied through an array of atomic words, so a reader
// never observes a data race, and a torn copy is detected by the sequence word and discarded.
// Implemented here (header-only). Sprint-frozen.
//
// Ordering: the writer makes seq odd, then (release fence) stores the words, then (release fence) makes seq even. A
// reader that observed any word of a later publish has, through its acquire fence, also observed that publish's odd
// seq store, so its re-check fails and the copy is discarded.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace fcdsp {

template <class T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 4 == 0);
    static constexpr int kWords = sizeof(T) / 4;
    alignas(64) std::atomic<uint32_t> seq_{0};
    std::array<std::atomic<uint32_t>, kWords> words_{};

public:
    // single writer: memcpy->uint32_t[]; seq+1 (release, odd); release fence; relaxed stores; release fence; seq+1 (even)
    void publish(const T& value) noexcept {
        uint32_t w[kWords];
        std::memcpy(w, &value, sizeof(T));
        const uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1u, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < kWords; ++i)
            words_[static_cast<std::size_t>(i)].store(w[i], std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2u, std::memory_order_release);
    }

    // any reader, <= 8 attempts: acquire seq (skip odd); relaxed loads; acquire fence; relaxed re-check; memcpy.
    // false = contention: caller keeps its previous frame
    bool read(T& out) const noexcept {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t before = seq_.load(std::memory_order_acquire);
            if ((before & 1u) != 0u)
                continue;                                       // a publish is in progress
            uint32_t w[kWords];
            for (int i = 0; i < kWords; ++i)
                w[i] = words_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) != before)
                continue;                                       // overwritten while copying
            std::memcpy(&out, w, sizeof(T));
            return true;
        }
        return false;
    }
};

} // namespace fcdsp
