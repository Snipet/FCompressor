// FCMP_PROBE layer=dsp name=sharedelement scope=global timeout=120
//
// dsp.sharedelement (M5, S10; SPRINTS S10.2; 01 §5.2-5.3, §10.7; D §2.5; E §3.3; K2 #1, #4, #20): the policies Diode
// 609 adds, stage::SharedElementMax (the limiter sharing the compressor's gain element), stage::SlowHp (ATTACK SLOW's side-
// chain high-pass) and stage::DiodeBridge (the colour), proven on the policy functions (unit rows, 01 §8.4 step 3) and
// through Diode 609's registered engine (EngineRig, 48 kHz), plus the Mode's defining behaviours: the limiter on the
// shared element, A1 / A2 as DualRelease inside the FB solve, SLOW on the compressor's side chain only, and the
// stage-2 curve drawn by the analysis (ModeEntry::staticS2, S10 lead revision 4). Spec rows only (no golden: the Mode's
// goldens are dsp.static/time/... of `diode-609`). References are double precision on the policies' own float rates.
//
// SharedElementMax (unit rows; T2 = the stage-2 threshold, k2 = 99 its loop gain, W2 = 0.5 dB its knee):
//   sharedelement.off.mismatches        s2ThrDb at kS2Off: combine() == {r1_0, r1_1, 0, 0} bit for bit, and the state
//                                       rests at 0 dB (grDb 0) even under a +40 dB level
//   sharedelement.fb.max_err_db         20,000 random cases (T2, x, r1, the stage-2 GR r2', the times, 48/192 kHz,
//                                       lanes independent, link 0): the stage-2 GR (aux lanes) against the 200-step
//                                       bisection of r = A + B r^2_fb(x - max(r1, r)) (the shared sense point) for the
//                                       branch SmoothBranching's predictor picks, {(1 - c) r2', c}: <= 1e-5 dB
//                                       (tol::kFbSolveDb); .element_mismatches: lanes 0-1 == max(r1, r2) bit for bit;
//                                       .held.count / .own.count >= 1 (the compressor holds the sense point / the
//                                       limiter senses its own GR)
//   sharedelement.fb.aux.mismatches     x's lanes 0-1 (the compressor's, possibly shaped) do not reach stage 2
//   sharedelement.link.*                .k1.lane_diff: at link 1 the channels' stage-2 GR are equal, the max of the
//                                       unlinked ones (bit for bit); .swap.mismatches: swapped channels in, swapped out
//   sharedelement.static.max_err_db     combineStatic against max(r1, the static FB bisection root at x): <= 1e-5 dB
//   sharedelement.carry.mismatches      seed(v) -> grDb == {max(v0, 0), max(v1, 0), 0, 0}
// SlowHp:
//   sharedelement.slowhp.fast.mismatches     m[0] = 0 (FAST): tick() returns the input bit for bit, every lane
//   sharedelement.slowhp.aux.mismatches      SLOW: lanes 2-3 pass bit for bit (the limiter's side chain)
//   sharedelement.slowhp.response.max_err_db SLOW: the running filter's steady-state sine gain (single-bin DFT) at
//                                            20 ... 10,000 Hz against magDb: <= 0.01 dB; .corner_db: magDb(100 Hz) =
//                                            -3.0103 dB +- 0.01
//   sharedelement.slowhp.fade.*              FAST -> SLOW -> FAST: the amount moves by at most one 20 ms smoother step
//                                            per tick (.max_step_excess: <= 2^-23 over it, the rounding), lands on 1 and
//                                            on 0 (.lands_on, .lands_off), and the landed FAST is the bypass again
//                                            (.bypass_mismatches)
// DiodeBridge:
//   sharedelement.bridge.share.*        s(0) = 0 exactly, s(20 log10 2) = 1/2 +- 1e-5, s < 0 never, monotone
//   sharedelement.bridge.transfer_err   process() of a constant input against transfer(): <= 1e-6 relative
//   sharedelement.bridge.silence.nonzero  exactly 0 in -> exactly 0 out
//   sharedelement.bridge.bs.mismatches  one block against odd splits (1, 3, 5, 7, 17 ...): bit for bit
//   sharedelement.bridge.h2_vs_dft_db / h3_vs_dft_db  harmonicsEstimate against the single-bin DFT of the running stage
//                                       (1 kHz at 192 kHz, amp 0.4, GR 10 dB): <= 0.5 dB
//   sharedelement.bridge.bypass_thd_pct  THD of a -2 dBFS (+20 dBu) sine at 0 dB GR: <= 0.075 % (D §2.5 [V S10])
//   sharedelement.bridge.limit_thd_pct   THD of a +14 dBu sine at 10 dB GR: in [0.1, 0.45] % (D §2.5: "under 0.45 %
//                                       with the limiter in"; the bridge audible)
//   sharedelement.bridge.df_loss_db     the fundamental's describing-function loss at the highest wet level dsp.static
//                                       reaches (+3.3 dBFS peak at 7.7 dB of GR): <= 0.05 dB (half the 0.10 dB meter-
//                                       truth budget of a character Mode)
// Diode 609 (the registered engine; square levels are 1 kHz squares, |x| constant; COMP = +4 dBu 2:1 unless noted):
//   sharedelement.mode.s2.settled_max_err_db  LIMIT +10 dBu: the settled GR at 11 levels T - 10 ... T + 40 against
//                                       analysis::staticGain (stage 2 drawn): <= 1e-4 dB; .limiter_decides and
//                                       .comp_decides >= 1 (each stage sets the element somewhere)
//   sharedelement.mode.s2.ceiling_db    LIMIT +10 dBu, the compressor out of the way (+10 dBu 1.5:1), input 20 dB over
//                                       the limit: the settled output level against the limit, in [-0.5, +0.5] dB
//   sharedelement.mode.s2.attack.<fast|slow>_s  the limiter's closed-loop attack (t63 of the element GR, 20 dB step
//                                       over +4 dBu): in [0.5, 2] x the published 2 / 4 ms (ADR-63)
//   sharedelement.mode.s2.release.<d>_s the limiter's release (the loop opens): the published 50 / 100 / 200 / 800 ms
//                                       +- 5 %
//   sharedelement.mode.s2on.*           LIMIT OFF -> +4 dBu -> OFF at T + 14 (2:1; the limiter adds ~7 dB,
//                                       .limiter_adds_db >= 1): .max_step_db (the element GR's largest per-sample move
//                                       across both edges) <= 0.5 dB (a 20 ms fade, not a step); .lands_mismatches: from
//                                       25 ms after the OFF edge the element GR equals a run without the limiter, bit
//                                       for bit
//   sharedelement.mode.shared.*         the shared sense point: with the compressor holding the element (LIMIT +10 dBu
//                                       above the compressed output) LIMIT WINS = 0 and LIMIT GR <= COMP GR; 20 dB
//                                       over the limit, LIMIT WINS = 1
//   sharedelement.mode.link.*           LINK STEREO, L 20 dB over the limit, R silent: .stereo_lane_diff_db 0 (the
//                                       silent side takes the limiter's GR); .dual_r_db: DUAL leaves R at 0 dB
//   sharedelement.mode.a<1|2>.release.*  RELEASE A1 / A2 (DualRelease in FB): the release (1/e of the tapped GR) after
//                                       a 20 ms and a 3 s burst at T + 20: .short_s at the published fast recovery and
//                                       .long_s at the published slow one (+-10 % each), .long_over_short >= 4
//   sharedelement.mode.slow.lf_gr_db    ATTACK SLOW on a 50 Hz tone (T + 12) reduces the GR against FAST (>= 3 dB less),
//                                       .hf_gr_db at 1 kHz within 0.3 dB of FAST's; .limit_unshaped_db: with the limiter
//                                       holding a 50 Hz tone, SLOW leaves the LIMIT GR where FAST has it (<= 1e-3 dB)
//   sharedelement.mode.internals.nonfinite  the three internals finite everywhere above
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
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/DiodeBridge.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/scshape/SlowHp.h"
#include "fcdsp/engine/stages/stage2/SharedElementMax.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/modes/diode-609/Diode609.h"
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

    using SB = st::SmoothBranching;
    using SEM = st::SharedElementMax<st::PeakLog, SB>;
    using Hp = st::SlowHp;
    using Bridge = st::DiodeBridge;

    constexpr float kFs = 48000.0f;

    StageCtx ctxAt(float fs) { return StageCtx{ fs, fs, 1, {} }; }

    simd::f32x4 vec(float a, float b, float c, float d)
    {
        alignas(16) const float v[4] = { a, b, c, d };
        return simd::load(v);
    }

    std::array<float, 4> lanes(simd::f32x4 v)
    {
        alignas(16) std::array<float, 4> out{};
        simd::store(out.data(), v);
        return out;
    }

    bool same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

    float uniform(sig::Pcg32& g, float lo, float hi) { return lo + (hi - lo) * g.uniform(); }

    float logUniform(sig::Pcg32& g, float lo, float hi)
    {
        const double u = static_cast<double>(g.uniform());
        return static_cast<float>(static_cast<double>(lo)
                                  * sig::expDet(u * sig::logDet(static_cast<double>(hi) / static_cast<double>(lo))));
    }

    // ---- the limiter's FB law in double (QuadKnee.h's FB convention, SharedElementMax's constants) -----------------
    double rhat2(double t, double y)
    {
        const double w = static_cast<double>(SEM::kKneeDb);
        const double k = static_cast<double>(st::QuadKnee::loopGain(SEM::kSlope));
        const double o = y - t;
        const double q = std::clamp(o + 0.5 * w, 0.0, w);
        return k * (q * q / (2.0 * w) + std::max(0.0, o - 0.5 * w));
    }

    // The root of r = a + b r^2_fb(x - max(r1, r)) (non-increasing right side: bisection on [a, a + b r^2(x - r1)]).
    double sharedRoot(double t, double x, double r1, double a, double b)
    {
        double lo = a, hi = a + b * rhat2(t, x - std::max(r1, a));
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * rhat2(t, x - std::max(r1, mid)) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    LevelCtl levelWith(float s2ThrDb)
    {
        LevelCtl l;
        l.thrDb = simd::set1(-30.0f);
        l.slope = simd::set1(0.5f);
        l.kneeDb = simd::set1(6.0f);
        l.s2ThrDb = simd::set1(s2ThrDb);
        return l;
    }

    EngineParams timesOf(float atkMs, float relMs)
    {
        EngineParams p;
        p.s2AtkTauMs = atkMs;
        p.s2RelTauMs = relMs;
        p.link = 0.0f;
        return p;
    }

    // ---- SharedElementMax ---------------------------------------------------------------------------------------------
    void sharedElementRows(Probe& P)
    {
        sig::Pcg32 g(0x73686172, 3);

        // OFF: NoStage2's arithmetic, and the state rests
        {
            SEM::Coeffs c{};
            SEM::design(c, timesOf(1.0f, 100.0f), ctxAt(kFs));
            SEM::State s{};
            SEM::seed(s, simd::set1(7.0f));
            std::int64_t bad = 0;
            for (int i = 0; i < 1000; ++i)
            {
                const simd::f32x4 r1 = vec(uniform(g, 0, 40), uniform(g, 0, 40), uniform(g, 0, 40), uniform(g, 0, 40));
                const simd::f32x4 x = vec(uniform(g, -60, 40), uniform(g, -60, 40), 40.0f, 40.0f);
                const auto out = lanes(SEM::combine(c, s, r1, x, levelWith(kS2Off)));
                const auto in = lanes(r1);
                bad += same(out[0], in[0]) && same(out[1], in[1]) && out[2] == 0.0f && out[3] == 0.0f ? 0 : 1;
                const auto gr = lanes(SEM::grDb(s));
                bad += gr[0] == 0.0f && gr[1] == 0.0f && gr[2] == 0.0f && gr[3] == 0.0f ? 0 : 1;
            }
            P.eq("sharedelement.off.mismatches", bad, 0);
        }

        // FB: the shared sense point against bisection, per branch
        {
            double err = 0.0;
            std::int64_t element = 0, held = 0, own = 0, aux = 0;
            for (int i = 0; i < 20000; ++i)
            {
                const float fs = g.bounded(2) == 0 ? 48000.0f : 192000.0f;
                const float atk = logUniform(g, 0.05f, 400.0f), rel = logUniform(g, 5.0f, 800.0f);
                SEM::Coeffs c{};
                SEM::design(c, timesOf(atk, rel), ctxAt(fs));
                const float t2 = uniform(g, -40.0f, 0.0f);
                const float xa = uniform(g, t2 - 20.0f, t2 + 50.0f), xb = uniform(g, t2 - 20.0f, t2 + 50.0f);
                const float r1a = g.bounded(3) == 0 ? 0.0f : uniform(g, 0.0f, 40.0f);
                const float r1b = g.bounded(3) == 0 ? 0.0f : uniform(g, 0.0f, 40.0f);
                const float r2a = uniform(g, 0.0f, 40.0f), r2b = uniform(g, 0.0f, 40.0f);
                SEM::State s{};
                SEM::seed(s, vec(r2a, r2b, 0.0f, 0.0f));
                SEM::State s2 = s;
                const LevelCtl l = levelWith(t2);
                const auto out = lanes(SEM::combine(c, s, vec(r1a, r1b, -5.0f, 99.0f), vec(-80.0f, 7.0f, xa, xb), l));
                // x's lanes 0-1 and r1's lanes 2-3 never reach stage 2
                const auto out2 =
                    lanes(SEM::combine(c, s2, vec(r1a, r1b, 3.0f, 0.0f), vec(xa + 1.0f, -3.0f, xa, xb), l));
                aux += same(out[2], out2[2]) && same(out[3], out2[3]) ? 0 : 1;
                const float r1s[2] = { r1a, r1b }, r2s[2] = { r2a, r2b }, xs[2] = { xa, xb };
                const double cA = static_cast<double>(simd::lane<0>(c.bal.cA));
                const double cR = static_cast<double>(simd::lane<0>(c.bal.cR));
                for (int ln = 0; ln < 2; ++ln)
                {
                    const double r1 = r1s[ln], rp = r2s[ln], x = xs[ln], t = t2;
                    const double rootA = sharedRoot(t, x, r1, (1.0 - cA) * rp, cA);
                    const double want = rootA > rp ? rootA : sharedRoot(t, x, r1, (1.0 - cR) * rp, cR);
                    const float got = out[static_cast<std::size_t>(2 + ln)];
                    err = std::max(err, std::fabs(static_cast<double>(got) - want));
                    element += same(out[static_cast<std::size_t>(ln)], std::max(r1s[ln], got)) ? 0 : 1;
                    (want < r1 ? held : own) += 1;
                }
            }
            P.le("sharedelement.fb.max_err_db", err, tol::kFbSolveDb);
            P.eq("sharedelement.fb.element_mismatches", element, 0);
            P.ge("sharedelement.fb.held.count", static_cast<double>(held), 1.0);
            P.ge("sharedelement.fb.own.count", static_cast<double>(own), 1.0);
            P.eq("sharedelement.fb.aux.mismatches", aux, 0);
        }

        // link: k = 1 equal lanes (the max of the unlinked roots); swap symmetry
        {
            std::int64_t diff = 0, swap = 0;
            for (int i = 0; i < 2000; ++i)
            {
                EngineParams p = timesOf(logUniform(g, 0.1f, 50.0f), logUniform(g, 10.0f, 800.0f));
                SEM::Coeffs c0{}, c1{};
                SEM::design(c0, p, ctxAt(kFs));
                p.link = 1.0f;
                SEM::design(c1, p, ctxAt(kFs));
                const float t2 = uniform(g, -30.0f, -5.0f);
                const float xa = uniform(g, t2 - 10.0f, t2 + 40.0f), xb = uniform(g, t2 - 10.0f, t2 + 40.0f);
                const float r1a = uniform(g, 0.0f, 20.0f), r1b = uniform(g, 0.0f, 20.0f);
                const float ra = uniform(g, 0.0f, 20.0f), rb = uniform(g, 0.0f, 20.0f);
                SEM::State s0{}, s1{}, sw{};
                SEM::seed(s0, vec(ra, rb, 0, 0));
                s1 = s0;
                SEM::seed(sw, vec(rb, ra, 0, 0));
                const LevelCtl l = levelWith(t2);
                const auto u = lanes(SEM::combine(c0, s0, vec(r1a, r1b, 0, 0), vec(0, 0, xa, xb), l));
                const auto k1 = lanes(SEM::combine(c1, s1, vec(r1a, r1b, 0, 0), vec(0, 0, xa, xb), l));
                const auto k1s = lanes(SEM::combine(c1, sw, vec(r1b, r1a, 0, 0), vec(0, 0, xb, xa), l));
                diff += same(k1[2], k1[3]) && same(k1[2], std::max(u[2], u[3])) ? 0 : 1;
                swap += same(k1[0], k1s[1]) && same(k1[1], k1s[0]) && same(k1[2], k1s[3]) && same(k1[3], k1s[2])
                          ? 0
                          : 1;
            }
            P.eq("sharedelement.link.k1.lane_diff", diff, 0);
            P.eq("sharedelement.link.swap.mismatches", swap, 0);
        }

        // the static form
        {
            SEM::Coeffs c{};
            SEM::design(c, timesOf(1.0f, 100.0f), ctxAt(kFs));
            double err = 0.0;
            for (int i = 0; i < 5000; ++i)
            {
                const float t2 = uniform(g, -40.0f, 0.0f);
                std::array<float, 4> x{}, r1{};
                for (std::size_t ln = 0; ln < 4; ++ln)
                {
                    x[ln] = uniform(g, t2 - 20.0f, t2 + 60.0f);
                    r1[ln] = uniform(g, 0.0f, 30.0f);
                }
                const auto out =
                    lanes(SEM::combineStatic(c, simd::load(r1.data()), simd::load(x.data()), levelWith(t2)));
                for (std::size_t ln = 0; ln < 4; ++ln)
                {
                    const double want = std::max(static_cast<double>(r1[ln]),
                                                 sharedRoot(t2, static_cast<double>(x[ln]), 0.0, 0.0, 1.0));
                    err = std::max(err, std::fabs(static_cast<double>(out[ln]) - want));
                }
            }
            P.le("sharedelement.static.max_err_db", err, tol::kFbSolveDb);
        }

        // carry
        {
            std::int64_t bad = 0;
            for (int i = 0; i < 100; ++i)
            {
                const float a = uniform(g, -5.0f, 30.0f), b = uniform(g, -5.0f, 30.0f);
                SEM::State s{};
                SEM::seed(s, vec(a, b, 77.0f, 77.0f));
                const auto gr = lanes(SEM::grDb(s));
                bad += same(gr[0], std::max(a, 0.0f)) && same(gr[1], std::max(b, 0.0f)) && gr[2] == 0.0f && gr[3] == 0.0f
                         ? 0
                         : 1;
            }
            P.eq("sharedelement.carry.mismatches", bad, 0);
        }
    }

    // ---- SlowHp -------------------------------------------------------------------------------------------------------
    EngineParams hpParams(float hz)
    {
        EngineParams p;
        p.m[0] = hz;
        return p;
    }

    void slowHpRows(Probe& P)
    {
        sig::Pcg32 g(0x736c6f77, 5);
        // FAST: the bypass, every lane
        {
            Hp::Coeffs c{};
            Hp::design(c, hpParams(0.0f), ctxAt(kFs));
            Hp::State s{};
            std::int64_t bad = 0;
            for (int i = 0; i < 4096; ++i)
            {
                const simd::f32x4 v = vec(g.bipolar(), g.bipolar(), g.bipolar(), g.bipolar());
                const auto a = lanes(Hp::tick(c, s, v)), b = lanes(v);
                for (std::size_t ln = 0; ln < 4; ++ln)
                    bad += same(a[ln], b[ln]) ? 0 : 1;
            }
            P.eq("sharedelement.slowhp.fast.mismatches", bad, 0);
        }
        // SLOW: the aux lanes pass; the response against magDb
        {
            Hp::Coeffs c{};
            Hp::design(c, hpParams(100.0f), ctxAt(kFs));
            std::int64_t aux = 0;
            double err = 0.0;
            const double freqs[] = { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 5000.0, 10000.0 };
            for (const double hz : freqs)
            {
                Hp::State s{};
                const auto n = static_cast<std::size_t>(kFs);            // 1 s: whole cycles of every frequency
                std::vector<float> in(n), out(n);
                for (std::size_t k = 0; k < 2 * n; ++k)
                {
                    const auto x = static_cast<float>(0.5 * sig::sinTurns(hz * static_cast<double>(k) / kFs));
                    const auto y = lanes(Hp::tick(c, s, vec(x, -x, x, -0.25f * x)));
                    aux += same(y[2], x) && same(y[3], -0.25f * x) ? 0 : 1;
                    if (k >= n)
                    {
                        in[k - n] = x;
                        out[k - n] = y[0];
                    }
                }
                const measure::SingleBin bin(hz, static_cast<double>(kFs), n);
                const double gain = bin.gainDb(in, out, static_cast<std::int64_t>(n));
                err = std::max(err, std::fabs(gain - static_cast<double>(Hp::magDb(c, static_cast<float>(hz), kFs))));
            }
            P.eq("sharedelement.slowhp.aux.mismatches", aux, 0);
            P.le("sharedelement.slowhp.response.max_err_db", err, 0.01);
            P.near("sharedelement.slowhp.corner_db", static_cast<double>(Hp::magDb(c, 100.0f, kFs)), -3.0103, 0.01);
        }
        // the fade: FAST -> SLOW -> FAST at control ticks
        {
            Hp::Coeffs c{};
            Hp::design(c, hpParams(0.0f), ctxAt(kFs));
            const float tick = oneMinusAlpha(Hp::kSmoothMs, kFs / static_cast<float>(kTickSamples));
            double excess = 0.0;
            int onAt = -1, offAt = -1;
            float prev = Hp::amountOf(c);
            for (int t = 0; t < 4000; ++t)
            {
                Hp::design(c, hpParams(t < 2000 ? 100.0f : 0.0f), ctxAt(kFs));
                const float a = Hp::amountOf(c);
                const float target = t < 2000 ? 1.0f : 0.0f;
                const double allowed = static_cast<double>(tick) * std::fabs(static_cast<double>(target - prev));
                if (a != target)                    // (the landing within kLand is the only other move)
                    excess = std::max(excess, std::fabs(static_cast<double>(a - prev)) - allowed);
                if (onAt < 0 && a == 1.0f)
                    onAt = t;
                if (t >= 2000 && offAt < 0 && a == 0.0f)
                    offAt = t;
                prev = a;
            }
            P.le("sharedelement.slowhp.fade.max_step_excess", excess, 0x1p-23);
            P.eq("sharedelement.slowhp.fade.lands_on", onAt >= 0 ? 1 : 0, 1);
            P.eq("sharedelement.slowhp.fade.lands_off", offAt >= 0 ? 1 : 0, 1);
            Hp::State s{};
            std::int64_t bad = 0;
            for (int i = 0; i < 256; ++i)
            {
                const simd::f32x4 v = vec(g.bipolar(), g.bipolar(), g.bipolar(), g.bipolar());
                const auto a = lanes(Hp::tick(c, s, v)), b = lanes(v);
                for (std::size_t ln = 0; ln < 4; ++ln)
                    bad += same(a[ln], b[ln]) ? 0 : 1;
            }
            P.eq("sharedelement.slowhp.fade.bypass_mismatches", bad, 0);
        }
    }

    // ---- DiodeBridge --------------------------------------------------------------------------------------------------
    // THD (H2 ... H8 against H1, %) and the H2 / H3 levels (dB re H1) of a sine of amplitude `amp` at 1 kHz through the
    // stage at a constant GR, at 192 kHz.
    struct Harm
    {
        double thdPct = 0, h2Db = 0, h3Db = 0, h1 = 0;
    };

    Harm bridgeHarmonics(float amp, float grDb)
    {
        constexpr double kHz = 1000.0, kRate = 192000.0;
        const auto n = static_cast<std::size_t>(kRate / 10.0);        // 100 cycles
        Bridge::Coeffs c{};
        Bridge::design(c, EngineParams{}, ctxAt(static_cast<float>(kRate)));
        Bridge::State s{};
        std::vector<float> x(2 * n), gr(2 * n, grDb);
        for (std::size_t k = 0; k < x.size(); ++k)
            x[k] = static_cast<float>(static_cast<double>(amp) * sig::sinTurns(kHz * static_cast<double>(k) / kRate));
        Bridge::process(c, s, x.data(), gr.data(), static_cast<int>(x.size()), 0);
        const std::span<const float> w = std::span<const float>(x).subspan(n);
        Harm h;
        double hs = 0.0;
        for (int k = 1; k <= 8; ++k)
        {
            const measure::SingleBin bin(kHz * k, kRate, n);
            const double a = bin(w, static_cast<std::int64_t>(n)).amplitude();
            if (k == 1)
                h.h1 = a;
            else
                hs += a * a;
            if (k == 2)
                h.h2Db = measure::dbFromAmplitude(a);
            if (k == 3)
                h.h3Db = measure::dbFromAmplitude(a);
        }
        h.h2Db -= measure::dbFromAmplitude(h.h1);
        h.h3Db -= measure::dbFromAmplitude(h.h1);
        h.thdPct = 100.0 * std::sqrt(hs) / h.h1;
        return h;
    }

    void bridgeRows(Probe& P)
    {
        // the share
        {
            P.eq("sharedelement.bridge.share.zero", Bridge::share(0.0f) == 0.0f ? 1 : 0, 1);
            P.near("sharedelement.bridge.share.half", static_cast<double>(Bridge::share(6.0206f)), 0.5, 1e-5);
            std::int64_t bad = 0;
            float prev = 0.0f;
            for (int i = -100; i <= 800; ++i)
            {
                const float s = Bridge::share(0.1f * static_cast<float>(i));
                bad += s < 0.0f || s < prev || s > 1.0f ? 1 : 0;
                prev = s;
            }
            P.eq("sharedelement.bridge.share.monotone", bad, 0);
        }
        Bridge::Coeffs c{};
        Bridge::design(c, EngineParams{}, ctxAt(kFs));
        // process() of a constant input against transfer()
        {
            double err = 0.0;
            for (const float gr : { 0.0f, 6.0f, 20.0f })
                for (int i = -40; i <= 40; ++i)
                {
                    const float x0 = 0.05f * static_cast<float>(i);
                    std::array<float, 8> x{};
                    x.fill(x0);
                    std::array<float, 8> grs{};
                    grs.fill(gr);
                    Bridge::State s{};
                    Bridge::process(c, s, x.data(), grs.data(), 8, 0);
                    const float want = Bridge::transfer(c, x0, gr);
                    const double scale = std::max(1e-3, std::fabs(static_cast<double>(want)));
                    err = std::max(err, std::fabs(static_cast<double>(x[7] - want)) / scale);
                }
            P.le("sharedelement.bridge.transfer_err", err, 1e-6);
        }
        // silence and block splits
        {
            std::vector<float> z(1000, 0.0f), gr(1000, 12.0f);
            Bridge::State s{};
            Bridge::process(c, s, z.data(), gr.data(), 1000, 0);
            std::int64_t nz = 0;
            for (const float v : z)
                nz += v != 0.0f ? 1 : 0;
            P.eq("sharedelement.bridge.silence.nonzero", nz, 0);

            sig::Pcg32 g(0x62726467, 9);
            std::vector<float> a(2000), gra(2000);
            for (std::size_t k = 0; k < a.size(); ++k)
            {
                a[k] = 1.5f * g.bipolar();
                gra[k] = 15.0f * g.uniform();
            }
            std::vector<float> whole = a, split = a;
            Bridge::State s1{}, s2{};
            Bridge::process(c, s1, whole.data(), gra.data(), static_cast<int>(whole.size()), 0);
            const int sizes[] = { 1, 3, 5, 7, 17, 64, 2, 9 };
            std::size_t off = 0;
            for (int i = 0; off < split.size(); ++i)
            {
                const auto want = static_cast<std::size_t>(sizes[static_cast<std::size_t>(i) % 8]);
                const std::size_t m = std::min(want, split.size() - off);
                Bridge::process(c, s2, split.data() + off, gra.data() + off, static_cast<int>(m), 1);
                off += m;
            }
            std::int64_t bad = 0;
            for (std::size_t k = 0; k < whole.size(); ++k)
                bad += same(whole[k], split[k]) ? 0 : 1;
            P.eq("sharedelement.bridge.bs.mismatches", bad, 0);
        }
        // harmonics: the estimate against the running stage, and the published figures
        {
            const Harm h = bridgeHarmonics(0.4f, 10.0f);
            const Bridge::Harmonics e = Bridge::harmonicsEstimate(c, 0.4f, 10.0f);
            std::printf("NOTE     bridge: amp 0.4, GR 10 dB: H2 %.2f dB (estimate %.2f), H3 %.2f dB (estimate %.2f), THD "
                        "%.4f %%\n",
                        h.h2Db, static_cast<double>(e.h2Db), h.h3Db, static_cast<double>(e.h3Db), h.thdPct);
            P.le("sharedelement.bridge.h2_vs_dft_db", std::fabs(h.h2Db - static_cast<double>(e.h2Db)), 0.5);
            P.le("sharedelement.bridge.h3_vs_dft_db", std::fabs(h.h3Db - static_cast<double>(e.h3Db)), 0.5);
            const Harm bypass = bridgeHarmonics(static_cast<float>(measure::amplitudeFromDb(-2.0)), 0.0f);
            const Harm limit = bridgeHarmonics(static_cast<float>(measure::amplitudeFromDb(14.0 - 22.0)), 10.0f);
            std::printf("NOTE     bridge: THD %.4f %% at -2 dBFS (+20 dBu) without GR, %.4f %% at +14 dBu with 10 dB of "
                        "GR\n",
                        bypass.thdPct, limit.thdPct);
            P.le("sharedelement.bridge.bypass_thd_pct", bypass.thdPct, 0.075);
            P.in("sharedelement.bridge.limit_thd_pct", limit.thdPct, 0.1, 0.45);
            // the describing-function loss of the fundamental at dsp.static's highest wet level (+3.3 dBFS, 7.7 dB GR)
            const float amp = static_cast<float>(measure::amplitudeFromDb(3.3));
            const Harm top = bridgeHarmonics(amp, 7.7f);
            const double loss = -measure::dbFromAmplitude(top.h1 / static_cast<double>(amp));
            std::printf("NOTE     bridge: fundamental loss %.4f dB at +3.3 dBFS, 7.7 dB of GR\n", loss);
            P.le("sharedelement.bridge.df_loss_db", std::fabs(loss), 0.05);
        }
    }

    // ---- Diode 609, the registered engine ----------------------------------------------------------------------------
    const ModeEntry& diode() { return fcmp::probe::modeEntry("diode-609"); }

    RawParams diodeRaw(float thrDbu, float ratio, float s2Dbu)
    {
        const ModeEntry& en = diode();
        RawParams r = fcmp::probe::modeRaw(en);
        r[Pid::thr] = thrDbu - kDbuAt0dBFS;
        r[Pid::ratio] = 1.0f - 1.0f / ratio;
        r[Pid::s2thr] = s2Dbu >= 24.0f ? 24.0f : s2Dbu - kDbuAt0dBFS;
        return r;
    }

    EngineParams resolved(const RawParams& r) { return fcmp::probe::resolveRaw(diode(), r).eng; }

    // A 1 kHz square at `levelDb` (|x| constant), L and R scaled, for `seconds`; returns the tapped lane GR.
    struct Render
    {
        std::vector<float> grL, grR, outL;
        std::array<float, kInternals> internals{};
        bool finiteInternals = true;
    };

    Render square(fcmp::probe::EngineRig& rig, double levelDb, double seconds, float rScale = 1.0f, double hz = 1000.0)
    {
        const auto n = static_cast<std::size_t>(seconds * static_cast<double>(rig.fs()));
        const auto a = static_cast<float>(measure::amplitudeFromDb(levelDb));
        const auto half = static_cast<std::size_t>(static_cast<double>(rig.fs()) / (2.0 * hz));
        std::vector<float> l(n), r(n), yl(n), yr(n);
        const std::uint64_t n0 = rig.sampleIndex();
        for (std::size_t k = 0; k < n; ++k)
        {
            l[k] = (((n0 + k) / half) % 2 == 0 ? a : -a);
            r[k] = rScale * l[k];
        }
        rig.tap().clear();
        rig.setTapping(true);
        rig.process(l.data(), r.data(), yl.data(), yr.data(), n);
        Render out;
        out.grL = rig.tap().lane(rig.tap().grDb, 0);
        out.grR = rig.tap().lane(rig.tap().grDb, 1);
        out.outL = yl;
        rig.engine().internals(out.internals.data());
        for (const float w : out.internals)
            out.finiteInternals = out.finiteInternals && std::isfinite(w);
        return out;
    }

    Render sine(fcmp::probe::EngineRig& rig, double levelDb, double seconds, double hz)
    {
        const auto n = static_cast<std::size_t>(seconds * static_cast<double>(rig.fs()));
        const double a = measure::amplitudeFromDb(levelDb);
        std::vector<float> l(n), yl(n), yr(n);
        const std::uint64_t n0 = rig.sampleIndex();
        for (std::size_t k = 0; k < n; ++k)
        {
            const double turns = hz * static_cast<double>(n0 + k) / static_cast<double>(rig.fs());
            l[k] = static_cast<float>(a * sig::sinTurns(turns));
        }
        rig.tap().clear();
        rig.setTapping(true);
        rig.process(l.data(), l.data(), yl.data(), yr.data(), n);
        Render out;
        out.grL = rig.tap().lane(rig.tap().grDb, 0);
        out.outL = yl;
        rig.engine().internals(out.internals.data());
        for (const float w : out.internals)
            out.finiteInternals = out.finiteInternals && std::isfinite(w);
        return out;
    }

    double meanTail(const std::vector<float>& v, std::size_t tail)
    {
        double acc = 0.0;
        for (std::size_t k = v.size() - tail; k < v.size(); ++k)
            acc += static_cast<double>(v[k]);
        return acc / static_cast<double>(tail);
    }

    void modeRows(Probe& P)
    {
        const ModeEntry& en = diode();
        bool internalsFinite = true;

        // the stage-2 curve: the settled engine against staticGain (stage 2 drawn)
        {
            const EngineParams e = resolved(diodeRaw(4.0f, 2.0f, 10.0f));
            const double t = static_cast<double>(analysis::inputThresholdDb(e));
            fcmp::probe::EngineRig rig(en, e, kFs);
            double err = 0.0;
            std::int64_t limiter = 0, comp = 0;
            for (int i = 0; i <= 10; ++i)
            {
                const double level = t - 10.0 + 5.0 * i;
                const Render r = square(rig, level, 0.5);
                internalsFinite = internalsFinite && r.finiteInternals;
                const auto x = static_cast<float>(level) + e.preGainDb;
                float gain = 0.0f, stage1 = 0.0f;
                analysis::staticGain(en, e, std::span<const float>(&x, 1), std::span<float>(&gain, 1));
                analysis::staticGr(en, e, std::span<const float>(&x, 1), std::span<float>(&stage1, 1),
                                   analysis::CurveOpts{ false, false });
                const double want = static_cast<double>(e.preGainDb - gain);
                err = std::max(err, std::fabs(static_cast<double>(r.grL.back()) - want));
                (want > static_cast<double>(stage1) + 1e-6 ? limiter : comp) += 1;
            }
            P.le("sharedelement.mode.s2.settled_max_err_db", err, 1e-4);
            P.ge("sharedelement.mode.s2.limiter_decides", static_cast<double>(limiter), 1.0);
            P.ge("sharedelement.mode.s2.comp_decides", static_cast<double>(comp), 1.0);
        }

        // the limiter holds the output at its threshold
        {
            const EngineParams e = resolved(diodeRaw(10.0f, 1.5f, 10.0f));
            fcmp::probe::EngineRig rig(en, e, kFs);
            const double limitDbfs = 10.0 - static_cast<double>(kDbuAt0dBFS);
            const Render r = square(rig, limitDbfs + 20.0, 1.0);
            const double outDb = measure::dbFromAmplitude(std::fabs(static_cast<double>(r.outL.back())));
            std::printf("NOTE     mode.s2: 20 dB over the limit (%.1f dBFS): output %.4f dBFS, GR %.4f dB, LIMIT WINS "
                        "%g\n",
                        limitDbfs, outDb, static_cast<double>(r.grL.back()), static_cast<double>(r.internals[2]));
            P.in("sharedelement.mode.s2.ceiling_db", outDb - limitDbfs, -0.5, 0.5);
            P.eq("sharedelement.mode.shared.limit_wins", r.internals[2] == 1.0f ? 1 : 0, 1);
        }

        // the limiter's closed-loop attack and its release (the compressor out of the way: +10 dBu, 1.5:1)
        {
            const float atkPublished[2] = { 2.0f, 4.0f };
            for (int a = 0; a < 2; ++a)
            {
                RawParams raw = diodeRaw(10.0f, 1.5f, 4.0f);
                raw[Pid::s2atk] = atkPublished[a];
                const EngineParams e = resolved(raw);
                fcmp::probe::EngineRig rig(en, e, kFs);
                const double lim = 4.0 - static_cast<double>(kDbuAt0dBFS);
                (void) square(rig, lim - 20.0, 0.2);
                const Render r = square(rig, lim + 20.0, 0.3);
                const double tA = measure::crossingSeconds(r.grL, 0.0, static_cast<double>(r.grL.back()),
                                                           1.0 - 1.0 / 2.718281828459045, kFs);
                std::printf("NOTE     mode.s2.attack: published %g ms -> %.4g ms closed loop (open-loop tau %g ms)\n",
                            static_cast<double>(atkPublished[a]), 1000.0 * tA, static_cast<double>(e.s2AtkTauMs));
                P.in(std::string("sharedelement.mode.s2.attack.") + (a == 0 ? "fast_s" : "slow_s"), tA,
                     0.5e-3 * static_cast<double>(atkPublished[a]), 2e-3 * static_cast<double>(atkPublished[a]));
            }
            for (const float rel : { 50.0f, 100.0f, 200.0f, 800.0f })
            {
                RawParams raw = diodeRaw(10.0f, 1.5f, 4.0f);
                raw[Pid::s2rel] = rel;
                const EngineParams e = resolved(raw);
                fcmp::probe::EngineRig rig(en, e, kFs);
                const double lim = 4.0 - static_cast<double>(kDbuAt0dBFS);
                const Render h = square(rig, lim + 10.0, 0.3);
                const Render r = square(rig, lim - 30.0, 10.0 * static_cast<double>(rel) / 1000.0);
                const double r0 = static_cast<double>(h.grL.back());
                const double tR = measure::crossingSeconds(r.grL, r0, 0.0, 1.0 - 1.0 / 2.718281828459045, kFs);
                P.near("sharedelement.mode.s2.release.d" + std::to_string(static_cast<int>(rel)) + "_s", tR,
                       static_cast<double>(rel) / 1000.0, 0.0, 0.05);
            }
        }

        // stage 2 on and off: a 20 ms fade, and the landed OFF is the compressor alone
        {
            const EngineParams off = resolved(diodeRaw(4.0f, 2.0f, 24.0f));
            const EngineParams on = resolved(diodeRaw(4.0f, 2.0f, 4.0f));
            const double level = -18.0 + 14.0;                  // compressor 7 dB, the limiter holding more
            fcmp::probe::EngineRig rig(en, off, kFs), ref(en, off, kFs);
            std::vector<float> gr, gref;
            const auto run = [&](const EngineParams& e, double seconds) {
                rig.setParams(e);
                const Render a = square(rig, level, seconds);
                const Render b = square(ref, level, seconds);
                gr.insert(gr.end(), a.grL.begin(), a.grL.end());
                gref.insert(gref.end(), b.grL.begin(), b.grL.end());
            };
            run(off, 0.5);
            run(on, 0.5);
            const std::size_t offEdge = gr.size();
            run(off, 0.5);
            double maxStep = 0.0;
            for (std::size_t k = 1; k < gr.size(); ++k)
                maxStep = std::max(maxStep, std::fabs(static_cast<double>(gr[k] - gr[k - 1])));
            std::int64_t bad = 0;
            for (std::size_t k = offEdge + static_cast<std::size_t>(0.025f * kFs); k < gr.size(); ++k)
                bad += same(gr[k], gref[k]) ? 0 : 1;
            std::printf("NOTE     mode.s2on: GR %.4f dB with the limiter, %.4f dB without; largest per-sample move "
                        "%.4g dB\n",
                        static_cast<double>(gr[offEdge - 1]), static_cast<double>(gref[offEdge - 1]), maxStep);
            P.ge("sharedelement.mode.s2on.limiter_adds_db",
                 static_cast<double>(gr[offEdge - 1] - gref[offEdge - 1]), 1.0);
            P.le("sharedelement.mode.s2on.max_step_db", maxStep, 0.5);
            P.eq("sharedelement.mode.s2on.lands_mismatches", bad, 0);
        }

        // the shared sense point: the compressor holding the element
        {
            const EngineParams e = resolved(diodeRaw(4.0f, 4.0f, 10.0f));
            fcmp::probe::EngineRig rig(en, e, kFs);
            const Render r = square(rig, -18.0 + 8.0, 1.0);    // 4:1 at T + 8: the output stays under +10 dBu
            internalsFinite = internalsFinite && r.finiteInternals;
            std::printf("NOTE     mode.shared: COMP GR %.4f, LIMIT GR %.4f, LIMIT WINS %g\n",
                        static_cast<double>(r.internals[0]), static_cast<double>(r.internals[1]),
                        static_cast<double>(r.internals[2]));
            P.eq("sharedelement.mode.shared.comp_holds", r.internals[2] == 0.0f ? 1 : 0, 1);
            P.le("sharedelement.mode.shared.limit_le_comp_db",
                 static_cast<double>(r.internals[1] - r.internals[0]), 0.0);
        }

        // link: STEREO couples the limiter too, DUAL does not
        {
            for (const float link : { 1.0f, 0.0f })
            {
                RawParams raw = diodeRaw(10.0f, 1.5f, 4.0f);
                raw[Pid::link] = link;
                const EngineParams e = resolved(raw);
                fcmp::probe::EngineRig rig(en, e, kFs);
                const Render r = square(rig, 4.0 - static_cast<double>(kDbuAt0dBFS) + 20.0, 0.5, 0.0f);
                if (link == 1.0f)
                    P.le("sharedelement.mode.link.stereo_lane_diff_db",
                         std::fabs(static_cast<double>(r.grL.back() - r.grR.back())), 0.0);
                else
                    P.le("sharedelement.mode.link.dual_r_db", static_cast<double>(r.grR.back()), 0.0);
            }
        }

        // A1 / A2: program-dependent recovery in the FB loop
        {
            struct Auto { const char* name; float plain, fastS, slowS; };
            const Auto autos[] = { { "a1", 2000.0f, 0.1f, 2.0f }, { "a2", 5000.0f, 0.05f, 5.0f } };
            for (const Auto& a : autos)
            {
                RawParams raw = diodeRaw(4.0f, 2.0f, 24.0f);
                raw[Pid::rel] = a.plain;
                const EngineParams e = resolved(raw);
                const double t = static_cast<double>(analysis::inputThresholdDb(e));
                double rel[2] = { 0.0, 0.0 };
                const double bursts[2] = { 0.02, 3.0 };
                for (int b = 0; b < 2; ++b)
                {
                    fcmp::probe::EngineRig rig(en, e, kFs);
                    (void) square(rig, t - 20.0, 0.2);
                    const Render h = square(rig, t + 20.0, bursts[b]);
                    const Render r = square(rig, t - 20.0, 12.0);
                    internalsFinite = internalsFinite && r.finiteInternals && h.finiteInternals;
                    const double r0 = static_cast<double>(h.grL.back());
                    rel[b] = measure::crossingSeconds(r.grL, r0, 0.0, 1.0 - 1.0 / 2.718281828459045, kFs);
                }
                const std::string k = std::string("sharedelement.mode.") + a.name + ".release.";
                std::printf("NOTE     mode.%s: release %.4g s after 20 ms, %.4g s after 3 s (published %g / %g s)\n",
                            a.name, rel[0], rel[1], static_cast<double>(a.fastS), static_cast<double>(a.slowS));
                P.near(k + "short_s", rel[0], static_cast<double>(a.fastS), 0.0, 0.10);
                P.ge(k + "long_over_short", rel[1] / rel[0], 4.0);
                P.near(k + "long_s", rel[1], static_cast<double>(a.slowS), 0.0, 0.10);
            }
        }

        // ATTACK SLOW: the compressor's side chain loses its lows, the limiter's does not
        {
            const auto grAt = [&](float atk, double hz, float s2Dbu, float thrDbu, int word) {
                RawParams raw = diodeRaw(thrDbu, 2.0f, s2Dbu);
                raw[Pid::atk] = atk;
                const EngineParams e = resolved(raw);
                fcmp::probe::EngineRig rig(en, e, kFs);
                const Render r = sine(rig, static_cast<double>(thrDbu - kDbuAt0dBFS) + 12.0, 1.0, hz);
                internalsFinite = internalsFinite && r.finiteInternals;
                return word < 0 ? meanTail(r.grL, static_cast<std::size_t>(0.1f * kFs))
                                : static_cast<double>(r.internals[static_cast<std::size_t>(word)]);
            };
            const double lfFast = grAt(3.0f, 50.0, 24.0f, 4.0f, -1), lfSlow = grAt(6.0f, 50.0, 24.0f, 4.0f, -1);
            const double hfFast = grAt(3.0f, 1000.0, 24.0f, 4.0f, -1), hfSlow = grAt(6.0f, 1000.0, 24.0f, 4.0f, -1);
            std::printf("NOTE     mode.slow: GR at 50 Hz %.4f (FAST) / %.4f (SLOW) dB, at 1 kHz %.4f / %.4f dB\n",
                        lfFast, lfSlow, hfFast, hfSlow);
            P.ge("sharedelement.mode.slow.lf_gr_db", lfFast - lfSlow, 3.0);
            P.le("sharedelement.mode.slow.hf_gr_db", std::fabs(hfFast - hfSlow), 0.3);
            // the limiter (at +4 dBu) holding a 50 Hz tone under a compressor at +10 dBu
            const double limFast = grAt(3.0f, 50.0, 4.0f, 10.0f, 1), limSlow = grAt(6.0f, 50.0, 4.0f, 10.0f, 1);
            std::printf("NOTE     mode.slow: LIMIT GR at 50 Hz %.5f (FAST) / %.5f (SLOW) dB\n", limFast, limSlow);
            P.le("sharedelement.mode.slow.limit_unshaped_db", std::fabs(limFast - limSlow), 1e-3);
        }

        P.eq("sharedelement.mode.internals.nonfinite", internalsFinite ? 0 : 1, 0);
    }
} // namespace

FCMP_PROBE(dsp, sharedelement)
{
    (void) C;
    sharedElementRows(P);
    slowHpRows(P);
    bridgeRows(P);
    modeRows(P);
    return P.finish();
}
