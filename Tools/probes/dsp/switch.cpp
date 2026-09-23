// FCMP_PROBE layer=dsp name=switch scope=mode timeout=120
//
// dsp.switch.<key> (F7, S6; D4: 03 §3.4 "dsp.switch", §3.10 #1, C §5.5; 01 §5.5; E §5.1; K2 #3, #22; K3 #17; ADR-14):
// Mode and kernel switches through fcdsp::EngineHost at the plugin's default setup (STD, no lookahead, 48 kHz, blocks
// of 512, the editor attached so every block's UiFrame is read back).
//
// Setup (C §5.5). A 110 Hz sine at -6 dBFS (L = R); the raw state is the SOURCE Mode's defaults with threshold -20 dB
// and ratio 4:1 (slope 0.75: "nearest to 4", snapped on read by each Mode). A switch writes only `mode` (01 §4.5: the
// raw values are truth, snapping happens on read), so the target Mode resolves the same raw state. Three renders per
// ordered pair A -> B: T switches at t = 1 s (between blocks, as a host does), CA stays in A, CB stays in B, all from
// the same raw state. The output edge is 1 s + latencySamples().
//
// Pairs (K3 #17: a Mode owns every pair with a lower-slot Mode, both directions, so adding a Mode rewrites no other
// Mode's file): <a>-<b> for every registered Mode b with a lower slot than this Mode a, and <b>-<a>.
//   switch.<a>-<b>.hf_ratio_db      C §5.0 click metric at the output edge against CA and CB: <= +3 dB
//   switch.<a>-<b>.level_dev_db     5 ms RMS windows from -20 ms to +100 ms: how far T leaves [min(CA, CB) - c,
//                                   max(CA, CB)], in dB: <= 1 (the +-1 dB envelope of C §5.5), with c = max(0, GR of
//                                   CA - GR of CB at the switch; UiFrame appliedGrDb, the louder lane): the new engine
//                                   is seeded with the old applied GR (01 §5.5 step 3, E §5.1), so it starts that much
//                                   over-compressed and recovers at its own release; the hand-over never
//                                   under-compresses (K2 #3d), and above the envelope nothing is allowed. F7 choice:
//                                   with c = 0, every switch whose Modes differ by > 1 dB of GR at the test level
//                                   (Brickwall at infinity:1 against a 4:1 Mode: 3.1-3.8 dB) would fail on the
//                                   design's own seeding
//   switch.<a>-<b>.nonfinite        0
//   switch.<a>-<b>.latency_constant 1: the reported latency never changes, and T's lag against the input (the phase of
//                                   its 110 Hz fundamental over 0.1 s) equals CA's before the switch and CB's 200 ms
//                                   after it, within 0.5 samples: a switch is never a time jump (two Modes' own
//                                   waveform-shaped GR may place the fundamental a fraction of a sample apart, so the
//                                   comparison is with each side's control, not T with itself)
//   switch.<a>-<b>.faded            1: the switch ran the kernel crossfade (UiFrame kUiFading, fadeFromSlot)
//   switch.<a>-<b>.settle_ms        golden rel:0.1: the output time after which T nulls against CB at <= -60 dB in
//                                   every 5 ms window (the render's end, 1000 ms, when it never does)
// A -> B -> A (03 §3.10 #1), B = slot 0's Mode (for slot 0 itself: the next registered Mode), switches at 1 s and
// 1.5 s:
//   switch.aba.<b>.param_writes     raw fields that differ between the A and the B state: 1 (the mode write only)
//   switch.aba.<b>.raw_bitexact     the raw state after A -> B -> A equals the one before, bit for bit: 1
//   switch.aba.<b>.eng_bitexact     resolve(A, raw) after equals resolve(A, raw) before, bit for bit: 1
//   switch.aba.<b>.<ab|ba>.hf_ratio_db  each edge against its controls: <= +3 dB; switch.aba.<b>.nonfinite: 0
//   (NOTE) how far the output is from CA over the last 100 ms of 2.5 s
// A hovering automation lane (K2 #22): the Mode toggles between A and B every 64-sample block for 0.5 s, then rests;
// UiFrames per block (kUiFading runs mark crossfade starts):
//   switch.hover.starts             crossfade starts seen: >= 2 (the lane really switched)
//   switch.hover.min_gap_ms         the smallest distance between two starts, counted in samples: >= kMinFadeGapMs (50)
//   switch.hover.final_slot         after the lane rests, the audio runs the last requested Mode (latest wins): 1
//   switch.hover.nonfinite          0
// Extra pairs (K2 #3): owned by clean: an stmode flip within Clean (switch.stmode.st-ms, .ms-st: ST <-> M/S, a kernel
// key change with the lane-domain max-merge, K2 #3d); owned by fet-76: FET 76 <-> Clean at mix 0.5
// (switch.mix05.<a>-<b>) and an auto-makeup Mode <-> a manual one (switch.automakeup.<a>-<b>: Clean with AUTO on,
// FET 76 manual). Each with the hf_ratio_db, level_dev_db and nonfinite rows above.
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/host/Crossfade.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
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
    constexpr std::size_t kEdge = 48000;                // t = 1 s
    constexpr std::size_t kWin = 240;                   // 5 ms
    constexpr double kToneHz = 110.0;
    constexpr double kToneAmp = 0.5011872336272722;     // -6 dBFS
    constexpr float kSlope4 = 0.75f;                    // ratio 4:1 as the stored slope S = 1 - 1/R

    // The lookahead budget of a pair: 5 ms when either Mode wants lookahead (Brickwall: its attack IS the lookahead
    // window, so with the budget OFF it limits instantaneously and overshoots, 02 §6.6's footer hint), else OFF.
    LookaheadBudget budgetFor(const ModeEntry& a, const ModeEntry& b)
    {
        return a.desc->wantsLookahead || b.desc->wantsLookahead ? LookaheadBudget::ms5 : LookaheadBudget::off;
    }

    HostConfig config(LookaheadBudget budget)
    {
        HostConfig c;
        c.fs = kFs;
        c.maxBlock = kBlock;
        c.quality = Quality::std;                       // the plugin's default setup
        c.budget = budget;
        return c;
    }

    // The raw state of a switch from `from` under `budget`: its defaults, threshold -20 dB, ratio 4:1.
    RawParams switchRaw(const ModeEntry& from, LookaheadBudget budget = LookaheadBudget::off)
    {
        RawParams raw = fcmp::probe::modeRaw(from, budget);
        raw[Pid::thr] = -20.0f;
        raw[Pid::ratio] = kSlope4;
        return raw;
    }

    // The block parameters of Mode `en` on raw state `raw` (the mode write: only the slot changes).
    BlockParams blockOf(const ModeEntry& en, RawParams raw)
    {
        raw.modeSlot = static_cast<std::uint8_t>(slotOf(en));
        BlockParams bp;
        bp.slot = raw.modeSlot;
        bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
        return bp;
    }

    struct Run
    {
        std::vector<float> l, r;
        std::vector<std::size_t> blockAt;               // first sample of each process() call
        std::vector<UiFrame> frames;                    // the UiFrame after each call
        int latencyFirst = 0, latencyLast = 0;
        std::int64_t nonfinite = 0;
    };

    using ParamsAt = std::function<BlockParams(std::size_t off)>;

    // A fresh host (attached) over the tone in blocks of `bs`, split at every sample in `splits`.
    Run render(const ParamsAt& at, std::size_t n, int bs, const std::vector<std::size_t>& splits,
               LookaheadBudget budget)
    {
        std::vector<float> x(n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = sig::sineAt(static_cast<std::int64_t>(i), kToneHz, kFs, kToneAmp);
        auto host = std::make_unique<EngineHost>();
        host->configure(config(budget), at(0));
        host->setUiAttached(true);
        Run run;
        run.l.assign(n, 0.0f);
        run.r.assign(n, 0.0f);
        run.latencyFirst = host->latencySamples();
        for (std::size_t off = 0; off < n;)
        {
            std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(bs), n - off);
            for (const std::size_t s : splits)
                if (s > off && s < off + len)
                    len = s - off;
            const float* ins[2] = { x.data() + off, x.data() + off };
            float* outs[2] = { run.l.data() + off, run.r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, at(off));
            UiFrame f{};
            (void) host->readUiFrame(f);
            run.blockAt.push_back(off);
            run.frames.push_back(f);
            off += len;
        }
        run.latencyLast = host->latencySamples();
        host->setUiAttached(false);
        for (std::size_t i = 0; i < n; ++i)
            run.nonfinite += (std::isfinite(run.l[i]) ? 0 : 1) + (std::isfinite(run.r[i]) ? 0 : 1);
        return run;
    }

    Run renderHeld(const BlockParams& bp, std::size_t n, LookaheadBudget budget)
    {
        return render([&](std::size_t) { return bp; }, n, kBlock, {}, budget);
    }

    double rmsDb(const std::vector<float>& x, std::size_t from, std::size_t len)
    {
        double e = 0.0;
        for (std::size_t i = from; i < from + len && i < x.size(); ++i)
            e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return 10.0 * std::log10(std::max(e / static_cast<double>(len), 1e-30));
    }

    // How far T leaves [min(A, B) - carried, max(A, B)] in 5 ms RMS windows from e - 20 ms to e + 100 ms, dB (0
    // inside); `carried` = the over-compression the hand-over seeds (file comment, level_dev_db).
    double levelDevDb(const Run& t, const Run& a, const Run& b, std::size_t e, double carried)
    {
        double worst = 0.0;
        for (std::size_t s = e - 4 * kWin; s + kWin <= e + 20 * kWin; s += kWin)
        {
            const double lt = rmsDb(t.l, s, kWin), la = rmsDb(a.l, s, kWin), lb = rmsDb(b.l, s, kWin);
            worst = std::max({ worst, lt - std::max(la, lb), std::min(la, lb) - carried - lt });
        }
        return worst;
    }

    // The applied GR (the louder lane) at the end of the block that ends at sample `end`.
    double grAt(const Run& r, std::size_t end)
    {
        for (std::size_t i = 0; i + 1 < r.blockAt.size(); ++i)
            if (r.blockAt[i + 1] == end)
                return std::max(static_cast<double>(r.frames[i].appliedGrDb[0]),
                                static_cast<double>(r.frames[i].appliedGrDb[1]));
        return 0.0;
    }

    // The lag of T against the input tone, in samples, from the phase of the 110 Hz fundamental over 0.1 s at `from`.
    double lagSamples(const std::vector<float>& y, std::size_t from)
    {
        constexpr std::size_t kLen = 4800;              // 11 whole cycles of 110 Hz at 48 kHz
        const measure::SingleBin bin(kToneHz, kFs, kLen);
        const measure::Bin yb = bin(std::span<const float>(y.data() + from, kLen), static_cast<std::int64_t>(from));
        std::vector<float> x(kLen);
        for (std::size_t i = 0; i < kLen; ++i)
            x[i] = sig::sineAt(static_cast<std::int64_t>(from + i), kToneHz, kFs, kToneAmp);
        const measure::Bin xb = bin(x, static_cast<std::int64_t>(from));
        double d = std::atan2(xb.im, xb.re) - std::atan2(yb.im, yb.re);
        const double twoPi = 6.283185307179586;
        while (d < 0.0)
            d += twoPi;
        while (d >= twoPi)
            d -= twoPi;
        return d / (twoPi * kToneHz / kFs);
    }

    // The output time (ms after e) from which T nulls against B at <= -60 dB in every 5 ms window; the render's end
    // when it never does.
    double settleMs(const Run& t, const Run& b, std::size_t e)
    {
        const std::size_t n = t.l.size();
        std::size_t settledFrom = n;
        for (std::size_t s = e; s + kWin <= n; s += kWin)
        {
            double eDiff = 0.0, eRef = 0.0;
            for (std::size_t i = s; i < s + kWin; ++i)
            {
                const double d = static_cast<double>(t.l[i]) - b.l[i];
                eDiff += d * d;
                eRef += static_cast<double>(b.l[i]) * b.l[i];
            }
            const bool ok = eDiff <= 1e-6 * std::max(eRef, 1e-30);
            if (!ok)
                settledFrom = n;
            else if (settledFrom == n)
                settledFrom = s;
        }
        return 1000.0 * static_cast<double>(settledFrom - e) / kFs;
    }

    bool anyFading(const Run& r)
    {
        for (const UiFrame& f : r.frames)
            if ((f.flags & kUiFading) != 0)
                return true;
        return false;
    }

    struct PairRows
    {
        bool latency = false, settle = false, faded = false;
    };

    // T switches from `a` to `b` at kEdge; the spec rows of one ordered pair under key prefix k.
    void pairRows(Probe& P, const std::string& k, const BlockParams& a, const BlockParams& b, PairRows what,
                  LookaheadBudget budget)
    {
        const std::size_t n = static_cast<std::size_t>(2.0f * kFs);
        const auto held = [](const BlockParams& p) { return [p](std::size_t) { return p; }; };
        const Run ca = render(held(a), n, kBlock, { kEdge }, budget);
        const Run cb = render(held(b), n, kBlock, { kEdge }, budget);
        const Run t = render([&](std::size_t off) { return off < kEdge ? a : b; }, n, kBlock, { kEdge }, budget);
        const std::size_t e = kEdge + static_cast<std::size_t>(t.latencyFirst);
        const double hf = measure::hfRatioDb(t.l, ca.l, cb.l, e, kFs);
        const double carried = std::max(0.0, grAt(ca, kEdge) - grAt(cb, kEdge));
        const double dev = levelDevDb(t, ca, cb, e, carried);
        std::printf("NOTE     %s: level %.3f -> %.3f dB (CA, CB at the edge); GR %.3f -> %.3f dB; hf %.3f dB, "
                    "level_dev %.3f dB (carried %.3f dB)\n",
                    k.c_str(), rmsDb(ca.l, e - kWin, kWin), rmsDb(cb.l, e + 20 * kWin, kWin), grAt(ca, kEdge),
                    grAt(cb, kEdge), hf, dev, carried);
        P.le(k + ".hf_ratio_db", hf, fcmp::probe::tol::kClickHfRatioDb);
        P.le(k + ".level_dev_db", dev, 1.0);
        P.eq(k + ".nonfinite", t.nonfinite, 0);
        if (what.latency)
        {
            const double before = lagSamples(t.l, kEdge - 4800), after = lagSamples(t.l, e + 9600);
            const double beforeA = lagSamples(ca.l, kEdge - 4800), afterB = lagSamples(cb.l, e + 9600);
            std::printf("NOTE     %s: lag %.4f -> %.4f samples (controls %.4f, %.4f); reported latency %d -> %d\n",
                        k.c_str(), before, after, beforeA, afterB, t.latencyFirst, t.latencyLast);
            // Two Modes may place the fundamental differently (a limiter's waveform-shaped GR: Brickwall 0.6 samples
            // from Bus G here), so each side is compared with its own never-switched control.
            const bool constant = t.latencyFirst == t.latencyLast && std::fabs(before - beforeA) <= 0.5
                               && std::fabs(after - afterB) <= 0.5;
            P.eq(k + ".latency_constant", constant ? 1 : 0, 1);
        }
        if (what.faded)
            P.eq(k + ".faded", anyFading(t) ? 1 : 0, 1);
        if (what.settle)
        {
            const double s = settleMs(t, cb, e);
            std::printf("NOTE     %s: settles against CB in %.4g ms\n", k.c_str(), s);
            P.num(k + ".settle_ms", s, Tol::rel(0.1));
        }
    }

    std::int64_t rawDifferences(const RawParams& a, const RawParams& b)
    {
        std::int64_t d = 0;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            d += std::bit_cast<std::uint32_t>(a.v[i]) == std::bit_cast<std::uint32_t>(b.v[i]) ? 0 : 1;
        d += a.modeSlot == b.modeSlot ? 0 : 1;
        d += a.budget == b.budget ? 0 : 1;
        return d;
    }

    bool sameBits(const EngineParams& a, const EngineParams& b)
    {
        return std::memcmp(&a, &b, sizeof(EngineParams)) == 0;
    }
} // namespace

