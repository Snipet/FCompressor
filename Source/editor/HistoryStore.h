// Source/editor/HistoryStore.h — the UI side of the history (02 §6.5, §9.2, §9.6; 01 §6.3): 20 480 one-millisecond
// HistoryColumns (20.48 s >= the longest 20 s span; 655 KB per editor), drained from the processor's HistoryRing once per
// frame by the Panel, read by HISTORY, CONTROL PATH and the trail. Declared by U1a and frozen at FZ4; U2 (S7) owns it
// and may add to it; U3 (S9, lead revision 5b) adds the public column-window rule HISTORY and CONTROL PATH share.
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

#include <algorithm>
#include <cmath>
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

        // ---- U3 addition (S9, lead revision 5b; public and additive, no FZ4 declaration above changed) ---------------

        // One plot column over the store's 1 ms entries: it aggregates entries [e0, e1) and spans [xl, xr) (logical px,
        // clipped to the plot).
        struct ColumnWindow
        {
            int64_t e0 = 0, e1 = 0;
            float   xl = 0.0f, xr = 0.0f;
        };

        // The column-window rule of HISTORY (HistoryPlot.cpp, U2), public so CONTROL PATH lines up with it column for
        // column (02 §6.5, §7.3, §9.6): W = spanTenths · 100 / columns ms per column, cut at ABSOLUTE multiples of W over
        // the entries (W is fractional on the Characteristics screen), so a complete column's content never changes and
        // only the newest (partial) one grows; "now" (entry `head`) is the plot's right edge and the strip is offset by
        // the partial column's phase (the smooth scroll). Writes the windows that hold at least one entry, oldest first:
        // at most out.size() (the newest out.size() − 1 complete columns and the partial one; HistoryPlot passes 256).
        // Returns the count; *pxPerMs, when not null, receives colWidth / W. Same arithmetic as HistoryPlot's, in double.
        static int columnWindows(uint64_t head, int spanTenths, int columns, float colWidth, float left, float width,
                                 std::span<ColumnWindow> out, double* pxPerMs = nullptr) noexcept
        {
            if (spanTenths <= 0 || columns <= 0 || !(colWidth > 0.0f) || out.size() < 2)
                return 0;
            const float right = left + width;
            const double w = static_cast<double>(spanTenths) * 100.0 / static_cast<double>(columns);   // ms per column
            const auto colW = static_cast<double>(colWidth);
            if (pxPerMs != nullptr)
                *pxPerMs = colW / w;
            const auto hd = static_cast<double>(head);
            const double k = std::floor(hd / w);                  // complete columns since the store began
            const double phase = (hd - k * w) / w;                // [0, 1): the smooth scroll
            const int jMax = std::min(static_cast<int>(std::ceil(static_cast<double>(width) / colW - phase)) - 1,
                                      static_cast<int>(out.size()) - 2);
            int n = 0;
            for (int j = jMax; j >= -1; --j)                      // oldest first; j = −1 is the partial column
            {
                double a = 0.0, b = 0.0, xr = 0.0, xl = 0.0;
                if (j >= 0)
                {
                    a = (k - 1.0 - j) * w;
                    b = (k - j) * w;
                    xr = static_cast<double>(right) - (phase + j) * colW;
                    xl = xr - colW;
                }
                else
                {
                    a = k * w;
                    b = hd;
                    xl = static_cast<double>(right) - phase * colW;
                    xr = static_cast<double>(right);
                }
                if (b <= 0.0 || xr <= static_cast<double>(left))
                    continue;                                     // before the store began, or left of the plot
                const auto e0 = static_cast<int64_t>(std::ceil(a));
                const auto e1 = j >= 0 ? static_cast<int64_t>(std::ceil(b)) : static_cast<int64_t>(head);
                if (e1 <= e0)
                    continue;                                     // an empty partial column
                ColumnWindow& c = out[static_cast<std::size_t>(n++)];
                c.e0 = e0;
                c.e1 = e1;
                c.xl = std::max(static_cast<float>(xl), left);
                c.xr = std::min(static_cast<float>(xr), right);
            }
            return n;
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
