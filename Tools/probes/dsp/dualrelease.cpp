// FCMP_PROBE layer=dsp name=dualrelease scope=global timeout=120
//
// dsp.dualrelease (M1, S7; SPRINTS S7.3; 01 §5.2-5.3, §10.4; E §2.5b, §2.6; K2 #1, #4): the ballistics Bus G adds,
// stage::DualRelease and stage::AutoSwitch, proven on the policy functions (unit rows, 01 §8.4 step 3) and through
// Bus G's registered engine (EngineRig), plus the design constants of Bus G's VCA voice (stage::VcaBus). Spec rows
// only (no golden): the Mode-level goldens are dsp.static/time/... of `bus-g`. Reference numbers are double precision
// on the policies' own float rates.
//
// DualRelease, feed-forward (fs 48 and 192 kHz; Bus G's constants and random ones; a random target program):
//   dualrelease.ff.<fs>.<set>.model_max_err_db   the policy against the documented recurrence in double (fast smooth-
//                                     branching one-pole; slow one-pole toward it, charging while r_f > r_s; the max):
//                                     <= 1e-3 dB (the precision dsp.srsweep holds a long release to)
//   dualrelease.ff.grdb_mismatches    grDb(state) is the value tick returned, every sample
//   dualrelease.ff.status_mismatches  status() b2 and releaseNowMs follow "the slow path holds the GR" (r_s > r_f)
//   dualrelease.ff.seed_mismatches    seed(v): both paths at v, grDb = v
// DualRelease, feedback (K2 #1: the affine maps and the max of roots):
//   dualrelease.fb.solve.<branch>.max_err_db  20,000 random cases (T, W, S, fs, the four times, x, r_f1, r_s1; the
//                                     fbsolve grid): solveFb with QuadKnee's closed form against 200-step bisection of
//                                     r = max(g_f(r), g_s(r)), g_f = {alpha_f r_f1, 1 - alpha_f}, g_s = {alpha_s r_s1 +
//                                     (1 - alpha_s) alpha_f r_f1, (1 - alpha_s)(1 - alpha_f)} on r^_fb, the branches by
//                                     the documented predictors: <= 1e-5 dB (tol::kFbSolveDb); branch = fast_attack /
//                                     fast_release (the fast root won) / slow (the slow root won), each .count >= 1;
//                                     .charge.count >= 1 and .charge.slow_won 0 (a charging slow path never wins)
//   dualrelease.fb.commit.*           commitFb of the unlinked root: grDb == r (grdb_mismatches); where the fast root won,
//                                     r_s = alpha_s r_s1 + c_s r (follow_max_err_db <= 1e-5); where the slow root won,
//                                     r_f = the fast path's own root (fast_root_max_err_db <= 1e-5); a linked r' >= r
//                                     (LinkMax raises a lane) commits as the applied GR too (linked_mismatches)
//   dualrelease.fb.trajectory.*       the policy driven along a square-level program at 48 kHz with Bus G's constants:
//                                     every sample's root against the bisection from the current state (max_err_db
//                                     <= 1e-5), with samples where the fast and where the slow root won (>= 1 each)
//   dualrelease.fb.engine.*           a probe-local FB kernel (PeakLog, QuadKnee, LinkMax, DualRelease, NoStage2,
//                                     ColourNone, Flat; kTopologies = FB) through ModeEngine and the EngineRig: finite,
//                                     GR >= 0, the settled GR of a 2 s level on the static FB curve (<= 1e-3 dB, peak-
//                                     to-peak <= 1e-4 dB), the release never reverses, and the release after 3 s of GR
//                                     is >= 4 x the one after 20 ms (program dependence survives the loop)
// AutoSwitch<SmoothBranching, DualRelease> (01 §5.2 combinators):
//   dualrelease.autoswitch.select.mismatches  AutoTagSelect: B for kTagAuto or kTagAuto2, A otherwise
//   dualrelease.autoswitch.<ff|fb>.<a|b>_mismatches  with the selection constant, bit-identical to the path alone
//                                     (outputs, grDb, telemetry times, status)
//   dualrelease.autoswitch.<ff|fb>.handover.mismatches  live switches A -> B -> A mid-release: the documented hand-over
//                                     (the idle path seeded from the running one's GR, both run, the applied GR is
//                                     (1 - S(w)) r_A + S(w) r_B with host::rampShape over 20 ms; FB starts one sample
//                                     later, after the switching sample's commit), sample by sample, bit for bit;
//                                     .blend_samples = 20 ms of samples (+-1)
//   dualrelease.autoswitch.design.lands_mismatches   the newly selected path is designed from a value-initialised
//                                     Coeffs (it lands on its targets); .frozen_mismatches: the other path's Coeffs do
//                                     not move while it fades out
// Bus G (the registered `bus-g` engine, 48 kHz, 4:1, ATTACK .1 MS, RELEASE AUTO; 1 kHz square bursts at T + 20 dB,
// then T - 20 dB; the release measured on the tapped GR in the switch's law, as dsp.time):
//   dualrelease.busg.auto.release.<burst>_s  NOTE per burst (10 ms, 100 ms, 300 ms, 1 s, 5 s); spec: every release in
//                                     the published span [0.1, 1.2] s (.in_span; 1e-4 relative slack: a transient's
//                                     release is tau_Rf, the span's lower end), nondecreasing with the burst
//                                     (.nonmonotone 0), 10 ms -> tau_Rf (0.1 s, +-5 %), 5 s -> the charged two-pole
//                                     cascade's 1/e time from the fitted constants (+-2 %)
//   dualrelease.busg.auto.internals.*  after 5 s + 0.3 s: AUTO SLOW = 1, SLOW ENV > FAST ENV, status b2 set; after
//                                     10 ms + 20 ms: AUTO SLOW = 0, b2 clear; off AUTO (1.2 s): SLOW ENV = AUTO SLOW = 0
//                                     and FAST ENV is the GR
//   dualrelease.busg.vca.*            VcaBus through the entry's colourCurve (VOICE VCA): no even harmonics
//                                     (h2_db <= -140), H3 of a 0 dBFS sine -68.6 dB (+-0.3) at 0 dB drive and -44.6 dB
//                                     at +12 dB, unity small-signal gain (1e-6), and the fundamental of a +6 dBFS sine
//                                     within 0.05 dB (the modelled meter-truth budget, VcaBus.h)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/host/Ramps.h"
#include "fcdsp/engine/stages/ballistics/DualRelease.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/combinators/AutoSwitch.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fcdsp::modes
{
    extern const ModeDescriptor kBusG;          // BusGDesc.cpp: the probe-local FB traits borrow its descriptor
}

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace st = fcdsp::stage;
    namespace tol = fcmp::probe::tol;
    namespace measure = fcmp::probe::measure;
    namespace sig = fcmp::probe::sig;

    using SB = st::SmoothBranching;
    using DR = st::DualRelease;
    using AS = st::AutoSwitch<SB, DR>;

    constexpr float kFs = 48000.0f;
    // Bus G's fitted RELEASE AUTO constants (BusGDesc.cpp, docs/modes/bus-g.md): tau_Rf, tau_C, tau_Rs in ms.
    constexpr float kBusGFastMs = 100.0f, kBusGChargeMs = 300.0f, kBusGSlowMs = 1000.0f;
    constexpr uint32_t kAutoProgram = static_cast<uint32_t>(kTagAuto | kTagProgram);     // Bus G's AUTO step tags

    float lane0(simd::f32x4 v) { return simd::lane<0>(v); }
    double lane0d(simd::f32x4 v) { return static_cast<double>(simd::lane<0>(v)); }

    StageCtx ctxAt(float fs) { return StageCtx{ fs, fs, 1, {} }; }

    // EngineParams carrying the times DualRelease and SmoothBranching read (ms).
    EngineParams timesOf(float atkMs, float relMs, float fastMs, float chargeMs, float slowMs, uint32_t tags = 0)
    {
        EngineParams p;
        p.atkTauMs = atkMs;
        p.relTauMs = relMs;
        p.m[0] = fastMs;
        p.m[1] = chargeMs;
        p.m[2] = slowMs;
        p.tags = tags;
        return p;
    }

    float logUniform(sig::Pcg32& g, float lo, float hi)
    {
        const double u = static_cast<double>(g.uniform());
        return static_cast<float>(static_cast<double>(lo)
                                  * sig::expDet(u * sig::logDet(static_cast<double>(hi) / static_cast<double>(lo))));
    }

    float uniform(sig::Pcg32& g, float lo, float hi) { return lo + (hi - lo) * g.uniform(); }

    std::string rateKey(float fs) { return std::to_string(static_cast<long>(fs)); }

    // A piecewise-constant random program: `n` samples of levels in [lo, hi], segments of 1 ms ... 1.5 s.
    std::vector<float> randomProgram(sig::Pcg32& g, std::size_t n, float fs, float lo, float hi)
    {
        std::vector<float> v;
        v.reserve(n);
        while (v.size() < n)
        {
            const float level = g.bounded(4) == 0 ? lo : uniform(g, lo, hi);
            const auto len = static_cast<std::size_t>(logUniform(g, 0.001f, 1.5f) * fs) + 1;
            for (std::size_t k = 0; k < len && v.size() < n; ++k)
                v.push_back(level);
        }
        return v;
    }

    // ---- the FB reference (double; fbsolve's) --------------------------------------------------------------------
    struct Law
    {
        double t = 0, w = 0, k = 0;
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

    double root(const Law& l, double x, double a, double b)             // r = a + b r^_fb(x - r)
    {
        double lo = a, hi = a + b * rhatFb(l, x - a);
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * rhatFb(l, x - mid) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    LevelCtl levelCtl(float thr, float slope, float knee)
    {
        return LevelCtl{ simd::set1(thr), simd::set1(slope), simd::set1(knee), simd::set1(kS2Off) };
    }

    // DualRelease's FB reference from the state (r_f1, r_s1): the branch maps, their roots and THE root of
    // r = max(g_f(r), g_s(r)) by bisection.
    struct DualRef
    {
        double r = 0, rootF = 0, rootS = 0, cS = 0;
        bool attack = false, charge = false;
    };

    DualRef dualRef(const Law& l, double x, double rf1, double rs1, const DR::Coeffs& c)
    {
        DualRef out;
        out.attack = rhatFb(l, x - rf1) > rf1;
        const double cF = out.attack ? lane0d(c.fast.cA) : lane0d(c.fast.cR);
        const double aF = (1.0 - cF) * rf1;
        out.rootF = root(l, x, aF, cF);
        out.charge = out.rootF > rs1;
        out.cS = out.charge ? lane0d(c.slow.cA) : lane0d(c.slow.cR);
        const double aS = (1.0 - out.cS) * rs1 + out.cS * aF, bS = out.cS * cF;
        out.rootS = root(l, x, aS, bS);
        const auto f = [&](double r) {
            const double u = rhatFb(l, x - r);
            return r - std::max(aF + cF * u, aS + bS * u);
        };
        double lo = 0.0, hi = std::max(aF + cF * rhatFb(l, x), aS + bS * rhatFb(l, x));
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (f(mid) <= 0.0 ? lo : hi) = mid;
        }
        out.r = 0.5 * (lo + hi);
        return out;
    }

    // ---- A. DualRelease, feed-forward ----------------------------------------------------------------------------------
    void ffRows(Probe& P)
    {
        sig::Pcg32 g(0xd0a1ull, 0x7ull);
        std::int64_t grdbMismatch = 0, statusMismatch = 0;
        for (const float fs : { 48000.0f, 192000.0f })
            for (int set = 0; set < 2; ++set)
            {
                const EngineParams p = set == 0
                                           ? timesOf(0.1f, 300.0f, kBusGFastMs, kBusGChargeMs, kBusGSlowMs)
                                           : timesOf(logUniform(g, 0.05f, 30.0f), 300.0f, logUniform(g, 20.0f, 300.0f),
                                                     logUniform(g, 50.0f, 1000.0f), logUniform(g, 500.0f, 5000.0f));
                DR::Coeffs c{};
                DR::design(c, p, ctxAt(fs));
                const double cA = lane0d(c.fast.cA), cRf = lane0d(c.fast.cR), cC = lane0d(c.slow.cA),
                             cRs = lane0d(c.slow.cR);
                const std::vector<float> t = randomProgram(g, static_cast<std::size_t>(6.0f * fs), fs, 0.0f, 30.0f);
                DR::State s{};
                DR::seed(s, simd::set1(0.0f));
                double rf = 0.0, rs = 0.0, err = 0.0;
                for (const float tk : t)
                {
                    const simd::f32x4 r = DR::tick(c, s, simd::set1(tk));
                    const double td = static_cast<double>(tk);
                    rf += (td > rf ? cA : cRf) * (td - rf);
                    rs += (rf > rs ? cC : cRs) * (rf - rs);
                    err = std::max(err, std::fabs(lane0d(r) - std::max(rf, rs)));
                    grdbMismatch += lane0(DR::grDb(s)) == lane0(r) ? 0 : 1;
                    const bool slowHolds = lane0(s.s.r) > lane0(s.f.r);
                    const bool b2 = (DR::status(s) & DR::kAutoSlowBit) != 0;
                    const float relNow = lane0(DR::releaseNowMs(c, s));
                    statusMismatch += b2 == slowHolds && relNow == (slowHolds ? c.slow.tauRMs : c.fast.tauRMs) ? 0 : 1;
                }
                const std::string k = "dualrelease.ff." + rateKey(fs) + (set == 0 ? ".busg" : ".random");
                std::printf("NOTE     %s: tau_A %.4g ms, tau_Rf %.4g ms, tau_C %.4g ms, tau_Rs %.4g ms: max |err| %.3g dB\n",
                            k.c_str(), static_cast<double>(p.atkTauMs), static_cast<double>(p.m[0]),
                            static_cast<double>(p.m[1]), static_cast<double>(p.m[2]), err);
                P.le(k + ".model_max_err_db", err, 1e-3);
            }
        P.eq("dualrelease.ff.grdb_mismatches", grdbMismatch, 0);
        P.eq("dualrelease.ff.status_mismatches", statusMismatch, 0);

        std::int64_t seedMismatch = 0;
        for (const float v : { 0.0f, 3.5f, 27.25f })
        {
            DR::State s{};
            s.f.r = simd::set1(11.0f);
            s.s.r = simd::set1(2.0f);
            DR::seed(s, simd::set1(v));
            seedMismatch += lane0(s.f.r) == v && lane0(s.s.r) == v && lane0(DR::grDb(s)) == v ? 0 : 1;
        }
        P.eq("dualrelease.ff.seed_mismatches", seedMismatch, 0);
    }

    // ---- B. DualRelease, feedback --------------------------------------------------------------------------------------
    void fbSolveRows(Probe& P)
    {
        sig::Pcg32 g(0xd0a1fb01ull, 0x3ull);
        constexpr std::array<float, 5> kRates{ 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f };
        constexpr std::array<const char*, 3> kBranches{ "fast_attack", "fast_release", "slow" };
        std::array<double, 3> err{};
        std::array<std::int64_t, 3> count{};
        std::int64_t charges = 0, chargeSlowWon = 0, grdbMismatch = 0, linkedMismatch = 0;
        double followErr = 0.0, fastRootErr = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const float thr = uniform(g, -60.0f, 0.0f);
            const float knee = g.bounded(4) == 0 ? 0.0f : uniform(g, 0.0f, 24.0f);
            const std::uint32_t sPick = g.bounded(10);
            const float slope = sPick == 0 ? 0.0f : (sPick == 1 ? 1.0f : uniform(g, 0.0f, 1.0f));
            const float fs = kRates[g.bounded(static_cast<std::uint32_t>(kRates.size()))];
            const EngineParams p = timesOf(logUniform(g, 0.005f, 250.0f), 300.0f, logUniform(g, 5.0f, 500.0f),
                                           logUniform(g, 5.0f, 2000.0f), logUniform(g, 50.0f, 5000.0f));
            const float x = uniform(g, thr - 20.0f, thr + 50.0f);
            const float rf1 = g.bounded(8) == 0 ? 0.0f : uniform(g, 0.0f, 50.0f);
            const float rs1 = g.bounded(8) == 0 ? 0.0f : uniform(g, 0.0f, 50.0f);

            DR::Coeffs c{};
            DR::design(c, p, ctxAt(fs));
            const Law law = lawOf(thr, knee, slope);
            const LevelCtl l = levelCtl(thr, slope, knee);
            const simd::f32x4 xv = simd::set1(x);
            const auto closed = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb({}, xv, l, a); };

            DR::State s{};
            s.f.r = simd::set1(rf1);
            s.s.r = simd::set1(rs1);
            const float got = lane0(DR::solveFb(c, s, closed));
            const DualRef ref = dualRef(law, static_cast<double>(x), static_cast<double>(rf1),
                                        static_cast<double>(rs1), c);
            const bool slowWon = lane0(s.fbSlowWon) > 0.5f;
            const std::size_t b = slowWon ? 2u : (ref.attack ? 0u : 1u);
            ++count[b];
            err[b] = std::max(err[b], std::fabs(static_cast<double>(got) - ref.r));
            if (ref.charge)
            {
                ++charges;
                chargeSlowWon += slowWon ? 1 : 0;
            }

            // commit: the linked copy first (it needs the solve's scratch), then the unlinked root
            DR::State linked = s;
            const float up = got + uniform(g, 0.0f, 3.0f);
            DR::commitFb(c, linked, simd::set1(up));
            linkedMismatch += lane0(DR::grDb(linked)) == up ? 0 : 1;
            DR::commitFb(c, s, simd::set1(got));
            grdbMismatch += lane0(DR::grDb(s)) == got ? 0 : 1;
            if (slowWon)
                fastRootErr = std::max(fastRootErr, std::fabs(lane0d(s.f.r) - ref.rootF));
            else
                followErr = std::max(followErr, std::fabs(lane0d(s.s.r) - ((1.0 - ref.cS) * static_cast<double>(rs1)
                                                                              + ref.cS * static_cast<double>(got))));
        }
        for (std::size_t b = 0; b < kBranches.size(); ++b)
        {
            const std::string k = std::string("dualrelease.fb.solve.") + kBranches[b];
            std::printf("NOTE     %s: %lld case(s), max |err| %.3g dB\n", k.c_str(), static_cast<long long>(count[b]),
                        err[b]);
            P.ge(k + ".count", static_cast<double>(count[b]), 1.0);
            P.le(k + ".max_err_db", err[b], tol::kFbSolveDb);
        }
        std::printf("NOTE     dualrelease.fb.solve.charge: %lld charging case(s)\n", static_cast<long long>(charges));
        P.ge("dualrelease.fb.solve.charge.count", static_cast<double>(charges), 1.0);
        P.eq("dualrelease.fb.solve.charge.slow_won", chargeSlowWon, 0);
        P.eq("dualrelease.fb.commit.grdb_mismatches", grdbMismatch, 0);
        P.eq("dualrelease.fb.commit.linked_mismatches", linkedMismatch, 0);
        P.le("dualrelease.fb.commit.follow_max_err_db", followErr, tol::kFbSolveDb);
        P.le("dualrelease.fb.commit.fast_root_max_err_db", fastRootErr, tol::kFbSolveDb);
    }

    struct Seg
    {
        double levelDb = 0, seconds = 0;
    };

    void fbTrajectoryRows(Probe& P)
    {
        const float thr = -20.0f, slope = 0.75f, knee = 6.0f;
        const EngineParams p = timesOf(1.0f, 300.0f, kBusGFastMs, kBusGChargeMs, kBusGSlowMs);
        DR::Coeffs c{};
        DR::design(c, p, ctxAt(kFs));
        const Law law = lawOf(thr, knee, slope);
        const LevelCtl l = levelCtl(thr, slope, knee);
        const Seg segs[] = { { -40, 0.1 }, { 0, 1.5 },  { -40, 0.4 }, { -10, 0.05 },
                             { -40, 1.5 }, { 5, 0.02 }, { -50, 0.5 } };
        DR::State s{};
        DR::seed(s, simd::set1(0.0f));
        double err = 0.0;
        std::int64_t fastWon = 0, slowWon = 0;
        for (const Seg& sg : segs)
        {
            const auto n = static_cast<std::size_t>(sg.seconds * static_cast<double>(kFs));
            const auto x = static_cast<float>(sg.levelDb);
            const simd::f32x4 xv = simd::set1(x);
            const auto closed = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb({}, xv, l, a); };
            for (std::size_t k = 0; k < n; ++k)
            {
                const DualRef ref = dualRef(law, static_cast<double>(x), lane0d(s.f.r), lane0d(s.s.r), c);
                const simd::f32x4 r = DR::solveFb(c, s, closed);
                err = std::max(err, std::fabs(lane0d(r) - ref.r));
                (lane0(s.fbSlowWon) > 0.5f ? slowWon : fastWon) += 1;
                DR::commitFb(c, s, r);
            }
        }
        std::printf("NOTE     dualrelease.fb.trajectory: %lld fast-won, %lld slow-won sample(s), max |err| %.3g dB\n",
                    static_cast<long long>(fastWon), static_cast<long long>(slowWon), err);
        P.le("dualrelease.fb.trajectory.max_err_db", err, tol::kFbSolveDb);
        P.ge("dualrelease.fb.trajectory.fast_won_samples", static_cast<double>(fastWon), 1.0);
        P.ge("dualrelease.fb.trajectory.slow_won_samples", static_cast<double>(slowWon), 1.0);
    }

    // ---- the probe-local FB kernel -------------------------------------------------------------------------------------
    struct FbDualTraits
    {
        static constexpr const ModeDescriptor& desc = modes::kBusG;
        using Detector   = st::PeakLog;
        using Computer   = st::QuadKnee;
        using Link       = st::LinkMax;
        using Ballistics = st::DualRelease;
        using Stage2     = st::NoStage2;
        using Colour     = st::ColourNone;
        using ScShape    = st::Flat;
        static constexpr uint8_t kTopologies = 1u << kTopoFB;
    };
    constexpr ModeEntry kFbDualEntry = makeModeEntry<FbDualTraits>();
    static_assert(sizeof(ModeEngine<FbDualTraits>) <= kArenaBytes);

    std::int64_t reversals(std::span<const float> trace)
    {
        std::int64_t n = 0;
        for (std::size_t k = 1; k < trace.size(); ++k)
            n += trace[k] > trace[k - 1] ? 1 : 0;
        return n;
    }

    // The release after a burst of `burstS` at level `hi` (a square), dropping to `lo`: the tapped GR's 1/e time.
    struct Release
    {
        double seconds = 0, from = 0;
        std::int64_t reversals = 0, nonfinite = 0;
        double grMin = 0;
    };

    Release burstRelease(const ModeEntry& en, const EngineParams& e, double lo, double hi, double burstS,
                         double tailS)
    {
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::Segment segs[] = { { lo, 0.2 }, { hi, burstS }, { lo, tailS } };
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t b = run.edges[2];
        const std::span<const float> trace = std::span<const float>(run.tapGrDb).subspan(b);
        Release out;
        out.from = static_cast<double>(run.tapGrDb[b - 1]);
        out.seconds = measure::lawSeconds(trace, out.from, 0.0, static_cast<double>(kFs), TimeLaw::expDb);
        out.reversals = reversals(trace);
        out.nonfinite = run.nonfinite;
        for (const float v : run.tapGrDb)
            out.grMin = std::min(out.grMin, static_cast<double>(v));
        return out;
    }

    void fbEngineRows(Probe& P)
    {
        const ModeEntry& busg = fcmp::probe::modeEntry("bus-g");
        RawParams raw = fcmp::probe::modeRaw(busg);
        raw[Pid::rel] = 2400.0f;                                        // AUTO: m[0..2] = Bus G's constants
        EngineParams e = fcmp::probe::resolveRaw(busg, raw).eng;
        e.topo = kTopoFB;
        e.atkTauMs = 1.0f;
        e.voice = 0;
        const double t = static_cast<double>(analysis::inputThresholdDb(e));

        // settle on a level for 2 s: the static FB curve, flat
        fcmp::probe::EngineRig rig(kFbDualEntry, e, kFs);
        const fcmp::probe::Segment segs[] = { { t + 20.0, 2.0 } };
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const auto x = static_cast<float>(t + 20.0) + e.preGainDb;
        float want = 0.0f;
        kFbDualEntry.staticGr(e, &x, &want, 1);
        const std::size_t n = run.tapGrDb.size(), tail = static_cast<std::size_t>(0.05f * kFs);
        double lo = 1e9, hi = -1e9;
        for (std::size_t k = n - tail; k < n; ++k)
        {
            lo = std::min(lo, static_cast<double>(run.tapGrDb[k]));
            hi = std::max(hi, static_cast<double>(run.tapGrDb[k]));
        }
        const double settled = static_cast<double>(run.tapGrDb[n - 1]);
        std::printf("NOTE     dualrelease.fb.engine: settled GR %.6g dB, static FB curve %.6g dB\n", settled,
                    static_cast<double>(want));
        P.le("dualrelease.fb.engine.settled_err_db", std::fabs(settled - static_cast<double>(want)), 1e-3);
        P.le("dualrelease.fb.engine.settled_ptp_db", hi - lo, 1e-4);

        const Release shortR = burstRelease(kFbDualEntry, e, t - 20.0, t + 20.0, 0.02, 2.0);
        const Release longR = burstRelease(kFbDualEntry, e, t - 20.0, t + 20.0, 3.0, 5.0);
        std::printf("NOTE     dualrelease.fb.engine: release after 20 ms %.6g s (from %.4g dB), after 3 s %.6g s (from "
                    "%.4g dB)\n",
                    shortR.seconds, shortR.from, longR.seconds, longR.from);
        P.eq("dualrelease.fb.engine.nonfinite", run.nonfinite + shortR.nonfinite + longR.nonfinite, 0);
        P.ge("dualrelease.fb.engine.gr_min_db", std::min(shortR.grMin, longR.grMin), 0.0);
        P.eq("dualrelease.fb.engine.release_reversals", shortR.reversals + longR.reversals, 0);
        P.ge("dualrelease.fb.engine.program_ratio", shortR.seconds > 0.0 ? longR.seconds / shortR.seconds : 0.0, 4.0);
    }

    // ---- C. AutoSwitch ------------------------------------------------------------------------------------------------
    // One ballistics policy stepped like ModeEngine does: design every kTickSamples at the absolute index, FF tick or
    // FB solve + commit (the link is the identity: one lane pair, L = R).
    template <class B>
    struct Runner
    {
        typename B::Coeffs c{};
        typename B::State s{};
        std::uint64_t index = 0;
        float fs = kFs;

        Runner() { B::seed(s, simd::set1(0.0f)); }

        void designIfTick(const EngineParams& p)
        {
            if (index % static_cast<std::uint64_t>(kTickSamples) == 0)
                B::design(c, p, ctxAt(fs));
        }
        simd::f32x4 ff(const EngineParams& p, float t)
        {
            designIfTick(p);
            ++index;
            return B::tick(c, s, simd::set1(t));
        }
        template <class Solve>
        simd::f32x4 fb(const EngineParams& p, Solve&& solve)
        {
            designIfTick(p);
            ++index;
            const simd::f32x4 r = B::solveFb(c, std::as_const(s), solve);
            B::commitFb(c, s, r);
            return r;
        }
    };

    template <class X, class Y>
    std::int64_t outputMismatch(simd::f32x4 a, simd::f32x4 b, const X& ra, const Y& rb)
    {
        const bool same = lane0(a) == lane0(b) && lane0(ra.grDb()) == lane0(rb.grDb())
                       && lane0(ra.releaseNow()) == lane0(rb.releaseNow())
                       && lane0(ra.attackNow()) == lane0(rb.attackNow()) && ra.status() == rb.status();
        return same ? 0 : 1;
    }

    // Accessors so the bit-exactness rows compare AutoSwitch and a path alone through one interface.
    template <class B>
    struct View
    {
        const Runner<B>& r;
        simd::f32x4 grDb() const { return B::grDb(r.s); }
        simd::f32x4 releaseNow() const { return B::releaseNowMs(r.c, r.s); }
        simd::f32x4 attackNow() const { return B::attackNowMs(r.c, r.s); }
        uint8_t status() const { return B::status(r.s); }
    };

    // (1 - S(w)) a + S(w) b in host::blend's order (AutoSwitch.h).
    float mixRef(float a, float b, float w)
    {
        if (w <= 0.0f)
            return a;
        if (w >= 1.0f)
            return b;
        return w <= 0.5f ? a + host::rampShape(w) * (b - a) : b + host::rampShape(1.0f - w) * (a - b);
    }

    // Coefficients equal field by field (bit for bit: the floats compare with ==, which -0 / NaN never reach here).
    bool sameCoeffs(const SB::Coeffs& a, const SB::Coeffs& b)
    {
        const auto same4 = [](simd::f32x4 u, simd::f32x4 v) {
            return simd::lane<0>(u) == simd::lane<0>(v) && simd::lane<1>(u) == simd::lane<1>(v)
                && simd::lane<2>(u) == simd::lane<2>(v) && simd::lane<3>(u) == simd::lane<3>(v);
        };
        return same4(a.cA, b.cA) && same4(a.cR, b.cR) && a.tauAMs == b.tauAMs && a.tauRMs == b.tauRMs
            && a.logA == b.logA && a.logR == b.logR && a.kTick == b.kTick && a.primed == b.primed;
    }
    bool sameCoeffs(const DR::Coeffs& a, const DR::Coeffs& b)
    {
        return sameCoeffs(a.fast, b.fast) && sameCoeffs(a.slow, b.slow);
    }

    void autoSwitchRows(Probe& P)
    {
        // the selector
        {
            std::int64_t bad = 0;
            const std::pair<uint32_t, bool> cases[] = { { 0u, false },
                                                        { static_cast<uint32_t>(kTagAuto), true },
                                                        { static_cast<uint32_t>(kTagAuto2), true },
                                                        { static_cast<uint32_t>(kTagProgram), false },
                                                        { kAutoProgram, true },
                                                        { static_cast<uint32_t>(kTagOff | kTagAll), false } };
            for (const auto& [tags, useB] : cases)
            {
                EngineParams p;
                p.tags = tags;
                bad += st::AutoTagSelect::useB(p) == useB ? 0 : 1;
            }
            P.eq("dualrelease.autoswitch.select.mismatches", bad, 0);
        }

        sig::Pcg32 g(0xa5a5ull, 0x11ull);
        const std::size_t n = static_cast<std::size_t>(3.0f * kFs);
        const std::vector<float> targets = randomProgram(g, n, kFs, 0.0f, 24.0f);
        const EngineParams pA = timesOf(3.0f, 600.0f, kBusGFastMs, kBusGChargeMs, kBusGSlowMs, 0);
        const EngineParams pB = timesOf(3.0f, 2400.0f, kBusGFastMs, kBusGChargeMs, kBusGSlowMs, kAutoProgram);
        const float thr = -20.0f, slope = 0.75f, knee = 6.0f;
        const LevelCtl l = levelCtl(thr, slope, knee);

        // bit-exact against the path alone, FF and FB
        for (const bool fb : { false, true })
        {
            std::int64_t mA = 0, mB = 0;
            Runner<AS> asA, asB;
            Runner<SB> sb;
            Runner<DR> dr;
            for (std::size_t k = 0; k < n; ++k)
            {
                if (!fb)
                {
                    const simd::f32x4 a = asA.ff(pA, targets[k]), a2 = sb.ff(pA, targets[k]);
                    const simd::f32x4 b = asB.ff(pB, targets[k]), b2 = dr.ff(pB, targets[k]);
                    mA += outputMismatch(a, a2, View<AS>{ asA }, View<SB>{ sb });
                    mB += outputMismatch(b, b2, View<AS>{ asB }, View<DR>{ dr });
                }
                else
                {
                    const simd::f32x4 xv = simd::set1(thr - 10.0f + targets[k]);
                    const auto closed = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb({}, xv, l, a); };
                    const simd::f32x4 a = asA.fb(pA, closed), a2 = sb.fb(pA, closed);
                    const simd::f32x4 b = asB.fb(pB, closed), b2 = dr.fb(pB, closed);
                    mA += outputMismatch(a, a2, View<AS>{ asA }, View<SB>{ sb });
                    mB += outputMismatch(b, b2, View<AS>{ asB }, View<DR>{ dr });
                }
            }
            const std::string k = std::string("dualrelease.autoswitch.") + (fb ? "fb" : "ff");
            P.eq(k + ".a_mismatches", mA, 0);
            P.eq(k + ".b_mismatches", mB, 0);
        }

        // live switches A -> B -> A, against the documented hand-over
        const std::size_t toB = static_cast<std::size_t>(0.6f * kFs), toA = static_cast<std::size_t>(1.6f * kFs);
        static_assert(kTickSamples == 16);
        const auto paramsAt = [&](std::size_t k) { return k >= toB && k < toA ? pB : pA; };
        const std::size_t blendWant = static_cast<std::size_t>(std::lround(AS::kBlendMs * 0.001 * kFs));
        std::int64_t landsBad = 0, frozenBad = 0;
        for (const bool fb : { false, true })
        {
            Runner<AS> as;
            Runner<SB> ra;                      // the reference: both paths, stepped by hand
            Runner<DR> rb;
            float w = 0.0f;                     // B's blend position (starts on A: the program starts manual)
            std::int64_t mism = 0;
            std::size_t blending = 0;
            for (std::size_t k = 0; k < n; ++k)
            {
                const EngineParams p = paramsAt(k);
                const bool useB = (p.tags & static_cast<uint32_t>(kTagAuto)) != 0;
                const float goal = useB ? 1.0f : 0.0f;
                const simd::f32x4 xv = simd::set1(thr - 10.0f + targets[k]);
                const auto closed = [&](FbAffine a) noexcept { return st::QuadKnee::solveFb({}, xv, l, a); };

                // the combinator; its design lands / freezes the paths (checked at every tick)
                const AS::Coeffs before = as.c;
                const bool tick = k % static_cast<std::size_t>(kTickSamples) == 0;
                simd::f32x4 got{};
                if (!fb)
                    got = as.ff(p, targets[k]);
                else
                    got = as.fb(p, closed);
                if (tick && useB != before.useB)
                {
                    DR::Coeffs fresh{};
                    DR::design(fresh, p, ctxAt(kFs));
                    SB::Coeffs freshA{};
                    SB::design(freshA, p, ctxAt(kFs));
                    landsBad += useB ? (sameCoeffs(as.c.b, fresh) ? 0 : 1) : (sameCoeffs(as.c.a, freshA) ? 0 : 1);
                }
                if (tick && useB == before.useB)
                    frozenBad += useB ? (sameCoeffs(as.c.a, before.a) ? 0 : 1) : (sameCoeffs(as.c.b, before.b) ? 0 : 1);

                // the reference: design the selected path only (its Coeffs land on a switch: fresh Coeffs)
                if (tick)
                {
                    if (useB && k == toB)
                        rb.c = DR::Coeffs{};
                    if (!useB && k == toA)
                        ra.c = SB::Coeffs{};
                    if (useB)
                        DR::design(rb.c, p, ctxAt(kFs));
                    else
                        SB::design(ra.c, p, ctxAt(kFs));
                }
                ++ra.index;
                ++rb.index;
                float want = 0.0f;
                if (!fb)
                {
                    if (w != goal)
                    {
                        if (w == 0.0f)
                            DR::seed(rb.s, SB::grDb(ra.s));
                        else if (w == 1.0f)
                            SB::seed(ra.s, DR::grDb(rb.s));
                        w = goal > w ? std::min(goal, w + as.c.step) : std::max(goal, w - as.c.step);
                    }
                    const bool runA = w < 1.0f, runB = w > 0.0f;
                    const float a = runA ? lane0(SB::tick(ra.c, ra.s, simd::set1(targets[k]))) : 0.0f;
                    const float b = runB ? lane0(DR::tick(rb.c, rb.s, simd::set1(targets[k]))) : 0.0f;
                    want = !runB ? a : (!runA ? b : mixRef(a, b, w));
                }
                else
                {
                    const bool runA = w < 1.0f, runB = w > 0.0f;
                    const float a = runA ? lane0(SB::solveFb(ra.c, std::as_const(ra.s), closed)) : 0.0f;
                    const float b = runB ? lane0(DR::solveFb(rb.c, std::as_const(rb.s), closed)) : 0.0f;
                    want = !runB ? a : (!runA ? b : mixRef(a, b, w));
                    if (runA)
                        SB::commitFb(ra.c, ra.s, simd::set1(want));
                    if (runB)
                        DR::commitFb(rb.c, rb.s, simd::set1(want));
                    if (w != goal)
                    {
                        if (w == 0.0f)
                            DR::seed(rb.s, SB::grDb(ra.s));
                        else if (w == 1.0f)
                            SB::seed(ra.s, DR::grDb(rb.s));
                        w = goal > w ? std::min(goal, w + as.c.step) : std::max(goal, w - as.c.step);
                    }
                }
                mism += lane0(got) == want ? 0 : 1;
                blending += as.s.wB > 0.0f && as.s.wB < 1.0f ? 1 : 0;
            }
            const std::string k = std::string("dualrelease.autoswitch.") + (fb ? "fb" : "ff") + ".handover";
            std::printf("NOTE     %s: %zu blended sample(s) over two switches (%zu each expected)\n", k.c_str(),
                        blending, blendWant);
            P.eq(k + ".mismatches", mism, 0);
            P.near(k + ".blend_samples", static_cast<double>(blending), 2.0 * static_cast<double>(blendWant), 2.0);
        }
        P.eq("dualrelease.autoswitch.design.lands_mismatches", landsBad, 0);
        P.eq("dualrelease.autoswitch.design.frozen_mismatches", frozenBad, 0);
    }

    // ---- D. Bus G ------------------------------------------------------------------------------------------------------
    // The 1/e time of the charged cascade r_s <- r_f (both at R at t = 0, then r_f = R e^{-t / tf}): the release a
    // sustained program gets in AUTO (BusGDesc.cpp).
    double cascadeSeconds(double tf, double ts)
    {
        const auto r = [&](double t) {
            return std::max(sig::expDet(-t / tf), (ts * sig::expDet(-t / ts) - tf * sig::expDet(-t / tf)) / (ts - tf));
        };
        double lo = 0.0, hi = 20.0 * ts;
        const double target = sig::expDet(-1.0);
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (r(mid) > target ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    struct Words
    {
        float fast = 0, slow = 0, autoSlow = 0;
        uint8_t bits = 0;
    };

    // Bus G's internals and status bits after `segs` (a 1 kHz square program).
    Words busGWords(const ModeEntry& en, const EngineParams& e, std::span<const fcmp::probe::Segment> segs)
    {
        fcmp::probe::EngineRig rig(en, e, kFs);
        (void) fcmp::probe::runSquareSteps(rig, segs, 0.0);
        float out[kInternals];
        rig.engine().internals(out);
        Words w{ out[0], out[1], out[2], 0 };
        // one more sample, tapped, for the status bits
        rig.setTapping(true);
        const float x = 0.0f;
        float y = 0.0f, yr = 0.0f;
        rig.process(&x, &x, &y, &yr, 1);
        w.bits = rig.tap().bits.empty() ? uint8_t{ 0 } : rig.tap().bits.back();
        return w;
    }

    void busGRows(Probe& P)
    {
        const ModeEntry& en = fcmp::probe::modeEntry("bus-g");
        RawParams raw = fcmp::probe::modeRaw(en);
        raw[Pid::atk] = 0.1f;                                           // the fastest detent: the burst reaches its GR
        raw[Pid::rel] = 2400.0f;                                        // AUTO
        const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        std::printf("NOTE     dualrelease.busg: T %.4g dBFS, ratio slope %.4g, knee %.4g dB, AUTO m[0..2] = %.4g / %.4g "
                    "/ %.4g ms\n",
                    t, static_cast<double>(e.slope), static_cast<double>(e.kneeDb), static_cast<double>(e.m[0]),
                    static_cast<double>(e.m[1]), static_cast<double>(e.m[2]));

        // A transient's release IS tau_Rf, the span's lower end, to the float rounding of the measurement.
        constexpr double kSpanSlack = 1e-4;
        const double bursts[] = { 0.01, 0.1, 0.3, 1.0, 5.0 };
        const char* names[] = { "10ms", "100ms", "300ms", "1s", "5s" };
        std::vector<double> rel;
        std::int64_t outOfSpan = 0, nonmonotone = 0, reversalsSum = 0, nonfinite = 0;
        double grMin = 0.0;
        for (std::size_t i = 0; i < std::size(bursts); ++i)
        {
            const Release r = burstRelease(en, e, t - 20.0, t + 20.0, bursts[i], 6.0);
            std::printf("NOTE     dualrelease.busg.auto.release.%s_s = %.6g (from %.4g dB)\n", names[i], r.seconds,
                        r.from);
            rel.push_back(r.seconds);
            outOfSpan += r.seconds >= 0.1 * (1.0 - kSpanSlack) && r.seconds <= 1.2 * (1.0 + kSpanSlack) ? 0 : 1;
            nonmonotone += i > 0 && r.seconds < rel[i - 1] ? 1 : 0;
            reversalsSum += r.reversals;
            nonfinite += r.nonfinite;
            grMin = std::min(grMin, r.grMin);
        }
        const double cascade = cascadeSeconds(static_cast<double>(kBusGFastMs) / 1000.0,
                                              static_cast<double>(kBusGSlowMs) / 1000.0);
        P.eq("dualrelease.busg.auto.release.in_span", outOfSpan, 0);
        P.eq("dualrelease.busg.auto.release.nonmonotone", nonmonotone, 0);
        P.eq("dualrelease.busg.auto.release.reversals", reversalsSum, 0);
        P.eq("dualrelease.busg.auto.nonfinite", nonfinite, 0);
        P.ge("dualrelease.busg.auto.gr_min_db", grMin, 0.0);
        P.near("dualrelease.busg.auto.release.10ms_s", rel.front(), static_cast<double>(kBusGFastMs) / 1000.0, 0.0,
               0.05);
        P.near("dualrelease.busg.auto.release.5s_s", rel.back(), cascade, 0.0, 0.02);

        // internals and status bits
        const fcmp::probe::Segment slowSegs[] = { { t - 20.0, 0.2 }, { t + 20.0, 5.0 }, { t - 20.0, 0.3 } };
        const fcmp::probe::Segment fastSegs[] = { { t - 20.0, 0.2 }, { t + 20.0, 0.01 }, { t - 20.0, 0.02 } };
        const Words slowW = busGWords(en, e, slowSegs), fastW = busGWords(en, e, fastSegs);
        std::printf("NOTE     dualrelease.busg.auto.internals: after 5 s + 0.3 s FAST %.4g SLOW %.4g AUTO SLOW %g bits %u; "
                    "after 10 ms + 20 ms FAST %.4g SLOW %.4g AUTO SLOW %g bits %u\n",
                    static_cast<double>(slowW.fast), static_cast<double>(slowW.slow),
                    static_cast<double>(slowW.autoSlow), static_cast<unsigned>(slowW.bits),
                    static_cast<double>(fastW.fast), static_cast<double>(fastW.slow),
                    static_cast<double>(fastW.autoSlow), static_cast<unsigned>(fastW.bits));
        P.eq("dualrelease.busg.auto.internals.slow_holds",
             slowW.autoSlow == 1.0f && slowW.slow > slowW.fast && (slowW.bits & DR::kAutoSlowBit) != 0 ? 1 : 0, 1);
        P.eq("dualrelease.busg.auto.internals.fast_holds",
             fastW.autoSlow == 0.0f && fastW.fast > fastW.slow && (fastW.bits & DR::kAutoSlowBit) == 0 ? 1 : 0, 1);
        {
            RawParams manual = raw;
            manual[Pid::rel] = 1200.0f;
            const EngineParams em = fcmp::probe::resolveRaw(en, manual).eng;
            fcmp::probe::EngineRig rig(en, em, kFs);
            const fcmp::probe::Segment segs[] = { { t + 20.0, 0.5 } };
            const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
            float out[kInternals];
            rig.engine().internals(out);
            const float gr = run.tapGrDb.back();
            std::printf("NOTE     dualrelease.busg.manual.internals: GR %.6g dB, FAST %.6g SLOW %g AUTO SLOW %g\n",
                        static_cast<double>(gr), static_cast<double>(out[0]), static_cast<double>(out[1]),
                        static_cast<double>(out[2]));
            P.eq("dualrelease.busg.manual.internals", out[0] == gr && out[1] == 0.0f && out[2] == 0.0f ? 1 : 0, 1);
        }

        // VcaBus through the entry's colourCurve
        const auto harmonics = [&](const EngineParams& ev, double amp, int maxH) {
            constexpr int kN = 256;
            std::vector<float> x(kN), y(kN);
            for (int i = 0; i < kN; ++i)
                x[static_cast<std::size_t>(i)] = static_cast<float>(amp * sig::sinTurns(static_cast<double>(i) / kN));
            en.colourCurve(ev, 0.0f, x.data(), y.data(), kN);
            std::vector<double> h(static_cast<std::size_t>(maxH) + 1, 0.0);
            for (int k = 1; k <= maxH; ++k)
            {
                double re = 0.0, im = 0.0;
                for (int i = 0; i < kN; ++i)
                {
                    const double ph = static_cast<double>((k * i) % kN) / kN;
                    re += static_cast<double>(y[static_cast<std::size_t>(i)]) * sig::cosTurns(ph);
                    im += static_cast<double>(y[static_cast<std::size_t>(i)]) * sig::sinTurns(ph);
                }
                h[static_cast<std::size_t>(k)] = 2.0 * std::sqrt(re * re + im * im) / kN;
            }
            return h;
        };
        RawParams vca = fcmp::probe::modeRaw(en);
        const EngineParams e0 = fcmp::probe::resolveRaw(en, vca).eng;
        vca[Pid::drive] = 12.0f;
        const EngineParams e12 = fcmp::probe::resolveRaw(en, vca).eng;
        const std::vector<double> h0 = harmonics(e0, 1.0, 3), h12 = harmonics(e12, 1.0, 3), h6 = harmonics(e0, 2.0, 1);
        const double h2Db = measure::dbFromAmplitude(std::max(h0[2], h12[2]) / h0[1]);
        const double h3Db = measure::dbFromAmplitude(h0[3] / h0[1]), h3Db12 = measure::dbFromAmplitude(h12[3] / h12[1]);
        const double df6 = measure::dbFromAmplitude(h6[1] / 2.0);
        float small = 0.0f;
        const float tiny = 1e-3f;
        en.colourCurve(e0, 0.0f, &tiny, &small, 1);
        std::printf("NOTE     dualrelease.busg.vca: voice %u, drive %.3g / %.3g dB: H2 %.4g dB, H3 %.4g / %.4g dB re H1 "
                    "(0 dBFS); +6 dBFS fundamental %.4g dB\n",
                    static_cast<unsigned>(e0.voice), static_cast<double>(e0.driveDb), static_cast<double>(e12.driveDb),
                    h2Db, h3Db, h3Db12, df6);
        P.eq("dualrelease.busg.vca.voice", e0.voice, 0);
        P.le("dualrelease.busg.vca.h2_db", h2Db, -140.0);
        P.near("dualrelease.busg.vca.h3_0dbfs_db", h3Db, -68.6, 0.3);
        P.near("dualrelease.busg.vca.h3_drive12_db", h3Db12, -44.6, 0.3);
        P.le("dualrelease.busg.vca.df_6dbfs_db", std::fabs(df6), 0.05);
        P.near("dualrelease.busg.vca.unity_small_signal", static_cast<double>(small / tiny), 1.0, 1e-6);
    }
} // namespace

FCMP_PROBE(dsp, dualrelease)
{
    (void) C;
    ffRows(P);
    fbSolveRows(P);
    fbTrajectoryRows(P);
    fbEngineRows(P);
    autoSwitchRows(P);
    busGRows(P);
    return P.finish();
}
