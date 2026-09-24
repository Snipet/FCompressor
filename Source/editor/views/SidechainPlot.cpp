// Source/editor/views/SidechainPlot.cpp — the SIDECHAIN pane (see SidechainPlot.h; 02 §7.3, §7.4, §7.5, §9.3; 01 §7).
// tick() rebuilds the curve and the handle in logical px when what they depend on changed; draw() only emits.
#include "editor/views/SidechainPlot.h"

#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/widgets/FocusRing.h>
#include <funkgui/widgets/RuleSlider.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        namespace C = layout::chars;
        using fcdsp::Pid;

        constexpr int   kMaxPoints = 256;                          // >= SidechainGeom::points (161)
        constexpr float kDefaultFs = 48000.0f;                     // no UiFrame read yet
        constexpr float kGridDb = 6.0f;                            // dB grid every 6 dB
        constexpr float kHandleR = 2.5f;                           // the 5×5 ring
        constexpr float kHandleHitPad = 3.0f;
        constexpr float kCrossHalf = 2.5f;
        constexpr float kInset = 4.0f;
        constexpr float kLabelGap = 5.0f;                          // the handle's value beside the ring
        constexpr float kLine = 12.0f;                             // kMicro line pitch inside the plot
        constexpr float kNoLevelDb = -120.0f;                      // an external key quieter than this prints no level
        constexpr float kTitleS = 0.25f;
        constexpr uint32_t kImageId = 1;

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

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        // The log-frequency axis and the dB axis of the plot.
        float xOf(const layout::SidechainGeom& g, double hz) noexcept
        {
            const double u = std::log(hz / static_cast<double>(g.fMinHz))
                           / std::log(static_cast<double>(g.fMaxHz) / static_cast<double>(g.fMinHz));
            return g.plot.x + static_cast<float>(u) * g.plot.w;
        }

        double hzOf(const layout::SidechainGeom& g, float x) noexcept
        {
            const double u = static_cast<double>((x - g.plot.x) / g.plot.w);
            return static_cast<double>(g.fMinHz)
                 * std::pow(static_cast<double>(g.fMaxHz) / static_cast<double>(g.fMinHz), u);
        }

        float yOf(const layout::SidechainGeom& g, float db) noexcept
        {
            return g.plot.y + (g.dbTop - db) * g.plot.h / (g.dbTop - g.dbBottom);
        }

        // The handle's x for a HPF corner: OFF (below 20 Hz) sits on the left edge.
        float handleX(const layout::SidechainGeom& g, float hz) noexcept
        {
            if (!(hz >= C::kScOffHz))
                return g.plot.x;
            return std::clamp(xOf(g, static_cast<double>(hz)), g.plot.x, g.plot.right());
        }

        // Draws the parts of the polyline inside [top, bottom] (x increases along it; TransferPlot's rule).
        void clippedPolyline(funkgui::Canvas& c, const float* xs, const float* ys, int n, float top, float bottom,
                             float width, funkgui::Col col)
        {
            for (int i = 0; i + 1 < n; ++i)
            {
                float x0 = xs[i], y0 = ys[i], x1 = xs[i + 1], y1 = ys[i + 1];
                if ((y0 < top && y1 < top) || (y0 > bottom && y1 > bottom))
                    continue;
                const auto clip = [](float& xa, float& ya, float xb, float yb, float yc) {
                    const float t = (yc - ya) / (yb - ya);
                    xa += t * (xb - xa);
                    ya = yc;
                };
                if (y0 < top)
                    clip(x0, y0, x1, y1, top);
                else if (y0 > bottom)
                    clip(x0, y0, x1, y1, bottom);
                if (y1 < top)
                    clip(x1, y1, x0, y0, top);
                else if (y1 > bottom)
                    clip(x1, y1, x0, y0, bottom);
                if (x1 > x0 || y1 != y0)
                    c.segment(x0, y0, x1, y1, width, col);
            }
        }

        // a11y display units (SlotGrid's rule): the Mode's DisplayMap, else hertz as is.
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
        PlainRange trackSpan(const fcdsp::ParamSpec& s) noexcept
        {
            if (s.lo < s.hi)
                return { s.lo, s.hi };
            const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(Pid::schpf)];
            return { h.lo, h.hi };
        }

        float scPeak(const fcdsp::UiFrame& f) noexcept { return std::max(f.scPeakDb[0], f.scPeakDb[1]); }
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct SidechainPlot::State
    {
        State(PanelContext& ctx, uint32_t handleId)
            : slider(ctx.slot(Pid::schpf), layout::slotGeom(*layout::slotOf(Pid::schpf)), handleId)
        {
        }

        void build(const PanelContext&, const layout::SidechainGeom&, float fs);
        bool handleAt(funkgui::Point) const noexcept;

        funkgui::RuleSlider slider;                              // never drawn: wheel, keys, a11y, spec lines

        // what build() derived
        uint32_t builtEng = ~0u, builtResolve = ~0u;
        const fcdsp::ModeEntry* builtEntry = nullptr;
        float    builtFs = 0.0f;
        int      n = 0;
        std::array<float, kMaxPoints> x{}, y{};                  // the curve in px (y unclipped)
        funkgui::ValueState state = funkgui::ValueState::na;
        bool     handleShown = false;
        float    hx = 0.0f, hy = 0.0f;
        int      nDetents = 0;
        std::array<float, SlotModel::kMaxDetents> detentX{};

        // pointer, drag
        bool  pointerOver = false;
        bool  hot = false;
        enum class Mode : uint8_t { none, refused, absolute, stepped };
        Mode  mode = Mode::none;
        bool  dragging = false;
        float grabPx = 0.0f;
        int   detent = -1;

        uint32_t revision = 0;
        bool     wasNa = true;

        char     title[200] = "Side-chain response";
        float    titleAge = kTitleS;
    };

    void SidechainPlot::State::build(const PanelContext& ctx, const layout::SidechainGeom& g, float fs)
    {
        const FrameState& f = ctx.frame;
        builtEng = f.engSerial;
        builtResolve = f.resolveSerial;
        builtEntry = f.entry;
        builtFs = fs;
        n = 0;
        handleShown = false;
        nDetents = 0;
        state = f.entry != nullptr ? stateOf(f.res.view[Pid::schpf].state) : funkgui::ValueState::na;
        const bool na = state == funkgui::ValueState::na;
        if (na != wasNa)
        {
            wasNa = na;
            ++revision;
        }
        if (f.entry == nullptr)
            return;

        // SC_CURVE: 161 log-spaced points, the whole detector path (01 §7).
        n = std::clamp(g.points, 2, kMaxPoints);
        std::array<float, kMaxPoints> hz{}, db{};
        const double ratio = static_cast<double>(g.fMaxHz) / static_cast<double>(g.fMinHz);
        for (int i = 0; i < n; ++i)
            hz[static_cast<std::size_t>(i)] = static_cast<float>(
                static_cast<double>(g.fMinHz) * std::pow(ratio, static_cast<double>(i) / static_cast<double>(n - 1)));
        const auto un = static_cast<std::size_t>(n);
        fcdsp::analysis::scResponse(*f.entry, f.eng, fs, std::span<const float>(hz.data(), un),
                                    std::span<float>(db.data(), un));
        for (int i = 0; i < n; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            x[u] = g.plot.x + g.plot.w * static_cast<float>(i) / static_cast<float>(n - 1);
            y[u] = yOf(g, db[u]);
        }

        // SC_HANDLE at the corner the audio uses (the smoothed scHpfHz while live).
        handleShown = !na;
        hx = handleX(g, f.eng.scHpfHz);
        hy = yOf(g, C::kScHandleDb);
        if (state == funkgui::ValueState::stepped)
        {
            const funkgui::ValueView& v = slider.view();
            nDetents = std::min(v.detents != nullptr ? v.nDetents : 0, SlotModel::kMaxDetents);
            for (int i = 0; i < nDetents; ++i)
                detentX[static_cast<std::size_t>(i)] = handleX(g, fcdsp::toPlain(Pid::schpf, v.detents[i].host01));
        }
    }

    bool SidechainPlot::State::handleAt(funkgui::Point p) const noexcept
    {
        if (!handleShown)
            return false;
        const float r = kHandleR + kHandleHitPad;
        const float dx = p.x - hx, dy = p.y - hy;
        return dx * dx + dy * dy <= r * r;
    }

    // ---- construction ----------------------------------------------------------------------------------------------------

    SidechainPlot::SidechainPlot(PanelContext& ctx, const layout::SidechainGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>(ctx, idBase + kPlotHandleIds))
    {
    }

    SidechainPlot::~SidechainPlot() = default;

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void SidechainPlot::tick(float dt)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        const uint32_t handleId = s.slider.a11yId();
        s.slider.tick(dt, false, ctx_.focusVisible && ctx_.focus == handleId, false);
        const float fs = f.hasFrame && f.ui.sampleRate > 0.0f ? f.ui.sampleRate : kDefaultFs;
        const bool rebuilt = f.engSerial != s.builtEng || f.resolveSerial != s.builtResolve || f.entry != s.builtEntry
                          || !funkgui::ease::sameBits(fs, s.builtFs);
        if (rebuilt)
            s.build(ctx_, geom_, fs);

        char line[sizeof ctx_.handNext.spec];
        const auto offer = [&](HandKind kind) {
            s.slider.specLine(line, sizeof line);
            ctx_.offerHand(Pid::schpf, kind, handleId, line);
        };
        if (s.dragging && ctx_.pointerPressed)
            offer(HandKind::drag);
        else if (s.pointerOver && s.hot)
            offer(HandKind::hover);
        if (ctx_.focusVisible && ctx_.focus == handleId && s.state != funkgui::ValueState::na)
            offer(HandKind::focus);

        // "Side-chain response: high-pass 120 hertz, internal key", regenerated <= 4 Hz (and at once on a rebuild).
        s.titleAge += std::max(dt, 0.0f);
        if (rebuilt || s.titleAge >= kTitleS)
        {
            s.titleAge = 0.0f;
            const funkgui::ValueView& v = s.slider.view();
            const bool external = f.fresh && (f.ui.flags & fcdsp::kUiExtKeyActive) != 0;
            const bool listening = ctx_.facade.port(Pid::listen).value01() >= 0.5f;
            if (f.entry == nullptr)
                std::snprintf(s.title, sizeof s.title, "Side-chain response");
            else if (s.state == funkgui::ValueState::na)
                std::snprintf(s.title, sizeof s.title, "Side-chain response: %s key%s", external ? "external" : "internal",
                              listening ? ", listening" : "");
            else
                std::snprintf(s.title, sizeof s.title, "Side-chain response: high-pass %s%s%s, %s key%s",
                              v.text.spoken[0] != '\0' ? v.text.spoken : v.text.value,
                              v.text.spoken[0] != '\0' || v.text.unit[0] == '\0' ? "" : " ",
                              v.text.spoken[0] != '\0' ? "" : v.text.unit, external ? "external" : "internal",
                              listening ? ", listening" : "");
        }
    }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void SidechainPlot::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const FrameState& f = ctx_.frame;
        const layout::SidechainGeom& g = geom_;
        const funkgui::Rect& p = g.plot;
        const uint32_t handleId = s.slider.a11yId();
        const bool focused = ctx_.focusVisible && ctx_.focus == handleId;
        const Pid handPid = ctx_.hand.kind != HandKind::none ? ctx_.hand.pid : fcdsp::kNoPid;
        const bool hotCurve = handPid == Pid::schpf || handPid == Pid::sce;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, p, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            for (float db = g.dbTop - kGridDb; db > g.dbBottom; db -= kGridDb)
                c.hairlineH(p.x + 1.0f, yOf(g, db), p.w - 2.0f, th.ink16);
            for (const layout::AxisLabel& l : layout::sidechain::kLabels)
            {
                const float x = xOf(g, static_cast<double>(l.value));
                if (x > p.x + 1.0f && x < p.right() - 1.0f)
                    c.hairlineV(x, p.y + 1.0f, p.h - 2.0f, th.ink16);
            }
        }
        if (s.n > 1)
        {
            const funkgui::Canvas::Scope scope(c, tag::scCurve, false);
            clippedPolyline(c, s.x.data(), s.y.data(), s.n, p.y, p.bottom(), hotCurve ? 1.5f : 1.0f,
                            funkgui::premix(th.ground, hotCurve ? th.accent : th.ink70, 1.0f));
        }
        // The caption, top-right (the HPF handle lives on the left): INTERNAL | EXTERNAL · −14 DB PK (live text), and
        // LISTENING under it.
        const bool external = f.fresh && (f.ui.flags & fcdsp::kUiExtKeyActive) != 0;
        char caption[48] = "INTERNAL";
        if (external)
        {
            char v[16];
            if (scPeak(f.ui) > kNoLevelDb && funkgui::fmt::db(scPeak(f.ui), 0, v, sizeof v) >= 0)
                std::snprintf(caption, sizeof caption, "EXTERNAL · %s DB PK", v);
            else
                std::snprintf(caption, sizeof caption, "EXTERNAL");
        }
        const bool listening = ctx_.facade.port(Pid::listen).value01() >= 0.5f;
        const float capW = c.textWidth(caption, T::kMicro);
        const funkgui::Rect capBox { p.right() - kInset - capW, p.y + kInset, capW, (listening ? 2.0f : 1.0f) * kLine };
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, external);
            c.text(caption, p.right() - kInset, p.y + kInset, T::kMicro, th.ink32, funkgui::Align::right);
        }
        if (listening)
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("LISTENING", p.right() - kInset, p.y + kInset + kLine, T::kMicro, th.ink100, funkgui::Align::right);
        }
        if (s.handleShown)
        {
            const funkgui::Canvas::Scope scope(c, tag::scHandle, false);
            if (writable(s.state))
            {
                const bool hot = s.hot || s.dragging || focused;
                c.disc(s.hx, s.hy, kHandleR, th.ground, 1.0f, hot ? th.accent : th.ink70);
            }
            else
            {
                c.hairlineH(s.hx - kCrossHalf, s.hy, 2.0f * kCrossHalf, th.ink32);
                c.hairlineV(s.hx, s.hy - kCrossHalf, 2.0f * kCrossHalf, th.ink32);
            }
            // The corner's value ("120 HZ", "OFF") beside the ring on the side with room, or under it where the
            // caption is.
            const funkgui::ValueView& v = s.slider.view();
            char text[40];
            std::snprintf(text, sizeof text, "%s%s%s", v.text.value, v.text.unit[0] != '\0' ? " " : "", v.text.unit);
            const float w = c.textWidth(text, T::kMicro);
            const float right = s.hx + kHandleR + kLabelGap;
            float tx = right + w <= p.right() - kInset ? right : s.hx - kHandleR - kLabelGap - w;
            float ty = c.capCentreTop(s.hy, T::kMicro);
            const bool clash = tx < capBox.right() && capBox.x < tx + w && ty < capBox.bottom() && capBox.y < ty + kLine;
            if (clash)
            {
                tx = std::clamp(s.hx - 0.5f * w, p.x + 1.0f, p.right() - kInset - w);
                ty = s.hy + kHandleR + kLabelGap;
            }
            c.text(text, tx, ty, T::kMicro, th.ink52);
        }
        {
            // Frequency labels under the plot (the last one carries the unit), kept inside the plot's width.
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            const auto& labels = layout::sidechain::kLabels;
            for (std::size_t i = 0; i < labels.size(); ++i)
            {
                char text[16];
                std::snprintf(text, sizeof text, "%s%s", labels[i].text, i + 1 == labels.size() ? " HZ" : "");
                const float w = c.textWidth(text, T::kMicro);
                const float x = std::clamp(xOf(g, static_cast<double>(labels[i].value)) - 0.5f * w, p.x, p.right() - w);
                c.text(text, x, layout::sidechain::kLabelY, T::kMicro, th.ink32);
            }
        }
        if (focused && s.handleShown)
            funkgui::drawFocusRing(c, { s.hx - 6.0f, s.hy - 6.0f, 12.0f, 12.0f }, th.accent);
        const funkgui::AxisMap xAxis { p.x, p.right(), g.fMinHz, g.fMaxHz, true };
        const funkgui::AxisMap yAxis { p.y, p.bottom(), g.dbTop, g.dbBottom, false };
        c.axis(tag::scAxis, &xAxis, &yAxis);
    }

    // ---- hit testing and input -------------------------------------------------------------------------------------------

    bool SidechainPlot::hit(funkgui::Point p) const { return geom_.plot.contains(p); }

    void SidechainPlot::pointerMove(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointerOver = true;
        s.hot = s.handleAt({ e.x, e.y });
    }

    void SidechainPlot::pointerExit()
    {
        State& s = *st_;
        s.pointerOver = false;
        s.hot = false;
    }

    void SidechainPlot::pointerDown(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        s.pointerOver = true;
        s.mode = State::Mode::none;
        s.dragging = false;
        if (ctx_.gestures == nullptr || ctx_.host == nullptr || !s.handleAt({ e.x, e.y }))
            return;
        SlotModel& m = ctx_.slot(Pid::schpf);
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
            s.mode = State::Mode::absolute;                       // hertz on the log axis, the grab kept
            s.grabPx = e.x - s.hx;
        }
        ctx_.gestures->beginDrag(*port);
    }

    void SidechainPlot::pointerDrag(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        if (!s.dragging || ctx_.gestures == nullptr || !ctx_.gestures->dragging())
            return;
        SlotModel& m = ctx_.slot(Pid::schpf);
        switch (s.mode)
        {
            case State::Mode::absolute:
            {
                // Left past the plot's 20 Hz edge is OFF (SlotModel::plotToHost01 maps < 20 Hz to 0).
                const float x = e.x - s.grabPx;
                const float hz = x < geom_.plot.x ? 0.0f : static_cast<float>(hzOf(geom_, x));
                float host01 = 0.0f;
                if (m.plotToHost01(hz, host01))
                    ctx_.gestures->dragTo(host01);
                break;
            }
            case State::Mode::stepped:
            {
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

    void SidechainPlot::pointerUp(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        if ((s.mode == State::Mode::absolute || s.mode == State::Mode::stepped) && ctx_.gestures != nullptr)
            ctx_.gestures->endDrag();
        s.mode = State::Mode::none;
        s.dragging = false;
        s.hot = s.handleAt({ e.x, e.y });
    }

    void SidechainPlot::doubleClick(const funkgui::PointerEvent& e)
    {
        State& s = *st_;
        if (!s.handleAt({ e.x, e.y }) || ctx_.gestures == nullptr)
            return;
        s.slider.doubleClick(*ctx_.gestures);                     // the Mode default, as the slot (02 §7.4)
        if (writable(s.slider.view().state))
            ctx_.touch(Pid::schpf);
    }

    bool SidechainPlot::wheel(const funkgui::WheelEvent& e)
    {
        State& s = *st_;
        if (!s.handleAt({ e.x, e.y }) || ctx_.gestures == nullptr || ctx_.host == nullptr)
            return false;
        const bool used = s.slider.wheel(e, *ctx_.gestures, ctx_.host->nowSeconds());
        if (used && writable(s.slider.view().state))
            ctx_.touch(Pid::schpf);
        return used;
    }

    bool SidechainPlot::key(const funkgui::KeyEvent& e)
    {
        State& s = *st_;
        if (ctx_.gestures == nullptr || ctx_.focus != s.slider.a11yId())
            return false;
        const bool used = s.slider.key(e, *ctx_.gestures);         // the slot's keys (02 §7.5, §8.9)
        if (used && writable(s.slider.view().state))
            ctx_.touch(Pid::schpf);
        return used;
    }

    funkgui::Cursor SidechainPlot::cursor(funkgui::Point p) const
    {
        const State& s = *st_;
        if (s.dragging || s.handleAt(p))
            return writable(s.state) ? funkgui::Cursor::leftRight : funkgui::Cursor::crosshair;
        return funkgui::Cursor::normal;
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------------

    void SidechainPlot::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const State& s = *st_;
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.plot;
        it.title = s.title;
        it.readOnly = true;
        out.push_back(std::move(it));
        if (s.state == funkgui::ValueState::na)
            return;                                               // 02 §7.5: n/a handles are skipped
        funkgui::A11yItem h;
        s.slider.accessibility(h);
        const float r = kHandleR + kHandleHitPad;
        h.bounds = s.handleShown ? funkgui::Rect{ s.hx - r, s.hy - r, 2.0f * r, 2.0f * r } : geom_.plot;
        const SlotModel& m = ctx_.slot(Pid::schpf);
        const fcdsp::ParamSpec* sp = m.spec();
        if (sp != nullptr && h.role == funkgui::A11yRole::slider && s.state != funkgui::ValueState::stepped)
        {
            const PlainRange span = trackSpan(*sp);
            const double a = toDisplayUnits(*sp, span.a), b = toDisplayUnits(*sp, span.b);
            h.lo = std::min(a, b);
            h.hi = std::max(a, b);
            h.step = (h.hi - h.lo) * 0.01;
            h.v = std::clamp(toDisplayUnits(*sp, m.resolved().plain), h.lo, h.hi);
        }
        out.push_back(std::move(h));
    }

    int SidechainPlot::focusOrder(std::span<uint32_t> out) const
    {
        const State& s = *st_;
        if (s.state == funkgui::ValueState::na || out.empty())
            return 0;
        out[0] = s.slider.a11yId();                               // 02 §7.5: SC HPF, while SIDECHAIN is shown
        return 1;
    }

    void SidechainPlot::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        State& s = *st_;
        if (ctx_.gestures == nullptr || a == funkgui::A11yAction::focus || id != s.slider.a11yId())
            return;
        const SlotModel& m = ctx_.slot(Pid::schpf);
        double v = value;
        if (a == funkgui::A11yAction::setValue && m.spec() != nullptr && !std::isnan(value)
            && s.slider.view().state == funkgui::ValueState::continuous)
        {
            const PlainRange span = trackSpan(*m.spec());
            const float plain = fromDisplayUnits(*m.spec(), value);
            const float lo = std::min(span.a, span.b), hi = std::max(span.a, span.b);
            v = static_cast<double>(std::clamp(m.trackPosition(std::clamp(plain, lo, hi)), 0.0f, 1.0f));
        }
        s.slider.a11yAction(a, v, *ctx_.gestures);
        const bool write = a == funkgui::A11yAction::setValue || a == funkgui::A11yAction::increment
                        || a == funkgui::A11yAction::decrement;
        if (write && writable(s.slider.view().state))
            ctx_.touch(Pid::schpf);
    }

    uint32_t SidechainPlot::a11yRevision() const { return st_->revision; }
}
