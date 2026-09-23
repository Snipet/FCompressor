// FCMP_PROBE layer=dsp name=static scope=mode timeout=60
//
// dsp.static.<key> (F3, S2; D1: 03 §3.4, C §5.2; E §2.2, §6.5; Rig driver, K3 #10): the static curve at 1 kHz, measured
// through the Mode's own engine (EngineRig) with a single-bin DFT, against the Mode's DECLARED curve (its staticGr, the
// same policy code the audio thread runs) and, for family == textbook, against this probe's own independent textbook
// formula (Giannoulis et al. 2012), so a bug in the shared gain computer cannot pass both.
//
// Configurations (C §5.2): ratio = every detent (stepped) or {1.5, 2, 4, 10, 20, inf}:1 clamped to the Mode's range
// (continuous); threshold {-30, -10} dB (the nearest detents when stepped); knee {0, 6, 12} dB (detents when stepped),
// else the resolved values. Attack at the Mode's fastest and release at its slowest (C §5.2: release the slowest, so
// the 2f0 ripple stays below 0.01 dB; the fastest attack makes the GR-domain peak ballistics hold the sine's peak, E
// §2.4, instead of a duty-cycle average). Every other parameter at the Mode's defaults.
// Staircase: -60 ... max(+6, T + W/2 + 20) dBFS in 1 dB steps, plus 0.25 dB steps across [T - W/2 - 2, T + W/2 + 2],
// rising; each step holds max(0.3 s, 8 tauA) then measures 0.1 s (whole cycles at 48 kHz). Levels are the Mode's
// DetectorLaw x axis (rms: the sine peak is +3.01 dB).
//
// Rows per configuration <cfg> = r<R>.t<T>.k<W>:
//   fidelity (NOTE while provisional, SPRINTS §7 D12; else spec, tolerance by Rigor, 03 §3.7):
//     static.<cfg>.err_outside_db / .err_inside_db   measured gain against the declared curve, outside / inside
//                                                     the knee
//     static.<cfg>.ratio (or .slope_db_per_db at inf) dIn/dOut between T+10 and T+20 against the declared curve's
//     static.<cfg>.thr_db                             input where GR first exceeds 0.1 dB, against the declared curve's
//     static.<cfg>.textbook_db                        declared staticGr against the textbook formula (0.001 dB)
//     static.<cfg>.vs_textbook_db                     measured output against the textbook formula (curve tolerance)
//   structural (spec, blocking):
//     static.<cfg>.tap_vs_audio_db                    the tap's GR against the audio-derived GR (meter truth, C §5.2)
//     static.<cfg>.nonfinite, static.<cfg>.gr_min_db  finite output; GR >= 0
//     static.analysis.{sc_shape,colour_curve}_nonfinite  the entry's scShapeDb (20 Hz-20 kHz) and colourCurve (x in
//                                                        [-2, 2] at 6 dB GR) are finite
//   golden (abs:0.01; candidates while the Mode is provisional, adopt refuses them):
//     static.<cfg>.x<d>.out_db for d in {-10, 0, +10, +20} relative to T
// The reference configuration's curve is printed (NOTE) for inspection: level, measured output, textbook output.
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;

    constexpr float kFs = 48000.0f;
    constexpr double kHz = 1000.0;

    std::string fmtLabel(double v)                      // key-safe: 1.5 -> 1p5, -30 -> -30
    {
        char b[32];
        std::snprintf(b, sizeof b, "%g", v);
        std::string s(b);
        std::replace(s.begin(), s.end(), '.', 'p');
        return s;
    }

    std::string ratioLabel(float slope)
    {
        if (slope >= 1.0f)
            return slope == 1.0f ? "inf" : "neg" + fmtLabel(1.0 / (static_cast<double>(slope) - 1.0));
        return fmtLabel(std::round(1000.0 / (1.0 - static_cast<double>(slope))) / 1000.0);
    }

    // The values of `pid` to run: the detents (stepped), the wanted values clamped into [lo, hi] (continuous/hybrid),
    // or the resolved value (locked, derived, n/a).
    std::vector<float> choices(const ParamView& v, Pid pid, const std::vector<float>& wanted, bool nearestDetents)
    {
        const ParamSpec* s = v.spec[idx(pid)];
        std::vector<float> out;
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
        {
            if (!nearestDetents)
                for (const Step& st : s->steps)
                    out.push_back(st.plain);
            else
                for (const float w : wanted)
                {
                    const Step* best = &s->steps[0];
                    for (const Step& st : s->steps)
                        if (std::fabs(st.plain - w) < std::fabs(best->plain - w))
                            best = &st;
                    out.push_back(best->plain);
                }
        }
        else if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            for (const float w : wanted)
                out.push_back(std::clamp(w, s->lo, s->hi));
        else
            out.push_back(v[pid].plain);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // The fastest / slowest value of a time parameter's active spec (its resolved value when not live).
    float timeEdge(const ParamView& v, Pid pid, bool fastest)
    {
        const ParamSpec* s = v.spec[idx(pid)];
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
            return fastest ? s->steps.front().plain : s->steps.back().plain;
        if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            return fastest ? s->lo : s->hi;
        return v[pid].plain;
    }

    // The independent textbook curve (C §5.2; Giannoulis et al. 2012), output level in the detector domain, double.
    double textbookOut(double x, double t, double w, double slope)
    {
        const double invR = 1.0 - slope;                // 1/R; 0 at inf:1
        if (w <= 0.0)
            return x <= t ? x : t + (x - t) * invR;
        if (2.0 * (x - t) < -w)
            return x;
        if (2.0 * std::fabs(x - t) <= w)
            return x + (invR - 1.0) * (x - t + w / 2.0) * (x - t + w / 2.0) / (2.0 * w);
        return t + (x - t) * invR;
    }

    double declaredGr(const ModeEntry& en, const EngineParams& e, double levelDb)
    {
        const float x = static_cast<float>(levelDb) + e.preGainDb;      // detector domain
        float gr = 0.0f;
        en.staticGr(e, &x, &gr, 1);
        const float range = e.rangeDb < kRangeOff ? e.rangeDb : kRangeOff;
        gr = std::min(gr, range);
        return (e.flags & kEngGrOff) != 0 ? 0.0 : static_cast<double>(gr);
    }

    // First input level where the GR exceeds `at` dB, linearly interpolated over the staircase; NaN if never.
    double crossing(const std::vector<double>& levels, const std::vector<double>& gr, double at)
    {
        for (std::size_t i = 1; i < levels.size(); ++i)
            if (gr[i] > at && gr[i - 1] <= at)
                return levels[i - 1] + (at - gr[i - 1]) / (gr[i] - gr[i - 1]) * (levels[i] - levels[i - 1]);
        return gr.empty() || gr[0] <= at ? std::nan("") : levels[0];
    }
} // namespace

