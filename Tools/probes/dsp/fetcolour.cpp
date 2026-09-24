// FCMP_PROBE layer=dsp name=fetcolour scope=global timeout=180
//
// dsp.fetcolour (M2, S9; SPRINTS S9.1; 01 §5.2 law/ and colour/, §10.5; D §2.1; E §2.6-2.7; ADR-25, ADR-63; K2 #1, #4,
// #5a, #14): the policies FET 76 adds, law::FetVcr and stage::FetColour, proven on the policy functions (unit rows,
// 01 §8.4 step 3), and the Mode's defining behaviours through its registered engine (EngineRig, ECO: colour at the base
// rate). Spec rows only (no golden): the Mode-level goldens are dsp.static/time/... of `fet-76`. Every [H] constant and
// its source: docs/modes/fet-76.md.
//
// law::FetVcr (the divider R_s + R_ds; the channel conductance linear in the gate CV):
//   fetcolour.vcr.shunt_max_err          shunt(r) against 1 - 10^(-r/20) in double, r in [0, 60] dB: <= 2e-6
//   fetcolour.vcr.roundtrip_max_err_db   grFromCv(cvVolts(r)) = r below the element's depth (0.01-40 dB): <= 2e-3 dB
//   fetcolour.vcr.resistance_max_rel_err resistanceKohm(r) against R_s / (10^(r/20) - 1), clamped: <= 1e-5 relative
//   fetcolour.vcr.depth_db               maxGrDb() = 20 log10(1 + R_s / R_on): +-1e-3 dB
//   fetcolour.vcr.{cv,resistance}_nonmonotone  CV rises and R_ds falls with r (0 reversals); .open: R = 100 kOhm and
//                                        CV = 0 at 0 dB
// FetColour (per revision <rev> = ln, a, f; the policy designed from EngineParams at 48 kHz):
//   fetcolour.colour.<rev>.share_max_rel_err  (transfer(x, r) - transfer(x, 0)) / shunt(r) does not depend on r
//                                        (the FET residual scales with the FET's share of the divider exactly), |x|
//                                        0.5-1.5, r 6-20 dB against 30 dB: <= 1e-3 (a float difference of transfers)
//   fetcolour.colour.<rev>.process_vs_transfer_max_err  a constant input through process() equals transfer() (the
//                                        static shape IS what the process runs): <= 1e-6
//   fetcolour.colour.<rev>.estimate_{h2,h3}_db  harmonicsEstimate() against analysis::harmonicsDb (64-point DFT of the
//                                        COLOUR view) at amplitude 0.2 and 15 dB of GR: <= 0.5 dB (H2), 1 dB (H3)
//   fetcolour.colour.ln.thd_limiting_pct THD of the default revision in limiting (a -14 dBFS sine at 15 dB of GR):
//                                        <= 0.5 % (D §2.1 [V S2]: "within 0.5 % THD ... with limiting")
//   fetcolour.colour.a_more_than_ln_db   Rev A's limiting THD over LN's: >= +6 dB ("no LN, so more THD", D §2.1)
//   fetcolour.colour.f_h2_below_ln_db    Rev F's H2 at 0 dB of GR (the push-pull output alone) under LN's: >= 6 dB
//   fetcolour.colour.all_h2_over_db      ALL's limiting H2 over the same revision's non-ALL H2: >= 12 dB ("distortion
//                                        increases radically", D §2.1)
//   fetcolour.colour.silence_nonzero     exact zeros in (after a burst) give exact zeros out: 0
//   fetcolour.colour.bs_mismatches       a signal processed in 17-sample calls equals 4096-sample calls, bit for bit: 0
//   fetcolour.colour.nan_propagates, .huge_finite  NaN in -> NaN out; a +140 dBFS input stays finite
//   fetcolour.colour.all_{smooths,lands}  the ALL amount's first control tick moves it by less than a quarter, and it
//                                        lands exactly on 1 within 200 ms of control ticks
// FET 76 through its registered engine (fet-76; 1 kHz square steps: |x| constant, so the detector reads the level):
//   fetcolour.mode.dial.*                INPUT 24 = the fixed threshold kT0 with 0 dB of input gain (unity), the
//                                        maximum gain INPUT 48 + OUTPUT 48 = 45 dB (the fitted dial law), INPUT 0 / 48
//                                        = -22.5 / +22.5 dB
//   fetcolour.mode.input.fixed_threshold_db  INPUT at 12, 24 and 36 with the input 10 dB over its threshold: the gain
//                                        element's output level is the same (INPUT drives a fixed threshold): <= 1e-3
//                                        dB; .detector_thr_mismatches: thrDb (detector domain) is the same: 0
//   fetcolour.mode.ratio.<r>.ratio       4/8/12/20: dIn/dOut between T + 25 and T + 35 dB (above every FB knee)
//                                        against R, relative 8 % (character, 03 §3.7)
//   fetcolour.mode.ratio.onset_order     the input level where GR reaches 0.5 dB rises with the ratio button (higher
//                                        ratios raise the threshold, D §2.1 [V S2]): 1
//   fetcolour.mode.all.nonmonotone       ALL's static FB curve (staticGr) and the engine's settled output over
//                                        T - 20 ... T + 40 dB never fall (K2 #5a): 0; .steeper: its output rises less
//                                        from T + 10 to T + 30 than 20:1's: 1; .attack_ratio: its closed-loop attack
//                                        over 20:1's at the same knob = kAllAttackLag (3) +- 15 %
//   fetcolour.mode.attack.<fs>.<r>.<knob>.ratio  the closed-loop attack (D2's expDb t63 on the tapped GR,
//                                        T - 20 -> T + 20 dB, DUAL) over the published one (fetAttackSpec, ADR-63):
//                                        1 +- 0.15 at 192 kHz (20, 100, 800 us) and at 48 kHz (100, 800 us); 20 us at
//                                        48 kHz is under one sample (NOTE)
//   fetcolour.mode.release.<r>.ratio     the release (T + 20 -> T - 20 dB, the loop opens) over the published one: 1 +-
//                                        0.03 (not converted, ADR-63)
//   fetcolour.mode.attack.hw_reversed    atk and rel carry kFlagHwReversed (the 1176's knobs run fastest clockwise): 1
//   fetcolour.mode.link.min_attack_us    LINK at the 20 us knob: the declared attack is 40 us and the loop's open-loop
//                                        tau is 40 us x (1 + k) (D §2.1 [V S2]); DUAL keeps 20 us
//   fetcolour.mode.gr_switch.*           tmode raw 0, 1, 3 resolve ON and 4, 7 OFF (ON = 0, OFF = 7: every other Mode's
//                                        0/1 is ON, ADR-25): resolve_mismatches 0; OFF sets kEngGrOff and kTagOff; a
//                                        switch to OFF under 15 dB of GR ramps the applied GR monotonically (no rise
//                                        over 1e-4 dB: the settled loop still moves ~1e-5 dB) to exactly 0 in 20 ms (+1
//                                        sample) (ramp_reversals 0, lands 1); under OFF the output is the
//                                        colour stage at GR 0 of the pre-gained input, bit for bit
//                                        (colour_mismatches 0), and the stage is active (colour_active 1)
//   fetcolour.mode.stability.<fs>.<r>.{overshoot_db,reversals,ptp_db}  DUAL, the 20 us knob, a T - 20 -> T + 30 dB
//                                        step at 22.05, 44.1, 48, 96, 192 kHz, every ratio: the attack never passes
//                                        the settled GR (<= 1e-3 dB) nor turns back (0), and the settled GR holds still
//                                        (peak to peak <= 1e-3 dB over 50 ms): the zero-delay loop is stable and
//                                        ring-free
//   fetcolour.mode.stability.naive_ptp_db  E §2.6's naive one-sample-delay loop (4:1, 20 us open loop, 48 kHz,
//                                        20 dB over) buzzes at Nyquist: its settled peak-to-peak >= 10 dB, so the ptp
//                                        rows see an unstable loop
//   fetcolour.mode.internals.*           LOOP CV and FET R are law::FetVcr's at the applied GR (<= 1e-5 relative),
//                                        H2/H3 finite in [-100, 0] dB; under GR OFF: LOOP CV 0 V, FET R 100 kOhm
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/FetColour.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/law/FetVcr.h"
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
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using fcmp::probe::Segment;
    namespace measure = fcmp::probe::measure;
    namespace sig = fcmp::probe::sig;
    using FC = stage::FetColour;
    using Vcr = law::FetVcr;

    constexpr float kFs = 48000.0f;
    constexpr const char* kRevKeys[FC::kRevisionCount] = { "ln", "a", "f" };
    constexpr float kRatioPlain[] = { 0.75f, 0.875f, 0.91667f, 0.95f };    // 4 / 8 / 12 / 20 (Fet76Desc.cpp)
    constexpr double kRatioR[] = { 4.0, 8.0, 12.0, 20.0 };
    constexpr const char* kRatioKeys[] = { "r4", "r8", "r12", "r20" };
    constexpr float kAllPlain = 1.0f;
    constexpr double kAllAttackLag = 3.0;                                   // Fet76Desc.cpp kAllAttackLag

    double dbD(double a) { return measure::dbFromAmplitude(a); }

    // ---- law::FetVcr -----------------------------------------------------------------------------------------------
    void vcrRows(Probe& P)
    {
        const double rs = static_cast<double>(Vcr::kSeriesKohm), ron = static_cast<double>(Vcr::kOnKohm);
        double shuntErr = 0.0, roundErr = 0.0, resErr = 0.0;
        std::int64_t cvRev = 0, resRev = 0;
        float cvPrev = -1.0f, resPrev = 1e9f;
        for (int i = 0; i <= 6000; ++i)
        {
            const double r = 0.01 * i;
            const auto rf = static_cast<float>(r);
            shuntErr = std::max(shuntErr, std::fabs(static_cast<double>(Vcr::shunt(rf))
                                                    - (1.0 - measure::amplitudeFromDb(-static_cast<double>(rf)))));
            const float cv = Vcr::cvVolts(rf), res = Vcr::resistanceKohm(rf);
            cvRev += cv < cvPrev ? 1 : 0;
            resRev += res > resPrev ? 1 : 0;
            cvPrev = cv;
            resPrev = res;
            if (r >= 0.01 && r <= 40.0)
                roundErr = std::max(roundErr,
                                    std::fabs(static_cast<double>(Vcr::grFromCv(cv)) - static_cast<double>(rf)));
            const double want = std::clamp(rs / (measure::amplitudeFromDb(static_cast<double>(rf)) - 1.0), ron,
                                           static_cast<double>(Vcr::kOpenKohm));
            if (r > 0.0)
                resErr = std::max(resErr, std::fabs(static_cast<double>(res) - want) / want);
        }
        std::printf("NOTE     fetcolour.vcr: depth %.4f dB; R_ds at 6 / 20 / 40 dB = %.3f / %.3f / %.3f kOhm; "
                    "CV %.4f / %.4f / %.4f V\n",
                    static_cast<double>(Vcr::maxGrDb()), static_cast<double>(Vcr::resistanceKohm(6.0f)),
                    static_cast<double>(Vcr::resistanceKohm(20.0f)), static_cast<double>(Vcr::resistanceKohm(40.0f)),
                    static_cast<double>(Vcr::cvVolts(6.0f)), static_cast<double>(Vcr::cvVolts(20.0f)),
                    static_cast<double>(Vcr::cvVolts(40.0f)));
        P.le("fetcolour.vcr.shunt_max_err", shuntErr, 2e-6);
        P.le("fetcolour.vcr.roundtrip_max_err_db", roundErr, 2e-3);
        P.le("fetcolour.vcr.resistance_max_rel_err", resErr, 1e-5);
        P.near("fetcolour.vcr.depth_db", static_cast<double>(Vcr::maxGrDb()), dbD(1.0 + rs / ron), 1e-3);
        P.eq("fetcolour.vcr.cv_nonmonotone", cvRev, 0);
        P.eq("fetcolour.vcr.resistance_nonmonotone", resRev, 0);
        P.eq("fetcolour.vcr.open", Vcr::resistanceKohm(0.0f) == Vcr::kOpenKohm && Vcr::cvVolts(0.0f) == 0.0f ? 1 : 0,
             1);
    }

    // ---- FetColour -------------------------------------------------------------------------------------------------
    EngineParams colourParams(int revision, bool all)
    {
        EngineParams p;
        p.voice = static_cast<std::uint8_t>(revision);
        p.tags = all ? static_cast<std::uint32_t>(kTagAll) : 0u;
        return p;
    }

    FC::Coeffs designed(const EngineParams& p)
    {
        FC::Coeffs c{};
        FC::design(c, p, StageCtx{ kFs, kFs, 1, {} });
        return c;
    }

    // THD (H2-H8 over H1) in dB of the COLOUR view at amplitude `amp` and `grDb`, and H2 / H3.
    struct Harm
    {
        double h2, h3, thd;
    };
    Harm harmonics(const ModeEntry& en, const EngineParams& p, float grDb, float amp)
    {
        std::array<float, 8> h{};
        analysis::harmonicsDb(en, p, grDb, amp, h);
        double sum = 0.0;
        for (std::size_t i = 1; i < h.size(); ++i)
            sum += std::pow(10.0, static_cast<double>(h[i]) / 10.0);
        return { static_cast<double>(h[1]), static_cast<double>(h[2]), 10.0 * std::log10(std::max(sum, 1e-30)) };
    }

    void colourRows(Probe& P, const ModeEntry& en)
    {
        constexpr float kLimitAmp = 0.2f, kLimitGr = 15.0f;          // a -14 dBFS sine at 15 dB of GR (limiting)
        Harm lim[FC::kRevisionCount]{}, zero[FC::kRevisionCount]{};
        for (int rev = 0; rev < FC::kRevisionCount; ++rev)
        {
            const std::string k = std::string("fetcolour.colour.") + kRevKeys[rev];
            const EngineParams p = colourParams(rev, false);
            const FC::Coeffs c = designed(p);

            // the FET residual scales with the FET's share exactly
            double shareErr = 0.0;
            for (const float x : { -1.5f, -0.9f, -0.5f, 0.5f, 0.9f, 1.5f })
            {
                const double base = static_cast<double>(FC::transfer(c, x, 0.0f));
                const double ref = (static_cast<double>(FC::transfer(c, x, 30.0f)) - base)
                                 / static_cast<double>(Vcr::shunt(30.0f));
                for (const float g : { 6.0f, 10.0f, 15.0f, 20.0f })
                {
                    const double got = (static_cast<double>(FC::transfer(c, x, g)) - base)
                                     / static_cast<double>(Vcr::shunt(g));
                    shareErr = std::max(shareErr, std::fabs(got - ref) / std::max(std::fabs(ref), 1e-12));
                }
            }
            P.le(k + ".share_max_rel_err", shareErr, 1e-3);

            // a constant input through process() equals transfer()
            double pvt = 0.0;
            for (const float x : { -0.8f, -0.2f, 0.05f, 0.4f, 1.2f })
                for (const float g : { 0.0f, 10.0f })
                {
                    FC::State s{};
                    std::array<float, 8> buf{}, gr{};
                    buf.fill(x);
                    gr.fill(g);
                    FC::process(c, s, buf.data(), gr.data(), static_cast<int>(buf.size()), 0);
                    for (std::size_t i = 1; i < buf.size(); ++i)
                        pvt = std::max(pvt, std::fabs(static_cast<double>(buf[i]) - FC::transfer(c, x, g)));
                }
            P.le(k + ".process_vs_transfer_max_err", pvt, 1e-6);

            // the Taylor estimate against the DFT
            lim[rev] = harmonics(en, p, kLimitGr, kLimitAmp);
            zero[rev] = harmonics(en, p, 0.0f, kLimitAmp);
            const FC::Harmonics est = FC::harmonicsEstimate(c, kLimitAmp, kLimitGr);
            std::printf("NOTE     %s: limiting (amp %.2f, GR %.0f dB) H2 %.2f dB, H3 %.2f dB, THD %.2f dB (%.4f %%); "
                        "estimate H2 %.2f, H3 %.2f; no GR: H2 %.2f, H3 %.2f, THD %.2f dB\n",
                        k.c_str(), static_cast<double>(kLimitAmp), static_cast<double>(kLimitGr), lim[rev].h2,
                        lim[rev].h3, lim[rev].thd, 100.0 * std::pow(10.0, lim[rev].thd / 20.0),
                        static_cast<double>(est.h2Db), static_cast<double>(est.h3Db), zero[rev].h2, zero[rev].h3,
                        zero[rev].thd);
            P.le(k + ".estimate_h2_db", std::fabs(static_cast<double>(est.h2Db) - lim[rev].h2), 0.5);
            P.le(k + ".estimate_h3_db", std::fabs(static_cast<double>(est.h3Db) - lim[rev].h3), 1.0);
        }
        P.le("fetcolour.colour.ln.thd_limiting_pct", 100.0 * std::pow(10.0, lim[0].thd / 20.0), 0.5);
        P.ge("fetcolour.colour.a_more_than_ln_db", lim[1].thd - lim[0].thd, 6.0);
        P.ge("fetcolour.colour.f_h2_below_ln_db", zero[0].h2 - zero[2].h2, 6.0);
        const Harm all = harmonics(en, colourParams(0, true), kLimitGr, kLimitAmp);
        std::printf("NOTE     fetcolour.colour.all: LN + ALL limiting H2 %.2f dB, H3 %.2f dB, THD %.2f dB (%.3f %%)\n",
                    all.h2, all.h3, all.thd, 100.0 * std::pow(10.0, all.thd / 20.0));
        P.ge("fetcolour.colour.all_h2_over_db", all.h2 - lim[0].h2, 12.0);

        // silence, block-size invariance, poison
        {
            const FC::Coeffs c = designed(colourParams(0, false));
            std::vector<float> x(4096), gr(4096, 12.0f);
            sig::Pcg32 rng(0x66657463, 3);
            for (std::size_t i = 0; i < 2048; ++i)
                x[i] = 0.6f * rng.bipolar();
            std::vector<float> a = x, b = x;
            FC::State sa{}, sb{};
            FC::process(c, sa, a.data(), gr.data(), static_cast<int>(a.size()), 0);
            for (std::size_t off = 0; off < b.size(); off += 17)
            {
                const auto len = static_cast<int>(std::min<std::size_t>(17, b.size() - off));
                FC::process(c, sb, b.data() + off, gr.data() + off, len, 0);
            }
            std::int64_t nz = 0, bs = 0;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                nz += i >= 2048 && a[i] != 0.0f ? 1 : 0;
                bs += a[i] == b[i] ? 0 : 1;
            }
            P.eq("fetcolour.colour.silence_nonzero", nz, 0);
            P.eq("fetcolour.colour.bs_mismatches", bs, 0);
            std::array<float, 8> nan{}, huge{}, g0{};
            nan.fill(std::numeric_limits<float>::quiet_NaN());
            huge.fill(1e7f);
            FC::State s1{}, s2{};
            FC::process(c, s1, nan.data(), g0.data(), 8, 0);
            FC::process(c, s2, huge.data(), g0.data(), 8, 0);
            P.eq("fetcolour.colour.nan_propagates", std::isnan(nan[7]) ? 1 : 0, 1);
            const bool finite = std::all_of(huge.begin(), huge.end(), [](float v) { return std::isfinite(v); });
            P.eq("fetcolour.colour.huge_finite", finite ? 1 : 0, 1);
        }

        // the ALL amount: smoothed per tick, landing exactly
        {
            FC::Coeffs c = designed(colourParams(0, false));
            const EngineParams on = colourParams(0, true);
            const StageCtx ctx{ kFs, kFs, 1, {} };
            FC::design(c, on, ctx);
            const float first = c.all;
            const int ticks = static_cast<int>(0.2f * kFs) / kTickSamples;
            for (int t = 1; t < ticks; ++t)
                FC::design(c, on, ctx);
            P.eq("fetcolour.colour.all_smooths", first > 0.0f && first < 0.25f ? 1 : 0, 1);
            P.eq("fetcolour.colour.all_lands", c.all == 1.0f ? 1 : 0, 1);
        }
    }

    // ---- FET 76 through its engine ---------------------------------------------------------------------------------
    struct Fet
    {
        const ModeEntry& en;
        RawParams base;
    };

    Resolution resolveWith(const Fet& f, std::initializer_list<std::pair<Pid, float>> set)
    {
        RawParams raw = f.base;
        for (const auto& [pid, v] : set)
            raw[pid] = v;
        return fcmp::probe::resolveRaw(f.en, raw);
    }

    // The closed-loop t63 (expDb) of the step T - 20 -> T + 20 dB and the release T + 20 -> T - 20 (seconds).
    struct Times
    {
        double atk = -1, rel = -1;
    };
    Times stepTimes(const Fet& f, const EngineParams& e, float fs, double holdS, bool release)
    {
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        const double tail = release ? std::max(0.5, 8.0 * static_cast<double>(e.relTauMs) / 1000.0) : 0.01;
        const Segment segs[] = { { t - 20.0, 0.05 }, { t + 20.0, holdS }, { t - 20.0, tail } };
        fcmp::probe::EngineRig rig(f.en, e, fs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1], b = run.edges[2];
        const std::span<const float> tap(run.tapGrDb);
        Times out;
        out.atk =
            measure::lawSeconds(tap.subspan(a, b - a), run.tapGrDb[a - 1], run.tapGrDb[b - 1], fs, TimeLaw::expDb);
        if (release)
            out.rel = measure::lawSeconds(tap.subspan(b), run.tapGrDb[b - 1], 0.0, fs, TimeLaw::expDb);
        return out;
    }

    // The settled output (dB, before makeup: the gain element's output) of a square at input `level` after `holdS`.
    double settledOut(const Fet& f, const EngineParams& e, double level, double holdS = 0.3)
    {
        const Segment segs[] = { { level, holdS } };
        fcmp::probe::EngineRig rig(f.en, e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        return level + static_cast<double>(e.preGainDb) - static_cast<double>(run.tapGrDb.back());
    }

    void dialRows(Probe& P, const Fet& f)
    {
        const ParamView& v = fcmp::probe::resolveRaw(f.en, f.base).view;
        const DisplayMap& in = v.spec[idx(Pid::thr)]->display;
        const DisplayMap& out = v.spec[idx(Pid::makeup)]->display;
        const auto preAt = [&](float dial) {
            return resolveWith(f, { { Pid::thr, in.toPlain(dial) } }).eng.preGainDb;
        };
        const Resolution unity = resolveWith(f, { { Pid::thr, in.toPlain(24.0f) } });
        std::printf("NOTE     fetcolour.mode.dial: INPUT 24 = thr %.4g dBFS (detector threshold %.4g at 4:1), "
                    "INPUT 0 / 48 = %+.4g / %+.4g dB, OUTPUT 48 = %+.4g dB\n",
                    static_cast<double>(unity.view[Pid::thr].plain), static_cast<double>(unity.eng.thrDb),
                    static_cast<double>(preAt(0.0f)), static_cast<double>(preAt(48.0f)),
                    static_cast<double>(out.toPlain(48.0f)));
        P.near("fetcolour.mode.dial.unity_gain_db", static_cast<double>(unity.eng.preGainDb), 0.0, 1e-6);
        P.near("fetcolour.mode.dial.unity_is_threshold_db", static_cast<double>(unity.view[Pid::thr].plain),
               static_cast<double>(unity.eng.thrDb), 1e-6);
        P.near("fetcolour.mode.dial.max_gain_db", static_cast<double>(preAt(48.0f) + out.toPlain(48.0f)), 45.0, 1e-4);
        P.near("fetcolour.mode.dial.input0_db", static_cast<double>(preAt(0.0f)), -22.5, 1e-4);
        P.near("fetcolour.mode.dial.input48_db", static_cast<double>(preAt(48.0f)), 22.5, 1e-4);
    }

    void inputRows(Probe& P, const Fet& f)
    {
        const ParamView& v = fcmp::probe::resolveRaw(f.en, f.base).view;
        const DisplayMap& in = v.spec[idx(Pid::thr)]->display;
        double lo = 1e9, hi = -1e9;
        std::int64_t thrMismatch = 0;
        float thr0 = 0.0f;
        bool first = true;
        for (const float dial : { 12.0f, 24.0f, 36.0f })
        {
            const EngineParams e = resolveWith(f, { { Pid::thr, in.toPlain(dial) }, { Pid::atk, 0.02f } }).eng;
            const double t = static_cast<double>(analysis::inputThresholdDb(e));
            const double o = settledOut(f, e, t + 10.0);
            std::printf("NOTE     fetcolour.mode.input: INPUT %.0f: input threshold %.3f dBFS, gain %+.3f dB, the "
                        "element's output %.5f dBFS at T + 10\n",
                        static_cast<double>(dial), t, static_cast<double>(e.preGainDb), o);
            lo = std::min(lo, o);
            hi = std::max(hi, o);
            if (first)
                thr0 = e.thrDb;
            thrMismatch += e.thrDb == thr0 ? 0 : 1;
            first = false;
        }
        P.le("fetcolour.mode.input.fixed_threshold_db", hi - lo, 1e-3);
        P.eq("fetcolour.mode.input.detector_thr_mismatches", thrMismatch, 0);
    }

    void ratioRows(Probe& P, const Fet& f)
    {
        double onset[4]{};
        for (std::size_t r = 0; r < 4; ++r)
        {
            const EngineParams e = resolveWith(f, { { Pid::ratio, kRatioPlain[r] }, { Pid::atk, 0.02f } }).eng;
            const double t = static_cast<double>(analysis::inputThresholdDb(e));
            const double o25 = settledOut(f, e, t + 25.0), o35 = settledOut(f, e, t + 35.0);
            const double ratio = 10.0 / (o35 - o25);
            std::printf("NOTE     fetcolour.mode.ratio.%s: input threshold %.2f dBFS, measured %.4g:1 "
                        "(T + 25 ... T + 35)\n",
                        kRatioKeys[r], t, ratio);
            P.near(std::string("fetcolour.mode.ratio.") + kRatioKeys[r] + ".ratio", ratio, kRatioR[r], 0.0, 0.08);
            // onset: the static FB curve (staticGr) reaches 0.5 dB, input-referred
            double x = t - 10.0;
            for (; x < t + 10.0; x += 0.01)
            {
                const auto xd = static_cast<float>(x) + e.preGainDb;
                float gr = 0.0f;
                f.en.staticGr(e, &xd, &gr, 1);
                if (gr >= 0.5f)
                    break;
            }
            onset[r] = x;
        }
        std::printf("NOTE     fetcolour.mode.ratio: 0.5 dB onset at %.2f / %.2f / %.2f / %.2f dBFS (4 / 8 / 12 / 20)\n",
                    onset[0], onset[1], onset[2], onset[3]);
        const bool rising = onset[0] < onset[1] && onset[1] < onset[2] && onset[2] < onset[3];
        P.eq("fetcolour.mode.ratio.onset_order", rising ? 1 : 0, 1);
    }

    void allRows(Probe& P, const Fet& f)
    {
        const EngineParams all = resolveWith(f, { { Pid::ratio, kAllPlain }, { Pid::atk, 0.02f } }).eng;
        const EngineParams r20 = resolveWith(f, { { Pid::ratio, kRatioPlain[3] }, { Pid::atk, 0.02f } }).eng;
        const double t = static_cast<double>(analysis::inputThresholdDb(all));
        std::int64_t falls = 0;
        float prev = -1e9f;
        for (double x = t - 20.0; x <= t + 40.0; x += 0.05)
        {
            const auto xd = static_cast<float>(x) + all.preGainDb;
            float gr = 0.0f;
            f.en.staticGr(all, &xd, &gr, 1);
            const float y = xd - gr;
            falls += y < prev ? 1 : 0;
            prev = y;
        }
        double oPrev = -1e9;
        for (double x = t - 20.0; x <= t + 40.0; x += 5.0)
        {
            const double o = settledOut(f, all, x);
            falls += o < oPrev - 1e-6 ? 1 : 0;
            oPrev = o;
        }
        P.eq("fetcolour.mode.all.nonmonotone", falls, 0);
        const double tr = static_cast<double>(analysis::inputThresholdDb(r20));
        const double riseAll = settledOut(f, all, t + 30.0) - settledOut(f, all, t + 10.0);
        const double rise20 = settledOut(f, r20, tr + 30.0) - settledOut(f, r20, tr + 10.0);
        std::printf("NOTE     fetcolour.mode.all: output rise T + 10 -> T + 30: ALL %.4f dB, 20:1 %.4f dB\n", riseAll,
                    rise20);
        P.eq("fetcolour.mode.all.steeper", riseAll < rise20 ? 1 : 0, 1);

        RawParams dual = f.base;
        dual[Pid::link] = 0.0f;
        const Fet fd{ f.en, dual };
        const EngineParams aAll = resolveWith(fd, { { Pid::ratio, kAllPlain }, { Pid::atk, 0.2f } }).eng;
        const EngineParams a20 = resolveWith(fd, { { Pid::ratio, kRatioPlain[3] }, { Pid::atk, 0.2f } }).eng;
        const double tAll = stepTimes(fd, aAll, 192000.0f, 0.2, false).atk,
                     t20 = stepTimes(fd, a20, 192000.0f, 0.2, false).atk;
        std::printf("NOTE     fetcolour.mode.all: closed-loop attack at the 200 us knob, 192 kHz: ALL %.4g us, "
                    "20:1 %.4g us\n",
                    1e6 * tAll, 1e6 * t20);
        P.near("fetcolour.mode.all.attack_ratio", tAll / t20, kAllAttackLag, 0.0, 0.15);
    }

    void timeRows(Probe& P, const Fet& f)
    {
        RawParams dual = f.base;
        dual[Pid::link] = 0.0f;
        const Fet fd{ f.en, dual };
        for (const float fs : { 192000.0f, 48000.0f })
            for (std::size_t r = 0; r < 4; ++r)
                for (const float knob : { 0.02f, 0.1f, 0.8f })
                {
                    const Resolution res = resolveWith(fd, { { Pid::ratio, kRatioPlain[r] }, { Pid::atk, knob } });
                    const double pub = static_cast<double>(f.en.desc->attackSpec(res.view, res.eng).seconds);
                    const double got = stepTimes(fd, res.eng, fs, 0.05, false).atk;
                    const std::string k = "fetcolour.mode.attack." + std::to_string(static_cast<long>(fs)) + "."
                                        + kRatioKeys[r] + ".k"
                                        + std::to_string(static_cast<long>(std::lround(knob * 1000.0f)));
                    std::printf("NOTE     %s: published %.4g us, closed loop %.4g us (x%.4f), open-loop tau %.4g us, "
                                "%.2f samples\n",
                                k.c_str(), 1e6 * pub, 1e6 * got, got / pub, 1e3 * static_cast<double>(res.eng.atkTauMs),
                                pub * static_cast<double>(fs));
                    if (pub * static_cast<double>(fs) >= 3.0)
                        P.near(k + ".ratio", got / pub, 1.0, 0.15);
                }
        for (std::size_t r = 0; r < 4; ++r)
        {
            const Resolution res = resolveWith(fd, { { Pid::ratio, kRatioPlain[r] }, { Pid::atk, 0.1f } });
            const double pub = static_cast<double>(f.en.desc->releaseSpec(res.view, res.eng).seconds);
            const double got = stepTimes(fd, res.eng, kFs, 0.2, true).rel;
            std::printf("NOTE     fetcolour.mode.release.%s: published %.4g s, measured %.4g s\n", kRatioKeys[r], pub,
                        got);
            P.near(std::string("fetcolour.mode.release.") + kRatioKeys[r] + ".ratio", got / pub, 1.0, 0.03);
        }
        const ParamView& v = fcmp::probe::resolveRaw(f.en, f.base).view;
        const bool reversed = (v.spec[idx(Pid::atk)]->flags & kFlagHwReversed) != 0
                           && (v.spec[idx(Pid::rel)]->flags & kFlagHwReversed) != 0;
        P.eq("fetcolour.mode.attack.hw_reversed", reversed ? 1 : 0, 1);

        const Resolution linked = resolveWith(f, { { Pid::link, 1.0f }, { Pid::atk, 0.02f } });
        const Resolution dualR = resolveWith(f, { { Pid::link, 0.0f }, { Pid::atk, 0.02f } });
        const double k = static_cast<double>(stage::QuadKnee::loopGain(linked.eng.slope));
        P.near("fetcolour.mode.link.min_attack_us",
               1e6 * static_cast<double>(f.en.desc->attackSpec(linked.view, linked.eng).seconds), 40.0, 1e-3);
        P.near("fetcolour.mode.link.open_loop_tau_us", 1e3 * static_cast<double>(linked.eng.atkTauMs), 40.0 * (1.0 + k),
               1e-3);
        P.near("fetcolour.mode.link.dual_attack_us",
               1e6 * static_cast<double>(f.en.desc->attackSpec(dualR.view, dualR.eng).seconds), 20.0, 1e-3);
    }

    void grSwitchRows(Probe& P, const Fet& f)
    {
        std::int64_t bad = 0;
        for (const float raw : { 0.0f, 1.0f, 3.0f, 4.0f, 7.0f })
        {
            const Resolution res = resolveWith(f, { { Pid::tmode, raw } });
            const bool off = (res.view[Pid::tmode].tag & kTagOff) != 0 && (res.eng.flags & kEngGrOff) != 0;
            bad += off == (raw > 3.5f) ? 0 : 1;
        }
        P.eq("fetcolour.mode.gr_switch.resolve_mismatches", bad, 0);

        const EngineParams on = resolveWith(f, { { Pid::atk, 0.02f } }).eng;
        EngineParams off = resolveWith(f, { { Pid::atk, 0.02f }, { Pid::tmode, 7.0f } }).eng;
        const double t = static_cast<double>(analysis::inputThresholdDb(on));
        fcmp::probe::EngineRig rig(f.en, on, kFs);
        const std::size_t n = static_cast<std::size_t>(0.3f * kFs), m = static_cast<std::size_t>(0.1f * kFs);
        std::vector<float> in(n + m), outL(n + m), outR(n + m);
        const auto amp = static_cast<float>(measure::amplitudeFromDb(t + 20.0));
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = (i / 24) % 2 == 0 ? amp : -amp;
        rig.process(in.data(), in.data(), outL.data(), outR.data(), n);
        rig.setTapping(true);
        rig.process(in.data() + n, in.data() + n, outL.data() + n, outR.data() + n, 1);
        const float grOn = rig.tap().lane(rig.tap().grDb, 0).back();
        float words[kInternals];
        rig.engine().internals(words);
        std::printf("NOTE     fetcolour.mode.internals: GR %.4f dB: LOOP CV %.5f V, FET R %.4f kOhm, H2 %.2f dB, "
                    "H3 %.2f dB\n",
                    static_cast<double>(grOn), static_cast<double>(words[0]), static_cast<double>(words[1]),
                    static_cast<double>(words[2]), static_cast<double>(words[3]));
        const double cvWant = static_cast<double>(Vcr::cvVolts(grOn));
        const double rWant = static_cast<double>(Vcr::resistanceKohm(grOn));
        P.le("fetcolour.mode.internals.cv_rel_err",
             std::fabs(static_cast<double>(words[0]) - cvWant) / std::max(1e-9, cvWant), 1e-5);
        P.le("fetcolour.mode.internals.r_rel_err", std::fabs(static_cast<double>(words[1]) - rWant) / rWant, 1e-5);
        P.eq("fetcolour.mode.internals.harmonics_in_range",
             words[2] >= -100.0f && words[2] <= 0.0f && words[3] >= -100.0f && words[3] <= 0.0f ? 1 : 0, 1);
        rig.tap().clear();

        // switch OFF: the applied GR ramps monotonically to exactly 0 in 20 ms
        rig.setParams(off);
        rig.process(in.data() + n + 1, in.data() + n + 1, outL.data() + n + 1, outR.data() + n + 1, m - 1);
        const std::vector<float> gr = rig.tap().lane(rig.tap().grDb, 0);
        std::int64_t rev = 0;
        for (std::size_t i = 1; i < gr.size(); ++i)                 // the loop's own settling is ~1e-5 dB
            rev += gr[i] > gr[i - 1] + 1e-4f ? 1 : 0;
        const std::size_t land = static_cast<std::size_t>(0.02f * kFs) + 2;
        bool lands = true;
        for (std::size_t i = land; i < gr.size(); ++i)
            lands = lands && gr[i] == 0.0f;
        const bool before = gr[land - 4] > 0.0f;
        P.eq("fetcolour.mode.gr_switch.ramp_reversals", rev, 0);
        P.eq("fetcolour.mode.gr_switch.lands", lands && before ? 1 : 0, 1);
        rig.engine().internals(words);
        P.eq("fetcolour.mode.internals.off_open", words[0] == 0.0f && words[1] == Vcr::kOpenKohm ? 1 : 0, 1);

        // under OFF, the output is the colour stage at GR 0 of the pre-gained input, and the stage is active
        {
            fcmp::probe::EngineRig r2(f.en, off, kFs);
            const std::size_t len = 4096;
            std::vector<float> x(len), yl(len), yr(len), ref(len), g0(len, 0.0f);
            for (std::size_t i = 0; i < len; ++i)
                x[i] = static_cast<float>(0.5 * sig::sinTurns(static_cast<double>(i) * 997.0 / 48000.0));
            r2.process(x.data(), x.data(), yl.data(), yr.data(), len);
            const float pg = simd::lane<0>(linFromDb(simd::set1(off.preGainDb)));
            const float mk = simd::lane<0>(linFromDb(simd::set1(off.makeupDb)));
            FC::Coeffs c{};
            FC::design(c, off, StageCtx{ kFs, kFs, 1, {} });
            FC::State s{};
            for (std::size_t i = 0; i < len; ++i)
                ref[i] = x[i] * pg;
            for (std::size_t off0 = 0; off0 < len; off0 += static_cast<std::size_t>(kChunk))
                FC::process(c, s, ref.data() + off0, g0.data() + off0, kChunk, 0);
            std::int64_t mism = 0;
            double resid = 0.0;
            for (std::size_t i = 0; i < len; ++i)
            {
                const float want = off.mix * (ref[i] * mk) + (1.0f - off.mix) * x[i];
                mism += yl[i] == want ? 0 : 1;
                resid = std::max(resid, std::fabs(static_cast<double>(ref[i]) - static_cast<double>(x[i] * pg)));
            }
            P.eq("fetcolour.mode.gr_switch.colour_mismatches", mism, 0);
            P.eq("fetcolour.mode.gr_switch.colour_active", resid > 0.0 ? 1 : 0, 1);
        }
    }

    void stabilityRows(Probe& P, const Fet& f)
    {
        RawParams dual = f.base;
        dual[Pid::link] = 0.0f;
        dual[Pid::atk] = 0.02f;
        const Fet fd{ f.en, dual };
        for (const float fs : { 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f })
            for (std::size_t r = 0; r < 5; ++r)
            {
                const float plain = r < 4 ? kRatioPlain[r] : kAllPlain;
                const EngineParams e = resolveWith(fd, { { Pid::ratio, plain } }).eng;
                const double t = static_cast<double>(analysis::inputThresholdDb(e));
                const Segment segs[] = { { t - 20.0, 0.05 }, { t + 30.0, 0.25 } };
                fcmp::probe::EngineRig rig(f.en, e, fs);
                const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
                const std::size_t a = run.edges[1];
                const float settled = run.tapGrDb.back();
                double over = 0.0;
                std::int64_t rev = 0;
                for (std::size_t i = a; i < run.tapGrDb.size(); ++i)
                {
                    over = std::max(over, static_cast<double>(run.tapGrDb[i] - settled));
                    rev += i > a && run.tapGrDb[i] < run.tapGrDb[i - 1] ? 1 : 0;
                }
                const std::size_t win = static_cast<std::size_t>(0.05f * fs);
                const auto [lo, hi] = std::minmax_element(run.tapGrDb.end() - static_cast<std::ptrdiff_t>(win),
                                                          run.tapGrDb.end());
                const std::string k = "fetcolour.mode.stability." + std::to_string(static_cast<long>(fs)) + "."
                                    + (r < 4 ? kRatioKeys[r] : "all");
                P.le(k + ".overshoot_db", over, 1e-3);
                P.eq(k + ".reversals", rev, 0);
                P.le(k + ".ptp_db", static_cast<double>(*hi - *lo), 1e-3);
                P.eq(k + ".nonfinite", run.nonfinite, 0);
            }

        // E §2.6's naive loop: r[n] = alpha r[n-1] + (1 - alpha) k max(0, x - r[n-1] - T), 4:1, tau 20 us at 48 kHz
        const double alpha = std::exp(-1.0 / (20e-6 * 48000.0)), k = 3.0, over = 20.0;
        double r = 0.0, lo = 1e9, hi = -1e9;
        for (int i = 0; i < 2000; ++i)
        {
            r = alpha * r + (1.0 - alpha) * k * std::max(0.0, over - r);
            if (i >= 1900)
            {
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
        }
        std::printf("NOTE     fetcolour.mode.stability.naive: GR alternates %.2f / %.2f dB (E §2.6: 24.88 / 8.78)\n",
                    hi, lo);
        P.ge("fetcolour.mode.stability.naive_ptp_db", hi - lo, 10.0);
    }
} // namespace

FCMP_PROBE(dsp, fetcolour)
{
    const ModeEntry& en = fcmp::probe::modeEntry("fet-76");
    vcrRows(P);
    colourRows(P, en);
    RawParams base = fcmp::probe::modeRaw(en);
    const Fet f{ en, base };
    dialRows(P, f);
    inputRows(P, f);
    ratioRows(P, f);
    allRows(P, f);
    timeRows(P, f);
    grSwitchRows(P, f);
    stabilityRows(P, f);
    return P.finish();
}
