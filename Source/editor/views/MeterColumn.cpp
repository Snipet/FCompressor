// Source/editor/views/MeterColumn.cpp — METERS (see MeterColumn.h; 02 §6.3, §7.3, §8.8): the bars on the plot's level
// map, the holds, the band's readouts and the reset.
#include "editor/views/MeterColumn.h"

#include "editor/Tags.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/widgets/FocusRing.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        namespace M = layout::meters;
        using layout::MeterBar;

        constexpr float kSilentDb = -200.0f;                       // the telemetry floor (01 §6.2)
        constexpr float kShownGrDb = 0.05f;                        // a GR hold tick below this is not drawn
        constexpr uint32_t kBarId = 2, kResetId = 8;               // local ids: bars kBarId + i, the reset button
        constexpr int kMaxBars = 5;

        constexpr const char* kResetSpec = "METERS   MAX IN · GR · OUT SINCE RESET   CLICK OR RETURN RESETS THE HOLDS";
        constexpr const char* kDash = "\xE2\x80\x93";                // U+2013: before the first frame only (ADR-69)

        // A level readout: "−12.3", or "−∞" at the telemetry floor (after the first frame a level is never "–").
        void levelText(float db, char* out, std::size_t n) noexcept
        {
            if (funkgui::fmt::db(db > kSilentDb + 1.0f ? db : -INFINITY, 1, out, n) < 0)
                std::snprintf(out, n, "%s", kDash);
        }

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        bool isGr(MeterBar w) noexcept { return w == MeterBar::gr; }

        funkgui::Tag barTag(MeterBar w) noexcept
        {
            switch (w)
            {
                case MeterBar::inL: case MeterBar::inR: case MeterBar::in:    return tag::meterIn;
                case MeterBar::outL: case MeterBar::outR: case MeterBar::out: return tag::meterOut;
                case MeterBar::sc:                                            return tag::meterSc;
                case MeterBar::gr:                                            return tag::meterGr;
            }
            return tag::meterIn;
        }

        const char* barTitle(MeterBar w) noexcept
        {
            switch (w)
            {
                case MeterBar::inL:  return "Input left";
                case MeterBar::inR:  return "Input right";
                case MeterBar::in:   return "Input";
                case MeterBar::sc:   return "Side chain";
                case MeterBar::gr:   return "Gain reduction";
                case MeterBar::outL: return "Output left";
                case MeterBar::outR: return "Output right";
                case MeterBar::out:  return "Output";
            }
            return "";
        }

        struct Reading
        {
            float peak = kSilentDb, rms = kSilentDb, hold = kSilentDb;   // GR: applied, -, block max (dB of GR)
            bool  present = false;
        };

        // What the frame says for one bar (02 §8.8, §9.1): per output channel on the band, the max over them on the
        // Characteristics screen; GR the max over the engine lanes.
        Reading read(MeterBar w, const fcdsp::UiFrame& u) noexcept
        {
            Reading r;
            r.present = true;
            switch (w)
            {
                case MeterBar::inL:  r.peak = u.inPeakDb[0];  r.rms = u.inRmsDb[0];  break;
                case MeterBar::inR:  r.peak = u.inPeakDb[1];  r.rms = u.inRmsDb[1];  break;
                case MeterBar::in:
                    r.peak = std::max(u.inPeakDb[0], u.inPeakDb[1]);
                    r.rms = std::max(u.inRmsDb[0], u.inRmsDb[1]);
                    break;
                case MeterBar::outL: r.peak = u.outPeakDb[0]; r.rms = u.outRmsDb[0]; break;
                case MeterBar::outR: r.peak = u.outPeakDb[1]; r.rms = u.outRmsDb[1]; break;
                case MeterBar::out:
                    r.peak = std::max(u.outPeakDb[0], u.outPeakDb[1]);
                    r.rms = std::max(u.outRmsDb[0], u.outRmsDb[1]);
                    break;
                case MeterBar::sc:
                    r.present = (u.flags & fcdsp::kUiExtKeyActive) != 0;
                    r.peak = std::max(u.scPeakDb[0], u.scPeakDb[1]);
                    break;
                case MeterBar::gr:
                    r.peak = std::max(std::max(u.appliedGrDb[0], u.appliedGrDb[1]), 0.0f);
                    r.hold = std::max(std::max(u.blockMaxGrDb[0], u.blockMaxGrDb[1]), r.peak);
                    break;
            }
            if (!isGr(w))
                r.hold = r.peak;
            return r;
        }
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct MeterColumn::State
    {
        struct Bar
        {
            float peak = kSilentDb, rms = kSilentDb;              // shown (GR bars: dB of GR, from 0)
            float hold = kSilentDb, holdAge = 0.0f;
            bool  present = false;
        };
        std::array<Bar, kMaxBars> bars{};
        float inMax = kSilentDb, grMax = 0.0f, outMax = kSilentDb;   // since the last reset
        bool  over = false;
        bool  seen = false;                                       // a frame arrived (ADR-69: "–" only before that)
        bool  pointerOver = false, resetHover = false;

        void reset() noexcept
        {
            inMax = outMax = kSilentDb;                           // "−∞", "0.0", "−∞" until the next frame
            grMax = 0.0f;
            over = false;
            for (Bar& b : bars)
            {
                b.hold = b.peak;
                b.holdAge = 0.0f;
            }
        }
    };

    MeterColumn::MeterColumn(PanelContext& ctx, const layout::MeterGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>())
    {
        for (int i = 0; i < geom_.nBars; ++i)
            if (isGr(geom_.bars[static_cast<std::size_t>(i)].what))
                st_->bars[static_cast<std::size_t>(i)].peak = st_->bars[static_cast<std::size_t>(i)].hold = 0.0f;
    }

    MeterColumn::~MeterColumn() = default;

    // ---- tick -------------------------------------------------------------------------------------------------------------

    void MeterColumn::tick(float dt)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        const float t = std::max(dt, 0.0f);
        const float fall = M::kFallDbPerS * t;
        // ADR-69: a fresh frame (live or silent) is drawn as it is; with no fresh frame (stale, or none yet) the bars
        // and holds fall at 20 dB/s to the floor (02 §8.8). The engine's own meter envelopes bring a silent feed down.
        const bool fresh = telemetry::feed(ctx_) == telemetry::Feed::fresh;
        for (int i = 0; i < geom_.nBars && i < kMaxBars; ++i)
        {
            const MeterBar w = geom_.bars[static_cast<std::size_t>(i)].what;
            State::Bar& b = s.bars[static_cast<std::size_t>(i)];
            const Reading r = read(w, f.ui);
            const float rest = isGr(w) ? 0.0f : kSilentDb;
            b.present = r.present;
            if (fresh && r.present)
            {
                b.peak = r.peak;
                b.rms = r.rms;
            }
            else
            {
                b.peak = std::max(rest, b.peak - fall);           // no fresh frame: fall at 20 dB/s (02 §8.8)
                b.rms = std::max(rest, b.rms - fall);
            }
            const float src = fresh && r.present ? r.hold : b.peak;
            if (src >= b.hold)
            {
                b.hold = src;
                b.holdAge = 0.0f;
            }
            else
            {
                b.holdAge += t;
                if (b.holdAge > M::kHoldS || !fresh)
                    b.hold = std::max(src, b.hold - fall);        // hold 1.5 s, then fall at 20 dB/s
            }
        }
        if (fresh)
        {
            const fcdsp::UiFrame& u = f.ui;
            s.inMax = std::max(s.inMax, std::max(u.inPeakDb[0], u.inPeakDb[1]));
            s.outMax = std::max(s.outMax, std::max(u.outPeakDb[0], u.outPeakDb[1]));
            s.grMax = std::max(s.grMax, std::max(std::max(u.blockMaxGrDb[0], u.blockMaxGrDb[1]), 0.0f));
            s.seen = true;
            s.over = s.over || (u.flags & fcdsp::kUiOutOver) != 0;
        }
        if (s.pointerOver && s.resetHover)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, idBase_ + kResetId, kResetSpec);
        if (ctx_.focusVisible && ctx_.focus == idBase_ + kResetId)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, idBase_ + kResetId, kResetSpec);
    }

    // ---- draw -------------------------------------------------------------------------------------------------------------

    void MeterColumn::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        const float scale = static_cast<float>(ctx_.meterScaleDb);
        const float floorDb = layout::kLevelTopDb - scale;
        const layout::LevelMap& lm = geom_.level;
        const float top = lm.top, bottom = lm.top + lm.height;
        const float ppd = lm.pxPerDb(scale);
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            for (int i = 0; i < geom_.nBars; ++i)
                outline(c, geom_.bars[static_cast<std::size_t>(i)].r, th.ink16);
        }
        for (int i = 0; i < geom_.nBars && i < kMaxBars; ++i)
        {
            const layout::MeterGeom::Bar& g = geom_.bars[static_cast<std::size_t>(i)];
            const State::Bar& b = s.bars[static_cast<std::size_t>(i)];
            const funkgui::Rect& r = g.r;
            if (isGr(g.what))
            {
                const float h = std::clamp(b.peak * ppd, 0.0f, lm.height);
                if (h > 0.0f)
                {
                    const funkgui::Canvas::Scope scope(c, tag::meterGr, true);
                    c.rrect(r.x, top, r.w, h, 0.0f, th.signal);  // hangs from the top (02 §8.8)
                }
                if (b.hold > kShownGrDb)
                {
                    const funkgui::Canvas::Scope scope(c, tag::meterHold, true);
                    c.hairlineH(r.x, std::min(top + b.hold * ppd, bottom - 1.0f), r.w, th.ink100);
                }
                continue;
            }
            if (b.peak > floorDb)
            {
                const funkgui::Canvas::Scope scope(c, barTag(g.what), true);
                const float y = lm.y(std::min(b.peak, layout::kLevelTopDb), scale);
                c.rrect(r.x, y, r.w, bottom - y, 0.0f, th.ink32);
                if (b.peak > 0.0f)                                // above 0 dBFS: ink100, no red
                    c.rrect(r.x, y, r.w, lm.y(0.0f, scale) - y, 0.0f, th.ink100);
                if (b.rms > floorDb)
                {
                    const float yr = lm.y(std::min(b.rms, layout::kLevelTopDb), scale);
                    c.rrect(r.centreX() - 0.5f * geom_.rmsWidth, yr, geom_.rmsWidth, bottom - yr, 0.0f, th.ink70);
                }
            }
            if (b.hold > floorDb)
            {
                const funkgui::Canvas::Scope scope(c, tag::meterHold, true);
                c.hairlineH(r.x, lm.y(std::min(b.hold, layout::kLevelTopDb), scale), r.w, th.ink100);
            }
        }

        // Readouts (band): max IN peak, max GR, max OUT peak since reset — the maxima hold whatever the feed does
        // (ADR-69: "–" in ink16 only before the first frame; a silent maximum prints "−∞", no GR "0.0").
        if (geom_.nReadouts > 0)
        {
            const funkgui::Canvas::Scope scope(c, tag::meterReadout, true);
            const std::array<float, 3> v { s.inMax, s.grMax, s.outMax };
            for (int i = 0; i < geom_.nReadouts && i < 3; ++i)
            {
                const funkgui::Rect& r = geom_.readouts[static_cast<std::size_t>(i)];
                char t[24];
                std::snprintf(t, sizeof t, "%s", kDash);
                const float x = v[static_cast<std::size_t>(i)];
                if (s.seen && i == 1 && funkgui::fmt::db(x, 1, t, sizeof t) < 0)
                    std::snprintf(t, sizeof t, "%s", kDash);
                else if (s.seen && i != 1)
                    levelText(x, t, sizeof t);
                c.text(t, r.centreX(), c.capCentreTop(r.centreY(), T::kMicro), T::kMicro, s.seen ? th.ink100 : th.ink16,
                       funkgui::Align::centre);
            }
            if (s.over && geom_.nReadouts >= 3)
                outline(c, geom_.readouts[2], th.ink100);         // kUiOutOver latched until reset
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            for (int i = 0; i < geom_.nLabels; ++i)
            {
                const layout::MeterGeom::Label& l = geom_.labels[static_cast<std::size_t>(i)];
                c.text(l.text, l.centreX, geom_.labelY, T::kMicro, th.ink52, funkgui::Align::centre);
            }
        }
        if (geom_.hasCaption)
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("METERS", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        if (ctx_.focusVisible && ctx_.focus == idBase_ + kResetId)
            funkgui::drawFocusRing(c, geom_.reset, th.accent);
        const funkgui::AxisMap y { bottom, top, floorDb, layout::kLevelTopDb, false };
        c.axis(tag::meterAxis, nullptr, &y);
    }

    // ---- input ------------------------------------------------------------------------------------------------------------

    bool MeterColumn::hit(funkgui::Point p) const { return geom_.area.contains(p); }

    void MeterColumn::pointerMove(const funkgui::PointerEvent& e)
    {
        st_->pointerOver = true;
        st_->resetHover = geom_.reset.contains({ e.x, e.y });
    }

    void MeterColumn::pointerExit() { st_->pointerOver = st_->resetHover = false; }

    void MeterColumn::pointerDown(const funkgui::PointerEvent& e)
    {
        if (!e.popup && geom_.reset.contains({ e.x, e.y }))
            st_->reset();                                         // one click resets all three (02 §8.8)
    }

    funkgui::Cursor MeterColumn::cursor(funkgui::Point p) const
    {
        return geom_.reset.contains(p) ? funkgui::Cursor::pointingHand : funkgui::Cursor::normal;
    }

    bool MeterColumn::key(const funkgui::KeyEvent& e)
    {
        if (ctx_.focus != idBase_ + kResetId)
            return false;
        if (e.key == funkgui::Key::enter || e.key == funkgui::Key::space)
        {
            st_->reset();
            return true;
        }
        return false;
    }

    bool MeterColumn::wantsFullRate() const
    {
        // Falling bars and holds (no fresh frame) move every frame; while live the Panel runs at full rate anyway.
        const State& s = *st_;
        const float floorDb = layout::kLevelTopDb - static_cast<float>(ctx_.meterScaleDb);
        for (int i = 0; i < geom_.nBars && i < kMaxBars; ++i)
        {
            const State::Bar& b = s.bars[static_cast<std::size_t>(i)];
            if (isGr(geom_.bars[static_cast<std::size_t>(i)].what) ? (b.peak > 0.0f || b.hold > 0.0f)
                                                                    : (b.peak > floorDb || b.hold > floorDb))
                return true;
        }
        return false;
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------------

    void MeterColumn::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const State& s = *st_;
        const float floorDb = layout::kLevelTopDb - static_cast<float>(ctx_.meterScaleDb);
        for (int i = 0; i < geom_.nBars && i < kMaxBars; ++i)
        {
            const layout::MeterGeom::Bar& g = geom_.bars[static_cast<std::size_t>(i)];
            const State::Bar& b = s.bars[static_cast<std::size_t>(i)];
            funkgui::A11yItem it;
            it.id = idBase_ + kBarId + static_cast<uint32_t>(i);
            it.role = funkgui::A11yRole::progressBar;
            it.bounds = g.r;
            it.title = barTitle(g.what);
            it.readOnly = true;
            const float db = isGr(g.what) ? (b.peak > 0.0f ? -b.peak : 0.0f) : b.peak;   // GR spoken as "−4.2 dB"
            char t[24];
            // ADR-69: after the first frame a bar always has a value (GR "0.0 dB", a level at the floor "−∞ dB"); "–"
            // before it, and for a bar without a signal (SC without an external key).
            bool shown = s.seen && (isGr(g.what) || b.present);
            it.value = kDash;
            if (shown)
            {
                if (isGr(g.what))
                    shown = funkgui::fmt::db(db, 1, t, sizeof t) >= 0;
                else
                    levelText(b.peak > floorDb ? b.peak : kSilentDb, t, sizeof t);
                if (shown)
                    it.value = std::string(t) + " dB";
            }
            it.lo = isGr(g.what) ? -static_cast<double>(ctx_.meterScaleDb) : static_cast<double>(floorDb);
            it.hi = isGr(g.what) ? 0.0 : static_cast<double>(layout::kLevelTopDb);
            it.v = std::clamp(static_cast<double>(db), it.lo, it.hi);
            out.push_back(std::move(it));
        }
        funkgui::A11yItem r;
        r.id = idBase_ + kResetId;
        r.role = funkgui::A11yRole::button;
        r.bounds = geom_.reset;
        r.title = "Reset meters";
        r.help = "Resets the peak holds and the maxima since the last reset";
        out.push_back(std::move(r));
    }

    int MeterColumn::focusOrder(std::span<uint32_t> out) const
    {
        if (out.empty())
            return 0;
        out[0] = idBase_ + kResetId;                               // 02 §8.9 item 5: meter reset
        return 1;
    }

    void MeterColumn::a11yAction(uint32_t id, funkgui::A11yAction a, double)
    {
        if (id == idBase_ + kResetId && (a == funkgui::A11yAction::press || a == funkgui::A11yAction::toggle))
            st_->reset();
    }
}
