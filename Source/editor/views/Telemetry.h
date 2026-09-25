// Source/editor/views/Telemetry.h — how every view reads the telemetry, including when the audio stops (UF1a, S11;
// ADR-69, ADR-70). Shared by DisplayRow, HistoryPlot, ControlPathPlot, TransferPlot, MeterColumn, Readouts, SlotGrid,
// SidechainPlot, CharScreen and SlotModel, so they agree on one rule each.
//
// ADR-69 (the user's Ableton test): when the host stops calling processBlock the stream goes stale (FrameState::fresh is
// false 0.5 s after publishCount stops moving), and the panel must never look disabled or frozen. A view reads the feed
// in one of three states (feed()):
// - none   no UiFrame has been read by this editor yet, or PanelOptions::ignoreLive: the rest look. Readouts print "–"
//          (U+2013) in ink16, nothing live is drawn, HISTORY is empty. The only state that prints "–" for a value.
// - fresh  frames arrive, with signal (kUiLive) or silent: everything is drawn from the frames; HISTORY follows the store
//          (audio time, 02 §6.5).
// - stale  frames stopped: nothing dims — controls, labels, captions, chrome and curves keep their inks; HISTORY and
//          CONTROL PATH keep scrolling at wall-clock rate over a gap (HistoryTimeline); meters and bars fall to the floor
//          at 20 dB/s; readouts print the last frame at rest (atRest: GR 0.0, levels −∞, crest 0, phase IDLE; effective
//          times and Mode internals hold, as the engine does while it is not called); the operating dot, GR needle,
//          trail and target ring fade out where they were (layout::live::kFadeS).
// The frame rate: full while kUiLive (Panel), while something visibly moves (a scroll with data in view, a falling
// meter, a fade) and for layout::live::kActiveS after any input (DisplayRow); else the idle rate.
//
// Also here:
// - operatingPoint / operatingX: the operating point's 10 ms peak envelope (U2): UiFrame::curveXDb is the detector's
//   value at the block's last sample, which for a peak law swings with the waveform's phase, so the TRANSFER dot, the
//   READOUTS DET row, and the THRESHOLD slot's DET readout and tick (U1s follow-up) all take the max of it and the newest
//   10 ms of history columns' detMaxDb — none of them jitters.
// - grHoldDb: the GAIN REDUCTION readout (ADR-70), the max GR over the last layout::display::kGrHoldS of wall-clock
//   time: the store's newest columns (1 ms of audio each) and the newest frame, the stale time counting as silence.
#pragma once

#include "editor/SubView.h"

#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <array>
#include <cstdint>

namespace fcmp::ui
{
    class HistoryStore;
}

namespace fcmp::ui::telemetry
{
    inline constexpr float kFloorDb = -200.0f;                   // the telemetry floor (01 §6.2)

    enum class Feed : uint8_t { none, fresh, stale };

    Feed feed(const PanelContext&) noexcept;

    // The frame at rest (stale): every level at the floor (meters, SC, colour input, curveXDb), every GR 0 (target,
    // applied, block max, stage 2), crest 0, the phase bits idle and the per-block flags (kUiLive, kUiOutOver,
    // kUiAutoSlow, kUiRangeLimited, kUiS2Active, kUiPoisonReset) clear. The rest holds: identity, the smoothed parameter
    // fields, the effective times, the internals and the state flags (bypass, delta, listen, ext key, M/S, topology).
    fcdsp::UiFrame atRest(const fcdsp::UiFrame&) noexcept;

    // The lane the operating dot follows: the one with the larger applied GR (02 §6.5).
    inline int grLane(const fcdsp::UiFrame& u) noexcept { return u.appliedGrDb[1] > u.appliedGrDb[0] ? 1 : 0; }

    struct OperatingPoint
    {
        int   lane = 0;
        float x = kFloorDb;                                      // curveXDb of the lane, peak-held over 10 ms
        float target = 0.0f;                                     // targetGrDb of the lane, peak-held likewise (>= 0
                                                                 // not enforced: the caller clamps)
    };
    inline constexpr uint64_t kEnvelopeMs = 10;                  // the trail's step (02 §6.5)

    // The operating point of frame u over the store's newest 10 columns (gap columns skipped).
    OperatingPoint operatingPoint(const fcdsp::UiFrame& u, const HistoryStore&) noexcept;

