// FCMP_PROBE layer=dsp name=time scope=mode timeout=60
//
// dsp.time.<key> (F3, S2; D2: 03 §3.4 Rig rows, C §5.3; E §2.3; K2 #4 iii; Rig driver, K3 #10): attack and release
// time constants against the Mode's declared TimeSpec, through the Mode's own engine (EngineRig), plus the GR OFF ramp.
//
// Stimulus: a 1 kHz square wave (|x| = A at every sample: peak == RMS, 01 §7's StepStimulus convention), L = R, at
// T - 20 dB for 0.5 s, then T + 20 dB for max(0.5 s, 10 tauA), then T - 20 dB for max(1 s, 10 tauR); T is the input
// threshold (analysis::inputThresholdDb). A square, not C §5.3's sine: with the GR-domain peak ballistics a sine's
// target GR swings with the waveform every cycle, so its attack runs only part of each cycle and the measured time is
// the waveform's, not the Mode's; a constant |x| steps the target exactly once, which is the time constant the Mode
// declares (the sine's static behaviour is dsp.static's).
// Configurations: attack at every detent (stepped) or min / mid / max of the Mode's range (continuous: the host map's
// midpoint), release at its default; release likewise, attack at its default. Extraction by the declared TimeLaw from
// the tap's GR (Measure lawSeconds; the step is between the last pre-step sample, time 0, and the first post-step one).
//
// Rows (<cfg> = atk.min, atk.mid, atk.max, rel.min, ... or atk.d<i> / rel.d<i> for detents):
//   fidelity (NOTE while provisional, SPRINTS §7 D12):
//     time.<cfg>.s                 the measured time against TimeSpec.seconds, +- max(tauRel * tau, 1.5 / fs)
//                                  (03 §3.7); a program-dependent spec is judged against [lo, hi] instead
//   structural (spec, blocking):
//     time.<cfg>.tap_vs_audio_db   the tap's GR against the GR derived from y / x at every sample (qualifies the tap)
//     time.<cfg>.reversals         the GR moves one way through the step (0 samples against the step's direction)
//     time.<cfg>.nonfinite, time.<cfg>.gr_min_db
//     time.groff.<edge>.hf_ratio_db  GR OFF toggled just after t = 1 s, at a waveform peak (on -> off, off -> on),
//                                  on a 110 Hz tone at -6 dBFS with about 8 dB of GR (a live threshold is moved 8 dB
//                                  under the tone's detector level, as dsp.null's bypass and dsp.zipper's edges do;
//                                  else the Mode's defaults): energy above 8 kHz in +-2 ms
//                                  against both steady controls, <= +3 dB (offAmt's shaped 20 ms ramp, K2 #4 iii; a
//                                  linear ramp reads about +40 dB here); time.groff.off.lands: GR exactly 0 from 21 ms
//                                  after the edge; time.groff.metric_sensitivity_db: the same controls spliced with a
//                                  hard cut must read >= +20 dB, which proves the metric can see a step (C §5.0:
//                                  +42.8 dB)
//     time.carry.*                 the hand-over (01 §5.5): carry() is valid and finite(); a fresh engine seeded
//                                  from it continues bit-identically with the old one (seamless: within 2 ulp of
//                                  the carried GR, the rounding remainder a slow one-pole keeps and Carry cannot
//                                  hold, SmoothBranching.h); a carry from the
//                                  other lane domain seeds max(lane0, lane1) (K2 #3d); a cold carry (valid 0) is a
//                                  no-op
//     time.telemetry.finite, time.internals.*  telemetry() finite and >= 0; internals() finite, 0 beyond the
//                                  descriptor's declared words (K3 #11)
//   golden (rel:0.1; candidates while provisional): time.rel.after_<1s|10s>.<t50|t90>_ms, the release after 1 s
//     and 10 s of GR at the Mode's defaults (C §5.3: program-dependent release shows up here)
//   Quality (F7, S6; 01 §5.6; K2 #11b; 03 §3.7 "tau across ECO/STD/HQ <= 1.5 base samples"), through the Host
//     (fcdsp::EngineHost at ECO, STD and HQ, no lookahead): the attack and the release are measured from the AUDIO, so
//     the row sees where the gain lands on the (upsampled) signal, which is what D_up in the side-chain delay aligns.
//     The detector runs on an external key (a 2-channel bus carrying D2's 1 kHz square steps at T - 20 / T + 20 /
//     T - 20 dB, T = the detector-domain threshold: a key is never pre-gained), so both SC lanes are equal and every
//     lane takes the same GR (whatever the link law; an FB kernel evaluates FF on a key, E §2.6). The main input is a
//     quadrature pair, L = a sin, R = a cos at 500 Hz and -20 dBFS, so the envelope sqrt(yL^2 + yR^2) never crosses 0:
//     against a mix-0 render of the same Quality (the OS round trip of the pair) it is the applied gain sample by
//     sample, GR = -20 log10(env / env0), read at output index n + L (L = latencySamples()). stmode is set to STEREO
//     and mix to 1 for the renders (a probe-level choice: the row judges the host's alignment, not the Mode's routing).
//     Configurations: the Mode's defaults ("def") and its fastest attack ("fast": the lowest detent or range end).
//     time.quality.<cfg>.<atk|rel>.<q>_s  the measured time by the declared law (NOTE)
//     time.quality.<cfg>.<atk|rel>.spread_samples  max over Qualities - min, in base samples: <= 1.5 (spec)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    using fcmp::probe::Segment;

    constexpr float kFs = 48000.0f;

    struct TimeChoice
    {
        float plain;
        std::string label;
    };

    // Every detent (stepped) or min / mid / max (continuous, hybrid); the resolved value otherwise.
    std::vector<TimeChoice> timeChoices(const ParamView& v, Pid pid, const char* prefix)
    {
        const ParamSpec* s = v.spec[idx(pid)];
        std::vector<TimeChoice> out;
        if (s != nullptr && s->kind == Kind::stepped)
            for (std::size_t i = 0; i < s->steps.size(); ++i)
                out.push_back({ s->steps[i].plain, std::string(prefix) + ".d" + std::to_string(i) });
        else if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
        {
            const float mid = toPlain(pid, 0.5f * (toNorm(pid, s->lo) + toNorm(pid, s->hi)));
            out.push_back({ s->lo, std::string(prefix) + ".min" });
            out.push_back({ mid, std::string(prefix) + ".mid" });
            out.push_back({ s->hi, std::string(prefix) + ".max" });
        }
        else
            out.push_back({ v[pid].plain, std::string(prefix) + ".fixed" });
        return out;
    }

    std::int64_t reversals(std::span<const float> trace, bool rising)
    {
        std::int64_t n = 0;
        for (std::size_t k = 1; k < trace.size(); ++k)
            n += rising ? (trace[k] < trace[k - 1] ? 1 : 0) : (trace[k] > trace[k - 1] ? 1 : 0);
        return n;
    }

    struct Rendered
    {
        std::vector<float> out, gr;
    };

    // The edge of the GR OFF renders: 1 s plus a quarter period of 110 Hz, a waveform peak (the worst place for a
    // gain-slope discontinuity: y = x g then jumps in slope by x * dg).
    constexpr std::size_t kToggleEdge = static_cast<std::size_t>(kFs) + static_cast<std::size_t>(kFs / 440.0f + 0.5f);

    // A 110 Hz tone at -6 dBFS through the rig for 2 s; at kToggleEdge the params switch from `first` to `second`.
    Rendered renderToggle(const ModeEntry& en, const EngineParams& first, const EngineParams& second)
    {
        const std::size_t n = static_cast<std::size_t>(2.0f * kFs), edge = kToggleEdge;
        std::vector<float> in(n), outR(n);
        Rendered r;
        r.out.resize(n);
        for (std::size_t k = 0; k < n; ++k)
            in[k] = fcmp::probe::sig::sineAt(static_cast<std::int64_t>(k), 110.0, kFs, 0.5011872336272722);
        fcmp::probe::EngineRig rig(en, first, kFs);
        rig.setTapping(true);
        rig.process(in.data(), in.data(), r.out.data(), outR.data(), edge);
        rig.setParams(second);
        rig.process(in.data() + edge, in.data() + edge, r.out.data() + edge, outR.data() + edge, n - edge);
        r.gr = rig.tap().lane(rig.tap().grDb, 0);
        return r;
    }

    // ---- the Quality row: the audio-derived GR of a keyed step through EngineHost (file comment) --------------------
    constexpr Quality kQualities[3] = { Quality::eco, Quality::std, Quality::hq };

    struct KeyedRun
    {
        std::vector<float> l, r;
        int latency = 0;
    };

    KeyedRun renderKeyed(Quality q, const BlockParams& bp, const std::vector<float>& ml, const std::vector<float>& mr,
                         const std::vector<float>& key)
    {
        HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = 512;
        cfg.quality = q;
        cfg.budget = LookaheadBudget::off;
        cfg.keyChans = 2;
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        const std::size_t n = ml.size();
        KeyedRun run;
        run.l.assign(n, 0.0f);
        run.r.assign(n, 0.0f);
        run.latency = host->latencySamples();
        for (std::size_t off = 0; off < n; off += 512)
        {
            const std::size_t len = std::min<std::size_t>(512, n - off);
            const float* ins[2] = { ml.data() + off, mr.data() + off };
            const float* keys[2] = { key.data() + off, key.data() + off };
            float* outs[2] = { run.l.data() + off, run.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.key = keys;
            io.numKey = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, bp);
        }
        return run;
    }

    // Per Quality: {attack, release} in seconds by the declared laws, from the audio of a keyed D2 step.
    struct QualityTimes
    {
        double atk[3]{}, rel[3]{};
    };

    QualityTimes qualityTimes(const ModeEntry& en, const Resolution& res)
    {
        const ModeDescriptor& desc = *en.desc;
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = res.eng;
        bp.eng.stmode = 0;
        bp.eng.mix = 1.0f;
        bp.extKey = true;
        BlockParams dry = bp;
        dry.eng.mix = 0.0f;
        const TimeLaw atkLaw = desc.attackSpec(res.view, res.eng).law, relLaw = desc.releaseSpec(res.view, res.eng).law;

        const double t = static_cast<double>(res.eng.thrDb);           // the key is never pre-gained
        const double tauA = static_cast<double>(res.eng.atkTauMs) / 1000.0;
        const double tauR = static_cast<double>(res.eng.relTauMs) / 1000.0;
        const std::size_t e1 = static_cast<std::size_t>(0.5f * kFs);
        const std::size_t e2 = e1 + static_cast<std::size_t>(std::max(0.5, 10.0 * tauA) * kFs);
        const std::size_t n = e2 + static_cast<std::size_t>(std::max(1.0, 10.0 * tauR) * kFs);
        std::vector<float> key(n), ml(n), mr(n);
        const auto lo = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(t - 20.0));
        const auto hi = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(t + 20.0));
        for (std::size_t k = 0; k < n; ++k)
        {
            const float a = k >= e1 && k < e2 ? hi : lo;
            key[k] = (k / 24) % 2 == 0 ? a : -a;                      // 1 kHz square at 48 kHz, edges on the steps
            ml[k] = fcmp::probe::sig::sineAt(static_cast<std::int64_t>(k), 500.0, kFs, 0.1);
            mr[k] = fcmp::probe::sig::sineAt(static_cast<std::int64_t>(k), 500.0, kFs, 0.1, 0.25);
        }
        QualityTimes out;
        for (int qi = 0; qi < 3; ++qi)
        {
            const KeyedRun y = renderKeyed(kQualities[qi], bp, ml, mr, key);
            const KeyedRun y0 = renderKeyed(kQualities[qi], dry, ml, mr, key);
            const auto L = static_cast<std::size_t>(y.latency);
            std::vector<float> gr(n - L);
            for (std::size_t k = 0; k + L < n; ++k)
            {
                const double env = std::hypot(static_cast<double>(y.l[k + L]), static_cast<double>(y.r[k + L]));
                const double env0 = std::hypot(static_cast<double>(y0.l[k + L]), static_cast<double>(y0.r[k + L]));
                gr[k] = static_cast<float>(-fcmp::probe::measure::dbFromAmplitude(env / std::max(env0, 1e-30)));
            }
            const std::span<const float> tr(gr);
            namespace measure = fcmp::probe::measure;
            out.atk[qi] = measure::lawSeconds(tr.subspan(e1, e2 - e1), gr[e1 - 1], gr[e2 - 1], kFs, atkLaw);
            out.rel[qi] = measure::lawSeconds(tr.subspan(e2), gr[e2 - 1], gr[gr.size() - 1], kFs, relLaw);
        }
        return out;
    }
} // namespace

