// EngineHost.cpp: the Mode-agnostic engine host (01 §5.4-5.8, §6; E §3.1, §5, §7; K2 #2, #3, #8, #11-#13, #18, #21a,
// #22; ADR-14, ADR-16, ADR-17). Orchestration only: the work lives in the pure components under engine/host/
// (ScFilter.h, Router.h, Delay.h: F5; Ramps.h, TelemetryAccum.h: F4; Crossfade.h: F7), in engine/Oversampler and in
// the Mode engines behind IEngine (one virtual call per chunk).
//
// ==== Scope: the complete host (F4 at ECO in S3, F7 in S6) ==========================================================
// Every block (01 §5.4):
//   0. ScopedFtz; the unconfigured guard (input copied to output); main and key sanitised into host scratch
//      (core/Sanitize.h) before any delay line, filter or meter reads them (K2 #13).
//   1. Block start: the snap flag (acquire), the defensive look clamp, the block's KernelKey {slot, topo, det, stmode,
//      voice, keyExt} (keyExt = extKey AND an active key bus, K2 #22). A key that differs from the incoming path's
//      starts a crossfade (01 §5.5) when host::FadeClock allows it (no fade running, >= kMinFadeGapMs since the last
//      start, counted in samples); otherwise the request is latched: it is re-evaluated at every block start, so the
//      latest one wins. The path whose key equals the block's takes the block's EngineParams; any other path keeps its
//      EngineParams frozen (the outgoing path of a fade, and the incoming path while a request is latched: another
//      kernel's parameters are never fed to an engine, K2 #3a). Per-path targets: gain {preGainDb, makeupDb +
//      autoMakeupDb()} (K2 #3b), SC filters, SC delay. Host targets: mix (the incoming path's value), bypass, listen,
//      delta.
//   2. Per chunk of at most kChunk base samples, the chunks aligned to ABSOLUTE multiples of kChunk (a block boundary
//      splits a chunk, a chunk never straddles a multiple of kChunk), so everything sampled per chunk is block-size
//      invariant:
//        a. route: a mono input is duplicated; the SC source of a path is the key when that path's keyExt is set (and
//           a key is delivered), else the main input (Router selectSc/scLanes: lanes {L, R, L, R});
//        b. SC filters per path (ScFilter: HPF + tilt; exact bypass at OFF / 0 dB/oct);
//        c. lookahead: the main input delayed by L_la; each path's filtered SC delayed by
//           L_la - look + D_up(quality) - engine.scDelaySamples(), floored at 0 (K2 #11b, #21a; Delay.h slews the read
//           position one sample per control tick, so moving `look` never changes latency and never jumps); the input
//           delayed by the full latency for bypass and the poison fallback; the filtered SC delayed by the latency for
//           SC listen;
//        d. ONE up() of the delayed main: osMain, which is also the dry signal and is never pre-gained (K2 #3c, #3e);
//        e. per path (1, or 2 while fading): the gain smoothers per base sample; the SC x linFromDb(preGainDb) when
//           internal (a key is never pre-gained), encodeSc per the path's stmode; control(); maskGr (the passing
//           channel of MID/SIDE/M>S/S>M at 0 dB); osMain encoded at the OS rate; the gain in dB, preGainDb - GR,
//           interpolated linearly to the OS rate (K2 #11c, below) and applied as linFromDb; colour() at the OS rate
//           with the interpolated GR; makeup (interpolated likewise); the residual decode against osMain;
//        f. blend: wet = (1 - w) wetOut + w wetIn, w the FadeClock weight per OS sample (20 ms, equal gain, shaped);
//        g. mix inside the OS domain against osMain: y = (1 - mix) osMain + mix wet (E §5.3); delta: y = mix (osMain -
//           sum_p w_p wet_p linFromDb(-preGainDb_p - makeupTotalDb_p)), faded in and out by the delta ramp;
//        h. ONE down() (the STD Thiran section included, 01 §5.6), then the OUTPUT trim (v1.2, ADR-88): the linear gain
//           of BlockParams::outputDb through the host's two-stage smoother, per base sample, on the processed signal
//           only (the mix and delta alike; SC listen and bypass below replace it, untrimmed). A resting 0 dB touches
//           nothing, so every output at OUTPUT 0 dB is bit-identical to the host without it;
//        i. poison check (01 §5.8), then SC listen (the incoming path's filtered SC, delayed by the latency, decoded
//           as the engine hears it, unity gain) and bypass (the input delayed by the latency), each a 20 ms ramp;
//        j. the tap (incoming path) and, while attached, telemetry accumulation.
//   3. While attached: the UiFrame, published once per process() call (the columns are pushed as they complete).
//
// Documented choices where 01 is silent or ambiguous (see also the F7 handoff):
//   - OS-rate gain interpolation (K2 #11c: "OS sample F n + k takes gr[n-1] + (k/F)(gr[n] - gr[n-1])"). k counts the
//     OS samples of base sample n from 1 to F, so the last of them takes gr[n] itself and ECO (F = 1) applies gr[n] to
//     sample n exactly as F4 and the Rig do. With D_up in the SC delay the gain then trails the audio it acts on by
//     0 (ECO), 0.66 (STD: up-stage delay 1.84, D_up 2) and 0.5 (HQ: 33.25, 33) base samples, well inside dsp.time's
//     1.5-sample Quality row; k = 0...F-1 would add a full sample at ECO and break ECO's identity with the Rig.
//     preGainDb - GR, the GR alone (colour's grDbOs) and makeupTotalDb are all interpolated this way; mix too.
//   - Smoothing (F4 finding, kept: S3 lead revision 2): each host one-pole of 01 §5.1 (PathState::gain, the mix) is
//     followed by a second identical Smoother4 stage (a critically damped two-pole), so the applied gain starts moving
//     with zero slope. Both stages land exactly. Stage 1 is PathState::gain (01 §5.5); stage 2 is Impl::gainOut[slot]
//     and Impl::mixOut.
//   - Per-path side chain. Each path has its own SC filter and SC delay line (its own look, schpf, sce and
//     scDelaySamples, frozen with its EngineParams), fed from its own source (keyExt is part of the key). While no fade
//     runs, the idle delay line is fed the incoming path's filtered SC, so a new path starts with the same history;
//     at a start the new path's filter state is copied from the incoming one (same history, then its own targets).
//     A path with keyExt whose key bus vanished reads the main input (the key is gone; it fades out in 20 ms).
//   - The tap carries the engine's own ControlIo outputs (unmasked); telemetry carries the masked GR ("what multiplies
//     the audio"). Listen and bypass run after the poison check; bypass wins over listen.
//   - Poison is checked per chunk (the chunk's whole output, every running engine's finite(), the SC filters, the
//     host smoothers). On failure that chunk outputs the latency-aligned sanitised dry, a running fade is abandoned
//     (the incoming path stays), the engine, the SC filters and the oversampler are reset, and the next block starts
//     snapped (its smoothers jump to the new targets, so a non-finite target cannot survive the reset); kUiPoisonReset
//     is set for the block, as it is when sanitize() replaced or clamped a sample. The delay lines hold sanitised input
//     only and are not cleared. Recovery is therefore at most one block.
//   - requestSnap() (state recall) jumps the running engines' smoothers, both paths' gains, the mix, the SC filters,
//     the SC delays and the bypass/listen/delta ramps to their targets at the next block start. A Mode change in the
//     same block still crossfades (E §4.6), from a snapped new engine. reset() abandons a fade, resets the engine, the
//     SC filters, the oversampler and every delay line (silence) and snaps the host smoothers and ramps; the host's
//     sample index never goes back, so control ticks, delay slews and history columns stay on the absolute grid.
//   - Scratch: 4 x nextPow2(L_la,max + kChunk) floats per arena slot with L_la,max = 20 ms at the configured rate
//     (01 §5.3), whatever budget is configured.
//   - UiFrame: kUiFading when a crossfade ran during the block, fadeProgress its linear position at the block end (1
//     when none), fadeFromSlot the outgoing slot; kUiLookahead when a lookahead budget is configured (L_la > 0);
//     kUiMidSide when the incoming engine's lanes are M/S; kUiBypassed/kUiDelta/kUiListen the block's targets (the
//     latch truth; bypassAmt carries the ramp). preGainDb, makeupEffDb and mix are the host's smoothed (stage-2)
//     values of the incoming path; the engine-internal words are its resolved values. Non-finite engine telemetry and
//     internals are published as 0.
//   - The history internal (the Mode's InternalSpec with history == true) is latched from the incoming engine at every
//     absolute multiple of kChunk (before that chunk's control()), so HistoryColumn::internal0 is block-size
//     invariant; on an attach transition it is latched at once. Columns cut while a fade runs carry b6 (kColFading).
//   - UiFrame::publishCount survives a reconfigure (the editor's staleness check sees it keep moving).
//   - Telemetry cost (S13 H1b, lead revision 5d; fcmp_bench attached vs detached, one A/B run). The common attached
//     cost is ~5 ns/sample/ch (Clean STD): TelemetryAccum::accumulate ~3.3, the colour-input peak ~0.7, the detail
//     outputs of control() and the per-chunk internals latch the rest. accumulate keeps its accumulators in locals
//     (TelemetryAccum.h) and the colour-input meter gets each chunk's peak at colIn[c][0] (the meter reads only the
//     block's peak; colIn[c][1...] stay 0), 4 wide: both bit-identical. An FB kernel's static target (ControlIo::tgtDb,
//     the FB curve solved at every sample by Newton) was most of Opto 2A's attached cost and a third of Mu 67's; it is
//     cut where that is exact: FeedbackZdf stops at a Newton fixed point and ModeEngine reuses the last solve while x
//     and the level controls are the same bits (both bit-identical). It stays a per-sample value: HistoryColumn::
//     tgtMaxDb is the max over the column's samples (01 §6.3, ui.truth holds it to 0.1 dB against the tap), and a
//     target evaluated only on a sample grid misses a periodic detector's peaks whenever they fall off the grid (a
//     1 kHz tone repeats every 48 samples, so its peaks land on the same residues every period: ui.truth.diode-609
//     read 0.17 dB low at every grid stride tried, 4 to 16).

