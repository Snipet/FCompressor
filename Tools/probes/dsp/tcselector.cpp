// FCMP_PROBE layer=dsp name=tcselector scope=global timeout=180
//
// dsp.tcselector (M4, S10; SPRINTS S10.1; 01 §5.2-5.3, §10.7; E §2.5d-e, §2.6-2.7; D §2.3; K2 #1; S10 X10 and lead
// revision 3): the ballistics Mu 67 adds, stage::MultiStage3 (the programme-dependent network of TC5 / TC6) and
// stage::TcSelector (AutoSwitch<SmoothBranching, MultiStage3, TcSelect>), proven on the policy functions (unit rows,
// 01 §8.4 step 3) and through the registered `mu-67` engine (EngineRig, 48 kHz unless noted). Spec rows only (no
// golden). The gain computer of the FB rows is Mu 67's, stage::ProgressiveKnee (dsp.progressiveknee proves its solve);
// "the float curve" is its r^_fb evaluated at a double level. Reference numbers are double precision on the policies'
// own float rates.
//
// TcSelector:
//   tcselector.select.mismatches          TcSelect::useB is true exactly on the TC5 / TC6 tags, and Mu 67's TIME steps
//                                         resolve to them on steps 5 and 6 only
//   tcselector.identity.<ff|fb>.mismatches  with the selection constant (TC6), TcSelector is bit-identical to
//                                         MultiStage3 alone over a random programme (outputs and grDb, every sample)
// MultiStage3, feed-forward (TC5 and TC6 constants, 48 and 192 kHz, a random target programme):
//   multistage3.ff.<tc>.<fs>.model_max_err_db  the policy against the documented recurrence in double (stage 1 smooth
//                                         branching toward the target; stage 2 a one-pole toward stage 1, charging
//                                         while stage 1 is above it; stage 3 likewise toward stage 2; the max):
//                                         <= 1e-3 dB (the long-release precision dsp.srsweep holds)
// MultiStage3, feedback (K2 #1; 20,000 random states and levels per network: T in [-40, 0], DC THRESH law, stage GRs in
// [0, 40] dB in any order, x in [T - 20, T + 50], 48 or 192 kHz):
//   multistage3.fb.branch_mismatches      the branches solveFb used follow the documented predictors, recomputed here
//                                         from independent solves: stage 1 attacks where its attack root exceeds r1,
//                                         stage 2 charges where stage 1's root exceeds r2, stage 3 where stage 2's
//                                         value (its follow value where it charges, else its root) exceeds r3
//   multistage3.fb.solve.max_err_db       the returned GR against 200-step bisection of r = max(g1(r), g2(r), g3(r))
//                                         with those maps, on the float curve: <= 1e-5 dB; .winner<i>.count >= 1 for
//                                         each stage (TC6; TC5 has no stage 3); .charge_won 0 (a charging stage never
//                                         wins)
//   multistage3.fb.commit.max_err_db      commitFb with r^ at the returned GR: every stage but the winner takes its
//                                         map's value at r (capped at r), the winner r: <= 1e-5 dB
//   multistage3.fb.carry.<fs>.max_dev_db  the S10 sub-ulp carry: TC6's network from a charged state (every stage at
//                                         20 dB) releasing with the loop open (x far below T) for 60 s at 48 kHz and
//                                         30 s at 384 kHz, through solveFb / commitFb with ProgressiveKnee's solve:
//                                         every stage against the exact three-stage recurrence in double: <= 1e-4 dB
//                                         (a NOTE gives the plain float recurrence's deviation, the stall the carry
//                                         removes)
// Mu 67 (the registered engine):
//   tcselector.mu67.program.<tc>.b<ms>_s  NOTE: the release (t63 of the GR's dB fall, TimeLaw::expDb as the TIME spec
//                                         declares) after a burst of 20 ms, 200 ms, 1, 2, 5 and 20 s at T + 20 dB;
//                                         .<tc>.nonmonotone 0 (a longer burst never releases faster); D §2.3's
//                                         published recoveries within 15 %: after a peak (.<tc>.peak_s: 2 s / 0.3 s,
//                                         the 20 ms burst), after multiple peaks (.tc6.multiple_s: 10 s, the 2 s burst)
//                                         and after consistently high programme (.<tc>.programme_s: 10 s / 25 s, the
//                                         20 s burst)
//   tcselector.mu67.tc6.tail_s            NOTE and spec: after the 20 s burst, the time for the GR to fall to 1/e^2 of
//                                         its start is >= 25 s (the slow stages hold the tail)
//   tcselector.mu67.weight.*              TC WEIGHT (the share the programme stages hold): >= 0.5 after the 20 s
//                                         burst plus 2 s of release, < 0.5 after the 20 ms burst plus 0.3 s
//   tcselector.mu67.seeded.<fs>.max_dev_db  the Mode's engine at TC6 seeded (Carry) with 20 dB in every stage, the loop
//                                         open (a square 20 dB under T): the tapped GR against the exact recurrence's
//                                         max in double for 60 s at 48 kHz and 30 s at 384 kHz: <= 1e-4 dB
//   tcselector.mu67.switch.max_step_db    TIME switched TC4 -> TC6 -> TC4 while releasing: no per-sample GR step over
//                                         0.01 dB within 25 ms of either switch (AutoSwitch's 20 ms hand-over)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/MultiStage3.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/ballistics/TcSelector.h"
#include "fcdsp/engine/stages/gain/ProgressiveKnee.h"
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
#include <tuple>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace st = fcdsp::stage;
    namespace tol = fcmp::probe::tol;
    namespace measure = fcmp::probe::measure;
    namespace sig = fcmp::probe::sig;

    using MS = st::MultiStage3;
    using PK = st::ProgressiveKnee;

    constexpr float kFs = 48000.0f;

    // The networks (Mu67Desc.cpp, docs/modes/mu-67.md), ms: tau_R1, tau_C2, tau_R2, tau_C3, tau_R3; the TC attack.
    struct Net
    {
        const char* name;
        float rel1, charge2, rel2, charge3, rel3, attack;
    };
    constexpr Net kTc5{ "tc5", 2000.f, 3000.f, 7700.f, 0.f, 0.f, 0.4f };
    constexpr Net kTc6{ "tc6", 300.f, 1000.f, 8000.f, 8000.f, 16000.f, 0.2f };

    float lane0(simd::f32x4 v) { return simd::lane<0>(v); }

    EngineParams netParams(const Net& n, float atkMs)
    {
        EngineParams p;
        p.atkTauMs = atkMs;
        p.relTauMs = n.rel3 > 0.f ? n.rel3 : n.rel2;
        p.m[2] = n.rel1;
        p.m[3] = n.charge2;
        p.m[4] = n.rel2;
        p.m[5] = n.charge3;
        p.m[6] = n.rel3;
        p.tags = n.rel3 > 0.f ? kTagTc6 : kTagTc5;
        return p;
    }

    MS::Coeffs design(const EngineParams& p, float fs)
    {
        MS::Coeffs c{};
        MS::design(c, p, StageCtx{ fs, fs, 1, {} });
        return c;
    }

    // ---- TcSelector ------------------------------------------------------------------------------------------------
    void selectRows(Probe& P)
    {
        std::int64_t mismatches = 0;
        for (std::uint32_t tags = 0; tags < 256; ++tags)
        {
            EngineParams p;
            p.tags = tags;
            const bool want = (tags & (kTagTc5 | kTagTc6)) != 0;
            mismatches += st::TcSelect::useB(p) == want ? 0 : 1;
        }
        const ModeEntry& en = fcmp::probe::modeEntry("mu-67");
        const RawParams base = fcmp::probe::modeRaw(en);
        ParamView view;
        resolveView(*en.desc, base, view);
        const ParamSpec* rel = view.spec[idx(Pid::rel)];
        for (std::size_t i = 0; rel != nullptr && i < rel->steps.size(); ++i)
        {
            RawParams raw = base;
            raw[Pid::rel] = rel->steps[i].plain;
            const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
            mismatches += st::TcSelect::useB(e) == (i >= 4) ? 0 : 1;
        }
        P.eq("tcselector.select.mismatches", mismatches, 0);
    }

    void identityRows(Probe& P)
    {
        const EngineParams p = netParams(kTc6, 1.0f);
        st::TcSelector::Coeffs tc{};
        st::TcSelector::design(tc, p, StageCtx{ kFs, kFs, 1, {} });
        const MS::Coeffs mc = design(p, kFs);
        sig::Pcg32 g(0x69646e74, 3);
        // FF
        {
            st::TcSelector::State a{};
            MS::State b{};
            std::int64_t mismatches = 0;
            float t = 0.0f;
            for (int i = 0; i < 48000; ++i)
            {
                if (i % 2400 == 0)
                    t = 30.0f * g.uniform();
                const simd::f32x4 tv = simd::set1(t);
                const simd::f32x4 ra = st::TcSelector::tick(tc, a, tv), rb = MS::tick(mc, b, tv);
                mismatches += lane0(ra) == lane0(rb) && lane0(st::TcSelector::grDb(a)) == lane0(MS::grDb(b)) ? 0 : 1;
            }
            P.eq("tcselector.identity.ff.mismatches", mismatches, 0);
        }
        // FB (ProgressiveKnee, 5:1 asymptote, o_c 10 dB at T -30)
        {
            PK::Coeffs kc;
            kc.onsetDb = 10.0f;
            const LevelCtl l{ simd::set1(-30.0f), simd::set1(0.8f), simd::set1(0.0f), simd::set1(kS2Off) };
            st::TcSelector::State a{};
            MS::State b{};
            std::int64_t mismatches = 0;
            float x = -40.0f;
            for (int i = 0; i < 48000; ++i)
            {
                if (i % 2400 == 0)
                    x = -50.0f + 60.0f * g.uniform();
                const simd::f32x4 xv = simd::set1(x);
                const auto solve = [&](FbAffine fa) noexcept { return PK::solveFb(kc, xv, l, fa); };
                const simd::f32x4 ra = st::TcSelector::solveFb(tc, a, solve), rb = MS::solveFb(mc, b, solve);
                st::TcSelector::commitFb(tc, a, ra, PK::rhatFb(kc, simd::sub(xv, ra), l));
                MS::commitFb(mc, b, rb, PK::rhatFb(kc, simd::sub(xv, rb), l));
                mismatches += lane0(ra) == lane0(rb) && lane0(st::TcSelector::grDb(a)) == lane0(MS::grDb(b)) ? 0 : 1;
            }
            P.eq("tcselector.identity.fb.mismatches", mismatches, 0);
        }
    }

    // ---- MultiStage3, feed-forward ---------------------------------------------------------------------------------
    void ffRows(Probe& P)
    {
        for (const Net& n : { kTc5, kTc6 })
            for (const float fs : { 48000.0f, 192000.0f })
            {
                const EngineParams p = netParams(n, n.attack);
                const MS::Coeffs c = design(p, fs);
                const double cA = static_cast<double>(lane0(c.s1.cA)), cR1 = static_cast<double>(lane0(c.s1.cR));
                const double cC2 = static_cast<double>(lane0(c.s2.cA)), cR2 = static_cast<double>(lane0(c.s2.cR));
                const double cC3 = static_cast<double>(lane0(c.s3.cA)), cR3 = static_cast<double>(lane0(c.s3.cR));
                MS::State s{};
                double r1 = 0, r2 = 0, r3 = 0, worst = 0;
                sig::Pcg32 g(0x66666f72, static_cast<std::uint64_t>(fs));
                float t = 0.0f;
                const std::size_t total = static_cast<std::size_t>(40.0f * fs);
                for (std::size_t i = 0; i < total; ++i)
                {
                    if (i % static_cast<std::size_t>(0.5f * fs) == 0)
                        t = g.uniform() < 0.4f ? 0.0f : 30.0f * g.uniform();
                    const double td = static_cast<double>(t);
                    r1 += (td > r1 ? cA : cR1) * (td - r1);
                    r2 += (r1 > r2 ? cC2 : cR2) * (r1 - r2);
                    r3 += (r2 > r3 ? cC3 : cR3) * (r2 - r3);
                    const double want = std::max(r1, std::max(r2, r3));
                    const double got = static_cast<double>(lane0(MS::tick(c, s, simd::set1(t))));
                    worst = std::max(worst, std::fabs(got - want));
                }
                P.le(std::string("multistage3.ff.") + n.name + "." + std::to_string(static_cast<long>(fs))
                         + ".model_max_err_db",
                     worst, 1e-3);
            }
    }

    // ---- MultiStage3, feedback -------------------------------------------------------------------------------------
    struct FbCase
    {
        PK::Coeffs kc;
        LevelCtl l;
        float thr;
        float x;
    };

    double fbCurve(const FbCase& k, double y)
    {
        return static_cast<double>(lane0(PK::rhatFb(k.kc, simd::set1(static_cast<float>(y)), k.l)));
    }

    void fbRows(Probe& P)
    {
        for (const Net& n : { kTc5, kTc6 })
        {
            const bool three = n.rel3 > 0.f;
            std::int64_t branchMismatch = 0, chargeWon = 0;
            std::array<std::int64_t, 3> winners{};
            double solveErr = 0.0, commitErr = 0.0;
            sig::Pcg32 g(0x66627473, three ? 6u : 5u);
            for (int i = 0; i < 20000; ++i)
            {
                const float fs = i % 2 == 0 ? 48000.0f : 192000.0f;
                const MS::Coeffs c = design(netParams(n, n.attack * (1.0f + 20.0f * g.uniform())), fs);
                FbCase k;
                k.thr = -40.0f * g.uniform();
                const float knee = 40.0f * g.uniform();
                k.kc.onsetDb = 0.5f;
                k.kc.onsetPerDb = 1.0f;
                k.l = LevelCtl{ simd::set1(k.thr), simd::set1(1.0f - 1.0f / (20.0f - 0.25f * knee)), simd::set1(knee),
                                simd::set1(kS2Off) };
                k.x = k.thr - 20.0f + 70.0f * g.uniform();
                MS::State s{};
                MS::seed(s, simd::set1(0.0f));
                s.p1.r = simd::set1(40.0f * g.uniform());
                s.p2.r = simd::set1(40.0f * g.uniform());
                s.p3.r = three ? simd::set1(40.0f * g.uniform()) : s.p2.r;
                const MS::State before = s;
                const simd::f32x4 xv = simd::set1(k.x);
                const auto solve = [&](FbAffine fa) noexcept { return PK::solveFb(k.kc, xv, k.l, fa); };
                const float r = lane0(MS::solveFb(c, s, solve));

                // the predictors, recomputed from independent solves (header comment), in the policy's own float
                // arithmetic (fused steps), so a verdict at an exact tie is the same one
                const simd::f32x4 r1v = before.p1.r, r2v = before.p2.r, r3v = before.p3.r;
                const float r1 = lane0(r1v), r2 = lane0(r2v), r3 = lane0(r3v);
                const simd::f32x4 aAv = simd::fms(r1v, c.s1.cA, r1v), aRv = simd::fms(r1v, c.s1.cR, r1v);
                const float rootA = lane0(solve(FbAffine{ aAv, c.s1.cA }));
                const float rootR = lane0(solve(FbAffine{ aRv, c.s1.cR }));
                const bool attack = rootA > r1;
                const float root1 = attack ? rootA : rootR;
                const float k1 = attack ? lane0(c.s1.cA) : lane0(c.s1.cR);
                const simd::f32x4 a1v = attack ? aAv : aRv;
                const bool ch2 = root1 > r2;
                const simd::f32x4 c2v = ch2 ? c.s2.cA : c.s2.cR;
                const float c2 = lane0(c2v);
                const simd::f32x4 keep2 = simd::fms(r2v, c2v, r2v);
                const simd::f32x4 a2v = simd::fma(keep2, c2v, a1v), b2v = simd::mul(c2v, simd::set1(k1));
                const float root2 = ch2 ? root1 : lane0(solve(FbAffine{ a2v, b2v }));
                const float v2 = ch2 ? lane0(simd::fma(keep2, c2v, simd::set1(root1))) : root2;
                const bool ch3 = three && v2 > r3;
                const float c3 = !three ? 1.0f : (ch3 ? lane0(c.s3.cA) : lane0(c.s3.cR));
                const double a1 = static_cast<double>(lane0(a1v)), a2 = static_cast<double>(lane0(a2v));
                const double b2 = static_cast<double>(lane0(b2v));
                branchMismatch += lane0(s.fbK1) == k1 && lane0(s.fbC2) == c2 && lane0(s.fbC3) == c3 ? 0 : 1;

                // the maps in double and the bisection of r = max(g1, g2, g3)
                const double a3 =
                    (1.0 - static_cast<double>(c3)) * static_cast<double>(r3) + static_cast<double>(c3) * a2;
                const double b3 = static_cast<double>(c3) * b2;
                const std::array<double, 3> A{ a1, a2, a3 }, B{ static_cast<double>(k1), b2, b3 };
                const int maps = three ? 3 : 2;
                const auto g3 = [&](double rr) {
                    const double rh = fbCurve(k, static_cast<double>(k.x) - rr);
                    double m = 0.0;
                    for (int j = 0; j < maps; ++j)
                        m = std::max(m, A[static_cast<std::size_t>(j)] + B[static_cast<std::size_t>(j)] * rh);
                    return m;
                };
                double lo = 0.0, hi = std::max(0.0, g3(0.0));
                for (int it = 0; it < tol::kFbBisectionSteps && hi > lo; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    (mid - g3(mid) <= 0.0 ? lo : hi) = mid;
                }
                solveErr = std::max(solveErr, std::fabs(static_cast<double>(r) - 0.5 * (lo + hi)));
                const int win = static_cast<int>(lane0(s.fbWin));
                winners[static_cast<std::size_t>(std::clamp(win, 1, 3) - 1)] += 1;
                chargeWon += (win == 2 && ch2) || (win == 3 && ch3) ? 1 : 0;

                // the commit with r^ at r: the maps' values (the winner r), capped at r
                const float rhat = lane0(PK::rhatFb(k.kc, simd::set1(k.x - r), k.l));
                MS::commitFb(c, s, simd::set1(r), simd::set1(rhat));
                const std::array<float, 3> got{ lane0(s.p1.r), lane0(s.p2.r), lane0(s.p3.r) };
                double v1 = std::min(a1 + static_cast<double>(k1) * static_cast<double>(rhat), static_cast<double>(r));
                if (win == 1)
                    v1 = static_cast<double>(r);
                double v2n = std::min((1.0 - static_cast<double>(c2)) * static_cast<double>(r2)
                                          + static_cast<double>(c2) * static_cast<double>(got[0]),
                                      static_cast<double>(r));
                if (win == 2)
                    v2n = static_cast<double>(r);
                double v3n = !three ? static_cast<double>(got[1])
                                    : std::min((1.0 - static_cast<double>(c3)) * static_cast<double>(r3)
                                                   + static_cast<double>(c3) * static_cast<double>(got[1]),
                                               static_cast<double>(r));
                if (win == 3)
                    v3n = static_cast<double>(r);
                commitErr = std::max({ commitErr, std::fabs(static_cast<double>(got[0]) - v1),
                                       std::fabs(static_cast<double>(got[1]) - v2n),
                                       std::fabs(static_cast<double>(got[2]) - v3n) });
            }
            const std::string k = std::string("multistage3.fb.") + n.name + ".";
            std::printf("NOTE     %swinners: stage 1 %lld, stage 2 %lld, stage 3 %lld\n", k.c_str(),
                        static_cast<long long>(winners[0]), static_cast<long long>(winners[1]),
                        static_cast<long long>(winners[2]));
            P.eq(k + "branch_mismatches", branchMismatch, 0);
            P.le(k + "solve.max_err_db", solveErr, tol::kFbSolveDb);
            P.ge(k + "solve.winner1.count", static_cast<double>(winners[0]), 1.0);
            P.ge(k + "solve.winner2.count", static_cast<double>(winners[1]), 1.0);
            if (three)
                P.ge(k + "solve.winner3.count", static_cast<double>(winners[2]), 1.0);
            P.eq(k + "solve.charge_won", chargeWon, 0);
            P.le(k + "commit.max_err_db", commitErr, tol::kFbSolveDb);
        }
    }

    // The exact open-loop release of the TC6 network from (g, g, g) in double, and the plain float recurrence.
    struct Chain
    {
        double r1, r2, r3;
        void step(double c1, double c2, double c3) noexcept
        {
            r1 -= c1 * r1;
            r2 += c2 * (r1 - r2);
            r3 += c3 * (r2 - r3);
        }
        double max() const noexcept { return std::max(r1, std::max(r2, r3)); }
    };

    void carryRows(Probe& P)
    {
        for (const float fs : { 48000.0f, 384000.0f })
        {
            const MS::Coeffs c = design(netParams(kTc6, kTc6.attack), fs);
            const double c1 = static_cast<double>(lane0(c.s1.cR)), c2 = static_cast<double>(lane0(c.s2.cR)),
                         c3 = static_cast<double>(lane0(c.s3.cR));
            PK::Coeffs kc;
            kc.onsetDb = 10.0f;
            const LevelCtl l{ simd::set1(-20.0f), simd::set1(0.9f), simd::set1(0.0f), simd::set1(kS2Off) };
            const simd::f32x4 xv = simd::set1(-80.0f);                  // the loop open: r^_fb = 0
            MS::State s{};
            MS::seed(s, simd::set1(20.0f));
            Chain exact{ 20.0, 20.0, 20.0 };
            float f1 = 20.0f, f2 = 20.0f, f3 = 20.0f;                   // the plain float recurrence (NOTE)
            double worst = 0.0, worstPlain = 0.0;
            const auto n = static_cast<std::size_t>((fs > 100000.0f ? 30.0f : 60.0f) * fs);
            const auto fc1 = static_cast<float>(c1), fc2 = static_cast<float>(c2), fc3 = static_cast<float>(c3);
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto solve = [&](FbAffine fa) noexcept { return PK::solveFb(kc, xv, l, fa); };
                const simd::f32x4 r = MS::solveFb(c, s, solve);
                MS::commitFb(c, s, r, PK::rhatFb(kc, simd::sub(xv, r), l));
                exact.step(c1, c2, c3);
                f1 = f1 - fc1 * f1;
                f2 = f2 + fc2 * (f1 - f2);
                f3 = f3 + fc3 * (f2 - f3);
                worst = std::max({ worst, std::fabs(static_cast<double>(lane0(s.p1.r)) - exact.r1),
                                   std::fabs(static_cast<double>(lane0(s.p2.r)) - exact.r2),
                                   std::fabs(static_cast<double>(lane0(s.p3.r)) - exact.r3) });
                worstPlain = std::max(worstPlain, std::fabs(static_cast<double>(std::max(f1, std::max(f2, f3)))
                                                            - exact.max()));
            }
            const std::string k = "multistage3.fb.carry." + std::to_string(static_cast<long>(fs));
            std::printf("NOTE     %s: carried %.3g dB from the exact recurrence (the plain float recurrence: "
                        "%.3g dB)\n",
                        k.c_str(), worst, worstPlain);
            P.le(k + ".max_dev_db", worst, 1e-4);
        }
    }

    // ---- Mu 67 -----------------------------------------------------------------------------------------------------
    EngineParams muParams(const ModeEntry& en, std::size_t tcStep)
    {
        RawParams raw = fcmp::probe::modeRaw(en);
        ParamView view;
        resolveView(*en.desc, raw, view);
        raw[Pid::rel] = view.spec[idx(Pid::rel)]->steps[tcStep].plain;
        return fcmp::probe::resolveRaw(en, raw).eng;
    }

    // A burst of `burstS` at T + 20 after 0.2 s at T - 20, then T - 20 for up to `maxRelS`: the tapped GR from the
    // burst's end, and the engine's internals `afterS` into the release (-1: not read).
    struct Burst
    {
        std::vector<float> rel;
        float weight = 0.0f;
    };
    Burst burst(const ModeEntry& en, const EngineParams& e, double burstS, double maxRelS, double afterS)
    {
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::Segment pre[] = { { t - 20.0, 0.2 }, { t + 20.0, burstS } };
        (void) fcmp::probe::runSquareSteps(rig, pre, 0.0);
        Burst b;
        rig.setTapping(true);
        const auto amp = static_cast<float>(measure::amplitudeFromDb(t - 20.0));
        const auto total = static_cast<std::size_t>(maxRelS * static_cast<double>(kFs));
        const auto readAt = afterS < 0.0 ? total : static_cast<std::size_t>(afterS * static_cast<double>(kFs));
        constexpr std::size_t kBlock = 4800;
        std::vector<float> in(kBlock), yl(kBlock), yr(kBlock);
        float start = -1.0f;
        for (std::size_t off = 0; off < total; off += kBlock)
        {
            const std::size_t m = std::min(kBlock, total - off);
            const std::size_t idx0 = static_cast<std::size_t>(rig.sampleIndex());
            for (std::size_t k = 0; k < m; ++k)
                in[k] = ((idx0 + k) / 24) % 2 == 0 ? amp : -amp;
            rig.process(in.data(), in.data(), yl.data(), yr.data(), m);
            const std::vector<float> g = rig.tap().lane(rig.tap().grDb, 0);
            rig.tap().clear();
            b.rel.insert(b.rel.end(), g.begin(), g.end());
            if (start < 0.0f && !b.rel.empty())
                start = b.rel.front();
            if (off <= readAt && readAt < off + m)
            {
                float words[kInternals];
                rig.engine().internals(words);
                b.weight = words[2];
            }
            if (!b.rel.empty() && b.rel.back() < 0.05f * std::max(start, 1e-3f))
                break;
        }
        return b;
    }

    void programRows(Probe& P, const ModeEntry& en)
    {
        const double bursts[] = { 0.02, 0.2, 1.0, 2.0, 5.0, 20.0 };
        // the published recoveries (D §2.3): after a peak, after multiple peaks (2 s of programme; TC6 only) and after
        // consistently high programme (the 20 s burst)
        for (const auto& [step, name, peak, multiple, programme] :
             { std::tuple{ std::size_t{ 4 }, "tc5", 2.0, 0.0, 10.0 },
               std::tuple{ std::size_t{ 5 }, "tc6", 0.3, 10.0, 25.0 } })
        {
            const EngineParams e = muParams(en, step);
            const std::string k = std::string("tcselector.mu67.program.") + name;
            double prev = 0.0;
            std::int64_t nonmonotone = 0;
            for (const double b : bursts)
            {
                const Burst run = burst(en, e, b, 150.0, -1.0);
                const std::span<const float> tr(run.rel);
                const double from = static_cast<double>(run.rel.front());
                const double t63 =
                    measure::lawSeconds(tr.subspan(1), from, 0.0, static_cast<double>(kFs), TimeLaw::expDb);
                std::printf("NOTE     %s.b%ld_s: burst %.3g s, GR %.4g dB, release (t63) %.4g s\n", k.c_str(),
                            std::lround(1000.0 * b), b, from, t63);
                nonmonotone += t63 + 1e-6 < prev ? 1 : 0;
                prev = t63;
                if (b == bursts[0])
                    P.near(k + ".peak_s", t63, peak, 0.0, 0.15);
                if (b == 2.0 && multiple > 0.0)
                    P.near(k + ".multiple_s", t63, multiple, 0.0, 0.15);
                if (b == 20.0)
                    P.near(k + ".programme_s", t63, programme, 0.0, 0.15);
                if (step == 5 && b == 20.0)
                {
                    const double tail = measure::crossingSeconds(tr.subspan(1), from, 0.0, 1.0 - std::exp(-2.0),
                                                                 static_cast<double>(kFs));
                    std::printf("NOTE     tcselector.mu67.tc6.tail_s: %.4g s to 1/e^2 of the GR\n", tail);
                    P.ge("tcselector.mu67.tc6.tail_s", tail, 25.0);
                }
            }
            P.eq(k + ".nonmonotone", nonmonotone, 0);
        }

        const EngineParams tc6 = muParams(en, 5);
        const Burst held = burst(en, tc6, 20.0, 2.1, 2.0), peak = burst(en, tc6, 0.02, 0.4, 0.3);
        std::printf("NOTE     tcselector.mu67.weight: TC WEIGHT %.3g after 20 s + 2 s, %.3g after 20 ms + 0.3 s\n",
                    static_cast<double>(held.weight), static_cast<double>(peak.weight));
        P.ge("tcselector.mu67.weight.programme", static_cast<double>(held.weight), 0.5);
        P.le("tcselector.mu67.weight.peak", static_cast<double>(peak.weight), 0.5);
    }

    void seededRows(Probe& P, const ModeEntry& en)
    {
        const EngineParams e = muParams(en, 5);
        for (const float fs : { 48000.0f, 384000.0f })
        {
            const MS::Coeffs c = design(e, fs);
            const double c1 = static_cast<double>(lane0(c.s1.cR)), c2 = static_cast<double>(lane0(c.s2.cR)),
                         c3 = static_cast<double>(lane0(c.s3.cR));
            fcmp::probe::EngineRig rig(en, e, fs);
            Carry carry;
            carry.grDb = simd::set1(20.0f);
            carry.detDb = simd::set1(e.thrDb - 20.0f);
            carry.valid = 1;
            rig.engine().seed(carry);
            rig.setTapping(true);
            Chain exact{ 20.0, 20.0, 20.0 };
            const auto amp = static_cast<float>(measure::amplitudeFromDb(static_cast<double>(e.thrDb) - 20.0));
            const auto total = static_cast<std::size_t>((fs > 100000.0f ? 30.0f : 60.0f) * fs);
            const auto half = static_cast<std::size_t>(fs / 2000.0f);
            constexpr std::size_t kBlock = 8192;
            std::vector<float> in(kBlock), yl(kBlock), yr(kBlock);
            double worst = 0.0;
            for (std::size_t off = 0; off < total; off += kBlock)
            {
                const std::size_t m = std::min(kBlock, total - off);
                for (std::size_t k = 0; k < m; ++k)
                    in[k] = ((off + k) / half) % 2 == 0 ? amp : -amp;
                rig.process(in.data(), in.data(), yl.data(), yr.data(), m);
                const std::vector<float> g = rig.tap().lane(rig.tap().grDb, 0);
                rig.tap().clear();
                for (std::size_t k = 0; k < m; ++k)
                {
                    exact.step(c1, c2, c3);
                    worst = std::max(worst, std::fabs(static_cast<double>(g[k]) - exact.max()));
                }
            }
            const std::string k = "tcselector.mu67.seeded." + std::to_string(static_cast<long>(fs));
            std::printf("NOTE     %s: %.3g dB from the exact recurrence\n", k.c_str(), worst);
            P.le(k + ".max_dev_db", worst, 1e-4);
        }
    }

    void switchRows(Probe& P, const ModeEntry& en)
    {
        const EngineParams tc4 = muParams(en, 3), tc6 = muParams(en, 5);
        const double t = static_cast<double>(analysis::inputThresholdDb(tc4));
        fcmp::probe::EngineRig rig(en, tc4, kFs);
        const fcmp::probe::Segment pre[] = { { t + 20.0, 2.0 } };
        (void) fcmp::probe::runSquareSteps(rig, pre, 0.0);
        rig.setTapping(true);
        const auto amp = static_cast<float>(measure::amplitudeFromDb(t - 20.0));
        std::vector<float> gr;
        const std::size_t seg = static_cast<std::size_t>(0.5f * kFs);
        std::vector<float> in(seg), yl(seg), yr(seg);
        const EngineParams* order[] = { &tc4, &tc6, &tc4 };
        std::vector<std::size_t> edges;
        for (const EngineParams* p : order)
        {
            if (!gr.empty())
                edges.push_back(gr.size());
            rig.setParams(*p);
            const std::size_t idx0 = static_cast<std::size_t>(rig.sampleIndex());
            for (std::size_t k = 0; k < seg; ++k)
                in[k] = ((idx0 + k) / 24) % 2 == 0 ? amp : -amp;
            rig.process(in.data(), in.data(), yl.data(), yr.data(), seg);
            const std::vector<float> g = rig.tap().lane(rig.tap().grDb, 0);
            rig.tap().clear();
            gr.insert(gr.end(), g.begin(), g.end());
        }
        double worst = 0.0;
        const auto win = static_cast<std::size_t>(0.025f * kFs);
        for (const std::size_t e : edges)
            for (std::size_t i = e - win; i < e + win; ++i)
                worst = std::max(worst, std::fabs(static_cast<double>(gr[i]) - static_cast<double>(gr[i - 1])));
        std::printf("NOTE     tcselector.mu67.switch: largest per-sample GR step near the switches %.3g dB\n", worst);
        P.le("tcselector.mu67.switch.max_step_db", worst, 0.01);
    }
} // namespace

FCMP_PROBE(dsp, tcselector)
{
    selectRows(P);
    identityRows(P);
    ffRows(P);
    fbRows(P);
    carryRows(P);

    const ModeEntry& en = fcmp::probe::modeEntry("mu-67");
    programRows(P, en);
    seededRows(P, en);
    switchRows(P, en);
    return P.finish();
}
