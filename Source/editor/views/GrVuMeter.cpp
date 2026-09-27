// Source/editor/views/GrVuMeter.cpp — the GR VU meter (see GrVuMeter.h; ADR-72): the needle integrated in tick() over
// HISTORY's timeline (draw() only emits), the scale, the legend, the pivot and the a11y reading.
#include "editor/views/GrVuMeter.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/ProductTheme.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <numbers>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        namespace V = layout::vu;

        constexpr double kStepS   = 0.001;                         // one HistoryColumn: 1 ms of audio
        constexpr double kSnapPos = 1.0e-5;                        // settled: within 1e-5 of full scale (0.001°) ...
        constexpr double kSnapVel = 1.0e-4;                        // ... and slower than 1e-4 of full scale per second
        constexpr double kFloorReadingDb = -99.0;                  // the a11y reading's floor (d = 0 is −inf)
        constexpr double kPinLo = -0.03, kPinHi = 1.03;            // a hardware needle's stops (scale positions)
        constexpr double kLedFallPerStep = meterface::kLedFallDbPerS * kStepS;

        // ---- v1.2 plate detail (ADR-81): METER_DETAIL, over the ADR-76 faces ------------------------------------------
        constexpr funkgui::Col kWhite { 0xFF, 0xFF, 0xFF }, kBlack { 0x00, 0x00, 0x00 };

        bool lightFace(funkgui::Col c) noexcept
        {
            return 0.299f * static_cast<float>(c.r) + 0.587f * static_cast<float>(c.g) + 0.114f * static_cast<float>(c.b)
                 > 128.0f;
        }

        // A slotted screw head: a metal disc a shade off the plate, a dark rim, the slot at `slotDeg`, a glint.
        void screw(funkgui::Canvas& c, float x, float y, float r, funkgui::Col plate, float slotDeg)
        {
            const funkgui::Col head = funkgui::mix(plate, lightFace(plate) ? kBlack : kWhite, 0.22f);
            c.disc(x, y, r, head, 0.6f, funkgui::mix(plate, kBlack, 0.5f));
            const double a = static_cast<double>(slotDeg) * std::numbers::pi / 180.0;
            const auto dx = static_cast<float>(std::cos(a)) * r * 0.75f, dy = static_cast<float>(std::sin(a)) * r * 0.75f;
            c.segment(x - dx, y - dy, x + dx, y + dy, 0.7f, funkgui::mix(plate, kBlack, 0.6f));
            c.disc(x - 0.35f * r, y - 0.35f * r, 0.3f * r, kWhite.withAlpha(0.35f));
        }

        // Four screws in the bezel's corners, each slot at its own angle (as fitted by hand).
        void bezelScrews(funkgui::Canvas& c, const funkgui::Rect& bz, funkgui::Col plate)
        {
            const float in = V::kScrewInset, r = V::kScrewR;
            screw(c, bz.x + in, bz.y + in, r, plate, 25.0f);
            screw(c, bz.right() - in, bz.y + in, r, plate, -40.0f);
            screw(c, bz.x + in, bz.bottom() - in, r, plate, 70.0f);
            screw(c, bz.right() - in, bz.bottom() - in, r, plate, 5.0f);
        }

        // The window's recess (a stepped shadow under its top edge) and its glass (a light wedge from the top-left).
        void glass(funkgui::Canvas& c, const funkgui::Rect& win, funkgui::Col face)
        {
            const funkgui::Col shade = kBlack.withAlpha(V::kRecessAlpha);
            for (const float h : V::kRecessH)
                c.rrect4(win.x, win.y, win.w, h, V::kHwWindowR, V::kHwWindowR, 0.0f, 0.0f, shade);
            const float total = lightFace(face) ? V::kSheenAlphaLight : V::kSheenAlphaDark;
            const funkgui::Col sheen = kWhite.withAlpha(total / static_cast<float>(V::kSheenLayers));
            const float x0 = win.x + V::kHwWindowR, y0 = win.y + V::kRecessH[0];
            for (int i = 0; i < V::kSheenLayers; ++i)
            {
                const float k = static_cast<float>(i) * V::kSheenStep;
                c.area(x0, win.x + (V::kSheenW - k) * win.w, y0, y0, win.y + (V::kSheenH - k) * win.h, y0, sheen);
            }
        }

        // The Mode lamp as a jewel: a halo of its light, the lamp, a metal rim and a specular point.
        void jewel(funkgui::Canvas& c, funkgui::Point p, funkgui::Col lamp, funkgui::Col plate)
        {
            const float r = V::kHwLampR, h = r + V::kLampHalo;
            c.rrect(p.x - h, p.y - h, 2.0f * h, 2.0f * h, h, lamp.withAlpha(V::kLampHaloAlpha), 0.0f, {}, V::kLampHalo);
            c.disc(p.x, p.y, r + V::kLampRimW, funkgui::Col{ 0, 0, 0, 0 }, V::kLampRimW,
                   funkgui::mix(plate, lightFace(plate) ? kBlack : kWhite, 0.35f));
            c.disc(p.x - 0.35f * r, p.y - 0.35f * r, 0.3f * r, kWhite.withAlpha(0.8f));
        }
    }

    // ---- the law ----------------------------------------------------------------------------------------------------------

    double GrVuMeter::deflection(double readingDb) noexcept
    {
        return std::pow(10.0, (readingDb - static_cast<double>(V::kHighDb)) / 20.0);
    }

    double GrVuMeter::readingDb(double d) noexcept
    {
        return d > 0.0 ? 20.0 * std::log10(d) + static_cast<double>(V::kHighDb)
                       : -std::numeric_limits<double>::infinity();
    }

    double GrVuMeter::angleDeg(double d) noexcept
    {
        const double lo = deflection(static_cast<double>(V::kLowDb));
        const double hi = deflection(static_cast<double>(V::kHighDb));
        const double end = static_cast<double>(V::kEndDeg);
        return -end + 2.0 * end * (d - lo) / (hi - lo);
    }

    double GrVuMeter::angleOfPos(double x) noexcept
    {
        const double end = static_cast<double>(V::kEndDeg);
        return -end + 2.0 * end * x;
    }

    double GrVuMeter::restPos() const noexcept { return facePos(*face_, 0.0); }

    // The input of one step: the column's GR as the face's scale position; rest for no column (a gap or stale time).
    // A hardware needle stops at its pins; the LED ladder shows 0 … kLedMaxDb.
    double GrVuMeter::inputOf(const fcdsp::HistoryColumn* c) const noexcept
    {
        const float gr = c != nullptr ? c->grMaxDb : 0.0f;
        const double x = facePos(*face_, gr > 0.0f && std::isfinite(gr) ? -static_cast<double>(gr) : 0.0);
        if (face_->plate == FacePlate::panel)
            return x;
        if (face_->plate == FacePlate::led)
            return std::clamp(x, 0.0, static_cast<double>(meterface::kLedMaxDb));
        return std::clamp(x, kPinLo, kPinHi);
    }

    void GrVuMeter::setFace(const MeterFace& f) noexcept
    {
        face_ = &f;
        pos_ = target_ = shown_ = restPos();
        vel_ = 0.0;
        settled_ = true;
        track_.fill(pos_);
    }

    funkgui::Point GrVuMeter::onScale(double angle, float radius) noexcept
    {
        const double a = angle * std::numbers::pi / 180.0;
        const auto r = static_cast<double>(radius);
        return { static_cast<float>(static_cast<double>(V::kPivot.x) + r * std::sin(a)),
                 static_cast<float>(static_cast<double>(V::kPivot.y) - r * std::cos(a)) };
    }

    // ---- construction -----------------------------------------------------------------------------------------------------

    GrVuMeter::GrVuMeter(PanelContext& ctx, uint32_t a11yId) : ctx_(ctx), id_(a11yId)
    {
        // e^(A h) of x'' + 2ζω x' + ω² x = ω² u (underdamped), h = 1 ms: exact for an input held over the step.
        const double z = V::kZeta, w = V::kOmega;
        const double sigma = z * w;
        const double wd = w * std::sqrt(1.0 - z * z);
        const double e = std::exp(-sigma * kStepS);
        const double c = std::cos(wd * kStepS), s = std::sin(wd * kStepS);
        a00_ = e * (c + sigma / wd * s);
        a01_ = e * s / wd;
        a10_ = -e * w * w / wd * s;
        a11_ = e * (c - sigma / wd * s);

        pos_ = target_ = shown_ = restPos();
        track_.fill(pos_);
        for (int i = 0; i < kArcPoints; ++i)
        {
            const double a = -static_cast<double>(V::kEndDeg)
                           + 2.0 * static_cast<double>(V::kEndDeg) * static_cast<double>(i) / V::kArcSegments;
            const funkgui::Point p = onScale(a, V::kScaleR);
            arcX_[static_cast<std::size_t>(i)] = p.x;
            arcY_[static_cast<std::size_t>(i)] = p.y;
        }
    }

    // ---- the needle -------------------------------------------------------------------------------------------------------

    void GrVuMeter::restart(uint64_t at) noexcept
    {
        cursor_ = from_ = at;
        track_[static_cast<std::size_t>(at % kTrack)] = pos_;
    }

    // `steps` ms of one input from cursor_ on, each state kept. A settled needle whose input does not change holds.
    void GrVuMeter::advance(const fcdsp::HistoryColumn* c, int64_t steps) noexcept
    {
        if (steps <= 0)
            return;
        const double u = inputOf(c);
        if (settled_ && u == target_)
        {
            for (int64_t k = std::max<int64_t>(1, steps - kTrack + 1); k <= steps; ++k)
                track_[static_cast<std::size_t>((cursor_ + static_cast<uint64_t>(k)) % kTrack)] = pos_;
            cursor_ += static_cast<uint64_t>(steps);
            return;
        }
        target_ = u;
        settled_ = false;
        if (face_->law == FaceLaw::led)
        {
            // ADR-76: the LED ladder takes a rise at once and falls at kLedFallDbPerS.
            for (int64_t k = 0; k < steps; ++k)
            {
                pos_ = u >= pos_ ? u : std::max(u, pos_ - kLedFallPerStep);
                track_[static_cast<std::size_t>(++cursor_ % kTrack)] = pos_;
            }
            settled_ = pos_ == u;
            return;
        }
        for (int64_t k = 0; k < steps; ++k)
        {
            const double e = pos_ - u;
            const double p = u + a00_ * e + a01_ * vel_;
            vel_ = a10_ * e + a11_ * vel_;
            pos_ = p;
            track_[static_cast<std::size_t>(++cursor_ % kTrack)] = pos_;
        }
        if (std::fabs(pos_ - u) < kSnapPos && std::fabs(vel_) < kSnapVel)
        {
            pos_ = u;                                            // exactly on its input: settled
            vel_ = 0.0;
            settled_ = true;
            track_[static_cast<std::size_t>(cursor_ % kTrack)] = pos_;
        }
    }

    double GrVuMeter::stateAt(double ms) const noexcept
    {
        const double t = std::clamp(ms, static_cast<double>(from_), static_cast<double>(cursor_));
        const auto t0 = static_cast<uint64_t>(std::floor(t));
        const double a = track_[static_cast<std::size_t>(t0 % kTrack)];
        if (t0 >= cursor_)
            return a;
        const double b = track_[static_cast<std::size_t>((t0 + 1) % kTrack)];
        const double f = t - static_cast<double>(t0);
        return a == b || !(f > 0.0) ? a : a + (b - a) * f;
    }

    void GrVuMeter::tick(float dt)
    {
        // ADR-76: the face of the Mode shown; a Mode change puts the new face at rest.
        if (const MeterFace& f = faceFor(ctx_.frame.entry != nullptr ? ctx_.frame.entry->desc->key : std::string_view{});
            &f != face_)
            setFace(f);
        timeline_.tick(ctx_);
        const uint64_t head = timeline_.head();
        bool resync = false;
        if (!started_ || head < cursor_)
        {
            restart(head);                                       // the first tick, or the store was cleared
            resync = true;
        }
        started_ = true;
        if (head - cursor_ > static_cast<uint64_t>(V::kCatchUpMs))
            restart(head - static_cast<uint64_t>(V::kCatchUpMs));   // after a pause: the last second is enough

        // Every timeline ms in [cursor, head): the entry placed there (audio time), or rest where none is (a stale span).
        const HistoryStore& h = ctx_.history;
        const auto oldest = static_cast<int64_t>(h.oldest());
        auto t = static_cast<int64_t>(cursor_);
        const auto end = static_cast<int64_t>(head);
        timeline_.forEntries(t, end, [&](int64_t e0, int64_t e1, int64_t offset) {
            advance(nullptr, e0 + offset - t);
            for (int64_t e = e0; e < e1; ++e)
            {
                const fcdsp::HistoryColumn* c = nullptr;
                if (e >= oldest)
                    if (const fcdsp::HistoryColumn& col = h.at(static_cast<uint64_t>(e)); !HistoryStore::isGap(col))
                        c = &col;
                advance(c, 1);
            }
            t = e1 + offset;
        });
        advance(nullptr, end - t);
        from_ = std::max(from_, cursor_ >= kTrack ? cursor_ - kTrack + 1 : 0);

        // The display clock (Layout.h layout::vu::kShowLagMs): real time, kShowLagMs behind the head while fresh. It
        // takes the lag at once when it starts, when the first audio arrives and after a skip; when the audio returns
        // after a stale span (drawn at no lag) the pull restores it, so the needle never jumps back.
        const double hd = static_cast<double>(head);
        const double step = static_cast<double>(std::max(dt, 0.0f));
        const telemetry::Feed fd = telemetry::feed(ctx_);
        if (resync || (fd == telemetry::Feed::fresh && feed_ == telemetry::Feed::none) || hd - show_ > V::kMaxLagMs)
        {
            show_ = hd - V::kShowLagMs;
        }
        else
        {
            show_ += step * 1000.0;
            if (fd == telemetry::Feed::fresh)
                show_ += (hd - V::kShowLagMs - show_) * std::min(1.0, step / V::kPullS);
        }
        show_ = std::min(show_, hd);
        shown_ = stateAt(show_);
        feed_ = fd;

        // The a11y reading (02 §9.6: values <= 10 Hz): what is drawn.
        valueAge_ += std::max(dt, 0.0f);
        if (valueAge_ >= V::kValueS)
        {
            valueAge_ = 0.0f;
            const double db = std::max(faceReading(*face_, shown_), kFloorReadingDb);
            char t1[24];
            if (funkgui::fmt::db(static_cast<float>(db), 1, t1, sizeof t1) < 0)
                t1[0] = '\0';
            std::snprintf(value_, sizeof value_, "%s dB", t1);
        }
    }

    bool GrVuMeter::wantsFullRate() const { return !settled_ || shown_ != pos_; }

    // ---- draw ---------------------------------------------------------------------------------------------------------------

    void GrVuMeter::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        if (face_->plate == FacePlate::panel)
            drawPanel(c, th);
        else if (face_->plate == FacePlate::led)
            drawLed(c, th);
        else
            drawPlate(c, th);
    }

    // The panel face (ADR-72): theme inks, the needle in the signal ink (the Mode colour, ADR-75).
    void GrVuMeter::drawPanel(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const auto ink = [&th](funkgui::Col col) { return funkgui::premix(th.ground, col, 1.0f); };
        const auto radial = [&](double db, float len, funkgui::Col col) {
            const double a = angleDeg(deflection(db));
            const funkgui::Point p0 = onScale(a, V::kScaleR), p1 = onScale(a, V::kScaleR + len);
            c.segment(p0.x, p0.y, p1.x, p1.y, V::kTickW, ink(col));
        };
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            c.polyline(arcX_.data(), arcY_.data(), kArcPoints, V::kRuleW, ink(th.ink32));
            for (const layout::AxisLabel& l : V::kLabels)
                radial(static_cast<double>(l.value), V::kTickLong, th.ink52);
            for (const float db : V::kOverDb)
                radial(static_cast<double>(db), V::kTickLong, th.ink32);
            for (const float db : V::kMinorDb)
                radial(static_cast<double>(db), V::kTickShort, th.ink32);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            for (const layout::AxisLabel& l : V::kLabels)
            {
                const funkgui::Point p = onScale(angleDeg(deflection(static_cast<double>(l.value))), V::kLabelR);
                c.text(l.text, p.x, c.capCentreTop(p.y, T::kMicro), T::kMicro, th.ink52, funkgui::Align::centre);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text(V::kLegend, V::kPivot.x, c.capCentreTop(V::kPivot.y - V::kLegendDy, T::kMicro), T::kMicro, th.ink32,
                   funkgui::Align::centre);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grNeedle, true);
            const funkgui::Point tip = onScale(angleOfPos(shown_), V::kNeedleR);
            c.segment(V::kPivot.x, V::kPivot.y, tip.x, tip.y, V::kNeedleW, ink(th.signal));
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grNeedle, false);
            c.disc(V::kPivot.x, V::kPivot.y, V::kPivotR, ink(th.ink70));
        }
    }

    // A hardware-class plate (ADR-76): fixed colours, the scale's ticks and labels inside the arc, a red zone on a VU
    // face, the legend, the needle, and the shroud over its base; the Mode colour's lamp in the corner (ADR-75).
    void GrVuMeter::drawPlate(funkgui::Canvas& c, const funkgui::Theme& /*th*/) const
    {
        const MeterFace& f = *face_;
        const funkgui::Rect& bz = V::kHwBezel;
        const funkgui::Rect win = bz.reduced(V::kHwWindowInset);
        {
            const funkgui::Canvas::Scope scope(c, tag::meterFace, false);
            c.rrect(bz.x, bz.y, bz.w, bz.h, V::kHwBezelR, f.bezel, 1.0f,
                    funkgui::mix(f.bezel, funkgui::Col{ 0xFF, 0xFF, 0xFF }, V::kHwEdgeMix));
            if (f.plate == FacePlate::ring)
            {
                const funkgui::Rect ring = bz.reduced(V::kHwRingInset);
                c.rrect(ring.x, ring.y, ring.w, ring.h, V::kHwBezelR - V::kHwRingInset, f.bezel, V::kHwRingW, f.ring);
            }
            c.rrect(win.x, win.y, win.w, win.h, V::kHwWindowR, f.face);
            if (f.plate == FacePlate::backlit)
            {
                const funkgui::Rect& g = V::kHwGlow;
                c.rrect(g.x, g.y, g.w, g.h, 0.5f * g.h, f.glow.withAlpha(V::kHwGlowAlpha), 0.0f, {}, V::kHwGlowSoft);
            }
            c.disc(V::kHwLamp.x, V::kHwLamp.y, V::kHwLampR, modeColour(f.key, funkgui::Theme::graphite()));   // lit
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, false);                // ADR-81
            glass(c, win, f.face);
            bezelScrews(c, bz, f.bezel);
            jewel(c, V::kHwLamp, modeColour(f.key, funkgui::Theme::graphite()), f.face);
        }
        const auto radial = [&](double readingDb, float from, float to, float w) {
            const double a = angleOfPos(facePos(f, readingDb));
            const funkgui::Point p0 = onScale(a, from), p1 = onScale(a, to);
            c.segment(p0.x, p0.y, p1.x, p1.y, w, f.print);
        };
        {
            const funkgui::Canvas::Scope scope(c, tag::grid, false);
            c.polyline(arcX_.data(), arcY_.data(), kArcPoints, V::kHwArcW, f.print);
            if (f.law == FaceLaw::vuGain && f.zoneFromDb < f.highDb)
            {
                constexpr int n = 24;
                std::array<float, n + 1> zx{}, zy{};
                const double a0 = angleOfPos(facePos(f, static_cast<double>(f.zoneFromDb)));
                const double a1 = angleOfPos(1.0);
                for (int i = 0; i <= n; ++i)
                {
                    const funkgui::Point p = onScale(a0 + (a1 - a0) * i / n, V::kHwZoneR);
                    zx[static_cast<std::size_t>(i)] = p.x;
                    zy[static_cast<std::size_t>(i)] = p.y;
                }
                c.polyline(zx.data(), zy.data(), n + 1, V::kHwZoneW, f.zone);
            }
            for (const FaceMark& m : f.marks)
                radial(static_cast<double>(m.readingDb), V::kScaleR,
                       V::kScaleR - (m.label != nullptr ? V::kHwTickLong : V::kHwTickShort),
                       m.label != nullptr ? V::kHwTickW : 1.0f);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            for (const FaceMark& m : f.marks)
            {
                if (m.label == nullptr)
                    continue;
                const funkgui::Point p =
                    onScale(angleOfPos(facePos(f, static_cast<double>(m.readingDb))), V::kHwLabelR);
                const bool over = f.law == FaceLaw::vuGain && m.readingDb > 0.0f;   // 1 2 3 above 0: the zone's ink
                c.text(m.label, p.x, c.capCentreTop(p.y, T::kCaption), T::kCaption, over ? f.zone : f.print,
                       funkgui::Align::centre);
            }
        }
        {
            // ADR-81: a VU face's lower 0–100 % scale; a GR face's fine ruler at the whole dB its marks leave out.
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, false);
            if (f.law == FaceLaw::vuGain)
            {
                constexpr int n = 48;                               // the scale's own arc, 10 % (the left end) … the end
                std::array<float, n + 1> ax{}, ay{};
                const double a0 = angleOfPos(facePos(f, -20.0)), a1 = angleOfPos(1.0);
                for (int i = 0; i <= n; ++i)
                {
                    const funkgui::Point p = onScale(a0 + (a1 - a0) * i / n, V::kPctTickR);
                    ax[static_cast<std::size_t>(i)] = p.x;
                    ay[static_cast<std::size_t>(i)] = p.y;
                }
                c.polyline(ax.data(), ay.data(), n + 1, V::kPctArcW, f.print);
                for (int pct = 10; pct <= 100; pct += 10)
                {
                    const double db = 20.0 * std::log10(static_cast<double>(pct) / 100.0);
                    const bool major = pct % 20 == 0;
                    radial(db, V::kPctTickR, V::kPctTickR - (major ? V::kPctTickLong : V::kPctTickShort),
                           major ? 1.0f : 0.8f);
                    if (!major)
                        continue;
                    char t[8];
                    std::snprintf(t, sizeof t, "%d", pct);
                    const funkgui::Point p = onScale(angleOfPos(facePos(f, db)), V::kPctLabelR);
                    c.text(t, p.x, c.capCentreTop(p.y, T::kMicro), T::kMicro, f.print, funkgui::Align::centre);
                }
                const funkgui::Point p = onScale(angleOfPos(1.0), V::kPctLabelR);
                c.text("%", p.x, c.capCentreTop(p.y, T::kMicro), T::kMicro, f.print, funkgui::Align::centre);
            }
            else if (f.law == FaceLaw::grLinear)
            {
                for (int gr = 1; gr < 20; ++gr)
                {
                    const auto r = static_cast<float>(-gr);
                    const bool marked = std::any_of(f.marks.begin(), f.marks.end(),
                                                    [r](const FaceMark& m) { return m.readingDb == r; });
                    if (!marked)
                        radial(static_cast<double>(r), V::kScaleR, V::kScaleR - V::kFineTick, 0.7f);
                }
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            const bool big = std::string_view(f.legend).size() <= 3;
            const funkgui::TextStyle& ls = big ? T::kValueS : T::kCaption;
            c.text(f.legend, V::kPivot.x, c.capCentreTop(V::kPivot.y - V::kHwLegendDy, ls), ls, f.print,
                   funkgui::Align::centre);
            if (f.legend2[0] != '\0')
                c.text(f.legend2, V::kPivot.x, c.capCentreTop(V::kPivot.y - V::kHwLegend2Dy, T::kMicro), T::kMicro,
                       f.print, funkgui::Align::centre);
        }
        const double needleDeg = angleOfPos(shown_);
        {
            // ADR-81: the needle's shadow on the face (it rides above the print)
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, true);
            const funkgui::Point from = onScale(needleDeg, V::kNeedleShadowR0), tip = onScale(needleDeg, V::kHwNeedleR);
            const funkgui::Point o = V::kNeedleShadow;
            c.segment(from.x + o.x, from.y + o.y, tip.x + o.x, tip.y + o.y, V::kHwNeedleW + 0.8f,
                      kBlack.withAlpha(V::kNeedleShadowAlpha), V::kNeedleShadowSoft);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grNeedle, true);
            const funkgui::Point tip = onScale(needleDeg, V::kHwNeedleR);
            c.segment(V::kPivot.x, V::kPivot.y, tip.x, tip.y, V::kHwNeedleW, f.needle);
        }
        {
            // ADR-81: the needle's thicker base, above the shroud
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, true);
            const funkgui::Point end = onScale(needleDeg, V::kNeedleBaseR);
            c.segment(V::kPivot.x, V::kPivot.y, end.x, end.y, V::kNeedleBaseW, f.needle);
        }
        const funkgui::Rect& s = V::kHwShroud;
        {
            const funkgui::Canvas::Scope scope(c, tag::meterFace, false);
            c.rrect4(s.x, s.y, s.w, s.h, 0.5f * s.h, 0.5f * s.h, 0.0f, 0.0f, f.bezel);
        }
        {
            // ADR-81: the shroud's top catches the light; its zero-adjust screw
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, false);
            const funkgui::Col edge = funkgui::mix(f.bezel, kWhite, lightFace(f.bezel) ? 0.35f : 0.16f);
            c.segment(s.x + 0.5f * s.h, s.y + 0.8f, s.right() - 0.5f * s.h, s.y + 0.8f, 1.0f, edge);
            screw(c, V::kPivot.x, s.y + V::kZeroScrewDy, V::kZeroScrewR, f.bezel, 80.0f);
        }
    }

    // The LED ladder (ADR-76): 24 segments of 1 dB, lit from the left up to the GR shown (the needle ink, the zone ink
    // from the face's zoneFromDb: Brickwall amber, red from 12 dB; Octo all red), the legend over them and the dB labels
    // under them at the segment edges.
    void GrVuMeter::drawLed(funkgui::Canvas& c, const funkgui::Theme& /*th*/) const
    {
        const MeterFace& f = *face_;
        const funkgui::Rect& bz = V::kLedBezel;
        const funkgui::Rect win = bz.reduced(V::kHwWindowInset);
        {
            const funkgui::Canvas::Scope scope(c, tag::meterFace, false);
            c.rrect(bz.x, bz.y, bz.w, bz.h, V::kHwBezelR, f.bezel, 1.0f,
                    funkgui::mix(f.bezel, funkgui::Col{ 0xFF, 0xFF, 0xFF }, V::kHwEdgeMix));
            c.rrect(win.x, win.y, win.w, win.h, V::kHwWindowR, f.face);
            c.disc(V::kLedLamp.x, V::kLedLamp.y, V::kHwLampR, modeColour(f.key, funkgui::Theme::graphite()));
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, false);                // ADR-81
            bezelScrews(c, bz, f.bezel);
            jewel(c, V::kLedLamp, modeColour(f.key, funkgui::Theme::graphite()), f.face);
        }
        const int lit = std::clamp(static_cast<int>(std::floor(shown_ + 0.5)), 0, meterface::kLedSegments);
        for (int i = 0; i < lit; ++i)                              // ADR-81: the lit segments' bloom, under them all
        {
            const float x = V::kLedLeft + static_cast<float>(i) * V::kLedPitch;
            const funkgui::Col on = static_cast<float>(i) >= f.zoneFromDb ? f.zone : f.needle;
            const float h = V::kLedHalo;
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, true);
            c.rrect(x - h, V::kLedSegY - h, V::kLedSegW + 2.0f * h, V::kLedSegH + 2.0f * h, V::kLedSegR + h,
                    on.withAlpha(V::kLedHaloAlpha), 0.0f, {}, V::kLedHaloSoft);
        }
        for (int i = 0; i < meterface::kLedSegments; ++i)
        {
            const float x = V::kLedLeft + static_cast<float>(i) * V::kLedPitch;
            const bool red = static_cast<float>(i) >= f.zoneFromDb;
            if (i < lit)
            {
                const funkgui::Canvas::Scope scope(c, tag::grNeedle, true);
                c.rrect(x, V::kLedSegY, V::kLedSegW, V::kLedSegH, V::kLedSegR, red ? f.zone : f.needle);
            }
            else
            {
                const funkgui::Canvas::Scope scope(c, tag::meterFace, false);
                c.rrect(x, V::kLedSegY, V::kLedSegW, V::kLedSegH, V::kLedSegR,
                        red ? funkgui::mix(f.dim, f.zone, 0.18f) : f.dim);
            }
            // ADR-81: the lens's highlight across the segment's top, brighter when lit
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, i < lit);
            c.rrect(x + 1.5f, V::kLedSegY + 1.5f, V::kLedSegW - 3.0f, V::kLedLensFrac * V::kLedSegH, 1.5f,
                    kWhite.withAlpha(i < lit ? V::kLedLensAlphaLit : V::kLedLensAlphaDim));
        }
        {
            // ADR-81: the smoked glass over the ladder, and a tick over each label
            const funkgui::Canvas::Scope scope(c, tag::meterDetail, false);
            glass(c, win, f.face);
            for (const FaceMark& m : f.marks)
            {
                const float x = V::kLedLeft + static_cast<float>(-m.readingDb) * V::kLedPitch
                              - 0.5f * (V::kLedPitch - V::kLedSegW);
                c.segment(x, V::kLedTickY, x, V::kLedTickY + V::kLedTickH, 0.8f, f.print.withAlpha(0.6f));
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::axisLabel, false);
            for (const FaceMark& m : f.marks)
            {
                const float x = V::kLedLeft + static_cast<float>(-m.readingDb) * V::kLedPitch
                              - 0.5f * (V::kLedPitch - V::kLedSegW);
                c.text(m.label, x, c.capCentreTop(V::kLedLabelY, T::kMicro), T::kMicro, f.print,
                       funkgui::Align::centre);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text(f.legend, V::kLedLamp.x + 10.0f, c.capCentreTop(V::kLedLegendY, T::kMicro), T::kMicro, f.print);
            c.text(f.legend2, win.right() - 12.0f, c.capCentreTop(V::kLedLegendY, T::kMicro), T::kMicro, f.print,
                   funkgui::Align::right);
        }
    }

    // ---- hit testing and accessibility --------------------------------------------------------------------------------------

    bool GrVuMeter::hit(funkgui::Point p) const { return layout::kBandHistory.plot.contains(p); }

    void GrVuMeter::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = id_;
        it.role = funkgui::A11yRole::progressBar;
        it.bounds = layout::kBandHistory.plot;
        it.title = "Gain reduction VU meter";
        it.help = "A needle meter of gain reduction on a VU scale, 0 to minus 20 dB";
        it.value = value_;
        it.readOnly = true;
        it.lo = static_cast<double>(face_->plate == FacePlate::panel ? V::kLowDb : face_->lowDb);
        it.hi = static_cast<double>(face_->plate == FacePlate::panel ? V::kHighDb : face_->highDb);
        it.v = std::clamp(faceReading(*face_, shown_), it.lo, it.hi);
        out.push_back(std::move(it));
    }

    int GrVuMeter::focusOrder(std::span<uint32_t>) const { return 0; }
}
