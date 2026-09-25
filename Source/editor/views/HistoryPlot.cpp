// Source/editor/views/HistoryPlot.cpp — HISTORY (see HistoryPlot.h; 02 §6.5, §7.3, §9.2, §9.6): the columns rebuilt
// in tick() from the HistoryStore over the wall-clock timeline (draw() only emits), the four traces, the grid, the
// threshold line and its drag, the state lane, Mode ticks and gaps, press-and-hold freeze and the span cells; on the
// band, the HISTORY · VU switch and the GR VU meter it shows instead of the traces (UF2, ADR-72).
#include "editor/views/HistoryPlot.h"

#include "editor/HistoryStore.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"
#include "editor/views/GrVuMeter.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/widgets/RuleSlider.h>
#include <funkgui/widgets/SegmentedSelector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;

        constexpr const char* kSpanKey = "historySpanTenths";    // UiPreferences (02 §5.9)
        constexpr int   kMaxColumns = 256;                         // >= every geometry's columns + 2
        constexpr int   kMaxSamples = 3 * kMaxColumns + 8;         // strip samples: centres + two ends per run
        constexpr int   kMaxRuns = kMaxColumns / 2 + 2;
        constexpr int   kMaxTicks = 16;                            // Mode boundary ticks drawn
        constexpr float kGrFill = 0.18f;                           // HIST_GR fill: premix(ground, signal, 0.18)
        constexpr float kGrStroke = 1.5f;
        constexpr float kGapDotStep = 3.0f;
        constexpr float kGapDotDy = 3.0f;                          // the GAP dots, above the plot's bottom edge
        constexpr float kTitleS = 0.25f;                           // a11y title <= 4 Hz (02 §7.5, §9.6)
        constexpr float kInkGrDb = 0.01f;                          // a column with less GR draws no GR ink
        constexpr float kTrackPx = 240.0f;                         // RuleSlider: 240 px per full track ...
        constexpr float kPitchMin = 24.0f, kPitchMax = 64.0f;     // ... stepped: clamp(240/(n−1), 24, 64) px per detent
        constexpr uint32_t kImageId = 1, kGroupId = 2, kLineId = 7; // local ids (cells: kGroupId + 1 + i)
        constexpr uint32_t kViewGroupId = 8, kVuId = 11;           // UF2: the GR view group (cells 9, 10), the meter
        constexpr const char* kViewKey = "grView";                 // UiPreferences (ADR-72): 0 HISTORY, 1 VU
        constexpr uint32_t kSlotShift = 8, kSlotMask = 0xFFu << kSlotShift;
        constexpr uint32_t kPhaseMask = 3u;
        constexpr funkgui::Col kClear { 0, 0, 0, 0 };

        constexpr const char* kSpanSpec = "HISTORY SPAN   2.5 · 5 · 10 · 20 S   CLICK A SPAN (EVERY WINDOW)";
        constexpr const char* kViewSpec = "GR VIEW   HISTORY · VU   CLICK A VIEW (EVERY WINDOW)";

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::HistoryGeom& g) noexcept
        {
            const float top = g.spanCells[0].y;
            return { g.plot.x, top, g.plot.w, g.timeLabelY + 14.0f - top };
        }

        // UF2 (ADR-72): the band's HISTORY has the HISTORY · VU switch; the Characteristics screen's does not.
        bool isBand(const layout::HistoryGeom& g) noexcept
        {
            const funkgui::Rect& a = g.plot;
            const funkgui::Rect& b = layout::kBandHistory.plot;
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        std::vector<funkgui::CellText> viewTexts()
        {
            return { { "HISTORY", "History", "The scrolling gain reduction history" },
                     { "VU", "VU meter", "One needle meter of gain reduction, with VU ballistics" } };
        }

        // The TRANSFER plot the threshold line runs into: the one on this plot's level map (02 §6.5, §7.3).
        const layout::TransferGeom& transferOf(const layout::HistoryGeom& g) noexcept
        {
            const layout::LevelMap& b = layout::kBandTransfer.level;
            return g.level.top == b.top && g.level.height == b.height ? layout::kBandTransfer : layout::kCharsTransfer;
        }

        int spanTenths(int v) noexcept
        {
            for (const int s : layout::kSpansTenths)
                if (s == v)
                    return v;
            return layout::kDefaultSpanTenths;
        }

        std::vector<funkgui::CellText> spanTexts()
        {
            return { { "2.5", "2.5 seconds", nullptr }, { "5", "5 seconds", nullptr },
                     { "10", "10 seconds", nullptr }, { "20", "20 seconds", nullptr } };
        }

        // 02 §6.5: HIST_DET only where it can differ from IN (SC HPF on, SC EMPH ≠ 0, a non-peak law, an external key,
        // link < 1, or a feedback topology). The key and the topology are the last frame's (ADR-69: they do not change
        // when the audio stops).
        bool detCanDiffer(const PanelContext& ctx) noexcept
        {
            const FrameState& f = ctx.frame;
            if (f.entry == nullptr)
                return false;
            const fcdsp::EngineParams& e = f.eng;
            const bool law = f.entry->desc->detectorLaw != nullptr
                          && f.entry->desc->detectorLaw(e) != fcdsp::DetectorLaw::peak;
            const bool known = telemetry::feed(ctx) != telemetry::Feed::none;
            const bool ext = known && (f.ui.flags & fcdsp::kUiExtKeyActive) != 0;
            const bool fb = e.topo == fcdsp::kTopoFB || (known && (f.ui.flags & fcdsp::kUiTopoFB) != 0);
            return e.scHpfHz > 0.0f || e.sceDbOct != 0.0f || law || ext || e.link < 1.0f || fb;
        }

        // One time label (02 §6.5: every timeLabelPitch px from "now"): "0", "−1", "−2.5"; the leftmost adds " S".
        void timeLabel(char* out, std::size_t n, float seconds, bool unit) noexcept
        {
            const float r = std::round(seconds);
            const int dp = std::fabs(seconds - r) < 1e-3f ? 0 : 1;
            if (funkgui::fmt::db(seconds, dp, out, n) < 0)
                out[0] = '\0';
            if (unit)
                std::strncat(out, " S", n - std::strlen(out) - 1);
        }

        bool writable(funkgui::ValueState s) noexcept
        {
            return s == funkgui::ValueState::continuous || s == funkgui::ValueState::stepped;
        }
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct HistoryPlot::State
    {
        struct Column
        {
            float    xl = 0.0f, xr = 0.0f;                        // clipped to the plot
            float    in = 0.0f, out = 0.0f, det = 0.0f, gr = 0.0f, grMin = 0.0f, tgt = 0.0f, internal = 0.0f;
            uint32_t bits = 0;                                    // the max-GR entry's bits
            bool     data = false;                                // holds at least one data entry
            bool     gap = false;                                 // holds a gap entry
            bool     valid() const noexcept { return data && !gap; }
        };
        struct Run { int first = 0, count = 0; };                // strip samples of one run of valid columns
        struct Span { float x0 = 0.0f, x1 = 0.0f; uint32_t phase = 0; };

        State(PanelContext& ctx, const layout::HistoryGeom& g, uint32_t idBase)
            : spanModel(kSpanKey, { 25, 50, 100, 200 }, spanTexts(), layout::kDefaultSpanTenths),
              span(spanModel, std::vector<funkgui::Rect>(g.spanCells.begin(), g.spanCells.end()),
                   funkgui::CellStyle::text, nullptr, funkgui::Point{}, idBase + kGroupId),
              thr(ctx.slot(fcdsp::Pid::thr), layout::slotGeom(*layout::slotOf(fcdsp::Pid::thr)), idBase + kLineId),
              hasView(isBand(g)),
              viewModel(kViewKey, { layout::vu::kHistory, layout::vu::kVu }, viewTexts(), layout::vu::kDefaultView),
              view(viewModel, std::vector<funkgui::Rect>(layout::vu::kViewCells.begin(), layout::vu::kViewCells.end()),
                   funkgui::CellStyle::text, nullptr, funkgui::Point{}, idBase + kViewGroupId)
        {
            span.setSpokenTitle("History span");
            view.setSpokenTitle("GR view");
            if (hasView)
                vu = std::make_unique<GrVuMeter>(ctx, idBase + kVuId);
        }

        void rebuild(const PanelContext&, const layout::HistoryGeom&) noexcept;
        void aggregate(const HistoryStore&, int64_t t0, int64_t t1, Column&, float right) noexcept;
        void take(const fcdsp::HistoryColumn&, int64_t pos, Column&, float right) noexcept;
        const Column* columnAt(float x) const noexcept;
        void syncView(PanelContext&) noexcept;                    // UF2: follow the preference (tick, cells, keys)

        funkgui::PrefCells         spanModel;
        funkgui::SegmentedSelector span;
        funkgui::RuleSlider        thr;                           // never drawn: THRESHOLD's spec line and detents

        // UF2 (ADR-72): the HISTORY · VU switch and the meter, on the band only (hasView).
        const bool                 hasView;
        funkgui::PrefCells         viewModel;
        funkgui::SegmentedSelector view;
        std::unique_ptr<GrVuMeter> vu;
        bool     vuShown = false;                                 // VU is shown (the preference as of the last tick)
        bool     viewHover = false;
        uint32_t revision = 0;                                    // bumps when VU is shown or hidden (a11y structure)

        bool showVu() const noexcept { return hasView && viewModel.value() == layout::vu::kVu; }

        // the displayed columns, left to right, and what tick() derived from them for draw()
        std::array<HistoryStore::ColumnWindow, kMaxColumns> windows{};   // HistoryStore::columnWindows' output
        std::array<Column, kMaxColumns> cols{};
        int nCols = 0;
        std::array<float, kMaxSamples> xs{}, yIn{}, yOut{}, yDet{}, yTop{}, yGr{};
        std::array<Run, kMaxRuns> runs{};
        int nRuns = 0;
        std::array<Span, kMaxRuns> lanes{};                       // merged phase runs, newest first
        int nLanes = 0;
        std::array<Span, kMaxRuns> gaps{};
        int nGaps = 0;
        std::array<float, kMaxTicks> ticks{};                     // Mode boundary x
        int nTicks = 0;
        int64_t lastSlot = -1;                                    // aggregate(): the previous data entry's slot

        telemetry::HistoryTimeline timeline;                      // audio time, wall clock while stale (ADR-69)
        uint64_t head = 0;                                        // the timeline ms "now" stands for (held by a freeze)
        uint64_t builtHead = ~uint64_t{ 0 }, builtCount = 0;
        int      builtSpan = 0, builtScale = 0;
        double   pxPerMs = 0.0;
        bool     moving = false;                                  // ink in view that the next head moves (full rate)
        bool     laneMoving = false;                              // the state lane holds a run (full rate under VU)

        bool  freeze = false;                                     // press and hold
        float freezeX = 0.0f;
        bool  lineHover = false, cellHover = false, pointerOver = false;
        funkgui::Point pointer{};

        enum class Drag : uint8_t { none, line, freeze, cells };
        Drag  drag = Drag::none;
        float grabDb = 0.0f;                                      // pointer level − T_in at the grab
        float downY = 0.0f;
        int   detent0 = -1, detent = -1;

        char     title[160] = "History";
        float    titleAge = 0.0f;
        uint32_t titleSerial = ~0u;
    };

    // Aggregates the store entries on timeline ms [t0, t1) into c: max levels, the max-GR entry's bits; entries older
    // than the store's oldest are missing, and timeline ms no entry maps to after the first (a stale span, ADR-69) make
    // the column a gap.
    void HistoryPlot::State::aggregate(const HistoryStore& h, int64_t t0, int64_t t1, Column& c, float right) noexcept
    {
        if (timeline.gapIn(t0, t1))
        {
            c.gap = true;
            lastSlot = -1;
        }
        const auto oldest = static_cast<int64_t>(h.oldest());
        timeline.forEntries(t0, t1, [&](int64_t e0, int64_t e1, int64_t offset) {
            for (int64_t e = std::max(e0, oldest); e < e1; ++e)
                take(h.at(static_cast<uint64_t>(e)), e + offset, c, right);
        });
    }

    // One entry at timeline position `pos` into c. A change of Mode slot between two data entries leaves a tick at the
    // later one.
    void HistoryPlot::State::take(const fcdsp::HistoryColumn& col, int64_t pos, Column& c, float right) noexcept
    {
        if (HistoryStore::isGap(col))
        {
            c.gap = true;
            lastSlot = -1;
            return;
        }
        const auto slot = static_cast<int64_t>((col.bits & kSlotMask) >> kSlotShift);
        if (lastSlot >= 0 && slot != lastSlot && nTicks < kMaxTicks)
            ticks[static_cast<std::size_t>(nTicks++)]
                = right - static_cast<float>(static_cast<double>(static_cast<int64_t>(head) - pos) * pxPerMs);
        lastSlot = slot;
        if (!c.data)
        {
            c.in = col.inPeakDb;
            c.out = col.outPeakDb;
            c.det = col.detMaxDb;
            c.gr = col.grMaxDb;
            c.grMin = col.grMinDb;
            c.tgt = col.tgtMaxDb;
            c.bits = col.bits;
            c.data = true;
        }
        else
        {
            c.in = std::max(c.in, col.inPeakDb);
            c.out = std::max(c.out, col.outPeakDb);
            c.det = std::max(c.det, col.detMaxDb);
            c.grMin = std::min(c.grMin, col.grMinDb);
            c.tgt = std::max(c.tgt, col.tgtMaxDb);
            if (col.grMaxDb > c.gr)
            {
                c.gr = col.grMaxDb;
                c.bits = col.bits;
            }
        }
        c.internal = col.internal0;                               // the last value (01 §6.3)
    }

    // 02 §6.5, §9.6: the columns at absolute multiples of W = span / columns ms over the timeline (1 ms entries, audio
    // time while fresh), offset by the partial column's phase, then the strips, the state lane and the gaps in logical px.
    void HistoryPlot::State::rebuild(const PanelContext& ctx, const layout::HistoryGeom& g) noexcept
    {
        const HistoryStore& h = ctx.history;
        builtHead = head;
        builtCount = h.count();
        builtSpan = ctx.historySpanTenths;
        builtScale = ctx.meterScaleDb;
        nCols = nRuns = nLanes = nGaps = nTicks = 0;
        lastSlot = -1;
        moving = false;

        // The windows are HistoryStore::columnWindows', the rule CONTROL PATH lines up with column for column (S13 H1a:
        // this plot kept its own copy of the arithmetic until then); the timeline ms of each are aggregated.
        const float left = g.plot.x, right = g.plot.right();
        const int nWin = HistoryStore::columnWindows(head, builtSpan, g.columns, g.colWidth, left, g.plot.w,
                                                     std::span<HistoryStore::ColumnWindow>(windows), &pxPerMs);
        for (int i = 0; i < nWin; ++i)
        {
            const HistoryStore::ColumnWindow& win = windows[static_cast<std::size_t>(i)];
            Column& c = cols[static_cast<std::size_t>(nCols++)];
            c = Column{};
            c.xl = win.xl;
            c.xr = win.xr;
            aggregate(h, win.e0, win.e1, c, right);
        }

        // The strips: one run per maximal sequence of valid columns, through the centres, flat to the outer edges.
        const float s = static_cast<float>(builtScale);
        const float floorDb = layout::kLevelTopDb - s;
        const float top = g.plot.y, bottom = g.plot.bottom();
        const float ppd = g.level.pxPerDb(s);
        // Anything the next head would move (full rate, ADR-69): a column with ink above the floor, a Mode tick, a state
        // lane run, or a gap's inner edge. A silent strip at the floor or a plot all gap is at rest.
        for (int i = 0; i < nCols && !moving; ++i)
        {
            const Column& c = cols[static_cast<std::size_t>(i)];
            moving = c.valid() && (c.in > floorDb || c.out > floorDb || c.det > floorDb || c.gr > kInkGrDb);
        }
        const auto yOf = [&](float db) {
            return std::clamp(g.level.y(std::max(db, floorDb), s), top, bottom);
        };
        const auto grY = [&](float db) { return std::clamp(top + std::max(db, 0.0f) * ppd, top, bottom); };
        int n = 0;
        for (int i = 0; i < nCols;)
        {
            if (!cols[static_cast<std::size_t>(i)].valid())
            {
                ++i;
                continue;
            }
            Run& r = runs[static_cast<std::size_t>(nRuns++)];
            r.first = n;
            const auto put = [&](float x, const Column& c) {
                const auto u = static_cast<std::size_t>(n++);
                xs[u] = x;
                yIn[u] = yOf(c.in);
                yOut[u] = yOf(c.out);
                yDet[u] = yOf(c.det);
                yTop[u] = top;
                yGr[u] = grY(c.gr);
            };
            put(cols[static_cast<std::size_t>(i)].xl, cols[static_cast<std::size_t>(i)]);
            int last = i;
            for (; i < nCols && cols[static_cast<std::size_t>(i)].valid(); ++i)
            {
                const Column& c = cols[static_cast<std::size_t>(i)];
                put((c.xl + c.xr) * 0.5f, c);
                last = i;
            }
            put(cols[static_cast<std::size_t>(last)].xr, cols[static_cast<std::size_t>(last)]);
            r.count = n - r.first;
        }

        // The state lane (02 §6.5): the phase of each valid column, runs merged, at most kStateLaneMaxRuns, newest first.
        for (int i = nCols - 1; i >= 0 && nLanes < layout::band::kStateLaneMaxRuns; --i)
        {
            const Column& c = cols[static_cast<std::size_t>(i)];
            const uint32_t ph = c.valid() ? (c.bits & kPhaseMask) : 0u;
            if (ph == 0)
                continue;
            if (nLanes > 0)
            {
                Span& prev = lanes[static_cast<std::size_t>(nLanes - 1)];
                if (prev.phase == ph && std::fabs(prev.x0 - c.xr) < 1e-3f)
                {
                    prev.x0 = c.xl;
                    continue;
                }
            }
            lanes[static_cast<std::size_t>(nLanes++)] = { c.xl, c.xr, ph };
        }

        // Gaps: runs of columns holding a gap entry.
        for (int i = 0; i < nCols; ++i)
        {
            const Column& c = cols[static_cast<std::size_t>(i)];
            if (!c.gap)
                continue;
            if (nGaps > 0 && std::fabs(gaps[static_cast<std::size_t>(nGaps - 1)].x1 - c.xl) < 1e-3f)
                gaps[static_cast<std::size_t>(nGaps - 1)].x1 = c.xr;
            else if (nGaps < kMaxRuns)
                gaps[static_cast<std::size_t>(nGaps++)] = { c.xl, c.xr, 0u };
        }
        for (int i = 0; i < nGaps && !moving; ++i)
            moving = gaps[static_cast<std::size_t>(i)].x0 > left + 0.5f;
        laneMoving = nLanes > 0;
        moving = moving || nTicks > 0 || nLanes > 0;
    }

    // UF2 (ADR-72): VU shown or hidden follows the "grView" preference: at every tick (another editor may have changed
    // it) and at once after this plot's own switch. Showing VU ends what only HISTORY offers — a threshold-line drag (its
    // gesture closes), a press-and-hold freeze, the line and span hovers — and bumps the a11y revision, since the image
    // and the span group give way to the meter.
    void HistoryPlot::State::syncView(PanelContext& ctx) noexcept
    {
        const bool shown = showVu();
        if (shown == vuShown)
            return;
        vuShown = shown;
        ++revision;
        if (!shown)
            return;
        if (drag == Drag::line && ctx.gestures != nullptr)
            ctx.gestures->endDrag();
        if (drag == Drag::line || drag == Drag::freeze)
            drag = Drag::none;
        if (freeze)
        {
            freeze = false;
            ctx.freeze.active = false;
        }
        lineHover = cellHover = false;
    }

    const HistoryPlot::State::Column* HistoryPlot::State::columnAt(float x) const noexcept
    {
        for (int i = nCols - 1; i >= 0; --i)
        {
            const Column& c = cols[static_cast<std::size_t>(i)];
            if (x >= c.xl && x < c.xr)
                return &c;
        }
        return nullptr;
    }

    // ---- construction ----------------------------------------------------------------------------------------------------

    HistoryPlot::HistoryPlot(PanelContext& ctx, const layout::HistoryGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>(ctx, geom, idBase))
    {
        ctx_.historySpanTenths = spanTenths(st_->spanModel.value());
        st_->head = ctx_.history.count();
        st_->vuShown = st_->showVu();
    }

    HistoryPlot::~HistoryPlot() = default;

    // S13 H1a (UF1a follow-up): while the other screen is shown the Panel does not tick this plot, and a timeline that
    // is not ticked cannot see the feed go stale and come back: a stop that started and ended in between was lost, and
    // the plot drew the audio on both sides of it as one run. Its composite calls this instead, every such frame.
    void HistoryPlot::keepTime(float dt)
    {
        State& s = *st_;
        s.timeline.tick(ctx_);
        if (s.hasView)
            s.vu->tick(dt);                                       // the needle stays with the audio, as under HISTORY
    }

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void HistoryPlot::tick(float dt)
    {
        State& s = *st_;
        ctx_.historySpanTenths = spanTenths(s.spanModel.value());   // the preference, mirrored (no file access here)
        s.syncView(ctx_);                                         // UF2: HISTORY or VU, the same way
        constexpr funkgui::Point kAway { -1.0f, -1.0f };
        s.span.tick(dt, s.pointerOver && !s.vuShown ? s.pointer : kAway);
        s.thr.tick(dt, false, false, false);
        if (s.hasView)
        {
            s.view.tick(dt, s.pointerOver ? s.pointer : kAway);
            s.vu->tick(dt);                                       // every frame, shown or not: the needle is current
        }

        // The head: the timeline's now (audio time while fresh, the wall clock over a gap once the audio stops, ADR-69)
        // unless a press and hold keeps the view.
        const FrameState& f = ctx_.frame;
        const HistoryStore& h = ctx_.history;
        s.timeline.tick(ctx_);
        if (!s.freeze)
            s.head = s.timeline.head();

        if (s.head != s.builtHead || h.count() != s.builtCount || ctx_.historySpanTenths != s.builtSpan
            || ctx_.meterScaleDb != s.builtScale)
            s.rebuild(ctx_, geom_);

        // The freeze column under the pointer (the display row reads it, 02 §6.5).
        if (s.freeze)
        {
            ctx_.freeze.active = true;
            ctx_.freeze.atSeconds = -(geom_.plot.right() - s.freezeX) / static_cast<float>(s.pxPerMs * 1000.0);
            fcdsp::HistoryColumn col{};
            col.inPeakDb = col.outPeakDb = col.detMaxDb = HistoryStore::kFloorDb;
            if (const State::Column* c = s.columnAt(s.freezeX); c != nullptr && c->valid())
            {
                col.inPeakDb = c->in;
                col.outPeakDb = c->out;
                col.detMaxDb = c->det;
                col.grMaxDb = c->gr;
                col.grMinDb = c->grMin;
                col.tgtMaxDb = c->tgt;
                col.internal0 = c->internal;
                col.bits = c->bits;
            }
            ctx_.freeze.column = col;
        }

        // The item under the hand: the threshold line (THRESHOLD) or the span cells.
        char line[sizeof ctx_.handNext.spec];
        if (s.drag == State::Drag::line && ctx_.pointerPressed)
        {
            s.thr.specLine(line, sizeof line);
            ctx_.offerHand(fcdsp::Pid::thr, HandKind::drag, idBase_ + kLineId, line);
        }
        else if (s.pointerOver && s.lineHover)
        {
            s.thr.specLine(line, sizeof line);
            ctx_.offerHand(fcdsp::Pid::thr, HandKind::hover, idBase_ + kLineId, line);
        }
        else if (s.pointerOver && s.cellHover)
        {
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, idBase_ + kGroupId, kSpanSpec);
        }
        else if (s.pointerOver && s.viewHover)
        {
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, idBase_ + kViewGroupId, kViewSpec);
        }
        if (ctx_.focusVisible && ctx_.focus == idBase_ + kGroupId && !s.vuShown)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, idBase_ + kGroupId, kSpanSpec);
        if (ctx_.focusVisible && s.hasView && ctx_.focus == idBase_ + kViewGroupId)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, idBase_ + kViewGroupId, kViewSpec);

        // The image's title (02 §7.5): regenerated at <= 4 Hz.
        s.titleAge += std::max(dt, 0.0f);
        if (s.titleAge >= kTitleS || s.titleSerial != f.resolveSerial)
        {
            s.titleAge = 0.0f;
            s.titleSerial = f.resolveSerial;
            const State::Column* newest = s.nCols > 0 ? &s.cols[static_cast<std::size_t>(s.nCols - 1)] : nullptr;
            char gr[24] = "";
            const bool live = f.live && newest != nullptr && newest->valid();
            if (live && funkgui::fmt::db(newest->gr, 1, gr, sizeof gr) < 0)
                gr[0] = '\0';
            std::snprintf(s.title, sizeof s.title, "History, last %g seconds%s%s%s",
                          static_cast<double>(ctx_.historySpanTenths) / 10.0, live ? ": gain reduction " : "",
                          live ? gr : "", live ? " dB" : "");
        }
    }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void HistoryPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const FrameState& f = ctx_.frame;
        const funkgui::Rect& p = geom_.plot;
        const float scale = static_cast<float>(ctx_.meterScaleDb);
        const float floorDb = layout::kLevelTopDb - scale;
        const layout::LevelMap& lm = geom_.level;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
        }

        // UF2 (ADR-72): with VU shown the meter fills the plot instead of the grid and the traces (the columns keep
        // advancing in tick(), so HISTORY comes back without a gap).
        if (s.vuShown)
            s.vu->draw(c, th);

        // Grid: every 12 dB (every 6 at S <= 24); 0 dBFS always, labelled.
        if (!s.vuShown)
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            const float step = scale <= 24.0f ? layout::band::kGridDbFine : layout::band::kGridDb;
            for (float db = std::floor(layout::kLevelTopDb / step) * step; db > floorDb; db -= step)
                if (db < layout::kLevelTopDb)
                    c.hairlineH(p.x + 1.0f, lm.y(db, scale), p.w - 2.0f, th.ink16);
            const float y0 = lm.y(0.0f, scale);
            if (y0 + layout::band::kZeroLabelDy >= p.y)
                c.text("0 DBFS", p.x + layout::band::kZeroLabelDx, y0 + layout::band::kZeroLabelDy, T::kMicro, th.ink32);
        }

        // Traces (02 §6.5): IN area, DET, OUT, hanging GR; pre-mixed over the ground, never dimmed (ADR-69).
        if (!s.vuShown)
        {
            constexpr float d = 1.0f;
            const bool det = geom_.alwaysDet || detCanDiffer(ctx_);
            for (int r = 0; r < s.nRuns; ++r)
            {
                const State::Run& run = s.runs[static_cast<std::size_t>(r)];
                const auto at = static_cast<std::size_t>(run.first);
                const float* xs = s.xs.data() + at;
                {
                    const funkgui::Canvas::Scope scope(c, tag::histIn, true);
                    c.areaStrip(xs, run.count, s.yIn.data() + at, nullptr, p.bottom(),
                                funkgui::premix(th.ground, th.ink16, d), 1.0f, funkgui::premix(th.ground, th.ink32, d),
                                funkgui::AreaEdge::top);
                }
                if (det)
                {
                    const funkgui::Canvas::Scope scope(c, tag::histDet, true);
                    c.areaStrip(xs, run.count, s.yDet.data() + at, nullptr, p.bottom(), kClear, 1.0f,
                                funkgui::premix(th.ground, th.ink52, d), funkgui::AreaEdge::top);
                }
                {
                    const funkgui::Canvas::Scope scope(c, tag::histOut, true);
                    c.areaStrip(xs, run.count, s.yOut.data() + at, nullptr, p.bottom(), kClear, 1.0f,
                                funkgui::premix(th.ground, th.ink70, d), funkgui::AreaEdge::top);
                }
                {
                    const funkgui::Canvas::Scope scope(c, tag::histGr, true);
                    c.areaStrip(xs, run.count, s.yTop.data() + at, s.yGr.data() + at, p.y,
                                funkgui::premix(th.ground, th.signal, kGrFill * d), kGrStroke,
                                funkgui::premix(th.ground, th.signal, d), funkgui::AreaEdge::bottom);
                }
            }
            if (s.nGaps > 0)
            {
                const funkgui::Canvas::Scope scope(c, tag::gap, true);
                for (int i = 0; i < s.nGaps; ++i)
                {
                    const State::Span& g = s.gaps[static_cast<std::size_t>(i)];
                    c.dotted(g.x0, p.bottom() - kGapDotDy, g.x1 - g.x0, kGapDotStep,
                             funkgui::premix(th.ground, th.ink16, d));
                }
            }
            if (s.nTicks > 0)
            {
                const funkgui::Canvas::Scope scope(c, tag::modeTick, true);
                for (int i = 0; i < s.nTicks; ++i)
                    c.hairlineV(s.ticks[static_cast<std::size_t>(i)], p.y, p.h, funkgui::premix(th.ground, th.ink32, d));
            }
        }

        // The threshold line at T_in, into the TRANSFER handle (02 §6.5; K1 #9). Under VU only its part outside the plot
        // (from the frame's right edge on, so every pixel right of the plot is the same), and it is not draggable.
        if (f.entry != nullptr)
        {
            const float tIn = fcdsp::analysis::inputThresholdDb(f.eng);
            if (tIn > floorDb && tIn < layout::kLevelTopDb)
            {
                const layout::TransferGeom& tg = transferOf(geom_);
                const float x0 = s.vuShown ? p.right() - 1.0f : p.x;
                const float x1 = layout::transferX(tg, tIn, scale);
                const bool hand = ctx_.hand.kind != HandKind::none && ctx_.hand.pid == fcdsp::Pid::thr;
                const funkgui::Canvas::Scope scope(c, tag::thresholdMark, false);
                c.hairlineH(x0, lm.y(tIn, scale), x1 - x0, hand ? th.accent : th.ink32);
            }
        }

        // The state lane (02 §6.5): ATTACK ink70, HOLD ink100, RELEASE ink32.
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, geom_.stateLane, th.ink16);
        }
        if (s.nLanes > 0)
        {
            const funkgui::Canvas::Scope scope(c, tag::stateLane, true);
            const funkgui::Rect& ln = geom_.stateLane;
            for (int i = 0; i < s.nLanes; ++i)
            {
                const State::Span& sp = s.lanes[static_cast<std::size_t>(i)];
                const funkgui::Col ink = sp.phase == 1 ? th.ink70 : sp.phase == 2 ? th.ink100 : th.ink32;
                c.rrect(sp.x0, ln.y, sp.x1 - sp.x0, ln.h, 0.0f, funkgui::premix(th.ground, ink, 1.0f));
            }
        }

        if (s.freeze)
        {
            const funkgui::Canvas::Scope scope(c, tag::freezeCursor, false);
            c.hairlineV(s.freezeX, p.y, p.h, th.ink52);
        }

        // Chrome: caption (on the band the HISTORY · VU switch in its place, UF2), span cells, unit word, time labels —
        // the last three hidden under VU.
        if (s.hasView)
        {
            s.view.draw(c, th, ctx_.focusVisible && ctx_.focus == idBase_ + kViewGroupId);
        }
        else
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("HISTORY", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        if (s.vuShown)
            return;
        s.span.draw(c, th, ctx_.focusVisible && ctx_.focus == idBase_ + kGroupId);
        {
            const funkgui::Canvas::Scope scope(c, tag::unitWord, false);
            c.text("S", geom_.unitRight, geom_.caption.y, T::kCaption, th.ink32, funkgui::Align::right);
        }
        const float span = static_cast<float>(ctx_.historySpanTenths) / 10.0f;
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            const auto n = static_cast<int>(std::lround(p.w / geom_.timeLabelPitch));
            for (int k = 0; k <= n; ++k)
            {
                char t[24];
                timeLabel(t, sizeof t, -span * static_cast<float>(k) / static_cast<float>(n), k == n);
                const float x = p.right() - geom_.timeLabelPitch * static_cast<float>(k);
                const funkgui::Align a = k == 0 ? funkgui::Align::right : k == n ? funkgui::Align::left
                                                                                 : funkgui::Align::centre;
                c.text(t, k == n ? p.x : x, geom_.timeLabelY, T::kMicro, th.ink32, a);
            }
        }
        const funkgui::AxisMap x { p.x, p.right(), -span, 0.0f, false };
        const funkgui::AxisMap y { p.y, p.bottom(), layout::kLevelTopDb, floorDb, false };
        c.axis(tag::histAxis, &x, &y);
    }

    // ---- hit testing and input -------------------------------------------------------------------------------------------

    bool HistoryPlot::hit(funkgui::Point p) const
    {
        return area(geom_).contains(p) || (st_->hasView && st_->view.contains(p));   // UF2: the switch starts at x 33
    }

    namespace
    {
        // The threshold line's y, or NaN when it is not drawn.
        float lineY(const PanelContext& ctx, const layout::HistoryGeom& g) noexcept
        {
            if (ctx.frame.entry == nullptr)
                return std::nanf("");
            const float scale = static_cast<float>(ctx.meterScaleDb);
            const float tIn = fcdsp::analysis::inputThresholdDb(ctx.frame.eng);
            if (!(tIn > layout::kLevelTopDb - scale && tIn < layout::kLevelTopDb))
                return std::nanf("");
            return g.level.y(tIn, scale);
        }

        bool onLine(const PanelContext& ctx, const layout::HistoryGeom& g, funkgui::Point p) noexcept
        {
            const float y = lineY(ctx, g);
            return !std::isnan(y) && p.x >= g.plot.x && p.x < g.plot.right()
                && std::fabs(p.y - y) <= layout::band::kLineHitPx;
        }
    }

    void HistoryPlot::pointerMove(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointerOver = true;
        s.pointer = { e.x, e.y };
        s.lineHover = !s.vuShown && onLine(ctx_, geom_, s.pointer);
        s.cellHover = !s.vuShown && s.span.contains(s.pointer);
        s.viewHover = s.hasView && s.view.contains(s.pointer);
    }

    void HistoryPlot::pointerExit()
    {
        State& s = *st_;
        s.pointerOver = s.lineHover = s.cellHover = s.viewHover = false;
    }

    void HistoryPlot::pointerDown(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        const funkgui::Point p{ e.x, e.y };
        s.pointer = p;
        s.pointerOver = true;
        s.drag = State::Drag::none;
        if (ctx_.gestures == nullptr || ctx_.host == nullptr)
            return;
        if (s.hasView && s.view.contains(p))
        {
            s.view.pointerDown(e, *ctx_.gestures);                // UF2: HISTORY or VU (a preference, no parameter)
            s.syncView(ctx_);
            s.drag = State::Drag::cells;
            return;
        }
        if (s.vuShown)
            return;                                               // the meter takes no input
        if (s.span.contains(p))
        {
            s.span.pointerDown(e, *ctx_.gestures);
            ctx_.historySpanTenths = spanTenths(s.spanModel.value());
            s.drag = State::Drag::cells;
            return;
        }
        SlotModel& thr = ctx_.slot(fcdsp::Pid::thr);
        if (onLine(ctx_, geom_, p))
        {
            if (e.popup)
            {
                if (funkgui::ParamPort* port = thr.port(); port != nullptr)
                    ctx_.host->showParamMenu(*port, e.x, e.y);
                return;
            }
            s.drag = State::Drag::line;                          // a refused THRESHOLD still shows its reason
            const funkgui::ValueView& v = s.thr.view();
            funkgui::ParamPort* port = thr.port();
            if (!writable(v.state) || port == nullptr)
                return;
            const float scale = static_cast<float>(ctx_.meterScaleDb);
            s.grabDb = geom_.level.db(e.y, scale) - fcdsp::analysis::inputThresholdDb(ctx_.frame.res.eng);
            s.downY = e.y;
            s.detent0 = s.detent = v.detent;
            ctx_.gestures->beginDrag(*port);
            return;
        }
        if (geom_.plot.contains(p) && !e.popup)
        {
            s.drag = State::Drag::freeze;
            s.freeze = true;
            s.freezeX = std::clamp(e.x, geom_.plot.x, geom_.plot.right() - 0.5f);
        }
    }

    void HistoryPlot::pointerDrag(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointer = { e.x, e.y };
        if (s.drag == State::Drag::freeze)
        {
            s.freezeX = std::clamp(e.x, geom_.plot.x, geom_.plot.right() - 0.5f);
            return;
        }
        if (s.drag != State::Drag::line || ctx_.gestures == nullptr || !ctx_.gestures->dragging())
            return;
        const funkgui::ValueView& v = s.thr.view();
        if (v.state == funkgui::ValueState::stepped && v.detents != nullptr && v.nDetents > 1 && s.detent0 >= 0)
        {
            // RuleSlider's stepped rule: one detent per pitch, committed half a pitch + 6 px on; up raises THRESHOLD.
            const float pitch = std::clamp(kTrackPx / static_cast<float>(v.nDetents - 1), kPitchMin, kPitchMax);
            const float commit = 0.5f * pitch + layout::band::kSnapHysteresisPx;
            const float travel = s.downY - e.y;
            int cur = s.detent;
            while (cur < v.nDetents - 1 && travel - static_cast<float>(cur - s.detent0) * pitch > commit)
                ++cur;
            while (cur > 0 && travel - static_cast<float>(cur - s.detent0) * pitch < -commit)
                --cur;
            if (cur != s.detent)
            {
                s.detent = cur;
                ctx_.gestures->dragTo(v.detents[cur].host01);
            }
            return;
        }
        // Continuous: absolute in the plot's own units (K1 #9), keeping the grab offset.
        const float target = geom_.level.db(e.y, static_cast<float>(ctx_.meterScaleDb)) - s.grabDb;
        float host01 = 0.0f;
        if (ctx_.slot(fcdsp::Pid::thr).plotToHost01(target, host01))
            ctx_.gestures->dragTo(host01);
    }

    void HistoryPlot::pointerUp(const funkgui::PointerEvent&)
    {
        State& s = *st_;
        if (s.drag == State::Drag::line && ctx_.gestures != nullptr)
            ctx_.gestures->endDrag();
        if (s.drag == State::Drag::freeze)
        {
            s.freeze = false;
            ctx_.freeze.active = false;
        }
        s.drag = State::Drag::none;
    }

    funkgui::Cursor HistoryPlot::cursor(funkgui::Point p) const
    {
        const State& s = *st_;
        if (s.hasView && s.view.contains(p))
            return s.view.cursorAt(p);
        if (s.vuShown)
            return funkgui::Cursor::normal;
        if (s.drag == State::Drag::line || onLine(ctx_, geom_, p))
            return funkgui::Cursor::upDown;
        if (s.span.contains(p))
            return s.span.cursorAt(p);
        return funkgui::Cursor::normal;
    }

    bool HistoryPlot::key(const funkgui::KeyEvent& e)
    {
        State& s = *st_;
        if (ctx_.gestures != nullptr && s.hasView && ctx_.focus == idBase_ + kViewGroupId)
        {
            const bool used = s.view.key(e, *ctx_.gestures);
            s.syncView(ctx_);
            return used;
        }
        if (ctx_.gestures == nullptr || ctx_.focus != idBase_ + kGroupId || s.vuShown)
            return false;
        const bool used = st_->span.key(e, *ctx_.gestures);
        ctx_.historySpanTenths = spanTenths(st_->spanModel.value());
        return used;
    }

    bool HistoryPlot::wantsFullRate() const
    {
        // ADR-69: the strip scrolls at full rate while it holds ink the head moves; at rest (nothing in view, or all
        // silence / gap) it leaves the rate to the rest of the panel.
        const State& s = *st_;
        if (!s.span.settled() || s.freeze || (s.hasView && !s.view.settled()))
            return true;
        if (s.vuShown)                                            // UF2: the needle, and the state lane under it
            return s.vu->wantsFullRate() || (s.laneMoving && s.timeline.started());
        return s.moving && s.timeline.started();
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------------

    void HistoryPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const State& s = *st_;
        if (s.vuShown)                                            // UF2: the meter, then the switch; no span group
        {
            s.vu->accessibility(out);
            s.view.accessibility(out);
            return;
        }
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = st_->title;
        it.help = "Press and hold to read a moment; drag the threshold line to set the threshold";
        it.readOnly = true;
        out.push_back(std::move(it));
        if (s.hasView)
            s.view.accessibility(out);
        s.span.accessibility(out);
    }

    int HistoryPlot::focusOrder(std::span<uint32_t> out) const
    {
        // 02 §8.9 item 5: the GR view group (UF2, the band only), then the span group (hidden under VU).
        const State& s = *st_;
        std::size_t n = 0;
        if (s.hasView && n < out.size())
            out[n++] = idBase_ + kViewGroupId;
        if (!s.vuShown && n < out.size())
            out[n++] = idBase_ + kGroupId;
        return static_cast<int>(n);
    }

    void HistoryPlot::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (ctx_.gestures == nullptr || a == funkgui::A11yAction::focus)
            return;
        State& s = *st_;
        if (s.hasView && s.view.a11yAction(id, a, value, *ctx_.gestures))
        {
            s.syncView(ctx_);
            return;
        }
        if (!s.vuShown && s.span.a11yAction(id, a, value, *ctx_.gestures))
            ctx_.historySpanTenths = spanTenths(s.spanModel.value());
    }

    uint32_t HistoryPlot::a11yRevision() const { return st_->revision; }
}
