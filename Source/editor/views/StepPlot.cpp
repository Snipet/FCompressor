// Source/editor/views/StepPlot.cpp — one STEP RESPONSE pane (see StepPlot.h; 02 §7.3, §7.4, §7.5, §9.3; 01 §7; ADR-41).
// tick() requests the step responses and, when the worker's result, the resolved view or the EngineParams moved,
// rebuilds every curve and marker in logical px; draw() only emits.
#include "editor/views/StepPlot.h"

#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/widgets/FocusRing.h>
#include <funkgui/widgets/RuleSlider.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        namespace C = layout::chars;
        using fcdsp::Pid;

        constexpr int   kCols = PreviewWorker::kColumns;
        constexpr int   kMaxRuns = 3;
        constexpr int   kMaxGhosts = 12;                           // other detents of one stepped time slot
        constexpr int   kGhostStride = 4;                          // ghosts: every 4th column edge (nominal curves)
        constexpr int   kGhostPoints = kCols / kGhostStride + 1;
        constexpr float kMinSpanDb = 0.05f;                        // a run whose GR moves less draws nothing
        constexpr float kBarPx = 0.5f;                             // a column's min/max bar when it leaves the chord
        constexpr float kTitleS = 0.25f;                           // a11y title <= 4 Hz (02 §7.5)
        constexpr float kCrossHalf = 2.5f;
        constexpr float kInset = 4.0f;                             // captions inside the plot
        constexpr float kLegendGap = 6.0f;
        constexpr float kMarkOver = 2.0f;                          // markers and grid run 2 px past the 0/100 % lines
        constexpr float kTrackPx = 240.0f;                         // relative fallback: 240 px per track (Shift 1200)
        constexpr float kTrackPxFine = 1200.0f;
        constexpr uint32_t kImageId = 1;
        constexpr funkgui::Col kClear { 0, 0, 0, 0 };

        constexpr int kMeasuredAttack = 1;                         // the +12 dB run: the caption's, measured
        constexpr int kMeasuredRelease = 1;                        // the 2 s burst: from the +12 dB steady state

        bool writable(funkgui::ValueState s) noexcept
        {
            return s == funkgui::ValueState::continuous || s == funkgui::ValueState::stepped;
        }

        funkgui::ValueState stateOf(fcdsp::SlotState s) noexcept
        {
            switch (s)
            {
                case fcdsp::SlotState::live:    return funkgui::ValueState::continuous;
                case fcdsp::SlotState::stepped: return funkgui::ValueState::stepped;
                case fcdsp::SlotState::locked:  return funkgui::ValueState::locked;
                case fcdsp::SlotState::derived: return funkgui::ValueState::derived;
                case fcdsp::SlotState::na:      return funkgui::ValueState::na;
            }
            return funkgui::ValueState::na;
        }

        // A time control: its hand lights the measured run (02 §7.3 "+12 in accent while a time control or marker is
        // under the hand").
        bool timeControl(Pid p) noexcept { return p == Pid::atk || p == Pid::rel || p == Pid::hold || p == Pid::tmode; }

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        funkgui::Rect area(const layout::StepGeom& g) noexcept
        {
            const float top = g.caption.y - 4.0f;
            return { g.plot.x, top, g.plot.w, g.labelY + 14.0f - top };
        }

        // The log-time axis: t (s from the step) <-> x.
        float xOf(const layout::StepGeom& g, double t) noexcept
        {
            const double u = std::log(t / static_cast<double>(g.tMinS))
                           / std::log(static_cast<double>(g.tMaxS) / static_cast<double>(g.tMinS));
            return g.plot.x + static_cast<float>(u) * g.plot.w;
        }

        double tOf(const layout::StepGeom& g, float x) noexcept
        {
            const double u = static_cast<double>((x - g.plot.x) / g.plot.w);
            return static_cast<double>(g.tMinS)
                 * std::pow(static_cast<double>(g.tMaxS) / static_cast<double>(g.tMinS), u);
        }

        // The fraction axis (hanging): 0 at y0, 1 at y100, clamped to the plot.
        float yOf(const layout::StepGeom& g, double fraction) noexcept
        {
            const float y = g.y0 + static_cast<float>(fraction) * (g.y100 - g.y0);
            return std::clamp(y, g.plot.y, g.plot.bottom());
        }

        // The TimeLaw's name for the a11y title: "63 percent at", "10 to 90 percent in".
        const char* lawWords(fcdsp::TimeLaw law) noexcept
        {
            switch (law)
            {
                case fcdsp::TimeLaw::expDb:
                case fcdsp::TimeLaw::expLin:     return "63 percent at";
                case fcdsp::TimeLaw::t10_90:     return "10 to 90 percent in";
                case fcdsp::TimeLaw::t0_90:      return "90 percent at";
                case fcdsp::TimeLaw::t50:        return "50 percent at";
                case fcdsp::TimeLaw::rateDbPerS: return "";
            }
            return "";
        }

        // a11y display units (SlotGrid's rule, 02 §8.9): the Mode's DisplayMap, else the plain value (atk and rel are
        // milliseconds, neither a ratio nor a percentage).
        double toDisplayUnits(const fcdsp::ParamSpec& s, float plain) noexcept
        {
            return s.display.toDisplay != nullptr ? static_cast<double>(s.display.toDisplay(plain))
                                                  : static_cast<double>(plain);
        }

        float fromDisplayUnits(const fcdsp::ParamSpec& s, double d) noexcept
        {
            return s.display.toPlain != nullptr ? s.display.toPlain(static_cast<float>(d)) : static_cast<float>(d);
        }

        struct PlainRange { float a, b; };
        PlainRange trackSpan(Pid pid, const fcdsp::ParamSpec& s) noexcept
        {
            if (s.lo < s.hi)
                return { s.lo, s.hi };
            const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
            return { h.lo, h.hi };
        }

        // The key the PreviewWorker dedupes on: the EngineParams, the Mode and the sample rate (never 0).
        uint64_t previewKey(uint64_t engHash, const fcdsp::ModeEntry& e, float fs) noexcept
        {
            uint64_t h = engHash ^ 0x9E3779B97F4A7C15ull;
            const auto mixIn = [&h](uint64_t v) {
                h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            };
            mixIn(static_cast<uint64_t>(std::bit_cast<uint32_t>(fs)));
            mixIn(static_cast<uint64_t>(static_cast<uint32_t>(fcdsp::slotOf(e) + 1)));
            return h != 0 ? h : 1;
        }
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct StepPlot::State
    {
        struct Curve
        {
            bool shown = false;
            std::array<float, kCols + 1> y{};                     // at the column edges x = plot.x + k
            std::array<bool, kCols> bar{};                        // the column's min/max bar is drawn
            std::array<float, kCols> barTop{}, barBottom{};
        };

        State(PanelContext& ctx, Pid p, uint32_t handleId)
            : pid(p), slider(ctx.slot(p), layout::slotGeom(*layout::slotOf(p)), handleId)
        {
        }

        void build(const PanelContext&, const layout::StepGeom&);
        bool markerAt(funkgui::Point, const layout::StepGeom&) const noexcept;
        void updateTitle(const PanelContext&, const layout::StepGeom&);

        const Pid          pid;
        funkgui::RuleSlider slider;                              // never drawn: wheel, keys, a11y, spec lines

        // what build() derived
        uint32_t builtResult = ~0u, builtResolve = ~0u, builtEng = ~0u;
        const fcdsp::ModeEntry* builtEntry = nullptr;
        int nRuns = 0;
        std::array<Curve, kMaxRuns> curves{};
        int nGhosts = 0;
        std::array<std::array<float, kGhostPoints>, kMaxGhosts> ghosts{};
        funkgui::ValueState state = funkgui::ValueState::na;
        fcdsp::TimeSpec spec{ 0.0f, fcdsp::TimeLaw::expDb };
        bool  specShown = false;
        float specX = 0.0f;
        bool  rangeShown = false;
        float rangeX0 = 0.0f, rangeX1 = 0.0f;
        bool  measShown = false;
        float measX = 0.0f, measY = 0.0f, measS = -1.0f;
        int   nDetents = 0;
        std::array<float, SlotModel::kMaxDetents> detentX{};
        bool  tgtValid = false;
        float tgtDb = 0.0f;

        // pointer, drag
        bool  pointerOver = false;
        funkgui::Point pointer{};
        bool  hot = false;
        enum class Mode : uint8_t { none, refused, absolute, relative, stepped };
        Mode  mode = Mode::none;
        bool  dragging = false;
        float grabPx = 0.0f;                                      // absolute: pointer x − marker x at down
        float downX = 0.0f, u0 = 0.0f;                            // relative fallback
        bool  invert = false;
        int   detent = -1;

        uint32_t revision = 0;                                    // bumps when the marker's a11y item comes or goes
        bool     wasNa = true;

        char     title[200] = "";
        float    titleAge = 0.0f;
        uint32_t titleResult = ~0u, titleResolve = ~0u;           // what the title last spoke of
        bool     titleDirty = true;                               // build() ran since
    };

    // Everything the worker's result, the frame's view and its EngineParams decide, in px.
    void StepPlot::State::build(const PanelContext& ctx, const layout::StepGeom& g)
    {
        const FrameState& f = ctx.frame;
        const PreviewWorker& w = ctx.preview;
        const bool attack = g.kind == layout::StepKind::attack;
        builtResult = w.result().serial;
        builtResolve = f.resolveSerial;
        builtEng = f.engSerial;
        builtEntry = f.entry;
        nRuns = attack ? PreviewWorker::kAttackRuns : PreviewWorker::kReleaseRuns;
        nGhosts = 0;
        specShown = rangeShown = measShown = tgtValid = false;
        measS = -1.0f;
        nDetents = 0;
        titleDirty = true;
        for (Curve& cv : curves)
            cv.shown = false;

        state = f.entry != nullptr ? stateOf(f.res.view[pid].state) : funkgui::ValueState::na;
        const bool na = state == funkgui::ValueState::na;
        if (na != wasNa)
        {
            wasNa = na;
            ++revision;
        }
        if (f.entry == nullptr || f.entry->desc == nullptr)
            return;
        const fcdsp::ModeDescriptor& desc = *f.entry->desc;

        // The curves: the fraction of the steady-state GR (StepPlot.h).
        const PreviewWorker::Trace& ref = attack ? w.attackTrace(kMeasuredAttack) : w.releaseTrace(kMeasuredRelease);
        for (int i = 0; i < nRuns; ++i)
        {
            const PreviewWorker::Trace& tr = attack ? w.attackTrace(i) : w.releaseTrace(i);
            const double base = attack ? static_cast<double>(tr.from) : static_cast<double>(ref.to);
            const double full = attack ? static_cast<double>(tr.to) : static_cast<double>(ref.from);
            const double span = full - base;
            Curve& cv = curves[static_cast<std::size_t>(i)];
            if (!tr.valid || (!attack && !ref.valid) || !(span >= static_cast<double>(kMinSpanDb)))
                continue;
            cv.shown = true;
            const auto frac = [&](float gr) { return (static_cast<double>(gr) - base) / span; };
            for (int k = 0; k <= kCols; ++k)
                cv.y[static_cast<std::size_t>(k)] = yOf(g, frac(tr.edge[static_cast<std::size_t>(k)]));
            for (int k = 0; k < kCols; ++k)
            {
                const auto u = static_cast<std::size_t>(k);
                const float a = yOf(g, frac(tr.lo[u])), b = yOf(g, frac(tr.hi[u]));
                const float top = std::min(a, b), bottom = std::max(a, b);
                const float c0 = std::min(cv.y[u], cv.y[u + 1]), c1 = std::max(cv.y[u], cv.y[u + 1]);
                cv.bar[u] = top < c0 - kBarPx || bottom > c1 + kBarPx;
                cv.barTop[u] = top;
                cv.barBottom[u] = bottom;
            }
        }

        // The declared spec, under its law.
        const auto specFn = attack ? desc.attackSpec : desc.releaseSpec;
        if (specFn != nullptr && !na)
        {
            spec = specFn(f.res.view, f.eng);
            const double t = static_cast<double>(spec.seconds);
            const bool isTime = spec.law != fcdsp::TimeLaw::rateDbPerS;
            specShown = isTime && t >= static_cast<double>(g.tMinS) && t <= static_cast<double>(g.tMaxS);
            if (specShown)
                specX = xOf(g, t);
            if (isTime && spec.program && spec.lo > 0.0f && spec.hi > spec.lo)
            {
                rangeX0 = std::clamp(xOf(g, static_cast<double>(spec.lo)), g.plot.x, g.plot.right());
                rangeX1 = std::clamp(xOf(g, static_cast<double>(spec.hi)), g.plot.x, g.plot.right());
                rangeShown = rangeX1 > rangeX0;
            }

            // The measured crossing on the measured run's curve.
            const int mi = attack ? kMeasuredAttack : kMeasuredRelease;
            const Curve& mc = curves[static_cast<std::size_t>(mi)];
            if (isTime && mc.shown)
            {
                measS = ref.measured[static_cast<std::size_t>(spec.law)];
                const double tm = static_cast<double>(measS);
                if (tm > 0.0 && tm >= static_cast<double>(g.tMinS) && tm <= static_cast<double>(g.tMaxS))
                {
                    measShown = true;
                    measX = xOf(g, tm);
                    const float pos = std::clamp(measX - g.plot.x, 0.0f, static_cast<float>(kCols));
                    const int k = std::min(static_cast<int>(pos), kCols - 1);
                    const float u = pos - static_cast<float>(k);
                    const auto uk = static_cast<std::size_t>(k);
                    measY = mc.y[uk] + u * (mc.y[uk + 1] - mc.y[uk]);
                }
            }
        }

        // Stepped: the detents' positions (drag snapping) and the other detents' nominal curves.
        const fcdsp::ParamSpec* ps = f.res.view.spec[fcdsp::idx(pid)];
        if (state == funkgui::ValueState::stepped && ps != nullptr)
        {
            const funkgui::ValueView& v = slider.view();
            nDetents = std::min(v.detents != nullptr ? v.nDetents : 0, SlotModel::kMaxDetents);
            for (int i = 0; i < nDetents; ++i)
                detentX[static_cast<std::size_t>(i)]
                    = xOf(g, static_cast<double>(fcdsp::toPlain(pid, v.detents[i].host01)) / 1000.0);
            const int steps = fcdsp::stepCount(*ps);
            const double factor = static_cast<double>(fcdsp::lawFactor(ps->law));
            const int current = f.res.view[pid].step;
            for (int i = 0; i < steps && nGhosts < kMaxGhosts && ps->law != fcdsp::TimeLaw::rateDbPerS; ++i)
            {
                const auto ui = static_cast<std::size_t>(i);
                if (i == current || ui >= ps->steps.size() || (ps->steps[ui].tag & fcdsp::kTagProgram) != 0)
                    continue;
                const double tau = static_cast<double>(fcdsp::stepPlain(*ps, i)) / 1000.0 / factor;
                if (!(tau > 0.0))
                    continue;
                std::array<float, kGhostPoints>& gy = ghosts[static_cast<std::size_t>(nGhosts++)];
                for (int k = 0; k < kGhostPoints; ++k)
                {
                    const double t = tOf(g, g.plot.x + static_cast<float>(k * kGhostStride));
                    const double e = std::exp(-t / tau);
                    gy[static_cast<std::size_t>(k)] = yOf(g, attack ? 1.0 - e : e);
                }
            }
        }

        // TGT: the static GR at +12 dB over the input-referred threshold (the curve's steady state, 01 §7).
        if (attack)
        {
            const float x = fcdsp::analysis::inputThresholdDb(f.eng) + C::kAttackStepsDb[kMeasuredAttack];
            float gain = 0.0f;
            fcdsp::analysis::staticGain(*f.entry, f.eng, std::span<const float>(&x, 1), std::span<float>(&gain, 1));
            tgtDb = std::max(f.eng.preGainDb - gain, 0.0f);
            tgtValid = std::isfinite(tgtDb);
        }
    }

    bool StepPlot::State::markerAt(funkgui::Point p, const layout::StepGeom& g) const noexcept
    {
        return specShown && state != funkgui::ValueState::na && std::fabs(p.x - specX) <= C::kMarkerHitPx
            && p.y >= g.plot.y && p.y <= g.plot.bottom();
    }

    // "Attack response: 63 percent at 0.31 milliseconds, declared 0.30" (02 §7.5), in the measured value's unit.
    void StepPlot::State::updateTitle(const PanelContext& ctx, const layout::StepGeom& g)
    {
        const bool attack = g.kind == layout::StepKind::attack;
        const char* name = attack ? "Attack response" : "Release response";
        const char* words = lawWords(spec.law);
        if (ctx.frame.entry == nullptr || state == funkgui::ValueState::na || words[0] == '\0')
        {
            std::snprintf(title, sizeof title, "%s", name);
            return;
        }
        const double ref = measS > 0.0f ? static_cast<double>(measS) : static_cast<double>(spec.seconds);
        const double scale = ref < 1e-3 ? 1e6 : (ref < 1.0 ? 1e3 : 1.0);
        const char* unit = ref < 1e-3 ? "microseconds" : (ref < 1.0 ? "milliseconds" : "seconds");
        const double declared = static_cast<double>(spec.seconds) * scale;
        if (measS > 0.0f)
            std::snprintf(title, sizeof title, "%s: %s %.3g %s, declared %.3g", name, words,
                          static_cast<double>(measS) * scale, unit, declared);
        else
            std::snprintf(title, sizeof title, "%s: declared %.3g %s", name, declared, unit);
    }

    // ---- construction ----------------------------------------------------------------------------------------------------

    StepPlot::StepPlot(PanelContext& ctx, const layout::StepGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase),
          st_(std::make_unique<State>(ctx, geom.kind == layout::StepKind::attack ? Pid::atk : Pid::rel,
                                      idBase + kPlotHandleIds))
    {
    }

    StepPlot::~StepPlot() = default;

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void StepPlot::tick(float dt)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        const uint32_t handleId = s.slider.a11yId();
        s.slider.tick(dt, false, ctx_.focusVisible && ctx_.focus == handleId, false);

        // The step responses of this frame's EngineParams (the worker drops a repeat and runs <= 20 Hz).
        if (f.entry != nullptr)
        {
            const float fs = telemetry::sampleRate(ctx_);
            ctx_.preview.request(*f.entry, f.eng, fs, previewKey(f.engHash, *f.entry, fs));
        }
        if (ctx_.preview.result().serial != s.builtResult || f.resolveSerial != s.builtResolve
            || f.engSerial != s.builtEng || f.entry != s.builtEntry)
            s.build(ctx_, geom_);

        // The item under the hand.
        char line[sizeof ctx_.handNext.spec];
        const auto offer = [&](HandKind kind) {
            s.slider.specLine(line, sizeof line);
            ctx_.offerHand(s.pid, kind, handleId, line);
        };
        if (s.dragging && ctx_.pointerPressed)
            offer(HandKind::drag);
        else if (s.pointerOver && s.hot)
            offer(HandKind::hover);
        if (ctx_.focusVisible && ctx_.focus == handleId && s.state != funkgui::ValueState::na)
            offer(HandKind::focus);

        // The image's title: at once after a new result or a new resolved view, else (live EngineParams) <= 4 Hz.
        s.titleAge += std::max(dt, 0.0f);
        const bool discrete = s.titleResult != s.builtResult || s.titleResolve != f.resolveSerial;
        if (s.titleDirty && (discrete || s.titleAge >= kTitleS))
        {
            s.titleAge = 0.0f;
            s.titleDirty = false;
            s.titleResult = s.builtResult;
            s.titleResolve = f.resolveSerial;
            s.updateTitle(ctx_, geom_);
        }
    }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void StepPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const layout::StepGeom& g = geom_;
        const funkgui::Rect& p = g.plot;
        const bool attack = g.kind == layout::StepKind::attack;
        const uint32_t handleId = s.slider.a11yId();
        const bool focused = ctx_.focusVisible && ctx_.focus == handleId;
        const Pid handPid = ctx_.hand.kind != HandKind::none ? ctx_.hand.pid : fcdsp::kNoPid;
        const bool accent = timeControl(handPid) || s.hot || s.dragging || focused;
        const funkgui::AxisMap xAxis { p.x, p.right(), g.tMinS, g.tMaxS, true };

        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
        }
        {
            // The 0 % and 100 % lines and the labelled decades.
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            c.hairlineH(p.x + 1.0f, g.y0, p.w - 2.0f, th.ink16);
            c.hairlineH(p.x + 1.0f, g.y100, p.w - 2.0f, th.ink16);
            const auto& labels = attack ? layout::step::kAttackLabels : layout::step::kReleaseLabels;
            for (const layout::AxisLabel& l : labels)
            {
                const float x = xOf(g, static_cast<double>(l.value));
                if (x > p.x + 1.0f && x < p.right() - 1.0f)
                    c.hairlineV(x, g.y0 - kMarkOver, g.y100 - g.y0 + 2.0f * kMarkOver, th.ink16);
            }
        }
        if (s.rangeShown)
        {
            const funkgui::Canvas::Scope scope(c, tag::stepSpec, false);
            c.rrect(s.rangeX0, g.y0 - kMarkOver, s.rangeX1 - s.rangeX0, g.y100 - g.y0 + 2.0f * kMarkOver, 0.0f,
                    funkgui::premix(th.ground, th.ink16, 0.5f));
        }
        if (s.nGhosts > 0)
        {
            const funkgui::Canvas::Scope scope(c, tag::stepGhost, false);
            const funkgui::Col col = funkgui::premix(th.ground, th.ink16, 1.0f);
            for (int i = 0; i < s.nGhosts; ++i)
            {
                const auto& gy = s.ghosts[static_cast<std::size_t>(i)];
                for (int k = 0; k + 1 < kGhostPoints; ++k)
                    c.segment(p.x + static_cast<float>(k * kGhostStride), gy[static_cast<std::size_t>(k)],
                              p.x + static_cast<float>((k + 1) * kGhostStride), gy[static_cast<std::size_t>(k + 1)], 1.0f,
                              col);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::stepCurve, false);
            const std::array<funkgui::Col, kMaxRuns> inks = attack
                ? std::array<funkgui::Col, kMaxRuns>{ th.ink32, th.ink52, th.ink70 }
                : std::array<funkgui::Col, kMaxRuns>{ th.ink52, th.ink70, th.ink70 };
            const int measured = attack ? kMeasuredAttack : kMeasuredRelease;
            // The measured run last, so its accent lies on top.
            for (int n = 0; n < s.nRuns; ++n)
            {
                const int i = n < measured ? n : (n + 1 < s.nRuns ? n + 1 : measured);
                const State::Curve& cv = s.curves[static_cast<std::size_t>(i)];
                if (!cv.shown)
                    continue;
                const funkgui::Col ink = i == measured && accent ? th.accent : inks[static_cast<std::size_t>(i)];
                const funkgui::Col col = funkgui::premix(th.ground, ink, 1.0f);
                for (int k = 0; k < kCols; ++k)
                {
                    const auto u = static_cast<std::size_t>(k);
                    const float x0 = p.x + static_cast<float>(k);
                    c.segment(x0, cv.y[u], x0 + 1.0f, cv.y[u + 1], 1.0f, col);
                    if (cv.bar[u])
                        c.segment(x0 + 0.5f, cv.barTop[u], x0 + 0.5f, cv.barBottom[u], 1.0f, col);
                }
            }
        }
        if (s.specShown)
        {
            const funkgui::Canvas::Scope scope(c, tag::stepSpec, false);
            const bool hot = s.hot || s.dragging || focused;
            c.hairlineV(s.specX, g.y0 - kMarkOver, g.y100 - g.y0 + 2.0f * kMarkOver,
                        hot && writable(s.state) ? th.accent : th.ink32);
        }
        if (s.specShown && (s.state == funkgui::ValueState::locked || s.state == funkgui::ValueState::derived))
        {
            // Locked / derived: a cross on the marker; it refuses every write (02 §7.4).
            const funkgui::Canvas::Scope scope(c, tag::handle, false);
            const float cy = 0.5f * (g.y0 + g.y100);
            c.hairlineH(s.specX - kCrossHalf, cy, 2.0f * kCrossHalf, th.ink32);
            c.hairlineV(s.specX, cy - kCrossHalf, 2.0f * kCrossHalf, th.ink32);
        }
        if (s.measShown)
        {
            const funkgui::Canvas::Scope scope(c, tag::stepMeas, false);
            c.disc(s.measX, s.measY, C::kStepMeasR, kClear, 1.0f, th.ink100);
        }

        // Captions: the pane's name above; inside, the target (attack) or the bursts (release) and the legend.
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text(g.captionText, g.caption.x, g.caption.y, T::kCaption, th.ink52);
            const float ty = p.y + kInset;
            if (attack)
            {
                if (s.tgtValid && ctx_.frame.entry != nullptr)
                {
                    char v[24];
                    if (funkgui::fmt::db(s.tgtDb, 1, v, sizeof v) < 0)
                        v[0] = '\0';
                    char text[64];
                    std::snprintf(text, sizeof text, "TGT %s DB AT +%d", v,
                                  static_cast<int>(C::kAttackStepsDb[kMeasuredAttack]));
                    if (c.textWidth(text, T::kMicro) > p.w - 2.0f * kInset)
                        std::snprintf(text, sizeof text, "TGT %s AT +%d", v,
                                      static_cast<int>(C::kAttackStepsDb[kMeasuredAttack]));
                    c.text(text, p.x + kInset, ty, T::kMicro, th.ink32);
                }
                // The legend, in the curves' inks, bottom-left (where the attack curves never are).
                float x = p.x + kInset;
                const float ly = p.bottom() - kInset - 10.0f;
                const std::array<funkgui::Col, 3> inks { th.ink32, th.ink52, th.ink70 };
                for (std::size_t i = 0; i < C::kAttackStepsDb.size(); ++i)
                {
                    char t[8];
                    std::snprintf(t, sizeof t, "+%d", static_cast<int>(C::kAttackStepsDb[i]));
                    const bool lit = static_cast<int>(i) == kMeasuredAttack && accent;
                    c.text(t, x, ly, T::kMicro, lit ? th.accent : inks[i]);
                    x += c.textWidth(t, T::kMicro) + kLegendGap;
                }
            }
            else
            {
                // "AFTER 50 MS · 2.0 S": each burst in its curve's ink.
                std::array<char[16], 2> burst{};
                for (std::size_t i = 0; i < burst.size(); ++i)
                {
                    char v[12];
                    const char* unit = "";
                    if (funkgui::fmt::seconds(C::kReleaseBurstsS[i], v, sizeof v, &unit) < 0)
                        v[0] = '\0';
                    std::snprintf(burst[i], sizeof burst[i], "%s %s", v, unit);
                }
                const std::array<funkgui::Col, 2> inks { th.ink52, accent ? th.accent : th.ink70 };
                float x = p.x + kInset;
                const auto piece = [&](const char* t, funkgui::Col col) {
                    c.text(t, x, ty, T::kMicro, col);
                    x += c.textWidth(t, T::kMicro);
                };
                piece("AFTER ", th.ink32);
                piece(burst[0], inks[0]);
                piece(" · ", th.ink32);
                piece(burst[1], inks[1]);
            }
        }
        {
            // Time labels under the plot, kept inside the plot's width.
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            const auto& labels = attack ? layout::step::kAttackLabels : layout::step::kReleaseLabels;
            for (const layout::AxisLabel& l : labels)
            {
                const float w = c.textWidth(l.text, T::kMicro);
                const float x = std::clamp(xOf(g, static_cast<double>(l.value)) - 0.5f * w, p.x, p.right() - w);
                c.text(l.text, x, g.labelY, T::kMicro, th.ink32);
            }
        }
        if (focused && s.specShown)
            funkgui::drawFocusRing(c, { s.specX - C::kMarkerHitPx - 2.0f, p.y, 2.0f * C::kMarkerHitPx + 4.0f, p.h },
                                   th.accent);
        const funkgui::AxisMap yAxis { g.y0, g.y100, 0.0f, 1.0f, false };
        c.axis(tag::stepAxis, &xAxis, &yAxis);
    }

    // ---- hit testing and input -------------------------------------------------------------------------------------------

    bool StepPlot::hit(funkgui::Point p) const { return area(geom_).contains(p); }

    void StepPlot::pointerMove(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointerOver = true;
        s.pointer = { e.x, e.y };
        s.hot = s.markerAt(s.pointer, geom_);
    }

    void StepPlot::pointerExit()
    {
        State& s = *st_;
        s.pointerOver = false;
        s.hot = false;
    }

    void StepPlot::pointerDown(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointer = { e.x, e.y };
        s.pointerOver = true;
        s.mode = State::Mode::none;
        s.dragging = false;
        if (ctx_.gestures == nullptr || ctx_.host == nullptr || !s.markerAt(s.pointer, geom_))
            return;
        SlotModel& m = ctx_.slot(s.pid);
        funkgui::ParamPort* port = m.port();
        if (e.popup)
        {
            if (port != nullptr)
                ctx_.host->showParamMenu(*port, e.x, e.y);
            return;
        }
        s.dragging = true;
        s.hot = true;
        const funkgui::ValueView& v = s.slider.view();
        if (!writable(v.state) || port == nullptr)
        {
            s.mode = State::Mode::refused;                        // the footer shows the reason (hand offer)
            return;
        }
        if (v.state == funkgui::ValueState::stepped && v.detents != nullptr && s.nDetents > 1)
        {
            s.mode = State::Mode::stepped;
            s.detent = v.detent;
        }
        else
        {
            float probe = 0.0f;
            if (m.plotToHost01(static_cast<float>(tOf(geom_, s.specX)), probe))
            {
                s.mode = State::Mode::absolute;                   // seconds on the log axis, the grab kept
                s.grabPx = e.x - s.specX;
            }
            else
            {
                s.mode = State::Mode::relative;                   // 240 px per full track, right = longer
                const fcdsp::ParamSpec* sp = m.spec();
                s.invert = sp != nullptr && sp->display.invert;
                s.u0 = s.invert ? 1.0f - v.track : v.track;
                s.downX = e.x;
            }
        }
        ctx_.gestures->beginDrag(*port);
    }

    void StepPlot::pointerDrag(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointer = { e.x, e.y };
        if (!s.dragging || ctx_.gestures == nullptr || !ctx_.gestures->dragging())
            return;
        SlotModel& m = ctx_.slot(s.pid);
        switch (s.mode)
        {
            case State::Mode::absolute:
            {
                float host01 = 0.0f;
                if (m.plotToHost01(static_cast<float>(tOf(geom_, e.x - s.grabPx)), host01))
                    ctx_.gestures->dragTo(host01);
                break;
            }
            case State::Mode::relative:
            {
                const float travel = (e.x - s.downX) / (e.mods.shift ? kTrackPxFine : kTrackPx);
                const float u = std::clamp(s.u0 + travel, 0.0f, 1.0f);
                ctx_.gestures->dragTo(m.host01FromTrack(s.invert ? 1.0f - u : u));
                break;
            }
            case State::Mode::stepped:
            {
                // The detent nearest the pointer, once it is nearer than the current one by the hysteresis (6 px).
                const funkgui::ValueView& v = s.slider.view();
                if (v.detents == nullptr || s.detent < 0 || s.detent >= s.nDetents)
                    break;
                int best = s.detent;
                float bestD = std::fabs(e.x - s.detentX[static_cast<std::size_t>(s.detent)]);
                for (int i = 0; i < s.nDetents; ++i)
                {
                    const float d = std::fabs(e.x - s.detentX[static_cast<std::size_t>(i)]);
                    if (d + layout::band::kSnapHysteresisPx < bestD)
                    {
                        best = i;
                        bestD = d + layout::band::kSnapHysteresisPx;
                    }
                }
                if (best != s.detent && best < v.nDetents)
                {
                    s.detent = best;
                    ctx_.gestures->dragTo(v.detents[best].host01);
                }
                break;
            }
            case State::Mode::none:
            case State::Mode::refused:
                break;
        }
    }

    void StepPlot::pointerUp(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        const bool writing = s.mode == State::Mode::absolute || s.mode == State::Mode::relative
                          || s.mode == State::Mode::stepped;
        if (writing && ctx_.gestures != nullptr)
            ctx_.gestures->endDrag();
        s.mode = State::Mode::none;
        s.dragging = false;
        s.hot = s.markerAt({ e.x, e.y }, geom_);
    }

    void StepPlot::doubleClick(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        if (!s.markerAt({ e.x, e.y }, geom_) || ctx_.gestures == nullptr)
            return;
        s.slider.doubleClick(*ctx_.gestures);                     // the Mode default, as the slot (02 §7.4)
        if (writable(s.slider.view().state))
            ctx_.touch(s.pid);
    }

    bool StepPlot::wheel(const funkgui::WheelEvent& e)
    {
        State& s = *st_;
        if (!s.markerAt({ e.x, e.y }, geom_) || ctx_.gestures == nullptr || ctx_.host == nullptr)
            return false;
        const bool used = s.slider.wheel(e, *ctx_.gestures, ctx_.host->nowSeconds());
        if (used && writable(s.slider.view().state))
            ctx_.touch(s.pid);
        return used;
    }

    bool StepPlot::key(const funkgui::KeyEvent& e)
    {
        State& s = *st_;
        if (ctx_.gestures == nullptr || ctx_.focus != s.slider.a11yId())
            return false;
        const bool used = s.slider.key(e, *ctx_.gestures);         // the slot's keys (02 §7.5, §8.9)
        if (used && writable(s.slider.view().state))
            ctx_.touch(s.pid);
        return used;
    }

    funkgui::Cursor StepPlot::cursor(funkgui::Point p) const
    {
        const State& s = *st_;
        if (s.dragging || s.markerAt(p, geom_))
            return writable(s.state) ? funkgui::Cursor::leftRight : funkgui::Cursor::crosshair;
        return funkgui::Cursor::normal;
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------------

    void StepPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const State& s = *st_;
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = s.title[0] != '\0' ? s.title
                                      : (geom_.kind == layout::StepKind::attack ? "Attack response" : "Release response");
        it.readOnly = true;
        out.push_back(std::move(it));
        if (s.state == funkgui::ValueState::na)
            return;                                               // 02 §7.5: n/a handles are skipped
        funkgui::A11yItem h;
        s.slider.accessibility(h);
        const float r = C::kMarkerHitPx;
        h.bounds = s.specShown ? funkgui::Rect{ s.specX - r, geom_.plot.y, 2.0f * r, geom_.plot.h } : geom_.plot;
        const SlotModel& m = ctx_.slot(s.pid);
        const fcdsp::ParamSpec* sp = m.spec();
        const funkgui::ValueState vs = s.slider.view().state;
        if (sp != nullptr && h.role == funkgui::A11yRole::slider && vs != funkgui::ValueState::stepped)
        {
            // The slot's value interface in display units (SlotGrid's rewrite of RuleSlider's track space), on the
            // slider's state as SlotGrid reads it: a hybrid on one of its steps is continuous there (Diode 54's 5 MS
            // attack, v1.2), where the resolved SlotState says stepped.
            const PlainRange span = trackSpan(s.pid, *sp);
            const double a = toDisplayUnits(*sp, span.a), b = toDisplayUnits(*sp, span.b);
            h.lo = std::min(a, b);
            h.hi = std::max(a, b);
            h.step = (h.hi - h.lo) * 0.01;
            h.v = std::clamp(toDisplayUnits(*sp, m.resolved().plain), h.lo, h.hi);
        }
        out.push_back(std::move(h));
    }

    int StepPlot::focusOrder(std::span<uint32_t> out) const
    {
        const State& s = *st_;
        if (s.state == funkgui::ValueState::na || out.empty())
            return 0;
        out[0] = s.slider.a11yId();                               // 02 §7.5: ATTACK, RELEASE (locked ones read-only)
        return 1;
    }

    void StepPlot::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        State& s = *st_;
        if (ctx_.gestures == nullptr || a == funkgui::A11yAction::focus || id != s.slider.a11yId())
            return;
        const SlotModel& m = ctx_.slot(s.pid);
        double v = value;
        if (a == funkgui::A11yAction::setValue && m.spec() != nullptr && !std::isnan(value)
            && s.slider.view().state == funkgui::ValueState::continuous)
        {
            // Display units back to the track (accessibility() rewrote the value interface).
            const PlainRange span = trackSpan(s.pid, *m.spec());
            const float plain = fromDisplayUnits(*m.spec(), value);
            const float lo = std::min(span.a, span.b), hi = std::max(span.a, span.b);
            v = static_cast<double>(std::clamp(m.trackPosition(std::clamp(plain, lo, hi)), 0.0f, 1.0f));
        }
        s.slider.a11yAction(a, v, *ctx_.gestures);
        const bool write = a == funkgui::A11yAction::setValue || a == funkgui::A11yAction::increment
                        || a == funkgui::A11yAction::decrement;
        if (write && writable(s.slider.view().state))
            ctx_.touch(s.pid);
    }

    uint32_t StepPlot::a11yRevision() const { return st_->revision; }
}
