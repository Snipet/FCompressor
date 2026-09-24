// FCMP_PROBE layer=dsp name=progressiveknee scope=global timeout=180
//
// dsp.progressiveknee (M4, S10; SPRINTS S10.1; 01 §5.2-5.3, §10.7; E §2.2, §2.6-2.7; D §2.3; K2 #1, #5a; S10 X10): the
// gain computer and the colour Mu 67 adds, stage::ProgressiveKnee and stage::TubePushPull, proven on the policy
// functions (unit rows, 01 §8.4 step 3) and through the registered `mu-67` engine and its descriptor (the Mode's sheet,
// docs/modes/mu-67.md). Spec rows only (no golden): the Mode-level goldens are dsp.static/time/... of `mu-67`.
// References are double precision; "the float curve" is the solver's own r^_fb (rhatFb) evaluated at a double level.
//
// The law (header ProgressiveKnee.h):
//   progressiveknee.shape.max_rel_err       H(u) (series below 1/2, direct above) against u + expm1(-u) in double over
//                                           u in [1e-6, 80]: <= 2e-6 relative; .seam_rel_jump at u = 1/2: <= 2e-6
//   progressiveknee.target.err_over_bound   target() against the law in double (random T, S, W, onset law, x):
//                                           within 2e-6 of the GR plus 1e-6 dB (<= 1); .slope.max_err: slope()
//                                           against the law's derivative, <= 1e-6 (absolute, per unit S)
//   progressiveknee.monotone.decreases      r^_fb (the loop gain k in place of S) non-decreasing on y in [-80, +40] dB
//                                           every 0.1 dB and every 0.001 dB over [T - 1, T + 5], S in {0.05 ... 1} x
//                                           o_c in {1e-3 ... 40} (K2 #5a)
// The FB solve (20,000 random cases per branch; T in [-60, 0], S in [0.3, 1), 1 at 5 %, onset law o_c = m0 + m1 W with
// m0 in [0, 2], m1 in [0, 1.5], W in [0, 40], x in [T - 20, T + 50], r1 in [0, 50]):
//   progressiveknee.fb.<branch>.max_err_db  solveFb against 200-step bisection of r = A + B r^_fb(x - r) on the float
//                                           curve: branch = static {0, 1} / attack {(1 - c) r1, c}, c in [1e-3, 0.6] /
//                                           release, c in [1e-7, 1e-3] / hold {r1, 0}: <= 1e-5 dB (tol::kFbSolveDb)
//   progressiveknee.fb.zdf.<branch>.max_err_db  FeedbackZdf<ProgressiveKnee> (the wrapper 01 §10.7 names) on the same
//                                           cases: <= 1e-5 dB; a NOTE times both solves (ns per 4-lane solve)
//   progressiveknee.fb.base.inc_rel_err     a based solve {lo - c (base + lo), c, base} (base up to 50 dB, c in [1e-8,
//                                           0.3], lo within 1e-6 of base) against the exact increment (bisection in
//                                           double on the law in double): |d - d_ref| / (|A| + B (1 + k)(|x - T| +
//                                           |base| + W + 1)) <= 1e-6 (fbsolve's measure); a NOTE gives the absolute
//                                           solve's ratio (its error is an ulp of the GR); .zero_base_mismatch: base 0
//                                           is the unbased solve, bit for bit
//   progressiveknee.fb.lanes.mismatches     four cases packed into one call against each alone, bit for bit (a lane's
//                                           early stop never depends on the others: analysis::staticGr = tgtDb)
//   progressiveknee.fb.poison.finite        NaN or inf in x gives NaN in every lane that carries it, and the others
//                                           are unchanged (01 §5.8)
// Mu 67 (the registered engine and descriptor; DC THRESH = the knee slot, dial = knee / 4):
//   progressiveknee.mu67.fb_monotone.decreases  "fb.monotone for every DC THRESH" (the card's acceptance): the
//                                           resolved FB curve (staticGr, topo FF, slope k) non-decreasing on y in
//                                           [-80, +40] every 0.1 dB and every 0.005 dB over [T - 1, T + 5], for DC
//                                           THRESH 0 ... 40 dB every 0.25 dB x AC THRESH {-40, -18, 0, 24}; .configs
//                                           counts them (644)
//   progressiveknee.mu67.static_fb.max_err_db   staticGr (FB) against the bisection root on its own FF curve
//                                           (dsp.static's method) for DC THRESH 0 ... 40 every 1 dB, x in [T - 20,
//                                           T + 60]: <= 1e-5
//   progressiveknee.mu67.ratio.*            the sheet (D §2.3): the local ratio (analysis::localRatio) at T + {1, 2, 5,
//                                           10, 20, 40} dB for DC 0 ... 10 (a NOTE table): it rises with level
//                                           (.rise_violations 0) and falls as DC THRESH turns clockwise at T + 10
//                                           (.dc_order_violations 0); DC 0 is a hard-knee limiter (.dc0_1db >= 5:1,
//                                           .dc0_10db >= 15:1), the default DC 5 starts "between 1:1 and 2:1 for
//                                           smaller peaks" (.dc5_2db in [1.2, 2]), DC 10 is gentle (.dc10_10db <= 3:1),
//                                           the ratio rises "up to 20:1" (.dc0_40db >= 18:1) and never passes the
//                                           asymptote 1 / (1 - S) (.over_asymptote 0)
//   progressiveknee.mu67.nominal.max_rel_err  the RATIO slot's derived value (1 / (1 - S)) against the local ratio at
//                                           T + 10 dB, DC 0 ... 40: <= 1 %
//   progressiveknee.mu67.internals.*        a 1 kHz square at T + 10 dB for 1 s at the defaults: EFF RATIO against the
//                                           nominal ratio (<= 2 %), BIAS = GR / 2 V (<= 1e-3), TC WEIGHT 0 on TC2
//   progressiveknee.mu67.attack.<fs>.dc<d>.tc<n>_s  the D2 attack (T - 20 -> T + 20 dB square, t63 in dB) through the
//                                           Rig (NOTE), and .attack.192000.max_rel_err: at 192 kHz every published
//                                           attack (TC1-TC6 x DC 0/5/10) within 5 % (ADR-63's conversion,
//                                           Mu67::attackOpenLoopFactor)
//   progressiveknee.mu67.latvert.*          LAT/VERT (stmode = the universal M/S code) through fcdsp::EngineHost at ECO
//                                           and STD: a mid-only input (L = R, T + 10 dB) comes out with L == R bit for
//                                           bit (.mid_only.<q>.side_mismatches) and compressed >= 3 dB
//                                           (.mid_only.<q>.gr_db); a side-only input (L = -R) with L == -R bit for bit;
//                                           with M at T + 10 dB and a quiet S: LINK gives S the mid's GR (.link.
//                                           side_vs_mid_db <= 0.1 dB) and IND leaves S unreduced (.ind.side_gr_db
//                                           <= 0.1 dB)
//   progressiveknee.mu67.colour.*           TubePushPull through the entry's colourCurve (analysis::harmonicsDb, a
//                                           0 dBFS sine at the colour input): no even harmonics (.h2_db <= -100), H3
//                                           rising with GR (.h3_rise_violations 0 over GR 0 / 5 / 10 / 20 dB) from
//                                           <= -85 dB with no GR to [-65, -50] dB at 20 dB (.h3_0db, .h3_20db), INPUT
//                                           -20 dB lowering it >= 35 dB (.input_drop_db), unity small-signal gain
//                                           (.small_gain within 1e-6), and digital silence after a noise burst staying
//                                           exactly 0 through the Rig (.silence_nonzero 0)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/TubePushPull.h"
#include "fcdsp/engine/stages/combinators/FeedbackZdf.h"
#include "fcdsp/engine/stages/gain/ProgressiveKnee.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <array>
#include <chrono>
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
    namespace tol = fcmp::probe::tol;
    namespace measure = fcmp::probe::measure;
    namespace sig = fcmp::probe::sig;

    using PK = st::ProgressiveKnee;
    using Zdf = st::FeedbackZdf<PK>;

    constexpr float kFs = 48000.0f;

    float lane0(simd::f32x4 v) { return simd::lane<0>(v); }

    LevelCtl levelCtl(float thr, float slope, float knee)
    {
        return LevelCtl{ simd::set1(thr), simd::set1(slope), simd::set1(knee), simd::set1(kS2Off) };
    }

    PK::Coeffs lawOf(float onset0, float perDb)
    {
        PK::Coeffs c;
        c.onsetDb = onset0;
        c.onsetPerDb = perDb;
        return c;
    }

    double hExact(double u) { return u > 0.0 ? u + std::expm1(-u) : 0.0; }

    // o_c in double from the float law (ProgressiveKnee::onset).
    double onsetD(const PK::Coeffs& c, float knee)
    {
        return std::max(static_cast<double>(PK::kMinOnsetDb),
                        static_cast<double>(c.onsetDb) + static_cast<double>(c.onsetPerDb) * static_cast<double>(knee));
    }

    // One random FB case (header comment).
    struct Case
    {
        PK::Coeffs c;
        LevelCtl l;
        float thr, slope, knee, x, A, B, base;
    };

    // The float FB curve the solver uses (rhatFb) at a double output level.
    double fbCurve(const Case& k, double y)
    {
        return static_cast<double>(lane0(PK::rhatFb(k.c, simd::set1(static_cast<float>(y)), k.l)));
    }

    // 200-step bisection in double of d = A + B r^_fb(x - base - d) on the float curve.
    double bisectFloat(const Case& k)
    {
        const double x = k.x, a = k.A, b = k.B, base = k.base;
        double lo = a, hi = a + b * fbCurve(k, x - base - a);
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * fbCurve(k, x - base - mid) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    // The same on the law in double (the exact increment of a based solve).
    double bisectExact(const Case& k)
    {
        const double kk = static_cast<double>(st::QuadKnee::loopGain(k.slope));
        const double oc = onsetD(k.c, k.knee);
        const auto curve = [&](double y) { return kk * oc * hExact((y - static_cast<double>(k.thr)) / oc); };
        const double x = k.x, a = k.A, b = k.B, base = k.base;
        double lo = a, hi = a + b * curve(x - base - a);
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * curve(x - base - mid) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    float solveOwn(const Case& k)
    {
        return lane0(PK::solveFb(k.c, simd::set1(k.x), k.l,
                                 FbAffine{ simd::set1(k.A), simd::set1(k.B), simd::set1(k.base) }));
    }
    float solveZdf(const Case& k)
    {
        return lane0(Zdf::solveFb(Zdf::Coeffs{ k.c }, simd::set1(k.x), k.l,
                                  FbAffine{ simd::set1(k.A), simd::set1(k.B), simd::set1(k.base) }));
    }

    double logUniform(sig::Pcg32& g, double lo, double hi)
    {
        return lo * std::exp(std::log(hi / lo) * g.uniformDouble());
    }

    enum class Branch
    {
        stat,
        attack,
        release,
        hold
    };

    Case randomCase(sig::Pcg32& g, Branch b)
    {
        Case k{};
        k.thr = static_cast<float>(-60.0 * g.uniformDouble());
        k.slope = g.uniformDouble() < 0.05 ? 1.0f : static_cast<float>(0.3 + 0.7 * g.uniformDouble());
        k.knee = static_cast<float>(40.0 * g.uniformDouble());
        k.c = lawOf(static_cast<float>(2.0 * g.uniformDouble()), static_cast<float>(1.5 * g.uniformDouble()));
        k.l = levelCtl(k.thr, k.slope, k.knee);
        k.x = static_cast<float>(static_cast<double>(k.thr) - 20.0 + 70.0 * g.uniformDouble());
        const auto r1 = static_cast<float>(50.0 * g.uniformDouble());
        float c = 1.0f;
        if (b == Branch::attack)
            c = static_cast<float>(logUniform(g, 1e-3, 0.6));
        else if (b == Branch::release)
            c = static_cast<float>(logUniform(g, 1e-7, 1e-3));
        switch (b)
        {
            case Branch::stat:
                k.A = 0.0f;
                k.B = 1.0f;
                break;
            case Branch::attack:
            case Branch::release:
                k.A = r1 - c * r1;
                k.B = c;
                break;
            case Branch::hold:
                k.A = r1;
                k.B = 0.0f;
                break;
        }
        k.base = 0.0f;
        return k;
    }

    // ---- the law ---------------------------------------------------------------------------------------------------
    void shapeRows(Probe& P)
    {
        double worst = 0.0;
        for (int i = 0; i <= 200000; ++i)
        {
            const double ud = 1e-6 * std::pow(8e7, static_cast<double>(i) / 200000.0);     // 1e-6 ... 80
            const auto u = static_cast<float>(ud);
            const simd::f32x4 uv = simd::set1(u);
            const float h = lane0(PK::shape(uv, fcdsp::exp2(simd::mul(uv, simd::set1(-PK::kLog2E)))));
            const double ref = hExact(static_cast<double>(u));
            worst = std::max(worst, std::fabs(static_cast<double>(h) - ref) / ref);
        }
        P.le("progressiveknee.shape.max_rel_err", worst, 2e-6);
        const auto at = [](float u) {
            const simd::f32x4 uv = simd::set1(u);
            return static_cast<double>(lane0(PK::shape(uv, fcdsp::exp2(simd::mul(uv, simd::set1(-PK::kLog2E))))));
        };
        const float below = std::nextafter(PK::kSeriesBelow, 0.0f);
        const double jump = std::fabs(at(PK::kSeriesBelow) - at(below)) / at(below);
        std::printf("NOTE     progressiveknee.shape: seam at u = 1/2, relative jump %.3g\n", jump);
        P.le("progressiveknee.shape.seam_rel_jump", jump, 2e-6);
    }

    void targetRows(Probe& P)
    {
        sig::Pcg32 g(0x70726f67, 1);
        double worstDb = 0.0, worstSlope = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            Case k = randomCase(g, Branch::stat);
            const double oc = onsetD(k.c, k.knee), o = static_cast<double>(k.x) - static_cast<double>(k.thr);
            const double want = static_cast<double>(k.slope) * oc * hExact(o / oc);
            const double got = static_cast<double>(lane0(PK::target(k.c, simd::set1(k.x), k.l)));
            worstDb = std::max(worstDb, std::fabs(got - want) / (2e-6 * want + 1e-6));
            const double wantSlope = o > 0.0 ? static_cast<double>(k.slope) * -std::expm1(-o / oc) : 0.0;
            const double gotSlope = static_cast<double>(lane0(PK::slope(k.c, simd::set1(k.x), k.l)));
            worstSlope = std::max(worstSlope,
                                  std::fabs(gotSlope - wantSlope) / std::max(1.0, static_cast<double>(k.slope)));
        }
        // worstDb is in units of the bound: <= 1 passes
        P.le("progressiveknee.target.err_over_bound", worstDb, 1.0);
        P.le("progressiveknee.target.slope.max_err", worstSlope, 1e-6);
    }

    void monotoneRows(Probe& P)
    {
        std::int64_t decreases = 0, configs = 0;
        const float thr = -30.0f;
        for (const float s : { 0.05f, 0.5f, 0.75f, 0.9f, 0.95f, 1.0f })
            for (const float oc : { 1e-3f, 0.5f, 2.0f, 10.0f, 40.0f })
            {
                ++configs;
                const PK::Coeffs c = lawOf(oc, 0.0f);
                const LevelCtl l = levelCtl(thr, s, 0.0f);
                float prev = -1.0f;
                const auto step = [&](float y) {
                    const float r = lane0(PK::rhatFb(c, simd::set1(y), l));
                    decreases += r < prev ? 1 : 0;
                    prev = r;
                };
                for (int i = 0; i <= 1200; ++i)
                    step(-80.0f + 0.1f * static_cast<float>(i));
                prev = -1.0f;
                for (int i = 0; i <= 6000; ++i)
                    step(thr - 1.0f + 0.001f * static_cast<float>(i));
            }
        std::printf("NOTE     progressiveknee.monotone: %lld curve(s)\n", static_cast<long long>(configs));
        P.eq("progressiveknee.monotone.decreases", decreases, 0);
    }

    // ---- the FB solve ----------------------------------------------------------------------------------------------
    void solveRows(Probe& P)
    {
        const struct
        {
            Branch b;
            const char* name;
        } branches[] = { { Branch::stat, "static" }, { Branch::attack, "attack" }, { Branch::release, "release" },
                         { Branch::hold, "hold" } };
        std::array<std::vector<Case>, 4> timing;
        for (const auto& br : branches)
        {
            sig::Pcg32 g(0x736f6c76, static_cast<std::uint64_t>(br.b) + 3u);
            double own = 0.0, zdf = 0.0;
            for (int i = 0; i < 20000; ++i)
            {
                const Case k = randomCase(g, br.b);
                const double want = bisectFloat(k);
                own = std::max(own, std::fabs(static_cast<double>(solveOwn(k)) - want));
                zdf = std::max(zdf, std::fabs(static_cast<double>(solveZdf(k)) - want));
                if (i < 256)
                    timing[static_cast<std::size_t>(br.b)].push_back(k);
            }
            P.le(std::string("progressiveknee.fb.") + br.name + ".max_err_db", own, tol::kFbSolveDb);
            P.le(std::string("progressiveknee.fb.zdf.") + br.name + ".max_err_db", zdf, tol::kFbSolveDb);
        }

        // the cost of one 4-lane solve per branch, own and FeedbackZdf's (NOTE: a same-machine ratio, not a
        // measurement)
        const auto timeIt = [&](const std::vector<Case>& cases, auto&& solve) {
            volatile float sink = 0.0f;
            const auto t0 = std::chrono::steady_clock::now();
            constexpr int kReps = 400;
            for (int rep = 0; rep < kReps; ++rep)
                for (const Case& k : cases)
                    sink = sink + solve(k);
            const auto t1 = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::nano>(t1 - t0).count()
                 / (static_cast<double>(kReps) * static_cast<double>(cases.size()));
        };
        for (const auto& br : branches)
        {
            const std::vector<Case>& cases = timing[static_cast<std::size_t>(br.b)];
            std::printf("NOTE     progressiveknee.fb.%s: %.1f ns per solve (own, early stop) vs %.1f ns "
                        "(FeedbackZdf, six steps)\n",
                        br.name, timeIt(cases, solveOwn), timeIt(cases, solveZdf));
        }
    }

    void baseRows(Probe& P)
    {
        sig::Pcg32 g(0x62617365, 7);
        double worst = 0.0, worstAbs = 0.0;
        std::int64_t zeroBase = 0;
        for (int i = 0; i < 20000; ++i)
        {
            Case k = randomCase(g, Branch::release);
            const auto base = static_cast<float>(50.0 * g.uniformDouble());
            const auto lo = static_cast<float>((g.uniformDouble() - 0.5) * 2e-6 * static_cast<double>(base));
            const auto c = static_cast<float>(logUniform(g, 1e-8, 0.3));
            k.base = base;
            k.A = lo - c * (base + lo);
            k.B = c;
            const double want = bisectExact(k);
            const double kk = static_cast<double>(st::QuadKnee::loopGain(k.slope));
            const double scale = std::fabs(static_cast<double>(k.A))
                               + static_cast<double>(k.B) * (1.0 + kk)
                                     * (std::fabs(static_cast<double>(k.x) - static_cast<double>(k.thr))
                                        + static_cast<double>(base) + static_cast<double>(k.knee) + 1.0);
            worst = std::max(worst, std::fabs(static_cast<double>(solveOwn(k)) - want) / scale);
            // the absolute solve of the same value, for the NOTE: base + A, base 0
            Case a = k;
            a.A = base + k.A;
            a.base = 0.0f;
            worstAbs = std::max(worstAbs,
                                std::fabs(static_cast<double>(solveOwn(a)) - static_cast<double>(base) - want) / scale);
            // base 0 is the unbased solve, bit for bit
            Case z = k;
            z.base = 0.0f;
            const float withBase = solveOwn(z);
            const float without = lane0(PK::solveFb(z.c, simd::set1(z.x), z.l, FbAffine{ simd::set1(z.A),
                                                                                        simd::set1(z.B) }));
            zeroBase += withBase == without ? 0 : 1;
        }
        std::printf("NOTE     progressiveknee.fb.base: increment error %.3g of its scale (the absolute solve of the "
                    "same value: %.3g)\n",
                    worst, worstAbs);
        P.le("progressiveknee.fb.base.inc_rel_err", worst, 1e-6);
        P.eq("progressiveknee.fb.base.zero_base_mismatch", zeroBase, 0);
    }

    void laneRows(Probe& P)
    {
        sig::Pcg32 g(0x6c616e65, 5);
        std::int64_t mismatches = 0;
        for (int i = 0; i < 4000; ++i)
        {
            std::array<Case, 4> ks{};
            for (std::size_t j = 0; j < 4; ++j)
                ks[j] = randomCase(g, static_cast<Branch>(static_cast<int>((static_cast<std::size_t>(i) + j) % 4)));
            // one shared LevelCtl and law per call (the engine's case): the cases differ in x and the map
            alignas(16) float xs[4], as[4], bs[4];
            for (std::size_t j = 0; j < 4; ++j)
            {
                ks[j].c = ks[0].c;
                ks[j].l = ks[0].l;
                ks[j].thr = ks[0].thr;
                ks[j].slope = ks[0].slope;
                ks[j].knee = ks[0].knee;
                xs[j] = ks[j].x;
                as[j] = ks[j].A;
                bs[j] = ks[j].B;
            }
            alignas(16) float packed[4];
            simd::store(packed, PK::solveFb(ks[0].c, simd::load(xs), ks[0].l,
                                             FbAffine{ simd::load(as), simd::load(bs) }));
            for (std::size_t j = 0; j < 4; ++j)
                mismatches += packed[j] == solveOwn(ks[j]) ? 0 : 1;
        }
        P.eq("progressiveknee.fb.lanes.mismatches", mismatches, 0);
    }

    void poisonRows(Probe& P)
    {
        const PK::Coeffs c = lawOf(0.5f, 1.0f);
        const LevelCtl l = levelCtl(-18.0f, 0.95f, 20.0f);
        alignas(16) const float xs[4] = { std::nanf(""), -10.0f, INFINITY, -30.0f };
        alignas(16) float out[4];
        simd::store(out, PK::solveFb(c, simd::load(xs), l, FbAffine{ simd::set1(0.0f), simd::set1(1.0f) }));
        const FbAffine stat{ simd::set1(0.0f), simd::set1(1.0f) };
        const float clean1 = lane0(PK::solveFb(c, simd::set1(-10.0f), l, stat));
        const float clean3 = lane0(PK::solveFb(c, simd::set1(-30.0f), l, stat));
        const bool ok = std::isnan(out[0]) && std::isnan(out[2]) && out[1] == clean1 && out[3] == clean3;
        P.eq("progressiveknee.fb.poison.finite", ok ? 1 : 0, 1);
    }

    // ---- Mu 67 -----------------------------------------------------------------------------------------------------
    EngineParams resolved(const ModeEntry& en, RawParams raw) { return fcmp::probe::resolveRaw(en, raw).eng; }

    // r^_fb through the entry (topo FF, slope k: QuadKnee.h's convention, as dsp.registry's fb.monotone reads it).
    EngineParams fbCurveParams(const EngineParams& e)
    {
        EngineParams ff = e;
        ff.topo = kTopoFF;
        ff.slope = st::QuadKnee::loopGain(e.slope);
        return ff;
    }

    void muMonotoneRows(Probe& P, const ModeEntry& en)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        std::int64_t decreases = 0, configs = 0;
        std::vector<float> ys, rs;
        for (const float thr : { -40.0f, -18.0f, 0.0f, 24.0f })
            for (int q = 0; q <= 160; ++q)
            {
                RawParams raw = base;
                raw[Pid::thr] = thr;
                raw[Pid::knee] = 0.25f * static_cast<float>(q);
                const EngineParams e = resolved(en, raw);
                if (e.topo != kTopoFB)
                    continue;
                ++configs;
                ys.clear();
                for (int i = 0; i <= 1200; ++i)
                    ys.push_back(-80.0f + 0.1f * static_cast<float>(i));
                for (int i = 0; i <= 1200; ++i)
                    ys.push_back(e.thrDb - 1.0f + 0.005f * static_cast<float>(i));
                rs.assign(ys.size(), 0.0f);
                en.staticGr(fbCurveParams(e), ys.data(), rs.data(), static_cast<int>(ys.size()));
                for (std::size_t i = 1; i < 1201; ++i)
                    decreases += rs[i] < rs[i - 1] ? 1 : 0;
                for (std::size_t i = 1202; i < rs.size(); ++i)
                    decreases += rs[i] < rs[i - 1] ? 1 : 0;
            }
        std::printf("NOTE     progressiveknee.mu67.fb_monotone: %lld FB configuration(s) (DC THRESH 0 ... 40 dB every "
                    "0.25 dB x 4 AC THRESH)\n",
                    static_cast<long long>(configs));
        P.eq("progressiveknee.mu67.fb_monotone.decreases", decreases, 0);
        P.eq("progressiveknee.mu67.fb_monotone.configs", configs, 644);
    }

    void muStaticRows(Probe& P, const ModeEntry& en)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        double worst = 0.0;
        for (int knee = 0; knee <= 40; ++knee)
        {
            RawParams raw = base;
            raw[Pid::knee] = static_cast<float>(knee);
            const EngineParams e = resolved(en, raw), ff = fbCurveParams(e);
            const auto curve = [&](double y) {
                const auto yf = static_cast<float>(y);
                float r = 0.0f;
                en.staticGr(ff, &yf, &r, 1);
                return static_cast<double>(r);
            };
            for (double x = static_cast<double>(e.thrDb) - 20.0; x <= static_cast<double>(e.thrDb) + 60.0 + 1e-9;
                 x += 0.25)
            {
                const auto xf = static_cast<float>(x);
                float r = 0.0f;
                en.staticGr(e, &xf, &r, 1);
                double lo = 0.0, hi = curve(static_cast<double>(xf));
                for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
                {
                    const double mid = 0.5 * (lo + hi);
                    (mid - curve(static_cast<double>(xf) - mid) <= 0.0 ? lo : hi) = mid;
                }
                worst = std::max(worst, std::fabs(static_cast<double>(r) - 0.5 * (lo + hi)));
            }
        }
        P.le("progressiveknee.mu67.static_fb.max_err_db", worst, tol::kFbSolveDb);
    }

    void muRatioRows(Probe& P, const ModeEntry& en)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        constexpr std::array<float, 6> kOver = { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 40.0f };
        std::int64_t rise = 0, dcOrder = 0, overAsymptote = 0;
        double dc0At1 = 0, dc0At10 = 0, dc0At40 = 0, dc5At2 = 0, dc10At10 = 0, prevAt10 = 1e9;
        std::printf("NOTE     progressiveknee.mu67.ratio: local ratio at T + 1 / 2 / 5 / 10 / 20 / 40 dB "
                    "(asymptote)\n");
        for (int dc = 0; dc <= 10; ++dc)
        {
            RawParams raw = base;
            raw[Pid::knee] = 4.0f * static_cast<float>(dc);
            const EngineParams e = resolved(en, raw);
            const double t = static_cast<double>(analysis::inputThresholdDb(e));
            const double asymptote = 1.0 / (1.0 - static_cast<double>(e.slope));
            std::array<double, kOver.size()> r{};
            for (std::size_t i = 0; i < kOver.size(); ++i)
            {
                r[i] = static_cast<double>(analysis::localRatio(en, e, static_cast<float>(t) + kOver[i]));
                rise += i > 0 && !(r[i] > r[i - 1]) ? 1 : 0;
                overAsymptote += r[i] > asymptote ? 1 : 0;
            }
            std::printf("NOTE       DC %2d: %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f   (%.1f:1)\n", dc, r[0], r[1], r[2],
                        r[3], r[4], r[5], asymptote);
            dcOrder += r[3] < prevAt10 ? 0 : 1;
            prevAt10 = r[3];
            if (dc == 0)
            {
                dc0At1 = r[0];
                dc0At10 = r[3];
                dc0At40 = r[5];
            }
            if (dc == 5)
                dc5At2 = r[1];
            if (dc == 10)
                dc10At10 = r[3];
        }
        P.eq("progressiveknee.mu67.ratio.rise_violations", rise, 0);
        P.eq("progressiveknee.mu67.ratio.dc_order_violations", dcOrder, 0);
        P.eq("progressiveknee.mu67.ratio.over_asymptote", overAsymptote, 0);
        P.ge("progressiveknee.mu67.ratio.dc0_1db", dc0At1, 5.0);
        P.ge("progressiveknee.mu67.ratio.dc0_10db", dc0At10, 15.0);
        P.ge("progressiveknee.mu67.ratio.dc0_40db", dc0At40, 18.0);
        P.in("progressiveknee.mu67.ratio.dc5_2db", dc5At2, 1.2, 2.0);
        P.le("progressiveknee.mu67.ratio.dc10_10db", dc10At10, 3.0);

        double worst = 0.0;
        for (int knee = 0; knee <= 40; ++knee)
        {
            RawParams raw = base;
            raw[Pid::knee] = static_cast<float>(knee);
            const Resolution res = fcmp::probe::resolveRaw(en, raw);
            const double nominal = 1.0 / (1.0 - static_cast<double>(res.view[Pid::ratio].plain));
            const double local = static_cast<double>(
                analysis::localRatio(en, res.eng, analysis::inputThresholdDb(res.eng) + 10.0f));
            worst = std::max(worst, std::fabs(nominal / local - 1.0));
        }
        P.le("progressiveknee.mu67.nominal.max_rel_err", worst, 0.01);
    }

    void muInternalsRows(Probe& P, const ModeEntry& en)
    {
        const Resolution res = fcmp::probe::resolveRaw(en, fcmp::probe::modeRaw(en));
        const EngineParams& e = res.eng;
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::Segment segs[] = { { t + 10.0, 1.0 } };
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        float words[kInternals];
        rig.engine().internals(words);
        const double gr = static_cast<double>(run.tapGrDb.back());
        const double nominal = 1.0 / (1.0 - static_cast<double>(res.view[Pid::ratio].plain));
        std::printf("NOTE     progressiveknee.mu67.internals: GR %.4g dB; BIAS %.4g V, EFF RATIO %.4g (nominal %.4g), "
                    "TC WEIGHT %.4g\n",
                    gr, static_cast<double>(words[0]), static_cast<double>(words[1]), nominal,
                    static_cast<double>(words[2]));
        P.le("progressiveknee.mu67.internals.eff_ratio_rel_err",
             std::fabs(static_cast<double>(words[1]) / nominal - 1.0), 0.02);
        P.le("progressiveknee.mu67.internals.bias_err_v", std::fabs(static_cast<double>(words[0]) - gr / 2.0), 1e-3);
        P.eq("progressiveknee.mu67.internals.tc_weight_tc2_zero", words[2] == 0.0f ? 1 : 0, 1);
    }

    void muAttackRows(Probe& P, const ModeEntry& en)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        const ParamSpec* relSpec = nullptr;
        {
            ParamView view;
            resolveView(*en.desc, base, view);
            relSpec = view.spec[idx(Pid::rel)];
        }
        if (relSpec == nullptr || relSpec->kind != Kind::stepped)
        {
            P.harnessError("mu-67: TIME is not a step list");
            return;
        }
        double worst192 = 0.0;
        for (const float fs : { 48000.0f, 192000.0f })
            for (const int dc : { 0, 5, 10 })
                for (std::size_t tc = 0; tc < relSpec->steps.size(); ++tc)
                {
                    RawParams raw = base;
                    raw[Pid::knee] = 4.0f * static_cast<float>(dc);
                    raw[Pid::rel] = relSpec->steps[tc].plain;
                    const Resolution res = fcmp::probe::resolveRaw(en, raw);
                    const TimeSpec spec = en.desc->attackSpec(res.view, res.eng);
                    const double t = static_cast<double>(analysis::inputThresholdDb(res.eng));
                    fcmp::probe::EngineRig rig(en, res.eng, fs);
                    const fcmp::probe::Segment segs[] = { { t - 20.0, 0.2 }, { t + 20.0, 0.2 } };
                    const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
                    const std::size_t a = run.edges[1];
                    const std::span<const float> tap(run.tapGrDb);
                    const double got = measure::lawSeconds(tap.subspan(a), run.tapGrDb[a - 1], run.tapGrDb.back(),
                                                           static_cast<double>(fs), spec.law);
                    const double rel = got / static_cast<double>(spec.seconds) - 1.0;
                    std::printf("NOTE     progressiveknee.mu67.attack.%ld.dc%d.tc%zu: published %.4g ms, measured "
                                "%.4g ms (%+.1f %%; open-loop tau %.4g ms)\n",
                                static_cast<long>(fs), dc, tc + 1, 1000.0 * static_cast<double>(spec.seconds),
                                1000.0 * got, 100.0 * rel, static_cast<double>(res.eng.atkTauMs));
                    if (fs == 192000.0f)
                        worst192 = std::max(worst192, got > 0.0 ? std::fabs(rel) : 1e9);
                }
        P.le("progressiveknee.mu67.attack.192000.max_rel_err", worst192, 0.05);
    }

    // ---- LAT/VERT through the host ---------------------------------------------------------------------------------
    struct Stereo
    {
        std::vector<float> l, r;
    };

    Stereo renderHost(const ModeEntry& en, const EngineParams& e, Quality q, const Stereo& in)
    {
        HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = 512;
        cfg.quality = q;
        cfg.budget = LookaheadBudget::off;
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = e;
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        const std::size_t n = in.l.size();
        Stereo out{ std::vector<float>(n), std::vector<float>(n) };
        for (std::size_t off = 0; off < n; off += 512)
        {
            const std::size_t len = std::min<std::size_t>(512, n - off);
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            float* outs[2] = { out.l.data() + off, out.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, bp);
        }
        return out;
    }

    Stereo tone(std::size_t n, double midAmp, double midHz, double sideAmp, double sideHz)
    {
        Stereo s{ std::vector<float>(n), std::vector<float>(n) };
        for (std::size_t i = 0; i < n; ++i)
        {
            const double m = static_cast<double>(sig::sineAt(static_cast<std::int64_t>(i), midHz, kFs, midAmp));
            const double sd = sideAmp > 0.0
                                ? static_cast<double>(sig::sineAt(static_cast<std::int64_t>(i), sideHz, kFs, sideAmp))
                                : 0.0;
            s.l[i] = static_cast<float>(m + sd);
            s.r[i] = static_cast<float>(m - sd);
        }
        return s;
    }

    // The single-bin level (dB) of (L + sign R) / 2 at hz over the last 0.1 s.
    double levelDb(const Stereo& s, double sign, double hz)
    {
        const std::size_t w = static_cast<std::size_t>(0.1f * kFs), n0 = s.l.size() - w;
        std::vector<float> v(w);
        for (std::size_t i = 0; i < w; ++i)
            v[i] = 0.5f * (s.l[n0 + i] + static_cast<float>(sign) * s.r[n0 + i]);
        return measure::dbFromAmplitude(
            measure::SingleBin(hz, static_cast<double>(kFs), w)(v, static_cast<std::int64_t>(n0)).amplitude());
    }

    void latVertRows(Probe& P, const ModeEntry& en)
    {
        RawParams raw = fcmp::probe::modeRaw(en);
        raw[Pid::stmode] = 1.0f;                                    // LAT/VERT
        raw[Pid::link] = 0.0f;                                      // IND
        const EngineParams ind = resolved(en, raw);
        raw[Pid::link] = 1.0f;
        const EngineParams linked = resolved(en, raw);
        if (ind.stmode != 1)
        {
            P.harnessError("mu-67: LAT/VERT does not resolve to the M/S code");
            return;
        }
        const double t = static_cast<double>(analysis::inputThresholdDb(ind));
        const std::size_t n = static_cast<std::size_t>(1.0f * kFs);
        const double loud = measure::amplitudeFromDb(t + 10.0);
        for (const auto& [q, qn] : { std::pair{ Quality::eco, "eco" }, std::pair{ Quality::std, "std" } })
        {
            const std::string k = std::string("progressiveknee.mu67.latvert.");
            // mid only: L = R
            const Stereo mid = tone(n, loud, 1000.0, 0.0, 0.0);
            const Stereo ym = renderHost(en, ind, q, mid);
            std::int64_t sideMismatch = 0;
            for (std::size_t i = 0; i < n; ++i)
                sideMismatch += ym.l[i] == ym.r[i] ? 0 : 1;
            P.eq(k + "mid_only." + qn + ".side_mismatches", sideMismatch, 0);
            P.ge(k + "mid_only." + qn + ".gr_db", levelDb(mid, 1.0, 1000.0) - levelDb(ym, 1.0, 1000.0), 3.0);
            // side only: L = -R
            Stereo side = tone(n, 0.0, 1000.0, loud, 1000.0);
            const Stereo ys = renderHost(en, ind, q, side);
            std::int64_t midMismatch = 0;
            for (std::size_t i = 0; i < n; ++i)
                midMismatch += ys.l[i] == -ys.r[i] ? 0 : 1;
            P.eq(k + "side_only." + qn + ".mid_mismatches", midMismatch, 0);
            P.ge(k + "side_only." + qn + ".gr_db", levelDb(side, -1.0, 1000.0) - levelDb(ys, -1.0, 1000.0), 3.0);
        }
        // M loud (1 kHz), S quiet (750 Hz, 30 dB under the threshold): LINK gives S the mid's GR, IND none
        const Stereo ms = tone(n, loud, 1000.0, measure::amplitudeFromDb(t - 30.0), 750.0);
        const Stereo yi = renderHost(en, ind, Quality::eco, ms), yl = renderHost(en, linked, Quality::eco, ms);
        const double midGr = levelDb(ms, 1.0, 1000.0) - levelDb(yl, 1.0, 1000.0);
        const double sideGrLinked = levelDb(ms, -1.0, 750.0) - levelDb(yl, -1.0, 750.0);
        const double sideGrInd = levelDb(ms, -1.0, 750.0) - levelDb(yi, -1.0, 750.0);
        std::printf("NOTE     progressiveknee.mu67.latvert: mid GR %.4g dB; side GR %.4g dB (LINK), %.4g dB (IND)\n",
                    midGr, sideGrLinked, sideGrInd);
        P.le("progressiveknee.mu67.latvert.link.side_vs_mid_db", std::fabs(sideGrLinked - midGr), 0.1);
        P.le("progressiveknee.mu67.latvert.ind.side_gr_db", std::fabs(sideGrInd), 0.1);
    }

    // ---- the colour ------------------------------------------------------------------------------------------------
    void muColourRows(Probe& P, const ModeEntry& en)
    {
        const RawParams base = fcmp::probe::modeRaw(en);
        const EngineParams e = resolved(en, base);
        std::array<float, 8> h{};
        double worstH2 = -400.0, prevH3 = -400.0, h3At0 = 0.0, h3At20 = 0.0;
        std::int64_t rise = 0;
        for (const float gr : { 0.0f, 5.0f, 10.0f, 20.0f })
        {
            analysis::harmonicsDb(en, e, gr, 1.0f, h);
            const double h2 = static_cast<double>(h[1]), h3 = static_cast<double>(h[2]);
            std::printf("NOTE     progressiveknee.mu67.colour: GR %4.1f dB, 0 dBFS at the colour input: H2 %.1f dB, "
                        "H3 %.1f dB, H5 %.1f dB\n",
                        static_cast<double>(gr), h2, h3, static_cast<double>(h[4]));
            worstH2 = std::max(worstH2, h2);
            rise += h3 > prevH3 ? 0 : 1;
            prevH3 = h3;
            if (gr == 0.0f)
                h3At0 = h3;
            if (gr == 20.0f)
                h3At20 = h3;
        }
        P.le("progressiveknee.mu67.colour.h2_db", worstH2, -100.0);
        P.eq("progressiveknee.mu67.colour.h3_rise_violations", rise, 0);
        P.le("progressiveknee.mu67.colour.h3_0db", h3At0, -85.0);
        P.in("progressiveknee.mu67.colour.h3_20db", h3At20, -65.0, -50.0);
        RawParams quiet = base;
        quiet[Pid::drive] = -20.0f;
        analysis::harmonicsDb(en, resolved(en, quiet), 20.0f, 1.0f, h);
        std::printf("NOTE     progressiveknee.mu67.colour: INPUT -20 dB, GR 20 dB: H3 %.1f dB\n",
                    static_cast<double>(h[2]));
        P.ge("progressiveknee.mu67.colour.input_drop_db", h3At20 - static_cast<double>(h[2]), 35.0);
        const float x = 1e-4f;
        float y = 0.0f;
        en.colourCurve(e, 20.0f, &x, &y, 1);
        P.le("progressiveknee.mu67.colour.small_gain", std::fabs(static_cast<double>(y / x) - 1.0), 1e-6);

        // digital silence after a noise burst stays exactly 0 through the Rig (colour included)
        fcmp::probe::EngineRig rig(en, e, kFs);
        const std::size_t n = static_cast<std::size_t>(0.3f * kFs);
        std::vector<float> in(2 * n, 0.0f), yl(2 * n), yr(2 * n);
        sig::Pcg32 g(0x7369, 9);
        for (std::size_t i = 0; i < n; ++i)
            in[i] = 0.5f * g.bipolar();
        rig.process(in.data(), in.data(), yl.data(), yr.data(), 2 * n);
        std::int64_t nonzero = 0;
        for (std::size_t i = n; i < 2 * n; ++i)
            nonzero += yl[i] == 0.0f && yr[i] == 0.0f ? 0 : 1;
        P.eq("progressiveknee.mu67.colour.silence_nonzero", nonzero, 0);
    }
} // namespace

FCMP_PROBE(dsp, progressiveknee)
{
    shapeRows(P);
    targetRows(P);
    monotoneRows(P);
    solveRows(P);
    baseRows(P);
    laneRows(P);
    poisonRows(P);

    const ModeEntry& en = fcmp::probe::modeEntry("mu-67");
    muMonotoneRows(P, en);
    muStaticRows(P, en);
    muRatioRows(P, en);
    muInternalsRows(P, en);
    muAttackRows(P, en);
    latVertRows(P, en);
    muColourRows(P, en);
    return P.finish();
}