#include "fcdsp/engine/EngineHost.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Sanitize.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Smoother.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/engine/host/Crossfade.h"
#include "fcdsp/engine/host/Delay.h"
#include "fcdsp/engine/host/Ramps.h"
#include "fcdsp/engine/host/Router.h"
#include "fcdsp/engine/host/ScFilter.h"
#include "fcdsp/engine/host/TelemetryAccum.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace fcdsp {

namespace {

constexpr float kGainSmoothMs = 20.0f;                  // PathState::gain and the host mix (01 §5.1)
constexpr int kMaxOsFactor = 4;
constexpr int kOsChunk = kChunk * kMaxOsFactor;         // OS samples of one chunk at the largest factor
constexpr LookaheadBudget kMaxBudget = LookaheadBudget::ms20;   // L_la,max for the engine scratch (01 §5.3)

static_assert(kOs[0].factor == 1 && kOs[1].factor <= kMaxOsFactor && kOs[2].factor <= kMaxOsFactor);
// kChunk is a multiple of 4, so the 4-wide passes below may round a chunk up and stay inside the buffers.
static_assert(kChunk % 4 == 0);

inline int roundUp4(int n) noexcept FCDSP_NONBLOCKING { return (n + 3) & ~3; }

inline bool finiteF(float v) noexcept FCDSP_NONBLOCKING
{
    return (std::bit_cast<uint32_t>(v) & 0x7f800000u) != 0x7f800000u;
}

inline float finiteOr0(float v) noexcept FCDSP_NONBLOCKING { return finiteF(v) ? v : 0.0f; }

// BlockParams::outputDb as a linear gain (ADR-88): clamped to the host range (-24 ... +24 dB), a non-finite value is
// 0 dB, and 0 dB is exactly 1 (the resting trim then touches nothing).
inline float outputGain(float db) noexcept FCDSP_NONBLOCKING
{
    if (!finiteF(db) || db == 0.0f)
        return 1.0f;
    const float d = db < -24.0f ? -24.0f : (db > 24.0f ? 24.0f : db);
    return fcdsp::exp2(d * kLog2PerDb);
}

inline bool finiteLanes(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
{
    return finiteF(simd::lane<0>(v)) && finiteF(simd::lane<1>(v)) && finiteF(simd::lane<2>(v))
        && finiteF(simd::lane<3>(v));
}

inline simd::f32x4 vec4(float a, float b, float c, float d) noexcept FCDSP_NONBLOCKING
{
    alignas(16) const float v[4] = { a, b, c, d };
    return simd::load(v);
}

// lin[i] = linFromDb(db[i]) for i < roundUp4(n) (the buffers are sized for it).
inline void linFromDbArray(const float* db, float* lin, int n) noexcept FCDSP_NONBLOCKING
{
    for (int i = 0; i < n; i += 4)
        simd::store(lin + i, linFromDb(simd::load(db + i)));
}

// True when every sample of x[0..n) is finite: x - x is 0 for a finite x and NaN otherwise, so the sum is 0 exactly
// when all are finite. Exactly n samples are read: the lanes a 4-wide pass computes past n are never looked at.
inline bool allFinite(const float* x, int n) noexcept FCDSP_NONBLOCKING
{
    simd::f32x4 acc = simd::set1(0.0f);
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const simd::f32x4 v = simd::load(x + i);
        acc = simd::add(acc, simd::sub(v, v));
    }
    bool ok = finiteLanes(acc);
    for (; i < n; ++i)
        ok = ok && finiteF(x[i]);
    return ok;
}

inline int nextPow2(int v) noexcept
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

inline int qualityIndex(Quality q) noexcept FCDSP_NONBLOCKING
{
    const int i = static_cast<int>(q);
    return i < 0 ? 0 : (i > 2 ? 2 : i);
}

// The history-flagged internal of a Mode (<= 1 by the registry lint), or -1.
inline int historyInternal(const ModeEntry& e) noexcept FCDSP_NONBLOCKING
{
    if (e.desc == nullptr)
        return -1;
    const std::span<const InternalSpec> in = e.desc->internals;
    for (std::size_t i = 0; i < in.size() && i < static_cast<std::size_t>(kInternals); ++i)
        if (in[i].history)
            return static_cast<int>(i);
    return -1;
}

inline simd::f32x4 gainTarget(const EngineParams& e, const IEngine& engine) noexcept FCDSP_NONBLOCKING
{
    return vec4(e.preGainDb, e.makeupDb + engine.autoMakeupDb(), 0.0f, 0.0f);
}

inline EngineParams clampLook(EngineParams e, LookaheadBudget budget) noexcept FCDSP_NONBLOCKING
{
    const float b = budgetMs(budget);
    if (!(e.lookMs <= b))                               // NaN clamps too
        e.lookMs = b;
    return e;
}

// `look` in base samples, rounded to nearest, within [0, laSamples] (NaN reads as 0).
inline int lookSamples(float lookMs, float fs, int laSamples) noexcept FCDSP_NONBLOCKING
{
    const float s = lookMs * fs * 0.001f + 0.5f;
    if (!(s >= 1.0f))
        return 0;
    return s < static_cast<float>(laSamples) ? static_cast<int>(s) : laSamples;
}

// os[F i + j] = prev + ((j + 1) / F) (base[i] - prev), prev = base[i - 1] (for i = 0: `last`, the previous chunk's
// final value); the last OS sample of base sample i is base[i] itself; F == 1 copies. `last` becomes base[n - 1].
// Returns true when the chunk is flat (every base value equals the carried one): every OS value is then that value, so
// a caller may apply it as one scalar, bit for bit the same as the per-sample array.
inline bool toOsRate(const float* base, int n, int factor, float& last, float* os) noexcept FCDSP_NONBLOCKING
{
    bool flat = true;
    if (factor == 1)
    {
        for (int i = 0; i < n; ++i)
        {
            os[i] = base[i];
            flat = flat && base[i] == last;
        }
    }
    else
    {
        const float step = 1.0f / static_cast<float>(factor);
        float prev = last;
        for (int i = 0; i < n; ++i)
        {
            const float cur = base[i], d = cur - prev;
            flat = flat && d == 0.0f;
            float* o = os + factor * i;
            for (int j = 0; j + 1 < factor; ++j)
                o[j] = prev + static_cast<float>(j + 1) * step * d;
            o[factor - 1] = cur;
            prev = cur;
        }
    }
    if (n > 0)
        last = base[n - 1];
    return flat;
}

// w[k] *= g[k] for k < n4 (4-wide), or w[k] *= g0 when `flat`.
inline void applyGain(float* w, const float* g, float g0, bool flat, int n4) noexcept FCDSP_NONBLOCKING
{
    if (flat)
    {
        const simd::f32x4 gv = simd::set1(g0);
        for (int k = 0; k < n4; k += 4)
            simd::store(w + k, simd::mul(simd::load(w + k), gv));
        return;
    }
    for (int k = 0; k < n4; k += 4)
        simd::store(w + k, simd::mul(simd::load(w + k), simd::load(g + k)));
}

// Copies chunk samples [first, first + n) into the tap's non-empty spans (element e = absolute sample
// firstSample + e) and advances `written` up to the smallest capacity.
void writeTap(TestTap& t, uint64_t first, int n, const simd::f32x4* gr, const simd::f32x4* det,
              const simd::f32x4* tgt, const simd::f32x4* s2, const uint8_t* bits) noexcept FCDSP_NONBLOCKING
{
    const uint64_t end = first + static_cast<uint64_t>(n);
    if (end <= t.firstSample)
        return;
    const uint64_t begin = first > t.firstSample ? first : t.firstSample;
    uint64_t cap = ~uint64_t{ 0 };
    const auto put = [&](std::span<simd::f32x4> dst, const simd::f32x4* src) noexcept FCDSP_NONBLOCKING {
        if (dst.empty())
            return;
        cap = dst.size() < cap ? dst.size() : cap;
        for (uint64_t k = begin; k < end; ++k)
        {
            const uint64_t e = k - t.firstSample;
            if (e >= dst.size())
                break;
            dst[e] = src[k - first];
        }
    };
    put(t.grDb, gr);
    put(t.detDb, det);
    put(t.tgtDb, tgt);
    put(t.s2GrDb, s2);
    if (!t.bits.empty())
    {
        cap = t.bits.size() < cap ? t.bits.size() : cap;
        for (uint64_t k = begin; k < end && k - t.firstSample < t.bits.size(); ++k)
            t.bits[k - t.firstSample] = bits[k - first];
    }
    if (cap == ~uint64_t{ 0 })
        return;                                         // nothing tapped
    const uint64_t reached = end - t.firstSample;
    const uint64_t w = reached < cap ? reached : cap;
    if (w > t.written)
        t.written = w;
}

} // namespace

