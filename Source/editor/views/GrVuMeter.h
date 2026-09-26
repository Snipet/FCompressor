// Source/editor/views/GrVuMeter.h — the GR VU meter (ADR-72, card UF2, S12): one analog-style needle meter for gain
// reduction, drawn in the band's HISTORY rectangle when the HISTORY · VU switch shows VU. A new view (not an FZ4
// declaration); HistoryPlot owns it on the band only and draws it instead of the traces (HistoryPlot.h). Geometry and
// constants: layout::vu (Layout.h).
//
// What it draws, in the panel's own style (theme inks, so it follows GRAPHITE and PAPER; nothing is ever dimmed):
// - the scale: a thin arc rule (ink32), ticks pointing outwards (labelled ones ink52, the +1…+3 region and the minor
//   ticks ink32; no red zone), and the labels 0, −1, −2, −3, −5, −7, −10, −20 in kMicro ink52 outside them;
// - the legend "GR · VU" (kMicro ink32) under the arc, the needle (1.5 px, signal: the one ink for live GR, 02 §8.10)
//   and its pivot (a small ink70 disc).
// Tags: GRID (arc, ticks), AXIS_LABEL (labels), CAPTION (legend), GR_NEEDLE (the needle, live; the pivot, static).
//
// The law (the classic GR-on-a-VU): the needle's deflection is proportional to the linear gain, d = 10^(−GR / 20)
// scaled so that 0 dB sits on the VU "0" mark at 10^(−3/20) of full scale (+3 dB is full scale); the angle is linear in
// d. At rest (no GR) the needle stands on 0 and it swings left as GR grows.
//
// The ballistics (ADR-72): a second-order (mass-spring) needle driven by the tapped GR as linear gain — each 1 ms
// HistoryColumn's grMaxDb, a zero-order hold, integrated exactly — over HISTORY's timeline (telemetry::HistoryTimeline):
// at audio time while the feed is fresh; over a stale span (the host stopped calling processBlock) the timeline keeps
// time at wall-clock rate and holds no entry, so the needle falls back to rest with the same ballistics (ADR-69), and
// nothing dims. A gap entry (a lap marker, the first column after an attach) counts as rest, as HISTORY draws it empty.
// A step reaches 99 % of its travel in 300 ms and overshoots by 1.25 % (IEC 60268-17). Once it has settled it snaps
// exactly onto its input.
//
// Presentation: the state at every timeline ms is kept (the last kTrack ms) and the needle is drawn at a display clock
// that advances with the panel's dt, layout::vu::kShowLagMs behind the head while the audio runs (a slow pull keeps the
// lag; over a stale span it runs at real time up to the head; beyond kMaxLagMs it skips forward), linearly between
// two ms. So audio that arrives in host blocks moves the needle smoothly at the frame rate. Everything depends only on
// the store, the timeline and the dt sequence: deterministic under a fixed dt (UI_FIXED_DT, HeadlessHost). The Panel
// runs at full rate while the drawn needle has not reached the settled one (wantsFullRate).
//
// Faces (ADR-76, v1.1): the meter wears the face of the Mode it shows (views/MeterFaces.h): the panel face above for
// Clean (and any Mode without its own), a hardware-class plate for the others (fixed colours, a scale of their own, the
// needle's base under a shroud), or an LED ladder for Brickwall. The state integrated is the needle's position on the
// face's scale (0 left, 1 right; the panel face's is affine in the deflection d, so everything above holds as it was);
// the LED ladder's is the GR in dB, with an instant attack and a kLedFallDbPerS fall. A Mode change puts the new face
// at rest.
//
// HistoryPlot ticks it every frame the band is ticked, whichever view is shown, so switching to VU shows the needle
// where it is. A meter ticked after a pause (the other screen was shown) integrates at most the last
// layout::vu::kCatchUpMs of the timeline.
#pragma once

