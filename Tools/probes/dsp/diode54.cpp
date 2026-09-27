// FCMP_PROBE layer=dsp name=diode54 scope=global timeout=180
//
// dsp.diode54 (v1.2, the Diode 54 Mode; D §2.5 the Neve 2254, M17): what the generic probes do not reach in Diode 54,
// through its own engine (EngineRig, 48 kHz, the defaults otherwise: 3:1, THRESHOLD +4 dBu) and its static curve.
// Spec rows only (the Mode's goldens are dsp.static, dsp.time, ... like every Mode's).
//
//   diode54.attack.*    ATTACK is a hybrid: dsp.time sweeps its FAST range (0.1–2 ms); here the fixed 5 MS step, a
//                       1 kHz square from T - 20 to T + 20 (63 % of the GR change, expDb, the closed loop, ADR-63), is
//                       5 ms ± 0.5 ms, and the FAST pot's end (0.1 ms) is at most 0.05 x that
//   diode54.auto.*      RECOVERY AUTO follows the program (DualRelease): after a 30 ms burst the GR falls to 1/e in
//                       0.1 s ± 0.02, after 3 s of program in 0.8–1.5 s; both inside AUTO's published 0.1–1.5 s
//   diode54.limit.*     the limiter reaches +20 dBu (its top step resolves to -2 dBFS, 2 dB steps) and holds the
//                       element: with the compressor at +10 dBu, 1.5:1 and the limiter at +12 dBu (-10 dBFS), the
//                       static GR of a -4 dBFS level is at least 3 dB deeper than the compressor's alone and leaves the
//                       output within 0.3 dB of -10 dBFS (> 100:1)
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
    constexpr float kAutoMs = 1500.0f;          // the AUTO step's plain value

    const ModeEntry& d54() { return fcmp::probe::modeEntry("diode-54"); }

    RawParams raw() { return fcmp::probe::modeRaw(d54()); }
    EngineParams resolved(const RawParams& r) { return fcmp::probe::resolveRaw(d54(), r).eng; }

    double attackSeconds(float atkMs)
    {
        RawParams r = raw();
        r[Pid::atk] = atkMs;
        const EngineParams e = resolved(r);
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.3 }, { t + 20.0, 0.3 } };
        fcmp::probe::EngineRig rig(d54(), e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1];
        return measure::lawSeconds(std::span<const float>(run.tapGrDb).subspan(a), run.tapGrDb[a - 1],
                                   run.tapGrDb.back(), kFs, TimeLaw::expDb);
    }

    // RECOVERY AUTO: the release to 1/e (expDb) after a `burstS` burst at T + 20.
    double autoRelease(double burstS)
    {
        RawParams r = raw();
        r[Pid::rel] = kAutoMs;
        const EngineParams e = resolved(r);
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.2 }, { t + 20.0, burstS }, { t - 20.0, 8.0 } };
        fcmp::probe::EngineRig rig(d54(), e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t b = run.edges[2];
        return measure::lawSeconds(std::span<const float>(run.tapGrDb).subspan(b), run.tapGrDb[b - 1], 0.0, kFs,
                                   TimeLaw::expDb);
    }

    void attackRows(Probe& P)
    {
        const double fixed = attackSeconds(5.0f), fast = attackSeconds(0.1f);
        std::printf("NOTE     diode54.attack: 5 MS %.4f ms, FAST 0.1 %.4f ms\n", 1e3 * fixed, 1e3 * fast);
        P.near("diode54.attack.fixed_s", fixed, 0.005, 0.0005);
        P.le("diode54.attack.fast_share", fixed > 0.0 ? fast / fixed : 99.0, 0.05);
    }

    void autoRows(Probe& P)
    {
        const double transient = autoRelease(0.03), sustained = autoRelease(3.0);
        std::printf("NOTE     diode54.auto: to 1/e after 30 ms %.4f s, after 3 s %.4f s\n", transient, sustained);
        P.near("diode54.auto.transient_s", transient, 0.1, 0.02);
        P.in("diode54.auto.sustained_s", sustained, 0.8, 1.5);
    }

    void limitRows(Probe& P)
    {
        RawParams r = raw();
        r[Pid::s2thr] = -2.0f;                                  // +20 dBu, the top step
        P.near("diode54.limit.top_dbfs", static_cast<double>(resolved(r).s2ThrDb), -2.0, 1e-6);

        r = raw();
        r[Pid::thr] = -12.0f;                                   // +10 dBu
        r[Pid::ratio] = 1.0f - 1.0f / 1.5f;
        const EngineParams off = resolved(r);
        r[Pid::s2thr] = -10.0f;                                 // +12 dBu
        const EngineParams on = resolved(r);
        const float x[1] = { -4.0f + on.preGainDb };
        float gOff = 0.0f, gOn = 0.0f;
        const analysis::CurveOpts withStage2{ .colour = false, .stage2 = true };   // the element: compressor, limiter
        analysis::staticGr(d54(), off, std::span<const float>(x), std::span<float>(&gOff, 1), withStage2);
        analysis::staticGr(d54(), on, std::span<const float>(x), std::span<float>(&gOn, 1), withStage2);
        const double out = -4.0 - static_cast<double>(gOn);
        std::printf("NOTE     diode54.limit: -4 dBFS: compressor alone %.3f dB, with the limiter at +12 dBu %.3f dB "
                    "(out %.3f dBFS)\n",
                    static_cast<double>(gOff), static_cast<double>(gOn), out);
        P.ge("diode54.limit.deeper_db", static_cast<double>(gOn - gOff), 3.0);
        P.near("diode54.limit.ceiling_dbfs", out, -10.0, 0.3);
    }
}

FCMP_PROBE(dsp, diode54)
{
    (void) C;
    attackRows(P);
    autoRows(P);
    limitRows(P);
    return P.finish();
}
