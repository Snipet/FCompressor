// FCMP_PROBE layer=dsp name=octo scope=global timeout=180
//
// dsp.octo (v1.2, the Octo Mode; D §2.7 the hybrid VCA, M18): what the generic probes do not reach in Octo, through its
// own engine (EngineRig, 48 kHz) and the analysis entry points. Spec rows only (the Mode's goldens are dsp.static,
// dsp.time, ... like every Mode's).
//
//   octo.opto.*       10:1 OPTO turns the release program-dependent (DualRelease: a fast release, then the slow
//                     path's tail; OctoDesc.cpp): after 3 s of a 1 kHz sine 12 dB over the threshold the GR falls to
//                     1/e (expDb law) inside the published range [lo, hi] (octoReleaseSpec); after a 30 ms burst it
//                     falls at most 0.5 x as long (.transient_share); at 4:1 the same pair differs by less than 25 %
//                     (.manual_share: the release there is not program-dependent)
//   octo.be.*         BAND EMPH: the Mode's SC shape (scShapeDb) is +10 dB (± 0.05) at 6 kHz and within 0.15 dB of 0
//                     at 100 Hz; 0 dB (within 1e-6) with the button off; and the running filter agrees: a steady 6 kHz
//                     sine 5 dB under the threshold gets at least 2 dB of GR with the button and none (<= 0.05 dB)
//                     without; a 200 Hz sine's GR moves by less than 0.1 dB
//   octo.audiohp.*    AUDIO HP: a third-order Butterworth high-pass, -3 dB at 65 Hz: the gain of a -40 dBFS sine
//                     (no GR) at 65 Hz is -3.01 ± 0.25 dB, at 32.5 Hz -18.13 ± 0.5 dB, at 1 kHz 0 ± 0.02 dB; CLEAN
//                     (no HP) at 65 Hz is 0 ± 0.02 dB
//   octo.harmonics.*  the AUDIO voices' harmonic balance (analysis::harmonicsDb, a -18 dBFS sine = 0 VU): DIST 2's H2
//                     exceeds its H3 by at least 3 dB and sits between -60 and -20 dB; DIST 3's H3 exceeds its H2 by at
//                     least 20 dB and sits between -50 and -25 dB; CLEAN's THD at a 0 dBFS sine stays under -60 dB
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"

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
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;

    constexpr float kFs = 48000.0f;
    constexpr float kOptoS = 0.9f;               // RATIO 10:1 OPTO (S = 1 - 1/10)
    constexpr float kFourS = 0.75f;              // RATIO 4:1

    const ModeEntry& octo() { return fcmp::probe::modeEntry("octo"); }

    Resolution resolved(float ratioS, float relMs, float voice = 0.0f, float be = 0.0f)
    {
        RawParams raw = fcmp::probe::modeRaw(octo());
        raw[Pid::ratio] = ratioS;
        raw[Pid::rel] = relMs;
        raw[Pid::atk] = 1.0f;
        raw[Pid::voice] = voice;
        raw[Pid::sce] = be;
        return fcmp::probe::resolveRaw(octo(), raw);
    }

    // The tapped GR (lane 0) after `onS` seconds of a 1 kHz sine at `amp`, over the following `offS` seconds of silence.
    std::vector<float> releaseTrace(const EngineParams& e, double amp, double onS, double offS)
    {
        fcmp::probe::EngineRig rig(octo(), e, kFs);
        const auto on = static_cast<std::size_t>(onS * kFs), off = static_cast<std::size_t>(offS * kFs);
        std::vector<float> x(on + off, 0.0f), y(on + off, 0.0f);
        for (std::size_t i = 0; i < on; ++i)
            x[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, amp);
        rig.setTapping(true);
        rig.process(x.data(), x.data(), y.data(), y.data(), x.size());
        std::vector<float> gr = rig.tap().lane(rig.tap().grDb, 0);
        return std::vector<float>(gr.begin() + static_cast<std::ptrdiff_t>(on), gr.end());
    }

    double releaseSeconds(const std::vector<float>& trace)
    {
        return trace.empty() ? -1.0 : measure::lawSeconds(trace, trace.front(), 0.0, kFs, TimeLaw::expDb);
    }

    void optoRows(Probe& P)
    {
        const double amp = measure::amplitudeFromDb(-18.0 + 12.0);   // 12 dB over the fixed threshold (INPUT 5)
        const Resolution opto = resolved(kOptoS, 2000.0f);
        const TimeSpec spec = octo().desc->releaseSpec(opto.view, opto.eng);
        const double sustained = releaseSeconds(releaseTrace(opto.eng, amp, 3.0, 12.0));
        const double transient = releaseSeconds(releaseTrace(opto.eng, amp, 0.03, 12.0));
        std::printf("NOTE     octo.opto: RELEASE 2 s: sustained %.3f s, transient %.3f s (spec %.3f ... %.3f s)\n",
                    sustained, transient, static_cast<double>(spec.lo), static_cast<double>(spec.hi));
        P.eq("octo.opto.program", spec.program ? 1 : 0, 1);
        P.in("octo.opto.sustained_release_s", sustained, static_cast<double>(spec.lo), static_cast<double>(spec.hi));
        P.le("octo.opto.transient_share", sustained > 0.0 ? transient / sustained : 99.0, 0.5);

        const Resolution four = resolved(kFourS, 2000.0f);
        const double s4 = releaseSeconds(releaseTrace(four.eng, amp, 3.0, 12.0));
        const double t4 = releaseSeconds(releaseTrace(four.eng, amp, 0.03, 12.0));
        std::printf("NOTE     octo.opto: 4:1, RELEASE 2 s: sustained %.3f s, transient %.3f s\n", s4, t4);
        P.in("octo.opto.manual_share", s4 > 0.0 ? t4 / s4 : 99.0, 0.8, 1.25);
    }

    // The settled GR (tap grDb, lane 0, the last sample of 1 s) of a steady sine at `hz`, amplitude `amp`.
    double settledGrDb(const EngineParams& e, double hz, double amp)
    {
        fcmp::probe::EngineRig rig(octo(), e, kFs);
        const auto n = static_cast<std::size_t>(1.0 * kFs);
        std::vector<float> x(n), y(n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = sig::sineAt(static_cast<std::int64_t>(i), hz, kFs, amp);
        rig.setTapping(true);
        rig.process(x.data(), x.data(), y.data(), y.data(), n);
        return static_cast<double>(rig.tap().lane(rig.tap().grDb, 0).back());
    }

    void emphasisRows(Probe& P)
    {
        const Resolution on = resolved(kFourS, 500.0f, 0.0f, 1.0f), off = resolved(kFourS, 500.0f, 0.0f, 0.0f);
        const std::array<float, 3> hz { 100.0f, 6000.0f, 1000.0f };
        std::array<float, 3> magOn{}, magOff{};
        octo().scShapeDb(on.eng, kFs, hz.data(), magOn.data(), static_cast<int>(hz.size()));
        octo().scShapeDb(off.eng, kFs, hz.data(), magOff.data(), static_cast<int>(hz.size()));
        std::printf("NOTE     octo.be: scShapeDb 100 Hz %.3f, 6 kHz %.3f, 1 kHz %.3f dB\n", static_cast<double>(magOn[0]),
                    static_cast<double>(magOn[1]), static_cast<double>(magOn[2]));
        P.near("octo.be.peak_db", static_cast<double>(magOn[1]), 10.0, 0.05);
        P.near("octo.be.low_db", static_cast<double>(magOn[0]), 0.0, 0.15);
        P.le("octo.be.off_db", std::max({ std::fabs(static_cast<double>(magOff[0])), std::fabs(static_cast<double>(magOff[1])),
                                          std::fabs(static_cast<double>(magOff[2])) }), 1e-6);

        // The running filter: a sine 5 dB below the threshold (the knee's foot at 4:1, W 10) compresses at 6 kHz only
        // with the button (the detector hears it 10 dB louder: the knee's top, 3.75 dB of GR on the static curve).
        const double amp = measure::amplitudeFromDb(-18.0 - 5.0);
        const double on6 = settledGrDb(on.eng, 6000.0, amp), off6 = settledGrDb(off.eng, 6000.0, amp);
        const double on2 = settledGrDb(on.eng, 200.0, amp), off2 = settledGrDb(off.eng, 200.0, amp);
        std::printf("NOTE     octo.be: GR 5 dB under the threshold: 6 kHz %.3f dB on / %.3f off; 200 Hz %.3f / %.3f\n",
                    on6, off6, on2, off2);
        P.ge("octo.be.running_6k_gr_db", on6, 2.0);
        P.le("octo.be.running_6k_off_gr_db", off6, 0.05);
        P.le("octo.be.running_200_diff_db", std::fabs(on2 - off2), 0.1);
    }

    // The gain (dB) at `hz` of a -40 dBFS sine through the engine, over the last `windowS` seconds of 1.5 s.
    double gainDb(const EngineParams& e, double hz, std::size_t window)
    {
        fcmp::probe::EngineRig rig(octo(), e, kFs);
        const auto n = static_cast<std::size_t>(1.5 * kFs);
        std::vector<float> x(n), y(n);
        const double amp = measure::amplitudeFromDb(-40.0);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = sig::sineAt(static_cast<std::int64_t>(i), hz, kFs, amp);
        rig.process(x.data(), x.data(), y.data(), y.data(), n);
        const measure::SingleBin bin(hz, kFs, window);
        const auto n0 = static_cast<std::int64_t>(n - window);
        return bin.gainDb(std::span<const float>(x).subspan(static_cast<std::size_t>(n0), window),
                          std::span<const float>(y).subspan(static_cast<std::size_t>(n0), window), n0)
             - static_cast<double>(rig.makeupTotalDb());
    }

    void audioHpRows(Probe& P)
    {
        const Resolution hp = resolved(kFourS, 500.0f, 1.0f), clean = resolved(kFourS, 500.0f, 0.0f);
        const double g65 = gainDb(hp.eng, 65.0, 9600), g32 = gainDb(hp.eng, 32.5, 19200), g1k = gainDb(hp.eng, 1000.0, 4800);
        const double c65 = gainDb(clean.eng, 65.0, 9600);
        std::printf("NOTE     octo.audiohp: 65 Hz %.3f dB, 32.5 Hz %.3f dB, 1 kHz %.4f dB; CLEAN 65 Hz %.4f dB\n", g65,
                    g32, g1k, c65);
        P.near("octo.audiohp.corner_db", g65, -3.01, 0.25);
        P.near("octo.audiohp.octave_below_db", g32, -18.13, 0.5);
        P.near("octo.audiohp.passband_db", g1k, 0.0, 0.02);
        P.near("octo.audiohp.clean_65_db", c65, 0.0, 0.02);
    }

    void harmonicRows(Probe& P)
    {
        const float vu = static_cast<float>(measure::amplitudeFromDb(-18.0));
        std::array<float, 8> d2{}, d3{}, cl{};
        analysis::harmonicsDb(octo(), resolved(kFourS, 500.0f, 2.0f).eng, 0.0f, vu, d2);
        analysis::harmonicsDb(octo(), resolved(kFourS, 500.0f, 4.0f).eng, 0.0f, vu, d3);
        analysis::harmonicsDb(octo(), resolved(kFourS, 500.0f, 0.0f).eng, 0.0f, 1.0f, cl);
        double thd = 0.0;
        for (std::size_t k = 1; k < cl.size(); ++k)
            thd += std::pow(10.0, static_cast<double>(cl[k]) / 10.0);
        const double cleanThd = 10.0 * std::log10(thd > 0.0 ? thd : 1e-30);
        std::printf("NOTE     octo.harmonics at 0 VU: DIST 2 H2 %.1f H3 %.1f dB; DIST 3 H2 %.1f H3 %.1f dB; CLEAN THD at "
                    "0 dBFS %.1f dB\n", static_cast<double>(d2[1]), static_cast<double>(d2[2]), static_cast<double>(d3[1]),
                    static_cast<double>(d3[2]), cleanThd);
        P.ge("octo.harmonics.dist2_h2_over_h3_db", static_cast<double>(d2[1] - d2[2]), 3.0);
        P.in("octo.harmonics.dist2_h2_db", static_cast<double>(d2[1]), -60.0, -20.0);
        P.ge("octo.harmonics.dist3_h3_over_h2_db", static_cast<double>(d3[2] - d3[1]), 20.0);
        P.in("octo.harmonics.dist3_h3_db", static_cast<double>(d3[2]), -50.0, -25.0);
        P.le("octo.harmonics.clean_thd_db", cleanThd, -60.0);
    }
}

FCMP_PROBE(dsp, octo)
{
    (void) C;
    optoRows(P);
    emphasisRows(P);
    audioHpRows(P);
    harmonicRows(P);
    return P.finish();
}
