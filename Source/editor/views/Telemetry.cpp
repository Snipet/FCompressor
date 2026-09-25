// Source/editor/views/Telemetry.cpp — the telemetry states, the frame at rest, the operating point's envelope, the
// GAIN REDUCTION hold and HISTORY's timeline (see Telemetry.h; ADR-69, ADR-70).
#include "editor/views/Telemetry.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace fcmp::ui::telemetry
{
    namespace
    {
        constexpr uint32_t kPhaseBits = 0xFu << 16;              // UiFrame::flags bits 16–19: both lanes' phases
        constexpr uint32_t kBlockFlags = fcdsp::kUiLive | fcdsp::kUiOutOver | fcdsp::kUiAutoSlow | fcdsp::kUiRangeLimited
                                       | fcdsp::kUiS2Active | fcdsp::kUiPoisonReset | kPhaseBits;

        float maxLane(const float (&v)[2]) noexcept { return std::max(v[0], v[1]); }
    }

    Feed feed(const PanelContext& ctx) noexcept
    {
        const FrameState& f = ctx.frame;
        if (!f.hasFrame || ctx.options.ignoreLive)
            return Feed::none;
        return f.fresh ? Feed::fresh : Feed::stale;
    }

    fcdsp::UiFrame atRest(const fcdsp::UiFrame& u) noexcept
    {
        fcdsp::UiFrame r = u;
        for (int i = 0; i < 2; ++i)
        {
            const auto k = static_cast<std::size_t>(i);
            r.inPeakDb[k] = r.inRmsDb[k] = r.outPeakDb[k] = r.outRmsDb[k] = kFloorDb;
            r.scPeakDb[k] = r.colourInPeakDb[k] = r.curveXDb[k] = kFloorDb;
            r.targetGrDb[k] = r.appliedGrDb[k] = r.blockMaxGrDb[k] = r.s2GrDb[k] = 0.0f;
            r.crestDb[k] = 0.0f;
        }
        r.flags &= ~kBlockFlags;
        return r;
    }

    OperatingPoint operatingPoint(const fcdsp::UiFrame& u, const HistoryStore& h) noexcept
    {
        OperatingPoint p;
        p.lane = grLane(u);
        const auto ul = static_cast<std::size_t>(p.lane);
        p.x = u.curveXDb[ul];
        p.target = u.targetGrDb[ul];
        const uint64_t count = h.count();
        const uint64_t e0 = std::max(count >= kEnvelopeMs ? count - kEnvelopeMs : 0, h.oldest());
        for (uint64_t e = e0; e < count; ++e)
            if (const fcdsp::HistoryColumn& c = h.at(e); !HistoryStore::isGap(c))
            {
                p.x = std::max(p.x, c.detMaxDb);
                p.target = std::max(p.target, c.tgtMaxDb);
            }
        return p;
    }

    float operatingX(const fcdsp::UiFrame& u, const fcdsp::HistoryRing& ring) noexcept
    {
        float x = u.curveXDb[static_cast<std::size_t>(grLane(u))];
        const uint64_t w = ring.written();
        std::array<fcdsp::HistoryColumn, kEnvelopeMs> cols{};
        uint32_t n = 0;
        ring.read(w >= kEnvelopeMs ? w - kEnvelopeMs : 0, std::span<fcdsp::HistoryColumn>(cols), n);
        for (uint32_t i = 0; i < n; ++i)
            if (!HistoryStore::isGap(cols[i]))
                x = std::max(x, cols[i].detMaxDb);
        return x;
    }

    float grHoldDb(const PanelContext& ctx) noexcept
    {
        if (feed(ctx) == Feed::none)
            return 0.0f;
        const FrameState& f = ctx.frame;
        const HistoryStore& h = ctx.history;
        const double holdMs = static_cast<double>(layout::display::kGrHoldS) * 1000.0;
        const double staleMs = static_cast<double>(std::max(f.staleSeconds, 0.0f)) * 1000.0;
        float gr = 0.0f;
        // The newest frame: its block's samples after the newest complete column (a lap marker or the attach column
        // counts too: a marker's GR is 0, the attach column's is audio).
        if (staleMs < holdMs)
            gr = std::max({ gr, maxLane(f.ui.appliedGrDb), maxLane(f.ui.blockMaxGrDb) });
        const auto window = static_cast<uint64_t>(std::max(0.0, std::round(holdMs - staleMs)));
        const uint64_t count = h.count();
        const uint64_t e0 = std::max(count >= window ? count - window : 0, h.oldest());
        for (uint64_t e = e0; e < count; ++e)
            gr = std::max(gr, h.at(e).grMaxDb);
        return std::isfinite(gr) ? std::max(gr, 0.0f) : 0.0f;
    }

    // ---- HistoryTimeline ------------------------------------------------------------------------------------------------

    void HistoryTimeline::push(uint64_t e0, int64_t offset) noexcept
    {
        if (n_ > 0 && seg_[static_cast<std::size_t>(n_ - 1)].e0 == e0)
        {
            seg_[static_cast<std::size_t>(n_ - 1)].offset = offset;   // the last segment holds no entry yet: move it
            return;
        }
        if (n_ == kMaxSegments)
        {
            std::move(seg_.begin() + 1, seg_.end(), seg_.begin());  // the oldest is long out of view
            --n_;
        }
        seg_[static_cast<std::size_t>(n_++)] = { e0, offset };
    }

    void HistoryTimeline::tick(const PanelContext& ctx) noexcept
    {
        const uint64_t count = ctx.history.count();
        const double elapsedMs = seconds_ >= 0.0 ? std::max(0.0, (ctx.seconds - seconds_) * 1000.0) : 0.0;
        seconds_ = ctx.seconds;
        const Feed fd = feed(ctx);
        if (count < count_)
            n_ = 0;                                              // the store was cleared: start over
        if (n_ == 0)
        {
            exact_ = static_cast<double>(count);
            if (fd != Feed::none && count > 0)
            {
                push(0, 0);                                      // audio time from the store's first entry
                // A plot first ticked after the audio stopped (the other screen was shown) starts where one ticked all
                // along would be: its stale clock started kStaleS after the last frame.
                if (fd == Feed::stale)
                    exact_ += std::max(0.0, static_cast<double>(ctx.frame.staleSeconds - layout::band::kStaleS)) * 1000.0;
            }
            head_ = static_cast<uint64_t>(std::floor(exact_));
            count_ = count;
            fresh_ = fd == Feed::fresh;
            return;
        }
        if (fd == Feed::fresh)
        {
            int64_t off = seg_[static_cast<std::size_t>(n_ - 1)].offset;
            if (!fresh_)
            {
                // Back from a stale span: what arrives ends at the wall-clock head, after the gap.
                const double now = exact_ + elapsedMs;
                const auto want = static_cast<int64_t>(std::ceil(now - static_cast<double>(count)));
                if (want > off)
                {
                    push(count_, want);
                    off = want;
                }
            }
            const int64_t h = static_cast<int64_t>(count) + off;
            head_ = std::max(head_, static_cast<uint64_t>(std::max<int64_t>(h, 0)));
            exact_ = static_cast<double>(head_);
        }
        else if (fd == Feed::stale)
        {
            exact_ += elapsedMs;
            head_ = std::max(head_, static_cast<uint64_t>(std::floor(exact_)));
        }
        count_ = count;
        fresh_ = fd == Feed::fresh;

        // Segments whose successor starts before the longest span are out of view.
        while (n_ >= 2)
        {
            const Segment& next = seg_[1];
            if (static_cast<int64_t>(next.e0) + next.offset + kKeepMs > static_cast<int64_t>(head_))
                break;
            std::move(seg_.begin() + 1, seg_.begin() + n_, seg_.begin());
            --n_;
        }
    }

    bool HistoryTimeline::gapIn(int64_t t0, int64_t t1) const noexcept
    {
        if (n_ == 0 || t1 <= t0)
            return false;
        const auto overlaps = [t0, t1](int64_t a, int64_t b) { return a < b && a < t1 && t0 < b; };
        for (int k = 0; k + 1 < n_; ++k)
        {
            const auto e = static_cast<int64_t>(seg_[static_cast<std::size_t>(k + 1)].e0);
            if (overlaps(e + seg_[static_cast<std::size_t>(k)].offset, e + seg_[static_cast<std::size_t>(k + 1)].offset))
                return true;
        }
        const int64_t tail = static_cast<int64_t>(count_) + seg_[static_cast<std::size_t>(n_ - 1)].offset;
        return overlaps(tail, static_cast<int64_t>(head_));
    }
}
