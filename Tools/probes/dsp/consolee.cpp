// FCMP_PROBE layer=dsp name=consolee scope=global timeout=120
//
// dsp.consolee (v1.2, the Console E Mode; D §2.4 the SSL E/G channel dynamics, M10): what the generic probes do not
// reach in Console E, through its own engine (EngineRig, 48 kHz) and its static curve. Spec rows only (the Mode's
// goldens are dsp.static, dsp.time, ... like every Mode's). The ballistics rows use DETECT PEAK and RATIO inf with the
// HARD knee on a 1 kHz square (|x| constant: the detector reads the level at once and the target GR is the overshoot),
// so they see VcaChannel.h alone.
//
//   consolee.auto.*     ATTACK AUTO follows the program: a 15 dB overshoot reaches 1/e of its GR (expDb) in 3–8 ms, a
//                       3 dB one in 10–25 ms (VcaChannel.h: about 5.5 and 15.7 ms), the small one at least 2 x slower;
//                       FAST is its published 1 ms (± 0.05 ms)
//   consolee.lin.*      RELEASE LIN at 1 s falls at a constant 10 dB/s: the rate from 16 to 4 dB of GR is 10 ± 0.1 dB/s,
//                       the 10 → 4 dB leg takes the 16 → 10 dB leg's time (ratio 1 ± 0.02), and the release spec is
//                       that rate (rateDbPerS, 10 dB/s); LOG at 1 s is the exponential (the ratio ln 2.5 / ln 1.6 =
//                       1.9495 ± 0.02)
//   consolee.switch.*   LOG → LIN in the middle of a release: no GR step (the largest per-sample change within 50 ms of
//                       the switch <= 5e-4 dB, a 1 s release's own is ~3.4e-4) and LIN's 10 dB/s ± 0.2 from 0.1 s on
//   consolee.knee.*     OVEREASY is QuadKnee's W = 10 dB: S W / 8 = 0.9375 dB of GR at the threshold at 4:1 (± 1e-4),
//                       HARD ~0 (<= 1e-4), and both 7.5 dB at 10 dB over (± 1e-4)
//   consolee.automu.*   AUTO makeup is locked on and is the static curve's GR at 0 VU (-18 dBFS), in makeupDb (the
//                       engine's r^(0 dBFS) law is off: kEngAutoMakeup clear): 0.9375 dB at the defaults (T -18 dBFS,
//                       4:1, OVEREASY) and 7.5 dB at T -28, each within 1e-4 dB of the curve; and a steady 0 VU square
//                       leaves at the same level at T -18 and T -28 (± 0.01 dB): the output "remains constant"
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace measure = fcmp::probe::measure;
    using fcmp::probe::Segment;

    constexpr float kFs = 48000.0f;
    constexpr float kAutoMs = 10.0f, kFastMs = 1.0f;    // ATTACK's two positions (the AUTO step's nominal plain)
    constexpr float kLog = 0.0f, kLin = 1.0f;           // the release curve
    constexpr float kPeak = 1.0f;                       // DETECT PEAK
    constexpr float kHard = 0.0f, kEasy = 10.0f;        // KNEE

    const ModeEntry& ce() { return fcmp::probe::modeEntry("console-e"); }

    Resolution resolved(float atk, float relMs, float curve, float ratioS = 1.0f, float knee = kHard, float det = kPeak)
    {
        RawParams raw = fcmp::probe::modeRaw(ce());
        raw[Pid::atk] = atk;
        raw[Pid::rel] = relMs;
        raw[Pid::tmode] = curve;
        raw[Pid::ratio] = ratioS;
        raw[Pid::knee] = knee;
        raw[Pid::det] = det;
        return fcmp::probe::resolveRaw(ce(), raw);
    }

    // The attack to 1/e (expDb) of a square stepping from 20 dB under the threshold to `overDb` over it.
    double attackSeconds(const EngineParams& e, double overDb)
    {
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.3 }, { t + overDb, 0.3 } };
        fcmp::probe::EngineRig rig(ce(), e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1];
        const std::span<const float> trace = std::span<const float>(run.tapGrDb).subspan(a);
        return measure::lawSeconds(trace, run.tapGrDb[a - 1], run.tapGrDb.back(), kFs, TimeLaw::expDb);
    }

    // The first time (s, interpolated) the falling trace reaches `level`; -1 when it never does.
    double crossing(std::span<const float> tr, double level)
    {
        for (std::size_t i = 1; i < tr.size(); ++i)
            if (static_cast<double>(tr[i]) <= level)
            {
                const double a = static_cast<double>(tr[i - 1]), b = static_cast<double>(tr[i]);
                const double f = a > b ? (a - level) / (a - b) : 0.0;
                return (static_cast<double>(i - 1) + f) / static_cast<double>(kFs);
            }
        return -1.0;
    }

    // The release trace (tap GR, lane 0) after 0.5 s at 20 dB of GR, over 3 s.
    std::vector<float> releaseTrace(const EngineParams& e)
    {
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.2 }, { t + 20.0, 0.5 }, { t - 20.0, 3.0 } };
        fcmp::probe::EngineRig rig(ce(), e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        return std::vector<float>(run.tapGrDb.begin() + static_cast<std::ptrdiff_t>(run.edges[2]), run.tapGrDb.end());
    }

    void autoRows(Probe& P)
    {
        const EngineParams autoE = resolved(kAutoMs, 300.0f, kLog).eng, fastE = resolved(kFastMs, 300.0f, kLog).eng;
        const double big = attackSeconds(autoE, 15.0), small = attackSeconds(autoE, 3.0);
        const double fast = attackSeconds(fastE, 15.0);
        std::printf("NOTE     consolee.auto: 15 dB overshoot %.3f ms, 3 dB %.3f ms; FAST %.4f ms\n", 1e3 * big,
                    1e3 * small, 1e3 * fast);
        P.in("consolee.auto.big_s", big, 0.003, 0.008);
        P.in("consolee.auto.small_s", small, 0.010, 0.025);
        P.ge("consolee.auto.small_over_big", big > 0.0 ? small / big : 0.0, 2.0);
        P.near("consolee.fast.s", fast, 0.001, 0.00005);
    }

    void releaseRows(Probe& P)
    {
        const Resolution lin = resolved(kFastMs, 1000.0f, kLin), log = resolved(kFastMs, 1000.0f, kLog);
        const std::vector<float> l = releaseTrace(lin.eng), g = releaseTrace(log.eng);
        const double l16 = crossing(l, 16.0), l10 = crossing(l, 10.0), l4 = crossing(l, 4.0);
        const double g16 = crossing(g, 16.0), g10 = crossing(g, 10.0), g4 = crossing(g, 4.0);
        const double rate = l4 > l16 ? 12.0 / (l4 - l16) : 0.0;
        const double linShape = l10 > l16 ? (l4 - l10) / (l10 - l16) : 0.0;
        const double logShape = g10 > g16 ? (g4 - g10) / (g10 - g16) : 0.0;
        const TimeSpec spec = ce().desc->releaseSpec(lin.view, lin.eng);
        std::printf("NOTE     consolee.lin: RELEASE 1 s: %.4f dB/s, legs %.4f / %.4f s; LOG legs %.4f / %.4f s; spec "
                    "law %d value %.4g\n",
                    rate, l10 - l16, l4 - l10, g10 - g16, g4 - g10, static_cast<int>(spec.law),
                    static_cast<double>(spec.seconds));
        P.near("consolee.lin.rate_db_per_s", rate, 10.0, 0.1);
        P.near("consolee.lin.leg_ratio", linShape, 1.0, 0.02);
        P.eq("consolee.lin.spec_law", static_cast<int>(spec.law), static_cast<int>(TimeLaw::rateDbPerS));
        P.near("consolee.lin.spec_db_per_s", static_cast<double>(spec.seconds), 10.0, 1e-4);
        P.near("consolee.log.leg_ratio", logShape, std::log(2.5) / std::log(1.6), 0.02);
    }

    void switchRows(Probe& P)
    {
        const EngineParams log = resolved(kFastMs, 1000.0f, kLog).eng, lin = resolved(kFastMs, 1000.0f, kLin).eng;
        const double t = analysis::inputThresholdDb(log);
        fcmp::probe::EngineRig rig(ce(), log, kFs);
        const auto hi = static_cast<float>(measure::amplitudeFromDb(t + 20.0));
        const auto lo = static_cast<float>(measure::amplitudeFromDb(t - 20.0));
        const auto half = static_cast<std::size_t>(kFs / 2000.0f);
        const auto on = static_cast<std::size_t>(0.5f * kFs), pre = static_cast<std::size_t>(0.2f * kFs);
        const auto post = static_cast<std::size_t>(1.0f * kFs);
        std::vector<float> x(on + pre + post), y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = ((i / half) % 2 == 0 ? 1.0f : -1.0f) * (i < on ? hi : lo);
        rig.setTapping(true);
        rig.process(x.data(), x.data(), y.data(), y.data(), on + pre);
        rig.setParams(lin);
        rig.process(x.data() + on + pre, x.data() + on + pre, y.data(), y.data(), post);
        const std::vector<float> gr = rig.tap().lane(rig.tap().grDb, 0);
        const std::size_t sw = on + pre;
        double step = 0.0;
        for (std::size_t i = sw - 1; i < sw + static_cast<std::size_t>(0.05f * kFs); ++i)
            step = std::max(step, std::fabs(static_cast<double>(gr[i + 1] - gr[i])));
        const std::size_t a = sw + static_cast<std::size_t>(0.1f * kFs), b = sw + static_cast<std::size_t>(0.6f * kFs);
        const double rate = static_cast<double>(gr[a] - gr[b]) / 0.5;
        std::printf("NOTE     consolee.switch: GR %.3f dB at the switch; largest step %.3g dB; %.4f dB/s after\n",
                    static_cast<double>(gr[sw]), step, rate);
        P.le("consolee.switch.max_step_db", step, 5e-4);
        P.near("consolee.switch.lin_rate_db_per_s", rate, 10.0, 0.2);
    }

    void kneeRows(Probe& P)
    {
        const EngineParams easy = resolved(kAutoMs, 300.0f, kLog, 0.75f, kEasy).eng;
        const EngineParams hard = resolved(kAutoMs, 300.0f, kLog, 0.75f, kHard).eng;
        const std::array<float, 2> x { easy.thrDb, easy.thrDb + 10.0f };
        std::array<float, 2> ge{}, gh{};
        ce().staticGr(easy, x.data(), ge.data(), 2);
        ce().staticGr(hard, x.data(), gh.data(), 2);
        std::printf("NOTE     consolee.knee: at T %.5f / %.5f dB, T + 10 %.5f / %.5f dB (OVEREASY / HARD)\n",
                    static_cast<double>(ge[0]), static_cast<double>(gh[0]), static_cast<double>(ge[1]),
                    static_cast<double>(gh[1]));
        P.near("consolee.knee.easy_at_thr_db", static_cast<double>(ge[0]), 0.75 * 10.0 / 8.0, 1e-4);
        P.le("consolee.knee.hard_at_thr_db", static_cast<double>(gh[0]), 1e-4);
        P.near("consolee.knee.easy_over_db", static_cast<double>(ge[1]), 7.5, 1e-4);
        P.near("consolee.knee.hard_over_db", static_cast<double>(gh[1]), 7.5, 1e-4);
    }

    // The makeup the Mode applies (trim 0) and the static curve at 0 VU, at THRESHOLD `thr` (the defaults otherwise).
    struct Makeup { double applied = 0, curve = 0, outDb = 0; int flag = 0; };
    Makeup makeupAt(float thr)
    {
        RawParams raw = fcmp::probe::modeRaw(ce());
        raw[Pid::thr] = thr;
        const Resolution d = fcmp::probe::resolveRaw(ce(), raw);
        fcmp::probe::EngineRig rig(ce(), d.eng, kFs);
        const Segment segs[] = { { -18.0, 1.0 } };             // a 0 VU square: peak == RMS == -18 dBFS
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        Makeup m;
        m.applied = static_cast<double>(rig.makeupTotalDb() - raw[Pid::makeup]);
        const float x = -18.0f + d.eng.preGainDb;
        float curve = 0.0f;
        ce().staticGr(d.eng, &x, &curve, 1);
        m.curve = static_cast<double>(curve);
        m.outDb = measure::dbFromAmplitude(std::fabs(static_cast<double>(run.out.back())));
        m.flag = (d.eng.flags & kEngAutoMakeup) != 0 ? 1 : 0;
        return m;
    }

    void autoMakeupRows(Probe& P)
    {
        const Makeup d = makeupAt(-18.0f), deep = makeupAt(-28.0f);
        std::printf("NOTE     consolee.automu: T -18: %.5f dB (curve %.5f), out %.4f dBFS; T -28: %.5f dB (curve %.5f), "
                    "out %.4f dBFS\n",
                    d.applied, d.curve, d.outDb, deep.applied, deep.curve, deep.outDb);
        P.eq("consolee.automu.engine_flag", d.flag + deep.flag, 0);
        P.near("consolee.automu.default_db", d.applied, 0.9375, 1e-4);
        P.near("consolee.automu.default_curve_db", d.applied, d.curve, 1e-4);
        P.near("consolee.automu.t28_db", deep.applied, 7.5, 1e-4);
        P.near("consolee.automu.t28_curve_db", deep.applied, deep.curve, 1e-4);
        P.near("consolee.automu.constant_out_db", deep.outDb, d.outDb, 0.01);
    }
}

FCMP_PROBE(dsp, consolee)
{
    (void) C;
    autoRows(P);
    releaseRows(P);
    switchRows(P);
    kneeRows(P);
    autoMakeupRows(P);
    return P.finish();
}