    // The same x over the ring's newest 10 columns (SlotModel reaches the ring, not the store; the Panel drains the store
    // from the ring before any view ticks, so both see the same columns unless a block landed in between).
    float operatingX(const fcdsp::UiFrame& u, const fcdsp::HistoryRing&) noexcept;

    // ADR-70: the GAIN REDUCTION readout, >= 0 dB. The max of grMaxDb over the store's newest (kGrHoldS − stale time)
    // columns and, while the newest frame is younger than kGrHoldS, its applied and block-max GR (the samples after the
    // newest complete column). 0 when the feed is none.
    float grHoldDb(const PanelContext&) noexcept;

    // HISTORY's and CONTROL PATH's time axis (ADR-69). The HistoryStore is audio time (1 ms of audio per entry); the
    // plots draw a timeline in ms whose "now" is head():
    // - Before the first tick that sees a frame and a stored entry (and always under PanelOptions::ignoreLive) nothing is
    //   mapped and head() is the store's count: an empty plot that does not move. A plot first ticked after the audio
    //   stopped (it was on the other screen) starts on the clock a plot ticked all along would show.
    // - Fresh: head() = count + the current offset, so the strip advances with the audio (the offset is 0 until the
    //   first stale span, so a session that never stalls draws exactly 02 §6.5's audio-time strip).
    // - Stale: head() advances at wall-clock rate (PanelContext::seconds), over time no entry maps to: a gap.
    // - Fresh again: the entries that arrive are placed to end at the wall-clock head, so the stale span stays a gap
    //   between the old audio and the new (a new segment with a larger offset). Offsets never decrease, so head() never
    //   moves backwards and no two entries share a position.
    // The stale clock starts when the feed turns stale (0.5 s after the last frame), so the strip rests for that half
    // second and then scrolls. A plot that was not ticked (the other screen) catches up from panel time on its next tick.
    class HistoryTimeline
    {
    public:
        void tick(const PanelContext&) noexcept;                 // once per frame, before the plot reads it

        uint64_t head() const noexcept { return head_; }         // "now" on the timeline (ms)
        bool     started() const noexcept { return n_ > 0; }

        // fn(e0, e1, offset) for the store entries [e0, e1) whose timeline positions (entry + offset) lie in [t0, t1),
        // oldest first. Entries older than the store's oldest are the caller's to skip.
        template <class Fn>
        void forEntries(int64_t t0, int64_t t1, Fn&& fn) const
        {
            for (int k = 0; k < n_; ++k)
            {
                const int64_t off = seg_[static_cast<std::size_t>(k)].offset;
                const auto e0 = static_cast<int64_t>(seg_[static_cast<std::size_t>(k)].e0);
                const auto e1 = static_cast<int64_t>(k + 1 < n_ ? seg_[static_cast<std::size_t>(k + 1)].e0 : count_);
                const int64_t lo = t0 - off > e0 ? t0 - off : e0;
                const int64_t hi = t1 - off < e1 ? t1 - off : e1;
                if (lo < hi)
                    fn(lo, hi, off);
            }
        }

        // True when [t0, t1) holds timeline ms that no entry maps to after the first entry: a stale span (a gap).
        bool gapIn(int64_t t0, int64_t t1) const noexcept;

    private:
        struct Segment
        {
            uint64_t e0 = 0;                                     // the first store entry of this segment ...
            int64_t  offset = 0;                                 // ... and every later one sits at entry + offset
        };
        static constexpr int kMaxSegments = 64;                  // one per stale span in view (>= 0.5 s each)
        static constexpr int64_t kKeepMs = 20480;                // HistoryStore::kCapacity: older segments are out of view

        void push(uint64_t e0, int64_t offset) noexcept;

        std::array<Segment, kMaxSegments> seg_{};
        int      n_ = 0;
        uint64_t count_ = 0;                                     // the store's count at the last tick
        uint64_t head_ = 0;
        double   exact_ = 0.0;                                   // the wall clock while stale, fractional ms
        double   seconds_ = -1.0;                                // panel time of the last tick (-1: none)
        bool     fresh_ = false;                                 // the feed was fresh at the last tick
    };
}