FCMP_PROBE(dsp, switch)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const int slot = slotOf(en);
    const std::span<const ModeSlot> slots = modeSlots();

    // ---- every pair with a lower-slot Mode, both directions ---------------------------------------------------------
    for (const ModeSlot& ms : slots)
    {
        if (ms.entry == nullptr || static_cast<int>(ms.slot) >= slot)
            continue;
        const ModeEntry& lower = *ms.entry;
        for (const bool fromThis : { true, false })
        {
            const ModeEntry& a = fromThis ? en : lower;
            const ModeEntry& b = fromThis ? lower : en;
            const LookaheadBudget budget = budgetFor(a, b);
            const RawParams raw = switchRaw(a, budget);
            const std::string k = "switch." + std::string(a.desc->key) + "-" + std::string(b.desc->key);
            pairRows(P, k, blockOf(a, raw), blockOf(b, raw), PairRows{ true, true, true }, budget);
        }
    }

    // ---- A -> B -> A: the raw state is truth, the switch writes only `mode` ----------------------------------------
    const ModeEntry* other = nullptr;
    for (const ModeSlot& ms : slots)
        if (ms.entry != nullptr && ms.entry != &en)
        {
            other = ms.entry;
            break;
        }
    if (other != nullptr)
    {
        const ModeEntry& b = *other;
        const std::string k = "switch.aba." + std::string(b.desc->key);
        const LookaheadBudget budget = budgetFor(en, b);
        RawParams raw = switchRaw(en, budget);
        raw.modeSlot = static_cast<std::uint8_t>(slot);
        const RawParams before = raw;
        const EngineParams engBefore = fcmp::probe::resolveRaw(en, before).eng;
        RawParams inB = raw;
        inB.modeSlot = static_cast<std::uint8_t>(slotOf(b));                  // the mode write
        P.eq(k + ".param_writes", rawDifferences(before, inB), fcmp::probe::tol::kModeSwitchParamWrites);
        RawParams after = inB;
        after.modeSlot = static_cast<std::uint8_t>(slot);                     // and back
        P.eq(k + ".raw_bitexact", rawDifferences(before, after) == 0 ? 1 : 0, 1);
        P.eq(k + ".eng_bitexact", sameBits(engBefore, fcmp::probe::resolveRaw(en, after).eng) ? 1 : 0, 1);

        const BlockParams pa = blockOf(en, before), pb = blockOf(b, inB), pa2 = blockOf(en, after);
        const std::size_t n = static_cast<std::size_t>(2.5f * kFs), back = kEdge + static_cast<std::size_t>(0.5f * kFs);
        const Run ca = renderHeld(pa, n, budget), cb = renderHeld(pb, n, budget);
        const Run t = render([&](std::size_t off) { return off < kEdge ? pa : (off < back ? pb : pa2); }, n, kBlock,
                             { kEdge, back }, budget);
        const auto lat = static_cast<std::size_t>(t.latencyFirst);
        P.le(k + ".ab.hf_ratio_db", measure::hfRatioDb(t.l, ca.l, cb.l, kEdge + lat, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        P.le(k + ".ba.hf_ratio_db", measure::hfRatioDb(t.l, cb.l, ca.l, back + lat, kFs),
             fcmp::probe::tol::kClickHfRatioDb);
        P.eq(k + ".nonfinite", t.nonfinite, 0);
        const std::size_t tail = n - static_cast<std::size_t>(0.1f * kFs);
        double eDiff = 0.0, eRef = 0.0;
        for (std::size_t i = tail; i < n; ++i)
        {
            const double d = static_cast<double>(t.l[i]) - ca.l[i];
            eDiff += d * d;
            eRef += static_cast<double>(ca.l[i]) * ca.l[i];
        }
        std::printf("NOTE     %s: 1 s after B -> A the output is %.4g dB from CA (last 100 ms)\n", k.c_str(),
                    eDiff <= 0.0 ? -400.0 : 10.0 * std::log10(eDiff / std::max(eRef, 1e-30)));

        // ---- the hovering automation lane (K2 #22) ------------------------------------------------------------------
        const std::size_t hn = static_cast<std::size_t>(1.5f * kFs), h0 = static_cast<std::size_t>(0.5f * kFs),
                          h1 = static_cast<std::size_t>(1.0f * kFs);
        const auto lane = [&](std::size_t off) {
            if (off < h0)
                return pa;
            const std::size_t blocks = (std::min(off, h1 - 64) - h0) / 64;
            return blocks % 2 == 0 ? pb : pa;
        };
        const Run h = render(lane, hn, 64, {}, budget);
        std::vector<std::size_t> starts;
        bool prev = false;
        for (std::size_t i = 0; i < h.frames.size(); ++i)
        {
            const bool fading = (h.frames[i].flags & kUiFading) != 0;
            if (fading && !prev)
                starts.push_back(h.blockAt[i]);
            prev = fading;
        }
        std::size_t gap = hn;
        for (std::size_t i = 1; i < starts.size(); ++i)
            gap = std::min(gap, starts[i] - starts[i - 1]);
        const BlockParams last = lane(h1);
        const UiFrame& fin = h.frames.back();
        std::printf("NOTE     switch.hover: %zu crossfade starts, smallest gap %zu samples; final slot %d "
                    "(wanted %d)\n",
                    starts.size(), gap, static_cast<int>(fin.modeSlot), static_cast<int>(last.slot));
        P.ge("switch.hover.starts", static_cast<double>(starts.size()), 2.0);
        P.ge("switch.hover.min_gap_ms", 1000.0 * static_cast<double>(gap) / kFs, static_cast<double>(kMinFadeGapMs));
        P.eq("switch.hover.final_slot",
             fin.modeSlot == last.slot && fin.fadeFromSlot == last.slot && (fin.flags & kUiFading) == 0 ? 1 : 0, 1);
        P.eq("switch.hover.nonfinite", h.nonfinite, 0);
    }

    // ---- extra pairs (K2 #3) ----------------------------------------------------------------------------------------
    const std::string key(en.desc->key);
    if (key == "clean")
    {
        RawParams st = switchRaw(en), ms = st;
        st[Pid::stmode] = 0.0f;
        ms[Pid::stmode] = 1.0f;
        const BlockParams pst = blockOf(en, st), pms = blockOf(en, ms);
        if (pst.eng.stmode != pms.eng.stmode)
        {
            pairRows(P, "switch.stmode.st-ms", pst, pms, PairRows{ false, false, true }, LookaheadBudget::off);
            pairRows(P, "switch.stmode.ms-st", pms, pst, PairRows{ false, false, true }, LookaheadBudget::off);
        }
        else
            std::printf("NOTE     switch.stmode: Clean resolves ST and M/S to one stereo mode; skipped\n");
    }
    if (key == "fet-76")
    {
        const ModeEntry* clean = byKey("clean");
        if (clean != nullptr)
            for (const bool fromFet : { true, false })
            {
                const ModeEntry& a = fromFet ? en : *clean;
                const ModeEntry& b = fromFet ? *clean : en;
                const std::string pair = std::string(a.desc->key) + "-" + std::string(b.desc->key);
                RawParams mix = switchRaw(a);
                mix[Pid::mix] = 0.5f;
                pairRows(P, "switch.mix05." + pair, blockOf(a, mix), blockOf(b, mix), PairRows{ false, false, true },
                         LookaheadBudget::off);

                RawParams am = switchRaw(a);
                am[Pid::automu] = 1.0f;
                const BlockParams pa = blockOf(a, am), pb = blockOf(b, am);
                std::printf("NOTE     switch.automakeup.%s: auto makeup %s -> %s\n", pair.c_str(),
                            (pa.eng.flags & kEngAutoMakeup) != 0 ? "on" : "off",
                            (pb.eng.flags & kEngAutoMakeup) != 0 ? "on" : "off");
                pairRows(P, "switch.automakeup." + pair, pa, pb, PairRows{ false, false, true }, LookaheadBudget::off);
            }
    }

    return P.finish();
}