// ==== Impl: everything the audio path owns; configure() allocates it ================================================

struct EngineHost::Impl {
    using Buf = std::array<float, kChunk>;
    using OsBuf = std::array<float, kOsChunk>;
    using VBuf = std::array<simd::f32x4, kChunk>;

    // Per path, beside its PathState: the stereo route (fixed by its stmode, part of its key) and the OS-rate
    // interpolation state (the previous base sample's values).
    struct PathRt {
        host::Route route{};
        float lastGr[2]{}, lastPre = 0.0f, lastMk = 0.0f;
        bool fresh = true;                              // no previous base sample yet: interpolate from the first
    };

    HostConfig cfg{};
    float fs = 48000.0f, fsOs = 48000.0f;
    int factor = 1;                                     // kOs[quality].factor
    int la = 0;                                         // L_la: lookahead samples of the configured budget
    int dUp = 0;                                        // kOs[quality].dUp
    int latency = 0;                                    // la + kOs[quality].latency: what process() delays by
    uint32_t publishCount = 0;

    alignas(64) std::array<std::byte, kArenaBytes> arena[2]{};
    std::vector<float> scratch[2];                      // per arena slot, host-owned (01 §5.3 "Scratch")
    PathState path[2]{};
    PathRt prt[2]{};
    Smoother4 gainOut[2]{};                             // second stage of path[i].gain (file comment: "Smoothing")
    int active = 0;                                     // the incoming path (the only one while no fade runs)
    int histInternal = -1;                              // the incoming Mode's history internal, or -1
    host::FadeClock fade{};
    Smoother4 mix{};                                    // lane 0: the incoming path's mix
    Smoother4 mixOut{};                                 // second stage of mix
    Smoother4 output{};                                 // lane 0: the OUTPUT trim's linear gain (ADR-88), stage 1
    Smoother4 outputOut{};                              // its second stage
    float lastMix = 1.0f;
    bool mixFresh = true;
    host::HostRamp bypass{}, listen{}, delta{};         // bypass/listen at the base rate, delta at the OS rate
    host::ScFilter scf[2]{};                            // per path
    host::DelayLine<simd::f32x4> scDelay[2];            // per path
    host::DelayLine<simd::f32x4> listenDelay;           // filtered SC of the incoming path, delayed by the latency
    host::DelayLine<float> mainDelay[2], dryDelay[2];   // L_la; the latency (bypass, poison fallback)
    Oversampler os{};
    host::TelemetryAccum telem{};
    uint64_t sampleIndex = 0;                           // absolute, since configure(); never goes back
    bool snapPending = false;                           // after a poison reset: the next block starts snapped
    bool wasAttached = false;
    float internal0 = 0.0f;                             // latched history internal

