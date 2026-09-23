#pragma once

// HistoryRing: the 1 ms telemetry columns behind HISTORY and CONTROL PATH (01 §6.3; E §7, F §7.2). SPSC: the audio
// thread pushes while the editor attach count is > 0; one editor drains it once per frame. A column is 1 ms of audio
// time, cut at absolute sample boundaries, so the strip is independent of block size. Implemented here
// (header-only). Sprint-frozen.
//
// Tear-free protocol (K2 #8), a claim-word seqlock variant:
//   push(): j = written_; claim_ = j; release fence (the claim is ordered before the data); 8 relaxed word stores
//           into slot j & (kCapacity - 1); written_ = j + 1 (release).
//   read(): w = written_ (acquire); copy [max(from, w - kCapacity + 1), w) with relaxed loads; acquire fence;
//           c = claim_ (relaxed); keep column i iff i + kCapacity > c. A reader that saw any word of an overwrite of
//           column i (column i + kCapacity or later) also sees claim >= i + kCapacity, so a torn column is dropped,
//           never delivered.
// Writer rules: push only while attached; on a 0 -> 1 attach transition the host resets its column accumulator and
// sets bits.b5 on the next column; written_ is monotonic for the life of the host object and is never reset.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace fcdsp {

struct HistoryColumn {            // 32 bytes
    float inPeakDb;               // max over the column, both channels
    float outPeakDb;
    float detMaxDb;               // max curve-axis level
    float grMaxDb;                // most applied GR (>= 0)
    float grMinDb;                // least applied GR: max and min keep 1-ms spikes both ways (E §7)
    float tgtMaxDb;               // max static target GR
    float internal0;              // the Mode's history-flagged internal (last value), else 0
    uint32_t bits;                // b0-1 phase at the max-GR sample; b2 auto-slow; b3 range-limited; b4 s2 active;
                                  // b5 first column after attach (gap); b6 fading; b8-15 mode slot
};
static_assert(sizeof(HistoryColumn) == 32 && std::is_trivially_copyable_v<HistoryColumn>);

class HistoryRing {
public:
    static constexpr uint32_t kCapacity = 4096;          // 4.1 s; power of two; 128 KiB of atomic words
    static_assert((kCapacity & (kCapacity - 1)) == 0);

    // Audio thread ONLY (claim-word protocol above).
    void push(const HistoryColumn& c) noexcept {
        const uint64_t j = written_.load(std::memory_order_relaxed);
        claim_.store(j, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);          // the claim is ordered before the data
        uint32_t w[kColumnWords];
        std::memcpy(w, &c, sizeof(HistoryColumn));
        const std::size_t base = static_cast<std::size_t>(j & (kCapacity - 1)) * kColumnWords;
        for (std::size_t k = 0; k < kColumnWords; ++k)
            words_[base + k].store(w[k], std::memory_order_relaxed);
        written_.store(j + 1, std::memory_order_release);
    }

    uint64_t written() const noexcept { return written_.load(std::memory_order_acquire); }

    // Reader (one editor): copies columns [from, written()) into out, oldest first, at most out.size().
    // Returns the index of the first column delivered. It is > from if the writer lapped the reader (gap).
    // Columns the writer may have overwritten during the copy are dropped (claim check above), never delivered torn.
    // The next read starts at the returned index + count.
    uint64_t read(uint64_t from, std::span<HistoryColumn> out, uint32_t& count) const noexcept {
        count = 0;
        const uint64_t w = written_.load(std::memory_order_acquire);
        if (from >= w || out.empty())
            return from;
        const uint64_t oldest = w >= kCapacity ? w - kCapacity + 1 : 0;   // slot w & mask may be being written
        const uint64_t first = std::max(from, oldest);
        const std::size_t n = static_cast<std::size_t>(std::min<uint64_t>(w - first, out.size()));
        for (std::size_t k = 0; k < n; ++k)
            load(first + k, out[k]);
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t c = claim_.load(std::memory_order_relaxed);
        // keep column i iff i + kCapacity > c: every column below `keep` may be torn
        const uint64_t keep = c >= kCapacity ? std::max(first, c - kCapacity + 1) : first;
        if (keep >= first + n)
            return keep;                                  // all copied columns may be torn: deliver none
        const std::size_t drop = static_cast<std::size_t>(keep - first);
        if (drop > 0)
            std::copy(out.begin() + static_cast<std::ptrdiff_t>(drop), out.begin() + static_cast<std::ptrdiff_t>(n),
                      out.begin());
        count = static_cast<uint32_t>(n - drop);
        return keep;
    }

private:
    static constexpr std::size_t kColumnWords = sizeof(HistoryColumn) / 4;   // 8

    void load(uint64_t i, HistoryColumn& out) const noexcept {
        uint32_t w[kColumnWords];
        const std::size_t base = static_cast<std::size_t>(i & (kCapacity - 1)) * kColumnWords;
        for (std::size_t k = 0; k < kColumnWords; ++k)
            w[k] = words_[base + k].load(std::memory_order_relaxed);
        std::memcpy(&out, w, sizeof(HistoryColumn));
    }

    std::array<std::atomic<uint32_t>, kCapacity * 8> words_{};
    alignas(64) std::atomic<uint64_t> written_{0};
    alignas(64) std::atomic<uint64_t> claim_{0};         // index of the column being written (K2 #8)
};

} // namespace fcdsp