FCMP_PROBE(dsp, time)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    const auto& tol = fcmp::probe::tol::forRigor(desc.rigor);
    fcmp::probe::Fidelity F(P, desc.provisional);

    const RawParams base = fcmp::probe::modeRaw(en);
    ParamView view;
    resolveView(desc, base, view);

    std::vector<std::pair<Pid, TimeChoice>> configs;
    for (const TimeChoice& c : timeChoices(view, Pid::atk, "atk"))
        configs.emplace_back(Pid::atk, c);
    for (const TimeChoice& c : timeChoices(view, Pid::rel, "rel"))
        configs.emplace_back(Pid::rel, c);

    for (const auto& [pid, choice] : configs)
    {
        RawParams raw = base;
        raw[pid] = choice.plain;
        const Resolution res = fcmp::probe::resolveRaw(en, raw);
        const EngineParams& e = res.eng;
        const bool attack = pid == Pid::atk;
        const TimeSpec spec = attack ? desc.attackSpec(res.view, e) : desc.releaseSpec(res.view, e);
        const double t = analysis::inputThresholdDb(e);
        const double tauA = static_cast<double>(e.atkTauMs) / 1000.0, tauR = static_cast<double>(e.relTauMs) / 1000.0;
        const Segment segs[] = { { t - 20.0, 0.5 }, { t + 20.0, std::max(0.5, 10.0 * tauA) },
                                 { t - 20.0, std::max(1.0, 10.0 * tauR) } };
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);

        const std::string k = "time." + choice.label;
        const std::size_t a = run.edges[1], b = run.edges[2];
        const std::span<const float> tap(run.tapGrDb);
        const std::span<const float> trace = attack ? tap.subspan(a, b - a) : tap.subspan(b);
        const double from = run.tapGrDb[(attack ? a : b) - 1];
        const double to = attack ? static_cast<double>(run.tapGrDb[b - 1]) : 0.0;
        const double got = fcmp::probe::measure::lawSeconds(trace, from, to, kFs, spec.law);
        std::printf("NOTE     %s: %s %.9g ms (published, law %d) -> %.6g s measured; GR %.4g -> %.4g dB\n", k.c_str(),
                    attack ? "attack" : "release", static_cast<double>(choice.plain), static_cast<int>(spec.law), got,
                    from, to);
        if (spec.program && spec.hi > spec.lo)
            F.in(k + ".s", got, static_cast<double>(spec.lo), static_cast<double>(spec.hi));
        else if (tol.tauBySpec)
            F.near(k + ".s", got, static_cast<double>(spec.seconds),
                   fcmp::probe::tol::tauToleranceSeconds(tol, static_cast<double>(spec.seconds), kFs));

        double tapVsAudio = 0.0, grMin = 0.0;
        for (std::size_t i = 0; i < run.tapGrDb.size(); ++i)
        {
            tapVsAudio = std::max(tapVsAudio, std::fabs(static_cast<double>(run.tapGrDb[i] - run.audioGrDb[i])));
            grMin = std::min(grMin, static_cast<double>(run.tapGrDb[i]));
        }
        P.le(k + ".tap_vs_audio_db", tapVsAudio, tol.tapGrDb);
        P.eq(k + ".reversals", reversals(trace, attack), 0);
        P.eq(k + ".nonfinite", run.nonfinite, 0);
        P.ge(k + ".gr_min_db", grMin, 0.0);
    }

    // ---- GR OFF never steps (K2 #4 iii): offAmt's 20 ms ramp --------------------------------------------------------
    {
        // The toggle needs GR: with a live threshold, the input threshold goes 8 dB under the tone's detector level
        // (T_in is affine in thr with slope 1, K1 #9). DW (S4): Opto 2A's PEAK RED. 40 and Brickwall's -6 dBFS
        // default leave a -6 dBFS tone uncompressed, which toggles nothing.
        RawParams toggle = base;
        if (const ParamSpec* ts = view.spec[idx(Pid::thr)];
            ts != nullptr && (ts->kind == Kind::continuous || ts->kind == Kind::hybrid))
        {
            const EngineParams e0 = fcmp::probe::resolveRaw(en, base).eng;
            const double want = -6.0 - 8.0 - fcmp::probe::peakOffsetDb(en, e0);
            toggle[Pid::thr] = std::clamp(static_cast<float>(static_cast<double>(base[Pid::thr]) + want
                                                             - static_cast<double>(analysis::inputThresholdDb(e0))),
                                          ts->lo, ts->hi);
        }
        EngineParams on = fcmp::probe::resolveRaw(en, toggle).eng;
        EngineParams off = on;
        off.flags = static_cast<uint8_t>(off.flags | kEngGrOff);
        const Rendered ctlOn = renderToggle(en, on, on), ctlOff = renderToggle(en, off, off);
        const Rendered onOff = renderToggle(en, on, off), offOn = renderToggle(en, off, on);
        const std::size_t edge = kToggleEdge;
        std::printf("NOTE     time.groff: GR before the edge %.4g dB (110 Hz, -6 dBFS, edge at a waveform peak)\n",
                    static_cast<double>(ctlOn.gr[edge - 1]));
        P.le("time.groff.off.hf_ratio_db", fcmp::probe::measure::hfRatioDb(onOff.out, ctlOn.out, ctlOff.out, edge, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        P.le("time.groff.on.hf_ratio_db", fcmp::probe::measure::hfRatioDb(offOn.out, ctlOn.out, ctlOff.out, edge, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        double after = 0.0;
        for (std::size_t i = edge + static_cast<std::size_t>(0.021f * kFs); i < onOff.gr.size(); ++i)
            after = std::max(after, static_cast<double>(std::fabs(onOff.gr[i])));
        P.eq("time.groff.off.lands", after == 0.0 ? 1 : 0, 1);
        std::vector<float> splice(ctlOn.out);
        std::copy(ctlOff.out.begin() + static_cast<std::ptrdiff_t>(edge), ctlOff.out.end(),
                  splice.begin() + static_cast<std::ptrdiff_t>(edge));
        P.ge("time.groff.metric_sensitivity_db",
             fcmp::probe::measure::hfRatioDb(splice, ctlOn.out, ctlOff.out, edge, kFs), 20.0);
    }

    // ---- the engine's hand-over and telemetry (01 §5.3, §5.5; K2 #3d; K3 #11) ---------------------------------------
    {
        const EngineParams e = fcmp::probe::resolveRaw(en, base).eng;
        const double t = analysis::inputThresholdDb(e);
        fcmp::probe::EngineRig a(en, e, kFs);
        const Segment segs[] = { { t + 10.0, 0.3 }, { t - 10.0, 0.05 } };          // GR part-way through a release
        (void) fcmp::probe::runSquareSteps(a, segs, 0.0);
        const Carry c = a.engine().carry();
        P.eq("time.carry.valid", c.valid, 1);
        P.eq("time.carry.finite", a.engine().finite() ? 1 : 0, 1);

        // A fresh engine seeded from the carry continues exactly where the old one is (same parameters, same input).
        fcmp::probe::EngineRig b(en, e, kFs);
        b.engine().seed(c);
        const std::size_t n = static_cast<std::size_t>(0.2f * kFs);
        std::vector<float> in(n), oa(n), ob(n), scratch(n);
        const auto amp = static_cast<float>(fcmp::probe::measure::amplitudeFromDb(t + 5.0));
        for (std::size_t k = 0; k < n; ++k)
            in[k] = (k / 24) % 2 == 0 ? amp : -amp;                                 // 1 kHz square at 48 kHz
        a.tap().clear();
        a.setTapping(true);
        b.setTapping(true);
        a.process(in.data(), in.data(), oa.data(), scratch.data(), n);
        b.process(in.data(), in.data(), ob.data(), scratch.data(), n);
        double seam = 0.0;
        for (const int ln : { 0, 1 })
        {
            const std::vector<float> ga = a.tap().lane(a.tap().grDb, ln), gb = b.tap().lane(b.tap().grDb, ln);
            for (std::size_t k = 0; k < n; ++k)
                seam = std::max(seam, static_cast<double>(std::fabs(ga[k] - gb[k])));
        }
        std::printf("NOTE     time.carry: GR %.6g dB, detector %.6g dB handed over; seeded engine differs by %.3g dB\n",
                    static_cast<double>(simd::lane<0>(c.grDb)), static_cast<double>(simd::lane<0>(c.detDb)), seam);
        // A release slower than 2^14 samples keeps its sub-ulp rounding remainder in the ballistics state, which the
        // frozen Carry (01 §5.5) cannot hold: the hand-over drops at most half an ulp (SmoothBranching.h), and the
        // seeded engine may round an ulp away. DW (S4): Bus 25's 0.5 s default release. Faster ones are bit-exact.
        const float carried = std::max(std::fabs(simd::lane<0>(c.grDb)), std::fabs(simd::lane<1>(c.grDb)));
        const double ulp2 = 2.0 * static_cast<double>(std::nextafter(carried, HUGE_VALF) - carried);
        P.eq("time.carry.seamless", seam <= ulp2 ? 1 : 0, 1);

        // The lane-domain rule (K2 #3d): a carry from the other domain seeds max(lane0, lane1) into every lane; a
        // cold carry (valid 0) changes nothing.
        alignas(16) const float lanes[4] = { 3.0f, 7.0f, 1.0f, 2.0f };
        Carry other = c;
        other.grDb = simd::load(lanes);
        other.msDomain = static_cast<uint8_t>(e.stmode == 0 ? LaneDomain::ms : LaneDomain::lr);
        fcmp::probe::EngineRig d(en, e, kFs);
        d.engine().seed(other);
        const Carry got = d.engine().carry();
        const bool hasGr = simd::lane<0>(got.grDb) != 0.0f || simd::lane<1>(got.grDb) != 0.0f;
        if (hasGr)                                              // the ballistics hand over GR (their grDb getter)
            P.eq("time.carry.domain_rule", simd::lane<0>(got.grDb) == 7.0f && simd::lane<1>(got.grDb) == 7.0f ? 1 : 0,
                 1);
        else
            std::printf("NOTE     time.carry: the ballistics expose no grDb(state); the domain rule is not "
                        "observable\n");
        d.engine().seed(Carry{});
        const Carry again = d.engine().carry();
        const bool unchanged = simd::lane<0>(again.grDb) == simd::lane<0>(got.grDb)
                            && simd::lane<1>(again.grDb) == simd::lane<1>(got.grDb);
        P.eq("time.carry.cold_is_noop", unchanged ? 1 : 0, 1);

        EngineTelemetry tm;
        a.engine().telemetry(tm);
        float words[kInternals];
        a.engine().internals(words);
        bool telemetryOk = true;
        for (int ch = 0; ch < 2; ++ch)
            telemetryOk = telemetryOk && std::isfinite(tm.attackNowMs[ch]) && tm.attackNowMs[ch] >= 0.0f
                       && std::isfinite(tm.releaseNowMs[ch]) && tm.releaseNowMs[ch] >= 0.0f
                       && std::isfinite(tm.crestDb[ch]);
        std::int64_t badWords = 0, reservedWords = 0;
        for (int w = 0; w < kInternals; ++w)
        {
            badWords += std::isfinite(words[w]) ? 0 : 1;
            reservedWords += w >= static_cast<int>(desc.internals.size()) && words[w] != 0.0f ? 1 : 0;
        }
        std::printf("NOTE     time.telemetry: attack %.6g ms, release %.6g ms; internals",
                    static_cast<double>(tm.attackNowMs[0]), static_cast<double>(tm.releaseNowMs[0]));
        for (std::size_t w = 0; w < desc.internals.size(); ++w)
            std::printf(" %s=%.6g", desc.internals[w].name, static_cast<double>(words[w]));
        std::printf("\n");
        P.eq("time.telemetry.finite", telemetryOk ? 1 : 0, 1);
        P.eq("time.internals.nonfinite", badWords, 0);
        P.eq("time.internals.undeclared_nonzero", reservedWords, 0);
    }

    // ---- golden: release after 1 s and 10 s of GR, at the Mode's defaults (C §5.3) ----------------------------------
    {
        const Resolution res = fcmp::probe::resolveRaw(en, base);
        const EngineParams& e = res.eng;
        const double t = analysis::inputThresholdDb(e), tauR = static_cast<double>(e.relTauMs) / 1000.0;
        for (const double held : { 1.0, 10.0 })
        {
            const Segment segs[] = { { t - 20.0, 0.5 }, { t + 20.0, held }, { t - 20.0, std::max(1.0, 10.0 * tauR) } };
            fcmp::probe::EngineRig rig(en, e, kFs);
            const fcmp::probe::StepRun run = fcmp::probe::runSquareSteps(rig, segs, 0.0);
            const std::size_t b = run.edges[2];
            const std::span<const float> trace = std::span<const float>(run.tapGrDb).subspan(b);
            const double from = run.tapGrDb[b - 1];
            const std::string k = held < 5.0 ? "time.rel.after_1s" : "time.rel.after_10s";
            P.num(k + ".t50_ms", 1000.0 * fcmp::probe::measure::crossingSeconds(trace, from, 0.0, 0.5, kFs),
                  Tol::rel(tol.tauGoldenRel));
            P.num(k + ".t90_ms", 1000.0 * fcmp::probe::measure::crossingSeconds(trace, from, 0.0, 0.9, kFs),
                  Tol::rel(tol.tauGoldenRel));
        }
    }

    // ---- the Quality row: tau at ECO, STD and HQ agree within 1.5 base samples (K2 #11b) ---------------------------
    {
        std::vector<std::pair<std::string, RawParams>> cfgs{ { "def", base } };
        if (const ParamSpec* as = view.spec[idx(Pid::atk)];
            as != nullptr && (as->kind == Kind::stepped || as->kind == Kind::continuous || as->kind == Kind::hybrid))
        {
            RawParams fast = base;
            fast[Pid::atk] = as->kind == Kind::stepped ? as->steps.front().plain : as->lo;
            if (fast[Pid::atk] != base[Pid::atk])
                cfgs.emplace_back("fast", fast);
        }
        for (const auto& [name, raw] : cfgs)
        {
            const Resolution res = fcmp::probe::resolveRaw(en, raw);
            const QualityTimes qt = qualityTimes(en, res);
            for (const bool attack : { true, false })
            {
                const double* v = attack ? qt.atk : qt.rel;
                const std::string k = "time.quality." + name + (attack ? ".atk" : ".rel");
                double lo = v[0], hi = v[0];
                for (int qi = 0; qi < 3; ++qi)
                {
                    lo = std::min(lo, v[qi]);
                    hi = std::max(hi, v[qi]);
                }
                std::printf("NOTE     %s: eco %.9g s, std %.9g s, hq %.9g s (tau %.6g ms)\n", k.c_str(), v[0], v[1],
                            v[2], static_cast<double>(attack ? res.eng.atkTauMs : res.eng.relTauMs));
                const bool measured = lo > 0.0;
                P.eq(k + ".measured", measured ? 1 : 0, 1);
                P.le(k + ".spread_samples", measured ? (hi - lo) * kFs : 1e9,
                     fcmp::probe::tol::kTauAcrossQualitySamples);
            }
        }
    }

    F.summary();
    return P.finish();
}
