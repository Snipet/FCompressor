// FCMP_PROBE layer=dsp name=optotube1b scope=global timeout=120
//
// dsp.optotube1b (v1.2, the Opto Tube 1B Mode; D §2.2 the Tube-Tech CL 1B, M04): the ATTACK/RELEASE SELECT's three
// positions through the Mode's own engine (EngineRig, 48 kHz, a 1 kHz square: the optical cell reads its level at
// once, only its 5 ms hold and afterglow remain), the continuous ratio and THRESHOLD's OFF on the static curve. Spec
// rows only.
//
//   optotube1b.fixed.*    FIXED: the attack (a step from T - 20 to T + 20, 63 % of the GR change, expDb) 1 ms (0.8–1.5
//                         ms) and the release to 1/e 50 ms plus the cell's hold (45–70 ms), whatever the knobs say
//   optotube1b.fixman.*   FIX/MAN, DELAY 100 ms, RELEASE 1 s: after a 30 ms burst (shorter than DELAY) the release is the
//                         fixed fast one (<= 0.1 s); after 1 s (longer) the manual one holds the GR (0.7–1.8 s); the
//                         attack stays the fixed 1 ms (0.8–1.5 ms) with DELAY at 100 ms
//   optotube1b.ratio.*    RATIO is continuous 2 … 10:1: 20 dB over the threshold (above the knee) the static GR is
//                         S x 20 dB at both ends (10 and 18 dB, ± 0.01)
//   optotube1b.off.*      THRESHOLD OFF: no GR at 0 dBFS on the static curve
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
#include <cstdio>
#include <span>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace measure = fcmp::probe::measure;
    using fcmp::probe::Segment;

    constexpr float kFs = 48000.0f;
    constexpr float kManual = 0.0f, kFixed = 1.0f, kFixMan = 2.0f;

    const ModeEntry& ot() { return fcmp::probe::modeEntry("opto-tube-1b"); }

    EngineParams resolved(float sel, float atk, float rel)
    {
        RawParams r = fcmp::probe::modeRaw(ot());
        r[Pid::tmode] = sel;
        r[Pid::atk] = atk;
        r[Pid::rel] = rel;
        return fcmp::probe::resolveRaw(ot(), r).eng;
    }

    // A 1 kHz square: 0.3 s at T - 20, `burstS` at T + 20, then T - 20 for 4 s. The attack (to 1/e of the step, expDb)
    // and the release (from the burst's end to 1/e of its GR).
    struct Times { double attack = -1, release = -1; };
    Times timesOf(const EngineParams& e, double burstS)
    {
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.3 }, { t + 20.0, burstS }, { t - 20.0, 4.0 } };
        fcmp::probe::EngineRig rig(ot(), e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1], b = run.edges[2];
        const std::span<const float> tap(run.tapGrDb);
        Times out;
        out.attack = measure::lawSeconds(tap.subspan(a, b - a), run.tapGrDb[a - 1], run.tapGrDb[b - 1], kFs, TimeLaw::expDb);
        out.release = measure::lawSeconds(tap.subspan(b), run.tapGrDb[b - 1], 0.0, kFs, TimeLaw::expDb);
        return out;
    }

    void fixedRows(Probe& P)
    {
        const Times f = timesOf(resolved(kFixed, 200.0f, 3000.0f), 0.5);    // the knobs far away: FIXED ignores them
        std::printf("NOTE     optotube1b.fixed: attack %.3f ms, release %.2f ms\n", 1e3 * f.attack, 1e3 * f.release);
        P.in("optotube1b.fixed.attack_s", f.attack, 0.0008, 0.0015);
        P.in("optotube1b.fixed.release_s", f.release, 0.045, 0.070);
    }

    void fixManRows(Probe& P)
    {
        const EngineParams e = resolved(kFixMan, 100.0f, 1000.0f);         // DELAY 100 ms, manual RELEASE 1 s
        const Times shortPeak = timesOf(e, 0.03), longPeak = timesOf(e, 1.0);
        std::printf("NOTE     optotube1b.fixman: DELAY 100 ms, RELEASE 1 s: release after 30 ms %.1f ms, after 1 s %.1f ms; "
                    "attack %.3f ms\n", 1e3 * shortPeak.release, 1e3 * longPeak.release, 1e3 * longPeak.attack);
        P.le("optotube1b.fixman.short_release_s", shortPeak.release, 0.1);
        P.in("optotube1b.fixman.long_release_s", longPeak.release, 0.7, 1.8);
        P.in("optotube1b.fixman.attack_s", longPeak.attack, 0.0008, 0.0015);
        const Times manual = timesOf(resolved(kManual, 100.0f, 1000.0f), 0.03);
        std::printf("NOTE     optotube1b.manual: ATTACK 100 ms, RELEASE 1 s: release after 30 ms %.1f ms\n",
                    1e3 * manual.release);
    }

    void ratioRows(Probe& P)
    {
        for (const float s : { 0.5f, 0.9f })
        {
            RawParams r = fcmp::probe::modeRaw(ot());
            r[Pid::ratio] = s;
            const EngineParams e = fcmp::probe::resolveRaw(ot(), r).eng;
            const float x = e.thrDb + 20.0f;
            float gr = 0.0f;
            ot().staticGr(e, &x, &gr, 1);
            char k[64];
            std::snprintf(k, sizeof k, "optotube1b.ratio.s%02d_db", static_cast<int>(s * 10.0f + 0.5f));
            P.near(k, static_cast<double>(gr), 20.0 * static_cast<double>(s), 0.01);
        }
    }

    void offRows(Probe& P)
    {
        RawParams r = fcmp::probe::modeRaw(ot());
        r[Pid::thr] = 24.0f;                                               // the OFF step
        const EngineParams e = fcmp::probe::resolveRaw(ot(), r).eng;
        const float x = 0.0f + e.preGainDb;
        float gr = 0.0f;
        ot().staticGr(e, &x, &gr, 1);
        P.le("optotube1b.off.gr_at_0dbfs_db", static_cast<double>(gr), 1e-6);
    }
}

FCMP_PROBE(dsp, optotube1b)
{
    (void) C;
    fixedRows(P);
    fixManRows(P);
    ratioRows(P);
    offRows(P);
    return P.finish();
}
