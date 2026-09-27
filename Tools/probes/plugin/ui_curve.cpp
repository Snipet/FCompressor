// FCMP_PROBE layer=ui name=curve scope=mode timeout=300
//
// ui.curve.<key> (03 §3.6, C's G2; U2): the band's TRANSFER curve is the analytic curve, mapped back through the
// LEVEL axis record. Headless over a FakeFacade (the curve needs no audio), dpi 1, per configuration <cfg>:
//   def                 the Mode's defaults
//   r<i>                RATIO: every detent (stepped), or lo / mid / hi (continuous)
//   k<i>                KNEE: every detent, or lo / hi
//   t<i>                THRESHOLD: first and last detent, or lo / hi
//   g6                  RANGE 6 dB (continuous RANGE only): the range break inside the plot
//   s12 s24 s72         the scale cells clicked (the preference; 16 / 8 / 2.67 px per dB)
//   live                a scripted live UiFrame whose smoothed fields differ from resolve() (thr −4 dB, makeup
//                       +3 dB, mix 50 %): the curves follow resolve() + overlaySmoothed (K1 #7, K2 #24)
// Rows (spec):
//   curve.<cfg>.vertex_px     every TRANSFER_CURVE vertex within 0.5 px of axis(x + staticGain(x) − preGainDb)
//   curve.<cfg>.chord_px      the curve between vertices (8 points per chord) within 0.25 px of its chord
//   curve.<cfg>.span_px       the curve reaches both plot edges where it lies inside the plot (0.5 px)
//   curve.<cfg>.threshold_px  THRESHOLD_MARK centred on y(T_in) and ending at the TRANSFER x(T_in) (0.5 px); absent
//                             when T_in lies outside the axis
//   curve.<cfg>.ratio_label   the TRANSFER image's title names the ratio as the RATIO slot's active detent speaks it
//                             (stepped), or as the slot's value speaks it (continuous)
//   curve.<cfg>.net           NET_CURVE drawn iff x + netGainDb(gain, makeup, mix) differs from the curve by > 0.05 dB
// Interaction rows at the defaults (each write inside one gesture, 02 §5.2; locked/derived write nothing, §8.4.5):
//   drag.handles_hidden / drag.handles_shown   handles only while the pointer is over the plot (02 §6.5)
//   drag.thr.*          the threshold handle, dragged along unity: thr += ΔT_in (absolute, K1 #9), or one detent on
//   drag.ratio.*        the ratio handle, dragged down: the next detent / a larger ratio (relative)
//   drag.knee.*         the knee's right ring: W += 2 Δx (absolute with kFlagPlotIsPlain), else a relative raise
//   drag.line.*         the HISTORY threshold line, dragged up: thr += Δ (absolute)
// Mode-switch landing (02 §8.7), switching to the first other registered Mode:
//   landing.easing, landing.old_curve_ghosted   the old curve is kept as a ghost and the plot runs at full rate
//   landing.new_curve_px                        after 160 ms the curve is the new Mode's (0.5 px)
//   landing.settles, landing.ghost_released     the panel settles; after 0.9 s only the new Mode's own ghosts remain
//
// The scale configurations write UiPreferences: the probe refuses to run without FCMP_PREFS_DIR (CTest's sandbox).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Text.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>
#include <funkgui/widgets/ValueModel.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    using fcdsp::Pid;
    namespace ui = fcmp::ui;
    namespace layout = fcmp::ui::layout;
    namespace probe = fcmp::probe;

    constexpr float kDt = 1.0f / 60.0f;
    constexpr int kMaxSettle = 600;
    constexpr ui::PanelOptions kOpts { true, true, false };

    struct Box { float x = 0, y = 0, w = 0, h = 0; };
    struct Seg { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

    Box boxOf(const funkgui::Prim& p)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx - p.d0[2], cy - p.d0[3], 2.0f * p.d0[2], 2.0f * p.d0[3] };
    }

    Seg segOf(const funkgui::Prim& p)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx + p.d1[0], cy + p.d1[1], cx + p.d1[2], cy + p.d1[3] };
    }

    std::vector<const funkgui::Prim*> tagged(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        std::vector<const funkgui::Prim*> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == t)
                v.push_back(&p);
        return v;
    }

    const funkgui::AxisRec* axisOf(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        for (const funkgui::AxisRec& a : pl.axes)
            if (a.tag == t)
                return &a;
        return nullptr;
    }

    float toPx(const funkgui::AxisMap& m, float v) { return m.px0 + (v - m.v0) * (m.px1 - m.px0) / (m.v1 - m.v0); }
    float toValue(const funkgui::AxisMap& m, float px) { return m.v0 + (px - m.px0) * (m.v1 - m.v0) / (m.px1 - m.px0); }

    // ---- configurations -----------------------------------------------------------------------------------------------------

    struct Cfg
    {
        std::string name;
        std::vector<std::pair<Pid, float>> plains;
        int  scale = layout::kDefaultScaleDb;
        bool live = false;
    };

    // Up to `most` plain values a parameter offers: its detents (stepped), else lo / (mid) / hi of its range.
    std::vector<float> values(const fcdsp::ParamView& v, Pid p, int most, bool mid)
    {
        std::vector<float> out;
        const fcdsp::ParamSpec* s = v.spec[fcdsp::idx(p)];
        if (s == nullptr)
            return out;
        const fcdsp::SlotState st = v[p].state;
        if (st == fcdsp::SlotState::stepped)
        {
            const int n = fcdsp::stepCount(*s);
            for (int i = 0; i < n && static_cast<int>(out.size()) < most; ++i)
                out.push_back(fcdsp::stepPlain(*s, n <= most ? i : (i == 0 ? 0 : n - 1)));
            if (n > most)
                out.resize(2);
        }
        else if (st == fcdsp::SlotState::live && s->lo < s->hi)
        {
            out.push_back(s->lo);
            if (mid)
                out.push_back(0.5f * (s->lo + s->hi));
            out.push_back(s->hi);
        }
        return out;
    }

    std::vector<Cfg> configs(const fcdsp::ModeEntry& entry)
    {
        probe::FakeFacade f(entry.desc->key);
        fcdsp::Resolution res;
        fcdsp::resolve(entry, f.currentRaw(), res);
        std::vector<Cfg> out;
        out.push_back({ "def", {}, layout::kDefaultScaleDb, false });
        const auto add = [&](const char* prefix, Pid p, int most, bool mid) {
            const std::vector<float> v = values(res.view, p, most, mid);
            for (std::size_t i = 0; i < v.size(); ++i)
                out.push_back({ std::string(prefix) + std::to_string(i), { { p, v[i] } }, layout::kDefaultScaleDb, false });
        };
        add("r", Pid::ratio, 12, true);
        add("k", Pid::knee, 12, false);
        add("t", Pid::thr, 2, false);
        if (res.view[Pid::range].state == fcdsp::SlotState::live)
        {
            const fcdsp::ParamSpec* s = res.view.spec[fcdsp::idx(Pid::range)];
            if (s != nullptr && s->lo <= 6.0f && s->hi >= 6.0f)
                out.push_back({ "g6", { { Pid::range, 6.0f } }, layout::kDefaultScaleDb, false });
        }
        out.push_back({ "s12", {}, 12, false });
        out.push_back({ "s24", {}, 24, false });
        out.push_back({ "s72", {}, 72, false });
        out.push_back({ "live", {}, layout::kDefaultScaleDb, true });
        return out;
    }

    void setScale(funkgui::HeadlessHost& host, int scale)
    {
        for (std::size_t i = 0; i < layout::kScalesDb.size(); ++i)
            if (layout::kScalesDb[i] == scale)
            {
                const funkgui::Rect& r = layout::kBandTransfer.scaleCells[i];
                host.click(r.centreX(), r.centreY());
            }
    }

    // The ratio as the RATIO slot speaks its active detent (stepped) or its value (continuous).
    std::string ratioWords(const ui::Panel& panel)
    {
        funkgui::ValueView v;
        panel.context().slot(Pid::ratio).view(v);
        if (v.state == funkgui::ValueState::stepped && v.detents != nullptr && v.detent >= 0 && v.detent < v.nDetents)
        {
            const funkgui::Detent& d = v.detents[v.detent];
            return d.spoken != nullptr && d.spoken[0] != '\0' ? d.spoken : d.label;
        }
        return v.text.spoken[0] != '\0' ? std::string(v.text.spoken) : std::string(v.text.value);
    }

    // ---- one configuration's rows ----------------------------------------------------------------------------------------

    void curveRows(Probe& P, const fcdsp::ModeEntry& entry, const Cfg& cfg)
    {
        probe::FakeFacade facade(entry.desc->key);
        for (const auto& [pid, plain] : cfg.plains)
            facade.setPlain(pid, plain);
        fcdsp::Resolution res;
        fcdsp::resolve(entry, facade.currentRaw(), res);
        fcdsp::EngineParams eng = res.eng;
        ui::Panel panel(facade, kOpts);
        funkgui::HeadlessHost host(panel, 0, 1.0f);
        if (cfg.scale != layout::kDefaultScaleDb)
            setScale(host, cfg.scale);
        if (cfg.live)
        {
            const auto slot = static_cast<uint16_t>(fcdsp::slotOf(entry));
            fcdsp::UiFrame f = probe::FakeFacade::quietFrame(slot, res.eng);
            f.flags |= fcdsp::kUiLive;
            f.thrDb -= 4.0f;
            f.makeupEffDb += 3.0f;
            f.mix = 0.5f;
            facade.publish(f);
            host.tick(2, kDt);
            fcdsp::overlaySmoothed(f, eng);
            P.eq("curve.live.overlaid", panel.context().frame.overlaid ? 1 : 0, 1);
        }
        else if (host.settle(kMaxSettle, kDt) > kMaxSettle)
        {
            P.harnessError("ui.curve: the panel did not settle (" + cfg.name + ")");
            return;
        }
        const std::string key = "curve." + cfg.name;
        const funkgui::PrimList& pl = host.draw();
        const funkgui::AxisRec* level = axisOf(pl, ui::tag::level);
        if (level == nullptr || !level->hasX || !level->hasY)
        {
            P.harnessError("ui.curve: no LEVEL axis (" + cfg.name + ")");
            return;
        }
        P.eq(key + ".scale", panel.context().meterScaleDb, cfg.scale);
        const funkgui::Rect& plot = layout::kBandTransfer.plot;
        const float lo = level->x.v0, hi = level->x.v1;
        const auto f = [&](float x) {
            float g = 0.0f;
            fcdsp::analysis::staticGain(entry, eng, std::span<const float>(&x, 1), std::span<float>(&g, 1));
            return x + g - eng.preGainDb;
        };
        const auto ay = [&](float db) { return toPx(level->y, db); };

        // Vertices and chords.
        double vertex = 0.0, chord = 0.0;
        float minX = 1e9f, maxX = -1e9f;
        int segs = 0;
        for (const funkgui::Prim* p : tagged(pl, ui::tag::transferCurve))
        {
            const Seg s = segOf(*p);
            ++segs;
            minX = std::min(minX, std::min(s.x0, s.x1));
            maxX = std::max(maxX, std::max(s.x0, s.x1));
            for (const auto& [px, py] : { std::pair{ s.x0, s.y0 }, std::pair{ s.x1, s.y1 } })
                vertex = std::max(vertex, static_cast<double>(std::fabs(py - ay(f(toValue(level->x, px))))));
            for (int k = 1; k <= 8; ++k)
            {
                const float t = static_cast<float>(k) / 9.0f;
                const float px = s.x0 + t * (s.x1 - s.x0), py = s.y0 + t * (s.y1 - s.y0);
                chord = std::max(chord, static_cast<double>(std::fabs(py - ay(f(toValue(level->x, px))))));
            }
        }
        // Drawn where the analytic curve enters the plot; a curve that only grazes the frame may or may not leave a
        // sliver after clipping.
        bool inside = false, touches = false;
        for (int k = 0; k <= 192; ++k)
        {
            const float y = f(lo + (hi - lo) * static_cast<float>(k) / 192.0f);
            inside = inside || (y > lo + 0.05f && y < hi - 0.05f);
            touches = touches || (y >= lo - 0.05f && y <= hi + 0.05f);
        }
        P.eq(key + ".drawn", inside ? (segs > 0 ? 1 : 0) : (segs == 0 || touches ? 1 : 0), 1);
        P.le(key + ".vertex_px", vertex, 0.5);
        P.le(key + ".chord_px", chord, 0.25);
        {
            // A curve that only grazes the frame may draw nothing (the drawn row above); then there is no span to
            // judge (v1.2 Opto Tube 1B: THRESHOLD's low end at 4:1 meets the plot's bottom exactly at its right edge).
            const bool leftIn = f(lo) >= lo - 1e-3f, rightIn = f(hi) >= lo - 1e-3f && f(hi) <= hi + 1e-3f;
            const double gapL = leftIn && segs > 0 ? std::max(0.0f, minX - plot.x) : 0.0;
            const double gapR = rightIn && segs > 0 ? std::max(0.0f, plot.right() - maxX) : 0.0;
            P.le(key + ".span_px", std::max(gapL, gapR), 0.5);
        }

        // The threshold line: centred on y(T_in), ending at the TRANSFER handle's x.
        {
            const float t = fcdsp::analysis::inputThresholdDb(eng);
            const std::vector<const funkgui::Prim*> marks = tagged(pl, ui::tag::thresholdMark);
            if (t > lo && t < hi)
            {
                P.eq(key + ".threshold_found", static_cast<int64_t>(marks.size()), 1);
                if (marks.size() == 1)
                {
                    const Box b = boxOf(*marks[0]);
                    const double dy = std::fabs(b.y + 0.5f * b.h - ay(t));
                    const double dx = std::fabs(b.x + b.w - toPx(level->x, t));
                    P.le(key + ".threshold_px", std::max(dy, dx), 0.5);
                }
            }
            else
            {
                P.eq(key + ".threshold_absent", static_cast<int64_t>(marks.size()), 0);
            }
        }

        // The ratio label: the TRANSFER image's title against the RATIO slot's own words.
        {
            std::string title;
            for (const funkgui::A11yItem& it : host.accessibility())
                if (it.role == funkgui::A11yRole::image && it.title.rfind("Transfer curve:", 0) == 0 && it.visible)
                    title = it.title;
            const std::string want = "ratio " + ratioWords(panel);
            const bool ok = title.find(want + ",") != std::string::npos
                         || (title.size() >= want.size() && title.compare(title.size() - want.size(), want.size(), want) == 0);
            if (!ok)
                std::printf("NOTE     %s: title '%s' does not name '%s'\n", key.c_str(), title.c_str(), want.c_str());
            P.eq(key + ".ratio_label", ok ? 1 : 0, 1);
        }

        // NET_CURVE iff it differs by > 0.05 dB (and some of it lies inside the plot, which it is clipped to).
        {
            float worst = 0.0f;
            bool netInside = false;
            for (int k = 0; k <= 192; ++k)
            {
                const float x = lo + (hi - lo) * static_cast<float>(k) / 192.0f;
                const float y = f(x);
                const float gain = y + eng.preGainDb - x;
                const float yn = x + fcdsp::analysis::netGainDb(gain, eng.makeupDb, eng.mix);
                worst = std::max(worst, std::fabs(yn - y));
                netInside = netInside || (yn > lo + 0.05f && yn < hi - 0.05f);
            }
            const bool want = worst > layout::band::kNetCurveDb + 1e-3f && netInside;
            const bool fuzzy = std::fabs(worst - layout::band::kNetCurveDb) <= 1e-3f;
            const bool drawn = !tagged(pl, ui::tag::netCurve).empty();
            if (!fuzzy && drawn != want)
                std::printf("NOTE     %s: net curve drawn %d, differs by %.3f dB, inside %d\n", key.c_str(), drawn ? 1 : 0,
                            static_cast<double>(worst), netInside ? 1 : 0);
            P.eq(key + ".net", fuzzy || drawn == want ? 1 : 0, 1);
        }
    }

    // ---- interaction --------------------------------------------------------------------------------------------------------

    struct Rig
    {
        explicit Rig(const fcdsp::ModeEntry& e) : entry(e), facade(e.desc->key), panel(facade, kOpts), host(panel, 0, 1.0f)
        {
            host.settle(kMaxSettle, kDt);
            level = *axisOf(host.draw(), ui::tag::level);
            fcdsp::resolve(entry, facade.currentRaw(), res);
        }
        float f(float x) const
        {
            float g = 0.0f;
            fcdsp::analysis::staticGain(entry, res.eng, std::span<const float>(&x, 1), std::span<float>(&g, 1));
            return x + g - res.eng.preGainDb;
        }
        float ax(float db) const { return toPx(level.x, db); }
        float ay(float db) const { return toPx(level.y, db); }
        bool inside(float px, float py) const
        {
            const funkgui::Rect& r = layout::kBandTransfer.plot;
            return px >= r.x && px <= r.right() && py >= r.y && py <= r.bottom();
        }
        funkgui::ValueView view(Pid p) const
        {
            funkgui::ValueView v;
            panel.context().slot(p).view(v);
            return v;
        }

        const fcdsp::ModeEntry& entry;
        probe::FakeFacade facade;
        ui::Panel panel;
        funkgui::HeadlessHost host;
        funkgui::AxisRec level{};
        fcdsp::Resolution res;
    };

    // Writes to `pid` since resetCounts(): one gesture, every write inside it (or none at all when refused).
    void gestureRows(Probe& P, const std::string& key, probe::FakePort& port, bool writable)
    {
        if (writable)
        {
            P.eq(key + ".gesture", port.begins() == 1 && port.ends() == 1 && !port.inGesture() ? 1 : 0, 1);
            P.eq(key + ".outside", port.setsOutsideGesture(), 0);
        }
        else
        {
            P.eq(key + ".refused", port.sets(), 0);
        }
    }

    bool writable(const funkgui::ValueView& v)
    {
        return v.state == funkgui::ValueState::continuous || v.state == funkgui::ValueState::stepped;
    }

    int detentOf(const funkgui::ValueView& v, float host01)
    {
        for (int i = 0; v.detents != nullptr && i < v.nDetents; ++i)
            if (std::fabs(v.detents[i].host01 - host01) < 1e-6f)
                return i;
        return -1;
    }

    void interactionRows(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const float tIn = [&] {
            Rig r(entry);
            return fcdsp::analysis::inputThresholdDb(r.res.eng);
        }();

        {   // handles show only under the pointer
            Rig r(entry);
            const bool hidden = tagged(r.host.draw(), ui::tag::handle).empty();
            r.host.move(layout::kBandTransfer.plot.centreX(), layout::kBandTransfer.plot.centreY());
            r.host.settle(kMaxSettle, kDt);
            const bool shown = !tagged(r.host.draw(), ui::tag::handle).empty();
            P.eq("drag.handles_hidden", hidden ? 1 : 0, 1);
            P.eq("drag.handles_shown", shown || !(tIn > r.level.x.v0 && tIn < r.level.x.v1) ? 1 : 0, 1);
        }

        {   // the threshold handle along unity: +12 px right and up = +3 dB of T_in at 4 px/dB
            Rig r(entry);
            const float x0 = r.ax(tIn), y0 = r.ay(tIn);
            if (r.inside(x0, y0))
            {
                const funkgui::ValueView v = r.view(Pid::thr);
                probe::FakePort& port = r.facade.fakePort(Pid::thr);
                const float before = port.plain();
                port.resetCounts();
                const bool stepped = v.state == funkgui::ValueState::stepped;
                const float d = stepped ? 60.0f : 12.0f;
                r.host.drag(x0, y0, x0 + d, y0 - d, 8);
                gestureRows(P, "drag.thr", port, writable(v));
                if (v.state == funkgui::ValueState::continuous)
                    P.near("drag.thr.delta_db", port.plain() - before, 3.0, 0.05);
                else if (stepped)
                    P.eq("drag.thr.detent_up", detentOf(v, port.value01()) > v.detent ? 1 : 0, 1);
            }
        }

        {   // the ratio handle down: one detent (half a pitch + 7 px), or 70 px of a continuous ratio
            Rig r(entry);
            const float xr = std::min(tIn + layout::band::kRatioHandleDb, layout::band::kRatioHandleMaxDb);
            const float x0 = r.ax(xr), y0 = r.ay(r.f(xr));
            const funkgui::ValueView v = r.view(Pid::ratio);
            if (r.inside(x0, y0) && v.state != funkgui::ValueState::na)
            {
                probe::FakePort& port = r.facade.fakePort(Pid::ratio);
                const float before = port.value01();
                port.resetCounts();
                const float pitch = v.nDetents > 1 ? std::clamp(240.0f / static_cast<float>(v.nDetents - 1), 24.0f, 64.0f)
                                                   : 0.0f;
                const float travel = v.state == funkgui::ValueState::stepped
                                         ? 0.5f * pitch + layout::band::kSnapHysteresisPx + 1.0f : 70.0f;
                r.host.drag(x0, y0, x0, y0 + travel, 8);
                gestureRows(P, "drag.ratio", port, writable(v));
                if (v.state == funkgui::ValueState::stepped)
                {
                    const int want = std::min(v.detent + 1, v.nDetents - 1);
                    P.eq("drag.ratio.detent", detentOf(v, port.value01()), want);
                }
                else if (v.state == funkgui::ValueState::continuous)
                {
                    P.eq("drag.ratio.raised", port.value01() > before || before >= 1.0f ? 1 : 0, 1);
                }
            }
        }

        {   // the knee's right ring 8 px out (in when W sits at its top): W ± 4 dB absolutely (kFlagPlotIsPlain), else
            // a relative move the same way
            Rig r(entry);
            const funkgui::ValueView v = r.view(Pid::knee);
            const float w = std::max(r.res.eng.kneeDb, 0.0f);
            const float xk = tIn + 0.5f * w;
            const float x0 = r.ax(xk), y0 = r.ay(r.f(xk));
            const bool shown = 0.5f * w * layout::kBandLevel.pxPerDb(48.0f) >= 4.0f && r.inside(x0, y0);
            if (v.state != funkgui::ValueState::na && shown)
            {
                probe::FakePort& port = r.facade.fakePort(Pid::knee);
                const float before = port.plain();
                port.resetCounts();
                const fcdsp::ParamSpec* s = r.res.view.spec[fcdsp::idx(Pid::knee)];
                const bool up = s == nullptr || !(s->lo < s->hi) || before + 4.0f <= s->hi;
                r.host.drag(x0, y0, x0 + (up ? 8.0f : -8.0f), y0, 8);
                gestureRows(P, "drag.knee", port, writable(v));
                const bool plain = s != nullptr && (s->flags & fcdsp::kFlagPlotIsPlain) != 0 && s->display.toDisplay == nullptr;
                if (v.state == funkgui::ValueState::continuous && plain)
                    P.near("drag.knee.delta_db", port.plain() - before, up ? 4.0 : -4.0, 0.05);
                else if (writable(v))
                    P.eq("drag.knee.moved", up ? (port.plain() >= before ? 1 : 0) : (port.plain() <= before ? 1 : 0), 1);
            }
        }

        {   // the HISTORY threshold line up 8 px: +2 dB
            Rig r(entry);
            const funkgui::ValueView v = r.view(Pid::thr);
            const float y0 = r.ay(tIn);
            if (v.state == funkgui::ValueState::continuous && tIn > r.level.x.v0 && tIn < r.level.x.v1)
            {
                probe::FakePort& port = r.facade.fakePort(Pid::thr);
                const float before = port.plain();
                port.resetCounts();
                r.host.drag(140.0f, y0, 140.0f, y0 - 8.0f, 8);
                gestureRows(P, "drag.line", port, true);
                P.near("drag.line.delta_db", port.plain() - before, 2.0, 0.05);
            }
        }
    }

    // ---- the Mode-switch landing (02 §8.7) ------------------------------------------------------------------------------

    double curveError(const funkgui::PrimList& pl, const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng)
    {
        const funkgui::AxisRec* level = axisOf(pl, ui::tag::level);
        if (level == nullptr)
            return 99.0;
        const funkgui::Rect& plot = layout::kBandTransfer.plot;
        double worst = 0.0;
        for (const funkgui::Prim* p : tagged(pl, ui::tag::transferCurve))
        {
            const Seg sg = segOf(*p);
            for (const auto& [px, py] : { std::pair{ sg.x0, sg.y0 }, std::pair{ sg.x1, sg.y1 } })
            {
                const float x = toValue(level->x, px);
                float g = 0.0f;
                fcdsp::analysis::staticGain(entry, eng, std::span<const float>(&x, 1), std::span<float>(&g, 1));
                const float want = toPx(level->y, x + g - eng.preGainDb);
                if (want >= plot.y - 0.5f && want <= plot.bottom() + 0.5f)
                    worst = std::max(worst, static_cast<double>(std::fabs(py - want)));
            }
        }
        return worst;
    }

    void landingRows(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeEntry* other = nullptr;
        for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
            if (m.entry != nullptr && m.entry != &entry && other == nullptr)
                other = m.entry;
        if (other == nullptr)
            return;
        std::size_t ghostsThere = 0;
        {
            probe::FakeFacade f(other->desc->key);
            ui::Panel panel(f, kOpts);
            funkgui::HeadlessHost host(panel, 0, 1.0f);
            host.settle(kMaxSettle, kDt);
            ghostsThere = tagged(host.draw(), ui::tag::ghostCurve).size();
        }
        probe::FakeFacade facade(entry.desc->key);
        ui::Panel panel(facade, kOpts);
        funkgui::HeadlessHost host(panel, 0, 1.0f);
        host.settle(kMaxSettle, kDt);
        facade.setMode(other->desc->key);                             // a host writes `mode` (no other parameter)
        host.tick(1, kDt);
        const bool easing = panel.wantsFullRate();
        const std::size_t ghostsNow = tagged(host.draw(), ui::tag::ghostCurve).size();
        host.tick(static_cast<int>(std::ceil(layout::band::kCurveEaseS / kDt)) + 1, kDt);
        fcdsp::Resolution res;
        fcdsp::resolve(*other, facade.currentRaw(), res);
        const double landed = curveError(host.draw(), *other, res.eng);
        const int frames = host.settle(kMaxSettle, kDt);
        const std::size_t ghostsAfter = tagged(host.draw(), ui::tag::ghostCurve).size();
        P.eq("landing.easing", easing ? 1 : 0, 1);
        P.eq("landing.old_curve_ghosted", ghostsNow > ghostsThere ? 1 : 0, 1);
        P.le("landing.new_curve_px", landed, 0.5);
        P.eq("landing.settles", frames <= kMaxSettle ? 1 : 0, 1);
        P.eq("landing.ghost_released", static_cast<int64_t>(ghostsAfter), static_cast<int64_t>(ghostsThere));
    }
}

FCMP_PROBE(ui, curve)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.curve writes UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.curve: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    funkgui::UiPreferences& prefs = funkgui::UiPreferences::get();
    for (const Cfg& cfg : configs(*entry))
    {
        prefs.setInt("meterScaleDb", layout::kDefaultScaleDb);
        prefs.setInt("historySpanTenths", layout::kDefaultSpanTenths);
        curveRows(P, *entry, cfg);
    }
    prefs.setInt("meterScaleDb", layout::kDefaultScaleDb);
    interactionRows(P, *entry);
    landingRows(P, *entry);
    return P.finish();
}
