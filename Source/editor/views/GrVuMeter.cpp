// Source/editor/views/GrVuMeter.cpp — the GR VU meter (see GrVuMeter.h; ADR-72): the needle integrated in tick() over
// HISTORY's timeline (draw() only emits), the scale, the legend, the pivot and the a11y reading.
#include "editor/views/GrVuMeter.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Tags.h"

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

        double rest() noexcept { return GrVuMeter::deflection(0.0); }

        // The input of one step: the column's GR as a deflection; rest for no column (a gap or stale time).
        double inputOf(const fcdsp::HistoryColumn* c) noexcept
        {
            if (c == nullptr)
                return rest();
            const float gr = c->grMaxDb;
            return GrVuMeter::deflection(gr > 0.0f && std::isfinite(gr) ? -static_cast<double>(gr) : 0.0);
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

        pos_ = target_ = shown_ = rest();
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
            const double db = std::max(readingDb(shown_), kFloorReadingDb);
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
            const funkgui::Point tip = onScale(angleDeg(shown_), V::kNeedleR);
            c.segment(V::kPivot.x, V::kPivot.y, tip.x, tip.y, V::kNeedleW, ink(th.signal));
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::grNeedle, false);
            c.disc(V::kPivot.x, V::kPivot.y, V::kPivotR, ink(th.ink70));
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
        it.lo = static_cast<double>(V::kLowDb);
        it.hi = static_cast<double>(V::kHighDb);
        it.v = std::clamp(readingDb(shown_), it.lo, it.hi);
        out.push_back(std::move(it));
    }

    int GrVuMeter::focusOrder(std::span<uint32_t>) const { return 0; }
}