#include "editor/Layout.h"
#include "editor/SubView.h"
#include "editor/views/MeterFaces.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/telemetry/HistoryRing.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class GrVuMeter final : public SubView
    {
    public:
        GrVuMeter(PanelContext&, uint32_t a11yId);

        GrVuMeter(const GrVuMeter&) = delete;
        GrVuMeter& operator=(const GrVuMeter&) = delete;

        void tick(float dt) override;                           // the timeline, the needle up to its head, the display
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;                // the plot rectangle
        void accessibility(std::vector<funkgui::A11yItem>&) const override;   // one progressBar: the drawn reading
        int  focusOrder(std::span<uint32_t> out) const override;              // none: a meter is not a Tab stop
        bool wantsFullRate() const override;                    // while the drawn needle moves

        // ---- the law and the needle, read-only (probes) ------------------------------------------------------------
        static double deflection(double readingDb) noexcept;    // 10^((dB − 3) / 20): 0 dB -> 0.708, +3 dB -> 1
        static double readingDb(double deflection) noexcept;    // its inverse (−inf at 0)
        static double angleDeg(double deflection) noexcept;     // from vertical, + to the right; linear in d
        static funkgui::Point onScale(double angleDeg, float radius) noexcept;   // a point of the face at that angle
        static double angleOfPos(double x) noexcept;            // ADR-76: −E + 2E·x, x the face's scale position

        double needle() const noexcept { return pos_; }         // the needle's scale position at the head (ADR-76)
        double shown() const noexcept { return shown_; }        // ... and as drawn (at the display clock)
        bool   settled() const noexcept { return settled_; }
        const MeterFace& face() const noexcept { return *face_; }

    private:
        static constexpr int kTrack = 512;                      // needle states kept, one per ms (> kMaxLagMs + 2)
        static constexpr int kArcPoints = layout::vu::kArcSegments + 1;

        void   advance(const fcdsp::HistoryColumn*, int64_t steps) noexcept;   // nullptr: rest; moves cursor_
        void   restart(uint64_t at) noexcept;                   // the kept states begin at `at` (the needle as it is)
        double stateAt(double ms) const noexcept;               // the kept state at ms, linear between two
        double restPos() const noexcept;                        // the face's position at 0 dB GR
        double inputOf(const fcdsp::HistoryColumn*) const noexcept;   // a column's GR as the face's position
        void   setFace(const MeterFace&) noexcept;              // a Mode change: the new face, at rest
        void   drawPanel(funkgui::Canvas&, const funkgui::Theme&) const;
        void   drawPlate(funkgui::Canvas&, const funkgui::Theme&) const;
        void   drawLed(funkgui::Canvas&, const funkgui::Theme&) const;

        PanelContext&              ctx_;
        const uint32_t             id_;
        const MeterFace*           face_ = &meterface::kPanel;
        telemetry::HistoryTimeline timeline_;
        uint64_t cursor_ = 0;                                   // the timeline ms the needle has reached
        uint64_t from_ = 0;                                     // the oldest kept state
        bool     started_ = false;
        double   pos_ = 0.0, vel_ = 0.0;                        // deflection and its rate (1/s)
        double   target_ = 0.0;                                 // the last step's input
        bool     settled_ = true;
        double   a00_ = 0.0, a01_ = 0.0, a10_ = 0.0, a11_ = 0.0;   // e^(A · 1 ms)
        std::array<double, kTrack> track_{};                    // the state at ms t in track_[t % kTrack]
        double   show_ = 0.0;                                   // the display clock (timeline ms, fractional)
        double   shown_ = 0.0;                                  // the deflection drawn
        telemetry::Feed feed_ = telemetry::Feed::none;          // the feed at the last tick

        std::array<float, kArcPoints> arcX_{}, arcY_{};

        char  value_[32] = "0.0 dB";                            // the a11y value (<= 10 Hz)
        float valueAge_ = 0.0f;
    };
}
