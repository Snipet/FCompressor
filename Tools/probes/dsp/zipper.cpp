// FCMP_PROBE layer=dsp name=zipper scope=mode timeout=60
//
// dsp.zipper.<key> (F4, S3; D5: 03 §3.4 Host rows, C §5.6; K2 #4 iv, #20; 01 §5.1): parameter automation through
// fcdsp::EngineHost at ECO, 48 kHz. <p> is the host parameter id (kHostParams[].id).
//
// 1. Step edge (C §5.6.1), every parameter the Mode leaves continuous (live and continuous or hybrid at its defaults):
//    a 110 Hz tone at -6 dBFS, the threshold 8 dB below it (about 6 dB of GR), attack and release at their slowest;
//    the parameter goes 25 % -> 75 % of its range (host-normalised) in one block at t = 1 s, against controls held at
//    25 % and at 75 %.
//      zipper.edge.<p>.hf_ratio_db     <= +3 dB (C §5.0 click metric: energy above 8 kHz within +-2 ms of the edge)
//      zipper.edge.metric_sensitivity_db  the two controls of the largest-effect parameter spliced with a hard cut at
//                                      the edge read >= +20 dB: the metric sees an unsmoothed step there
// 2. Detent edges (K2 #4 iv), every stepped parameter and the steps of every hybrid one: each pair of adjacent detents,
//    both directions, at t = 1 s, same tone and setting, against the two steady controls. The engine's attack is held
//    at >= kDetentAttackMs (50 ms) in all three renders, except for `atk`'s own detents: the row judges the detent's
//    own transition, and a fast attack (DW, S4: FET 76's 0.8 ms maximum, Bus 25's 1 ms and Diode 609's 3 ms defaults)
//    reads the compressor's legitimate reaction to a new static curve, GR steps at the next waveform peak, as a click.
//      zipper.detent.<p>.<i>-<j>.hf_ratio_db  <= +3 dB
//    A pair whose detents resolve to different kernel keys (det, stmode, voice, topo: 01 §5.5) is a kernel swap, which
//    S3 runs snapped: the 20 ms crossfade is F7's. Those rows are measured and printed as NOTE lines with their verdict
//    ("pending F7"), not judged, until kKernelCrossfade is true (F7 flips it with the crossfade).
// 3. Block-rate ramp (C §5.6.2), every continuous parameter: 0 -> 1 of its range (host-normalised) over 2 s, set once
//    per block at bs = 512 (the ramp's value at the block's centre, so a half-block automation lag does not count)
//    against the same ramp set per sample (bs = 1); a 1 kHz tone at -12 dBFS, the Mode's defaults.
//      zipper.ramp.<p>.zipper_db       <= -40 dB (spec): the zipper lines f0 +- k fs/bs (k = 1..8) of y512 - y1 over
//                                      the ramp's middle second, against the tone
//      zipper.ramp.<p>.err_db          10 log10(E(y512 - y1) / E(y1)), C's number: golden abs:1 and printed
//      zipper.ramp.makeup.suppression_db  the lines of the makeup staircase applied UNSMOOTHED minus the measured ones:
//                                      >= 20 dB (the host smoothing removes the zipper; an unsmoothed staircase reads
//                                      about -42 dB here, so the -40 dB cap alone would not catch it at this rate)
//    Why the spec is on the lines (F4 finding, see the handoff): core Smoother4 lands on its target when a tick would
//    move it by less than half an ulp (its stall rule), so a per-sample ramp slower than about ulp / (2 (1 - a)) per
//    sample (thr from -60 dB at 30 dB/s) passes through with NO lag, while a block staircase is smoothed with the full
//    20 ms lag. E(y512 - y1) then measures that lag difference (thr: -33 dB on Clean), not zipper; the lines measure
//    the staircase itself (Clean: thr -73 dB, makeup -86 dB; the makeup staircase applied unsmoothed: -42 dB).
// 4. Block-size invariance (C §5.6.3): static parameters, a 1 s program (1 kHz at T + 10 dB, noise bursts, silence),
//    rendered at bs {1, 17, 64, 128, 512, 4096}, with the editor attached and the tap set, and once detached:
//      zipper.bs.mismatches            output samples differing from the bs = 512 render: 0
//      zipper.bs.tap_mismatches        tap GR (lanes 0-1) differing from the bs = 512 render: 0
//      zipper.bs.telemetry_mismatches  the detached render against the attached one (telemetry changes nothing): 0
//      zipper.bs.key.mismatches        the same over an external key (2-channel key bus, extKey on)
// 5. requestSnap() (state recall, 01 §5.1): a 1 kHz tone 20 dB below threshold (GR exactly 0; a clean-rigor Mode with
//    no colour stage at its defaults), makeup moved from 25 % to 75 % of its range between two blocks:
//      zipper.snap.mismatches          with requestSnap() before the second block, every sample of it equals
//                                      x * linFromDb(new makeup) bit for bit (the smoothers jumped): 0
//      zipper.snap.glides              without it, the second block's first sample differs (the smoothers glide): 1
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/TestTap.h"
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
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
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
    constexpr std::size_t kEdge = 48000;              // t = 1 s: a zero crossing of 110 Hz (C §5.6)
    constexpr float kDetentAttackMs = 50.0f;          // the detent renders' attack floor (header, 2.)

    // F7 sets this with the kernel crossfade (01 §5.5 step 4): kernel-key detent edges become judged spec rows.
    constexpr bool kKernelCrossfade = false;

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

    struct Signal
    {
        std::vector<float> l, r, kl, kr;
    };

    struct Run
    {
        std::vector<float> l, r;
        std::vector<simd::f32x4> gr;
    };

    using ParamsAt = std::function<BlockParams(std::size_t off, std::size_t len)>;

    // A fresh host over the signal in blocks of bs (split at `splitAt` when non-zero); paramsAt(off, len) gives each
    // block's parameters.
    Run render(const ParamsAt& paramsAt, const Signal& s, int bs, std::size_t splitAt = 0, bool attached = false,
               int keyChans = 0)
    {
        auto host = std::make_unique<EngineHost>();
        host->configure(ecoConfig(keyChans), paramsAt(0, 0));
        if (attached)
            host->setUiAttached(true);
        const std::size_t n = s.l.size();
        const bool key = !s.kl.empty();
        Run run;
        run.l.assign(n, 0.0f);
        run.r.assign(n, 0.0f);
        run.gr.assign(n, simd::set1(0.0f));
        TestTap tap;
        tap.grDb = run.gr;
        host->setTap(&tap);
        for (std::size_t off = 0; off < n;)
        {
            std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(bs), n - off);
            if (splitAt > off && splitAt < off + len)
                len = splitAt - off;
            const float* ins[2] = { s.l.data() + off, s.r.data() + off };
            float* outs[2] = { run.l.data() + off, run.r.data() + off };
            const float* keys[2] = { key ? s.kl.data() + off : nullptr, key ? s.kr.data() + off : nullptr };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.key = key ? keys : nullptr;
            io.numKey = key ? 2 : 0;
            io.n = static_cast<int>(len);
            host->process(io, paramsAt(off, len));
            off += len;
        }
        host->setTap(nullptr);
        return run;
    }

    // Held parameters: `first` before kEdge, `second` from it on.
    Run renderEdge(const BlockParams& first, const BlockParams& second, const Signal& s)
    {
        return render([&](std::size_t off, std::size_t) { return off < kEdge ? first : second; }, s, kBlock, kEdge);
    }

    float lane(simd::f32x4 v, int ln)
    {
        alignas(16) float t[4];
        simd::store(t, v);
        return t[ln & 3];
    }

    std::int64_t mismatches(const std::vector<float>& a, const std::vector<float>& b)
    {
        std::int64_t m = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            m += a[i] == b[i] ? 0 : 1;
        return m;
    }

    std::int64_t tapMismatches(const Run& a, const Run& b)
    {
        std::int64_t m = 0;
        for (std::size_t i = 0; i < a.gr.size() && i < b.gr.size(); ++i)
            m += lane(a.gr[i], 0) == lane(b.gr[i], 0) && lane(a.gr[i], 1) == lane(b.gr[i], 1) ? 0 : 1;
        return m;
    }

    // The plain value at fraction t of the spec's continuous range, host-normalised.
    float atFraction(Pid pid, const ParamSpec& s, float t)
    {
        const float a = toNorm(pid, s.lo), b = toNorm(pid, s.hi);
        return toPlain(pid, a + t * (b - a));
    }

    bool continuousLive(const ParamView& v, Pid p)
    {
        const ParamSpec* s = v.spec[idx(p)];
        return s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid) && s->lo < s->hi
            && v[p].state == SlotState::live;
    }

    bool sameKernel(const EngineParams& a, const EngineParams& b)
    {
        return a.det == b.det && a.stmode == b.stmode && a.voice == b.voice && a.topo == b.topo;
    }

    std::string pidName(Pid p) { return kHostParams[idx(p)].id; }

    // The zipper lines f0 +- k fs/bs (k = 1..8, f0 = 1 kHz, bs = 512) of `diff` against the f0 amplitude of `y`, dB,
    // over the middle of a 2 s ramp (a window of 1536 m samples holds whole cycles of every line at 48 kHz).
    double zipperLinesDb(const std::vector<float>& diff, const std::vector<float>& y)
    {
        const std::size_t w0 = diff.size() / 4, w = 1536 * 31;
        const std::span<const float> dw(diff.data() + w0, w), yw(y.data() + w0, w);
        const double f0Amp = measure::SingleBin(1000.0, kFs, w)(yw, static_cast<std::int64_t>(w0)).amplitude();
        double lines = 0.0;
        for (int m = 1; m <= 8; ++m)
            for (const double sgn : { -1.0, 1.0 })
            {
                const double a = measure::SingleBin(1000.0 + sgn * m * (kFs / kBlock), kFs, w)(
                                     dw, static_cast<std::int64_t>(w0)).amplitude();
                lines += a * a;
            }
        return lines <= 0.0 ? -400.0 : 10.0 * std::log10(lines / std::max(f0Amp * f0Amp, 1e-300));
    }
} // namespace