    // chunk buffers (value-initialised: the 4-wide passes may read a few lanes past n)
    alignas(16) VBuf scSrc{}, scFilt[2]{}, scDel[2]{}, scIn[2]{}, scLsn{}, grP[2]{}, det{}, tgt{}, s2{};
    std::array<uint8_t, kChunk> bits{};
    alignas(16) Buf dry[2]{}, key[2]{}, dly[2]{}, byp[2]{}, out[2]{}, lsn[2]{}, colIn[2]{};
    alignas(16) Buf pre[2]{}, mk[2]{}, mixv{}, lin{}, amount{}, grCh[2]{};
    alignas(16) OsBuf osMain[2]{}, wet[2][2]{}, grOs[2]{}, gdb{}, preOs[2]{}, mkOs[2]{}, linOs{}, und[2]{};
    alignas(16) OsBuf y[2]{}, mixOs{}, fadePos{}, dAmt{}, blendBuf{};

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl()
    {
        for (PathState& p : path)
            destroy(p);
    }

    static void destroy(PathState& p) noexcept FCDSP_NONBLOCKING
    {
        if (p.engine != nullptr)
            p.engine->~IEngine();                       // engines own no resources (01 §5.3)
        p.engine = nullptr;
    }

    PathState& activePath() noexcept FCDSP_NONBLOCKING { return path[active]; }
    int outgoing() const noexcept FCDSP_NONBLOCKING { return 1 - active; }

    // L_la - look + D_up - scDelaySamples(), floored at 0 (the DelayLine clamps to its capacity L_la + D_up).
    int scDelayFor(int p) const noexcept FCDSP_NONBLOCKING
    {
        const PathState& ps = path[p];
        const int scd = ps.engine != nullptr ? ps.engine->scDelaySamples() : 0;
        const int d = la - lookSamples(ps.eng.lookMs, fs, la) + dUp - scd;
        return d > 0 ? d : 0;
    }

    // 01 §5.5 step 2 (+ 3 when a predecessor runs): construct into slot `idle`, prepare, setParams, snapParams, seed
    // from `from`'s carry, the new path's gain snapped, its route, SC filter (copied from `from`) and SC delay.
    void build(int idle, const ModeEntry& entry, const KernelKey& k, const EngineParams& e, int from) noexcept
        FCDSP_NONBLOCKING
    {
        PathState& nu = path[idle];
        destroy(nu);
        std::vector<float>& scr = scratch[idle];
        nu.engine = entry.construct(arena[idle].data());
        nu.engine->prepare(PrepareInfo{ fs, factor, std::span<float>(scr.data(), scr.size()) });
        nu.engine->setParams(e);
        nu.engine->snapParams();
        if (from >= 0 && path[from].engine != nullptr)
            nu.engine->seed(path[from].engine->carry());
        nu.key = k;
        nu.eng = e;
        nu.gain.setTarget(gainTarget(e, *nu.engine));
        snapGain(idle);
        prt[idle] = PathRt{};
        prt[idle].route = host::routeOf(e.stmode);
        if (from >= 0)
            scf[idle] = scf[from];                      // the same filter history; its own targets from here
        scf[idle].setTarget(e.scHpfHz, e.sceDbOct);
        if (from < 0)
            scf[idle].snap();
        scDelay[idle].setDelay(scDelayFor(idle));
    }

    // The first engine (configure): no predecessor, no fade.
    void install(const ModeEntry& entry, const KernelKey& k, const EngineParams& e) noexcept FCDSP_NONBLOCKING
    {
        destroy(path[1 - active]);
        build(active, entry, k, e, -1);
        histInternal = historyInternal(entry);
    }

    // 01 §5.5 steps 1-3: the new engine in the idle slot, seeded from the incoming one, which becomes the outgoing
    // path; the fade starts at base-rate sample `index`.
    void startFade(const ModeEntry& entry, const KernelKey& k, const EngineParams& e, uint64_t index) noexcept
        FCDSP_NONBLOCKING
    {
        const int from = active, idle = 1 - active;
        build(idle, entry, k, e, from);
        active = idle;
        histInternal = historyInternal(entry);
        fade.start(index);
    }

    // 01 §5.5 step 5 (or an abandoned fade): the outgoing engine destroyed, the incoming path alone.
    void endFade() noexcept FCDSP_NONBLOCKING
    {
        destroy(path[outgoing()]);
        fade.finish();
    }

