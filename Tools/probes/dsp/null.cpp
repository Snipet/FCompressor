// FCMP_PROBE layer=dsp name=null scope=mode timeout=60
//
// dsp.null.<key> (F4, S3; D8 (a), (b), (c), (e) at engine level: 03 §3.4 Host rows, C §5.8; K1 #2, K2 #3c, #12): the
// nulls of fcdsp::EngineHost, ECO rows. The reference is the dry input delayed by the host's latency, delay(x, L) with
// L = latencySamples() (0 at S3: no lookahead delay yet); F7 adds the STD and HQ rows (a control Oversampler round
// trip of the delayed input) when the host gains its oversampler.
//
// Program: 1.5 s at 48 kHz, stereo, L != R: a 1 kHz sine at the input threshold + 12 dB (left) and seeded noise bursts
// (right), so the wet path compresses (the rows below check that it does) and the lanes differ. Blocks of 512 samples.
//
// Rows (spec, blocking; <q> = eco):
//   null.<q>.mix0.mismatches        mix 0: every output sample == delay(x, L) (max_err = 0), stereo
//   null.<q>.mix0.gr_active_db      ... while the wet path runs >= 3 dB of GR (so the null is not vacuous)
//   null.<q>.mix0.pregain.mismatches  mix 0 with EngineParams::preGainDb = +12 (the threshold moved with it): the dry
//                                   path is never pre-gained (K2 #3c)
//   null.<q>.mix0.mono.mismatches   mono in, mono out
//   null.<q>.mix0.key.mismatches    an active external key (extKey on, a 2-channel key bus)
//   null.<q>.below.*                (b) input 20 dB below threshold - knee/2, voice OFF, makeup 0, auto makeup off:
//                                   rigor clean: .gr_nonzero (tap GR lanes 0-1 exactly 0.0) and .mismatches against
//                                   delay(x, L); modelled: .residual_db <= -120 dB; character: .thd_db golden abs:0.5
//                                   (1 kHz at -10 dBFS, H2...H8), no spec row
//   null.<q>.ratio1.*               (c) ratio 1:1 (slope 0) where the Mode allows it, input at threshold + 12 dB: the
//                                   same rules as (b) (character: skipped)
//   null.<q>.mix05.err_db           (e) mix 0.5 against the mean of mix 0 and mix 1: <= -120 dB
//   null.<q>.bypass.mismatches      bypass engaged (from configure): bit-exact against delay(x, L), every sample
//   null.<q>.bypass.host.mismatches ProcessIo::hostBypassed (processBlockBypassed) likewise, once its 20 ms ramp has
//                                   landed (configure() cannot know the host's bypass, so it ramps in)
//   null.<q>.bypass.<on|off>.hf_ratio_db  the bypass edge at a waveform peak (the worst place) of a 110 Hz tone at
//                                   -6 dBFS with about 6 dB of GR (C §5.6's level), against both steady controls:
//                                   <= +3 dB (the smoothstep-shaped 20 ms ramp, host/Ramps.h)
//   null.<q>.bypass.<on|off>.lands  from 20 ms after the edge the output equals the bypassed control (on) or the
//                                   processed control (off) bit for bit: the processing ran on under bypass
//   null.<q>.bypass.metric_sensitivity_db  the controls spliced with a hard cut read >= +20 dB (the metric sees a step)
// Latency and tail (01 §5.4, §5.6), the reference L above:
//   null.latency.design_mismatches  EngineHost::latencyFor over Quality x budget at 44.1/48/96 kHz against
//                                   lookaheadSamples(budget, fs) + kOs[quality].latency: 0
//   null.latency.eco.<fs>           latencySamples() of an ECO host without lookahead == latencyFor(its config) == 0
//                                   (S3 runs the ECO path without delay lines for every config: F7; printed)
//   null.tail.err_s                 tailSeconds(bp) == desc.tailSeconds(eng) + latency / fs
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;

    constexpr float kFs = 48000.0f;
    constexpr int kBlock = 512;

    // ---- host driver (the same small driver as the other F4 probes; one per file, no shared probe file) -------------
    HostConfig ecoConfig(int keyChans = 0)
    {
        HostConfig c;
        c.fs = kFs;
        c.maxBlock = kBlock;
        c.quality = Quality::eco;
        c.budget = LookaheadBudget::off;
        c.keyChans = keyChans;
        return c;
    }

    BlockParams blockOf(const ModeEntry& en, const RawParams& raw)
    {
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
        return bp;
    }

    struct Event
    {
        std::size_t at = 0;                 // first sample processed with these parameters
        BlockParams bp;
        bool hostBypassed = false;
    };

    struct Run
    {
        std::vector<float> l, r;
        std::vector<simd::f32x4> gr;        // tap, lanes {c0, c1, aux0, aux1}
        int latency = 0;
    };

    struct Io
    {
        const std::vector<float>* l = nullptr;
        const std::vector<float>* r = nullptr;          // nullptr: mono in (and mono out)
        const std::vector<float>* keyL = nullptr;       // non-null: a 2-channel key bus
        const std::vector<float>* keyR = nullptr;
    };

    // A fresh host configured with cfg and events[0].bp, run over the input in blocks of `bs` (split at the events).
    Run render(const HostConfig& cfg, const std::vector<Event>& events, const Io& in, int bs = kBlock)
    {
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, events.front().bp);
        const std::size_t n = in.l->size();
        const bool mono = in.r == nullptr;
        Run run;
        run.l.assign(n, 0.0f);
        run.r.assign(n, 0.0f);
        run.gr.assign(n, simd::set1(0.0f));
        run.latency = host->latencySamples();
        TestTap tap;
        tap.grDb = run.gr;
        tap.firstSample = 0;
        host->setTap(&tap);
        std::size_t ev = 0;
        for (std::size_t off = 0; off < n;)
        {
            while (ev + 1 < events.size() && events[ev + 1].at <= off)
                ++ev;
            std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(bs), n - off);
            if (ev + 1 < events.size())
                len = std::min(len, events[ev + 1].at - off);
            const float* ins[2] = { in.l->data() + off, mono ? nullptr : in.r->data() + off };
            float* outs[2] = { run.l.data() + off, run.r.data() + off };
            const float* keys[2] = { in.keyL != nullptr ? in.keyL->data() + off : nullptr,
                                     in.keyR != nullptr ? in.keyR->data() + off : nullptr };
            ProcessIo io;
            io.in = ins;
            io.numIn = mono ? 1 : 2;
            io.out = outs;
            io.numOut = mono ? 1 : 2;
            io.key = in.keyL != nullptr ? keys : nullptr;
            io.numKey = in.keyL != nullptr ? 2 : 0;
            io.n = static_cast<int>(len);
            io.hostBypassed = events[ev].hostBypassed;
            host->process(io, events[ev].bp);
            off += len;
        }
        host->setTap(nullptr);
        return run;
    }

    Run render(const HostConfig& cfg, const BlockParams& bp, const Io& in, int bs = kBlock)
    {
        return render(cfg, std::vector<Event>{ Event{ 0, bp, false } }, in, bs);
    }

    std::vector<float> delayed(const std::vector<float>& x, int latency)
    {
        std::vector<float> y(x.size(), 0.0f);
        const auto l = static_cast<std::size_t>(latency < 0 ? 0 : latency);
        for (std::size_t i = l; i < x.size(); ++i)
            y[i] = x[i - l];
        return y;
    }

    // Samples where got != want (float compare: max_err = 0, C §5.8 D8), from `from` on.
    std::int64_t mismatches(const std::vector<float>& got, const std::vector<float>& want, std::size_t from = 0)
    {
        std::int64_t m = 0;
        for (std::size_t i = from; i < got.size() && i < want.size(); ++i)
            m += got[i] == want[i] ? 0 : 1;
        return m;
    }

    float lane(simd::f32x4 v, int ln)
    {
        alignas(16) float t[4];
        simd::store(t, v);
        return t[ln & 3];
    }

    double maxGr(const Run& r)
    {
        double m = 0.0;
        for (const simd::f32x4 g : r.gr)
            m = std::max({ m, static_cast<double>(lane(g, 0)), static_cast<double>(lane(g, 1)) });
        return m;
    }

    std::int64_t nonzeroGr(const Run& r)
    {
        std::int64_t k = 0;
        for (const simd::f32x4 g : r.gr)
            k += lane(g, 0) != 0.0f || lane(g, 1) != 0.0f ? 1 : 0;
        return k;
    }

    // 10 log10(energy(a - b) / energy(b)) over both channels; -400 for an exact match.
    double residualDb(const Run& a, const std::vector<float>& bl, const std::vector<float>& br)
    {
        double e = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < a.l.size(); ++i)
        {
            const double dl = static_cast<double>(a.l[i]) - bl[i], dr = static_cast<double>(a.r[i]) - br[i];
            e += dl * dl + dr * dr;
            ref += static_cast<double>(bl[i]) * bl[i] + static_cast<double>(br[i]) * br[i];
        }
        return e <= 0.0 ? -400.0 : 10.0 * std::log10(e / std::max(ref, 1e-300));
    }

    // The step of a stepped spec labelled `label`, or -1.
    int stepLabelled(const ParamSpec* s, std::string_view label)
    {
        if (s == nullptr || s->kind != Kind::stepped)
            return -1;
        for (std::size_t i = 0; i < s->steps.size(); ++i)
            if (label == s->steps[i].label)
                return static_cast<int>(i);
        return -1;
    }

    bool live(const ParamView& v, Pid p)
    {
        return v[p].state == SlotState::live || v[p].state == SlotState::stepped;
    }

    // THD of a 1 kHz tone (H2..H8 against H1) over the last 0.1 s of a channel, dB.
    double thdDb(const std::vector<float>& y, float fs)
    {
        const std::size_t win = static_cast<std::size_t>(0.1f * fs);
        const std::size_t start = y.size() - win;
        const std::span<const float> w(y.data() + start, win);
        const double h1 = measure::SingleBin(1000.0, fs, win)(w, static_cast<std::int64_t>(start)).amplitude();
        double hs = 0.0;
        for (int k = 2; k <= 8; ++k)
        {
            const double a = measure::SingleBin(1000.0 * k, fs, win)(w, static_cast<std::int64_t>(start)).amplitude();
            hs += a * a;
        }
        return 10.0 * std::log10(std::max(hs, 1e-300) / std::max(h1 * h1, 1e-300));
    }
} // namespace

