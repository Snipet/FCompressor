// FCMP_PROBE layer=dsp name=bus25topo scope=global timeout=180
//
// dsp.bus25topo (M6, S11; SPRINTS S11.1; 01 §5.2-5.5, §10.7; D §2.4; E §2.6, §8; K2 #1, #5b, #22; ADR-63, ADR-67): Bus
// 25's defining behaviours, proven on the Mode-local policies (unit rows, 01 §8.4 step 3) and through the registered
// engine (EngineRig, 48 kHz) and fcdsp::EngineHost. Spec rows only (no golden: the Mode's goldens are dsp.static/time/...
// of `bus-25`). References are double precision on the policies' own float rates.
//
// TYPE (VOICE NEW = feed-forward, OLD = feedback) is a kernel key (01 §5.5): a flip is the host's 20 ms kernel crossfade.
//   bus25topo.flip.<cfg>.<new-old|old-new>.<q>.hf_ratio_db  the flip at t = 1 s at a waveform peak, against the steady
//                                       NEW and OLD renders: energy above 8 kHz within +-2 ms, <= +3 dB (C §5.0, the
//                                       zipper / switch metric), both channels, at ECO, STD and HQ; <cfg> = mono (a
//                                       110 Hz tone at -6 dBFS, L = R, the threshold 8 dB under its RMS: ~6 dB of GR,
//                                       the Mode's defaults otherwise: 4:1 MED, ATTACK 1 ms, RELEASE 0.5 s, LINK 100 %)
//                                       and stereo (L as mono, R = 170 Hz at -14 dBFS, LINK 70 %)
//     .pre_mismatches                   before the edge the flipped render equals the steady one, bit for bit
//     .lands_db                         from 1.8 s after the edge the tapped GR (lanes 0-1) is the steady target
//                                       render's within 0.05 dB: the incoming loop settles from its seeded state
//     .nonfinite                        0
// The CV-sum link in OLD (the ADR-67 fix, Bus25Ballistics.h), unit rows on bus25::CvSumBranching with QuadKnee's
// closed-form solve, against the coupled system (*) solved in double (bisection per lane, Jacobi to 1e-12 dB):
//   bus25topo.cvsum.unit.max_err_db     20,000 random cases (fs 44.1-192 kHz, the ratio / knee / threshold steps, levels
//                                       T - 20 ... T + 40 per lane, CVs 0 ... 30 dB, OLD's attack detents x (1 + k),
//                                       releases 50 ms ... 3 s, LINK 50 ... 100 %): the applied GR (the engine's
//                                       LinkCvSum of the solved CVs) against the reference: <= 1e-4 dB;
//                                       .cv_err_db the solved CVs likewise; .max_iters <= the cap and .capped (cases
//                                       that ran to it) printed; .nonfinite 0
//   bus25topo.cvsum.ind.mismatches      LINK IND: solveFb / commitFb equal stage::SmoothBranching's, bit for bit, over a
//                                       2 s trajectory (its FB sub-ulp carry included)
//   bus25topo.cvsum.ff.mismatches       NEW: tick equals SmoothBranching's, bit for bit
//   bus25topo.cvsum.mono_db             L = R at LINK 100 % over a 0.5 s trajectory: the GR equals the uncoupled root
//                                       (E's predictor on the previous GR) within 1e-5 dB; .mono.lane_mismatches: the
//                                       two lanes are identical, bit for bit
//   bus25topo.cvsum.settled.k<k>.{l,r}_err_db   through the engine (OLD, 4:1 MED): L a square at T + 12, R silent,
//                                       settled; the GR per channel against the fixed point of (*) (r_L = (1 - w) c,
//                                       r_R = w c, c = r^_fb(x - (1 - w) c)): <= 1e-3 dB, for LINK 50 ... 100 %;
//                                       .k50.r_over_l in [0.49, 0.51] (the node's w / (1 - w) = 1/2: a partial link
//                                       stays partial; FZ0's re-link read 99.9 % at 40 %, ADR-67)
// The detector, bus25::RmsCatch (Bus25Rms.h), unit rows at 48 kHz:
//   bus25topo.rms.sine1k.dev_db         a 1 kHz sine, window 10 ms (RELEASE 0.5 s): the settled level against its RMS,
//                                       max |dev| <= 0.05 dB (the window's 2 f0 ripple); .sine1k_slow_db at 2 s: <= 0.01
//   bus25topo.rms.catch.jump40_db       a square 40 dB up: one sample later the level is within 0.05 dB of the new one
//   bus25topo.rms.catch.jump12_short_db a square 12 dB up: one sample later still >= 5 dB short (the window integrates)
//   bus25topo.rms.catch.monotone        100,000 random (ms, p1 <= p2): the update is non-decreasing in v^2: 0 violations
//   bus25topo.rms.noise.catches         10 s of Gaussian noise: samples where the catch wins: 0; .noise.mean_db: the
//                                       mean square's mean against sigma^2 within 0.1 dB
//   bus25topo.rms.window.rel<ms>.rate_ratio  a square 40 dB down: the level's fall rate over its first 10 dB against
//                                       4.343 / (tau_R / 50) dB/s: 1 +- 0.01, at RELEASE 50 ms, 0.5 s, 2 s
//   bus25topo.rms.seed.max_err_db       seed(dB) -> levelDb over -200 ... +100 dB: <= 1e-3 dB
// Time constants (D2 steps T - 20 / T + 20 / T - 20 dB, 1 kHz squares, the law expDb, modelled tolerance 03 §3.7):
//   bus25topo.old.atk.d<i>.s            OLD at 4:1: every ATTACK detent, closed loop (ADR-63: physical() converts to the
//                                       open-loop tau x (1 + k)); .old.atk.inf.d3.s at inf:1 with the HARD knee (the
//                                       conversion's k = R - 1 is the loop gain above the knee; MED's output knee
//                                       moves the equilibrium and reads ~16 % slower at inf:1: printed, a known limit)
//   bus25topo.old.rel.d<i>.s            OLD: every RELEASE detent (the loop opens: not converted)
//   bus25topo.var.rel.<ms>.s            TIME MODE VAR (NEW): the continuous pot at 50, 500 and 3000 ms
//   bus25topo.var.spec_kinds            RELEASE is stepped under FIXED and continuous 50 ... 3000 ms under VAR: 1
// THRUST is the host tilt, never a second emphasis (S11 lead revision 4):
//   bus25topo.thrust.<step>.dboct_mismatch  EngineParams::sceDbOct is the step's 0 / 1.5 / 3.01 dB/oct exactly: 0
//   bus25topo.thrust.<step>.m_nonzero   physical() leaves m[] neutral (all zero): 0
//   bus25topo.thrust.<step>.mode_shape_max_db  the Mode's own SC shaping (scShapeDb, 20 Hz ... 20 kHz): 0 dB
//   bus25topo.thrust.loud.{250hz,2khz}_db  analysis::scResponse at LOUD against -/+ 3.01 dB/oct from 1 kHz: +-0.1 dB
//                                       (ADR-67's judged band)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkCvSum.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/modes/bus-25/Bus25.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace st = fcdsp::stage;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;
    namespace tolns = fcmp::probe::tol;
    using CvSum = fcdsp::modes::bus25::CvSumBranching;
    using Rms = fcdsp::modes::bus25::RmsCatch;

    constexpr float kFs = 48000.0f;
    constexpr int kBlock = 512;

    const ModeEntry& bus25() { return fcmp::probe::modeEntry("bus-25"); }

    float lane(simd::f32x4 v, int ln)
    {
        alignas(16) float t[4];
        simd::store(t, v);
        return t[ln & 3];
    }

    simd::f32x4 vec(float a, float b, float c, float d)
    {
        alignas(16) const float t[4] = { a, b, c, d };
        return simd::load(t);
    }

    // ---- the static FB curve and the coupled system (*) in double ----------------------------------------------------

    struct Knee
    {
        double t = 0, w = 0, k = 0;                                 // threshold, knee width (floored), loop gain
    };

    Knee kneeOf(float thrDb, float slope, float kneeDb)
    {
        Knee q;
        q.t = static_cast<double>(thrDb);
        q.w = std::max(static_cast<double>(st::QuadKnee::kMinKneeDb), static_cast<double>(kneeDb));
        q.k = static_cast<double>(st::QuadKnee::loopGain(slope));
        return q;
    }

    double rhatFb(const Knee& q, double y)                          // QuadKnee's FB curve at output level y (E §2.6)
    {
        const double o = y - q.t;
        const double c = std::clamp(o + 0.5 * q.w, 0.0, q.w);
        return q.k * (c * c / (2.0 * q.w) + std::max(0.0, o - 0.5 * q.w));
    }

    double rootFb(const Knee& q, double x, double a, double b)      // r = a + b r^_fb(x - r), bisection
    {
        double lo = a, hi = a + b * rhatFb(q, x - a);
        for (int i = 0; i < 200 && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * rhatFb(q, x - mid) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    struct Coupled
    {
        double cv[2] = { 0, 0 }, applied[2] = { 0, 0 };
    };

    // (*) of Bus25Ballistics.h: per lane c' = alpha c + (1 - alpha) r^_fb(x - r'), r' = (1 - w) c' + w c'_other, the
    // branch by E's predictor given the other lane's CV; Jacobi on the other lanes' CVs to 1e-12 dB.
    Coupled coupledStep(const Knee& q, const double x[2], const double c[2], double kA, double kR, double w)
    {
        double est[2] = { c[1], c[0] };
        Coupled out;
        for (int it = 0; it < 400; ++it)
        {
            double next[2];
            for (int i = 0; i < 2; ++i)
            {
                const double feed = w * est[i];
                const auto branch = [&](double kk) {
                    const double r = rootFb(q, x[i], (1.0 - w) * (1.0 - kk) * c[i] + feed, (1.0 - w) * kk);
                    return (r - feed) / (1.0 - w);
                };
                const double ca = branch(kA);
                next[i] = std::max(0.0, ca > c[i] ? ca : branch(kR));
            }
            const double moved = std::max(std::fabs(next[0] - out.cv[0]), std::fabs(next[1] - out.cv[1]));
            out.cv[0] = next[0];
            out.cv[1] = next[1];
            est[0] = next[1];
            est[1] = next[0];
            if (it > 0 && moved < 1e-12)
                break;
        }
        out.applied[0] = (1.0 - w) * out.cv[0] + w * out.cv[1];
        out.applied[1] = (1.0 - w) * out.cv[1] + w * out.cv[0];
        return out;
    }

    LevelCtl levelCtl(float thrDb, float slope, float kneeDb)
    {
        return LevelCtl{ simd::set1(thrDb), simd::set1(slope), simd::set1(kneeDb), simd::set1(kS2Off) };
    }

    // ---- the TYPE flip through the host --------------------------------------------------------------------------

    struct Signal
    {
        std::vector<float> l, r;
    };

    struct Run
    {
        std::vector<float> l, r;
        std::vector<simd::f32x4> gr;
    };

    BlockParams blockOf(const RawParams& raw)
    {
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(bus25()));
        bp.eng = fcmp::probe::resolveRaw(bus25(), raw).eng;
        return bp;
    }

    // A fresh host over the signal in blocks of kBlock, split at `edge`; `first` before it, `second` from it on.
    Run render(const BlockParams& first, const BlockParams& second, const Signal& s, std::size_t edge, Quality q)
    {
        HostConfig c;
        c.fs = kFs;
        c.maxBlock = kBlock;
        c.quality = q;
        auto host = std::make_unique<EngineHost>();
        host->configure(c, first);
        const std::size_t n = s.l.size();
        Run run;
        run.l.assign(n, 0.0f);
        run.r.assign(n, 0.0f);
        run.gr.assign(n, simd::set1(0.0f));
        TestTap tap;
        tap.grDb = run.gr;
        host->setTap(&tap);
        for (std::size_t off = 0; off < n;)
        {
            std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(kBlock), n - off);
            if (edge > off && edge < off + len)
                len = edge - off;
            const float* ins[2] = { s.l.data() + off, s.r.data() + off };
            float* outs[2] = { run.l.data() + off, run.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, off < edge ? first : second);
            off += len;
        }
        host->setTap(nullptr);
        return run;
    }

    const char* qualityName(Quality q) { return q == Quality::eco ? "eco" : q == Quality::std ? "std" : "hq"; }

    void flipRows(Probe& P)
    {
        const std::size_t n = static_cast<std::size_t>(3.0f * kFs);
        const auto edge = static_cast<std::size_t>(kFs + kFs / (4.0f * 110.0f));     // 1 s + a quarter period: a peak
        struct Cfg
        {
            const char* key;
            bool stereo;
            float link;
        };
        const Cfg cfgs[] = { { "mono", false, 1.0f }, { "stereo", true, 0.7f } };
        for (const Cfg& cfg : cfgs)
        {
            Signal s;
            s.l.resize(n);
            s.r.resize(n);
            const double ampL = measure::amplitudeFromDb(-6.0), ampR = measure::amplitudeFromDb(-14.0);
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto k = static_cast<std::int64_t>(i);
                s.l[i] = sig::sineAt(k, 110.0, kFs, ampL);
                s.r[i] = cfg.stereo ? sig::sineAt(k, 170.0, kFs, ampR) : s.l[i];
            }
            RawParams raw = fcmp::probe::modeRaw(bus25());
            raw[Pid::thr] = static_cast<float>(-6.0 - 3.0103 - 8.0);   // 8 dB under the L tone's RMS
            raw[Pid::link] = cfg.link;
            RawParams rawNew = raw, rawOld = raw;
            rawNew[Pid::voice] = 0.0f;
            rawOld[Pid::voice] = 1.0f;
            const BlockParams bNew = blockOf(rawNew), bOld = blockOf(rawOld);
            for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
            {
                const Run ctlNew = render(bNew, bNew, s, edge, q), ctlOld = render(bOld, bOld, s, edge, q);
                const struct
                {
                    const char* dir;
                    const BlockParams& a;
                    const BlockParams& b;
                    const Run& ctlA;
                    const Run& ctlB;
                } flips[] = { { "new-old", bNew, bOld, ctlNew, ctlOld }, { "old-new", bOld, bNew, ctlOld, ctlNew } };
                for (const auto& f : flips)
                {
                    const Run run = render(f.a, f.b, s, edge, q);
                    const std::string k = std::string("bus25topo.flip.") + cfg.key + "." + f.dir + "." + qualityName(q);
                    const double hfL = measure::hfRatioDb(run.l, f.ctlA.l, f.ctlB.l, edge, kFs);
                    const double hfR = measure::hfRatioDb(run.r, f.ctlA.r, f.ctlB.r, edge, kFs);
                    std::int64_t pre = 0, nonfinite = 0;
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        if (i < edge)
                            pre += run.l[i] == f.ctlA.l[i] && run.r[i] == f.ctlA.r[i] ? 0 : 1;
                        nonfinite += std::isfinite(run.l[i]) && std::isfinite(run.r[i]) ? 0 : 1;
                    }
                    double lands = 0.0;
                    for (std::size_t i = edge + static_cast<std::size_t>(1.8f * kFs); i < n; ++i)
                        for (const int ln : { 0, 1 })
                            lands = std::max(lands, static_cast<double>(std::fabs(lane(run.gr[i], ln)
                                                                                  - lane(f.ctlB.gr[i], ln))));
                    // the hard splice (outgoing up to the edge, incoming after), for inspection
                    std::vector<float> splice(f.ctlA.l);
                    std::copy(f.ctlB.l.begin() + static_cast<std::ptrdiff_t>(edge), f.ctlB.l.end(),
                              splice.begin() + static_cast<std::ptrdiff_t>(edge));
                    const double hard = measure::hfRatioDb(splice, f.ctlA.l, f.ctlB.l, edge, kFs);
                    std::printf("NOTE     %s: GR %.4g -> %.4g dB (L, steady), hf L %.3g dB R %.3g dB; a hard splice "
                                "reads %.3g dB\n",
                                k.c_str(), static_cast<double>(lane(f.ctlA.gr[edge], 0)),
                                static_cast<double>(lane(f.ctlB.gr[edge], 0)), hfL, hfR, hard);
                    P.le(k + ".hf_ratio_db", std::max(hfL, hfR), tolns::kClickHfRatioDb);
                    P.eq(k + ".pre_mismatches", pre, 0);
                    P.le(k + ".lands_db", lands, 0.05);
                    P.eq(k + ".nonfinite", nonfinite, 0);
                }
            }
        }
    }

    // ---- the CV-sum link in OLD (unit rows) ---------------------------------------------------------------------------

    void cvSumUnitRows(Probe& P)
    {
        const float ratios[] = { 1.0f - 1.0f / 1.5f, 0.5f, 1.0f - 1.0f / 3.0f, 0.75f, 1.0f - 1.0f / 6.0f, 0.9f, 1.0f };
        const float knees[] = { 0.0f, 6.0f, 12.0f };
        const float atks[] = { 0.03f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
        const float links[] = { 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f };
        const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
        sig::Pcg32 rng(0xb25c5a11, 7);
        double errR = 0.0, errC = 0.0;
        std::int64_t nonfinite = 0, capped = 0;
        int maxIters = 0;
        double sumIters = 0.0;
        constexpr int kCases = 20000;
        for (int n = 0; n < kCases; ++n)
        {
            EngineParams p;
            p.slope = ratios[rng.bounded(7)];
            p.kneeDb = knees[rng.bounded(3)];
            p.thrDb = -40.0f + 35.0f * rng.uniform();
            p.atkTauMs = atks[rng.bounded(7)] * (1.0f + st::QuadKnee::loopGain(p.slope));   // physical() under OLD
            p.relTauMs = 50.0f + 2950.0f * rng.uniform();
            p.link = links[rng.bounded(6)];
            p.topo = kTopoFB;
            const float fs = rates[rng.bounded(4)];
            CvSum::Coeffs c{};
            CvSum::design(c, p, StageCtx{ fs, fs, 1, {} });
            const float xl = p.thrDb - 20.0f + 60.0f * rng.uniform(), xr = p.thrDb - 20.0f + 60.0f * rng.uniform();
            const float cl = 30.0f * rng.uniform(), cr = 30.0f * rng.uniform();
            CvSum::State s{};
            CvSum::seed(s, vec(cl, cr, 0.0f, 0.0f));
            const simd::f32x4 x = vec(xl, xr, xl, xr);
            const LevelCtl l = levelCtl(p.thrDb, p.slope, p.kneeDb);
            st::QuadKnee::Coeffs gc{};
            const auto solve = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb(gc, x, l, a); };
            const simd::f32x4 cv = CvSum::solveFb(c, s, solve);
            const simd::f32x4 r = st::LinkCvSum::apply(cv, p.link);
            CvSum::commitFb(c, s, r);
            const Knee q = kneeOf(p.thrDb, p.slope, p.kneeDb);
            const double xd[2] = { xl, xr }, cd[2] = { cl, cr };
            const Coupled ref = coupledStep(q, xd, cd, static_cast<double>(lane(c.sb.cA, 0)),
                                            static_cast<double>(lane(c.sb.cR, 0)),
                                            static_cast<double>(st::LinkCvSum::weight(p.link)));
            for (const int ln : { 0, 1 })
            {
                const double gr = lane(r, ln), cvv = lane(CvSum::cvDb(s), ln);
                if (!std::isfinite(gr) || !std::isfinite(cvv))
                {
                    ++nonfinite;
                    continue;
                }
                errR = std::max(errR, std::fabs(gr - ref.applied[ln]));
                errC = std::max(errC, std::fabs(cvv - ref.cv[ln]));
            }
            maxIters = std::max(maxIters, s.iters);
            sumIters += s.iters;
            capped += s.iters >= CvSum::kMaxIters ? 1 : 0;
        }
        std::printf("NOTE     bus25topo.cvsum.unit: %d cases, applied max err %.3g dB, CV max err %.3g dB; iterations "
                    "mean %.3g, max %d, %lld at the cap %d\n",
                    kCases, errR, errC, sumIters / kCases, maxIters, static_cast<long long>(capped), CvSum::kMaxIters);
        P.le("bus25topo.cvsum.unit.max_err_db", errR, 1e-4);
        P.le("bus25topo.cvsum.unit.cv_err_db", errC, 1e-4);
        P.le("bus25topo.cvsum.unit.max_iters", maxIters, CvSum::kMaxIters);
        P.eq("bus25topo.cvsum.unit.nonfinite", nonfinite, 0);

        // LINK IND is SmoothBranching's FB step, and NEW its FF step, bit for bit (a 2 s trajectory each)
        {
            EngineParams p;
            p.slope = 0.75f;
            p.kneeDb = 6.0f;
            p.thrDb = -22.0f;
            p.atkTauMs = 4.0f;
            p.relTauMs = 3000.0f;                       // slow enough for the FB sub-ulp carry
            p.link = 0.0f;
            const StageCtx ctx{ kFs, kFs, 1, {} };
            CvSum::Coeffs c{};
            CvSum::design(c, p, ctx);
            st::SmoothBranching::Coeffs sc{};
            st::SmoothBranching::design(sc, p, ctx);
            CvSum::State s{}, sf{};
            st::SmoothBranching::State b{}, bf{};
            const LevelCtl l = levelCtl(p.thrDb, p.slope, p.kneeDb);
            st::QuadKnee::Coeffs gc{};
            std::int64_t fb = 0, ff = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(2.0f * kFs); ++i)
            {
                const float t = static_cast<float>(i) / kFs;
                const float lv = t < 0.5f ? -10.0f : (t < 1.0f ? -2.0f : -19.0f);
                const simd::f32x4 x = vec(lv, lv - 7.0f, lv, lv);
                const auto solve = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb(gc, x, l, a); };
                simd::f32x4 r1 = CvSum::solveFb(c, s, solve);
                r1 = st::LinkCvSum::apply(r1, p.link);
                CvSum::commitFb(c, s, r1);
                simd::f32x4 r2 = st::SmoothBranching::solveFb(sc, b, solve);
                r2 = st::LinkCvSum::apply(r2, p.link);
                st::SmoothBranching::commitFb(sc, b, r2);
                for (const int ln : { 0, 1, 2, 3 })
                    fb += lane(r1, ln) == lane(r2, ln) ? 0 : 1;
                const simd::f32x4 tgt = st::QuadKnee::target(gc, x, l);
                const simd::f32x4 f1 = CvSum::tick(c, sf, tgt), f2 = st::SmoothBranching::tick(sc, bf, tgt);
                for (const int ln : { 0, 1, 2, 3 })
                    ff += lane(f1, ln) == lane(f2, ln) ? 0 : 1;
            }
            P.eq("bus25topo.cvsum.ind.mismatches", fb, 0);
            P.eq("bus25topo.cvsum.ff.mismatches", ff, 0);
        }

        // L = R at LINK 100 %: identical lanes, the uncoupled root
        {
            EngineParams p;
            p.slope = 0.75f;
            p.kneeDb = 12.0f;
            p.thrDb = -30.0f;
            p.atkTauMs = 4.0f;
            p.relTauMs = 500.0f;
            p.link = 1.0f;
            CvSum::Coeffs c{};
            CvSum::design(c, p, StageCtx{ kFs, kFs, 1, {} });
            const LevelCtl l = levelCtl(p.thrDb, p.slope, p.kneeDb);
            const Knee q = kneeOf(p.thrDb, p.slope, p.kneeDb);
            st::QuadKnee::Coeffs gc{};
            CvSum::State s{};
            double dev = 0.0;
            std::int64_t differ = 0;
            double r0 = 0.0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(0.5f * kFs); ++i)
            {
                const float lv = i < 12000 ? -12.0f : -34.0f;
                const simd::f32x4 x = simd::set1(lv);
                const auto solve = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb(gc, x, l, a); };
                simd::f32x4 r = CvSum::solveFb(c, s, solve);
                r = st::LinkCvSum::apply(r, p.link);
                CvSum::commitFb(c, s, r);
                differ += lane(r, 0) == lane(r, 1) ? 0 : 1;
                const double kk = static_cast<double>(r0 < rhatFb(q, lv - r0) ? lane(c.sb.cA, 0) : lane(c.sb.cR, 0));
                const double want = rootFb(q, lv, (1.0 - kk) * r0, kk);
                dev = std::max(dev, std::fabs(static_cast<double>(lane(r, 0)) - want));
                r0 = lane(r, 0);
            }
            std::printf("NOTE     bus25topo.cvsum.mono: %lld lane differences, %.3g dB from the uncoupled root\n",
                        static_cast<long long>(differ), dev);
            P.eq("bus25topo.cvsum.mono.lane_mismatches", differ, 0);
            P.le("bus25topo.cvsum.mono_db", dev, 1e-5);
        }
    }

    // ---- the CV-sum link in OLD through the engine (settled law) ---------------------------------------------------------

    void cvSumSettledRows(Probe& P)
    {
        RawParams raw = fcmp::probe::modeRaw(bus25());
        raw[Pid::voice] = 1.0f;                                         // OLD
        raw[Pid::atk] = 0.3f;
        const float links[] = { 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f };
        for (const float k : links)
        {
            raw[Pid::link] = k;
            const EngineParams e = fcmp::probe::resolveRaw(bus25(), raw).eng;
            const double t = analysis::inputThresholdDb(e);
            const auto amp = static_cast<float>(measure::amplitudeFromDb(t + 12.0));
            fcmp::probe::EngineRig rig(bus25(), e, kFs);
            const std::size_t n = static_cast<std::size_t>(2.0f * kFs);
            std::vector<float> in(n), silent(n, 0.0f), yl(n), yr(n);
            for (std::size_t i = 0; i < n; ++i)
                in[i] = (i / 24) % 2 == 0 ? amp : -amp;
            rig.setTapping(true);
            rig.process(in.data(), silent.data(), yl.data(), yr.data(), n);
            const float grL = rig.tap().lane(rig.tap().grDb, 0).back(), grR = rig.tap().lane(rig.tap().grDb, 1).back();
            const Knee q = kneeOf(e.thrDb, e.slope, e.kneeDb);
            const double w = static_cast<double>(st::LinkCvSum::weight(k));
            const double x = t + 12.0 + static_cast<double>(e.preGainDb);
            double lo = 0.0, hi = 60.0;                                 // c = r^_fb(x - (1 - w) c), bisection
            for (int i = 0; i < 200; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                (mid - rhatFb(q, x - (1.0 - w) * mid) <= 0.0 ? lo : hi) = mid;
            }
            const double cv = 0.5 * (lo + hi);
            const std::string key = "bus25topo.cvsum.settled.k" + std::to_string(static_cast<int>(k * 100.0f + 0.5f));
            std::printf("NOTE     %s: GR L %.5g dB, R %.5g dB (R / L %.4g); the node's fixed point %.5g / %.5g dB\n",
                        key.c_str(), static_cast<double>(grL), static_cast<double>(grR),
                        grL > 0.0f ? static_cast<double>(grR / grL) : 0.0, (1.0 - w) * cv, w * cv);
            P.le(key + ".l_err_db", std::fabs(static_cast<double>(grL) - (1.0 - w) * cv), 1e-3);
            P.le(key + ".r_err_db", std::fabs(static_cast<double>(grR) - w * cv), 1e-3);
            if (k == 0.5f)
                P.in(key + ".r_over_l", grL > 0.0f ? static_cast<double>(grR / grL) : 0.0, 0.49, 0.51);
        }
    }

    // ---- the detector ---------------------------------------------------------------------------------------------------

    struct Det
    {
        Rms::Coeffs c{};
        Rms::State s{};
        explicit Det(float relTauMs)
        {
            EngineParams p;
            p.relTauMs = relTauMs;
            Rms::design(c, p, StageCtx{ kFs, kFs, 1, {} });
            Rms::seed(s, simd::set1(-240.0f));
        }
        float tick(float v) { return lane(Rms::tick(c, s, simd::set1(v)), 0); }
    };

    void detectorRows(Probe& P)
    {
        // a 1 kHz sine reads its RMS
        for (const float rel : { 500.0f, 2000.0f })
        {
            Det d(rel);
            const double amp = 0.3, rmsDb = measure::dbFromAmplitude(amp) - 3.0102999566398120;
            double dev = 0.0;
            for (std::int64_t i = 0; i < static_cast<std::int64_t>(0.6f * kFs); ++i)
            {
                const float lv = d.tick(sig::sineAt(i, 1000.0, kFs, amp));
                if (i >= static_cast<std::int64_t>(0.5f * kFs))
                    dev = std::max(dev, std::fabs(static_cast<double>(lv) - rmsDb));
            }
            if (rel == 500.0f)
                P.le("bus25topo.rms.sine1k.dev_db", dev, 0.05);
            else
                P.le("bus25topo.rms.sine1k_slow_db", dev, 0.01);
            std::printf("NOTE     bus25topo.rms: 1 kHz sine, window %.3g ms: %.3g dB from its RMS\n",
                        static_cast<double>(rel / 50.0f), dev);
        }
        // the jump catch
        {
            Det d(500.0f);
            const auto a = static_cast<float>(measure::amplitudeFromDb(-50.0));
            for (int i = 0; i < 24000; ++i)
                (void) d.tick(i % 2 == 0 ? a : -a);
            const float big = static_cast<float>(measure::amplitudeFromDb(-10.0));
            const float lv = d.tick(big);
            P.le("bus25topo.rms.catch.jump40_db", std::fabs(static_cast<double>(lv) + 10.0), 0.05);
            Det e(500.0f);
            for (int i = 0; i < 24000; ++i)
                (void) e.tick(i % 2 == 0 ? a : -a);
            const float mid = static_cast<float>(measure::amplitudeFromDb(-38.0));
            const float lv2 = e.tick(mid);
            P.ge("bus25topo.rms.catch.jump12_short_db", -38.0 - static_cast<double>(lv2), 5.0);
        }
        // non-decreasing in v^2
        {
            sig::Pcg32 rng(0x25c47c, 3);
            Det d(500.0f);
            std::int64_t bad = 0;
            for (int i = 0; i < 100000; ++i)
            {
                const float ms = std::pow(10.0f, -12.0f + 12.0f * rng.uniform());
                const float v1 = std::pow(10.0f, -7.0f + 7.0f * rng.uniform()), v2 = v1 * (1.0f + rng.uniform());
                Rms::State s1{ simd::set1(ms) }, s2{ simd::set1(ms) };
                Rms::accumulate(d.c, s1, simd::set1(v1));
                Rms::accumulate(d.c, s2, simd::set1(v2));
                bad += lane(s1.ms, 0) <= lane(s2.ms, 0) ? 0 : 1;
            }
            P.eq("bus25topo.rms.catch.monotone", bad, 0);
        }
        // Gaussian noise never meets the catch, and reads its mean square
        {
            Det d(500.0f);
            sig::Pcg32 rng(0x6a055, 5);
            const double sigma = 0.05;
            std::int64_t catches = 0;
            double sum = 0.0;
            std::int64_t count = 0;
            const auto total = static_cast<std::int64_t>(10.0f * kFs);
            for (std::int64_t i = 0; i < total; ++i)
            {
                const double u1 = std::max(1e-300, rng.uniformDouble()), u2 = rng.uniformDouble();
                const auto v = static_cast<float>(sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2));
                // the two branches of RmsCatch::accumulate, as it computes them (the catch wins where jump > avg)
                const simd::f32x4 ms = d.s.ms, pw = simd::set1(v * v);
                const simd::f32x4 avg = simd::fma(ms, d.c.c, simd::sub(pw, ms));
                const simd::f32x4 jump = simd::fms(pw, simd::set1(Rms::kCatchRatio), ms);
                (void) d.tick(v);
                if (i >= static_cast<std::int64_t>(0.1f * kFs))               // after the cold start's first charge
                    catches += lane(jump, 0) > lane(avg, 0) ? 1 : 0;
                if (i >= total / 2)
                {
                    sum += static_cast<double>(lane(d.s.ms, 0));
                    ++count;
                }
            }
            const double meanDb = 10.0 * std::log10(sum / static_cast<double>(count) / (sigma * sigma));
            std::printf("NOTE     bus25topo.rms.noise: %lld catch(es), mean square %.3g dB from sigma^2\n",
                        static_cast<long long>(catches), meanDb);
            P.eq("bus25topo.rms.noise.catches", catches, 0);
            P.le("bus25topo.rms.noise.mean_db", std::fabs(meanDb), 0.1);
        }
        // the window follows the release: a 40 dB drop falls at 4.343 / tau_w dB/s
        for (const float rel : { 50.0f, 500.0f, 2000.0f })
        {
            Det d(rel);
            const auto hi = static_cast<float>(measure::amplitudeFromDb(-10.0));
            const auto lo = static_cast<float>(measure::amplitudeFromDb(-50.0));
            for (int i = 0; i < 48000; ++i)
                (void) d.tick(i % 2 == 0 ? hi : -hi);
            std::size_t fell = 0;
            float lv = -10.0f;
            for (std::size_t i = 0; lv > -20.0f && i < 480000; ++i, ++fell)
                lv = d.tick(i % 2 == 0 ? lo : -lo);
            const double rate = 10.0 / (static_cast<double>(fell) / static_cast<double>(kFs));
            EngineParams p;
            p.relTauMs = rel;
            const double want = 10.0 / std::log(10.0) / (static_cast<double>(Rms::windowMs(p)) / 1000.0);
            const std::string key = "bus25topo.rms.window.rel" + std::to_string(static_cast<int>(rel)) + ".rate_ratio";
            std::printf("NOTE     %s: %.4g dB/s against %.4g\n", key.c_str(), rate, want);
            P.near(key, rate / want, 1.0, 0.01);
        }
        // seed / levelDb round trip
        {
            double err = 0.0;
            for (double db = -200.0; db <= 100.0; db += 0.37)
            {
                Rms::State s{};
                Rms::seed(s, simd::set1(static_cast<float>(db)));
                err = std::max(err, std::fabs(static_cast<double>(lane(Rms::levelDb(s), 0)) - db));
            }
            P.le("bus25topo.rms.seed.max_err_db", err, 1e-3);
        }
    }

    // ---- time constants: OLD (ADR-63) and VAR ------------------------------------------------------------------------

    double d2Seconds(const EngineParams& e, bool attack, const TimeSpec& spec)
    {
        const double t = analysis::inputThresholdDb(e);
        const double tauA = static_cast<double>(e.atkTauMs) / 1000.0, tauR = static_cast<double>(e.relTauMs) / 1000.0;
        const fcmp::probe::Segment segs[] = { { t - 20.0, 0.5 }, { t + 20.0, std::max(0.5, 10.0 * tauA) },
                                              { t - 20.0, std::max(1.0, 10.0 * tauR) } };
        fcmp::probe::EngineRig rig(bus25(), e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1], b = run.edges[2];
        const std::span<const float> tap(run.tapGrDb);
        const std::span<const float> trace = attack ? tap.subspan(a, b - a) : tap.subspan(b);
        const double from = run.tapGrDb[(attack ? a : b) - 1];
        const double to = attack ? static_cast<double>(run.tapGrDb[b - 1]) : 0.0;
        return measure::lawSeconds(trace, from, to, kFs, spec.law);
    }

    void timeRows(Probe& P)
    {
        const ModeDescriptor& desc = *bus25().desc;
        const auto& tol = tolns::forRigor(desc.rigor);
        const RawParams dflt = fcmp::probe::modeRaw(bus25());
        ParamView view;
        resolveView(desc, dflt, view);
        const auto judge = [&](const std::string& key, const RawParams& raw, bool attack) {
            const Resolution res = fcmp::probe::resolveRaw(bus25(), raw);
            const TimeSpec spec = attack ? desc.attackSpec(res.view, res.eng) : desc.releaseSpec(res.view, res.eng);
            const double got = d2Seconds(res.eng, attack, spec);
            std::printf("NOTE     %s: published %.6g s, measured %.6g s (open-loop tau %.6g ms)\n", key.c_str(),
                        static_cast<double>(spec.seconds), got,
                        static_cast<double>(attack ? res.eng.atkTauMs : res.eng.relTauMs));
            P.near(key, got, static_cast<double>(spec.seconds),
                   tolns::tauToleranceSeconds(tol, static_cast<double>(spec.seconds), kFs));
        };
        RawParams old = dflt;
        old[Pid::voice] = 1.0f;
        const ParamSpec* atk = view.spec[idx(Pid::atk)];
        const ParamSpec* rel = view.spec[idx(Pid::rel)];
        for (std::size_t i = 0; atk != nullptr && i < atk->steps.size(); ++i)
        {
            RawParams r = old;
            r[Pid::atk] = atk->steps[i].plain;
            judge("bus25topo.old.atk.d" + std::to_string(i) + ".s", r, true);
        }
        {
            RawParams r = old;
            r[Pid::atk] = 1.0f;
            r[Pid::ratio] = 1.0f;
            r[Pid::knee] = 0.0f;                        // HARD: the step's 63 % lies above the knee, where k is R - 1
            judge("bus25topo.old.atk.inf.d3.s", r, true);
            // MED: the output knee moves the loop's equilibrium (21.4 dB, not 19.8), so the 63 % point is later (NOTE)
            r[Pid::knee] = 6.0f;
            const Resolution res = fcmp::probe::resolveRaw(bus25(), r);
            std::printf("NOTE     bus25topo.old.atk.inf.med.d3: measured %.6g s against the published 0.001 s (the "
                        "conversion's 1 + k holds above the knee)\n",
                        d2Seconds(res.eng, true, desc.attackSpec(res.view, res.eng)));
        }
        for (std::size_t i = 0; rel != nullptr && i < rel->steps.size(); ++i)
        {
            RawParams r = old;
            r[Pid::rel] = rel->steps[i].plain;
            judge("bus25topo.old.rel.d" + std::to_string(i) + ".s", r, false);
        }
        // TIME MODE VAR: the continuous pot
        RawParams var = dflt;
        var[Pid::tmode] = 1.0f;
        ParamView vv;
        resolveView(desc, var, vv);
        const ParamSpec* vr = vv.spec[idx(Pid::rel)];
        const bool kinds = rel != nullptr && rel->kind == Kind::stepped && vr != nullptr && vr->kind == Kind::continuous
                        && vr->lo == 50.0f && vr->hi == 3000.0f;
        P.eq("bus25topo.var.spec_kinds", kinds ? 1 : 0, 1);
        for (const float ms : { 50.0f, 500.0f, 3000.0f })
        {
            RawParams r = var;
            r[Pid::rel] = ms;
            judge("bus25topo.var.rel." + std::to_string(static_cast<int>(ms)) + ".s", r, false);
        }
    }

    // ---- THRUST -----------------------------------------------------------------------------------------------------

    void thrustRows(Probe& P)
    {
        const ModeEntry& en = bus25();
        const struct
        {
            const char* key;
            float dbOct;
        } steps[] = { { "norm", 0.0f }, { "med", 1.5f }, { "loud", 3.01f } };
        std::vector<float> hz;
        for (double f = 20.0; f <= 20000.0; f *= 1.0592537251772889)            // 1/12 octave
            hz.push_back(static_cast<float>(f));
        std::vector<float> mag(hz.size());
        for (const auto& s : steps)
        {
            RawParams raw = fcmp::probe::modeRaw(en);
            raw[Pid::sce] = s.dbOct;
            const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
            const std::string key = std::string("bus25topo.thrust.") + s.key;
            P.eq(key + ".dboct_mismatch", e.sceDbOct == s.dbOct ? 0 : 1, 0);
            std::int64_t nz = 0;
            for (const float m : e.m)
                nz += m == 0.0f ? 0 : 1;
            P.eq(key + ".m_nonzero", nz, 0);
            en.scShapeDb(e, kFs, hz.data(), mag.data(), static_cast<int>(hz.size()));
            double shape = 0.0;
            for (const float m : mag)
                shape = std::max(shape, std::fabs(static_cast<double>(m)));
            P.le(key + ".mode_shape_max_db", shape, 0.0);
            if (s.dbOct == 3.01f)
            {
                const float at[] = { 250.0f, 1000.0f, 2000.0f };
                float resp[3] = { 0.0f, 0.0f, 0.0f };
                analysis::scResponse(en, e, kFs, at, resp);
                std::printf("NOTE     %s: scResponse 250 Hz %.4g dB, 1 kHz %.4g dB, 2 kHz %.4g dB\n", key.c_str(),
                            static_cast<double>(resp[0]), static_cast<double>(resp[1]), static_cast<double>(resp[2]));
                P.near(key + ".250hz_db", static_cast<double>(resp[0] - resp[1]), -2.0 * 3.01, 0.1);
                P.near(key + ".2khz_db", static_cast<double>(resp[2] - resp[1]), 3.01, 0.1);
            }
        }
    }
} // namespace

FCMP_PROBE(dsp, bus25topo)
{
    (void) C;
    flipRows(P);
    cvSumUnitRows(P);
    cvSumSettledRows(P);
    detectorRows(P);
    timeRows(P);
    thrustRows(P);
    return P.finish();
}