    // The host's two-stage smoothers (file comment "Smoothing"): stage 1 is 01 §5.1's one-pole, stage 2 follows it.
    void snapGain(int i) noexcept FCDSP_NONBLOCKING
    {
        path[i].gain.snap();
        gainOut[i].setTarget(path[i].gain.cur);
        gainOut[i].snap();
    }
    void snapMix() noexcept FCDSP_NONBLOCKING
    {
        mix.snap();
        mixOut.setTarget(mix.cur);
        mixOut.snap();
    }
    void snapOutput() noexcept FCDSP_NONBLOCKING
    {
        output.snap();
        outputOut.setTarget(output.cur);
        outputOut.snap();
    }
    // The OUTPUT trim on out[0..1][0, n) (file comment, step h): a resting gain is one multiply per sample, or nothing
    // at exactly 1; a moving one ticks both stages per base sample.
    void applyOutput(int n) noexcept FCDSP_NONBLOCKING
    {
        float* const o0 = out[0].data();
        float* const o1 = out[1].data();
        const float t = simd::lane<0>(output.tgt);
        if (simd::lane<0>(output.cur) == t && simd::lane<0>(outputOut.cur) == t)
        {
            if (t == 1.0f)
                return;
            for (int i = 0; i < n; ++i)
            {
                o0[i] *= t;
                o1[i] *= t;
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            outputOut.setTarget(output.tick());
            const float g = simd::lane<0>(outputOut.tick());
            o0[i] *= g;
            o1[i] *= g;
        }
    }
    simd::f32x4 tickGain(int i) noexcept FCDSP_NONBLOCKING
    {
        gainOut[i].setTarget(path[i].gain.tick());
        return gainOut[i].tick();
    }
    simd::f32x4 tickMix() noexcept FCDSP_NONBLOCKING
    {
        mixOut.setTarget(mix.tick());
        return mixOut.tick();
    }
    bool smoothersFinite(int i) const noexcept FCDSP_NONBLOCKING
    {
        return finiteLanes(path[i].gain.cur) && finiteLanes(gainOut[i].cur);
    }

    float latchInternal() const noexcept FCDSP_NONBLOCKING
    {
        const PathState& p = path[active];
        if (histInternal < 0 || p.engine == nullptr)
            return 0.0f;
        float o[kInternals]{};
        p.engine->internals(o);
        return finiteOr0(o[histInternal]);
    }

    // One chunk of one path (01 §5.4 step 2e): wet[p] = its decoded output at the OS rate; preOs[p] and mkOs[p] its
    // gains at the OS rate (the delta path undoes them). `detail`: the incoming path's telemetry/tap outputs; `tap`:
    // the probe tap (incoming path only; it takes the engine's own ControlIo outputs, before maskGr).
    void runPath(int p, int n, uint64_t index, bool keyDelivered, bool detail, bool keepColourIn,
                 TestTap* tap) noexcept FCDSP_NONBLOCKING
    {
        PathState& ps = path[p];
        PathRt& rt = prt[p];
        const int nOs = n * factor, nOs4 = roundUp4(nOs), n4 = roundUp4(n);

        // gains per base sample
        for (int i = 0; i < n; ++i)
        {
            const simd::f32x4 g = tickGain(p);
            const auto u = static_cast<std::size_t>(i);
            pre[p][u] = simd::lane<0>(g);
            mk[p][u] = simd::lane<1>(g);
        }

        // side chain: x linFromDb(preGainDb) when internal (a key is never pre-gained), then the stmode encode
        const bool internal = ps.key.keyExt == 0 || !keyDelivered;
        simd::f32x4* sc = scIn[p].data();
        if (internal)
        {
            linFromDbArray(pre[p].data(), lin.data(), n4);
            for (int i = 0; i < n; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                sc[i] = host::encodeSc(simd::mul(scDel[p][u], simd::set1(lin[u])), rt.route);
            }
        }
        else
            host::encodeSc(scDel[p].data(), sc, n, rt.route);

        // control
        ControlIo cio;
        cio.n = n;
        cio.sampleIndex = index;
        cio.sc = sc;
        cio.grDb = grP[p].data();
        if (detail)
        {
            cio.detDb = det.data();
            cio.tgtDb = tgt.data();
            cio.s2GrDb = s2.data();
            cio.bits = bits.data();
        }
        cio.keyExternal = ps.key.keyExt != 0;
        ps.engine->control(cio);
        if (tap != nullptr)
            writeTap(*tap, index, n, grP[p].data(), det.data(), tgt.data(), s2.data(), bits.data());
        for (int i = 0; i < n; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            grP[p][u] = host::maskGr(grP[p][u], rt.route);
            grCh[0][u] = simd::lane<0>(grP[p][u]);
            grCh[1][u] = simd::lane<1>(grP[p][u]);
        }

        // the OS rate: gains interpolated (file comment), osMain encoded, gain, colour, makeup, decode
        if (rt.fresh)
        {
            rt.lastGr[0] = grCh[0][0];
            rt.lastGr[1] = grCh[1][0];
            rt.lastPre = pre[p][0];
            rt.lastMk = mk[p][0];
            rt.fresh = false;
        }
        const bool preFlat = toOsRate(pre[p].data(), n, factor, rt.lastPre, preOs[p].data());
        const bool mkFlat = toOsRate(mk[p].data(), n, factor, rt.lastMk, mkOs[p].data());
        float* w0 = wet[p][0].data();
        float* w1 = wet[p][1].data();
        host::encode(osMain[0].data(), osMain[1].data(), w0, w1, nOs, rt.route);
        for (int c = 0; c < 2; ++c)
        {
            const bool flat = toOsRate(grCh[c].data(), n, factor, rt.lastGr[c], grOs[c].data()) && preFlat;
            float* w = wet[p][c].data();
            if (flat)                                   // a steady chunk: one linFromDb (the same value per sample)
                applyGain(w, nullptr, simd::lane<0>(linFromDb(simd::set1(preOs[p][0] - grOs[c][0]))), true, nOs4);
            else
            {
                for (int k = 0; k < nOs4; k += 4)
                {
                    const auto u = static_cast<std::size_t>(k);
                    simd::store(&gdb[u], simd::sub(simd::load(&preOs[p][u]), simd::load(&grOs[c][u])));
                }
                linFromDbArray(gdb.data(), linOs.data(), nOs4);
                applyGain(w, linOs.data(), 0.0f, false, nOs4);
            }
            if (keepColourIn)
            {
                // The colour-input meter reads only the block's peak (TelemetryAccum's colPeak_), so the chunk's peak
                // over its OS samples is all it needs (file comment "Telemetry cost"): 4 wide, then the tail.
                const int nOsFloor4 = nOs & ~3;
                simd::f32x4 mv = simd::set1(0.0f);
                for (int k = 0; k < nOsFloor4; k += 4)
                    mv = simd::max(mv, simd::abs(simd::load(w + k)));
                float m = simd::lane<0>(mv);
                m = simd::lane<1>(mv) > m ? simd::lane<1>(mv) : m;
                m = simd::lane<2>(mv) > m ? simd::lane<2>(mv) : m;
                m = simd::lane<3>(mv) > m ? simd::lane<3>(mv) : m;
                for (int k = nOsFloor4; k < nOs; ++k)
                {
                    const float a = w[k] < 0.0f ? -w[k] : w[k];
                    m = a > m ? a : m;
                }
                colIn[c][0] = m;                                // colIn[c][1...] stay 0 (value-initialised)
            }
        }
        float* wetPtr[2] = { w0, w1 };
        const float* grPtr[2] = { grOs[0].data(), grOs[1].data() };
        AudioIo aio;
        aio.nOs = nOs;
        aio.wet = wetPtr;
        aio.grDbOs = grPtr;
        ps.engine->colour(aio);
        const float mk0 = mkFlat ? simd::lane<0>(linFromDb(simd::set1(mkOs[p][0]))) : 0.0f;
        if (!mkFlat)
            linFromDbArray(mkOs[p].data(), linOs.data(), nOs4);
        for (int c = 0; c < 2; ++c)
            applyGain(wet[p][c].data(), linOs.data(), mk0, mkFlat, nOs4);
        host::decode(w0, w1, osMain[0].data(), osMain[1].data(), w0, w1, nOs, rt.route);
    }
};

namespace {

// The unconfigured guard (HR B §1.7): input to output, a missing input channel as its neighbour or silence.
void passThrough(const ProcessIo& io) noexcept FCDSP_NONBLOCKING
{
    const int nOut = io.numOut < 2 ? io.numOut : 2;
    const int nIn = io.in == nullptr ? 0 : (io.numIn < 2 ? io.numIn : 2);
    for (int c = 0; c < nOut; ++c)
    {
        float* o = io.out[c];
        if (o == nullptr)
            continue;
        const float* in = nIn > 0 ? io.in[c < nIn ? c : nIn - 1] : nullptr;
        if (in == nullptr)
            for (int i = 0; i < io.n; ++i)
                o[i] = 0.0f;
        else if (in != o)
            for (int i = 0; i < io.n; ++i)
                o[i] = in[i];
    }
}

} // namespace

// ==== construction and configuration =================================================================================

EngineHost::EngineHost() noexcept = default;

EngineHost::~EngineHost() = default;

void EngineHost::configure(const HostConfig& cfg, const BlockParams& initial)
{
    auto next = std::make_unique<Impl>();
    Impl& s = *next;
    s.cfg = cfg;
    const double fsD = cfg.fs > 0.0 ? cfg.fs : 48000.0;
    const int q = qualityIndex(cfg.quality);
    s.fs = static_cast<float>(fsD);
    s.factor = kOs[q].factor;
    s.fsOs = s.fs * static_cast<float>(s.factor);
    s.la = lookaheadSamples(cfg.budget, fsD);
    s.dUp = kOs[q].dUp;
    s.latency = s.la + kOs[q].latency;

    const int laMax = lookaheadSamples(kMaxBudget, fsD);
    const std::size_t scratchFloats = 4u * static_cast<std::size_t>(nextPow2(laMax + kChunk));
    for (std::vector<float>& v : s.scratch)
        v.assign(scratchFloats, 0.0f);
    for (int i = 0; i < 2; ++i)
    {
        s.path[i].gain.prepare(s.fs, kGainSmoothMs);
        s.gainOut[i].prepare(s.fs, kGainSmoothMs);
        s.scf[i].prepare(s.fs);
        s.scDelay[i].configure(s.la + s.dUp);
        s.mainDelay[i].configure(s.la);
        s.mainDelay[i].setDelay(s.la);
        s.dryDelay[i].configure(s.latency);
        s.dryDelay[i].setDelay(s.latency);
    }
    s.listenDelay.configure(s.latency);
    s.listenDelay.setDelay(s.latency);
    s.os.configure(cfg.quality, cfg.maxBlock, 2);
    s.mix.prepare(s.fs, kGainSmoothMs);
    s.mixOut.prepare(s.fs, kGainSmoothMs);
    s.output.prepare(s.fs, kGainSmoothMs);
    s.outputOut.prepare(s.fs, kGainSmoothMs);
    s.bypass.prepare(s.fs);
    s.listen.prepare(s.fs);
    s.delta.prepare(s.fsOs);
    s.fade.prepare(s.fsOs, fsD);
    s.telem.prepare(fsD);

    const EngineParams eng = clampLook(initial.eng, cfg.budget);
    const ModeSlot& ms = resolveSlot(initial.slot);
    if (ms.entry != nullptr)
    {
        const bool keyExt = initial.extKey && cfg.keyChans > 0;
        s.install(*ms.entry, host::kernelKey(ms.slot, eng, keyExt), eng);
    }
    s.mix.setTarget(simd::set1(eng.mix));
    s.snapMix();
    s.output.setTarget(simd::set1(outputGain(initial.outputDb)));
    s.snapOutput();
    s.bypass.setTarget(initial.bypass);
    s.bypass.snap();
    s.listen.setTarget(initial.listen);
    s.listen.snap();
    s.delta.setTarget(initial.delta);
    s.delta.snap();
    s.publishCount = impl_ != nullptr ? impl_->publishCount : 0u;
    impl_ = std::move(next);
}

int EngineHost::latencyFor(const HostConfig& cfg) noexcept FCDSP_NONBLOCKING
{
    return lookaheadSamples(cfg.budget, cfg.fs) + kOs[qualityIndex(cfg.quality)].latency;
}

int EngineHost::latencySamples() const noexcept FCDSP_NONBLOCKING
{
    return impl_ != nullptr ? impl_->latency : 0;
}

double EngineHost::tailSeconds(const BlockParams& bp) const noexcept
{
    const ModeEntry* e = resolveSlot(bp.slot).entry;
    double t = 0.0;
    if (e != nullptr && e->desc != nullptr && e->desc->tailSeconds != nullptr)
        t = static_cast<double>(e->desc->tailSeconds(bp.eng));
    if (impl_ != nullptr && impl_->cfg.fs > 0.0)
        t += static_cast<double>(impl_->latency) / impl_->cfg.fs;
    return t;
}

// ==== the audio thread ===============================================================================================

void EngineHost::process(const ProcessIo& io, const BlockParams& bp) noexcept FCDSP_NONBLOCKING
{
    const ScopedFtz ftz;                                // step 0 (01 §5.7): the FP mode is the host's own
    if (io.n <= 0 || io.out == nullptr || io.numOut <= 0)
        return;
    Impl* const sp = impl_.get();
    if (sp == nullptr || sp->activePath().engine == nullptr)
    {
        passThrough(io);
        return;
    }
    Impl& s = *sp;

    // ---- 1. block start ---------------------------------------------------------------------------------------------
    TestTap* const tap = tap_.load(std::memory_order_acquire);
    const bool attached = attached_.load(std::memory_order_acquire) > 0;
    const bool snapRequested = snap_.exchange(false, std::memory_order_acquire);
    const bool snap = snapRequested || s.snapPending;
    s.snapPending = false;

    const EngineParams eng = clampLook(bp.eng, s.cfg.budget);
    const int nIn = io.in == nullptr ? 0 : (io.numIn < 2 ? io.numIn : 2);
    const int nOut = io.numOut < 2 ? io.numOut : 2;
    const bool keyExt = host::keyExternal(bp.extKey, s.cfg.keyChans, io.key, io.numKey);
    const int nKey = io.numKey < 2 ? io.numKey : 2;

    const ModeSlot& ms = resolveSlot(bp.slot);
    const KernelKey want = host::kernelKey(ms.slot, eng, keyExt);
    if (ms.entry != nullptr && !(want == s.activePath().key) && s.fade.mayStart(s.sampleIndex))
        s.startFade(*ms.entry, want, eng, s.sampleIndex);      // else latched: seen again at the next block start

    const int in = s.active;
    PathState& p = s.path[in];
    if (p.key == want)                                  // the block's parameters reach only their own kernel (K2 #3a)
    {
        p.eng = eng;
        p.engine->setParams(eng);
        p.gain.setTarget(gainTarget(eng, *p.engine));
        s.scf[in].setTarget(eng.scHpfHz, eng.sceDbOct);
        s.scDelay[in].setTarget(s.scDelayFor(in));
    }
    s.mix.setTarget(simd::set1(p.eng.mix));
    s.output.setTarget(simd::set1(outputGain(bp.outputDb)));
    s.bypass.setTarget(bp.bypass || io.hostBypassed);
    s.listen.setTarget(bp.listen);
    s.delta.setTarget(bp.delta);
    if (snap)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (s.path[i].engine == nullptr)
                continue;
            s.path[i].engine->snapParams();
            s.snapGain(i);
            s.scf[i].snap();
            s.scDelay[i].setDelay(s.scDelayFor(i));
        }
        s.snapMix();
        s.snapOutput();
        s.bypass.snap();
        s.listen.snap();
        s.delta.snap();
    }

    if (attached && !s.wasAttached)
    {
        s.telem.attach();
        s.internal0 = s.latchInternal();
    }
    s.wasAttached = attached;
    if (attached)
        s.telem.beginBlock();
    const bool detail = attached || tap != nullptr;
    const bool keyDelivered = io.key != nullptr && io.numKey > 0 && io.key[0] != nullptr && s.cfg.keyChans > 0;

    int replaced = 0;
    bool poisoned = false, fadedInBlock = s.fade.running();

    // ---- 2. chunks on the absolute kChunk grid ----------------------------------------------------------------------
    for (int off = 0; off < io.n;)
    {
        const int toGrid = kChunk - static_cast<int>(s.sampleIndex % static_cast<uint64_t>(kChunk));
        const int n = io.n - off < toGrid ? io.n - off : toGrid;
        const int F = s.factor, nOs = n * F, nOs4 = roundUp4(nOs);
        const int cur = s.active, old = s.outgoing();
        const bool fading = s.fade.running();
        if (attached && s.sampleIndex % static_cast<uint64_t>(kChunk) == 0)
            s.internal0 = s.latchInternal();

        // a. route and sanitise (before any other read of the input)
        for (int c = 0; c < 2; ++c)
        {
            float* d = s.dry[c].data();
            if (nIn == 0 || io.in[c < nIn ? c : nIn - 1] == nullptr)
            {
                for (int i = 0; i < n; ++i)
                    d[i] = 0.0f;
            }
            else if (c == 1 && nIn == 1)
            {
                for (int i = 0; i < n; ++i)
                    d[i] = s.dry[0][static_cast<std::size_t>(i)];      // mono duplication
            }
            else
                replaced += sanitize(io.in[c] + off, d, n);
        }
        const bool needKey = keyDelivered && (s.path[cur].key.keyExt != 0 || (fading && s.path[old].key.keyExt != 0));
        if (needKey)
        {
            replaced += sanitize(io.key[0] + off, s.key[0].data(), n);
            if (nKey > 1 && io.key[1] != nullptr)
                replaced += sanitize(io.key[1] + off, s.key[1].data(), n);
            else
                for (int i = 0; i < n; ++i)
                    s.key[1][static_cast<std::size_t>(i)] = s.key[0][static_cast<std::size_t>(i)];
        }

        // b, c. per-path side chains: source, SC filter, SC delay; the idle line mirrors the incoming path's history
        const auto sideChain = [&](int pi) noexcept FCDSP_NONBLOCKING {
            const bool ext = keyDelivered && s.path[pi].key.keyExt != 0;
            host::ScSource src;
            src.ch[0] = ext ? s.key[0].data() : s.dry[0].data();
            src.ch[1] = ext ? s.key[1].data() : s.dry[1].data();
            src.external = ext;
            host::scLanes(src, s.scSrc.data(), n);
            s.scf[pi].process(s.scSrc.data(), s.scFilt[pi].data(), n, s.sampleIndex);
            s.scDelay[pi].process(s.scFilt[pi].data(), s.scDel[pi].data(), n, s.sampleIndex);
        };
        sideChain(cur);
        if (fading)
            sideChain(old);
        else
            s.scDelay[old].process(s.scFilt[cur].data(), s.scDel[old].data(), n, s.sampleIndex);
        s.listenDelay.process(s.scFilt[cur].data(), s.scLsn.data(), n, s.sampleIndex);
        for (int c = 0; c < 2; ++c)
        {
            s.mainDelay[c].process(s.dry[c].data(), s.dly[c].data(), n, s.sampleIndex);
            s.dryDelay[c].process(s.dry[c].data(), s.byp[c].data(), n, s.sampleIndex);
        }

        // d. one up() of the delayed main: the dry signal of the OS domain, never pre-gained
        {
            const float* upIn[2] = { s.dly[0].data(), s.dly[1].data() };
            float* upOut[2] = { s.osMain[0].data(), s.osMain[1].data() };
            (void) s.os.up(upIn, n, upOut);
        }

        // e. the paths
        if (fading)
            s.runPath(old, n, s.sampleIndex, keyDelivered, false, false, nullptr);
        s.runPath(cur, n, s.sampleIndex, keyDelivered, detail, attached, tap);

        // f, g. blend, mix and delta inside the OS domain
        for (int i = 0; i < n; ++i)
            s.mixv[static_cast<std::size_t>(i)] = simd::lane<0>(s.tickMix());
        if (s.mixFresh)
        {
            s.lastMix = s.mixv[0];
            s.mixFresh = false;
        }
        toOsRate(s.mixv.data(), n, F, s.lastMix, s.mixOs.data());
        const bool landed = fading && s.fade.positions(s.fadePos.data(), nOs);
        const bool deltaOn = !s.delta.restingOff();
        if (deltaOn)
        {
            s.delta.positions(s.dAmt.data(), nOs);
            for (const int pi : { cur, old })
            {
                if (pi == old && !fading)
                    continue;
                for (int k = 0; k < nOs4; k += 4)
                {
                    const auto u = static_cast<std::size_t>(k);
                    s.gdb[u + 0] = -s.preOs[pi][u + 0] - s.mkOs[pi][u + 0];
                    s.gdb[u + 1] = -s.preOs[pi][u + 1] - s.mkOs[pi][u + 1];
                    s.gdb[u + 2] = -s.preOs[pi][u + 2] - s.mkOs[pi][u + 2];
                    s.gdb[u + 3] = -s.preOs[pi][u + 3] - s.mkOs[pi][u + 3];
                }
                linFromDbArray(s.gdb.data(), s.und[pi].data(), nOs4);
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            const float* dry = s.osMain[c].data();
            const float* wi = s.wet[cur][c].data();
            const float* wo = s.wet[old][c].data();
            float* yc = s.y[c].data();
            const float* w = wi;
            if (fading)
            {
                for (int k = 0; k < nOs; ++k)
                {
                    const auto u = static_cast<std::size_t>(k);
                    s.blendBuf[u] = host::blend(wo[k], wi[k], s.fadePos[u]);
                }
                w = s.blendBuf.data();
            }
            for (int k = 0; k < nOs4; k += 4)
            {
                const simd::f32x4 m = simd::load(&s.mixOs[static_cast<std::size_t>(k)]);
                const simd::f32x4 d = simd::mul(simd::sub(simd::set1(1.0f), m), simd::load(dry + k));
                simd::store(yc + k, simd::fma(d, m, simd::load(w + k)));             // (1 - mix) dry + mix wet
            }
            if (deltaOn)
                for (int k = 0; k < nOs; ++k)
                {
                    const auto u = static_cast<std::size_t>(k);
                    const float ci = wi[k] * s.und[cur][u];
                    const float comp = fading ? host::blend(wo[k] * s.und[old][u], ci, s.fadePos[u]) : ci;
                    yc[k] = host::blend(yc[k], s.mixOs[u] * (dry[k] - comp), s.dAmt[u]);
                }
        }

        // h. one down()
        {
            const float* dnIn[2] = { s.y[0].data(), s.y[1].data() };
            float* dnOut[2] = { s.out[0].data(), s.out[1].data() };
            s.os.down(dnIn, nOs, dnOut);
        }
        s.applyOutput(n);

        // i. poison (01 §5.8), then listen and bypass against their latency-aligned signals
        bool ok = allFinite(s.out[0].data(), n) && allFinite(s.out[1].data(), n) && s.path[cur].engine->finite()
               && s.smoothersFinite(cur) && s.scf[cur].finite() && finiteLanes(s.mix.cur)
               && finiteLanes(s.mixOut.cur) && finiteLanes(s.output.cur) && finiteLanes(s.outputOut.cur);
        if (fading)
            ok = ok && s.path[old].engine->finite() && s.smoothersFinite(old) && s.scf[old].finite();
        if (!ok)
        {
            poisoned = true;
            s.snapPending = true;
            if (fading)
                s.endFade();
            s.path[cur].engine->reset();
            s.prt[cur].fresh = true;
            s.mixFresh = true;
            s.scf[cur].reset();
            s.os.reset();
            for (int c = 0; c < 2; ++c)
            {
                s.out[c] = s.byp[c];
                s.colIn[c].fill(0.0f);
            }
            s.scIn[cur].fill(simd::set1(0.0f));
            s.grP[cur].fill(simd::set1(0.0f));
            s.det.fill(simd::set1(0.0f));
            s.tgt.fill(simd::set1(0.0f));
            s.s2.fill(simd::set1(0.0f));
            s.bits.fill(0);
            s.pre[cur].fill(0.0f);
            s.listen.skip(n);
            s.bypass.skip(n);
        }
        else
        {
            float* outPtr[2] = { s.out[0].data(), s.out[1].data() };
            const float* procPtr[2] = { s.out[0].data(), s.out[1].data() };
            if (!s.listen.restingOff())
            {
                // the delayed filtered SC {L, R, L, R}, encoded as the incoming engine hears it, decoded to L/R
                host::encodeSc(s.scLsn.data(), s.scLsn.data(), n, s.prt[cur].route);
                host::listen(s.scLsn.data(), s.lsn[0].data(), s.lsn[1].data(), n, s.prt[cur].route);
                const float* lsnPtr[2] = { s.lsn[0].data(), s.lsn[1].data() };
                s.listen.apply(procPtr, lsnPtr, outPtr, 2, n, s.amount.data());
            }
            const float* bypPtr[2] = { s.byp[0].data(), s.byp[1].data() };
            s.bypass.apply(procPtr, bypPtr, outPtr, 2, n, s.amount.data());
        }
        for (int c = 0; c < nOut; ++c)
            if (io.out[c] != nullptr)
                for (int i = 0; i < n; ++i)
                    io.out[c][off + i] = s.out[c][static_cast<std::size_t>(i)];

        // j. telemetry
        if (attached)
        {
            host::TelemetryChunk t;
            t.n = n;
            for (int c = 0; c < 2; ++c)
            {
                t.in[c] = s.dry[c].data();
                t.out[c] = s.out[c].data();
                t.colourIn[c] = s.colIn[c].data();
            }
            t.preGainDb = s.pre[cur].data();
            t.sc = s.scIn[cur].data();
            t.grDb = s.grP[cur].data();
            t.detDb = s.det.data();
            t.tgtDb = s.tgt.data();
            t.s2GrDb = s.s2.data();
            t.bits = s.bits.data();
            const uint32_t colBits = static_cast<uint32_t>(s.path[cur].key.slot) << host::kColSlotShift
                                   | (fading ? host::kColFading : 0u);
            s.telem.accumulate(t, s.sampleIndex, colBits, s.internal0, history_);
        }
        else
            s.telem.advance(s.sampleIndex, n);

        if (landed && s.fade.running())
            s.endFade();                                // 01 §5.5 step 5
        s.sampleIndex += static_cast<uint64_t>(n);
        off += n;
        fadedInBlock = fadedInBlock || s.fade.running();
    }

    // ---- 3. publish -------------------------------------------------------------------------------------------------
    if (!attached)
        return;
    const PathState& pa = s.path[s.active];
    const bool fadingNow = s.fade.running();
    UiFrame f{};
    f.publishCount = ++s.publishCount;
    f.modeSlot = pa.key.slot;
    f.fadeFromSlot = fadingNow ? s.path[s.outgoing()].key.slot : pa.key.slot;
    uint32_t flags = s.telem.publish(io.n, f);
    if (bp.bypass || io.hostBypassed)
        flags |= kUiBypassed;
    if (bp.delta)
        flags |= kUiDelta;
    if (bp.listen)
        flags |= kUiListen;
    if (keyExt)
        flags |= kUiExtKeyActive;
    if (s.prt[s.active].route.domain == LaneDomain::ms)
        flags |= kUiMidSide;
    if (fadedInBlock)
        flags |= kUiFading;
    if (s.la > 0)
        flags |= kUiLookahead;
    if (pa.eng.topo == kTopoFB && pa.key.keyExt == 0)
        flags |= kUiTopoFB;
    if ((pa.eng.flags & kEngGrOff) != 0)
        flags |= kUiGrOff;
    if (poisoned || replaced > 0)
        flags |= kUiPoisonReset;
    f.flags = flags;
    f.sampleRate = s.fs;
    f.latencySamples = static_cast<uint32_t>(s.latency);
    f.fadeProgress = s.fade.position();
    f.bypassAmt = s.bypass.position();
    f.historyWritten = static_cast<uint32_t>(history_.written());

    EngineTelemetry et;
    pa.engine->telemetry(et);
    for (int c = 0; c < 2; ++c)
    {
        f.attackNowMs[c] = finiteOr0(et.attackNowMs[c]);
        f.releaseNowMs[c] = finiteOr0(et.releaseNowMs[c]);
        f.crestDb[c] = finiteOr0(et.crestDb[c]);
    }
    const EngineParams& e = pa.eng;
    f.preGainDb = finiteOr0(simd::lane<0>(s.gainOut[s.active].cur));
    f.thrDb = e.thrDb;
    f.slope = e.slope;
    f.kneeDb = e.kneeDb;
    f.rangeDb = e.rangeDb;
    f.atkTauMs = e.atkTauMs;
    f.relTauMs = e.relTauMs;
    f.holdMs = e.holdMs;
    f.lookMs = e.lookMs;
    f.driveDb = e.driveDb;
    f.makeupEffDb = finiteOr0(simd::lane<1>(s.gainOut[s.active].cur));
    f.mix = finiteOr0(simd::lane<0>(s.mixOut.cur));
    f.scHpfHz = e.scHpfHz;
    f.sceDbOct = e.sceDbOct;
    f.link = e.link;
    f.s2ThrDb = e.s2ThrDb;
    f.s2AtkTauMs = e.s2AtkTauMs;
    f.s2RelTauMs = e.s2RelTauMs;
    f.tags = e.tags;
    f.discrete = static_cast<uint32_t>(e.det) | static_cast<uint32_t>(e.stmode) << 8
               | static_cast<uint32_t>(e.voice) << 16 | static_cast<uint32_t>(e.tmode) << 24;
    pa.engine->internals(f.internals);
    for (float& v : f.internals)
        v = finiteOr0(v);
    uiFrame_.publish(f);
}