FCMP_PROBE(dsp, null)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    const std::string q = "null.eco";
    const HostConfig cfg = ecoConfig();

    const RawParams base = fcmp::probe::modeRaw(en);
    ParamView view;
    resolveView(desc, base, view);
    const EngineParams e0 = fcmp::probe::resolveRaw(en, base).eng;
    const double thrIn = analysis::inputThresholdDb(e0);
    const double peakOff = fcmp::probe::peakOffsetDb(en, e0);

    // the program: 1 kHz at T + 12 dB (left), noise bursts (right)
    const std::size_t n = static_cast<std::size_t>(1.5f * kFs);
    std::vector<float> xl(n), xr(n), kl(n), kr(n);
    {
        const double amp = measure::amplitudeFromDb(thrIn + 12.0 + peakOff);
        sig::Pcg32 rng(0x6e756c6c, 4);
        for (std::size_t i = 0; i < n; ++i)
        {
            xl[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, amp);
            const bool burst = (i / 4800) % 2 == 0;
            xr[i] = burst ? static_cast<float>(amp) * rng.bipolar() : 0.0f;
            kl[i] = 0.5f * rng.bipolar();
            kr[i] = sig::sineAt(static_cast<std::int64_t>(i), 220.0, kFs, 0.7);
        }
    }
    const Io stereo{ &xl, &xr, nullptr, nullptr };

    // ---- (a) mix 0 -------------------------------------------------------------------------------------------------
    if (live(view, Pid::mix))
    {
        RawParams raw = base;
        raw[Pid::mix] = 0.0f;
        const BlockParams bp = blockOf(en, raw);
        const Run r = render(cfg, bp, stereo);
        P.eq(q + ".mix0.mismatches", mismatches(r.l, delayed(xl, r.latency)) + mismatches(r.r, delayed(xr, r.latency)),
             0);
        P.ge(q + ".mix0.gr_active_db", maxGr(r), 3.0);

        BlockParams pg = bp;                                     // K2 #3c: preGain reaches the wet path only
        pg.eng.preGainDb += 12.0f;
        pg.eng.thrDb += 12.0f;
        const Run rp = render(cfg, pg, stereo);
        P.eq(q + ".mix0.pregain.mismatches",
             mismatches(rp.l, delayed(xl, rp.latency)) + mismatches(rp.r, delayed(xr, rp.latency)), 0);

        const Run rm = render(cfg, bp, Io{ &xl, nullptr, nullptr, nullptr });
        P.eq(q + ".mix0.mono.mismatches", mismatches(rm.l, delayed(xl, rm.latency)), 0);

        BlockParams kb = bp;
        kb.extKey = true;
        const Run rk = render(ecoConfig(2), kb, Io{ &xl, &xr, &kl, &kr });
        P.eq(q + ".mix0.key.mismatches",
             mismatches(rk.l, delayed(xl, rk.latency)) + mismatches(rk.r, delayed(xr, rk.latency)), 0);
    }
    else
        std::printf("NOTE     %s.mix0: mix is not a live parameter of %s; skipped\n", q.c_str(), desc.name.data());

    // ---- (b) below threshold and (c) ratio 1:1 ----------------------------------------------------------------------
    // Clean conditions: voice OFF (where there is such a step), makeup 0, auto makeup off.
    RawParams neutral = base;
    if (const int off = stepLabelled(view.spec[idx(Pid::voice)], "OFF"); off >= 0)
        neutral[Pid::voice] = view.spec[idx(Pid::voice)]->steps[static_cast<std::size_t>(off)].plain;
    if (live(view, Pid::makeup))
        neutral[Pid::makeup] = 0.0f;
    if (const int off = stepLabelled(view.spec[idx(Pid::automu)], "OFF"); off >= 0)
        neutral[Pid::automu] = view.spec[idx(Pid::automu)]->steps[static_cast<std::size_t>(off)].plain;

    const auto nullRows = [&](const std::string& k, const RawParams& raw, double levelDb) {
        const BlockParams bp = blockOf(en, raw);
        const double amp = measure::amplitudeFromDb(levelDb);
        std::vector<float> sl(n), sr(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            sl[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, amp);
            sr[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, amp, 0.25);
        }
        const Run r = render(cfg, bp, Io{ &sl, &sr, nullptr, nullptr });
        const std::vector<float> dl = delayed(sl, r.latency), dr = delayed(sr, r.latency);
        std::printf("NOTE     %s: input %.3f dBFS peak, preGain %.3g dB, makeup %.3g dB, max GR %.9g dB\n", k.c_str(),
                    levelDb, static_cast<double>(bp.eng.preGainDb), static_cast<double>(bp.eng.makeupDb), maxGr(r));
        switch (desc.rigor)
        {
            case Rigor::clean:
                P.eq(k + ".gr_nonzero", nonzeroGr(r), 0);
                P.eq(k + ".mismatches", mismatches(r.l, dl) + mismatches(r.r, dr), 0);
                break;
            case Rigor::modelled:
                P.le(k + ".residual_db", residualDb(r, dl, dr), -120.0);
                break;
            case Rigor::character:
                std::printf("NOTE     %s: character Mode: judged by the THD golden row\n", k.c_str());
                break;
        }
    };

    {
        const EngineParams e = fcmp::probe::resolveRaw(en, neutral).eng;
        const double level = analysis::inputThresholdDb(e) - 0.5 * static_cast<double>(e.kneeDb) - 20.0
                           + fcmp::probe::peakOffsetDb(en, e);
        nullRows(q + ".below", neutral, std::min(level, -1.0));
    }
    if (const ParamSpec* rs = view.spec[idx(Pid::ratio)];
        rs != nullptr && (rs->kind == Kind::continuous || rs->kind == Kind::hybrid) && rs->lo <= 0.0f
        && desc.rigor != Rigor::character)
    {
        RawParams raw = neutral;
        raw[Pid::ratio] = 0.0f;
        const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
        nullRows(q + ".ratio1", raw, std::min(analysis::inputThresholdDb(e) + 12.0, -1.0));
    }
    else
        std::printf("NOTE     %s.ratio1: ratio 1:1 is not reachable in %s (or a character Mode); skipped\n", q.c_str(),
                    desc.name.data());

    if (desc.rigor == Rigor::character)
    {
        const BlockParams bp = blockOf(en, base);
        std::vector<float> s(n);
        for (std::size_t i = 0; i < n; ++i)
            s[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, measure::amplitudeFromDb(-10.0));
        const Run r = render(cfg, bp, Io{ &s, &s, nullptr, nullptr });
        P.num(q + ".thd_db", thdDb(r.l, kFs), Tol::abs(fcmp::probe::tol::kThdGoldenAbsDb));
    }

    // ---- (e) mix 0.5 = mean of mix 0 and mix 1 ----------------------------------------------------------------------
    if (live(view, Pid::mix))
    {
        RawParams r0 = base, r5 = base, r1 = base;
        r0[Pid::mix] = 0.0f;
        r5[Pid::mix] = 0.5f;
        r1[Pid::mix] = 1.0f;
        const Run y0 = render(cfg, blockOf(en, r0), stereo), y5 = render(cfg, blockOf(en, r5), stereo),
                  y1 = render(cfg, blockOf(en, r1), stereo);
        std::vector<float> ml(n), mr(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            ml[i] = static_cast<float>(0.5 * (static_cast<double>(y0.l[i]) + y1.l[i]));
            mr[i] = static_cast<float>(0.5 * (static_cast<double>(y0.r[i]) + y1.r[i]));
        }
        P.le(q + ".mix05.err_db", residualDb(y5, ml, mr), -120.0);
    }

    // ---- bypass: bit-exact passthrough, click-free edges -----------------------------------------------------------
    {
        BlockParams on = blockOf(en, base), off = on;
        on.bypass = true;
        const Run rb = render(cfg, on, stereo);
        P.eq(q + ".bypass.mismatches",
             mismatches(rb.l, delayed(xl, rb.latency)) + mismatches(rb.r, delayed(xr, rb.latency)), 0);
        const std::size_t ramp = static_cast<std::size_t>(0.02f * kFs) + 2;
        const Run rh = render(cfg, std::vector<Event>{ Event{ 0, off, true } }, stereo);
        P.eq(q + ".bypass.host.mismatches",
             mismatches(rh.l, delayed(xl, rh.latency), ramp) + mismatches(rh.r, delayed(xr, rh.latency), ramp), 0);

        // edges at a waveform peak of a 110 Hz tone at -6 dBFS (threshold 8 dB below it: about 6 dB of GR)
        RawParams raw = base;
        raw[Pid::thr] = static_cast<float>(-6.0 - 8.0 - peakOff + static_cast<double>(e0.preGainDb));
        BlockParams pOn = blockOf(en, raw), pOff = pOn;
        pOn.bypass = true;
        const std::size_t m = static_cast<std::size_t>(2.0f * kFs);
        const std::size_t edge = static_cast<std::size_t>(kFs) + static_cast<std::size_t>(kFs / 440.0f + 0.5f);
        std::vector<float> t(m);
        for (std::size_t i = 0; i < m; ++i)
            t[i] = sig::sineAt(static_cast<std::int64_t>(i), 110.0, kFs, 0.5011872336272722);
        const Io tone{ &t, &t, nullptr, nullptr };
        const Run ctlProc = render(cfg, pOff, tone), ctlByp = render(cfg, pOn, tone);
        const Run toOn = render(cfg, std::vector<Event>{ Event{ 0, pOff, false }, Event{ edge, pOn, false } }, tone);
        const Run toOff = render(cfg, std::vector<Event>{ Event{ 0, pOn, false }, Event{ edge, pOff, false } }, tone);
        std::printf("NOTE     %s.bypass: GR before the edge %.4g dB (110 Hz, -6 dBFS, edge at a waveform peak)\n",
                    q.c_str(), static_cast<double>(lane(ctlProc.gr[edge - 1], 0)));
        P.le(q + ".bypass.on.hf_ratio_db", measure::hfRatioDb(toOn.l, ctlProc.l, ctlByp.l, edge, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        P.le(q + ".bypass.off.hf_ratio_db", measure::hfRatioDb(toOff.l, ctlProc.l, ctlByp.l, edge, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        const std::size_t landed = edge + ramp;
        P.eq(q + ".bypass.on.lands", mismatches(toOn.l, ctlByp.l, landed) + mismatches(toOn.r, ctlByp.r, landed), 0);
        P.eq(q + ".bypass.off.lands", mismatches(toOff.l, ctlProc.l, landed) + mismatches(toOff.r, ctlProc.r, landed),
             0);
        std::vector<float> splice(ctlProc.l);
        std::copy(ctlByp.l.begin() + static_cast<std::ptrdiff_t>(edge), ctlByp.l.end(),
                  splice.begin() + static_cast<std::ptrdiff_t>(edge));
        P.ge(q + ".bypass.metric_sensitivity_db", measure::hfRatioDb(splice, ctlProc.l, ctlByp.l, edge, kFs), 20.0);
    }

    // ---- latency and tail ---------------------------------------------------------------------------------------
    {
        std::int64_t design = 0;
        for (const double fs : { 44100.0, 48000.0, 96000.0 })
            for (const Quality qu : { Quality::eco, Quality::std, Quality::hq })
                for (const LookaheadBudget b : { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 })
                {
                    HostConfig c = cfg;
                    c.fs = fs;
                    c.quality = qu;
                    c.budget = b;
                    const int want = lookaheadSamples(b, fs) + kOs[static_cast<int>(qu)].latency;
                    design += EngineHost::latencyFor(c) == want ? 0 : 1;
                    if (qu != Quality::eco || b != LookaheadBudget::off)
                        continue;
                    EngineHost h;
                    h.configure(c, blockOf(en, base));
                    P.eq("null.latency.eco." + std::to_string(static_cast<int>(fs)),
                         h.latencySamples() == EngineHost::latencyFor(c) && h.latencySamples() == 0 ? 1 : 0, 1);
                }
        P.eq("null.latency.design_mismatches", design, 0);

        HostConfig c = cfg;
        c.quality = Quality::hq;
        c.budget = LookaheadBudget::ms20;
        EngineHost h;
        h.configure(c, blockOf(en, base));
        std::printf("NOTE     null.latency: an HQ + 20 ms host at 48 kHz reports %d samples at S3 (the design value, "
                    "latencyFor, is %d: F7 installs the oversampler and the lookahead delays)\n",
                    h.latencySamples(), EngineHost::latencyFor(c));

        const BlockParams bp = blockOf(en, base);
        EngineHost t;
        t.configure(cfg, bp);
        const double want = static_cast<double>(desc.tailSeconds(bp.eng))
                          + static_cast<double>(t.latencySamples()) / cfg.fs;
        P.near("null.tail.err_s", t.tailSeconds(bp) - want, 0.0, 1e-12);
    }

    return P.finish();
}
