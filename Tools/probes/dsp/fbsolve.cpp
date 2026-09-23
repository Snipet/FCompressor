// FCMP_PROBE layer=dsp name=fbsolve scope=global timeout=120
//
// dsp.fbsolve (F9, S3; SPRINTS S3.1; E §2.6; K2 #1, #5a, #5b, #5c; 01 §5.2-5.3): the feedback solvers, proven on
// PROBE-LOCAL FB traits (no registered Mode runs a feedback kernel in S3: Clean is feed-forward). The traits are
// Clean's policies with kTopologies = FB (PeakLog, QuadKnee, LinkMax, Hold<CrestAuto<SmoothBranching>>, NoStage2,
// ColourNone, Flat), instantiated here through ModeEngine and makeModeEntry exactly as FCDSP_DEFINE_MODE would, plus
// two variants whose computer is FeedbackZdf<QuadKnee> and FeedbackDelayed<QuadKnee>. Spec rows only (no golden).
//
// Reference: the root of r = A + B r^_fb(x - r) by 200-step bisection in double (tol::kFbBisectionSteps) on the same
// float inputs, with r^_fb the FB curve of QuadKnee.h's convention (loop gain k = QuadKnee::loopGain(S), the float
// value the solver uses). Every agreement row is <= 1e-5 dB (tol::kFbSolveDb). Random cases (seeded PCG32):
// T in [-60, 0], W 0 (hard) or [0, 24], S 0 (1:1), 1 (inf:1) or [0, 1), fs in {22.05, 44.1, 48, 96, 192} kHz, attack
// 5 us-250 ms, release 5 ms-5 s (log-uniform), x in [T - 20, T + 50] and r1 in [0, 50] (GR up to 60 dB, the range
// clamp's end).
//
//   fbsolve.closed.<branch>.<region>.max_err_db  QuadKnee::solveFb (closed form, E §2.6) through the ballistics' affine
//                          maps: branch = attack / release (SmoothBranching::solveFb's predictor) / hold (Hold's
//                          {r1, 0} branch, max of roots) / static (a = {0, 1}); region = below / knee / linear (where
//                          the reference root's output level y = x - r sits). Every branch x region occurs (count
//                          rows), except attack x below, which cannot (an attack root has overshoot).
//   fbsolve.zdf.<branch>.max_err_db               FeedbackZdf<QuadKnee> (bracketed Newton) on the same cases
//   fbsolve.maxroots.max_err_db                   max(solve(fast), solve(slow)) for DualRelease-shaped maps (01 §5.2:
//                          "max of roots is exact") against bisection of r = max(g1(r), g2(r))
//   fbsolve.monotone.decreases                    r^_fb non-decreasing on y in [-80, +40] dB every 0.1 dB (K2 #5a) for
//                          S in {0, 0.5, 0.75, 0.9, 1} x W in {0, 6, 24}
//   fbsolve.entry.static_gr.max_err_db            the probe-local ModeEntry's staticGr (topo FB) is the static FB curve
//   fbsolve.engine.<cfg>.<branch>.max_err_db      ModeEngine<FB traits> through the EngineRig, per sample: the tapped
//                          GR r[n] against the bisection root of the branch the predictor picks from the tapped
//                          r[n - 1] and detector level x[n]; held samples (r[n] == r[n - 1] while the release root is
//                          lower) are counted, and every hold run lasts exactly round(holdMs fs / 1000) samples
//                          (.hold_runs_wrong)
//   fbsolve.engine.zdf.<cfg>.*                    the same per-sample rows with FeedbackZdf<QuadKnee> as the computer
//   fbsolve.engine.auto.nonfinite                 TIME MODE AUTO (CrestAuto) in the FB kernel stays finite
//   fbsolve.stable.<fs>.<ratio>.{ptp_db,settle_err_db,reversals}   a 20 us attack at 4:1, 20:1 and inf:1 (hard knee,
//                          20 dB over): the settled GR's peak-to-peak <= 1e-4 dB, within 1e-3 dB of the static FB
//                          curve, and the attack never reverses (the ZDF loop's pole alpha / (1 + B k) never rings,
//                          E §2.6)
//   fbsolve.stable.naive_control_ptp_db           the naive one-sample-delay loop at 20 us, 4:1, 48 kHz buzzes (>= 1 dB
//                          peak-to-peak; E §2.6: 24.88 / 8.78 dB), which proves the ptp metric sees an unstable loop
//   fbsolve.guard.*                               FeedbackDelayed's run-time guard (K2 #5c): its bound alpha / (1 -
//                          alpha) at the actual fs; the delayed loop runs within it (opto-like 10 ms at 22.05 kHz,
//                          inf:1: the engine follows the delayed formula to 1e-5 dB and settles on the ZDF equilibrium)
//                          and falls back beyond it (20 us at 48 kHz, 4:1: the engine is bit-identical to
//                          FeedbackZdf's)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/combinators/CrestAuto.h"
#include "fcdsp/engine/stages/combinators/FeedbackDelayed.h"
#include "fcdsp/engine/stages/combinators/FeedbackZdf.h"
#include "fcdsp/engine/stages/combinators/Hold.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/DefineMode.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
    namespace st = fcdsp::stage;
    namespace tol = fcmp::probe::tol;

    // ---- probe-local FB traits
    // ---------------------------------------------------------------------------------------
    struct FbTraits
    {
        static constexpr const ModeDescriptor& desc = modes::kClean;
        using Detector   = st::PeakLog;
        using Computer   = st::QuadKnee;
        using Link       = st::LinkMax;
        using Ballistics = st::Hold<st::CrestAuto<st::SmoothBranching>>;
        using Stage2     = st::NoStage2;
        using Colour     = st::ColourNone;
        using ScShape    = st::Flat;
        static constexpr uint8_t kTopologies = 1u << kTopoFB;
    };
    struct FbZdfTraits : FbTraits
    {
        using Computer = st::FeedbackZdf<st::QuadKnee>;
    };
    struct FbDelayedTraits : FbTraits
    {
        using Computer = st::FeedbackDelayed<st::QuadKnee>;
    };

    constexpr ModeEntry kFbEntry = makeModeEntry<FbTraits>();
    constexpr ModeEntry kFbZdfEntry = makeModeEntry<FbZdfTraits>();
    constexpr ModeEntry kFbDelayedEntry = makeModeEntry<FbDelayedTraits>();
    static_assert(sizeof(ModeEngine<FbTraits>) <= kArenaBytes && sizeof(ModeEngine<FbDelayedTraits>) <= kArenaBytes);

    // ---- the reference (double)
    // --------------------------------------------------------------------------------------
    struct Law
    {
        double t = 0, w = 0, k = 0;             // threshold, knee (floored), loop gain (the solver's float value)
    };

    Law lawOf(float thr, float knee, float slope)
    {
        return { static_cast<double>(thr), static_cast<double>(std::max(st::QuadKnee::kMinKneeDb, knee)),
                 static_cast<double>(st::QuadKnee::loopGain(slope)) };
    }

    double rhatFb(const Law& l, double y)
    {
        const double o = y - l.t;
        const double q = std::clamp(o + 0.5 * l.w, 0.0, l.w);
        return l.k * (q * q / (2.0 * l.w) + std::max(0.0, o - 0.5 * l.w));
    }

    // The root of r = A + B r^_fb(x - r) by bisection on [A, A + B r^_fb(x - A)].
    double root(const Law& l, double x, double a, double b)
    {
        double lo = a, hi = a + b * rhatFb(l, x - a);
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * rhatFb(l, x - mid) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    enum Region { kBelow = 0, kKnee = 1, kLinear = 2 };
    constexpr std::array<const char*, 3> kRegionNames{ "below", "knee", "linear" };

    int regionOf(const Law& l, double x, double r)
    {
        const double o = x - r - l.t;
        return o < -0.5 * l.w ? kBelow : (o > 0.5 * l.w ? kLinear : kKnee);
    }

    // The branch SmoothBranching's predictor picks (E §2.6): attack if r^_fb(x - r1) > r1, with its root.
    struct BranchRoot
    {
        double r = 0;
        bool attack = false;
    };
    BranchRoot branchRoot(const Law& l, double x, double r1, double cA, double cR)
    {
        const bool attack = rhatFb(l, x - r1) > r1;
        const double c = attack ? cA : cR;
        return { root(l, x, (1.0 - c) * r1, c), attack };
    }

    // ---- float helpers
    // -----------------------------------------------------------------------------------------------
    LevelCtl levelCtl(float thr, float slope, float knee)
    {
        return LevelCtl{ simd::set1(thr), simd::set1(slope), simd::set1(knee), simd::set1(kS2Off) };
    }

    StageCtx ctxAt(float fs) { return StageCtx{ fs, fs, 1, {} }; }

    // Ballistics coefficients at (attack, release, hold) ms and fs, as a control tick designs them.
    template <class B>
    typename B::Coeffs ballistics(float atkMs, float relMs, float holdMs, float fs)
    {
        EngineParams p;
        p.atkTauMs = atkMs;
        p.relTauMs = relMs;
        p.holdMs = holdMs;
        typename B::Coeffs c{};
        B::design(c, p, ctxAt(fs));
        return c;
    }

    float logUniform(fcmp::probe::sig::Pcg32& g, float lo, float hi)
    {
        const double u = static_cast<double>(g.uniform());
        return static_cast<float>(static_cast<double>(lo) * fcmp::probe::sig::expDet(
                                      u * fcmp::probe::sig::logDet(static_cast<double>(hi) / static_cast<double>(lo))));
    }

    float uniform(fcmp::probe::sig::Pcg32& g, float lo, float hi) { return lo + (hi - lo) * g.uniform(); }

    // ---- the engine driver
    // -------------------------------------------------------------------------------------------
    struct Segment
    {
        double levelDb = 0, seconds = 0;
    };
    struct Render
    {
        std::vector<float> gr, det;             // lane 0 per sample: applied GR, detector level
        std::int64_t nonfinite = 0;
        std::vector<std::size_t> edges;
    };

    // A square wave (|x| = A every sample: the detector reads the level exactly), L = R, through the rig.
    Render renderSquare(const ModeEntry& entry, const EngineParams& e, float fs, const std::vector<Segment>& segs)
    {
        fcmp::probe::EngineRig rig(entry, e, fs);
        std::vector<float> in;
        Render out;
        for (const Segment& s : segs)
        {
            out.edges.push_back(in.size());
            const std::size_t n = static_cast<std::size_t>(s.seconds * static_cast<double>(fs) + 0.5);
            const auto a = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(s.levelDb));
            const std::size_t half = static_cast<std::size_t>(static_cast<double>(fs) / 2000.0);      // ~1 kHz
            for (std::size_t k = 0; k < n; ++k)
                in.push_back((k / (half > 0 ? half : 1)) % 2 == 0 ? a : -a);
        }
        std::vector<float> yl(in.size()), yr(in.size());
        rig.setTapping(true);
        constexpr std::size_t kBlock = 4096;
        for (std::size_t off = 0; off < in.size(); off += kBlock)
        {
            const std::size_t m = std::min(kBlock, in.size() - off);
            rig.process(in.data() + off, in.data() + off, yl.data() + off, yr.data() + off, m);
            const std::vector<float> g = rig.tap().lane(rig.tap().grDb, 0), d = rig.tap().lane(rig.tap().detDb, 0);
            out.gr.insert(out.gr.end(), g.begin(), g.end());
            out.det.insert(out.det.end(), d.begin(), d.end());
            rig.tap().clear();
        }
        for (std::size_t k = 0; k < in.size(); ++k)
            out.nonfinite += (std::isfinite(yl[k]) ? 0 : 1) + (std::isfinite(yr[k]) ? 0 : 1);
        return out;
    }

    EngineParams fbParams(float thr, float slope, float knee, float atkMs, float relMs, float holdMs)
    {
        const ModeEntry& clean = fcmp::probe::modeEntry("clean");
        EngineParams e = fcmp::probe::resolveRaw(clean, fcmp::probe::modeRaw(clean)).eng;
        e.topo = kTopoFB;
        e.preGainDb = 0.0f;
        e.thrDb = thr;
        e.slope = slope;
        e.kneeDb = knee;
        e.rangeDb = kRangeOff;
        e.atkTauMs = atkMs;
        e.relTauMs = relMs;
        e.holdMs = holdMs;
        e.link = 1.0f;
        e.mix = 1.0f;
        e.voice = 0;
        e.det = 0;
        e.flags = 0;
        return e;
    }

    std::string label(double v)                         // key-safe: 0.02 -> 0p02, -30 -> -30
    {
        char b[32];
        std::snprintf(b, sizeof b, "%g", v);
        std::string s(b);
        std::replace(s.begin(), s.end(), '.', 'p');
        return s;
    }

    // ---- sections
    // ----------------------------------------------------------------------------------------------------
    void solverRows(Probe& P)
    {
        fcmp::probe::sig::Pcg32 g(0x5eedfb01u, 0x9u);
        constexpr std::array<float, 5> kRates{ 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f };
        constexpr int kCases = 20000;
        // [branch][region]: branch 0 attack, 1 release, 2 hold, 3 static
        constexpr std::array<const char*, 4> kBranches{ "attack", "release", "hold", "static" };
        std::array<std::array<double, 3>, 4> closedErr{}, zdfErr{};
        std::array<std::array<std::int64_t, 3>, 4> count{};
        double maxRootsErr = 0.0;

        using SB = st::SmoothBranching;
        using HoldSB = st::Hold<SB>;
        for (int i = 0; i < kCases; ++i)
        {
            const float thr = uniform(g, -60.0f, 0.0f);
            const float knee = g.bounded(4) == 0 ? 0.0f : uniform(g, 0.0f, 24.0f);
            const std::uint32_t sPick = g.bounded(10);
            const float slope = sPick == 0 ? 0.0f : (sPick == 1 ? 1.0f : uniform(g, 0.0f, 1.0f));
            const float fs = kRates[g.bounded(static_cast<std::uint32_t>(kRates.size()))];
            const float atk = logUniform(g, 0.005f, 250.0f), rel = logUniform(g, 5.0f, 5000.0f);
            const float x = uniform(g, thr - 20.0f, thr + 50.0f);
            const float r1 = g.bounded(8) == 0 ? 0.0f : uniform(g, 0.0f, 50.0f);

            const Law law = lawOf(thr, knee, slope);
            const LevelCtl l = levelCtl(thr, slope, knee);
            const simd::f32x4 xv = simd::set1(x);
            const auto closed = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb({}, xv, l, a); };
            const auto zdf = [&](FbAffine a) noexcept { return st::FeedbackZdf<st::QuadKnee>::solveFb({}, xv, l, a); };

            const SB::Coeffs sc = ballistics<SB>(atk, rel, 0.0f, fs);
            SB::State ss{};
            ss.r = simd::set1(r1);
            const double cA = static_cast<double>(simd::lane<0>(sc.cA)), cR = static_cast<double>(simd::lane<0>(sc.cR));
            const double xd = static_cast<double>(x), r1d = static_cast<double>(r1);

            // attack / release through SmoothBranching::solveFb
            const BranchRoot ref = branchRoot(law, xd, r1d, cA, cR);
            const int b = ref.attack ? 0 : 1, reg = regionOf(law, xd, ref.r);
            ++count[static_cast<std::size_t>(b)][static_cast<std::size_t>(reg)];
            auto& ce = closedErr[static_cast<std::size_t>(b)][static_cast<std::size_t>(reg)];
            auto& ze = zdfErr[static_cast<std::size_t>(b)][static_cast<std::size_t>(reg)];
            ce = std::max(ce, std::fabs(static_cast<double>(simd::lane<0>(SB::solveFb(sc, ss, closed))) - ref.r));
            ze = std::max(ze, std::fabs(static_cast<double>(simd::lane<0>(SB::solveFb(sc, ss, zdf))) - ref.r));

            // hold: Hold<SmoothBranching> with a hold running: max(inner root, r1)
            {
                HoldSB::Coeffs hc = ballistics<HoldSB>(atk, rel, 10.0f, fs);
                HoldSB::State hs{};
                hs.inner.r = simd::set1(r1);
                hs.count = simd::set1(5.0f);
                const double want = std::max(ref.r, r1d);
                const int hreg = regionOf(law, xd, want);
                ++count[2][static_cast<std::size_t>(hreg)];
                auto& hce = closedErr[2][static_cast<std::size_t>(hreg)];
                auto& hze = zdfErr[2][static_cast<std::size_t>(hreg)];
                const double gotClosed = simd::lane<0>(HoldSB::solveFb(hc, hs, closed));
                hce = std::max(hce, std::fabs(gotClosed - want));
                const double gotZdf = simd::lane<0>(HoldSB::solveFb(hc, hs, zdf));
                hze = std::max(hze, std::fabs(gotZdf - want));
            }

            // static: a = {0, 1}
            {
                const FbAffine a{ simd::set1(0.0f), simd::set1(1.0f) };
                const double want = root(law, xd, 0.0, 1.0);
                const int sreg = regionOf(law, xd, want);
                ++count[3][static_cast<std::size_t>(sreg)];
                auto& sce = closedErr[3][static_cast<std::size_t>(sreg)];
                auto& sze = zdfErr[3][static_cast<std::size_t>(sreg)];
                sce = std::max(sce, std::fabs(static_cast<double>(simd::lane<0>(closed(a))) - want));
                sze = std::max(sze, std::fabs(static_cast<double>(simd::lane<0>(zdf(a))) - want));
            }

            // max of roots: DualRelease-shaped fast and slow maps (01 §5.2)
            {
                const double af = 1.0 - cR, as = 1.0 - static_cast<double>(oneMinusAlpha(rel * 4.0f, fs));
                const float rf1 = r1, rs1 = uniform(g, 0.0f, 50.0f);
                const auto a1f = static_cast<float>(af * static_cast<double>(rf1));
                const auto b1f = static_cast<float>(1.0 - af);
                const double rs1d = rs1, rf1d = rf1;
                const auto a2f = static_cast<float>(as * rs1d + (1.0 - as) * af * rf1d);
                const auto b2f = static_cast<float>((1.0 - as) * (1.0 - af));
                const float got = std::max(simd::lane<0>(closed(FbAffine{ simd::set1(a1f), simd::set1(b1f) })),
                                           simd::lane<0>(closed(FbAffine{ simd::set1(a2f), simd::set1(b2f) })));
                const double a1 = a1f, b1 = b1f, a2 = a2f, b2 = b2f;
                const auto f = [&](double r) {
                    const double u = rhatFb(law, xd - r);
                    return r - std::max(a1 + b1 * u, a2 + b2 * u);
                };
                double lo = 0.0, hi = std::max(a1 + b1 * rhatFb(law, xd), a2 + b2 * rhatFb(law, xd));
                for (int s = 0; s < tol::kFbBisectionSteps && hi > lo; ++s)
                {
                    const double mid = 0.5 * (lo + hi);
                    (f(mid) <= 0.0 ? lo : hi) = mid;
                }
                maxRootsErr = std::max(maxRootsErr, std::fabs(static_cast<double>(got) - 0.5 * (lo + hi)));
            }
        }

        for (std::size_t b = 0; b < kBranches.size(); ++b)
        {
            double zb = 0.0;
            for (std::size_t r = 0; r < kRegionNames.size(); ++r)
            {
                const std::string k = std::string("fbsolve.closed.") + kBranches[b] + "." + kRegionNames[r];
                std::printf("NOTE     %s: %lld case(s), closed %.3g dB, zdf %.3g dB\n", k.c_str(),
                            static_cast<long long>(count[b][r]), closedErr[b][r], zdfErr[b][r]);
                if (!(b == 0 && r == kBelow))            // an attack root always has overshoot: never below the knee
                    P.ge(k + ".count", static_cast<double>(count[b][r]), 1.0);
                P.le(k + ".max_err_db", closedErr[b][r], tol::kFbSolveDb);
                zb = std::max(zb, zdfErr[b][r]);
            }
            P.le(std::string("fbsolve.zdf.") + kBranches[b] + ".max_err_db", zb, tol::kFbSolveDb);
        }
        P.le("fbsolve.maxroots.max_err_db", maxRootsErr, tol::kFbSolveDb);
    }

    void monotoneRows(Probe& P)
    {
        std::int64_t decreases = 0;
        for (const float slope : { 0.0f, 0.5f, 0.75f, 0.9f, 1.0f })
            for (const float knee : { 0.0f, 6.0f, 24.0f })
            {
                const LevelCtl fb = st::QuadKnee::fbLevel(levelCtl(-20.0f, slope, knee));
                float prev = -1.0f;
                for (int i = 0; i <= 1200; ++i)
                {
                    const float y = -80.0f + 0.1f * static_cast<float>(i);
                    const float r = simd::lane<0>(st::QuadKnee::target({}, simd::set1(y), fb));
                    decreases += r < prev ? 1 : 0;
                    prev = r;
                }
            }
        P.eq("fbsolve.monotone.decreases", decreases, 0);
    }

    void entryRows(Probe& P)
    {
        const EngineParams e = fbParams(-30.0f, 0.8f, 8.0f, 1.0f, 100.0f, 0.0f);
        const Law law = lawOf(e.thrDb, e.kneeDb, e.slope);
        std::vector<float> xs, gr;
        for (int i = 0; i <= 1100; ++i)
            xs.push_back(-80.0f + 0.1f * static_cast<float>(i));
        gr.resize(xs.size());
        kFbEntry.staticGr(e, xs.data(), gr.data(), static_cast<int>(xs.size()));
        double err = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            const double want = root(law, static_cast<double>(xs[i]), 0.0, 1.0);
            err = std::max(err, std::fabs(static_cast<double>(gr[i]) - want));
        }
        P.le("fbsolve.entry.static_gr.max_err_db", err, tol::kFbSolveDb);
    }

    // Per sample: the engine's GR against the bisection root of the predictor's branch (from the tapped r[n - 1] and
    // x[n]); held samples are counted and every hold run must last exactly N samples.
    struct EngineCfg
    {
        const char* name;
        float fs, atk, rel, hold, slope, knee;
    };

    void perSampleRows(Probe& P, const std::string& k, const ModeEntry& entry, const EngineCfg& c,
                       const std::vector<Segment>& segs, float thr)
    {
        const EngineParams e = fbParams(thr, c.slope, c.knee, c.atk, c.rel, c.hold);
        const Render run = renderSquare(entry, e, c.fs, segs);
        const Law law = lawOf(e.thrDb, e.kneeDb, e.slope);
        using B = FbTraits::Ballistics;
        const B::Coeffs bc = ballistics<B>(c.atk, c.rel, c.hold, c.fs);
        const double cA = static_cast<double>(simd::lane<0>(bc.inner.inner.cA));
        const double cR = static_cast<double>(simd::lane<0>(bc.inner.inner.cR));
        const auto n = static_cast<std::int64_t>(simd::lane<0>(bc.n));

        double errA = 0.0, errR = 0.0;
        std::int64_t nA = 0, nR = 0, held = 0, runs = 0, wrongRuns = 0, heldRun = 0;
        for (std::size_t i = 1; i < run.gr.size(); ++i)
        {
            const double r1 = run.gr[i - 1], r = run.gr[i], x = run.det[i];
            const BranchRoot ref = branchRoot(law, x, r1, cA, cR);
            if (r == r1 && ref.r < r1 - 1e-6)
            {
                ++held;
                ++heldRun;
                continue;
            }
            if (heldRun > 0)
            {
                ++runs;
                wrongRuns += heldRun == n ? 0 : 1;
                heldRun = 0;
            }
            double& err = ref.attack ? errA : errR;
            err = std::max(err, std::fabs(r - ref.r));
            ++(ref.attack ? nA : nR);
        }
        std::printf("NOTE     %s: %lld attack, %lld release, %lld held sample(s) in %lld hold run(s) of %lld; "
                    "max err %.3g / %.3g dB\n",
                    k.c_str(), static_cast<long long>(nA), static_cast<long long>(nR), static_cast<long long>(held),
                    static_cast<long long>(runs), static_cast<long long>(n), errA, errR);
        P.le(k + ".attack.max_err_db", errA, tol::kFbSolveDb);
        P.le(k + ".release.max_err_db", errR, tol::kFbSolveDb);
        P.eq(k + ".nonfinite", run.nonfinite, 0);
        if (n > 0)
        {
            P.ge(k + ".hold_runs", static_cast<double>(runs), 1.0);
            P.eq(k + ".hold_runs_wrong", wrongRuns, 0);
        }
        else
            P.eq(k + ".held_samples", held, 0);
    }

    void engineRows(Probe& P)
    {
        constexpr float kThr = -30.0f;
        const std::vector<Segment> segs = { { kThr - 10.0, 0.1 }, { kThr + 20.0, 0.25 }, { kThr + 6.0, 0.35 },
                                            { kThr - 20.0, 0.4 } };
        const EngineCfg cfgs[] = {
            { "a1.r50.inf.k0", 48000.0f, 1.0f, 50.0f, 0.0f, 1.0f, 0.0f },
            { "a0p02.r100.r4.k12", 48000.0f, 0.02f, 100.0f, 0.0f, 0.75f, 12.0f },
            { "a0p5.r200.r10.k6.h20", 44100.0f, 0.5f, 200.0f, 20.0f, 0.9f, 6.0f },
            { "a10.r60.inf.k6.h5.96k", 96000.0f, 10.0f, 60.0f, 5.0f, 1.0f, 6.0f },
        };
        for (const EngineCfg& c : cfgs)
            perSampleRows(P, std::string("fbsolve.engine.") + c.name, kFbEntry, c, segs, kThr);

        // FeedbackZdf<QuadKnee> as the engine's computer: per sample against bisection, as the closed form. (Engine
        // against engine is not a 1e-5 dB comparison: near a slow release's equilibrium each FB kernel rests where its
        // own float rounding stalls, SmoothBranching.h's documented FB limit.)
        perSampleRows(P, "fbsolve.engine.zdf.a0p2.r80.r5.k10.h2", kFbZdfEntry,
                      EngineCfg{ "", 48000.0f, 0.2f, 80.0f, 2.0f, 0.8f, 10.0f }, segs, kThr);

        // TIME MODE AUTO (CrestAuto) in the FB kernel.
        {
            EngineParams e = fbParams(kThr, 0.8f, 10.0f, 0.2f, 80.0f, 0.0f);
            e.flags = static_cast<uint8_t>(e.flags | kEngAutoRelease);
            const Render c = renderSquare(kFbEntry, e, 48000.0f, segs);
            P.eq("fbsolve.engine.auto.nonfinite", c.nonfinite, 0);
            std::int64_t bad = 0;
            for (const float v : c.gr)
                bad += std::isfinite(v) && v >= 0.0f ? 0 : 1;
            P.eq("fbsolve.engine.auto.gr_invalid", bad, 0);
        }
    }

    // Peak-to-peak and the reversals of a trace window.
    double peakToPeak(const std::vector<float>& v, std::size_t from, std::size_t to)
    {
        const auto [lo, hi] = std::minmax_element(v.begin() + static_cast<std::ptrdiff_t>(from),
                                                  v.begin() + static_cast<std::ptrdiff_t>(to));
        return static_cast<double>(*hi) - static_cast<double>(*lo);
    }

    void stabilityRows(Probe& P)
    {
        constexpr float kThr = -30.0f;
        for (const float fs : { 48000.0f, 44100.0f, 22050.0f })
            for (const float slope : { 0.75f, 0.95f, 1.0f })
            {
                const EngineParams e = fbParams(kThr, slope, 0.0f, 0.02f, 100.0f, 0.0f);
                const Render run = renderSquare(kFbEntry, e, fs, { { kThr - 20.0, 0.1 }, { kThr + 20.0, 0.3 } });
                const std::size_t edge = run.edges[1], end = run.gr.size();
                const std::size_t settled = end - static_cast<std::size_t>(0.1f * fs);
                std::int64_t reversals = 0;
                for (std::size_t k = edge + 1; k < end; ++k)
                    reversals += run.gr[k] < run.gr[k - 1] ? 1 : 0;
                const double want = root(lawOf(kThr, 0.0f, slope), static_cast<double>(kThr) + 20.0, 0.0, 1.0);
                const std::string ratio = slope >= 1.0f ? std::string("inf")
                                                        : label(1.0 / (1.0 - static_cast<double>(slope)));
                const std::string k = "fbsolve.stable." + label(static_cast<double>(fs)) + ".r" + ratio;
                P.le(k + ".ptp_db", peakToPeak(run.gr, settled, end), 1e-4);
                P.le(k + ".settle_err_db", std::fabs(static_cast<double>(run.gr[end - 1]) - want), 1e-3);
                P.eq(k + ".reversals", reversals, 0);
                P.eq(k + ".nonfinite", run.nonfinite, 0);
            }

        // The naive one-sample-delay loop (E §2.6) at the same 20 us, 4:1, 48 kHz, hard knee, 20 dB over: it buzzes.
        {
            const double alpha = 1.0 - static_cast<double>(oneMinusAlpha(0.02f, 48000.0f)), k = 3.0, over = 20.0;
            std::vector<float> r(4000, 0.0f);
            double prev = 0.0;
            for (std::size_t n = 0; n < r.size(); ++n)
            {
                prev = alpha * prev + (1.0 - alpha) * k * std::max(0.0, over - prev);
                r[n] = static_cast<float>(prev);
            }
            const double ptp = peakToPeak(r, 2000, r.size());
            std::printf("NOTE     naive delayed loop, 20 us / 4:1 / 48 kHz: settled GR swings %.4g dB peak-to-peak\n",
                        ptp);
            P.ge("fbsolve.stable.naive_control_ptp_db", ptp, 1.0);
        }
    }

    void guardRows(Probe& P)
    {
        using D = st::FeedbackDelayed<st::QuadKnee>;
        // the bound alpha / (1 - alpha) at the actual fs, against the closed form in double
        struct G
        {
            const char* name;
            float atk, fs;
        };
        for (const G& g : { G{ "opto_10ms_22k", 10.0f, 22050.0f }, G{ "opto_10ms_44k", 10.0f, 44100.0f },
                            G{ "fet_20us_48k", 0.02f, 48000.0f }, G{ "fet_20us_192k", 0.02f, 192000.0f } })
        {
            const double samples = static_cast<double>(g.atk) * static_cast<double>(g.fs) / 1000.0;   // tau * fs
            const double a = fcmp::probe::sig::expDet(-1.0 / samples);
            const double want = a / (1.0 - a);
            P.near(std::string("fbsolve.guard.limit.") + g.name, static_cast<double>(D::guardLimit(g.atk, g.fs)), want,
                   0.0, 1e-4);
        }
        // the decision
        struct Dcase
        {
            const char* name;
            float atk, fs, slope, b;
            int want;
        };
        for (const Dcase& d : { Dcase{ "opto_inf_22k", 10.0f, 22050.0f, 1.0f, 0.1f, 1 },
                                Dcase{ "fet_r4_48k", 0.02f, 48000.0f, 0.75f, 0.6f, 0 },
                                Dcase{ "fet_r4_192k", 0.02f, 192000.0f, 0.75f, 0.2f, 1 },
                                Dcase{ "fet_r1p5_48k", 0.02f, 48000.0f, 1.0f / 3.0f, 0.6f, 1 },
                                Dcase{ "static_b1", 10.0f, 22050.0f, 0.75f, 1.0f, 0 } })
        {
            EngineParams p;
            p.atkTauMs = d.atk;
            D::Coeffs c{};
            D::design(c, p, ctxAt(d.fs));
            const FbAffine a{ simd::set1(1.0f), simd::set1(d.b) };
            const bool delayed = D::delayedAt(c, levelCtl(-30.0f, d.slope, 6.0f), a);
            P.eq(std::string("fbsolve.guard.delayed.") + d.name, delayed ? 1 : 0, d.want);
        }

        constexpr float kThr = -30.0f;
        const std::vector<Segment> segs = { { kThr - 20.0, 0.1 }, { kThr + 20.0, 0.4 }, { kThr + 5.0, 0.4 } };
        // beyond the bound: the engine falls back to FeedbackZdf, bit for bit
        {
            const EngineParams e = fbParams(kThr, 0.75f, 0.0f, 0.02f, 100.0f, 0.0f);
            const Render a = renderSquare(kFbDelayedEntry, e, 48000.0f, segs);
            const Render b = renderSquare(kFbZdfEntry, e, 48000.0f, segs);
            P.eq("fbsolve.guard.fallback.bit_identical", a.gr == b.gr ? 1 : 0, 1);
            P.le("fbsolve.guard.fallback.ptp_db", peakToPeak(a.gr, a.edges[2] - 4800, a.edges[2]), 1e-4);
        }
        // within it (opto-like, 22.05 kHz, inf:1): the delayed formula runs, is stable, and settles on the equilibrium
        {
            const float fs = 22050.0f;
            const EngineParams e = fbParams(kThr, 1.0f, 6.0f, 10.0f, 60.0f, 0.0f);
            const Render a = renderSquare(kFbDelayedEntry, e, fs, segs), z = renderSquare(kFbZdfEntry, e, fs, segs);
            const Law law = lawOf(e.thrDb, e.kneeDb, e.slope);
            using B = FbTraits::Ballistics;
            const B::Coeffs bc = ballistics<B>(e.atkTauMs, e.relTauMs, 0.0f, fs);
            const double cA = static_cast<double>(simd::lane<0>(bc.inner.inner.cA));
            const double cR = static_cast<double>(simd::lane<0>(bc.inner.inner.cR));
            double formula = 0.0;
            for (std::size_t k = 1; k < a.gr.size(); ++k)
            {
                const double r1 = a.gr[k - 1], x = a.det[k];
                const double u = rhatFb(law, x - r1);
                const double c = u > r1 ? cA : cR;
                formula = std::max(formula, std::fabs(static_cast<double>(a.gr[k]) - ((1.0 - c) * r1 + c * u)));
            }
            const std::size_t end = a.gr.size(), mid = a.edges[2];
            P.le("fbsolve.guard.delayed_22k.formula_err_db", formula, tol::kFbSolveDb);
            P.le("fbsolve.guard.delayed_22k.ptp_db", peakToPeak(a.gr, mid - 2205, mid), 1e-4);
            P.le("fbsolve.guard.delayed_22k.settle_vs_zdf_db",
                 std::max(std::fabs(static_cast<double>(a.gr[mid - 1]) - static_cast<double>(z.gr[mid - 1])),
                          std::fabs(static_cast<double>(a.gr[end - 1]) - static_cast<double>(z.gr[end - 1]))),
                 1e-3);
            P.eq("fbsolve.guard.delayed_22k.nonfinite", a.nonfinite, 0);
        }
    }
} // namespace

FCMP_PROBE(dsp, fbsolve)
{
    solverRows(P);
    monotoneRows(P);
    entryRows(P);
    engineRows(P);
    stabilityRows(P);
    guardRows(P);
    return P.finish();
}
