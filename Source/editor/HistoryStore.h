// Source/editor/HistoryStore.h — the UI side of the history (02 §6.5, §9.2, §9.6; 01 §6.3): 20 480 one-millisecond
// HistoryColumns (20.48 s >= the longest 20 s span; 655 KB per editor), drained from the processor's HistoryRing once per
// frame by the Panel, read by HISTORY, CONTROL PATH and the trail. Declared by U1a and frozen at FZ4; U2 (S7) owns it
// and may add to it.
//
// Audio time, not wall time: the store advances only when columns arrive. A lap (the ring overwrote columns this store
// never read; HistoryRing::read returned more than `from`) becomes ONE gap marker — a column with bits.b5 set and the
// levels at the floor — so the plots draw an empty span and never interpolate across it. The ring's own attach gap
// (bits.b5 on the first column after an attach) arrives as data and is kept. The first drain starts wherever the ring
// is (the time before the editor opened is not a gap).
//
// Message thread only (one reader of the SPSC ring). drain() never allocates; the buffers are allocated once here.
#pragma once

#include "fcdsp/telemetry/HistoryRing.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class HistoryStore
    {
    public:
        static constexpr uint32_t kCapacity = 20480;             // columns (1 ms each)
        static constexpr float    kFloorDb  = -200.0f;           // a gap marker's levels (the meters' floor, 01 §6.2)
        static constexpr uint32_t kGapBit   = 1u << 5;           // HistoryColumn::bits b5

        HistoryStore() : cols_(kCapacity), scratch_(fcdsp::HistoryRing::kCapacity) {}

        HistoryStore(const HistoryStore&) = delete;
        HistoryStore& operator=(const HistoryStore&) = delete;

        // Copies every column the ring holds past the last drain (at most the ring's capacity), oldest first.
        void drain(const fcdsp::HistoryRing& ring) noexcept
        {
            uint32_t n = 0;
            const uint64_t first = ring.read(ringNext_, std::span<fcdsp::HistoryColumn>(scratch_), n);
            if (first > ringNext_ && started_ && !lastWasMarker_)
            {
                push(gapColumn());
                ++gaps_;
                lastWasMarker_ = true;
            }
            for (uint32_t i = 0; i < n; ++i)
            {
                push(scratch_[i]);
                lastWasMarker_ = false;
            }
            if (n > 0 || first > ringNext_)
                ringNext_ = first + n;
            started_ = true;
        }

        uint64_t count() const noexcept { return count_; }       // columns stored so far (monotonic; gaps included)
        uint64_t oldest() const noexcept { return count_ > kCapacity ? count_ - kCapacity : 0; }
        bool     empty() const noexcept { return count_ == 0; }

        // Column `index`, oldest() <= index < count(); the newest is count() − 1.
        const fcdsp::HistoryColumn& at(uint64_t index) const noexcept
        {
            return cols_[static_cast<std::size_t>(index % kCapacity)];
        }

        static bool isGap(const fcdsp::HistoryColumn& c) noexcept { return (c.bits & kGapBit) != 0; }
        uint32_t gaps() const noexcept { return gaps_; }         // gap markers this store inserted (laps)
        uint64_t ringNext() const noexcept { return ringNext_; } // the next HistoryRing index it will read

        void clear() noexcept                                    // forget everything; keep reading where the ring is
        {
            count_ = 0;
            gaps_ = 0;
            lastWasMarker_ = false;
        }

    private:
        static fcdsp::HistoryColumn gapColumn() noexcept
        {
            fcdsp::HistoryColumn c{};
            c.inPeakDb = c.outPeakDb = c.detMaxDb = kFloorDb;
            c.grMaxDb = c.grMinDb = c.tgtMaxDb = 0.0f;
            c.internal0 = 0.0f;
            c.bits = kGapBit;
            return c;
        }

        void push(const fcdsp::HistoryColumn& c) noexcept
        {
            cols_[static_cast<std::size_t>(count_ % kCapacity)] = c;
            ++count_;
        }

        std::vector<fcdsp::HistoryColumn> cols_;
        std::vector<fcdsp::HistoryColumn> scratch_;
        uint64_t count_ = 0;
        uint64_t ringNext_ = 0;
        uint32_t gaps_ = 0;
        bool     started_ = false;
        bool     lastWasMarker_ = false;                         // one marker per gap
    };
}