void EngineHost::reset() noexcept FCDSP_NONBLOCKING
{
    Impl* const sp = impl_.get();
    if (sp == nullptr)
        return;
    Impl& s = *sp;
    if (s.fade.running())
        s.endFade();
    const int in = s.active;
    if (s.path[in].engine != nullptr)
        s.path[in].engine->reset();
    s.prt[in].fresh = true;
    s.mixFresh = true;
    s.snapGain(in);
    s.snapMix();
    s.snapOutput();
    s.bypass.snap();
    s.listen.snap();
    s.delta.snap();
    for (int i = 0; i < 2; ++i)
    {
        s.scf[i].reset();
        s.scDelay[i].reset();
        s.mainDelay[i].reset();
        s.dryDelay[i].reset();
    }
    s.listenDelay.reset();
    s.os.reset();
    s.telem.attach();                                   // meters and the open column restart; next column is a gap
    s.internal0 = s.latchInternal();
}

// ==== any thread =====================================================================================================

void EngineHost::requestSnap() noexcept FCDSP_NONBLOCKING
{
    snap_.store(true, std::memory_order_release);
}

void EngineHost::setUiAttached(bool attached) noexcept FCDSP_NONBLOCKING
{
    if (attached)
    {
        attached_.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    int32_t cur = attached_.load(std::memory_order_relaxed);
    while (cur > 0 && !attached_.compare_exchange_weak(cur, cur - 1, std::memory_order_acq_rel,
                                                        std::memory_order_relaxed))
    {
    }
}

bool EngineHost::readUiFrame(UiFrame& out) const noexcept FCDSP_NONBLOCKING
{
    return uiFrame_.read(out);
}

const HistoryRing& EngineHost::history() const noexcept FCDSP_NONBLOCKING
{
    return history_;
}

void EngineHost::setTap(TestTap* tap) noexcept FCDSP_NONBLOCKING
{
    tap_.store(tap, std::memory_order_release);
}

} // namespace fcdsp
