// FCMP_PROBE layer=dsp name=static scope=mode timeout=120
//
// dsp.static.<key> (F3, S2; F9, S3; D1: 03 §3.4, C §5.2; E §2.2, §2.6, §6.3, §6.5; K2 #1; Rig driver, K3 #10): the
// static curve at 1 kHz, measured through the Mode's own engine (EngineRig) with a single-bin DFT, against the Mode's
// DECLARED curve (its staticGr, the same policy code the audio thread runs, with a settled stage 2 where one is in:
// analysis::staticGr with CurveOpts::stage2, v1.2) and, for family == textbook, against this
// probe's own independent textbook formula (Giannoulis et al. 2012), so a bug in the shared gain computer cannot pass
// both.
//
// Configurations (C §5.2): ratio = every detent (stepped) or {1.5, 2, 4, 10, 20, inf}:1 clamped to the Mode's range
// (continuous); threshold {-30, -10} dB (the nearest detents when stepped); knee {0, 6, 12} dB (detents when stepped),
// else the resolved values. Attack at the Mode's fastest and release at its slowest (C §5.2: release the slowest, so
// the 2f0 ripple stays below 0.01 dB; the fastest attack makes the GR-domain peak ballistics hold the sine's peak, E
// §2.4, instead of a duty-cycle average; a feedback configuration skips AUTO release positions, M5 S10, below). Every
// other parameter at the Mode's defaults.
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
//
// F9 (S3) adds, for Modes whose parameters offer them (a spec that is live; skipped with a NOTE otherwise):
//   detectors  static.det.<step>.<cfg>.*  every other step of `det` (Clean: RMS, PK+RMS) at two configurations
//              (4:1, T -30, W 6 and inf:1, T -10, W 0), with the fidelity, structural and golden rows above; each
//              step's own DetectorLaw sets the x axis
//   voices     static.voice.<step>.d<drive>.<cfg>.*  every other step of `voice` (Clean: TUBE, DIODE, BRIGHT) at drive
//              {0, +12} dB, 4:1 T -30 W 6 and 1.5:1 T -10 W 6, with the Rig at 192 kHz (kVoiceFs): the declared curve
//              includes the colour stage's describing-function gain (E §6.3: N(A) of the entry's static colourCurve at
//              the wet amplitude, 64-point trapezoid), so err_outside/inside_db hold the voice to its declared static
//              shape; .gr_matches_off: the tapped GR equals voice OFF's bit for bit (colour never feeds the control
//              path, so voice OFF's tap_vs_audio row is the meter truth; a voice step that switches the topology, as
//              Bus 25's NEW/OLD = FF/FB does, has its own GR and prints a NOTE instead, DW S4); golden .x<d>.out_db,
//              and .thd_db (THD of a -10 dBFS sine with no GR at 48 kHz, H2-H8, abs:0.5, C §5.8 D8)
//   time mode  static.tmode.<step>.<cfg>.*  every other step of `tmode` (Clean: AUTO), and static.hold.<cfg>.* at the
//              longest hold: the static curve is the same
//   feedback   static.fb.* for every configuration above whose topo resolves to FB (K2 #1; none in Clean, which is FF):
//              .static_curve.max_err_db  staticGr (FB) against the 200-step bisection root of r = r^_fb(x - r)
//              .<branch>.max_err_db      (every ratio at the first threshold and the widest knee, and the hard knee at
//              the last ratio) per sample through the Rig (square steps T-10, T+20, T+6, T-20 dB at the
//              Mode's default times), the tapped GR against the bisection root of the branch E §2.6's predictor picks
//              from the tapped r[n-1] and x[n] (attack / release one-poles at the published times; held samples
//              counted); r^_fb is the Mode's computer with the loop gain k in place of the slope (QuadKnee.h's
//              convention), read through staticGr with topo FF. All <= 1e-5 dB. A Mode whose FB ballistics have other
//              branches (DualRelease's slow path, TcSelector) adds its branch maps here with its card. A configuration
//              whose attack or release is program-dependent (TimeSpec::program: the one-poles are not the published
//              times) prints a NOTE instead of the per-sample rows; its ballistics' probe holds its maps (Opto 2A's
//              T4 cell and its one-sample-delay loop: dsp.optocell, S9 M3). So does one whose stage 2 is in (v1.2, Mu
//              Mastering's LIMIT: the element's GR is the max of two loops; dsp.sharedelement holds those maps).
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace tolns = fcmp::probe::tol;

    constexpr float kFs = 48000.0f;
    constexpr double kHz = 1000.0;
    // A voice's static shape is judged at 192 kHz: at 48 kHz ADAA-1's one-sample kernel (Adaa.h) low-passes the
    // distortion residual (about 0.75 % of its fundamental at 1 kHz: 0.12 dB at 8 dB of saturation), the known price of
    // anti-aliasing at base rate; at 192 kHz (the rate HQ's 4x runs a 48 kHz session's colour at) it is 16x smaller.
    constexpr float kVoiceFs = 192000.0f;

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

    // A step label as a key part: lower case, [a-z0-9] only ("PK+RMS" -> "pkrms").
    std::string stepKey(const char* label)
    {
        std::string s;
        for (const char* p = label; p != nullptr && *p != '\0'; ++p)
            if (std::isalnum(static_cast<unsigned char>(*p)) != 0)
                s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        return s.empty() ? "step" : s;
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

    bool live(const ParamSpec* s)
    {
        return s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::stepped || s->kind == Kind::hybrid);
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

    // The element's settled curve: the computer, then a stage 2 (ModeEntry::staticS2: the identity while it is off, so
    // every Mode whose stage 2 rests OFF at the configuration reads its computer alone, bit for bit; Mu Mastering's LIMIT
    // rise, v1.2, is always in).
    double declaredGr(const ModeEntry& en, const EngineParams& e, double levelDb)
    {
        const float x[1] = { static_cast<float>(levelDb) + e.preGainDb };   // detector domain
        float gr = 0.0f;
        analysis::staticGr(en, e, std::span<const float>(x), std::span<float>(&gr, 1),
                           analysis::CurveOpts{ .colour = false, .stage2 = true });
        const float range = e.rangeDb < kRangeOff ? e.rangeDb : kRangeOff;
        gr = std::min(gr, range);
        return (e.flags & kEngGrOff) != 0 ? 0.0 : static_cast<double>(gr);
    }

    // The colour stage's describing-function gain (E §6.3) at sine amplitude `amp`, dB: N(A) = (1 / (pi A)) * the
    // integral of f(A sin t) sin t over a period, by the 64-point trapezoid on the entry's static colourCurve.
    double colourDfDb(const ModeEntry& en, const EngineParams& e, double amp)
    {
        constexpr int kPoints = 64;
        std::vector<float> x(kPoints), y(kPoints);
        std::vector<double> s(kPoints);
        for (int i = 0; i < kPoints; ++i)
        {
            s[static_cast<std::size_t>(i)] = fcmp::probe::sig::sinTurns(static_cast<double>(i) / kPoints);
            x[static_cast<std::size_t>(i)] = static_cast<float>(amp * s[static_cast<std::size_t>(i)]);
        }
        en.colourCurve(e, 0.0f, x.data(), y.data(), kPoints);
        double acc = 0.0;
        for (int i = 0; i < kPoints; ++i)
            acc += static_cast<double>(y[static_cast<std::size_t>(i)]) * s[static_cast<std::size_t>(i)];
        const double n = 2.0 * acc / (static_cast<double>(kPoints) * amp);
        return fcmp::probe::measure::dbFromAmplitude(n);
    }

    // First input level where the GR exceeds `at` dB, linearly interpolated over the staircase; NaN if never.
    double crossing(const std::vector<double>& levels, const std::vector<double>& gr, double at)
    {
        for (std::size_t i = 1; i < levels.size(); ++i)
            if (gr[i] > at && gr[i - 1] <= at)
                return levels[i - 1] + (at - gr[i - 1]) / (gr[i] - gr[i - 1]) * (levels[i] - levels[i - 1]);
        return gr.empty() || gr[0] <= at ? std::nan("") : levels[0];
    }

    struct CurveOpts
    {
        bool colour = false;                    // the declared curve includes the colour DF (voices)
        bool shapeRows = true;                  // ratio, threshold and textbook rows
        bool print = false;                     // print the curve (NOTE)
        bool emit = true;                       // false: measure only (no rows), for a reference run
        float fs = kFs;                         // the Rig's rate
    };

    // One D1 configuration: the staircase and its rows under `prefix`. Returns the tapped GR (mean per window) per
    // level.
    std::vector<double> curveRows(Probe& P, fcmp::probe::Fidelity& F, const tolns::RigorTolerances& tol,
                                  const ModeEntry& en, const RawParams& raw, const std::string& prefix,
                                  const CurveOpts& o)
    {
        const ModeDescriptor& desc = *en.desc;
        const Resolution res = fcmp::probe::resolveRaw(en, raw);
        const EngineParams& e = res.eng;
        const double t = analysis::inputThresholdDb(e);
        const double w = std::max(0.0f, e.kneeDb);
        const double thrDet = static_cast<double>(e.thrDb), slope = static_cast<double>(e.slope);
        const std::string cfg = prefix + "r" + ratioLabel(e.slope) + ".t" + fmtLabel(t) + ".k" + fmtLabel(w);

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
        const double peakOffset = fcmp::probe::peakOffsetDb(en, e);
        fcmp::probe::EngineRig rig(en, e, o.fs);
        const fcmp::probe::CurveRun run =
            fcmp::probe::runSineStaircase(rig, levels, peakOffset, kHz, std::max(0.3, 8.0 * tauA), 0.1);

        std::vector<double> taps(levels.size());
        for (std::size_t i = 0; i < levels.size(); ++i)
            taps[i] = run.points[i].tapGrDb;
        if (!o.emit)
            return taps;

        const double offset = static_cast<double>(e.preGainDb) + static_cast<double>(rig.makeupTotalDb());
        double errOut = 0.0, errIn = 0.0, tapVsAudio = 0.0, vsTextbook = 0.0;
        std::vector<double> grMeas(levels.size()), grDecl(levels.size());
        const bool textbook = o.shapeRows && desc.family == CurveFamily::textbook && tol.textbookApplies;
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            const fcmp::probe::CurvePoint& pt = run.points[i];
            grDecl[i] = declaredGr(en, e, pt.levelDb);
            grMeas[i] = offset - pt.gainDb;
            // a voice: the fundamental also carries the colour stage's describing-function gain at the wet amplitude
            // (the sine after the gain element: input peak x 10^((preGain - GR) / 20)), E §6.3
            double df = 0.0;
            if (o.colour)
                df = colourDfDb(en, e, fcmp::probe::measure::amplitudeFromDb(pt.levelDb + peakOffset
                                                                                + static_cast<double>(e.preGainDb)
                                                                                - grDecl[i]));
            const double err = std::fabs(grMeas[i] - (grDecl[i] - df));
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
        if (o.shapeRows)
        {
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
        }
        if (textbook)
        {
            double tbErr = 0.0;
            for (double x = -80.0; x <= 30.0 + 1e-9; x += 0.05)
            {
                const float xd = static_cast<float>(x);
                float gr = 0.0f;
                en.staticGr(e, &xd, &gr, 1);
                const double tb = static_cast<double>(xd) - textbookOut(xd, thrDet, w, slope);
                tbErr = std::max(tbErr, std::fabs(static_cast<double>(gr) - tb));
            }
            F.near(cfg + ".textbook_db", tbErr, 0.0, tol.textbookDb);
            F.near(cfg + ".vs_textbook_db", vsTextbook, 0.0, tol.curveOutsideKneeDb);
        }

        // structural (a voice's meter truth is voice OFF's row: its tap is voice OFF's, bit for bit, gr_matches_off)
        if (!o.colour)
            P.le(cfg + ".tap_vs_audio_db", tapVsAudio, tol.tapGrDb);
        P.eq(cfg + ".nonfinite", run.nonfinite, 0);
        P.ge(cfg + ".gr_min_db", run.minGrDb, 0.0);

        // golden
        for (const double d : { -10.0, 0.0, 10.0, 20.0 })
            if (const std::size_t i = at(t + d); i < levels.size())
                P.num(cfg + ".x" + fmtLabel(d) + ".out_db", levels[i] + run.points[i].gainDb, Tol::abs(0.01));

        if (o.print)
        {
            std::printf("NOTE     curve %s at %.0f Hz: level_db  out_db (measured)  out_db (textbook)  err_db\n",
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
                const double tbOut = textbookOut(x, thrDet, w, slope) + offset - static_cast<double>(e.preGainDb);
                const double measOut = l + run.points[i].gainDb;
                std::printf("NOTE       %7.2f  %10.4f  %10.4f  %+.4f\n", l, measOut, tbOut, measOut - tbOut);
            }
        }
        return taps;
    }

    // The raw parameters of a named configuration on top of `base`.
    RawParams withCurve(const RawParams& base, const ParamView& view, float slope, float thr, float knee)
    {
        RawParams raw = base;
        raw[Pid::ratio] = choices(view, Pid::ratio, { slope }, true).front();
        raw[Pid::thr] = choices(view, Pid::thr, { thr }, true).front();
        raw[Pid::knee] = choices(view, Pid::knee, { knee }, true).front();
        return raw;
    }

    // THD (H2-H8 against H1, dB) of a -10 dBFS 1 kHz sine through the Mode with no GR (threshold at its top).
    double thdDb(const ModeEntry& en, const EngineParams& e)
    {
        const std::size_t hold = static_cast<std::size_t>(0.2f * kFs), win = static_cast<std::size_t>(0.1f * kFs);
        std::vector<float> in(hold + win), yl(hold + win), yr(hold + win);
        const auto amp = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(-10.0));
        for (std::size_t k = 0; k < in.size(); ++k)
            in[k] = amp * fcmp::probe::sig::sineAt(static_cast<std::int64_t>(k), kHz, kFs);
        fcmp::probe::EngineRig rig(en, e, kFs);
        rig.process(in.data(), in.data(), yl.data(), yr.data(), in.size());
        const std::span<const float> out = std::span<const float>(yl).subspan(hold);
        double h1 = 0.0, hs = 0.0;
        for (int h = 1; h <= 8; ++h)
        {
            const fcmp::probe::measure::SingleBin bin(kHz * h, static_cast<double>(kFs), win);
            const double a = bin(out, static_cast<std::int64_t>(hold)).amplitude();
            (h == 1 ? h1 : hs) += h == 1 ? a : a * a;
        }
        return fcmp::probe::measure::dbFromAmplitude(std::sqrt(hs) / h1);
    }

    // ---- feedback (K2 #1)
    // -------------------------------------------------------------------------------------------- r^_fb at y: the
    // Mode's computer with the loop gain in place of the slope, through staticGr (topo FF).
    struct FbCurve
    {
        const ModeEntry* en;
        EngineParams ff;
        double operator()(double y) const
        {
            const auto yf = static_cast<float>(y);
            float r = 0.0f;
            en->staticGr(ff, &yf, &r, 1);
            return static_cast<double>(r);
        }
    };

    double fbRoot(const FbCurve& f, double x, double a, double b)
    {
        double lo = a, hi = a + b * f(x - a);
        for (int i = 0; i < tolns::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * f(x - mid) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    // The FB rows of one configuration whose topo is FB; the per-sample rows only when `perSample`.
    void fbRows(Probe& P, const ModeEntry& en, const RawParams& raw, const std::string& cfg, bool perSample)
    {
        const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
        FbCurve f{ &en, e };
        f.ff.topo = kTopoFF;
        f.ff.slope = stage::QuadKnee::loopGain(e.slope);
        const double t = static_cast<double>(e.thrDb);

        double staticErr = 0.0;
        for (double x = t - 20.0; x <= t + 40.0 + 1e-9; x += 0.25)
        {
            const auto xf = static_cast<float>(x);
            float r = 0.0f;
            en.staticGr(e, &xf, &r, 1);
            const double want = fbRoot(f, static_cast<double>(xf), 0.0, 1.0);
            staticErr = std::max(staticErr, std::fabs(static_cast<double>(r) - want));
        }
        P.le(cfg + ".static_curve.max_err_db", staticErr, tolns::kFbSolveDb);
        if (!perSample)
            return;
        const Resolution res = fcmp::probe::resolveRaw(en, raw);
        if (e.s2ThrDb < kS2Off)
        {
            std::printf("NOTE     %s: stage 2 shares the element here (the GR is the max of two loops); the per-sample "
                        "branch rows are the computer's alone: dsp.sharedelement holds the element's maps\n",
                        cfg.c_str());
            return;
        }
        if (en.desc->attackSpec(res.view, e).program || en.desc->releaseSpec(res.view, e).program)
        {
            std::printf("NOTE     %s: program-dependent ballistics; the per-sample branch rows are the policy's probe's "
                        "(file header)\n",
                        cfg.c_str());
            return;
        }

        // per sample, through the Rig: square steps (|x| constant, so the detector reads the level exactly)
        const double segs[][2] = { { -10.0, 0.1 }, { 20.0, 0.25 }, { 6.0, 0.35 }, { -20.0, 0.4 } };
        std::vector<float> in;
        for (const auto& s : segs)
        {
            const double level = static_cast<double>(analysis::inputThresholdDb(e)) + s[0];
            const auto a = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(level));
            const std::size_t n = static_cast<std::size_t>(s[1] * static_cast<double>(kFs));
            for (std::size_t k = 0; k < n; ++k)
                in.push_back((k / 24) % 2 == 0 ? a : -a);
        }
        fcmp::probe::EngineRig rig(en, e, kFs);
        std::vector<float> yl(in.size()), yr(in.size()), gr, det;
        rig.setTapping(true);
        for (std::size_t off = 0; off < in.size(); off += 4096)
        {
            const std::size_t m = std::min<std::size_t>(4096, in.size() - off);
            rig.process(in.data() + off, in.data() + off, yl.data() + off, yr.data() + off, m);
            const std::vector<float> g = rig.tap().lane(rig.tap().grDb, 0), d = rig.tap().lane(rig.tap().detDb, 0);
            gr.insert(gr.end(), g.begin(), g.end());
            det.insert(det.end(), d.begin(), d.end());
            rig.tap().clear();
        }
        const double cA = static_cast<double>(oneMinusAlpha(e.atkTauMs, kFs));
        const double cR = static_cast<double>(oneMinusAlpha(e.relTauMs, kFs));
        double errA = 0.0, errR = 0.0;
        std::int64_t held = 0;
        for (std::size_t k = 1; k < gr.size(); ++k)
        {
            const double r1 = gr[k - 1], r = gr[k], x = det[k];
            const bool attack = f(x - r1) > r1;
            const double c = attack ? cA : cR;
            const double want = fbRoot(f, x, (1.0 - c) * r1, c);
            if (!attack && r == r1 && want < r1 - 1e-6)
            {
                ++held;
                continue;
            }
            double& err = attack ? errA : errR;
            err = std::max(err, std::fabs(r - want));
        }
        std::printf("NOTE     %s: %lld held sample(s)\n", cfg.c_str(), static_cast<long long>(held));
        P.le(cfg + ".attack.max_err_db", errA, tolns::kFbSolveDb);
        P.le(cfg + ".release.max_err_db", errR, tolns::kFbSolveDb);
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
    // M5 (S10; for the lead's approval): a feedback configuration's slowest release skips the AUTO positions (kTagAuto,
    // kTagAuto2; Diode 609's A1 / A2, whose plain value is the slow constant, 01 §10.2). Their DualRelease fast path
    // (50 ms for A2) releases between the sine's peaks, and a loop that senses the compressed output re-attacks only
    // near them, so the GR averages ~2 dB under the curve at 6:1: the ripple C §5.2's slowest release is there to
    // avoid. Feed-forward AUTO positions (Bus G) hold the peak and keep their configuration (and blessed goldens).
    if (const ParamSpec* rs = view.spec[idx(Pid::rel)];
        rs != nullptr && rs->kind == Kind::stepped && (view[Pid::rel].tag & (kTagAuto | kTagAuto2)) != 0
        && fcmp::probe::resolveRaw(en, base).eng.topo == kTopoFB)
    {
        for (auto st = rs->steps.rbegin(); st != rs->steps.rend(); ++st)
            if ((st->tag & (kTagAuto | kTagAuto2)) == 0)
            {
                base[Pid::rel] = st->plain;
                break;
            }
        resolveView(desc, base, view);
    }

    const std::vector<float> ratios = choices(view, Pid::ratio,
                                              { 1.0f - 1.0f / 1.5f, 0.5f, 0.75f, 0.9f, 0.95f, 1.0f }, false);
    const std::vector<float> thrs = choices(view, Pid::thr, { -30.0f, -10.0f }, true);
    const std::vector<float> knees = choices(view, Pid::knee, { 0.0f, 6.0f, 12.0f }, false);
    std::printf("NOTE     %zu ratio(s) x %zu threshold(s) x %zu knee(s); attack %.9g ms, release %.9g ms\n",
                ratios.size(), thrs.size(), knees.size(), static_cast<double>(base[Pid::atk]),
                static_cast<double>(base[Pid::rel]));

    struct FbConfig
    {
        RawParams raw;
        std::string key;
        bool perSample;
    };
    std::vector<FbConfig> fbConfigs;                                // every FB configuration of the grid
    const auto noteFb = [&](const RawParams& raw, const std::string& key, bool perSample) {
        if (fcmp::probe::resolveRaw(en, raw).eng.topo == kTopoFB)
            fbConfigs.push_back({ raw, key, perSample });
    };

    for (const float ratio : ratios)
        for (const float thr : thrs)
            for (const float knee : knees)
            {
                RawParams raw = base;
                raw[Pid::ratio] = ratio;
                raw[Pid::thr] = thr;
                raw[Pid::knee] = knee;
                const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
                const double t = analysis::inputThresholdDb(e);
                CurveOpts o;
                // the reference curve, for inspection (4:1, T = -30, W = 6 when the Mode has it)
                o.print = std::fabs(e.slope - 0.75f) < 1e-6f && std::fabs(t + 30.0) < 1e-6
                       && std::fabs(std::max(0.0f, e.kneeDb) - 6.0f) < 1e-6f;
                curveRows(P, F, tol, en, raw, "static.", o);
                // the per-sample FB rows (about 0.7 s each) on every ratio at the first threshold with the widest
                // knee, plus the hard knee at the last ratio; the static FB curve on every configuration
                const bool perSample = thr == thrs.front()
                                    && (knee == knees.back() || (knee == knees.front() && ratio == ratios.back()));
                noteFb(raw, "static.fb.r" + ratioLabel(e.slope) + ".t" + fmtLabel(t) + ".k"
                                + fmtLabel(std::max(0.0f, e.kneeDb)), perSample);
            }

    // ---- every detector (F9) ----------------------------------------------------------------------------------------
    if (const ParamSpec* s = view.spec[idx(Pid::det)]; s != nullptr && s->kind == Kind::stepped && s->steps.size() > 1)
    {
        for (const Step& st : s->steps)
        {
            if (st.plain == view[Pid::det].plain)
                continue;                                       // the default detector: the grid above
            RawParams b = base;
            b[Pid::det] = st.plain;
            const std::string prefix = "static.det." + stepKey(st.label) + ".";
            curveRows(P, F, tol, en, withCurve(b, view, 0.75f, -30.0f, 6.0f), prefix, CurveOpts{});
            curveRows(P, F, tol, en, withCurve(b, view, 1.0f, -10.0f, 0.0f), prefix, CurveOpts{});
        }
    }
    else
        std::printf("NOTE     det is not a live step list in this Mode: no detector rows\n");

    // ---- every voice (F9): the declared curve with the colour stage's describing-function gain ----------------------
    if (const ParamSpec* s = view.spec[idx(Pid::voice)];
        s != nullptr && s->kind == Kind::stepped && s->steps.size() > 1)
    {
        const RawParams offCfgs[] = { withCurve(base, view, 0.75f, -30.0f, 6.0f),
                                      withCurve(base, view, 1.0f - 1.0f / 1.5f, -10.0f, 6.0f) };
        // The voices are judged at kVoiceFs (below); their THD golden at the probe's 48 kHz.
        std::vector<std::vector<double>> offTaps;
        CurveOpts measureOnly;
        measureOnly.emit = false;
        measureOnly.fs = kVoiceFs;
        for (const RawParams& raw : offCfgs)
            offTaps.push_back(curveRows(P, F, tol, en, raw, "static.voiceoff.", measureOnly));
        for (const Step& st : s->steps)
        {
            if (st.plain == view[Pid::voice].plain)
                continue;
            for (const float drive : { 0.0f, 12.0f })
            {
                const std::string prefix = "static.voice." + stepKey(st.label) + ".d" + fmtLabel(drive) + ".";
                std::int64_t differ = 0;
                for (std::size_t c = 0; c < std::size(offCfgs); ++c)
                {
                    RawParams raw = offCfgs[c];
                    raw[Pid::voice] = st.plain;
                    raw[Pid::drive] = drive;
                    CurveOpts o;
                    o.colour = true;
                    o.shapeRows = false;
                    o.fs = kVoiceFs;
                    const std::vector<double> taps = curveRows(P, F, tol, en, raw, prefix, o);
                    differ += taps == offTaps[c] ? 0 : 1;
                }
                RawParams voiced = base;
                voiced[Pid::voice] = st.plain;
                if (fcmp::probe::resolveRaw(en, voiced).eng.topo == fcmp::probe::resolveRaw(en, base).eng.topo)
                    P.eq(prefix + "gr_matches_off", differ, 0);
                else
                    std::printf("NOTE     %sgr_matches_off: this voice switches the topology, so its GR is its own "
                                "(%lld of %zu configuration(s) differ); not judged\n",
                                prefix.c_str(), static_cast<long long>(differ), std::size(offCfgs));

                RawParams clean = base;
                clean[Pid::voice] = st.plain;
                clean[Pid::drive] = drive;
                clean[Pid::thr] = choices(view, Pid::thr, { 0.0f }, true).front();
                const double thd = thdDb(en, fcmp::probe::resolveRaw(en, clean).eng);
                std::printf("NOTE     %sthd_db = %.4g (-10 dBFS, 1 kHz, no GR)\n", prefix.c_str(), thd);
                P.num(prefix + "thd_db", thd, Tol::abs(tolns::kThdGoldenAbsDb));
            }
        }
    }
    else
        std::printf("NOTE     voice is not a live step list in this Mode: no voice rows\n");

    // ---- time mode and hold (F9): the static curve does not move
    // -----------------------------------------------------
    if (const ParamSpec* s = view.spec[idx(Pid::tmode)];
        s != nullptr && s->kind == Kind::stepped && s->steps.size() > 1)
        for (const Step& st : s->steps)
        {
            if (st.plain == view[Pid::tmode].plain)
                continue;
            RawParams b = base;
            b[Pid::tmode] = st.plain;
            CurveOpts o;
            o.shapeRows = false;
            curveRows(P, F, tol, en, withCurve(b, view, 0.75f, -30.0f, 6.0f),
                      "static.tmode." + stepKey(st.label) + ".", o);
        }
    if (live(view.spec[idx(Pid::hold)]))
    {
        RawParams b = base;
        b[Pid::hold] = timeEdge(view, Pid::hold, false);
        CurveOpts o;
        o.shapeRows = false;
        curveRows(P, F, tol, en, withCurve(b, view, 0.75f, -30.0f, 6.0f), "static.hold.", o);
    }

    // ---- feedback configurations (K2 #1)
    // ------------------------------------------------------------------------------
    if (fbConfigs.empty())
        std::printf("NOTE     no configuration resolves to a feedback kernel (topo FF): no FB bisection rows\n");
    for (const FbConfig& fc : fbConfigs)
    {
        RawParams timed = fc.raw;
        const RawParams dflt = fcmp::probe::modeRaw(en);
        timed[Pid::atk] = dflt[Pid::atk];
        timed[Pid::rel] = dflt[Pid::rel];
        fbRows(P, en, timed, fc.key, fc.perSample);
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
