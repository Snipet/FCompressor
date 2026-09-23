// FCMP_PROBE layer=dsp name=link scope=mode timeout=180
//
// dsp.link.<key> (F5, S4; D9: 03 §3.4 "dsp.link", C §5.8; E §8; 01 §5.2 "link in FB"; K2 #5b; Rig driver, K3 #10):
// the stereo link. Table-driven over the registry (one test per Modes.def slot): every Mode runs the same rows with its
// declared LinkLaw, its own `link` spec and its own topologies, so the descriptor wave's Modes are covered as they
// register (fidelity rows NOTE while a Mode is provisional, SPRINTS §7 D12).
//
// Part 1, the link policies (Mode-independent; stages/link/{LinkIndependent,LinkMax,LinkMean,LinkCvSum}.h), every run:
//   link.policy.<law>.k0.mismatches         k = 0 returns r bit for bit (all four lanes)
//   link.policy.<law>.k1.lane_mismatches    k = 1 gives two identical channel lanes (not for independent)
//   link.policy.<law>.swap.mismatches       swapped lanes 0-1 in give swapped lanes out, bit for bit (D9 symmetry)
//   link.policy.<law>.aux.mismatches        lanes 2-3 pass unchanged
//   link.policy.<law>.law_max_err_db        against the law in double, k in {0.25, 0.5, 0.75}: <= 1e-5 dB
//   link.policy.<law>.nonexpansive          violations of |L(a) - L(b)|max <= |a - b|max (K2 #5b: an FB lane stays a
//                                           contraction); link.policy.<law>.equal_max_err_db: equal lanes come back
//   link.policy.<law>.clamp.mismatches      k outside [0, 1] and NaN clamp (mean, cvSum; LinkMax is F9's)
//   link.kernel.<law>.<ff|fb>.k<k>.*        each policy in a probe-local engine (Clean's policies with this Link:
//                                           PeakLog, QuadKnee, SmoothBranching, kTopologies FF|FB) through the Rig, L
//                                           and R 1 kHz squares on different level schedules around T:
//                                             ff: tgt_max_err_db, the tapped linked target against law(r^(x_L),
//                                                 r^(x_R)) in double (01 §5.2: link on targets, E §8);
//                                             fb: step_max_err_db, per sample the tapped GR against law(root_L, root_R)
//                                                 in double, each root the 200-step bisection root of the branch E's
//                                                 predictor picks from the tapped previous GR (the committed, linked
//                                                 state) and the tapped detector level: the link applies AFTER the
//                                                 per-lane solve and BEFORE the commit (K2 #5b);
//                                             settled_ptp_db, the last 20 ms of the final constant segment (<= 1e-4)
//   link.kernel.<law>.fb.silent.k<k>.*      the FB steady state with L at T + 12 and R silent, a = the unlinked GR:
//                                           settled_ptp_db, bounds (0 <= GR_R <= GR_L <= a), lane_diff_db at k = 1;
//                                           LinkMax: l_db (GR_L = a) and r_db (GR_R in [k a, a]). In FB the linked
//                                           value is committed and re-linked every sample, so a partial link settles
//                                           close to a full one (max at 40 %: R = 99.9 % of a; the NOTE prints it)
// Part 2, the Mode (Rig, D9 of C §5.8): configurations = the Mode's defaults ("ff" or "fb" by its topology) plus the
// first `voice` step whose topology differs (e.g. Bus 25 NEW/OLD: "fb.voice1"). Link values: every step of a stepped
// `link`, {lo, mid, hi} of a continuous one, the resolved value otherwise. Stimulus: L = a 1 kHz square at the input
// threshold + 12 dB, R silent, settled (max(1 s, 10 tau_A, 2 tau_R), at most 20 s), measured over the last 100 ms; a =
// the L GR of the same run at link 0 (set on EngineParams, so a Mode without a link 0 has its reference too).
//   structural (spec, blocking):
//     link.<cfg>.k<k>.nonfinite, .gr_min_db, .settled_ptp_db (<= 0.01 dB: FB Modes converge with the link inside the
//         loop, K2 #5b), .bounds (0 <= GR_R <= GR_L: the silent side never gets more GR than the loud one)
//     link.<cfg>.swap.mismatches   the run with L and R swapped gives swapped outputs and GR lanes, bit for bit
//     link.<cfg>.mono.mismatches   L = R (a mono source, duplicated): identical output channels and GR lanes
//   fidelity (NOTE while provisional; tolerance by Rigor, 03 §3.7 `link`):
//     FF: link.<cfg>.k<k>.law_{l,r}_db   the settled GR against the declared law of (a, 0) at k
//     FB: link.<cfg>.k<k>.law_{l,r}_db   LinkMax: GR_L = a (the louder lane is unchanged) and GR_R in [k a, a]; the
//         other laws' FB fixed point depends on the ballistics (01 §5.2 "static curves use the per-lane solve"), so
//         they are held to the structural bounds and k = 1 only
//     link.<cfg>.k100.lane_diff_db       at k = 1 the two lanes' settled GR agree (C §5.8: <= linkDb) for every law
//         but independent
//   golden (abs:0.01; candidates while provisional): link.<cfg>.k<k>.gr_{l,r}_db
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkCvSum.h"
#include "fcdsp/engine/stages/link/LinkIndependent.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/link/LinkMean.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/DefineMode.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace fcdsp::modes
{
    extern const ModeDescriptor kClean;         // CleanDesc.cpp: the probe-local traits borrow its descriptor
}

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace st = fcdsp::stage;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;
    namespace tolns = fcmp::probe::tol;

    constexpr float kFs = 48000.0f;
    constexpr double kPolicyTolDb = 1e-5;       // float rounding of a GR <= 60 dB through one fused blend
    constexpr double kKernelTolDb = 2e-5;       // tolns::kFbSolveDb (the solver) plus the blend's rounding
    constexpr std::array<LinkLaw, 4> kLaws{ LinkLaw::independent, LinkLaw::max, LinkLaw::mean, LinkLaw::cvSum };

    const char* lawName(LinkLaw l) noexcept
    {
        switch (l)
        {
            case LinkLaw::independent: return "independent";
            case LinkLaw::max:         return "max";
            case LinkLaw::mean:        return "mean";
            case LinkLaw::cvSum:       return "cvsum";
        }
        return "?";
    }

    bool same(float a, float b) noexcept { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

    std::string kLabel(double k)                // k100, k50, k0, k33
    {
        return "k" + std::to_string(static_cast<int>(std::lround(100.0 * k)));
    }

    // ---- the laws ------------------------------------------------------------------------------------------------------

    simd::f32x4 applyLaw(LinkLaw l, simd::f32x4 r, float k) noexcept
    {
        switch (l)
        {
            case LinkLaw::independent: return st::LinkIndependent::apply(r, k);
            case LinkLaw::max:         return st::LinkMax::apply(r, k);
            case LinkLaw::mean:        return st::LinkMean::apply(r, k);
            case LinkLaw::cvSum:       return st::LinkCvSum::apply(r, k);
        }
        return r;
    }

    // The declared law in double: the linked value of a channel with GR `own` next to one with GR `other`.
    double lawRef(LinkLaw l, double own, double other, double k)
    {
        switch (l)
        {
            case LinkLaw::independent: return own;
            case LinkLaw::max:         return (1.0 - k) * own + k * std::max(own, other);
            case LinkLaw::mean:        return (1.0 - k) * own + k * 0.5 * (own + other);
            case LinkLaw::cvSum:
            {
                const double w = k / (1.0 + k);
                return (1.0 - w) * own + w * other;
            }
        }
        return own;
    }

    simd::f32x4 vec(float a, float b, float c, float d) noexcept
    {
        alignas(16) const float v[4] = { a, b, c, d };
        return simd::load(v);
    }

    simd::f32x4 swap01(simd::f32x4 r) noexcept
    {
        return vec(simd::lane<1>(r), simd::lane<0>(r), simd::lane<2>(r), simd::lane<3>(r));
    }

    // ---- part 1a: the policies as functions ----------------------------------------------------------------------------

    void policyRows(Probe& P, LinkLaw law)
    {
        const std::string k = std::string("link.policy.") + lawName(law);
        sig::Pcg32 rng(0x11c0u + static_cast<std::uint64_t>(law), 3);
        const auto gr = [&rng]() { return rng.bounded(8) == 0 ? 0.0f : 60.0f * rng.uniform(); };
        std::int64_t k0 = 0, k1 = 0, swp = 0, aux = 0, nonexp = 0, clampBad = 0;
        double lawErr = 0.0, equalErr = 0.0;
        const float ks[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (int i = 0; i < 20000; ++i)
        {
            const simd::f32x4 r = vec(gr(), gr(), gr(), gr());
            const simd::f32x4 z = applyLaw(law, r, 0.0f);
            for (int ln = 0; ln < 4; ++ln)
            {
                alignas(16) float a[4], b[4];
                simd::store(a, z);
                simd::store(b, r);
                k0 += same(a[ln], b[ln]) ? 0 : 1;
            }
            if (law != LinkLaw::independent)
            {
                const simd::f32x4 o = applyLaw(law, r, 1.0f);
                k1 += same(simd::lane<0>(o), simd::lane<1>(o)) ? 0 : 1;
            }
            for (const float kk : ks)
            {
                const simd::f32x4 o = applyLaw(law, r, kk), s = applyLaw(law, swap01(r), kk);
                swp += same(simd::lane<0>(s), simd::lane<1>(o)) && same(simd::lane<1>(s), simd::lane<0>(o)) ? 0 : 1;
                aux += same(simd::lane<2>(o), simd::lane<2>(r)) && same(simd::lane<3>(o), simd::lane<3>(r)) ? 0 : 1;
                if (kk > 0.0f && kk < 1.0f)
                {
                    const double r0 = simd::lane<0>(r), r1 = simd::lane<1>(r), kd = kk;
                    lawErr = std::max(lawErr, std::fabs(static_cast<double>(simd::lane<0>(o)) - lawRef(law, r0, r1, kd)));
                    lawErr = std::max(lawErr, std::fabs(static_cast<double>(simd::lane<1>(o)) - lawRef(law, r1, r0, kd)));
                }
                const simd::f32x4 q = vec(gr(), gr(), 0.0f, 0.0f);
                const simd::f32x4 oq = applyLaw(law, q, kk);
                const double in = std::max(std::fabs(static_cast<double>(simd::lane<0>(r)) - simd::lane<0>(q)),
                                           std::fabs(static_cast<double>(simd::lane<1>(r)) - simd::lane<1>(q)));
                const double out = std::max(std::fabs(static_cast<double>(simd::lane<0>(o)) - simd::lane<0>(oq)),
                                            std::fabs(static_cast<double>(simd::lane<1>(o)) - simd::lane<1>(oq)));
                nonexp += out <= in + kPolicyTolDb ? 0 : 1;
                const float e = simd::lane<0>(r);
                const simd::f32x4 eq = applyLaw(law, vec(e, e, 0.0f, 0.0f), kk);
                equalErr = std::max(equalErr, std::max(std::fabs(static_cast<double>(simd::lane<0>(eq)) - e),
                                                       std::fabs(static_cast<double>(simd::lane<1>(eq)) - e)));
            }
            if (law == LinkLaw::mean || law == LinkLaw::cvSum)
            {
                const auto eq4 = [](simd::f32x4 a, simd::f32x4 b) {
                    return same(simd::lane<0>(a), simd::lane<0>(b)) && same(simd::lane<1>(a), simd::lane<1>(b));
                };
                clampBad += eq4(applyLaw(law, r, 1.7f), applyLaw(law, r, 1.0f)) ? 0 : 1;
                clampBad += eq4(applyLaw(law, r, -0.3f), applyLaw(law, r, 0.0f)) ? 0 : 1;
                clampBad += eq4(applyLaw(law, r, std::numeric_limits<float>::quiet_NaN()), applyLaw(law, r, 0.0f)) ? 0
                                                                                                                   : 1;
            }
        }
        P.eq(k + ".k0.mismatches", k0, 0);
        if (law != LinkLaw::independent)
            P.eq(k + ".k1.lane_mismatches", k1, 0);
        P.eq(k + ".swap.mismatches", swp, 0);
        P.eq(k + ".aux.mismatches", aux, 0);
        P.le(k + ".law_max_err_db", lawErr, kPolicyTolDb);
        P.eq(k + ".nonexpansive", nonexp, 0);
        P.le(k + ".equal_max_err_db", equalErr, kPolicyTolDb);
        if (law == LinkLaw::mean || law == LinkLaw::cvSum)
            P.eq(k + ".clamp.mismatches", clampBad, 0);
    }

    // ---- part 1b: the policies inside an engine (probe-local traits) ---------------------------------------------------

    template <class L>
    struct KernelTraits
    {
        static constexpr const ModeDescriptor& desc = modes::kClean;
        using Detector   = st::PeakLog;
        using Computer   = st::QuadKnee;
        using Link       = L;
        using Ballistics = st::SmoothBranching;
        using Stage2     = st::NoStage2;
        using Colour     = st::ColourNone;
        using ScShape    = st::Flat;
        static constexpr uint8_t kTopologies = (1u << kTopoFF) | (1u << kTopoFB);
    };

    constexpr std::array<ModeEntry, 4> kKernelEntries{ makeModeEntry<KernelTraits<st::LinkIndependent>>(),
                                                       makeModeEntry<KernelTraits<st::LinkMax>>(),
                                                       makeModeEntry<KernelTraits<st::LinkMean>>(),
                                                       makeModeEntry<KernelTraits<st::LinkCvSum>>() };
    static_assert(sizeof(ModeEngine<KernelTraits<st::LinkCvSum>>) <= kArenaBytes);

    struct KneeLaw
    {
        double t = 0, w = 0, s = 0, kfb = 0;
    };

    double rhat(const KneeLaw& l, double x, double slope)      // QuadKnee's curve (E §2.2) with slope or loop gain
    {
        const double o = x - l.t;
        const double q = std::clamp(o + 0.5 * l.w, 0.0, l.w);
        return slope * (q * q / (2.0 * l.w) + std::max(0.0, o - 0.5 * l.w));
    }

    // The root of r = A + B r^_fb(x - r) by 200-step bisection (dsp.fbsolve's reference).
    double fbRoot(const KneeLaw& l, double x, double a, double b)
    {
        double lo = a, hi = a + b * rhat(l, x - a, l.kfb);
        for (int i = 0; i < tolns::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * rhat(l, x - mid, l.kfb) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    // SmoothBranching's FB step for one lane (E §2.6 predictor: attack if r^_fb(x - r1) > r1).
    double branchRoot(const KneeLaw& l, double x, double r1, double cA, double cR)
    {
        const double c = rhat(l, x - r1, l.kfb) > r1 ? cA : cR;
        return fbRoot(l, x, (1.0 - c) * r1, c);
    }

    // ---- the settled stereo run (the kernel's FB steady state and the Mode rows) ---------------------------------------

    struct Settled
    {
        double grL = 0, grR = 0, ptp = 0, grMin = 0;
        std::int64_t nonfinite = 0;
        std::vector<float> outL, outR, gr0, gr1;        // the whole run (swap / mono comparisons)
    };

    // L = a 1 kHz square at `levelDb` (dBFS) for `seconds`, R = `rScale` * L (0: silent; 1: mono); swapped puts the
    // square on R. The last 100 ms are measured.
    Settled settle(const ModeEntry& en, const EngineParams& e, double levelDb, double seconds, float rScale, bool swapped)
    {
        const std::size_t n = static_cast<std::size_t>(seconds * static_cast<double>(kFs));
        const std::size_t win = 4800;
        const auto amp = static_cast<float>(measure::amplitudeFromDb(levelDb));
        std::vector<float> a(n), b(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            a[i] = (i / 24) % 2 == 0 ? amp : -amp;
            b[i] = rScale * a[i];
        }
        fcmp::probe::EngineRig rig(en, e, kFs);
        Settled s;
        s.outL.resize(n);
        s.outR.resize(n);
        rig.setTapping(true);
        constexpr std::size_t kBlock = 8192;
        for (std::size_t off = 0; off < n; off += kBlock)
        {
            const std::size_t m = std::min(kBlock, n - off);
            if (swapped)
                rig.process(b.data() + off, a.data() + off, s.outL.data() + off, s.outR.data() + off, m);
            else
                rig.process(a.data() + off, b.data() + off, s.outL.data() + off, s.outR.data() + off, m);
            const std::vector<float> g0 = rig.tap().lane(rig.tap().grDb, 0), g1 = rig.tap().lane(rig.tap().grDb, 1);
            s.gr0.insert(s.gr0.end(), g0.begin(), g0.end());
            s.gr1.insert(s.gr1.end(), g1.begin(), g1.end());
            rig.tap().clear();
        }
        double sumL = 0.0, sumR = 0.0, lo0 = 1e9, hi0 = -1e9, lo1 = 1e9, hi1 = -1e9;
        for (std::size_t i = 0; i < n; ++i)
        {
            s.nonfinite += (std::isfinite(s.outL[i]) ? 0 : 1) + (std::isfinite(s.outR[i]) ? 0 : 1)
                         + (std::isfinite(s.gr0[i]) ? 0 : 1) + (std::isfinite(s.gr1[i]) ? 0 : 1);
            s.grMin = std::min(s.grMin, static_cast<double>(std::min(s.gr0[i], s.gr1[i])));
            if (i >= n - win)
            {
                sumL += s.gr0[i];
                sumR += s.gr1[i];
                lo0 = std::min(lo0, static_cast<double>(s.gr0[i]));
                hi0 = std::max(hi0, static_cast<double>(s.gr0[i]));
                lo1 = std::min(lo1, static_cast<double>(s.gr1[i]));
                hi1 = std::max(hi1, static_cast<double>(s.gr1[i]));
            }
        }
        s.grL = sumL / static_cast<double>(win);
        s.grR = sumR / static_cast<double>(win);
        s.ptp = std::max(hi0 - lo0, hi1 - lo1);
        return s;
    }

    std::int64_t bitsDiffer(const std::vector<float>& a, const std::vector<float>& b)
    {
        std::int64_t n = a.size() == b.size() ? 0 : 1;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            n += same(a[i], b[i]) ? 0 : 1;
        return n;
    }

    struct KernelRun
    {
        std::vector<float> gr0, gr1, det0, det1, tgt0, tgt1;
        std::int64_t nonfinite = 0;
    };

    // L and R: 1 kHz squares (|x| = A every sample) on two level schedules (dB relative to T), 7200 samples each.
    KernelRun kernelRun(const ModeEntry& entry, const EngineParams& e)
    {
        const double lDb[] = { -10.0, 15.0, 5.0, 15.0, -20.0, 8.0 }, rDb[] = { 8.0, -20.0, 12.0, 3.0, -10.0, 15.0 };
        constexpr std::size_t kSeg = 7200;
        const std::size_t n = kSeg * std::size(lDb);
        std::vector<float> inL(n), inR(n), outL(n), outR(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t s = i / kSeg;
            const float sign = (i / 24) % 2 == 0 ? 1.0f : -1.0f;
            inL[i] = sign * static_cast<float>(measure::amplitudeFromDb(static_cast<double>(e.thrDb) + lDb[s]));
            inR[i] = sign * static_cast<float>(measure::amplitudeFromDb(static_cast<double>(e.thrDb) + rDb[s]));
        }
        fcmp::probe::EngineRig rig(entry, e, kFs);
        rig.setTapping(true);
        rig.process(inL.data(), inR.data(), outL.data(), outR.data(), n);
        KernelRun r;
        const fcmp::probe::RigTap& t = rig.tap();
        r.gr0 = t.lane(t.grDb, 0);
        r.gr1 = t.lane(t.grDb, 1);
        r.det0 = t.lane(t.detDb, 0);
        r.det1 = t.lane(t.detDb, 1);
        r.tgt0 = t.lane(t.tgtDb, 0);
        r.tgt1 = t.lane(t.tgtDb, 1);
        for (std::size_t i = 0; i < n; ++i)
            r.nonfinite += (std::isfinite(outL[i]) ? 0 : 1) + (std::isfinite(outR[i]) ? 0 : 1);
        return r;
    }

    void kernelRows(Probe& P, LinkLaw law)
    {
        const ModeEntry& entry = kKernelEntries[static_cast<std::size_t>(law)];
        EngineParams e;
        e.preGainDb = 0.0f;
        e.thrDb = -20.0f;
        e.slope = 0.75f;
        e.kneeDb = 6.0f;
        e.rangeDb = kRangeOff;
        e.atkTauMs = 1.0f;
        e.relTauMs = 50.0f;
        e.mix = 1.0f;
        e.s2ThrDb = kS2Off;
        const KneeLaw kl{ -20.0, std::max(6.0, static_cast<double>(st::QuadKnee::kMinKneeDb)), 0.75,
                          static_cast<double>(st::QuadKnee::loopGain(0.75f)) };
        st::SmoothBranching::Coeffs bc{};
        st::SmoothBranching::design(bc, e, StageCtx{ kFs, kFs, 1, {} });
        const double cA = simd::lane<0>(bc.cA), cR = simd::lane<0>(bc.cR);

        for (const float k : { 0.4f, 1.0f })
            for (const std::uint8_t topo : { std::uint8_t{ kTopoFF }, std::uint8_t{ kTopoFB } })
            {
                e.link = k;
                e.topo = topo;
                const KernelRun run = kernelRun(entry, e);
                const std::string key = std::string("link.kernel.") + lawName(law) + (topo == kTopoFB ? ".fb." : ".ff.")
                                      + kLabel(k);
                const double kd = k;
                double err = 0.0;
                if (topo == kTopoFF)
                    for (std::size_t i = 0; i < run.gr0.size(); ++i)
                    {
                        const double a = rhat(kl, run.det0[i], kl.s), b = rhat(kl, run.det1[i], kl.s);
                        err = std::max(err, std::fabs(static_cast<double>(run.tgt0[i]) - lawRef(law, a, b, kd)));
                        err = std::max(err, std::fabs(static_cast<double>(run.tgt1[i]) - lawRef(law, b, a, kd)));
                    }
                else
                    for (std::size_t i = 1; i < run.gr0.size(); ++i)
                    {
                        const double a = branchRoot(kl, run.det0[i], run.gr0[i - 1], cA, cR);
                        const double b = branchRoot(kl, run.det1[i], run.gr1[i - 1], cA, cR);
                        err = std::max(err, std::fabs(static_cast<double>(run.gr0[i]) - lawRef(law, a, b, kd)));
                        err = std::max(err, std::fabs(static_cast<double>(run.gr1[i]) - lawRef(law, b, a, kd)));
                    }
                P.le(key + (topo == kTopoFF ? ".tgt_max_err_db" : ".step_max_err_db"), err, kKernelTolDb);
                double lo0 = 1e9, hi0 = -1e9, lo1 = 1e9, hi1 = -1e9;
                for (std::size_t i = run.gr0.size() - 960; i < run.gr0.size(); ++i)
                {
                    lo0 = std::min(lo0, static_cast<double>(run.gr0[i]));
                    hi0 = std::max(hi0, static_cast<double>(run.gr0[i]));
                    lo1 = std::min(lo1, static_cast<double>(run.gr1[i]));
                    hi1 = std::max(hi1, static_cast<double>(run.gr1[i]));
                }
                P.le(key + ".settled_ptp_db", std::max(hi0 - lo0, hi1 - lo1), 1e-4);
                P.eq(key + ".nonfinite", run.nonfinite, 0);
            }

        // The FB steady state with R silent (K2 #5b; the Mode rows' FB expectations on a known FB engine): L at T + 12,
        // a = the unlinked GR. Every law: the loop settles and 0 <= GR_R <= GR_L <= a; at k = 1 the lanes are equal.
        // LinkMax: the louder lane is unchanged (GR_L = a) and the silent one sits in [k a, a] (its release state is
        // re-linked every sample, so it settles near a: k a / (1 - (1 - k) alpha_R)).
        e.topo = kTopoFB;
        e.link = 0.0f;
        const double level = static_cast<double>(e.thrDb) + 12.0;
        const double a = settle(entry, e, level, 1.0, 0.0f, false).grL;
        for (const float k : { 0.4f, 1.0f })
        {
            e.link = k;
            const Settled s = settle(entry, e, level, 1.0, 0.0f, false);
            const std::string key = std::string("link.kernel.") + lawName(law) + ".fb.silent." + kLabel(k);
            const double kd = k;
            P.le(key + ".settled_ptp_db", s.ptp, 1e-4);
            P.eq(key + ".bounds", s.grR >= 0.0 && s.grR <= s.grL + 1e-6 && s.grL <= a + 1e-4 ? 1 : 0, 1);
            if (law == LinkLaw::max)
            {
                P.near(key + ".l_db", s.grL, a, 1e-4);
                P.in(key + ".r_db", s.grR, kd * a - 1e-4, a + 1e-4);
            }
            if (k == 1.0f && law != LinkLaw::independent)
                P.le(key + ".lane_diff_db", std::fabs(s.grL - s.grR), 1e-6);
            std::printf("NOTE     %s: unlinked %.4f dB -> GR L %.4f dB, R %.4f dB\n", key.c_str(), a, s.grL, s.grR);
        }
    }

    // ---- part 2: the Mode ------------------------------------------------------------------------------------------------

    struct Config
    {
        std::string name;
        RawParams raw;
    };

    // The Mode's defaults, plus the first `voice` step whose topology differs (the FF/FB switch of a Mode like Bus 25).
    std::vector<Config> configs(const ModeEntry& en)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        const Resolution r0 = fcmp::probe::resolveRaw(en, base);
        std::vector<Config> out{ { r0.eng.topo == kTopoFB ? "fb" : "ff", base } };
        const ParamSpec* voice = r0.view.spec[idx(Pid::voice)];
        if (voice != nullptr && voice->kind == Kind::stepped)
            for (std::size_t i = 0; i < voice->steps.size(); ++i)
            {
                RawParams raw = base;
                raw[Pid::voice] = voice->steps[i].plain;
                const std::uint8_t topo = fcmp::probe::resolveRaw(en, raw).eng.topo;
                if (topo != r0.eng.topo)
                {
                    out.push_back({ std::string(topo == kTopoFB ? "fb" : "ff") + ".voice" + std::to_string(i), raw });
                    break;
                }
            }
        return out;
    }

    // The link values to run: every step (stepped), {lo, mid, hi} (continuous, hybrid), or the resolved value.
    std::vector<float> linkValues(const ParamView& v)
    {
        const ParamSpec* s = v.spec[idx(Pid::link)];
        std::vector<float> out;
        if (s != nullptr && s->kind == Kind::stepped)
            for (const Step& st : s->steps)
                out.push_back(st.plain);
        else if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            out = { s->lo, 0.5f * (s->lo + s->hi), s->hi };
        else
            out.push_back(v[Pid::link].plain);
        return out;
    }

    void modeRows(Probe& P, const ModeEntry& en)
    {
        const ModeDescriptor& desc = *en.desc;
        const auto& tol = tolns::forRigor(desc.rigor);
        fcmp::probe::Fidelity F(P, desc.provisional);
        const LinkLaw law = desc.linkLaw;
        std::printf("NOTE     link: Mode %.*s declares LinkLaw %s\n", static_cast<int>(desc.key.size()), desc.key.data(),
                    lawName(law));

        for (const Config& cfg : configs(en))
        {
            const Resolution res = fcmp::probe::resolveRaw(en, cfg.raw);
            const EngineParams& e0 = res.eng;
            const bool fb = e0.topo == kTopoFB;
            const double level = static_cast<double>(analysis::inputThresholdDb(e0)) + 12.0;
            const double seconds = std::min(20.0, std::max({ 1.0, 0.01 * static_cast<double>(e0.atkTauMs),
                                                             0.002 * static_cast<double>(e0.relTauMs) }));
            const std::string c = "link." + cfg.name;

            EngineParams ref = e0;
            ref.link = 0.0f;
            const Settled s0 = settle(en, ref, level, seconds, 0.0f, false);
            const double a = s0.grL;
            std::printf("NOTE     %s: topology %s, square at %.2f dBFS (T + 12), %.1f s; unlinked GR %.4f dB\n",
                        c.c_str(), fb ? "FB" : "FF", level, seconds, a);
            if (a < 1.0)
                std::printf("NOTE     %s: the unlinked GR is below 1 dB; the law rows see little signal\n", c.c_str());

            std::vector<float> ks;                                  // resolved link values, one per key label
            for (const float k : linkValues(res.view))
            {
                RawParams raw = cfg.raw;
                raw[Pid::link] = k;
                const float resolved = fcmp::probe::resolveRaw(en, raw).eng.link;
                if (std::none_of(ks.begin(), ks.end(), [resolved](float v) { return kLabel(v) == kLabel(resolved); }))
                    ks.push_back(resolved);
            }
            for (const float k : ks)
            {
                RawParams raw = cfg.raw;
                raw[Pid::link] = k;
                const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
                const Settled s = settle(en, e, level, seconds, 0.0f, false);
                const std::string key = c + "." + kLabel(k);
                const double kd = k;
                std::printf("NOTE     %s: link %.4g -> GR L %.4f dB, R %.4f dB (settled p-p %.2g dB)\n", key.c_str(), kd,
                            s.grL, s.grR, s.ptp);
                P.eq(key + ".nonfinite", s.nonfinite, 0);
                P.ge(key + ".gr_min_db", s.grMin, 0.0);
                P.le(key + ".settled_ptp_db", s.ptp, 0.01);
                P.eq(key + ".bounds", s.grR >= 0.0 && s.grR <= s.grL + 1e-4 ? 1 : 0, 1);
                if (!fb)
                {
                    F.near(key + ".law_l_db", s.grL, lawRef(law, a, 0.0, kd), tol.linkDb);
                    F.near(key + ".law_r_db", s.grR, lawRef(law, 0.0, a, kd), tol.linkDb);
                }
                else if (law == LinkLaw::max)
                {
                    F.near(key + ".law_l_db", s.grL, a, tol.linkDb);
                    F.in(key + ".law_r_db", s.grR, kd * a - tol.linkDb, a + tol.linkDb);
                }
                if (kd == 1.0 && law != LinkLaw::independent)
                    F.le(key + ".lane_diff_db", std::fabs(s.grL - s.grR), tol.linkDb);
                P.num(key + ".gr_l_db", s.grL, Tol::abs(0.01));
                P.num(key + ".gr_r_db", s.grR, Tol::abs(0.01));
            }

            // D9 symmetry: swapped channels give swapped outputs; a mono source gives identical channels.
            {
                RawParams raw = cfg.raw;
                raw[Pid::link] = ks[ks.size() / 2];
                const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
                const double sec = std::min(seconds, 2.0);
                const Settled o = settle(en, e, level, sec, 0.0f, false);
                const Settled w = settle(en, e, level, sec, 0.0f, true);
                P.eq(c + ".swap.mismatches", bitsDiffer(o.outL, w.outR) + bitsDiffer(o.outR, w.outL)
                                                 + bitsDiffer(o.gr0, w.gr1) + bitsDiffer(o.gr1, w.gr0), 0);
                const Settled m = settle(en, e, level, sec, 1.0f, false);
                P.eq(c + ".mono.mismatches", bitsDiffer(m.outL, m.outR) + bitsDiffer(m.gr0, m.gr1), 0);
            }
        }
        F.summary();
    }
} // namespace

FCMP_PROBE(dsp, link)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    for (const LinkLaw law : kLaws)
    {
        policyRows(P, law);
        kernelRows(P, law);
    }
    modeRows(P, en);
    return P.finish();
}