FCMP_PROBE(dsp, static)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    const auto& tol = fcmp::probe::tol::forRigor(desc.rigor);
    fcmp::probe::Fidelity F(P, desc.provisional);

    RawParams base = fcmp::probe::modeRaw(en);
    ParamView view;
    resolveView(desc, base, view);
    base[Pid::atk] = timeEdge(view, Pid::atk, true);
    base[Pid::rel] = timeEdge(view, Pid::rel, false);
    resolveView(desc, base, view);

    const std::vector<float> ratios = choices(view, Pid::ratio,
                                              { 1.0f - 1.0f / 1.5f, 0.5f, 0.75f, 0.9f, 0.95f, 1.0f }, false);
    const std::vector<float> thrs = choices(view, Pid::thr, { -30.0f, -10.0f }, true);
    const std::vector<float> knees = choices(view, Pid::knee, { 0.0f, 6.0f, 12.0f }, false);
    std::printf("NOTE     %zu ratio(s) x %zu threshold(s) x %zu knee(s); attack %.9g ms, release %.9g ms\n",
                ratios.size(), thrs.size(), knees.size(), static_cast<double>(base[Pid::atk]),
                static_cast<double>(base[Pid::rel]));

    for (const float ratio : ratios)
        for (const float thr : thrs)
            for (const float knee : knees)
            {
                RawParams raw = base;
                raw[Pid::ratio] = ratio;
                raw[Pid::thr] = thr;
                raw[Pid::knee] = knee;
                const Resolution res = fcmp::probe::resolveRaw(en, raw);
                const EngineParams& e = res.eng;
                const double t = analysis::inputThresholdDb(e);
                const double w = std::max(0.0f, e.kneeDb);
                const double thrDet = static_cast<double>(e.thrDb), slope = static_cast<double>(e.slope);
                const std::string cfg = "static.r" + ratioLabel(e.slope) + ".t" + fmtLabel(t) + ".k" + fmtLabel(w);

                // the rising staircase
                std::vector<double> levels;
                const double top = std::max(6.0, std::ceil(t + w / 2.0 + 20.0));
                for (double l = -60.0; l <= top + 1e-9; l += 1.0)
                    levels.push_back(l);
                for (double l = t - w / 2.0 - 2.0; l <= t + w / 2.0 + 2.0 + 1e-9; l += 0.25)
                    levels.push_back(l);
                for (const double d : { -10.0, 0.0, 10.0, 20.0 })
                    levels.push_back(t + d);
                std::sort(levels.begin(), levels.end());
                levels.erase(std::unique(levels.begin(), levels.end(),
                                         [](double a, double b) { return std::fabs(a - b) < 1e-9; }),
                             levels.end());

                const double tauA = static_cast<double>(e.atkTauMs) / 1000.0;
                fcmp::probe::EngineRig rig(en, e, kFs);
                const fcmp::probe::CurveRun run = fcmp::probe::runSineStaircase(
                    rig, levels, fcmp::probe::peakOffsetDb(en, e), kHz, std::max(0.3, 8.0 * tauA), 0.1);

                const double offset = static_cast<double>(e.preGainDb) + static_cast<double>(rig.makeupTotalDb());
                double errOut = 0.0, errIn = 0.0, tapVsAudio = 0.0, vsTextbook = 0.0;
                std::vector<double> grMeas(levels.size()), grDecl(levels.size());
                const bool textbook = desc.family == CurveFamily::textbook && tol.textbookApplies;
                for (std::size_t i = 0; i < levels.size(); ++i)
                {
                    const fcmp::probe::CurvePoint& pt = run.points[i];
                    grDecl[i] = declaredGr(en, e, pt.levelDb);
                    grMeas[i] = offset - pt.gainDb;
                    const double err = std::fabs(grMeas[i] - grDecl[i]);
                    if (std::fabs(pt.levelDb - t) > w / 2.0)
                        errOut = std::max(errOut, err);
                    else
                        errIn = std::max(errIn, err);
                    tapVsAudio = std::max(tapVsAudio, std::fabs(pt.tapGrDb - grMeas[i]));
                    if (textbook)
                    {
                        const double x = pt.levelDb + static_cast<double>(e.preGainDb);
                        const double tb = x - textbookOut(x, thrDet, w, slope);
                        vsTextbook = std::max(vsTextbook, std::fabs(grMeas[i] - std::min(tb, static_cast<double>(
                                                                                    std::min(e.rangeDb, kRangeOff)))));
                    }
                }

                // fidelity
                F.near(cfg + ".err_outside_db", errOut, 0.0, tol.curveOutsideKneeDb);
                F.near(cfg + ".err_inside_db", errIn, 0.0, tol.curveInsideKneeDb);
                const auto at = [&](double level) {
                    for (std::size_t i = 0; i < levels.size(); ++i)
                        if (std::fabs(levels[i] - level) < 1e-9)
                            return i;
                    return levels.size();
                };
                const std::size_t i10 = at(t + 10.0), i20 = at(t + 20.0);
                if (i10 < levels.size() && i20 < levels.size())
                {
                    const double slopeMeas = (10.0 - (grMeas[i20] - grMeas[i10])) / 10.0;    // dOut / dIn
                    const double slopeDecl = (10.0 - (grDecl[i20] - grDecl[i10])) / 10.0;
                    if (slopeDecl > 0.01)
                        F.near(cfg + ".ratio", 1.0 / slopeMeas, 1.0 / slopeDecl, 0.0, tol.ratioRel);
                    else
                        F.near(cfg + ".slope_db_per_db", slopeMeas, slopeDecl, 0.01);
                }
                const double thrMeas = crossing(levels, grMeas, 0.1), thrDecl = crossing(levels, grDecl, 0.1);
                if (!std::isnan(thrDecl))
                    F.near(cfg + ".thr_db", std::isnan(thrMeas) ? 1e9 : thrMeas, thrDecl, tol.curveInsideKneeDb);
                if (textbook)
                {
                    double tbErr = 0.0;
                    for (double x = -80.0; x <= 30.0 + 1e-9; x += 0.05)
                    {
                        const float xd = static_cast<float>(x);
                        float gr = 0.0f;
                        en.staticGr(e, &xd, &gr, 1);
                        const double tb = static_cast<double>(xd)
                                        - textbookOut(xd, thrDet, w, slope);
                        tbErr = std::max(tbErr, std::fabs(static_cast<double>(gr) - tb));
                    }
                    F.near(cfg + ".textbook_db", tbErr, 0.0, tol.textbookDb);
                    F.near(cfg + ".vs_textbook_db", vsTextbook, 0.0, tol.curveOutsideKneeDb);
                }

                // structural
                P.le(cfg + ".tap_vs_audio_db", tapVsAudio, tol.tapGrDb);
                P.eq(cfg + ".nonfinite", run.nonfinite, 0);
                P.ge(cfg + ".gr_min_db", run.minGrDb, 0.0);

                // golden
                for (const double d : { -10.0, 0.0, 10.0, 20.0 })
                    if (const std::size_t i = at(t + d); i < levels.size())
                        P.num(cfg + ".x" + fmtLabel(d) + ".out_db", levels[i] + run.points[i].gainDb, Tol::abs(0.01));

                // the reference curve, for inspection (4:1, T = -30, W = 6 when the Mode has it)
                if (std::fabs(e.slope - 0.75f) < 1e-6f && std::fabs(t + 30.0) < 1e-6 && std::fabs(w - 6.0) < 1e-6)
                {
                    std::printf("NOTE     curve %s at %.0f Hz: level_db  out_db (measured)  out_db (textbook)  "
                                "err_db\n",
                                cfg.c_str(), kHz);
                    for (std::size_t i = 0; i < levels.size(); ++i)
                    {
                        // whole dB from T - 10 up: every 2 dB, and every dB inside the knee
                        const double l = levels[i];
                        if (l < t - 10.0 - 1e-9 || std::fabs(l - std::round(l)) > 1e-9)
                            continue;
                        if (static_cast<long>(std::round(l)) % 2 != 0 && std::fabs(l - t) > w / 2.0 + 1e-9)
                            continue;
                        // output = y_G(x) + makeup, x = level + preGain (the wet path carries preGain - GR)
                        const double x = l + static_cast<double>(e.preGainDb);
                        const double tbOut = textbookOut(x, thrDet, w, slope)
                                           + offset - static_cast<double>(e.preGainDb);
                        const double measOut = l + run.points[i].gainDb;
                        std::printf("NOTE       %7.2f  %10.4f  %10.4f  %+.4f\n", l, measOut, tbOut, measOut - tbOut);
                    }
                }
            }

    // The other analysis entry points of the ModeEntry (01 §7, §8.2) are finite over their domains.
    {
        const EngineParams e = fcmp::probe::resolveRaw(en, base).eng;
        std::vector<float> hz, mag(31), x, y(41);
        for (int i = 0; i < 31; ++i)
            hz.push_back(20.0f * std::pow(1000.0f, static_cast<float>(i) / 30.0f));     // 20 Hz ... 20 kHz
        for (int i = 0; i <= 40; ++i)
            x.push_back(-2.0f + 0.1f * static_cast<float>(i));
        en.scShapeDb(e, kFs, hz.data(), mag.data(), static_cast<int>(hz.size()));
        en.colourCurve(e, 6.0f, x.data(), y.data(), static_cast<int>(x.size()));
        const auto nonfinite = [](const std::vector<float>& v) {
            return static_cast<std::int64_t>(
                std::count_if(v.begin(), v.end(), [](float f) { return !std::isfinite(f); }));
        };
        P.eq("static.analysis.sc_shape_nonfinite", nonfinite(mag), 0);
        P.eq("static.analysis.colour_curve_nonfinite", nonfinite(y), 0);
    }

    F.summary();
    return P.finish();
}
