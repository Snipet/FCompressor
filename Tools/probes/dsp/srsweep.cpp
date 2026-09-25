// FCMP_PROBE layer=dsp name=srsweep scope=mode timeout=120
//
// dsp.srsweep.<key> (F9, S3; D11: 03 §3.4, C §5.8; K2 #5c; S3 lead revision 3; Rig driver, K3 #10): the Mode across
// sample rates, through its own engine (EngineRig, ECO: colour at the base rate).
//
// Rates: 44.1, 48, 88.2, 96, 176.4 and 192 kHz are judged against 48 kHz; 22.05 and 384 kHz are robustness rates (C
// §5.8 D11: "robustness only"): judged for stability and finiteness, their curve and times printed (NOTE). The 22.05
// kHz rows are the FB stability guard's (K2 #5c: "a 22.05 kHz stability row for any Mode on FeedbackDelayed"); they run
// for every Mode, since a probe cannot see the traits.
//
// Per rate <fs> (Hz), at the Mode's defaults:
//   fidelity (tolerance by Rigor, 03 §3.7; NOTE while provisional):
//     srsweep.<fs>.curve_vs_48k_db       reduced D1 (1 kHz sine, fastest attack, slowest release; T - 10, -3, 0, +3,
//                                        +10, +20 dB): the output level against 48 kHz's, max over the levels
//     srsweep.<fs>.{atk,rel}.tau_ratio   D2 (1 kHz square steps, dsp.time's stimulus): the attack and release times
//                                        by the declared TimeLaw, over 48 kHz's (1 +- tauFsRatio); an attack published
//                                        below one sample at 48 kHz (instantaneous) is .atk.instant_diff_s instead:
//                                        |tau - tau(48k)| <= kTauMinSamples / 48 kHz (M7, S11)
//     srsweep.<fs>.hold_s                a live `hold` at 50 ms (clamped to its range): the GR stays exactly still for
//                                        the hold after the level drops (max(tauRel x hold, 1.5 / fs))
//     srsweep.<fs>.auto.tau_ratio        a `tmode` step tagged AUTO: the release over 48 kHz's
//     srsweep.<fs>.auto.rel_vs_telemetry the release the Mode reports (EngineTelemetry::releaseNowMs, the REL EFF
//                                        readout) is the release it runs, by the declared law (a square's constant
//                                        crest keeps an automatic release constant)
//   structural (spec, blocking; every rate):
//     srsweep.<fs>.nonfinite, .gr_min_db, .stable_ptp_db (the settled GR of a constant-level square moves <= 1e-3 dB
//                                        peak-to-peak over the last 50 ms: no oscillation)
//   at 48 and 384 kHz, the long-release precision (S3 lead revision 3; SmoothBranching.h "float precision"):
//     srsweep.<fs>.long_release.max_dev_db   the Mode's slowest release from 20 dB to 10 dB of GR (4 tau): the GR
//                                        against the exact exponential of its own one-pole, <= 1e-3 dB (the plain
//                                        float recurrence stalled 0.11 dB short at 48 kHz and 0.9 dB at 384 kHz); run
//                                        when the Mode's release is live and its slowest release resolves to a
//                                        feed-forward kernel. A feedback kernel's release is not its one-pole's
//                                        exponential (the loop gain k shortens it: the closed loop decays by
//                                        (1 - c) / (1 + c k) per sample, E §2.6), so FB prints a NOTE (DW, S4: FET 76);
//                                        its sub-ulp carry (FbAffine's base GR, S10 X10) is judged by dsp.fbsolve's
//                                        carry rows against the exact FB recurrence
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Measure.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
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
    using fcmp::probe::Segment;
    namespace tolns = fcmp::probe::tol;

    struct Rate
    {
        float fs;
        bool judged;                            // against 48 kHz (else a robustness rate)
    };
    constexpr Rate kRates[] = { { 48000.0f, true },  { 44100.0f, true },  { 88200.0f, true }, { 96000.0f, true },
                                { 176400.0f, true }, { 192000.0f, true }, { 22050.0f, false }, { 384000.0f, false } };

    std::string rateKey(float fs) { return "srsweep." + std::to_string(static_cast<long>(fs)); }

    float timeEdge(const ParamView& v, Pid pid, bool fastest)
    {
        const ParamSpec* s = v.spec[idx(pid)];
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
            return fastest ? s->steps.front().plain : s->steps.back().plain;
        if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            return fastest ? s->lo : s->hi;
        return v[pid].plain;
    }

    bool continuousLive(const ParamSpec* s)
    {
        return s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid);
    }

    // The staircase's measured output level per level (dB), the run's non-finite count and smallest GR.
    struct Curve
    {
        std::vector<double> out;
        std::int64_t nonfinite = 0;
        double minGr = 0;
    };
    Curve reducedD1(const ModeEntry& en, const EngineParams& e, float fs)
    {
        const double t = analysis::inputThresholdDb(e);
        std::vector<double> levels;
        for (const double d : { -10.0, -3.0, 0.0, 3.0, 10.0, 20.0 })
            levels.push_back(t + d);
        fcmp::probe::EngineRig rig(en, e, fs);
        const double tauA = static_cast<double>(e.atkTauMs) / 1000.0;
        const fcmp::probe::CurveRun run = fcmp::probe::runSineStaircase(rig, levels, fcmp::probe::peakOffsetDb(en, e),
                                                                        1000.0, std::max(0.3, 8.0 * tauA), 0.1);
        Curve c;
        for (std::size_t i = 0; i < levels.size(); ++i)
            c.out.push_back(levels[i] + run.points[i].gainDb);
        c.nonfinite = run.nonfinite;
        c.minGr = run.minGrDb;
        return c;
    }

    // D2: attack and release by the declared laws; the settled GR's peak-to-peak; the reported release at the end.
    struct Steps
    {
        double atkS = -1, relS = -1, ptpDb = 0, minGr = 0, relNowS = 0, holdS = -1;
        std::int64_t nonfinite = 0;
    };
    Steps stepsD2(const ModeEntry& en, const Resolution& res, float fs)
    {
        const EngineParams& e = res.eng;
        const ModeDescriptor& d = *en.desc;
        const double t = analysis::inputThresholdDb(e);
        const double tauA = static_cast<double>(e.atkTauMs) / 1000.0, tauR = static_cast<double>(e.relTauMs) / 1000.0;
        const Segment segs[] = { { t - 20.0, 0.5 }, { t + 20.0, std::max(0.5, 10.0 * tauA) },
                                 { t - 20.0, std::max(1.0, 10.0 * tauR) } };
        fcmp::probe::EngineRig rig(en, e, fs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
        const std::size_t a = run.edges[1], b = run.edges[2];
        const std::span<const float> tap(run.tapGrDb);
        const double top = run.tapGrDb[b - 1];
        Steps s;
        s.atkS = fcmp::probe::measure::lawSeconds(tap.subspan(a, b - a), run.tapGrDb[a - 1], top, fs,
                                                  d.attackSpec(res.view, e).law);
        // a hold (the GR exactly still after the drop) comes first; the release is measured from its end
        std::size_t held = 0;
        while (b + held < run.tapGrDb.size() && run.tapGrDb[b + held] == run.tapGrDb[b - 1])
            ++held;
        s.holdS = static_cast<double>(held) / static_cast<double>(fs);
        s.relS = fcmp::probe::measure::lawSeconds(tap.subspan(b + held), top, 0.0, fs, d.releaseSpec(res.view, e).law);
        const std::size_t win = static_cast<std::size_t>(0.05 * static_cast<double>(fs));
        const auto [lo, hi] = std::minmax_element(run.tapGrDb.begin() + static_cast<std::ptrdiff_t>(b - win),
                                                  run.tapGrDb.begin() + static_cast<std::ptrdiff_t>(b));
        s.ptpDb = static_cast<double>(*hi) - static_cast<double>(*lo);
        s.minGr = *std::min_element(run.tapGrDb.begin(), run.tapGrDb.end());
        s.nonfinite = run.nonfinite;

        // the release the Mode reports, sampled in the middle of the release (constant for a constant crest)
        fcmp::probe::EngineRig mid(en, e, fs);
        const Segment half[] = { segs[0], segs[1], { t - 20.0, 0.5 * tauR } };
        (void) fcmp::probe::runSquareSteps(mid, half, 0.0);
        EngineTelemetry tm;
        mid.engine().telemetry(tm);
        s.relNowS = static_cast<double>(std::max(tm.releaseNowMs[0], tm.releaseNowMs[1])) / 1000.0;
        return s;
    }

    // The slowest release from 20 dB to 10 dB of GR (square levels from staticGr), against the exact exponential of
    // the one-pole r[n] = t + (r0 - t)(1 - c)^n, c = oneMinusAlpha(tau, fs); max deviation over 4 tau (dB).
    double longReleaseDev(const ModeEntry& en, EngineParams e, float fs, double& stallBefore)
    {
        // levels giving 20 and 10 dB of static GR (bisection on staticGr: the curve is monotone)
        const auto levelFor = [&](double gr) {
            double lo = static_cast<double>(e.thrDb) - 20.0, hi = static_cast<double>(e.thrDb) + 120.0;
            for (int i = 0; i < 100; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const auto x = static_cast<float>(mid);
                float r = 0.0f;
                en.staticGr(e, &x, &r, 1);
                (static_cast<double>(r) < gr ? lo : hi) = mid;
            }
            return 0.5 * (lo + hi) - static_cast<double>(e.preGainDb);   // input level
        };
        const double hiLevel = levelFor(20.0), loLevel = levelFor(10.0);
        const auto target = [&](double level) {
            const auto x = static_cast<float>(level) + e.preGainDb;
            float r = 0.0f;
            en.staticGr(e, &x, &r, 1);
            return static_cast<double>(r);
        };
        const double tgt = target(loLevel);
        const double c = static_cast<double>(oneMinusAlpha(e.relTauMs, fs));
        const double tau = static_cast<double>(e.relTauMs) / 1000.0;

        fcmp::probe::EngineRig rig(en, e, fs);
        const std::size_t pre = static_cast<std::size_t>(0.5f * fs);
        const std::size_t post = static_cast<std::size_t>(4.0 * tau * static_cast<double>(fs));
        const std::size_t half = static_cast<std::size_t>(fs / 2000.0f);
        const auto ahi = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(hiLevel));
        const auto alo = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(loLevel));
        constexpr std::size_t kBlock = 8192;
        std::vector<float> in(kBlock), yl(kBlock), yr(kBlock);
        double r0 = 0.0, dev = 0.0, pow1c = 1.0;
        rig.setTapping(true);
        for (std::size_t off = 0; off < pre + post; off += kBlock)
        {
            const std::size_t m = std::min(kBlock, pre + post - off);
            for (std::size_t k = 0; k < m; ++k)
            {
                const std::size_t i = off + k;
                in[k] = ((i / half) % 2 == 0 ? 1.0f : -1.0f) * (i < pre ? ahi : alo);
            }
            rig.process(in.data(), in.data(), yl.data(), yr.data(), m);
            const std::vector<float> g = rig.tap().lane(rig.tap().grDb, 0);
            rig.tap().clear();
            for (std::size_t k = 0; k < m; ++k)
            {
                const std::size_t i = off + k;
                if (i + 1 == pre)
                    r0 = g[k];
                if (i < pre)
                    continue;
                pow1c *= 1.0 - c;                       // the exact curve at sample n + 1 after the drop
                dev = std::max(dev, std::fabs(static_cast<double>(g[k]) - (tgt + (r0 - tgt) * pow1c)));
            }
        }
        // for the NOTE: where the plain recurrence would have stalled, ulp(r) / (2c) at the 10 dB target
        stallBefore = static_cast<double>(std::nextafter(static_cast<float>(tgt), 100.0f) - static_cast<float>(tgt))
                    / (2.0 * c);
        return dev;
    }
} // namespace

