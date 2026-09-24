// FCMP_PROBE layer=ui name=chars scope=mode timeout=300
//
// ui.chars.<key> (02 §7.3, §9.3; 01 §7; ADR-41; U4): every curve and marker of the Characteristics screen's STEP,
// SIDECHAIN and COLOUR panes is the analysis API's, mapped back through the pane's recorded axis. Headless over a
// FakeFacade, Panel{skipHint, syncPreview}, dpi 1, theme 0 (the runs are told apart by their theme-0 inks). The probe
// renders its own references at full resolution (decimate 1) from 02 §9.3's stimuli, independently of PreviewWorker.
// Spec rows only (no golden rows).
//
// PreviewWorker (the Mode's defaults, 48 kHz; runs a0 a1 a2 = +6 / +12 / +24 dB attack, r0 r1 = 50 ms / 2 s release):
//   worker.sync                  a synchronous request completes inside tick() (serial, key)
//   worker.<run>.stim            Run::stim is 02 §9.3's stimulus
//   worker.<run>.gr_bitwise      Run::gr == analysis::stepResponse(Run::stim) itself (every d-th sample), bitwise
//   worker.<run>.measured        Run::measured == measure(full render, expDb); Trace::measured[law] for every law
//   worker.<run>.trace           Trace from / to / column edges / min-max are the full render's (1e-4 dB)
//   worker.async_equal           the worker thread's result equals the synchronous one bitwise (FTZ, K2 #24)
// Per configuration <cfg> (def; hpf, tilt: the SC HPF / SC tilt moved; atk0/atk1, rel0/rel1: the time slots at their
// ends or end detents; live: a scripted live UiFrame at 96 kHz whose smoothed fields differ from resolve()):
//   step.<cfg>.<pane>.axis       STEP_AXIS: log time [tMinS, tMaxS] over plot.x…right, fraction 0…1 over y0…y100
//   step.<cfg>.<pane>.r<i>.drawn   a run's 116 chords drawn iff its GR moves >= 0.05 dB
//   step.<cfg>.<pane>.r<i>.vertex_px   every chord end within 0.5 px of the reference (fraction of the steady state)
//   step.<cfg>.<pane>.r<i>.envelope_px each column's drawn extent within 0.5 px of the reference samples' min / max
//   step.<cfg>.<pane>.meas_found / meas_px / meas_on_curve   STEP_MEAS at axis(analysis::measure under the spec's
//                                law) ± 0.5 px, on the measured run's curve (± 0.5 px)
//   step.<cfg>.<pane>.spec_found / spec_px   STEP_SPEC's hairline covers axis(attackSpec/releaseSpec.seconds)
//   step.<cfg>.<pane>.band        the program band iff the spec is a program value with a published range
//   step.<cfg>.<pane>.ghosts      a stepped time slot draws its other (non-program) detents' nominal curves
//   sc.<cfg>.axis / drawn / vertex_px   SC_CURVE on analysis::scResponse (clamped to the plot) <= 0.5 px
//   sc.<cfg>.handle_found / handle_px   SC_HANDLE at (x(scHpfHz) or the 20 Hz edge when OFF, y(−3 dB)) ± 0.5 px
//   colour.<cfg>.axis / drawn / vertex_px   COLOUR_CURVE on analysis::colourCurve at the drawn GR <= 0.5 px
//   colour.<cfg>.bars / bars_px   HARMONICS bars = harmonicsDb at −6 dBFS (H2…H5, THD) over the HARMONICS axis
//   colour.live.marks_px          COLOUR_MARK at ±colourInPeakDb
// Interaction at the defaults (every write inside one gesture; locked / derived write nothing):
//   drag.atk.* / drag.rel.*       the spec marker dragged 10 px right: continuous = seconds on the log axis (rel
//                                 1e-3), stepped = the next detent, else refused
//   drag.schpf.*                  the SC corner dragged 20 px right (hertz on the log axis), then left past 20 Hz: OFF
//   a11y.handles                  one slider item per non-n/a handle (ATTACK, RELEASE, SC HPF)
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/core/Theme.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/text/FontService.h>
#include <funkgui/widgets/ValueModel.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
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
    namespace an = fcdsp::analysis;

    constexpr float kDt = 1.0f / 60.0f;
    constexpr int   kMaxSettle = 600;
    constexpr ui::PanelOptions kOpts { true, true, false };        // skipHint, syncPreview, !ignoreLive
    constexpr int   kCols = ui::PreviewWorker::kColumns;
    constexpr float kMinSpanDb = 0.05f;
    constexpr float kLiveFs = 96000.0f;
    constexpr double kPx = 0.5;                                    // 03 §3.7: curve on screen 0.5 px

    // ---- prims -------------------------------------------------------------------------------------------------------------

    struct Box { float x = 0, y = 0, w = 0, h = 0; float cx() const { return x + 0.5f * w; } float cy() const { return y + 0.5f * h; } };
    struct Seg { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; uint32_t col = 0; };

    bool isKind(const funkgui::Prim& p, funkgui::PrimKind k)
    {
        return static_cast<int>(p.d2[2] + 0.5f) == static_cast<int>(k);
    }

    Box boxOf(const funkgui::Prim& p)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx - p.d0[2], cy - p.d0[3], 2.0f * p.d0[2], 2.0f * p.d0[3] };
    }

    Seg segOf(const funkgui::Prim& p)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx + p.d1[0], cy + p.d1[1], cx + p.d1[2], cy + p.d1[3], p.c0 };
    }

    std::vector<const funkgui::Prim*> tagged(const funkgui::PrimList& pl, funkgui::Tag t,
                                             std::optional<funkgui::PrimKind> kind = std::nullopt)
    {
        std::vector<const funkgui::Prim*> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == t && (!kind || isKind(p, *kind)))
                v.push_back(&p);
        return v;
    }

    std::vector<funkgui::AxisRec> axesOf(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        std::vector<funkgui::AxisRec> v;
        for (const funkgui::AxisRec& a : pl.axes)
            if (a.tag == t)
                v.push_back(a);
        return v;
    }

    double toPx(const funkgui::AxisMap& m, double v)
    {
        const double u = m.log ? std::log(v / static_cast<double>(m.v0)) / std::log(static_cast<double>(m.v1 / m.v0))
                               : (v - static_cast<double>(m.v0)) / static_cast<double>(m.v1 - m.v0);
        return static_cast<double>(m.px0) + u * static_cast<double>(m.px1 - m.px0);
    }

    double toValue(const funkgui::AxisMap& m, double px)
    {
        const double u = (px - static_cast<double>(m.px0)) / static_cast<double>(m.px1 - m.px0);
        return m.log ? static_cast<double>(m.v0) * std::pow(static_cast<double>(m.v1 / m.v0), u)
                     : static_cast<double>(m.v0) + u * static_cast<double>(m.v1 - m.v0);
    }

    uint32_t pack(funkgui::Col c)
    {
        return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16)
             | (static_cast<uint32_t>(c.a) << 24);
    }

    // ---- the reference step responses (02 §9.3), rendered at full resolution ---------------------------------------------

    an::StepStimulus designStimulus(bool release, int i, float fs)
    {
        an::StepStimulus s;
        s.fs = fs;
        if (release)
        {
            s.hiDbOverThr = 12.0f;
            s.hiSec = i == 0 ? 0.05f : 2.0f;
            s.loSec = 10.0f;
        }
        else
        {
            const std::array<float, 3> steps { 6.0f, 12.0f, 24.0f };
            s.hiDbOverThr = steps[static_cast<std::size_t>(i)];
            s.hiSec = 1.0f;
            s.loDbUnderThr = 24.0f;
            s.loSec = 0.2f;
        }
        s.decimate = 1;
        return s;
    }

    int64_t samplesOf(float seconds, float fs)
    {
        const double v = static_cast<double>(seconds) * static_cast<double>(fs) + 0.5;
        return v >= 1.0 ? static_cast<int64_t>(v) : 0;
    }

    struct RefRun
    {
        an::StepStimulus stim{};
        std::vector<float> g;                    // the full render
        int64_t edge = 0, last = -1;             // the step's first sample, the segment's last written sample
        double fs = 48000.0;
        bool valid = false;
        float from = 0.0f, to = 0.0f;

        // GR at t s after the edge: linear between (0, from) and the samples at (k + 1) / fs (analysis::measure's model).
        double at(double t) const
        {
            const double pos = t * fs - 1.0;
            if (pos < 0.0)
                return static_cast<double>(from) + std::max(t * fs, 0.0) * (static_cast<double>(g[static_cast<std::size_t>(edge)]) - from);
            const auto k = static_cast<int64_t>(pos);
            if (edge + k >= last)
                return static_cast<double>(g[static_cast<std::size_t>(last)]);
            const double u = pos - static_cast<double>(k);
            const double a = g[static_cast<std::size_t>(edge + k)], b = g[static_cast<std::size_t>(edge + k + 1)];
            return a + u * (b - a);
        }

        // min / max GR over [t0, t1]: both ends and every sample strictly inside.
        std::pair<double, double> range(double t0, double t1) const
        {
            double lo = std::min(at(t0), at(t1)), hi = std::max(at(t0), at(t1));
            const auto j0 = static_cast<int64_t>(std::floor(t0 * fs - 1.0)) + 1;
            const auto j1 = static_cast<int64_t>(std::ceil(t1 * fs - 1.0)) - 1;
            for (int64_t j = std::max<int64_t>(j0, 0); j <= j1 && edge + j <= last; ++j)
            {
                const double v = g[static_cast<std::size_t>(edge + j)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            return { lo, hi };
        }
    };

    RefRun renderRef(const fcdsp::ModeEntry& e, const fcdsp::EngineParams& eng, bool release, int i, float fs)
    {
        RefRun r;
        r.stim = designStimulus(release, i, fs);
        r.fs = static_cast<double>(fs);
        const int64_t pre = samplesOf(r.stim.preSec, fs), hi = samplesOf(r.stim.hiSec, fs), lo = samplesOf(r.stim.loSec, fs);
        r.g.assign(static_cast<std::size_t>(pre + hi + lo + 4), 0.0f);
        const int n = an::stepResponse(e, eng, r.stim, r.g);
        r.g.resize(static_cast<std::size_t>(std::max(n, 0)));
        r.edge = release ? pre + hi : pre;
        r.last = std::min<int64_t>(release ? pre + hi + lo : pre + hi, n) - 1;
        r.valid = r.edge >= 1 && r.last >= r.edge;
        if (r.valid)
        {
            r.from = r.g[static_cast<std::size_t>(r.edge - 1)];
            r.to = r.g[static_cast<std::size_t>(r.last)];
        }
        return r;
    }

    float measured(const RefRun& r, bool release, fcdsp::TimeLaw law)
    {
        const an::TimeReadout t = an::measure(r.g, r.stim, law);
        return release ? t.releaseS : t.attackS;
    }

    // ---- configurations -----------------------------------------------------------------------------------------------------

    struct Cfg
    {
        std::string name;
        std::vector<std::pair<Pid, float>> plains;
        bool live = false;
    };

    // The ends of a slot: its first and last detents (stepped) or lo / hi (continuous); none otherwise.
    std::vector<float> ends(const fcdsp::ParamView& v, Pid p)
    {
        const fcdsp::ParamSpec* s = v.spec[fcdsp::idx(p)];
        if (s == nullptr)
            return {};
        if (v[p].state == fcdsp::SlotState::stepped)
        {
            const int n = fcdsp::stepCount(*s);
            if (n < 2)
                return {};
            return { fcdsp::stepPlain(*s, 0), fcdsp::stepPlain(*s, n - 1) };
        }
        if (v[p].state == fcdsp::SlotState::live && s->lo < s->hi)
            return { s->lo, s->hi };
        return {};
    }

    std::vector<Cfg> configs(const fcdsp::ModeEntry& entry)
    {
        probe::FakeFacade f(entry.desc->key);
        fcdsp::Resolution res;
        fcdsp::resolve(entry, f.currentRaw(), res);
        const fcdsp::ParamView& v = res.view;
        std::vector<Cfg> out;
        out.push_back({ "def", {}, false });
        {   // hpf: 120 Hz, or the second detent
            const fcdsp::ParamSpec* s = v.spec[fcdsp::idx(Pid::schpf)];
            if (s != nullptr && v[Pid::schpf].state == fcdsp::SlotState::live && s->lo <= 120.0f && s->hi >= 120.0f)
                out.push_back({ "hpf", { { Pid::schpf, 120.0f } }, false });
            else if (s != nullptr && v[Pid::schpf].state == fcdsp::SlotState::stepped && fcdsp::stepCount(*s) > 1)
                out.push_back({ "hpf", { { Pid::schpf, fcdsp::stepPlain(*s, 1) } }, false });
        }
        {   // tilt: the SC tilt at its top (with the HPF on, so both shape the curve)
            const std::vector<float> e = ends(v, Pid::sce);
            if (!e.empty())
            {
                Cfg c { "tilt", { { Pid::sce, e.back() } }, false };
                if (!out.empty() && out.back().name == "hpf")
                    c.plains.push_back(out.back().plains.front());
                out.push_back(c);
            }
        }
        const std::array<std::pair<Pid, const char*>, 2> times { { { Pid::atk, "atk" }, { Pid::rel, "rel" } } };
        for (const auto& [pid, name] : times)
        {
            const std::vector<float> e = ends(v, pid);
            for (std::size_t i = 0; i < e.size(); ++i)
                out.push_back({ std::string(name) + std::to_string(i), { { pid, e[i] } }, false });
        }
        {   // the colour stage: every other VOICE detent, then DRIVE at its top on the last voice
            const fcdsp::ParamSpec* vs = v.spec[fcdsp::idx(Pid::voice)];
            std::vector<std::pair<Pid, float>> last;
            if (vs != nullptr && v[Pid::voice].state == fcdsp::SlotState::stepped)
                for (int i = 0; i < fcdsp::stepCount(*vs); ++i)
                {
                    last = { { Pid::voice, fcdsp::stepPlain(*vs, i) } };
                    if (i != v[Pid::voice].step)
                        out.push_back({ "voice" + std::to_string(i), last, false });
                }
            probe::FakeFacade g(entry.desc->key);
            for (const auto& [pid, plain] : last)
                g.setPlain(pid, plain);
            fcdsp::Resolution rv;
            fcdsp::resolve(entry, g.currentRaw(), rv);
            const std::vector<float> e = ends(rv.view, Pid::drive);
            if (!e.empty())
            {
                last.push_back({ Pid::drive, e.back() });
                out.push_back({ "drive", last, false });
            }
        }
        out.push_back({ "live", {}, true });
        return out;
    }

    // The scripted live frame: 96 kHz, an external key, 6 / 4 dB of GR, the colour stage's input peak at −6 / −8 dB,
    // and smoothed fields that differ from resolve() (the SC HPF at 150 Hz, the threshold 3 dB lower).
    fcdsp::UiFrame liveFrame(const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng)
    {
        fcdsp::UiFrame f = probe::FakeFacade::quietFrame(static_cast<uint16_t>(fcdsp::slotOf(entry)), eng);
        f.flags |= fcdsp::kUiLive | fcdsp::kUiExtKeyActive;
        f.sampleRate = kLiveFs;
        f.appliedGrDb[0] = 6.0f;
        f.appliedGrDb[1] = 4.0f;
        f.colourInPeakDb[0] = -6.0f;
        f.colourInPeakDb[1] = -8.0f;
        f.scPeakDb[0] = -14.0f;
        f.scPeakDb[1] = -20.0f;
        f.scHpfHz = 150.0f;
        f.thrDb -= 3.0f;
        return f;
    }

    // A Panel on `view` for `cfg`, settled (or, live, ticked while the frame is fresh), with the EngineParams, fs and GR
    // the panes are drawn for.
    struct Rig
    {
        Rig(const fcdsp::ModeEntry& e, const Cfg& cfg, const char* view)
            : entry(e), facade(e.desc->key), panel(facade, kOpts), host(panel, 0, 1.0f)
        {
            for (const auto& [pid, plain] : cfg.plains)
                facade.setPlain(pid, plain);
            panel.setView(*ui::findView(view), true);
            fcdsp::resolve(entry, facade.currentRaw(), res);
            eng = res.eng;
            if (cfg.live)
            {
                const fcdsp::UiFrame f = liveFrame(entry, res.eng);
                facade.publish(f);
                host.tick(4, kDt);                                // request, compute, build; still fresh
                fcdsp::overlaySmoothed(f, eng);
                fs = kLiveFs;
                gr = std::max(f.appliedGrDb[0], f.appliedGrDb[1]);
                settled = panel.context().frame.live && !panel.context().preview.pending();
            }
            else
            {
                settled = host.settle(kMaxSettle, kDt) <= kMaxSettle;
            }
        }

        const fcdsp::ModeEntry& entry;
        probe::FakeFacade facade;
        ui::Panel panel;
        funkgui::HeadlessHost host;
        fcdsp::Resolution res;
        fcdsp::EngineParams eng{};
        float fs = 48000.0f;
        float gr = 0.0f;
        bool settled = false;
    };

    // ---- STEP rows ----------------------------------------------------------------------------------------------------------

    void stepRows(Probe& P, const std::string& key, const Rig& r, const funkgui::PrimList& pl, bool release)
    {
        const layout::StepGeom& g = release ? layout::kStepRelease : layout::kStepAttack;
        const std::string k = key + (release ? ".release" : ".attack");
        const funkgui::Rect& p = g.plot;
        const auto inPane = [&](float x) { return x >= p.x - 0.5f && x <= p.right() + 0.5f; };

        const funkgui::AxisRec* ax = nullptr;
        std::vector<funkgui::AxisRec> axes = axesOf(pl, ui::tag::stepAxis);
        for (const funkgui::AxisRec& a : axes)
            if (a.hasX && std::fabs(a.x.px0 - p.x) < 0.01f)
                ax = &a;
        const bool axisOk = ax != nullptr && ax->hasY && ax->x.log && std::fabs(ax->x.px1 - p.right()) < 0.01f
                         && ax->x.v0 == g.tMinS && ax->x.v1 == g.tMaxS && ax->y.px0 == g.y0 && ax->y.px1 == g.y100
                         && ax->y.v0 == 0.0f && ax->y.v1 == 1.0f;
        P.eq(k + ".axis", axisOk ? 1 : 0, 1);
        if (!axisOk)
            return;

        const int nRuns = release ? 2 : 3;
        std::vector<RefRun> runs;
        for (int i = 0; i < nRuns; ++i)
            runs.push_back(renderRef(r.entry, r.eng, release, i, r.fs));
        const int mi = 1;                                          // the measured run: +12 dB / the 2 s burst
        const RefRun& ref = runs[static_cast<std::size_t>(mi)];
        const funkgui::Theme th = funkgui::Theme::graphite();
        const std::array<uint32_t, 3> inks = release ? std::array<uint32_t, 3>{ pack(th.ink52), pack(th.ink70), 0u }
                                                     : std::array<uint32_t, 3>{ pack(th.ink32), pack(th.ink52), pack(th.ink70) };
        const auto yOf = [&](double fraction) {
            return std::clamp(static_cast<double>(g.y0) + fraction * static_cast<double>(g.y100 - g.y0),
                              static_cast<double>(p.y), static_cast<double>(p.bottom()));
        };

        std::vector<Seg> segs;
        for (const funkgui::Prim* q : tagged(pl, ui::tag::stepCurve, funkgui::PrimKind::segment))
        {
            const Seg s = segOf(*q);
            if (inPane(s.x0) && inPane(s.x1))
                segs.push_back(s);
        }
        for (int i = 0; i < nRuns; ++i)
        {
            const RefRun& run = runs[static_cast<std::size_t>(i)];
            const double base = release ? static_cast<double>(ref.to) : static_cast<double>(run.from);
            const double full = release ? static_cast<double>(ref.from) : static_cast<double>(run.to);
            const double span = full - base;
            const bool want = run.valid && ref.valid && span >= static_cast<double>(kMinSpanDb);
            const std::string rk = k + ".r" + std::to_string(i);
            int chords = 0;
            double vertex = 0.0, envelope = 0.0;
            std::vector<std::pair<double, double>> extent(kCols, { 1e9, -1e9 });
            for (const Seg& s : segs)
            {
                if (s.col != inks[static_cast<std::size_t>(i)])
                    continue;
                const bool bar = std::fabs(s.x1 - s.x0) < 0.01f;
                const int col = bar ? static_cast<int>(std::floor(s.x0 - p.x))
                                    : static_cast<int>(std::lround(std::min(s.x0, s.x1) - p.x));
                if (col < 0 || col >= kCols)
                    continue;
                auto& ex = extent[static_cast<std::size_t>(col)];
                ex.first = std::min({ ex.first, static_cast<double>(s.y0), static_cast<double>(s.y1) });
                ex.second = std::max({ ex.second, static_cast<double>(s.y0), static_cast<double>(s.y1) });
                if (bar)
                    continue;
                ++chords;
                if (!want)
                    continue;
                for (const auto& [x, y] : { std::pair{ s.x0, s.y0 }, std::pair{ s.x1, s.y1 } })
                {
                    const double t = toValue(ax->x, static_cast<double>(x));
                    const double yw = yOf((run.at(t) - base) / span);
                    vertex = std::max(vertex, std::fabs(static_cast<double>(y) - yw));
                }
            }
            if (want)
                for (int c = 0; c < kCols; ++c)
                {
                    const auto uc = static_cast<std::size_t>(c);
                    const double t0 = toValue(ax->x, static_cast<double>(p.x) + c);
                    const double t1 = toValue(ax->x, static_cast<double>(p.x) + c + 1);
                    const auto [lo, hi] = run.range(t0, t1);
                    const double a = yOf((lo - base) / span), b = yOf((hi - base) / span);
                    envelope = std::max({ envelope, std::fabs(extent[uc].first - std::min(a, b)),
                                          std::fabs(extent[uc].second - std::max(a, b)) });
                }
            P.eq(rk + ".drawn", chords, want ? kCols : 0);
            if (want)
            {
                P.le(rk + ".vertex_px", vertex, kPx);
                P.le(rk + ".envelope_px", envelope, kPx);
            }
        }

        // The declared spec and the measured crossing.
        const fcdsp::ModeDescriptor& d = *r.entry.desc;
        const auto fn = release ? d.releaseSpec : d.attackSpec;
        const Pid pid = release ? Pid::rel : Pid::atk;
        const fcdsp::SlotState st = r.res.view[pid].state;
        const bool na = st == fcdsp::SlotState::na || fn == nullptr;
        const fcdsp::TimeSpec spec = fn != nullptr ? fn(r.res.view, r.eng) : fcdsp::TimeSpec{ 0.0f, fcdsp::TimeLaw::expDb };
        const bool isTime = spec.law != fcdsp::TimeLaw::rateDbPerS;
        const double ts = static_cast<double>(spec.seconds);
        const bool specWant = !na && isTime && ts >= static_cast<double>(g.tMinS) && ts <= static_cast<double>(g.tMaxS);
        std::vector<Box> hairlines, bands;
        for (const funkgui::Prim* q : tagged(pl, ui::tag::stepSpec, funkgui::PrimKind::rrect))
        {
            const Box b = boxOf(*q);
            if (inPane(b.cx()))
                (b.w <= 1.01f ? hairlines : bands).push_back(b);
        }
        P.eq(k + ".spec_found", static_cast<int64_t>(hairlines.size()), specWant ? 1 : 0);
        if (specWant && hairlines.size() == 1)
        {
            const double x = toPx(ax->x, ts);                     // hairlineV floors x to the device px
            const Box& b = hairlines.front();
            const double out = std::max({ 0.0, static_cast<double>(b.x) - x, x - static_cast<double>(b.x + b.w) });
            P.le(k + ".spec_px", out, 1e-3);
        }
        const bool bandWant = !na && isTime && spec.program && spec.lo > 0.0f && spec.hi > spec.lo;
        P.eq(k + ".band", static_cast<int64_t>(bands.size()), bandWant ? 1 : 0);

        const RefRun& mr = runs[static_cast<std::size_t>(mi)];
        const double mbase = release ? static_cast<double>(ref.to) : static_cast<double>(mr.from);
        const double mspan = (release ? static_cast<double>(ref.from) : static_cast<double>(mr.to)) - mbase;
        const bool curve = mr.valid && mspan >= static_cast<double>(kMinSpanDb);
        const double tm = isTime && !na ? static_cast<double>(measured(mr, release, spec.law)) : -1.0;
        const bool measWant = curve && fn != nullptr && !na && isTime && tm > 0.0
                           && tm >= static_cast<double>(g.tMinS) && tm <= static_cast<double>(g.tMaxS);
        std::vector<Box> rings;
        for (const funkgui::Prim* q : tagged(pl, ui::tag::stepMeas, funkgui::PrimKind::rrect))
            if (inPane(boxOf(*q).cx()))
                rings.push_back(boxOf(*q));
        P.eq(k + ".meas_found", static_cast<int64_t>(rings.size()), measWant ? 1 : 0);
        if (measWant && rings.size() == 1)
        {
            const double x = toPx(ax->x, tm);
            P.le(k + ".meas_px", std::fabs(static_cast<double>(rings.front().cx()) - x), kPx);
            const double yw = yOf((mr.at(tm) - mbase) / mspan);
            P.le(k + ".meas_on_curve", std::fabs(static_cast<double>(rings.front().cy()) - yw), kPx);
        }

        // Stepped: the other non-program detents' nominal curves (29 chords each).
        int ghostsWant = 0;
        const fcdsp::ParamSpec* ps = r.res.view.spec[fcdsp::idx(pid)];
        if (st == fcdsp::SlotState::stepped && ps != nullptr && ps->law != fcdsp::TimeLaw::rateDbPerS)
            for (int i = 0; i < fcdsp::stepCount(*ps); ++i)
            {
                const auto ui = static_cast<std::size_t>(i);
                if (i != r.res.view[pid].step && ui < ps->steps.size() && (ps->steps[ui].tag & fcdsp::kTagProgram) == 0
                    && fcdsp::stepPlain(*ps, i) > 0.0f)
                    ++ghostsWant;
            }
        int ghostSegs = 0;
        for (const funkgui::Prim* q : tagged(pl, ui::tag::stepGhost, funkgui::PrimKind::segment))
            if (inPane(segOf(*q).x0))
                ++ghostSegs;
        P.eq(k + ".ghosts", ghostSegs, ghostsWant * (kCols / 4));
    }

    // ---- SIDECHAIN rows ------------------------------------------------------------------------------------------------------

    void scRows(Probe& P, const std::string& key, const Rig& r, const funkgui::PrimList& pl)
    {
        const layout::SidechainGeom& g = layout::kSidechain;
        const funkgui::Rect& p = g.plot;
        const std::vector<funkgui::AxisRec> axes = axesOf(pl, ui::tag::scAxis);
        const bool axisOk = axes.size() == 1 && axes[0].hasX && axes[0].hasY && axes[0].x.log && axes[0].x.px0 == p.x
                         && axes[0].x.px1 == p.right() && axes[0].x.v0 == g.fMinHz && axes[0].x.v1 == g.fMaxHz
                         && axes[0].y.px0 == p.y && axes[0].y.px1 == p.bottom() && axes[0].y.v0 == g.dbTop
                         && axes[0].y.v1 == g.dbBottom;
        P.eq(key + ".axis", axisOk ? 1 : 0, 1);
        if (!axisOk)
            return;
        const funkgui::AxisRec& ax = axes[0];
        const auto yAt = [&](double hz) {
            float h = static_cast<float>(hz), m = 0.0f;
            an::scResponse(r.entry, r.eng, r.fs, std::span<const float>(&h, 1), std::span<float>(&m, 1));
            return std::clamp(toPx(ax.y, static_cast<double>(m)), static_cast<double>(p.y), static_cast<double>(p.bottom()));
        };
        const std::vector<const funkgui::Prim*> segs = tagged(pl, ui::tag::scCurve, funkgui::PrimKind::segment);
        P.eq(key + ".drawn", segs.empty() ? 0 : 1, 1);
        double vertex = 0.0;
        for (const funkgui::Prim* q : segs)
        {
            const Seg s = segOf(*q);
            for (const auto& [x, y] : { std::pair{ s.x0, s.y0 }, std::pair{ s.x1, s.y1 } })
                vertex = std::max(vertex, std::fabs(static_cast<double>(y) - yAt(toValue(ax.x, static_cast<double>(x)))));
        }
        P.le(key + ".vertex_px", vertex, kPx);

        // The corner handle: the ring (writable) or the cross (locked, derived); none when n/a.
        const fcdsp::SlotState st = r.res.view[Pid::schpf].state;
        const bool want = st != fcdsp::SlotState::na;
        const float hz = r.eng.scHpfHz;
        const double hx = hz >= layout::chars::kScOffHz ? std::clamp(toPx(ax.x, static_cast<double>(hz)),
                                                                    static_cast<double>(p.x), static_cast<double>(p.right()))
                                                         : static_cast<double>(p.x);
        const double hy = toPx(ax.y, static_cast<double>(layout::chars::kScHandleDb));
        std::vector<Box> marks;
        for (const funkgui::Prim* q : tagged(pl, ui::tag::scHandle, funkgui::PrimKind::rrect))
        {
            const Box b = boxOf(*q);
            const bool ring = std::fabs(b.w - 5.0f) < 0.01f && std::fabs(b.h - 5.0f) < 0.01f;
            const bool vbar = b.w <= 1.01f && std::fabs(b.h - 5.0f) < 0.01f;   // the cross's vertical stroke
            if (ring || vbar)
                marks.push_back(b);
        }
        P.eq(key + ".handle_found", static_cast<int64_t>(marks.size()), want ? 1 : 0);
        if (want && marks.size() == 1)
        {
            const Box& b = marks.front();
            const bool ring = b.w > 1.01f;
            const double dx = ring ? std::fabs(static_cast<double>(b.cx()) - hx)
                                   : std::max({ 0.0, static_cast<double>(b.x) - hx, hx - static_cast<double>(b.x + b.w) });
            P.le(key + ".handle_px", std::max(dx, std::fabs(static_cast<double>(b.cy()) - hy)), kPx);
        }
    }

    // ---- COLOUR rows ---------------------------------------------------------------------------------------------------------

    void colourRows(Probe& P, const std::string& key, const Rig& r, const funkgui::PrimList& pl)
    {
        const layout::ColourGeom& g = layout::kColour;
        const funkgui::Rect& p = g.plot;
        const std::vector<funkgui::AxisRec> axes = axesOf(pl, ui::tag::colourAxis);
        const bool axisOk = axes.size() == 1 && axes[0].hasX && axes[0].hasY && axes[0].x.px0 == p.x
                         && axes[0].x.px1 == p.right() && axes[0].x.v0 == -1.0f && axes[0].x.v1 == 1.0f
                         && axes[0].y.px0 == p.bottom() && axes[0].y.px1 == p.y && axes[0].y.v0 == -1.0f
                         && axes[0].y.v1 == 1.0f;
        P.eq(key + ".axis", axisOk ? 1 : 0, 1);
        if (!axisOk)
            return;
        const funkgui::AxisRec& ax = axes[0];
        const bool colour = r.entry.desc->hasColour;
        const std::vector<const funkgui::Prim*> segs = tagged(pl, ui::tag::colourCurve, funkgui::PrimKind::segment);
        P.eq(key + ".drawn", segs.empty() ? 0 : 1, colour ? 1 : 0);
        double vertex = 0.0;
        for (const funkgui::Prim* q : segs)
        {
            const Seg s = segOf(*q);
            for (const auto& [x, y] : { std::pair{ s.x0, s.y0 }, std::pair{ s.x1, s.y1 } })
            {
                const float xin = static_cast<float>(toValue(ax.x, static_cast<double>(x)));
                float yo = 0.0f;
                an::colourCurve(r.entry, r.eng, r.gr, std::span<const float>(&xin, 1), std::span<float>(&yo, 1));
                const double yw = std::clamp(toPx(ax.y, static_cast<double>(yo)), static_cast<double>(p.y),
                                             static_cast<double>(p.bottom()));
                vertex = std::max(vertex, std::fabs(static_cast<double>(y) - yw));
            }
        }
        P.le(key + ".vertex_px", vertex, kPx);

        // Harmonics: H2…H5 and THD at −6 dBFS, bars over the HARMONICS axis.
        std::array<float, 8> h{};
        an::harmonicsDb(r.entry, r.eng, r.gr, std::pow(10.0f, g.harmonicsAmpDb / 20.0f), std::span<float, 8>(h));
        double power = 0.0;
        for (std::size_t i = 1; i < h.size(); ++i)
            power += std::pow(10.0, static_cast<double>(h[i]) / 10.0);
        const std::array<double, 5> want { h[1], h[2], h[3], h[4], 10.0 * std::log10(std::max(power, 1e-24)) };
        const std::vector<funkgui::AxisRec> bx = axesOf(pl, ui::tag::harmonics);
        std::vector<Box> bars;
        for (const funkgui::Prim* q : tagged(pl, ui::tag::harmonics, funkgui::PrimKind::rrect))
            bars.push_back(boxOf(*q));
        std::sort(bars.begin(), bars.end(), [](const Box& a, const Box& b) { return a.y < b.y; });
        int expected = 0;
        double worst = 0.0;
        const float pitch = g.harmonics.h / 5.0f;
        if (bx.size() == 1 && colour)
            for (std::size_t i = 0; i < want.size(); ++i)
            {
                const double w = toPx(bx[0].x, std::clamp(want[i], static_cast<double>(bx[0].x.v0), 0.0))
                               - static_cast<double>(bx[0].x.px0);
                if (!(w > 0.0))
                    continue;
                ++expected;
                const float top = g.harmonics.y + pitch * static_cast<float>(i);
                double got = -1.0;
                for (const Box& b : bars)
                    if (b.y >= top && b.y < top + pitch)
                        got = static_cast<double>(b.w);
                worst = std::max(worst, got < 0.0 ? 99.0 : std::fabs(got - w));
            }
        P.eq(key + ".bars", static_cast<int64_t>(bars.size()), expected);
        P.le(key + ".bars_px", worst, kPx);
    }

    // ---- the PreviewWorker ---------------------------------------------------------------------------------------------------

    // Bitwise equality, member by member (the structs have padding).
    template <typename T, std::size_t N>
    bool sameBits(const std::array<T, N>& a, const std::array<T, N>& b)
    {
        return std::memcmp(a.data(), b.data(), sizeof(T) * N) == 0;
    }

    bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

    bool sameTrace(const ui::PreviewWorker::Trace& a, const ui::PreviewWorker::Trace& b)
    {
        return a.valid == b.valid && sameBits(a.from, b.from) && sameBits(a.to, b.to) && sameBits(a.edge, b.edge)
            && sameBits(a.lo, b.lo) && sameBits(a.hi, b.hi) && sameBits(a.measured, b.measured);
    }

    bool sameRun(const ui::PreviewWorker::Run& a, const ui::PreviewWorker::Run& b)
    {
        return a.n == b.n && a.stim.decimate == b.stim.decimate && sameBits(a.stim.fs, b.stim.fs)
            && sameBits(a.measured.attackS, b.measured.attackS) && sameBits(a.measured.releaseS, b.measured.releaseS)
            && a.measured.law == b.measured.law && sameBits(a.gr, b.gr);
    }

    void workerRows(Probe& P, const fcdsp::ModeEntry& entry)
    {
        probe::FakeFacade f(entry.desc->key);
        fcdsp::Resolution res;
        fcdsp::resolve(entry, f.currentRaw(), res);
        ui::PreviewWorker w(true);
        w.setActive(true);
        w.request(entry, res.eng, 48000.0f, 7);
        w.tick(kDt);
        P.eq("worker.sync", w.result().key == 7 && w.result().serial == 1 && !w.pending() ? 1 : 0, 1);

        const auto check = [&](bool release, int i) {
            const std::string k = std::string("worker.") + (release ? "r" : "a") + std::to_string(i);
            const ui::PreviewWorker::Run& run = release ? w.result().release[static_cast<std::size_t>(i)]
                                                        : w.result().attack[static_cast<std::size_t>(i)];
            const ui::PreviewWorker::Trace& tr = release ? w.releaseTrace(i) : w.attackTrace(i);
            const an::StepStimulus want = designStimulus(release, i, 48000.0f);
            P.eq(k + ".stim", run.stim.fs == want.fs && run.stim.preSec == want.preSec
                                  && run.stim.hiDbOverThr == want.hiDbOverThr && run.stim.hiSec == want.hiSec
                                  && run.stim.loDbUnderThr == want.loDbUnderThr && run.stim.loSec == want.loSec
                                  && run.stim.decimate >= 1 ? 1 : 0, 1);

            // Run::gr is stepResponse(Run::stim) itself.
            std::vector<float> direct(ui::PreviewWorker::kMaxPoints, 0.0f);
            const int n = an::stepResponse(entry, res.eng, run.stim, direct);
            bool same = n == run.n && n > 0;
            for (int j = 0; same && j < n; ++j)
                same = std::memcmp(&direct[static_cast<std::size_t>(j)], &run.gr[static_cast<std::size_t>(j)], sizeof(float)) == 0;
            P.eq(k + ".gr_bitwise", same ? 1 : 0, 1);

            // Measured on the full render, under every law.
            const RefRun ref = renderRef(entry, res.eng, release, i, 48000.0f);
            const an::TimeReadout e = an::measure(ref.g, ref.stim, fcdsp::TimeLaw::expDb);
            bool meas = std::memcmp(&e.attackS, &run.measured.attackS, sizeof(float)) == 0
                     && std::memcmp(&e.releaseS, &run.measured.releaseS, sizeof(float)) == 0;
            for (int law = 0; law < ui::PreviewWorker::kTimeLaws; ++law)
            {
                const float m = measured(ref, release, static_cast<fcdsp::TimeLaw>(law));
                meas = meas && std::memcmp(&m, &tr.measured[static_cast<std::size_t>(law)], sizeof(float)) == 0;
            }
            P.eq(k + ".measured", meas ? 1 : 0, 1);

            // The trace is the full render's, on the pane's log-time columns.
            const layout::StepGeom& g = release ? layout::kStepRelease : layout::kStepAttack;
            double worst = ref.valid == tr.valid ? 0.0 : 99.0;
            if (ref.valid && tr.valid)
            {
                worst = std::max(std::fabs(static_cast<double>(tr.from) - ref.from), std::fabs(static_cast<double>(tr.to) - ref.to));
                const double span = static_cast<double>(g.tMaxS) / static_cast<double>(g.tMinS);
                for (int c = 0; c <= kCols; ++c)
                {
                    const double t0 = static_cast<double>(g.tMinS) * std::pow(span, static_cast<double>(c) / kCols);
                    worst = std::max(worst, std::fabs(static_cast<double>(tr.edge[static_cast<std::size_t>(c)]) - ref.at(t0)));
                    if (c == kCols)
                        break;
                    const double t1 = static_cast<double>(g.tMinS) * std::pow(span, static_cast<double>(c + 1) / kCols);
                    const auto [lo, hi] = ref.range(t0, t1);
                    worst = std::max({ worst, std::fabs(static_cast<double>(tr.lo[static_cast<std::size_t>(c)]) - lo),
                                       std::fabs(static_cast<double>(tr.hi[static_cast<std::size_t>(c)]) - hi) });
                }
            }
            P.le(k + ".trace", worst, 1e-4);                     // float rounding of the linear model (dB)
        };
        for (int i = 0; i < ui::PreviewWorker::kAttackRuns; ++i)
            check(false, i);
        for (int i = 0; i < ui::PreviewWorker::kReleaseRuns; ++i)
            check(true, i);

        // The worker thread computes exactly what the synchronous path computes (K2 #24: FTZ in every entry point).
        ui::PreviewWorker a(false);
        a.setActive(true);
        a.request(entry, res.eng, 48000.0f, 7);
        for (int ticks = 0; ticks < 4000 && (a.pending() || a.result().key != 7); ++ticks)
        {
            a.tick(kDt);
            juce::Thread::sleep(1);
        }
        bool equal = a.result().key == 7;
        for (int i = 0; equal && i < ui::PreviewWorker::kAttackRuns; ++i)
            equal = sameTrace(a.attackTrace(i), w.attackTrace(i))
                 && sameRun(a.result().attack[static_cast<std::size_t>(i)], w.result().attack[static_cast<std::size_t>(i)]);
        for (int i = 0; equal && i < ui::PreviewWorker::kReleaseRuns; ++i)
            equal = sameTrace(a.releaseTrace(i), w.releaseTrace(i))
                 && sameRun(a.result().release[static_cast<std::size_t>(i)], w.result().release[static_cast<std::size_t>(i)]);
        a.stop();
        P.eq("worker.async_equal", equal ? 1 : 0, 1);
    }

    // ---- interaction ---------------------------------------------------------------------------------------------------------

    int detentOf(const funkgui::ValueView& v, float host01)
    {
        for (int i = 0; v.detents != nullptr && i < v.nDetents; ++i)
            if (std::fabs(v.detents[i].host01 - host01) < 1e-6f)
                return i;
        return -1;
    }

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

    void dragMarker(Probe& P, const fcdsp::ModeEntry& entry, bool release)
    {
        const Cfg def { "def", {}, false };
        Rig r(entry, def, "chars.sidechain");
        const Pid pid = release ? Pid::rel : Pid::atk;
        const layout::StepGeom& g = release ? layout::kStepRelease : layout::kStepAttack;
        const std::string k = release ? "drag.rel" : "drag.atk";
        const auto fn = release ? entry.desc->releaseSpec : entry.desc->attackSpec;
        const fcdsp::SlotState st = r.res.view[pid].state;
        if (fn == nullptr || st == fcdsp::SlotState::na)
            return;
        const fcdsp::TimeSpec spec = fn(r.res.view, r.eng);
        const double ts = static_cast<double>(spec.seconds);
        if (spec.law == fcdsp::TimeLaw::rateDbPerS || ts < static_cast<double>(g.tMinS) || ts > static_cast<double>(g.tMaxS))
            return;
        const funkgui::AxisMap xAxis { g.plot.x, g.plot.right(), g.tMinS, g.tMaxS, true };
        const auto x0 = static_cast<float>(toPx(xAxis, ts));
        const float y = 0.5f * (g.y0 + g.y100);
        probe::FakePort& port = r.facade.fakePort(pid);
        funkgui::ValueView v;
        r.panel.context().slot(pid).view(v);
        const bool writable = v.state == funkgui::ValueState::continuous || v.state == funkgui::ValueState::stepped;
        const float before = port.plain();
        port.resetCounts();
        if (v.state == funkgui::ValueState::stepped && v.detents != nullptr && v.nDetents > 1 && v.detent >= 0)
        {
            const int target = v.detent + 1 < v.nDetents ? v.detent + 1 : v.detent - 1;
            const double tt = static_cast<double>(fcdsp::toPlain(pid, v.detents[target].host01)) / 1000.0;
            const auto x1 = static_cast<float>(toPx(xAxis, tt));
            if (std::fabs(x1 - x0) <= layout::band::kSnapHysteresisPx + 0.5f)
                return;                                           // detents closer than the hysteresis: not reachable
            r.host.drag(x0, y, x1, y, 8);
            gestureRows(P, k, port, true);
            P.eq(k + ".detent", detentOf(v, port.value01()), target);
            return;
        }
        // 10 px towards the inside of the slot's range (right, unless that clamps at its top).
        const fcdsp::ParamSpec* s = r.res.view.spec[fcdsp::idx(pid)];
        const bool ranged = s != nullptr && s->lo < s->hi;
        const float dx = ranged && toValue(xAxis, static_cast<double>(x0) + 10.0) * 1000.0 > static_cast<double>(s->hi)
                             ? -10.0f : 10.0f;
        r.host.drag(x0, y, x0 + dx, y, 8);
        gestureRows(P, k, port, writable);
        if (v.state == funkgui::ValueState::continuous)
        {
            double want = toValue(xAxis, static_cast<double>(x0 + dx)) * 1000.0;
            if (ranged)
                want = std::clamp(want, static_cast<double>(s->lo), static_cast<double>(s->hi));
            P.near(k + ".ms", port.plain(), want, 0.0, 1e-3);
        }
        else
        {
            P.eq(k + ".unchanged", port.plain() == before ? 1 : 0, 1);
        }
    }

    void dragCorner(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const Cfg def { "def", {}, false };
        Rig r(entry, def, "chars.sidechain");
        const fcdsp::SlotState st = r.res.view[Pid::schpf].state;
        if (st == fcdsp::SlotState::na)
            return;
        const layout::SidechainGeom& g = layout::kSidechain;
        const funkgui::AxisMap xAxis { g.plot.x, g.plot.right(), g.fMinHz, g.fMaxHz, true };
        const float hz = r.eng.scHpfHz;
        const float x0 = hz >= layout::chars::kScOffHz ? static_cast<float>(toPx(xAxis, static_cast<double>(hz))) : g.plot.x;
        const float y = g.plot.y + (g.dbTop - layout::chars::kScHandleDb) * g.plot.h / (g.dbTop - g.dbBottom);
        probe::FakePort& port = r.facade.fakePort(Pid::schpf);
        funkgui::ValueView v;
        r.panel.context().slot(Pid::schpf).view(v);
        port.resetCounts();
        if (v.state == funkgui::ValueState::stepped)
        {
            if (v.detents == nullptr || v.nDetents < 2 || v.detent < 0)
                return;
            const int target = v.detent + 1 < v.nDetents ? v.detent + 1 : v.detent - 1;
            const float th = fcdsp::toPlain(Pid::schpf, v.detents[target].host01);
            const float x1 = th >= layout::chars::kScOffHz ? static_cast<float>(toPx(xAxis, static_cast<double>(th))) : g.plot.x;
            if (std::fabs(x1 - x0) <= layout::band::kSnapHysteresisPx + 0.5f)
                return;
            r.host.drag(x0, y, x1, y, 8);
            gestureRows(P, "drag.schpf", port, true);
            P.eq("drag.schpf.detent", detentOf(v, port.value01()), target);
            return;
        }
        const bool writable = v.state == funkgui::ValueState::continuous;
        r.host.drag(x0, y, x0 + 20.0f, y, 8);
        gestureRows(P, "drag.schpf", port, writable);
        if (!writable)
            return;
        const fcdsp::ParamSpec* s = r.res.view.spec[fcdsp::idx(Pid::schpf)];
        double want = toValue(xAxis, static_cast<double>(x0) + 20.0);
        if (s != nullptr && s->lo < s->hi)
            want = std::clamp(want, static_cast<double>(s->lo), static_cast<double>(s->hi));
        P.near("drag.schpf.hz", port.plain(), want, 0.0, 1e-3);

        // Then left past the 20 Hz edge: OFF.
        r.host.settle(kMaxSettle, kDt);
        const float x1 = r.panel.context().frame.eng.scHpfHz >= layout::chars::kScOffHz
                             ? static_cast<float>(toPx(xAxis, static_cast<double>(r.panel.context().frame.eng.scHpfHz)))
                             : g.plot.x;
        port.resetCounts();
        r.host.drag(x1, y, g.plot.x - 10.0f, y, 8);
        gestureRows(P, "drag.schpf_off", port, true);
        P.eq("drag.schpf_off.off", port.plain() < layout::chars::kScOffHz ? 1 : 0, 1);
    }

    void a11yRows(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const Cfg def { "def", {}, false };
        Rig r(entry, def, "chars.sidechain");
        int want = 0;
        for (const Pid pid : { Pid::atk, Pid::rel, Pid::schpf })
            want += r.res.view[pid].state != fcdsp::SlotState::na ? 1 : 0;
        int got = 0;
        const ui::ViewIndex cs = ui::ViewIndex::charScreen;
        for (const funkgui::A11yItem& it : r.host.accessibility())
        {
            const int plot = ui::plotIndexOf(it.id);
            if (ui::viewIndexOf(it.id) == static_cast<int>(cs) && it.visible && ui::isHandleId(it.id)
                && (plot == 5 || plot == 6 || plot == 7) && it.role == funkgui::A11yRole::slider)
                ++got;
        }
        P.eq("a11y.handles", got, want);
    }
}

FCMP_PROBE(ui, chars)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.chars: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    workerRows(P, *entry);
    for (const Cfg& cfg : configs(*entry))
    {
        {
            Rig r(*entry, cfg, "chars.sidechain");
            if (!r.settled)
            {
                P.harnessError("ui.chars: the panel did not settle (" + cfg.name + ", chars.sidechain)");
                return P.finish();
            }
            const funkgui::PrimList& pl = r.host.draw();
            stepRows(P, "step." + cfg.name, r, pl, false);
            stepRows(P, "step." + cfg.name, r, pl, true);
            scRows(P, "sc." + cfg.name, r, pl);
        }
        {
            Rig r(*entry, cfg, "chars.colour");
            if (!r.settled)
            {
                P.harnessError("ui.chars: the panel did not settle (" + cfg.name + ", chars.colour)");
                return P.finish();
            }
            const funkgui::PrimList& pl = r.host.draw();
            colourRows(P, "colour." + cfg.name, r, pl);
            if (cfg.live && entry->desc->hasColour)
            {
                const float a = std::pow(10.0f, -6.0f / 20.0f);   // the louder lane's colourInPeakDb
                const layout::ColourGeom& g = layout::kColour;
                const std::array<double, 2> want { g.plot.x + 0.5 * (1.0 - a) * g.plot.w,
                                                   g.plot.x + 0.5 * (1.0 + a) * g.plot.w };
                std::vector<Box> marks;
                for (const funkgui::Prim* q : tagged(pl, ui::tag::colourMark, funkgui::PrimKind::rrect))
                    marks.push_back(boxOf(*q));
                std::sort(marks.begin(), marks.end(), [](const Box& x, const Box& y) { return x.x < y.x; });
                double worst = marks.size() == 2 ? 0.0 : 99.0;
                for (std::size_t i = 0; i < marks.size() && i < 2; ++i)
                    worst = std::max({ worst, std::max(0.0, static_cast<double>(marks[i].x) - want[i]),
                                       std::max(0.0, want[i] - static_cast<double>(marks[i].x + marks[i].w)) });
                P.le("colour.live.marks_px", worst, 1e-3);
            }
        }
    }
    dragMarker(P, *entry, false);
    dragMarker(P, *entry, true);
    dragCorner(P, *entry);
    a11yRows(P, *entry);
    return P.finish();
}
