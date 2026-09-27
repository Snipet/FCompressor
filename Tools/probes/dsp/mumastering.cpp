// FCMP_PROBE layer=dsp name=mumastering scope=global timeout=60
//
// dsp.mumastering (v1.2, the Mu Mastering Mode; D §2.3 the Manley Variable Mu, Mastering version, M06): the curve's two
// positions on the element's settled static curve (analysis::staticGr with stage 2: the 4:1 computer and LIMIT's 20:1
// rise sharing the element), INPUT's gain into the threshold and the tubes' distortion. Spec rows only.
//
//   mumastering.compress.*   COMPRESS is 1.5:1 above its knee (the local ratio at T + 25, ± 0.05) with no rise (stage
//                            2 out: s2ThrDb at kS2Off)
//   mumastering.limit.*      LIMIT is 4:1 under moderate GR (the local ratio where GR is 6 dB, ± 0.4), "at greater
//                            than 12 dB of limiting the ratio increases (up to 20:1)": the GR at which the local ratio
//                            passes 8:1 lies in 10–15 dB, and at 25 dB of GR the local ratio is at least 15:1
//   mumastering.input.*      INPUT +6 dB lowers the input threshold by 6 dB exactly (a gain into it)
//   mumastering.thd.*        a 0 VU (-18 dBFS) 1 kHz sine with no GR carries under 0.1 % THD (-60 dB; D §2.3 [V S12])
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
#include <cstdio>
#include <span>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace measure = fcmp::probe::measure;

    constexpr float kCompress = 1.0f - 1.0f / 1.5f, kLimit = 0.75f;

    const ModeEntry& mm() { return fcmp::probe::modeEntry("mu-mastering"); }

    EngineParams resolved(float ratio, float input = 0.0f)
    {
        RawParams r = fcmp::probe::modeRaw(mm());
        r[Pid::ratio] = ratio;
        r[Pid::drive] = input;
        return fcmp::probe::resolveRaw(mm(), r).eng;
    }

    // The element's settled GR at the detector level x (dB): the computer, then the rise.
    double elementGr(const EngineParams& e, double x)
    {
        const float xs[1] = { static_cast<float>(x) };
        float gr = 0.0f;
        analysis::staticGr(mm(), e, std::span<const float>(xs), std::span<float>(&gr, 1),
                           analysis::CurveOpts{ .colour = false, .stage2 = true });
        return static_cast<double>(gr);
    }

    // The local ratio dIn / dOut at x, a central difference over ± 0.05 dB.
    double localRatio(const EngineParams& e, double x)
    {
        const double h = 0.05;
        const double dOut = (x + h - elementGr(e, x + h)) - (x - h - elementGr(e, x - h));
        return dOut > 0.0 ? 2.0 * h / dOut : 1e9;
    }

    // The detector level (dB) at which the element's GR is `gr`, by bisection over [T - 20, T + 200].
    double levelForGr(const EngineParams& e, double gr)
    {
        double lo = static_cast<double>(e.thrDb) - 20.0, hi = static_cast<double>(e.thrDb) + 200.0;
        for (int i = 0; i < 100; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (elementGr(e, mid) < gr ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    void compressRows(Probe& P)
    {
        const EngineParams e = resolved(kCompress);
        const double r = localRatio(e, static_cast<double>(e.thrDb) + 25.0);
        std::printf("NOTE     mumastering.compress: local ratio at T + 25 %.4f\n", r);
        P.near("mumastering.compress.ratio", r, 1.5, 0.05);
        P.ge("mumastering.compress.no_rise", static_cast<double>(e.s2ThrDb), static_cast<double>(kS2Off));
    }

    void limitRows(Probe& P)
    {
        const EngineParams e = resolved(kLimit);
        const double r6 = localRatio(e, levelForGr(e, 6.0));
        double cross = -1.0;
        for (double g = 2.0; g <= 30.0; g += 0.05)
            if (localRatio(e, levelForGr(e, g)) >= 8.0)
            {
                cross = g;
                break;
            }
        const double r25 = localRatio(e, levelForGr(e, 25.0));
        std::printf("NOTE     mumastering.limit: local ratio at 6 dB GR %.3f, passes 8:1 at %.2f dB of GR, %.2f at 25 dB\n",
                    r6, cross, r25);
        P.near("mumastering.limit.moderate_ratio", r6, 4.0, 0.4);
        P.in("mumastering.limit.rise_gr_db", cross, 10.0, 15.0);
        P.ge("mumastering.limit.heavy_ratio", r25, 15.0);
    }

    void inputRows(Probe& P)
    {
        const EngineParams a = resolved(kLimit, 0.0f), b = resolved(kLimit, 6.0f);
        const double shift = static_cast<double>(analysis::inputThresholdDb(a) - analysis::inputThresholdDb(b));
        P.near("mumastering.input.threshold_shift_db", shift, 6.0, 1e-5);
    }

    void thdRows(Probe& P)
    {
        std::array<float, 8> h{};
        analysis::harmonicsDb(mm(), resolved(kCompress), 0.0f, static_cast<float>(measure::amplitudeFromDb(-18.0)), h);
        double thd = 0.0;
        for (std::size_t k = 1; k < h.size(); ++k)
            thd += std::pow(10.0, static_cast<double>(h[k]) / 10.0);
        const double thdDb = 10.0 * std::log10(thd > 0.0 ? thd : 1e-30);
        std::printf("NOTE     mumastering.thd at 0 VU, no GR: %.1f dB\n", thdDb);
        P.le("mumastering.thd.vu_db", thdDb, -60.0);
    }
}

FCMP_PROBE(dsp, mumastering)
{
    (void) C;
    compressRows(P);
    limitRows(P);
    inputRows(P);
    thdRows(P);
    return P.finish();
}