FCMP_PROBE(dsp, srsweep)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    const auto& tol = tolns::forRigor(desc.rigor);
    fcmp::probe::Fidelity F(P, desc.provisional);

    const RawParams dflt = fcmp::probe::modeRaw(en);
    ParamView view;
    resolveView(desc, dflt, view);

    // the D1 configuration: defaults at the fastest attack and the slowest release (dsp.static's reasons)
    RawParams curveRaw = dflt;
    curveRaw[Pid::atk] = timeEdge(view, Pid::atk, true);
    curveRaw[Pid::rel] = timeEdge(view, Pid::rel, false);
    const EngineParams curveEng = fcmp::probe::resolveRaw(en, curveRaw).eng;
    const Resolution timeRes = fcmp::probe::resolveRaw(en, dflt);

    // hold at 50 ms, when live
    const ParamSpec* holdSpec = view.spec[idx(Pid::hold)];
    const bool hasHold = continuousLive(holdSpec);
    RawParams holdRaw = dflt;
    if (hasHold)
        holdRaw[Pid::hold] = std::clamp(50.0f, holdSpec->lo, holdSpec->hi);
    const Resolution holdRes = fcmp::probe::resolveRaw(en, holdRaw);

    // AUTO: the tmode step tagged kTagAuto, when there is one
    const ParamSpec* tmSpec = view.spec[idx(Pid::tmode)];
    const Step* autoStep = nullptr;
    if (tmSpec != nullptr && tmSpec->kind == Kind::stepped)
        for (const Step& st : tmSpec->steps)
            if ((st.tag & kTagAuto) != 0)
                autoStep = &st;
    RawParams autoRaw = dflt;
    if (autoStep != nullptr)
        autoRaw[Pid::tmode] = autoStep->plain;
    const Resolution autoRes = fcmp::probe::resolveRaw(en, autoRaw);

    Curve ref;
    Steps refSteps, refAuto;
    for (const Rate& r : kRates)
    {
        const std::string k = rateKey(r.fs);
        const Curve cv = reducedD1(en, curveEng, r.fs);
        const Steps st = stepsD2(en, timeRes, r.fs);
        if (r.fs == 48000.0f)
        {
            ref = cv;
            refSteps = st;
        }
        double curveDev = 0.0;
        for (std::size_t i = 0; i < cv.out.size(); ++i)
            curveDev = std::max(curveDev, std::fabs(cv.out[i] - ref.out[i]));
        const double atkRatio = st.atkS / refSteps.atkS, relRatio = st.relS / refSteps.relS;
        std::printf("NOTE     %s: curve vs 48 kHz %.4g dB; attack %.6g s (x%.5f), release %.6g s (x%.5f); settled "
                    "ptp %.3g dB\n",
                    k.c_str(), curveDev, st.atkS, atkRatio, st.relS, relRatio, st.ptpDb);
        if (r.judged && r.fs != 48000.0f)
        {
            F.le(k + ".curve_vs_48k_db", curveDev, tol.curveVs48kDb);
            // An instantaneous attack (published below one sample at 48 kHz: Brickwall with the budget OFF, M7 S11)
            // measures one sample at every rate, so its ratio is the rates' ratio, not the Mode's: judged by the
            // 1.5-sample tau floor instead (03 §3.7, kTauMinSamples at 48 kHz).
            if (static_cast<double>(desc.attackSpec(timeRes.view, timeRes.eng).seconds) * 48000.0 < 1.0)
                F.le(k + ".atk.instant_diff_s", std::fabs(st.atkS - refSteps.atkS), tolns::kTauMinSamples / 48000.0);
            else
                F.near(k + ".atk.tau_ratio", atkRatio, 1.0, tol.tauFsRatio);
            F.near(k + ".rel.tau_ratio", relRatio, 1.0, tol.tauFsRatio);
        }
        P.eq(k + ".nonfinite", cv.nonfinite + st.nonfinite, 0);
        P.ge(k + ".gr_min_db", std::min(cv.minGr, st.minGr), 0.0);
        P.le(k + ".stable_ptp_db", st.ptpDb, 1e-3);

        if (hasHold)
        {
            const Steps hs = stepsD2(en, holdRes, r.fs);
            const double want = static_cast<double>(holdRes.eng.holdMs) / 1000.0;
            std::printf("NOTE     %s.hold: %.6g s held (published %.6g s)\n", k.c_str(), hs.holdS, want);
            F.near(k + ".hold_s", hs.holdS, want, tolns::tauToleranceSeconds(tol, want, r.fs));
        }
        if (autoStep != nullptr)
        {
            const Steps as = stepsD2(en, autoRes, r.fs);
            if (r.fs == 48000.0f)
                refAuto = as;
            std::printf("NOTE     %s.auto: release %.6g s, reported %.6g s\n", k.c_str(), as.relS, as.relNowS);
            if (r.judged && r.fs != 48000.0f)
                F.near(k + ".auto.tau_ratio", as.relS / refAuto.relS, 1.0, tol.tauFsRatio);
            F.near(k + ".auto.rel_vs_telemetry", as.relS, as.relNowS,
                   tolns::tauToleranceSeconds(tol, as.relNowS, r.fs));
            P.eq(k + ".auto.nonfinite", as.nonfinite, 0);
        }

        if ((r.fs == 48000.0f || r.fs == 384000.0f) && continuousLive(view.spec[idx(Pid::rel)]))
        {
            RawParams slow = dflt;
            slow[Pid::rel] = timeEdge(view, Pid::rel, false);
            const EngineParams slowEng = fcmp::probe::resolveRaw(en, slow).eng;
            if (slowEng.topo == kTopoFB)
                std::printf("NOTE     %s.long_release: a feedback kernel (the closed loop is not the one-pole's "
                            "exponential; its FB sub-ulp carry is dsp.fbsolve's carry rows): not judged\n",
                            k.c_str());
            else
            {
                double stall = 0.0;
                const double dev = longReleaseDev(en, slowEng, r.fs, stall);
                std::printf("NOTE     %s.long_release: %.4g dB from the exact exponential (a plain float one-pole "
                            "stalls %.3g dB short of a 10 dB target)\n",
                            k.c_str(), dev, stall);
                P.le(k + ".long_release.max_dev_db", dev, 1e-3);
            }
        }
    }

    F.summary();
    return P.finish();
}
