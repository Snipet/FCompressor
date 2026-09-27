// FCMP_PROBE layer=dsp name=opto3a scope=global timeout=180
//
// dsp.opto3a (v1.2, the Opto 3A Mode; D §2.2 the UREI LA-3A, M03): what sets Opto 3A apart from Opto 2A, whose cell
// it shares, measured through both Modes' engines (EngineRig, 48 kHz, the defaults: COMP, PEAK RED. 40) and their
// static curves. Spec rows only (the Mode's goldens are dsp.static, dsp.time, ... like every Mode's).
//
//   opto3a.attack.*    the attack (a 1 kHz square from T - 20 to T + 20, 63 % of the GR change, expDb) is "1.5 ms or
//                      less": in 0.75–3 ms (the kit's program band) and at most 0.2 x Opto 2A's (about 10.7 ms)
//   opto3a.memory.*    the release to 0.5 dB of GR after a burst at T + 20: "quick" after 0.1 s (<= 0.5 s), slower
//                      after 10 s of continuous GR (2–8 s), and at most 0.6 x Opto 2A's after the same 10 s (12.8 s)
//   opto3a.limit.*     COMP and LIMIT "virtually indistinguishable unless very heavy compression": the static GR
//                      within 1 dB at the threshold and at T + 10 (moderate GR), and LIMIT at least 1.5 dB deeper at
//                      T + 30 (about 20 dB of GR)
//   opto3a.hfsens.*    HF SENS at its end (10): the side chain at 100 Hz is the first-order shelf's own
//                      10 log10((f^2 + (G fc)^2) / (f^2 + fc^2)), G = 10^(-10 / 20), fc = 1 kHz: -9.63 dB (± 0.05),
//                      and within 0.3 dB of 0 at 10 kHz
//   opto3a.colour.*    CLASS A + TRANSFORMERS against Opto 2A's TUBE: H2 of a 0 dBFS sine 20 log10(0.04 / 0.1) =
//                      -7.96 dB lower (± 0.3), H3 the same (± 0.3)
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
    constexpr float kComp = 0.6667f, kLimit = 0.75f;

    const ModeEntry& three() { return fcmp::probe::modeEntry("opto-3a"); }
    const ModeEntry& two() { return fcmp::probe::modeEntry("opto-2a"); }

    EngineParams defaults(const ModeEntry& en, float ratio = kComp, float sce = 0.0f)
    {
        RawParams raw = fcmp::probe::modeRaw(en);
        raw[Pid::ratio] = ratio;
        raw[Pid::sce] = sce;
        return fcmp::probe::resolveRaw(en, raw).eng;
    }

    double attackSeconds(const ModeEntry& en)
    {
        const EngineParams e = defaults(en, en.desc == three().desc ? kComp : 0.6667f);
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.5 }, { t + 20.0, 0.5 } };
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1];
        return measure::lawSeconds(std::span<const float>(run.tapGrDb).subspan(a), run.tapGrDb[a - 1],
                                   run.tapGrDb.back(), kFs, TimeLaw::expDb);
    }

    // Seconds from the end of a `burstS` burst at T + 20 until the GR is <= 0.5 dB (-1: never within 30 s).
    double tailSeconds(const ModeEntry& en, double burstS)
    {
        const EngineParams e = defaults(en);
        const double t = analysis::inputThresholdDb(e);
        const Segment segs[] = { { t - 20.0, 0.2 }, { t + 20.0, burstS }, { t - 20.0, 30.0 } };
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        for (std::size_t i = run.edges[2]; i < run.tapGrDb.size(); ++i)
            if (run.tapGrDb[i] <= 0.5f)
                return static_cast<double>(i - run.edges[2]) / static_cast<double>(kFs);
        return -1.0;
    }

    void attackRows(Probe& P)
    {
        const double a3 = attackSeconds(three()), a2 = attackSeconds(two());
        std::printf("NOTE     opto3a.attack: %.3f ms (Opto 2A %.3f ms)\n", 1e3 * a3, 1e3 * a2);
        P.in("opto3a.attack.s", a3, 0.00075, 0.003);
        P.le("opto3a.attack.vs_2a", a2 > 0.0 ? a3 / a2 : 99.0, 0.2);
    }

    void memoryRows(Probe& P)
    {
        const double shortT = tailSeconds(three(), 0.1), longT = tailSeconds(three(), 10.0);
        const double long2 = tailSeconds(two(), 10.0);
        std::printf("NOTE     opto3a.memory: to 0.5 dB after 0.1 s %.3f s, after 10 s %.3f s (Opto 2A %.3f s)\n", shortT,
                    longT, long2);
        P.in("opto3a.memory.short_s", shortT, 0.0, 0.5);
        P.in("opto3a.memory.long_s", longT, 2.0, 8.0);
        P.le("opto3a.memory.vs_2a", long2 > 0.0 ? longT / long2 : 99.0, 0.6);
    }

    void limitRows(Probe& P)
    {
        const EngineParams comp = defaults(three(), kComp), lim = defaults(three(), kLimit);
        const std::array<float, 3> x { comp.thrDb, comp.thrDb + 10.0f, comp.thrDb + 30.0f };
        std::array<float, 3> gc{}, gl{};
        three().staticGr(comp, x.data(), gc.data(), 3);
        three().staticGr(lim, x.data(), gl.data(), 3);
        std::printf("NOTE     opto3a.limit: GR COMP / LIMIT at T %.3f / %.3f, T + 10 %.3f / %.3f, T + 30 %.3f / %.3f dB\n",
                    static_cast<double>(gc[0]), static_cast<double>(gl[0]), static_cast<double>(gc[1]),
                    static_cast<double>(gl[1]), static_cast<double>(gc[2]), static_cast<double>(gl[2]));
        P.le("opto3a.limit.at_thr_db", std::fabs(static_cast<double>(gl[0] - gc[0])), 1.0);
        P.le("opto3a.limit.moderate_db", std::fabs(static_cast<double>(gl[1] - gc[1])), 1.0);
        P.ge("opto3a.limit.heavy_db", static_cast<double>(gl[2] - gc[2]), 1.5);
    }

    void hfSensRows(Probe& P)
    {
        const EngineParams e = defaults(three(), kComp, 6.0f);        // HF SENS 10 (plain 6)
        const std::array<float, 2> hz { 100.0f, 10000.0f };
        std::array<float, 2> mag{};
        three().scShapeDb(e, kFs, hz.data(), mag.data(), 2);
        std::printf("NOTE     opto3a.hfsens: 100 Hz %.3f dB, 10 kHz %.3f dB\n", static_cast<double>(mag[0]),
                    static_cast<double>(mag[1]));
        const double g = std::pow(10.0, -10.0 / 20.0) * 1000.0, f = 100.0;
        const double want = 10.0 * std::log10((f * f + g * g) / (f * f + 1000.0 * 1000.0));
        P.near("opto3a.hfsens.low_db", static_cast<double>(mag[0]), want, 0.05);
        P.near("opto3a.hfsens.high_db", static_cast<double>(mag[1]), 0.0, 0.3);
    }

    void colourRows(Probe& P)
    {
        std::array<float, 8> h3{}, h2{};
        analysis::harmonicsDb(three(), defaults(three()), 0.0f, 1.0f, h3);
        analysis::harmonicsDb(two(), defaults(two()), 0.0f, 1.0f, h2);
        std::printf("NOTE     opto3a.colour at 0 dBFS: H2 %.2f dB, H3 %.2f dB (Opto 2A %.2f, %.2f dB)\n",
                    static_cast<double>(h3[1]), static_cast<double>(h3[2]), static_cast<double>(h2[1]),
                    static_cast<double>(h2[2]));
        P.near("opto3a.colour.h2_vs_2a_db", static_cast<double>(h3[1] - h2[1]), 20.0 * std::log10(0.04 / 0.1), 0.3);
        P.near("opto3a.colour.h3_vs_2a_db", static_cast<double>(h3[2] - h2[2]), 0.0, 0.3);
    }
}

FCMP_PROBE(dsp, opto3a)
{
    (void) C;
    attackRows(P);
    memoryRows(P);
    limitRows(P);
    hfSensRows(P);
    colourRows(P);
    return P.finish();
}
