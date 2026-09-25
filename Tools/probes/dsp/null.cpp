// FCMP_PROBE layer=dsp name=null scope=mode timeout=60
//
// dsp.null.<key> (F4, S3; F7, S6; D8 (a), (b), (c), (e) at engine level: 03 §3.4 Host rows, 03 §3.7, C §5.8; 01 §5.6;
// K1 #2, K2 #3c, #12; ADR-17): the nulls of fcdsp::EngineHost at every Quality, plus the delta and SC-listen
// identities (01 §5.4 steps 2g and 2i).
//
// References (01 §5.6, ADR-17). <q> = eco, std, hq; L = latencySamples() = L_la + kOs[q].latency.
//   ECO:      delay(x, L), bit for bit;
//   STD, HQ:  down(up(delay(x, L_la))) through a control Oversampler at the same Quality (2 channels, 512-sample
//             blocks; dsp.os proves the round trip is block-size and channel-count invariant), bit for bit;
//   bypass:   delay(x, L) at every Quality, bit for bit.
//
// Program: 1.5 s at 48 kHz, stereo, L != R: a 1 kHz sine at the input threshold + 12 dB (left) and seeded noise bursts
// (right), so the wet path compresses (the rows below check that it does) and the lanes differ. Blocks of 512 samples.
//
// Rows (spec, blocking), per Quality:
//   null.<q>.mix0.mismatches        mix 0: every output sample == the reference (max_err = 0), stereo
//   null.<q>.mix0.gr_active_db      ... while the wet path runs >= 3 dB of GR (so the null is not vacuous)
//   null.<q>.mix0.pregain.mismatches  mix 0 with EngineParams::preGainDb = +12 (the threshold moved with it): the dry
//                                   path is never pre-gained (K2 #3c)
//   null.<q>.mix0.mono.mismatches   mono in, mono out
//   null.<q>.mix0.key.mismatches    an active external key (extKey on, a 2-channel key bus)
//   null.<q>.mix0.la5.mismatches    a 5 ms lookahead budget: the reference of the delayed input (L_la = 240)
//   null.<q>.mix0.passband_db       STD/HQ: the host's mix-0 impulse response at 44.1 kHz is flat within 0.01 dB to
//                                   20 kHz (01 §5.6, 03 §3.7)
//   null.<q>.below.*                (b) input 20 dB below threshold - knee/2, voice OFF, makeup 0, auto makeup off:
//                                   rigor clean: .gr_nonzero (tap GR lanes 0-1 exactly 0.0) and .mismatches against
//                                   the reference; modelled: .residual_db <= -120 dB; character: judged by the THD row
//   null.<q>.ratio1.*               (c) ratio 1:1 (slope 0) where the Mode allows it, input at threshold + 12 dB: the
//                                   same rules as (b) (character: skipped)
//   null.<q>.mix05.err_db           (e) mix 0.5 against the mean of mix 0 and mix 1: <= -120 dB
//   null.<q>.bypass.mismatches      bypass engaged (from configure): bit-exact against delay(x, L), every sample
//   null.<q>.bypass.la20.mismatches ... with a 20 ms lookahead budget
//   null.<q>.bypass.host.mismatches ProcessIo::hostBypassed (processBlockBypassed) likewise, once its 20 ms ramp has
//                                   landed (configure() cannot know the host's bypass, so it ramps in)
//   null.<q>.bypass.<on|off>.hf_ratio_db  the bypass edge at a waveform peak (the worst place) of a 110 Hz tone at
//                                   -6 dBFS with about 6 dB of GR (C §5.6's level), against both steady controls:
//                                   <= +3 dB (the smootherstep-shaped 20 ms ramp, host/Ramps.h)
//   null.<q>.bypass.gr12.<on|off>.hf_ratio_db  the same with the threshold 16 dB under the tone (>= 10.5 dB of GR on
//                                   Clean: S6 lead revision 2, where smoothstep read +3.3 dB)
//   null.<q>.bypass.<on|off>.lands  from 20 ms after the edge the output equals the bypassed control (on) or the
//                                   processed control (off) bit for bit: the processing ran on under bypass
//   null.<q>.bypass.metric_sensitivity_db  the controls spliced with a hard cut read >= +20 dB (the metric sees a step)
//   null.<q>.delta.sum_err_db       delta (01 §5.4 step 2g: mix (dry - wet linFromDb(-preGain - makeupTotal))) at
//                                   mix 1 plus the processed output times linFromDb(-preGain - makeupTotal) equals the
//                                   mix-0 reference: <= -120 dB (what the GR and colour removed, and nothing else)
//   null.<q>.delta.nonzero          ... and the delta output is not silent (>= 1 sample with |y| > 1e-4)
//   null.<q>.delta.<on|off>.hf_ratio_db  the delta edge at a waveform peak of the 110 Hz tone (6 dB of GR): <= +3 dB
//   null.<q>.listen.mismatches      SC listen engaged (from configure) on the internal side chain, SC filters OFF and a
//                                   STEREO route: the output IS delay(x, L) (the side chain decoded, unity gain), bit
//                                   for bit; another route: <= -120 dB against the decoded reference (NOTE)
//   null.<q>.listen.key.mismatches  ... on an active external key: delay(key, L)
//   null.<q>.listen.<on|off>.hf_ratio_db  the listen edge at a waveform peak of the 110 Hz tone: <= +3 dB
// Character Modes: null.eco.thd_db, the THD of a 1 kHz tone at -10 dBFS, golden abs:0.5 (no spec row).
// Latency and tail (01 §5.4, §5.6):
//   null.latency.design_mismatches  EngineHost::latencyFor over Quality x budget at 44.1/48/96 kHz against
//                                   lookaheadSamples(budget, fs) + kOs[quality].latency: 0
//   null.latency.reported_mismatches  latencySamples() of a configured host == latencyFor(its config), same grid: 0
//   null.latency.eco.<fs>           latencySamples() of an ECO host without lookahead == 0
//   null.tail.err_s                 tailSeconds(bp) == desc.tailSeconds(eng) + latency / fs
// Lookahead limiter soak (Modes with wantsLookahead: Brickwall; K2 #21c, M7 S11): after 2 s of the program, 10 minutes
// under the threshold at STD with a 5 ms budget keep the tapped GR exactly 0.0 (null.soak.gr_active_db,
// null.soak.gr_nonzero; the section at the end of the body says how).
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
#include "fcdsp/engine/host/Router.h"
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
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    constexpr Quality kQualities[3] = { Quality::eco, Quality::std, Quality::hq };

    const char* nameOf(Quality q) { return q == Quality::eco ? "eco" : q == Quality::std ? "std" : "hq"; }

    // ---- host driver (the same small driver as the other host probes; one per file, no shared probe file) -----------
    HostConfig config(Quality q, LookaheadBudget b = LookaheadBudget::off, int keyChans = 0, double fs = kFs)
    {
        HostConfig c;
        c.fs = fs;
        c.maxBlock = kBlock;
        c.quality = q;
        c.budget = b;
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

    // The transparent-path reference of 01 §5.6 at Quality q for a host with `la` lookahead samples: ECO
    // delay(x, la) (its latency is la); STD/HQ the control Oversampler round trip of delay(x, la).
    std::pair<std::vector<float>, std::vector<float>> reference(Quality q, int la, const std::vector<float>& xl,
                                                                const std::vector<float>& xr)
    {
        std::vector<float> dl = delayed(xl, la), dr = delayed(xr, la);
        if (q == Quality::eco)
            return { dl, dr };
        Oversampler os;
        os.configure(q, kBlock, 2);
        const int f = kOs[static_cast<int>(q)].factor;
        std::vector<float> ul(static_cast<std::size_t>(kBlock * f)), ur(ul.size());
        for (std::size_t off = 0; off < dl.size(); off += static_cast<std::size_t>(kBlock))
        {
            const int m = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(kBlock), dl.size() - off));
            const float* in[2] = { dl.data() + off, dr.data() + off };
            float* up[2] = { ul.data(), ur.data() };
            const int nOs = os.up(in, m, up);
            float* out[2] = { dl.data() + off, dr.data() + off };
            const float* upc[2] = { ul.data(), ur.data() };
            os.down(upc, nOs, out);
        }
        return { dl, dr };
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
    double residualDb(const std::vector<float>& al, const std::vector<float>& ar, const std::vector<float>& bl,
                      const std::vector<float>& br)
    {
        double e = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < al.size(); ++i)
        {
            const double dl = static_cast<double>(al[i]) - bl[i], dr = static_cast<double>(ar[i]) - br[i];
            e += dl * dl + dr * dr;
            ref += static_cast<double>(bl[i]) * bl[i] + static_cast<double>(br[i]) * br[i];
        }
        return e <= 0.0 ? -400.0 : 10.0 * std::log10(e / std::max(ref, 1e-300));
    }

    double residualDb(const Run& a, const std::vector<float>& bl, const std::vector<float>& br)
    {
        return residualDb(a.l, a.r, bl, br);
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

    // The largest |20 log10 |H(f)|| from 20 Hz to kMix0PassbandTopHz of the host's mix-0 impulse response at 44.1 kHz
    // (a direct DFT in double of 16384 samples, 256 log-spaced frequencies).
    double passbandDb(const ModeEntry& en, const BlockParams& mix0, Quality q)
    {
        constexpr double kRate = 44100.0;
        constexpr std::size_t kLen = 16384, kAt = 64;
        std::vector<float> x(kLen, 0.0f);
        x[kAt] = 0.25f;
        (void) en;
        const Run r = render(config(q, LookaheadBudget::off, 0, kRate), mix0, Io{ &x, &x, nullptr, nullptr });
        double worst = 0.0;
        for (int k = 0; k < 256; ++k)
        {
            const double hz = 20.0 * std::pow(fcmp::probe::tol::kMix0PassbandTopHz / 20.0, k / 255.0);
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < kLen; ++i)
            {
                const double t = hz * static_cast<double>(i) / kRate;
                re += static_cast<double>(r.l[i]) * sig::cosTurns(t);
                im -= static_cast<double>(r.l[i]) * sig::sinTurns(t);
            }
            const double mag = std::sqrt(re * re + im * im) / 0.25;
            worst = std::max(worst, std::fabs(20.0 * std::log10(std::max(mag, 1e-30))));
        }
        return worst;
    }

    // The click rows of one host transition (bypass, delta, listen) at a waveform peak of a 110 Hz tone: <k>.on / .off
    // hf_ratio_db against the two steady controls; <k>.<on|off>.lands (optional) and the metric's sensitivity.
    void clickRows(Probe& P, const std::string& k, const HostConfig& cfg, const BlockParams& off, const BlockParams& on,
                   bool lands, bool sensitivity)
    {
        const std::size_t m = static_cast<std::size_t>(2.0f * kFs);
        const std::size_t edge = static_cast<std::size_t>(kFs) + static_cast<std::size_t>(kFs / 440.0f + 0.5f);
        std::vector<float> t(m);
        for (std::size_t i = 0; i < m; ++i)
            t[i] = sig::sineAt(static_cast<std::int64_t>(i), 110.0, kFs, 0.5011872336272722);
        const Io tone{ &t, &t, nullptr, nullptr };
        const Run ctlOff = render(cfg, off, tone), ctlOn = render(cfg, on, tone);
        const Run toOn = render(cfg, std::vector<Event>{ Event{ 0, off, false }, Event{ edge, on, false } }, tone);
        const Run toOff = render(cfg, std::vector<Event>{ Event{ 0, on, false }, Event{ edge, off, false } }, tone);
        // the edge seen at the output is `latency` samples later
        const std::size_t e = edge + static_cast<std::size_t>(ctlOff.latency);
        std::printf("NOTE     %s: GR before the edge %.4g dB (110 Hz, -6 dBFS, edge at a waveform peak)\n", k.c_str(),
                    static_cast<double>(lane(ctlOff.gr[edge - 1], 0)));
        P.le(k + ".on.hf_ratio_db", measure::hfRatioDb(toOn.l, ctlOff.l, ctlOn.l, e, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        P.le(k + ".off.hf_ratio_db", measure::hfRatioDb(toOff.l, ctlOff.l, ctlOn.l, e, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        if (lands)
        {
            const std::size_t landed = e + static_cast<std::size_t>(0.02f * kFs) + 2;
            P.eq(k + ".on.lands", mismatches(toOn.l, ctlOn.l, landed) + mismatches(toOn.r, ctlOn.r, landed), 0);
            P.eq(k + ".off.lands", mismatches(toOff.l, ctlOff.l, landed) + mismatches(toOff.r, ctlOff.r, landed), 0);
        }
        if (sensitivity)
        {
            std::vector<float> splice(ctlOff.l);
            std::copy(ctlOn.l.begin() + static_cast<std::ptrdiff_t>(e), ctlOn.l.end(),
                      splice.begin() + static_cast<std::ptrdiff_t>(e));
            P.ge(k + ".metric_sensitivity_db", measure::hfRatioDb(splice, ctlOff.l, ctlOn.l, e, kFs), 20.0);
        }
    }
} // namespace

FCMP_PROBE(dsp, null)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;

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

    // Clean conditions for (b) and (c): voice OFF (where there is such a step), makeup 0, auto makeup off.
    RawParams neutral = base;
    if (const int off = stepLabelled(view.spec[idx(Pid::voice)], "OFF"); off >= 0)
        neutral[Pid::voice] = view.spec[idx(Pid::voice)]->steps[static_cast<std::size_t>(off)].plain;
    if (live(view, Pid::makeup))
        neutral[Pid::makeup] = 0.0f;
    if (const int off = stepLabelled(view.spec[idx(Pid::automu)], "OFF"); off >= 0)
        neutral[Pid::automu] = view.spec[idx(Pid::automu)]->steps[static_cast<std::size_t>(off)].plain;

    for (const Quality qu : kQualities)
    {
        const std::string q = std::string("null.") + nameOf(qu);
        const HostConfig cfg = config(qu);
        const auto [refL, refR] = reference(qu, 0, xl, xr);

        // ---- (a) mix 0 ------------------------------------------------------------------------------------------
        if (live(view, Pid::mix))
        {
            RawParams raw = base;
            raw[Pid::mix] = 0.0f;
            const BlockParams bp = blockOf(en, raw);
            const Run r = render(cfg, bp, stereo);
            P.eq(q + ".mix0.mismatches", mismatches(r.l, refL) + mismatches(r.r, refR), 0);
            P.ge(q + ".mix0.gr_active_db", maxGr(r), 3.0);

            BlockParams pg = bp;                                     // K2 #3c: preGain reaches the wet path only
            pg.eng.preGainDb += 12.0f;
            pg.eng.thrDb += 12.0f;
            const Run rp = render(cfg, pg, stereo);
            P.eq(q + ".mix0.pregain.mismatches", mismatches(rp.l, refL) + mismatches(rp.r, refR), 0);

            const auto [monoRef, monoRef2] = reference(qu, 0, xl, xl);
            (void) monoRef2;
            const Run rm = render(cfg, bp, Io{ &xl, nullptr, nullptr, nullptr });
            P.eq(q + ".mix0.mono.mismatches", mismatches(rm.l, monoRef), 0);

            BlockParams kb = bp;
            kb.extKey = true;
            const Run rk = render(config(qu, LookaheadBudget::off, 2), kb, Io{ &xl, &xr, &kl, &kr });
            P.eq(q + ".mix0.key.mismatches", mismatches(rk.l, refL) + mismatches(rk.r, refR), 0);

            const HostConfig la5 = config(qu, LookaheadBudget::ms5);
            const auto [laL, laR] = reference(qu, lookaheadSamples(LookaheadBudget::ms5, kFs), xl, xr);
            const Run rl = render(la5, bp, stereo);
            P.eq(q + ".mix0.la5.mismatches", mismatches(rl.l, laL) + mismatches(rl.r, laR), 0);

            if (qu != Quality::eco)
            {
                const double pb = passbandDb(en, bp, qu);
                std::printf("NOTE     %s.mix0.passband_db = %.6g dB (20 Hz-20 kHz at 44.1 kHz)\n", q.c_str(), pb);
                P.le(q + ".mix0.passband_db", pb, fcmp::probe::tol::kMix0PassbandDb);
            }
        }
        else
            std::printf("NOTE     %s.mix0: mix is not a live parameter of %s; skipped\n", q.c_str(), desc.name.data());

        // ---- (b) below threshold and (c) ratio 1:1 ------------------------------------------------------------------
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
            const auto [dl, dr] = reference(qu, 0, sl, sr);
            std::printf("NOTE     %s: input %.3f dBFS peak, preGain %.3g dB, makeup %.3g dB, max GR %.9g dB\n",
                        k.c_str(), levelDb, static_cast<double>(bp.eng.preGainDb),
                        static_cast<double>(bp.eng.makeupDb), maxGr(r));
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
            std::printf("NOTE     %s.ratio1: ratio 1:1 is not reachable in %s (or a character Mode); skipped\n",
                        q.c_str(), desc.name.data());

        if (desc.rigor == Rigor::character)
        {
            const BlockParams bp = blockOf(en, base);
            std::vector<float> s(n);
            for (std::size_t i = 0; i < n; ++i)
                s[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, measure::amplitudeFromDb(-10.0));
            const Run r = render(cfg, bp, Io{ &s, &s, nullptr, nullptr });
            if (qu == Quality::eco)
                P.num(q + ".thd_db", thdDb(r.l, kFs), Tol::abs(fcmp::probe::tol::kThdGoldenAbsDb));
            else
                std::printf("NOTE     %s.thd_db = %.4g dB (1 kHz at -10 dBFS)\n", q.c_str(), thdDb(r.l, kFs));
        }

        // ---- (e) mix 0.5 = mean of mix 0 and mix 1 ------------------------------------------------------------------
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

        // ---- bypass: bit-exact passthrough, click-free edges --------------------------------------------------------
        {
            BlockParams on = blockOf(en, base), off = on;
            on.bypass = true;
            const Run rb = render(cfg, on, stereo);
            P.eq(q + ".bypass.mismatches",
                 mismatches(rb.l, delayed(xl, rb.latency)) + mismatches(rb.r, delayed(xr, rb.latency)), 0);
            const Run rb20 = render(config(qu, LookaheadBudget::ms20), on, stereo);
            P.eq(q + ".bypass.la20.mismatches",
                 mismatches(rb20.l, delayed(xl, rb20.latency)) + mismatches(rb20.r, delayed(xr, rb20.latency)), 0);
            const std::size_t ramp = static_cast<std::size_t>(0.02f * kFs) + 2;
            const Run rh = render(cfg, std::vector<Event>{ Event{ 0, off, true } }, stereo);
            P.eq(q + ".bypass.host.mismatches",
                 mismatches(rh.l, delayed(xl, rh.latency), ramp) + mismatches(rh.r, delayed(xr, rh.latency), ramp), 0);

            // edges at a waveform peak of a 110 Hz tone at -6 dBFS (threshold 8 dB below it: about 6 dB of GR; 16 dB
            // below it: >= 10.5 dB of GR on Clean)
            for (const double under : { 8.0, 16.0 })
            {
                RawParams raw = base;
                raw[Pid::thr] = static_cast<float>(-6.0 - under - peakOff + static_cast<double>(e0.preGainDb));
                BlockParams pOn = blockOf(en, raw), pOff = pOn;
                pOn.bypass = true;
                clickRows(P, q + (under < 10.0 ? ".bypass" : ".bypass.gr12"), cfg, pOff, pOn, under < 10.0,
                          under < 10.0);
            }
        }

        // ---- delta: what the GR (and colour) removed ----------------------------------------------------------------
        {
            BlockParams norm = blockOf(en, base);
            norm.eng.mix = 1.0f;
            BlockParams dlt = norm;
            dlt.delta = true;
            const Run yn = render(cfg, norm, stereo), yd = render(cfg, dlt, stereo);
            const fcmp::probe::EngineRig rig(en, norm.eng, kFs);
            const double undo = measure::amplitudeFromDb(-static_cast<double>(norm.eng.preGainDb)
                                                         - static_cast<double>(rig.makeupTotalDb()));
            std::vector<float> sl(n), sr(n);
            std::int64_t loud = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                sl[i] = static_cast<float>(static_cast<double>(yd.l[i]) + undo * yn.l[i]);
                sr[i] = static_cast<float>(static_cast<double>(yd.r[i]) + undo * yn.r[i]);
                loud += std::fabs(yd.l[i]) > 1e-4f || std::fabs(yd.r[i]) > 1e-4f ? 1 : 0;
            }
            P.le(q + ".delta.sum_err_db", residualDb(sl, sr, refL, refR), -120.0);
            P.ge(q + ".delta.nonzero", static_cast<double>(loud), 1.0);

            RawParams raw = base;
            raw[Pid::thr] = static_cast<float>(-6.0 - 8.0 - peakOff + static_cast<double>(e0.preGainDb));
            BlockParams pOff = blockOf(en, raw), pOn = pOff;
            pOn.delta = true;
            clickRows(P, q + ".delta", cfg, pOff, pOn, false, false);
        }

        // ---- SC listen: the side chain as the engine hears it, latency-aligned, unity gain -------------------------
        {
            BlockParams ls = blockOf(en, base);
            ls.eng.scHpfHz = 0.0f;
            ls.eng.sceDbOct = 0.0f;
            ls.listen = true;
            const host::Route route = host::routeOf(ls.eng.stmode);
            const Run r = render(cfg, ls, stereo);
            std::vector<float> wl = delayed(xl, r.latency), wr = delayed(xr, r.latency);
            if (route.domain == LaneDomain::lr)
                P.eq(q + ".listen.mismatches", mismatches(r.l, wl) + mismatches(r.r, wr), 0);
            else
            {
                // decode as Router::listen does: M/S -> L/R, a single source on both outputs
                for (std::size_t i = 0; i < n; ++i)
                {
                    const float m = 0.5f * (wl[i] + wr[i]), s = 0.5f * (wl[i] - wr[i]);
                    const float c0 = route.scFrom == 1 ? s : m, c1 = route.scFrom == 0 ? m : s;
                    wl[i] = route.scFrom >= 0 ? c0 : c0 + c1;
                    wr[i] = route.scFrom >= 0 ? c0 : c0 - c1;
                }
                const double err = residualDb(r, wl, wr);
                std::printf("NOTE     %s.listen: stereo mode %d (not STEREO): %.4g dB against the decoded reference\n",
                            q.c_str(), static_cast<int>(ls.eng.stmode), err);
                P.le(q + ".listen.decoded_err_db", err, -120.0);
            }
            BlockParams lk = ls;
            lk.extKey = true;
            const Run rk = render(config(qu, LookaheadBudget::off, 2), lk, Io{ &xl, &xr, &kl, &kr });
            if (route.domain == LaneDomain::lr)
                P.eq(q + ".listen.key.mismatches",
                     mismatches(rk.l, delayed(kl, rk.latency)) + mismatches(rk.r, delayed(kr, rk.latency)), 0);

            RawParams raw = base;
            raw[Pid::thr] = static_cast<float>(-6.0 - 8.0 - peakOff + static_cast<double>(e0.preGainDb));
            BlockParams pOff = blockOf(en, raw), pOn = pOff;
            pOn.listen = true;
            clickRows(P, q + ".listen", cfg, pOff, pOn, false, false);
        }
    }

    // ---- latency and tail -------------------------------------------------------------------------------------------
    {
        std::int64_t design = 0, reported = 0;
        const BlockParams bp = blockOf(en, base);
        for (const double fs : { 44100.0, 48000.0, 96000.0 })
            for (const Quality qu : kQualities)
                for (const LookaheadBudget b : { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 })
                {
                    const HostConfig c = config(qu, b, 0, fs);
                    const int want = lookaheadSamples(b, fs) + kOs[static_cast<int>(qu)].latency;
                    design += EngineHost::latencyFor(c) == want ? 0 : 1;
                    EngineHost h;
                    h.configure(c, bp);
                    reported += h.latencySamples() == want ? 0 : 1;
                    if (qu == Quality::eco && b == LookaheadBudget::off)
                    {
                        const std::string k = "null.latency.eco." + std::to_string(static_cast<int>(fs));
                        P.eq(k, h.latencySamples() == 0 ? 1 : 0, 1);
                    }
                }
        P.eq("null.latency.design_mismatches", design, 0);
        P.eq("null.latency.reported_mismatches", reported, 0);

        EngineHost t;
        const HostConfig c = config(Quality::hq, LookaheadBudget::ms20);
        t.configure(c, bp);
        const double want = static_cast<double>(desc.tailSeconds(bp.eng))
                          + static_cast<double>(t.latencySamples()) / c.fs;
        std::printf("NOTE     null.tail: %.6g s at HQ + 20 ms (latency %d samples)\n", t.tailSeconds(bp),
                    t.latencySamples());
        P.near("null.tail.err_s", t.tailSeconds(bp) - want, 0.0, 1e-12);
    }

    // ---- the lookahead limiter's soak (K2 #21c; 01 §10.7; 03 §3.4 "Brickwall: a 10-minute below-threshold soak keeps
    // GR exactly 0.0"; M7, S11) ---------------------------------------------------------------------------------------
    // Modes with wantsLookahead (Brickwall), STD with a 5 ms budget, the Mode's defaults: 2 s of the program above (12 dB
    // over the threshold, so the box sums take non-representable GR values), then 10 minutes 20 dB under the threshold
    // (1 kHz left, 220 Hz right); from 30 s after the loud part (the release has landed) the tapped GR of lanes 0-1 must be
    // exactly 0.0 at every sample: the box averages' running sums are re-summed exactly every 4096 samples, so their
    // rounding cannot accumulate (SlidingMaxBox.h).
    //   null.soak.gr_active_db  the loud part limits (>= 3 dB of GR), so the soak starts from charged sums
    //   null.soak.gr_nonzero    samples with GR != 0.0 (lanes 0-1) from 32 s to 10 min 2 s: 0
    if (desc.wantsLookahead)
    {
        constexpr std::size_t kSoakBlock = 4096;
        const std::size_t loud = static_cast<std::size_t>(2.0f * kFs), total = loud + static_cast<std::size_t>(600.0f * kFs);
        const std::size_t judgedFrom = loud + static_cast<std::size_t>(30.0f * kFs);
        const RawParams raw = fcmp::probe::modeRaw(en, LookaheadBudget::ms5);
        const BlockParams bp = blockOf(en, raw);
        const double quiet = measure::amplitudeFromDb(analysis::inputThresholdDb(bp.eng)
                                                      - 0.5 * static_cast<double>(bp.eng.kneeDb) - 20.0 + peakOff);
        auto host = std::make_unique<EngineHost>();
        host->configure(config(Quality::std, LookaheadBudget::ms5), bp);
        std::vector<float> il(kSoakBlock), ir(kSoakBlock), ol(kSoakBlock), orr(kSoakBlock);
        std::vector<simd::f32x4> gr(kSoakBlock);
        TestTap tap;
        tap.grDb = gr;
        double charged = 0.0;
        std::int64_t nonzero = 0;
        for (std::size_t off = 0; off < total; off += kSoakBlock)
        {
            const std::size_t m = std::min(kSoakBlock, total - off);
            for (std::size_t k = 0; k < m; ++k)
            {
                const std::size_t i = off + k;
                il[k] = i < loud ? xl[i % n] : sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, quiet);
                ir[k] = i < loud ? xr[i % n] : sig::sineAt(static_cast<std::int64_t>(i), 220.0, kFs, quiet);
            }
            tap.firstSample = off;
            tap.written = 0;
            host->setTap(&tap);
            const float* ins[2] = { il.data(), ir.data() };
            float* outs[2] = { ol.data(), orr.data() };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(m);
            host->process(io, bp);
            for (std::size_t k = 0; k < m; ++k)
            {
                const float g0 = lane(gr[k], 0), g1 = lane(gr[k], 1);
                if (off + k < loud)
                    charged = std::max({ charged, static_cast<double>(g0), static_cast<double>(g1) });
                else if (off + k >= judgedFrom)
                    nonzero += g0 != 0.0f || g1 != 0.0f ? 1 : 0;
            }
        }
        host->setTap(nullptr);
        std::printf("NOTE     null.soak: STD, 5 ms budget, look %.3g ms: %.4g dB of GR in the loud 2 s, then 10 min at "
                    "%.2f dBFS; %lld nonzero GR sample(s) from 32 s\n",
                    static_cast<double>(bp.eng.lookMs), charged, measure::dbFromAmplitude(quiet),
                    static_cast<long long>(nonzero));
        P.ge("null.soak.gr_active_db", charged, 3.0);
        P.eq("null.soak.gr_nonzero", nonzero, 0);
    }
    else
        std::printf("NOTE     null.soak: %s does not want a lookahead (no sliding-max box); skipped\n", desc.name.data());

    return P.finish();
}