FCMP_PROBE(dsp, zipper)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    const RawParams defaults = fcmp::probe::modeRaw(en);
    ParamView view;
    resolveView(desc, defaults, view);
    const EngineParams e0 = fcmp::probe::resolveRaw(en, defaults).eng;
    const double peakOff = fcmp::probe::peakOffsetDb(en, e0);

    // ---- the edge setting: 110 Hz at -6 dBFS, ~6 dB of GR, slowest attack and release ------------------------------
    RawParams edgeBase = defaults;
    for (const Pid p : { Pid::atk, Pid::rel })
        if (continuousLive(view, p))
            edgeBase[p] = view.spec[idx(p)]->hi;
    if (continuousLive(view, Pid::thr))
        edgeBase[Pid::thr] = static_cast<float>(-6.0 - 8.0 - peakOff + static_cast<double>(e0.preGainDb));
    Signal tone;
    {
        const std::size_t n = static_cast<std::size_t>(2.0f * kFs);
        tone.l.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            tone.l[i] = sig::sineAt(static_cast<std::int64_t>(i), 110.0, kFs, 0.5011872336272722);
        tone.r = tone.l;
    }

    // ---- 1. step edges ----------------------------------------------------------------------------------------------
    double bestSplice = -1.0, bestSpread = -1.0;
    for (std::size_t i = 0; i < kNumModeParams; ++i)
    {
        const auto pid = static_cast<Pid>(i);
        if (pid == Pid::look || !continuousLive(view, pid))
            continue;                                   // look: no lookahead delay at S3 (F7); locked at budget OFF
        const ParamSpec& s = *view.spec[i];
        RawParams lo = edgeBase, hi = edgeBase;
        lo[pid] = atFraction(pid, s, 0.25f);
        hi[pid] = atFraction(pid, s, 0.75f);
        const BlockParams a = blockOf(en, lo), b = blockOf(en, hi);
        const Run ctlA = renderEdge(a, a, tone), ctlB = renderEdge(b, b, tone), test = renderEdge(a, b, tone);
        const std::string k = "zipper.edge." + pidName(pid);
        std::printf("NOTE     %s: %.6g -> %.6g at t = 1 s; GR %.4g -> %.4g dB (controls)\n", k.c_str(),
                    static_cast<double>(lo[pid]), static_cast<double>(hi[pid]),
                    static_cast<double>(lane(ctlA.gr[kEdge - 1], 0)), static_cast<double>(lane(ctlB.gr[kEdge - 1], 0)));
        P.le(k + ".hf_ratio_db", measure::hfRatioDb(test.l, ctlA.l, ctlB.l, kEdge, kFs),
             fcmp::probe::tol::kClickHfRatioDb);

        // the parameter with the largest output difference at the edge proves the metric (a hard splice)
        double spread = 0.0;
        for (std::size_t j = kEdge - 200; j < kEdge + 200; ++j)
            spread = std::max(spread, std::fabs(static_cast<double>(ctlA.l[j]) - ctlB.l[j]));
        if (spread > bestSpread)
        {
            std::vector<float> splice(ctlA.l);
            std::copy(ctlB.l.begin() + static_cast<std::ptrdiff_t>(kEdge), ctlB.l.end(),
                      splice.begin() + static_cast<std::ptrdiff_t>(kEdge));
            bestSpread = spread;
            bestSplice = measure::hfRatioDb(splice, ctlA.l, ctlB.l, kEdge, kFs);
        }
    }
    if (bestSpread >= 0.0)
        P.ge("zipper.edge.metric_sensitivity_db", bestSplice, 20.0);

    // ---- 2. detent edges --------------------------------------------------------------------------------------------
    int pending = 0, pendingMiss = 0;
    for (std::size_t i = 0; i < kNumModeParams; ++i)
    {
        const auto pid = static_cast<Pid>(i);
        const ParamSpec* s = view.spec[i];
        if (s == nullptr || (s->kind != Kind::stepped && s->kind != Kind::hybrid) || s->steps.size() < 2)
            continue;
        for (std::size_t d = 0; d + 1 < s->steps.size(); ++d)
            for (const bool up : { true, false })
            {
                const std::size_t from = up ? d : d + 1, to = up ? d + 1 : d;
                RawParams ra = edgeBase, rb = edgeBase;
                ra[pid] = s->steps[from].plain;
                rb[pid] = s->steps[to].plain;
                BlockParams a = blockOf(en, ra), b = blockOf(en, rb);
                if (pid != Pid::atk)
                {
                    a.eng.atkTauMs = std::max(a.eng.atkTauMs, kDetentAttackMs);
                    b.eng.atkTauMs = std::max(b.eng.atkTauMs, kDetentAttackMs);
                }
                const Run ctlA = renderEdge(a, a, tone), ctlB = renderEdge(b, b, tone), test = renderEdge(a, b, tone);
                const std::string k = "zipper.detent." + pidName(pid) + "." + std::to_string(from) + "-"
                                    + std::to_string(to) + ".hf_ratio_db";
                const double hf = measure::hfRatioDb(test.l, ctlA.l, ctlB.l, kEdge, kFs);
                if (kKernelCrossfade || sameKernel(a.eng, b.eng))
                    P.le(k, hf, fcmp::probe::tol::kClickHfRatioDb);
                else if (P.wants(k))
                {
                    const bool ok = hf <= fcmp::probe::tol::kClickHfRatioDb;
                    ++pending;
                    pendingMiss += ok ? 0 : 1;
                    std::printf("NOTE     pending F7 %s  %s  got %.9g  <= %g  (kernel-key change: a snapped swap until "
                                "the crossfade; not judged)\n",
                                ok ? "PASS" : "MISS", k.c_str(), hf, fcmp::probe::tol::kClickHfRatioDb);
                }
            }
    }
    if (pending > 0)
        std::printf("NOTE     zipper.detent: %d kernel-key edge(s) pending F7's crossfade, %d over the limit today\n",
                    pending, pendingMiss);

    // ---- 3. block-rate ramps ----------------------------------------------------------------------------------------
    {
        const std::size_t n = static_cast<std::size_t>(2.0f * kFs);
        Signal s;
        s.l.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            s.l[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, 0.25118864315095801);
        s.r = s.l;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
        {
            const auto pid = static_cast<Pid>(i);
            if (pid == Pid::look || !continuousLive(view, pid))
                continue;
            const ParamSpec& spec = *view.spec[i];
            const auto at = [&](double sample) {
                RawParams raw = defaults;
                raw[pid] = atFraction(pid, spec, static_cast<float>(std::min(1.0, sample / static_cast<double>(n))));
                return blockOf(en, raw);
            };
            const Run perSample = render([&](std::size_t off, std::size_t) { return at(static_cast<double>(off)); },
                                         s, 1);
            const Run perBlock = render(
                [&](std::size_t off, std::size_t len) {
                    return at(static_cast<double>(off) + 0.5 * static_cast<double>(len));
                },
                s, kBlock);
            double e = 0.0, ref = 0.0;
            std::vector<float> diff(n);
            for (std::size_t j = 0; j < n; ++j)
            {
                const double dl = static_cast<double>(perBlock.l[j]) - perSample.l[j];
                diff[j] = static_cast<float>(dl);
                e += dl * dl;
                ref += static_cast<double>(perSample.l[j]) * perSample.l[j];
            }
            const double errDb = e <= 0.0 ? -400.0 : 10.0 * std::log10(e / std::max(ref, 1e-300));
            const double zipDb = zipperLinesDb(diff, perSample.l);
            const std::string k = "zipper.ramp." + pidName(pid);
            if (pid == Pid::makeup)
            {
                // The metric's sensitivity: the per-sample render with the makeup staircase applied UNSMOOTHED (its
                // gain error against the ramp multiplied in) must show its zipper lines above the limit.
                std::vector<float> unsmoothed(n);
                for (std::size_t j = 0; j < n; ++j)
                {
                    const std::size_t b0 = (j / kBlock) * kBlock, len = std::min<std::size_t>(kBlock, n - b0);
                    const double held = at(static_cast<double>(b0) + 0.5 * static_cast<double>(len)).eng.makeupDb;
                    const double errDbAt = held - static_cast<double>(at(static_cast<double>(j)).eng.makeupDb);
                    unsmoothed[j] = static_cast<float>((measure::amplitudeFromDb(errDbAt) - 1.0) * perSample.l[j]);
                }
                const double raw = zipperLinesDb(unsmoothed, perSample.l);
                std::printf("NOTE     zipper.ramp.makeup: an unsmoothed staircase would read %.4g dB (the -40 dB cap "
                            "of C §5.6.2 is coarse at this rate); smoothed %.4g dB\n",
                            raw, zipDb);
                P.ge("zipper.ramp.makeup.suppression_db", raw - zipDb, 20.0);
            }
            std::printf("NOTE     %s: err_db %.4g dB (energy of y512 - y1), zipper lines %.4g dB re f0\n", k.c_str(),
                        errDb, zipDb);
            P.le(k + ".zipper_db", zipDb, -40.0);
            P.num(k + ".err_db", errDb, Tol::abs(1.0));
        }
    }

    // ---- 4. block-size invariance -----------------------------------------------------------------------------------
    {
        const std::size_t n = static_cast<std::size_t>(1.0f * kFs);
        const double thrIn = analysis::inputThresholdDb(e0);
        Signal s;
        s.l.resize(n);
        s.r.resize(n);
        s.kl.resize(n);
        s.kr.resize(n);
        sig::Pcg32 rng(0x7a697070, 11);
        const double amp = measure::amplitudeFromDb(thrIn + 10.0 + peakOff);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t seg = i / 6000;
            s.l[i] = seg % 4 == 3 ? 0.0f : sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, amp);
            s.r[i] = seg % 3 == 1 ? static_cast<float>(amp) * rng.bipolar() : 0.25f * s.l[i];
            s.kl[i] = sig::sineAt(static_cast<std::int64_t>(i), 70.0, kFs, 0.6);
            s.kr[i] = seg % 2 == 0 ? 0.4f * rng.bipolar() : 0.0f;
        }
        Signal noKey = s;
        noKey.kl.clear();
        noKey.kr.clear();
        const BlockParams bp = blockOf(en, defaults);
        BlockParams bpKey = bp;
        bpKey.extKey = true;
        const auto held = [](const BlockParams& b) { return [b](std::size_t, std::size_t) { return b; }; };

        const Run ref = render(held(bp), noKey, 512, 0, true);
        const Run refKey = render(held(bpKey), s, 512, 0, true, 2);
        std::int64_t out = 0, tapM = 0, keyM = 0;
        for (const int bs : { 1, 17, 64, 128, 4096 })
        {
            const Run r = render(held(bp), noKey, bs, 0, true);
            out += mismatches(r.l, ref.l) + mismatches(r.r, ref.r);
            tapM += tapMismatches(r, ref);
            const Run rk = render(held(bpKey), s, bs, 0, true, 2);
            keyM += mismatches(rk.l, refKey.l) + mismatches(rk.r, refKey.r) + tapMismatches(rk, refKey);
        }
        const Run detached = render(held(bp), noKey, 512, 0, false);
        P.eq("zipper.bs.mismatches", out, 0);
        P.eq("zipper.bs.tap_mismatches", tapM, 0);
        P.eq("zipper.bs.telemetry_mismatches", mismatches(detached.l, ref.l) + mismatches(detached.r, ref.r), 0);
        P.eq("zipper.bs.key.mismatches", keyM, 0);
    }

    // ---- 5. requestSnap ---------------------------------------------------------------------------------------------
    if (desc.rigor == Rigor::clean && continuousLive(view, Pid::makeup) && view[Pid::drive].state == SlotState::na
        && (e0.flags & kEngAutoMakeup) == 0 && e0.preGainDb == 0.0f)
    {
        constexpr std::size_t kHalf = 2048;
        Signal s;
        s.l.resize(2 * kHalf);
        const double below = analysis::inputThresholdDb(e0) - 0.5 * static_cast<double>(e0.kneeDb) - 20.0;
        const double amp = measure::amplitudeFromDb(below + peakOff);
        for (std::size_t i = 0; i < s.l.size(); ++i)
            s.l[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, amp);
        s.r = s.l;
        RawParams ra = defaults, rb = defaults;
        ra[Pid::makeup] = atFraction(Pid::makeup, *view.spec[idx(Pid::makeup)], 0.25f);
        rb[Pid::makeup] = atFraction(Pid::makeup, *view.spec[idx(Pid::makeup)], 0.75f);
        const BlockParams pa = blockOf(en, ra), pb = blockOf(en, rb);
        const float g = simd::lane<0>(linFromDb(simd::set1(pb.eng.makeupDb)));
        for (const bool snap : { true, false })
        {
            auto host = std::make_unique<EngineHost>();
            host->configure(ecoConfig(), pa);
            std::vector<float> ol(2 * kHalf), orr(2 * kHalf);
            for (std::size_t off = 0; off < 2 * kHalf; off += kHalf)
            {
                if (off == kHalf && snap)
                    host->requestSnap();
                const float* ins[2] = { s.l.data() + off, s.r.data() + off };
                float* outs[2] = { ol.data() + off, orr.data() + off };
                ProcessIo io;
                io.in = ins;
                io.numIn = 2;
                io.out = outs;
                io.numOut = 2;
                io.n = static_cast<int>(kHalf);
                host->process(io, off == 0 ? pa : pb);
            }
            std::int64_t m = 0;
            for (std::size_t i = kHalf; i < 2 * kHalf; ++i)
                m += (ol[i] == s.l[i] * g ? 0 : 1) + (orr[i] == s.r[i] * g ? 0 : 1);
            if (snap)
                P.eq("zipper.snap.mismatches", m, 0);
            else
                P.eq("zipper.snap.glides", ol[kHalf + 1] != s.l[kHalf + 1] * g ? 1 : 0, 1);
        }
    }
    else
        std::printf("NOTE     zipper.snap: needs a clean-rigor Mode without colour, auto makeup or preGain at its "
                    "defaults; skipped\n");

    return P.finish();
}
