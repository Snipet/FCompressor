// Source/editor/views/ControlPathPlot.cpp — CONTROL PATH (see ControlPathPlot.h; 02 §7.3, §9.2, §9.6): the columns
// rebuilt in tick() from the HistoryStore over HISTORY's column windows and timeline (draw() only emits), the GR lane
// (target, applied area and its min line), the phase lane, the Mode's history internal, the event stripes, gaps and the
// freeze cursor.
#include "editor/views/ControlPathPlot.h"

#include "editor/HistoryStore.h"
#include "editor/Tags.h"
#include "editor/views/Readouts.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/engine/IEngine.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

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
        namespace B = layout::band;
        namespace CP = layout::controlPath;

        constexpr int   kMaxColumns = 256;                         // >= the geometry's columns + 2 (HistoryPlot's bound)
        constexpr int   kMaxSamples = 3 * kMaxColumns + 8;         // strip samples: centres + two ends per run
        constexpr int   kMaxRuns = kMaxColumns / 2 + 2;
        constexpr int   kEventRows = static_cast<int>(CP::kEvents.size());
        constexpr float kGrFill = 0.18f;                           // CP_APPLIED: premix(ground, signal, 0.18) ...
        constexpr float kGrStroke = 1.5f;                          // ... + a 1.5 px signal bottom stroke
        constexpr float kGapDotStep = 3.0f;
        constexpr float kGapDotDy = 3.0f;                          // the GAP dots, above the GR lane's bottom edge
        constexpr float kTitleS = 0.25f;                           // a11y title <= 4 Hz (02 §7.5, §9.6)
        constexpr float kInkGrDb = 0.01f;                          // a column with less GR draws no GR ink
        constexpr float kLabelDx = 4.0f;                           // the internal's label inside the lane's top-left
        constexpr float kLabelDy = 2.0f;
        constexpr float kLabelGap = 6.0f;                          // between the internal's name and its value
        constexpr int   kGridDivisions = 4;                        // GR lane grid: quarters of S/2
        constexpr uint32_t kImageId = 1;
        constexpr uint32_t kSlotShift = 8, kSlotMask = 0xFFu << kSlotShift;
        constexpr uint32_t kPhaseMask = 3u;
        constexpr funkgui::Col kClear { 0, 0, 0, 0 };
        constexpr const char* kDash = "\xE2\x80\x93";              // U+2013
        constexpr const char* kNoInternal = "NO HISTORY INTERNAL";

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::ControlPathGeom& g) noexcept
        {
            const float top = g.caption.y - 4.0f;
            return { g.plot.x, top, g.plot.w, g.timeLabelY + 14.0f - top };
        }

        // The Mode's history-flagged internal (01 §4.3: at most one per Mode) and its index, or nullptr.
        const fcdsp::InternalSpec* historyInternal(const fcdsp::ModeDescriptor* d, int* index = nullptr) noexcept
        {
            if (d == nullptr)
                return nullptr;
            for (std::size_t i = 0; i < d->internals.size() && i < static_cast<std::size_t>(fcdsp::kInternals); ++i)
                if (d->internals[i].history)
                {
                    if (index != nullptr)
                        *index = static_cast<int>(i);
                    return &d->internals[i];
                }
            return nullptr;
        }

        const fcdsp::InternalSpec* historyInternalOfSlot(int slot) noexcept
        {
            const fcdsp::ModeEntry* e = fcdsp::bySlot(slot);
            return historyInternal(e != nullptr ? e->desc : nullptr);
        }

        // One time label (HISTORY's: every timeLabelPitch px from "now"): "0", "−1", "−2.5"; the leftmost adds " S".
        void timeLabel(char* out, std::size_t n, float seconds, bool unit) noexcept
        {
            const float r = std::round(seconds);
            const int dp = std::fabs(seconds - r) < 1e-3f ? 0 : 1;
            if (funkgui::fmt::db(seconds, dp, out, n) < 0)
                out[0] = '\0';
            if (unit)
                std::strncat(out, " S", n - std::strlen(out) - 1);
        }
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct ControlPathPlot::State
    {
        struct Column
        {
            float    xl = 0.0f, xr = 0.0f;                        // clipped to the plot
            float    gr = 0.0f, grMin = 0.0f, tgt = 0.0f;         // max grMaxDb, min grMinDb, max tgtMaxDb
            float    internal = 0.0f;                             // the last data entry's internal0 ...
            int      slot = -1;                                   // ... and the Mode slot that wrote it
            uint32_t phase = 0;                                   // the max-GR entry's phase (b0–1)
            uint32_t events = 0;                                  // the OR of the entries' bits
            bool     data = false;                                // holds at least one data entry
            bool     gap = false;                                 // holds a gap entry
            bool     valid() const noexcept { return data && !gap; }
        };
        struct Run { int first = 0, count = 0; };
        struct Span { float x0 = 0.0f, x1 = 0.0f; uint32_t v = 0; };
        using Spans = std::array<Span, kMaxRuns>;

        void rebuild(const PanelContext&, const layout::ControlPathGeom&) noexcept;
        static void take(const fcdsp::HistoryColumn&, Column&) noexcept;

        std::array<HistoryStore::ColumnWindow, kMaxColumns> windows{};
        std::array<Column, kMaxColumns> cols{};
        int nCols = 0;

        // GR lane strips: one run per maximal sequence of valid columns, through the centres, flat to the outer edges.
        std::array<float, kMaxSamples> xs{}, yTop{}, yGr{}, yMin{}, yTgt{};
        std::array<Run, kMaxRuns> runs{};
        int nRuns = 0;
        // Internal lane strips: their own runs (a column of a Mode without a history internal breaks them).
        std::array<float, kMaxSamples> xi{}, yInt{};
        std::array<Run, kMaxRuns> iruns{};
        int nIRuns = 0;
        Spans phases{};                                           // merged, newest first, <= kStateLaneMaxRuns
        int nPhases = 0;
        std::array<Spans, kEventRows> events{};                   // merged, oldest first
        std::array<int, kEventRows> nEvents{};
        Spans gaps{};
        int nGaps = 0;

        telemetry::HistoryTimeline timeline;                      // HISTORY's: the same ticks, so the same columns
        uint64_t head = 0;                                        // the timeline ms "now" stands for (held by a freeze)
        uint64_t builtHead = ~uint64_t{ 0 }, builtCount = 0;
        int      builtSpan = 0, builtScale = 0;
        double   pxPerMs = 0.0;
        bool     moving = false;                                  // ink in view that the next head moves (full rate)

        // The current Mode's history internal: its name and live value, for the lane's label.
        bool hasInternal = false;
        bool valueLive = false;
        char name[32] = "";
        char value[40] = "";

        char     title[192] = "Control path";
        float    titleAge = 0.0f;
        uint32_t titleSerial = ~0u;
    };

    // One store entry into column c: max grMaxDb and tgtMaxDb, min grMinDb, the max-GR entry's phase, the OR of the
    // event bits, the last internal0 and its Mode slot; a gap entry makes the column a gap.
    void ControlPathPlot::State::take(const fcdsp::HistoryColumn& col, Column& c) noexcept
    {
        if (HistoryStore::isGap(col))
        {
            c.gap = true;
            return;
        }
        if (!c.data)
        {
            c.gr = col.grMaxDb;
            c.grMin = col.grMinDb;
            c.tgt = col.tgtMaxDb;
            c.phase = col.bits & kPhaseMask;
            c.data = true;
        }
        else
        {
            c.grMin = std::min(c.grMin, col.grMinDb);
            c.tgt = std::max(c.tgt, col.tgtMaxDb);
            if (col.grMaxDb > c.gr)
            {
                c.gr = col.grMaxDb;
                c.phase = col.bits & kPhaseMask;
            }
        }
        c.events |= col.bits;
        c.internal = col.internal0;                               // the last value (01 §6.3)
        c.slot = static_cast<int>((col.bits & kSlotMask) >> kSlotShift);
    }

    void ControlPathPlot::State::rebuild(const PanelContext& ctx, const layout::ControlPathGeom& g) noexcept
    {
        const HistoryStore& h = ctx.history;
        builtHead = head;
        builtCount = h.count();
        builtSpan = ctx.historySpanTenths;
        builtScale = ctx.meterScaleDb;
        nCols = nRuns = nIRuns = nPhases = nGaps = 0;
        nEvents.fill(0);
        moving = false;

        // The columns: HISTORY's windows over its timeline (audio time while fresh; a stale span is a gap), aggregated.
        const int nWin = HistoryStore::columnWindows(head, builtSpan, g.columns, g.colWidth, g.plot.x, g.plot.w,
                                                     std::span<HistoryStore::ColumnWindow>(windows), &pxPerMs);
        const auto oldest = static_cast<int64_t>(h.oldest());
        for (int i = 0; i < nWin; ++i)
        {
            const HistoryStore::ColumnWindow& w = windows[static_cast<std::size_t>(i)];
            Column& c = cols[static_cast<std::size_t>(nCols++)];
            c = Column{};
            c.xl = w.xl;
            c.xr = w.xr;
            c.gap = timeline.gapIn(w.e0, w.e1);
            timeline.forEntries(w.e0, w.e1, [&](int64_t e0, int64_t e1, int64_t) {
                for (int64_t e = std::max(e0, oldest); e < e1; ++e)
                    take(h.at(static_cast<uint64_t>(e)), c);
            });
            moving = moving || (c.valid() && (c.gr > kInkGrDb || c.tgt > kInkGrDb));
        }

        // GR lane strips (hanging: 0 dB at the lane's top, S/2 at its bottom).
        const funkgui::Rect& lane = g.grLane;
        const float ppd = lane.h / (0.5f * static_cast<float>(builtScale));
        const auto grY = [&](float db) { return std::clamp(lane.y + std::max(db, 0.0f) * ppd, lane.y, lane.bottom()); };
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
                yTop[u] = lane.y;
                yGr[u] = grY(c.gr);
                yMin[u] = grY(c.grMin);
                yTgt[u] = grY(c.tgt);
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

        // Internal lane strips: internal0 through the lo…hi of the Mode that wrote the column, clamped to the lane.
        const funkgui::Rect& il = g.internalLane;
        const auto internalY = [&](const Column& c, float& y) {
            const fcdsp::InternalSpec* spec = c.valid() ? historyInternalOfSlot(c.slot) : nullptr;
            if (spec == nullptr || !std::isfinite(c.internal))
                return false;
            const float span = spec->hi - spec->lo;
            const float v = span != 0.0f ? std::clamp((c.internal - spec->lo) / span, 0.0f, 1.0f) : 0.0f;
            y = il.bottom() - v * il.h;
            return true;
        };
        n = 0;
        for (int i = 0; i < nCols;)
        {
            float y = 0.0f;
            if (!internalY(cols[static_cast<std::size_t>(i)], y))
            {
                ++i;
                continue;
            }
            Run& r = iruns[static_cast<std::size_t>(nIRuns++)];
            r.first = n;
            const auto put = [&](float x, float py) {
                const auto u = static_cast<std::size_t>(n++);
                xi[u] = x;
                yInt[u] = py;
            };
            put(cols[static_cast<std::size_t>(i)].xl, y);
            int last = i;
            float lastY = y;
            for (; i < nCols && internalY(cols[static_cast<std::size_t>(i)], y); ++i)
            {
                const Column& c = cols[static_cast<std::size_t>(i)];
                put((c.xl + c.xr) * 0.5f, y);
                last = i;
                lastY = y;
            }
            put(cols[static_cast<std::size_t>(last)].xr, lastY);
            r.count = n - r.first;
        }

        // The phase lane: runs merged, newest first (HISTORY's state lane rule).
        for (int i = nCols - 1; i >= 0 && nPhases < B::kStateLaneMaxRuns; --i)
        {
            const Column& c = cols[static_cast<std::size_t>(i)];
            const uint32_t ph = c.valid() ? c.phase : 0u;
            if (ph == 0)
                continue;
            if (nPhases > 0)
            {
                Span& prev = phases[static_cast<std::size_t>(nPhases - 1)];
                if (prev.v == ph && std::fabs(prev.x0 - c.xr) < 1e-3f)
                {
                    prev.x0 = c.xl;
                    continue;
                }
            }
            phases[static_cast<std::size_t>(nPhases++)] = { c.xl, c.xr, ph };
        }

        // The event stripes: runs of valid columns with the row's bit, merged.
        for (int r = 0; r < kEventRows; ++r)
        {
            const uint32_t bit = CP::kEvents[static_cast<std::size_t>(r)].bit;
            Spans& sp = events[static_cast<std::size_t>(r)];
            int& ns = nEvents[static_cast<std::size_t>(r)];
            for (int i = 0; i < nCols; ++i)
            {
                const Column& c = cols[static_cast<std::size_t>(i)];
                if (!c.valid() || (c.events & bit) == 0)
                    continue;
                if (ns > 0 && std::fabs(sp[static_cast<std::size_t>(ns - 1)].x1 - c.xl) < 1e-3f)
                    sp[static_cast<std::size_t>(ns - 1)].x1 = c.xr;
                else if (ns < kMaxRuns)
                    sp[static_cast<std::size_t>(ns++)] = { c.xl, c.xr, bit };
            }
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

        // Anything the next head would move (full rate, ADR-69): GR ink, the internal lane, a phase or event stripe, a
        // gap's inner edge. At rest otherwise.
        for (int i = 0; i < nGaps && !moving; ++i)
            moving = gaps[static_cast<std::size_t>(i)].x0 > g.plot.x + 0.5f;
        for (int r = 0; r < kEventRows && !moving; ++r)
            moving = nEvents[static_cast<std::size_t>(r)] > 0;
        moving = moving || nIRuns > 0 || nPhases > 0;
    }

    // ---- construction ----------------------------------------------------------------------------------------------------

    ControlPathPlot::ControlPathPlot(PanelContext& ctx, const layout::ControlPathGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>())
    {
        st_->head = ctx_.history.count();
    }

    ControlPathPlot::~ControlPathPlot() = default;

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void ControlPathPlot::tick(float dt)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        const HistoryStore& h = ctx_.history;

        // HISTORY's time (ADR-69): the timeline's now — audio time while fresh, the wall clock over a gap once the
        // audio stops — unless HISTORY's press and hold keeps the view.
        s.timeline.tick(ctx_);
        if (!ctx_.freeze.active)
            s.head = s.timeline.head();

        if (s.head != s.builtHead || h.count() != s.builtCount || ctx_.historySpanTenths != s.builtSpan
            || ctx_.meterScaleDb != s.builtScale)
            s.rebuild(ctx_, geom_);

        // The internal lane's label: the current Mode's history internal and its value (UiFrame::internals) — from a
        // fresh frame, and held from the last one once the audio stops (as the engine holds it, ADR-69); "–" only
        // before the first frame or while the audio runs another Mode.
        int index = -1;
        const fcdsp::InternalSpec* spec = historyInternal(f.entry != nullptr ? f.entry->desc : nullptr, &index);
        s.hasInternal = spec != nullptr;
        s.valueLive = false;
        s.name[0] = '\0';
        std::strcpy(s.value, kDash);
        if (spec != nullptr)
        {
            std::snprintf(s.name, sizeof s.name, "%s", spec->name != nullptr ? spec->name : "");
            const bool live = telemetry::feed(ctx_) != telemetry::Feed::none
                           && static_cast<int>(f.ui.modeSlot) == static_cast<int>(f.res.view.slot);
            if (live && Readouts::internalText(*spec, f.ui.internals[index], s.value, sizeof s.value) > 0)
                s.valueLive = true;
            else
                std::strcpy(s.value, kDash);
        }

        // The image's title (02 §7.5): regenerated at <= 4 Hz, from the newest drawn column.
        s.titleAge += std::max(dt, 0.0f);
        if (s.titleAge >= kTitleS || s.titleSerial != f.resolveSerial)
        {
            s.titleAge = 0.0f;
            s.titleSerial = f.resolveSerial;
            const State::Column* newest = s.nCols > 0 ? &s.cols[static_cast<std::size_t>(s.nCols - 1)] : nullptr;
            if (f.live && newest != nullptr && newest->valid())
            {
                char tgt[24], gr[24];
                if (funkgui::fmt::db(std::max(newest->tgt, 0.0f), 1, tgt, sizeof tgt) < 0)
                    tgt[0] = '\0';
                if (funkgui::fmt::db(std::max(newest->gr, 0.0f), 1, gr, sizeof gr) < 0)
                    gr[0] = '\0';
                std::snprintf(s.title, sizeof s.title, "Control path: target %s dB, applied %s dB%s%s%s%s", tgt, gr,
                              s.valueLive ? ", " : "", s.valueLive ? s.name : "", s.valueLive ? " " : "",
                              s.valueLive ? s.value : "");
            }
            else
            {
                std::snprintf(s.title, sizeof s.title, "Control path, last %g seconds",
                              static_cast<double>(ctx_.historySpanTenths) / 10.0);
            }
        }
    }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void ControlPathPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const funkgui::Rect& p = geom_.plot;
        const funkgui::Rect& gl = geom_.grLane;
        constexpr float d = 1.0f;                                 // ADR-69: never dimmed
        const float half = 0.5f * static_cast<float>(ctx_.meterScaleDb);
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
            outline(c, gl, th.ink16);
            outline(c, geom_.phaseLane, th.ink16);
            outline(c, geom_.internalLane, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            for (int k = 1; k < kGridDivisions; ++k)
                c.hairlineH(gl.x + 1.0f, gl.y + gl.h * static_cast<float>(k) / static_cast<float>(kGridDivisions),
                            gl.w - 2.0f, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("CONTROL PATH \xC2\xB7 GR", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }

        // GR lane: the applied area and its min line, then the target (02 §7.3); pre-mixed over the ground.
        for (int r = 0; r < s.nRuns; ++r)
        {
            const State::Run& run = s.runs[static_cast<std::size_t>(r)];
            const auto at = static_cast<std::size_t>(run.first);
            const float* xs = s.xs.data() + at;
            const funkgui::Canvas::Scope scope(c, tag::cpApplied, true);
            c.areaStrip(xs, run.count, s.yTop.data() + at, s.yGr.data() + at, gl.y,
                        funkgui::premix(th.ground, th.signal, kGrFill * d), kGrStroke, funkgui::premix(th.ground, th.signal, d),
                        funkgui::AreaEdge::bottom);
            c.areaStrip(xs, run.count, s.yTop.data() + at, s.yMin.data() + at, gl.y, kClear, 1.0f,
                        funkgui::premix(th.ground, th.ink32, d), funkgui::AreaEdge::bottom);
        }
        for (int r = 0; r < s.nRuns; ++r)
        {
            const State::Run& run = s.runs[static_cast<std::size_t>(r)];
            const auto at = static_cast<std::size_t>(run.first);
            const funkgui::Canvas::Scope scope(c, tag::cpTarget, true);
            c.areaStrip(s.xs.data() + at, run.count, s.yTop.data() + at, s.yTgt.data() + at, gl.y, kClear, 1.0f,
                        funkgui::premix(th.ground, th.ink52, d), funkgui::AreaEdge::bottom);
        }
        if (s.nGaps > 0)
        {
            const funkgui::Canvas::Scope scope(c, tag::gap, true);
            for (int i = 0; i < s.nGaps; ++i)
            {
                const State::Span& g = s.gaps[static_cast<std::size_t>(i)];
                c.dotted(g.x0, gl.bottom() - kGapDotDy, g.x1 - g.x0, kGapDotStep, funkgui::premix(th.ground, th.ink16, d));
            }
        }

        // Phase lane: ATTACK ink70, HOLD ink100, RELEASE ink32 (HISTORY's state lane inks).
        if (s.nPhases > 0)
        {
            const funkgui::Canvas::Scope scope(c, tag::cpPhase, true);
            const funkgui::Rect& ln = geom_.phaseLane;
            for (int i = 0; i < s.nPhases; ++i)
            {
                const State::Span& sp = s.phases[static_cast<std::size_t>(i)];
                const funkgui::Col ink = sp.v == 1 ? th.ink70 : sp.v == 2 ? th.ink100 : th.ink32;
                c.rrect(sp.x0, ln.y, sp.x1 - sp.x0, ln.h, 0.0f, funkgui::premix(th.ground, ink, d));
            }
        }

        // Internal lane: the normalised history internal, stroke-only ink70, then its label (or NO HISTORY INTERNAL).
        const funkgui::Rect& il = geom_.internalLane;
        for (int r = 0; r < s.nIRuns; ++r)
        {
            const State::Run& run = s.iruns[static_cast<std::size_t>(r)];
            const auto at = static_cast<std::size_t>(run.first);
            const funkgui::Canvas::Scope scope(c, tag::cpInternal, true);
            c.areaStrip(s.xi.data() + at, run.count, s.yInt.data() + at, nullptr, il.bottom(), kClear, 1.0f,
                        funkgui::premix(th.ground, th.ink70, d), funkgui::AreaEdge::top);
        }
        {
            const float lx = il.x + kLabelDx, ly = il.y + kLabelDy;
            if (!s.hasInternal)
            {
                const funkgui::Canvas::Scope scope(c, tag::caption, false);
                c.text(kNoInternal, lx, ly, T::kMicro, th.ink32);
            }
            else
            {
                {
                    const funkgui::Canvas::Scope scope(c, tag::caption, false);
                    c.text(s.name, lx, ly, T::kMicro, th.ink52);
                }
                const funkgui::Canvas::Scope scope(c, tag::cpInternal, true);
                c.text(s.value, lx + c.textWidth(s.name, T::kMicro) + kLabelGap, ly, T::kMicro,
                       s.valueLive ? th.ink70 : th.ink16);
            }
        }

        // Events lane: four stripe rows (AUTO SLOW ink52, RANGE ink70, STAGE 2 ink100, FADE ink32), each labelled.
        const std::array<funkgui::Col, kEventRows> eventInk { th.ink52, th.ink70, th.ink100, th.ink32 };
        for (int r = 0; r < kEventRows; ++r)
        {
            const float y = geom_.eventsLane.y + geom_.eventRowPitch * static_cast<float>(r);
            const int ns = s.nEvents[static_cast<std::size_t>(r)];
            if (ns > 0)
            {
                const funkgui::Canvas::Scope scope(c, tag::cpEvents, true);
                for (int i = 0; i < ns; ++i)
                {
                    const State::Span& sp = s.events[static_cast<std::size_t>(r)][static_cast<std::size_t>(i)];
                    c.rrect(sp.x0, y, sp.x1 - sp.x0, geom_.eventRowHeight, 0.0f,
                            funkgui::premix(th.ground, eventInk[static_cast<std::size_t>(r)], d));
                }
            }
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text(CP::kEvents[static_cast<std::size_t>(r)].label, geom_.eventLabelX,
                   c.capCentreTop(y + 0.5f * geom_.eventRowHeight, T::kMicro), T::kMicro, th.ink32);
        }

        // HISTORY's press-and-hold column crosses this plot too (the same columns, so the same x).
        if (ctx_.freeze.active && s.pxPerMs > 0.0)
        {
            const float x = p.right() + static_cast<float>(static_cast<double>(ctx_.freeze.atSeconds) * s.pxPerMs * 1000.0);
            if (x >= p.x && x < p.right())
            {
                const funkgui::Canvas::Scope scope(c, tag::freezeCursor, false);
                c.hairlineV(x, p.y, p.h, th.ink52);
            }
        }

        // Time labels (HISTORY's) and the axis record.
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
        const funkgui::AxisMap y { gl.y, gl.bottom(), 0.0f, half, false };
        c.axis(tag::cpAxis, &x, &y);
    }

    bool ControlPathPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    bool ControlPathPlot::wantsFullRate() const
    {
        // ADR-69: the strip scrolls at full rate while it holds ink the head moves.
        return st_->moving && st_->timeline.started();
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------------

    void ControlPathPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = st_->title;
        it.help = "Target and applied gain reduction, the envelope phase, the Mode's history internal and its events";
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int ControlPathPlot::focusOrder(std::span<uint32_t>) const { return 0; }
}
