// EngineHost.cpp: the Mode-agnostic engine host (01 §5.4-5.8, §6; E §3.1, §7; K2 #2, #3c, #8, #13, #18).
// Orchestration only: the work lives in the pure components under engine/host/ (Ramps.h, TelemetryAccum.h; F4) and in
// the Mode engines behind IEngine (one virtual call per chunk).
//
// ==== Scope: the S3 ECO skeleton (F4) ================================================================================
// Every block runs the ECO signal path of 01 §5.4:
//   0. ScopedFtz; the unconfigured guard (input copied to output); main and key sanitised into host scratch
//      (core/Sanitize.h) before anything else reads them (K2 #13).
//   1. Block start: the snap flag (acquire), the defensive look clamp, the KernelKey {slot, topo, det, stmode, voice,
//      keyExt} (keyExt = extKey AND a key bus is active, K2 #22); a changed key swaps engines, SNAPPED: the new
//      engine is placement-constructed into the idle arena, prepared, given the block's parameters, snapped and seeded
//      from the old engine's carry(); then the old one is destroyed (01 §5.5 steps 2, 3 and 5; the 20 ms crossfade of
//      step 4, its 50 ms start gap and its latch are F7's). The path's gain targets {preGainDb, makeupDb +
//      autoMakeupDb()}, the host mix target and the bypass ramp's target are set here.
//   2. Per chunk of at most kChunk samples, the chunks aligned to ABSOLUTE multiples of kChunk (a block boundary
//      splits a chunk, a chunk never straddles a multiple of kChunk), so everything the host samples per chunk is
//      block-size invariant:
//        a. route: L/R; a mono input is duplicated; SC = the key when keyExt, else the main input. At S3 there is no
//           stmode encode/decode (Router.h, F5) and no SC filter (ScFilter.h, F5): lanes 0-1 are always L/R;
//        b. the path's gain smoother {preGainDb, makeupTotalDb} and the host mix smoother tick per sample;
//        c. SC lanes {L, R, L, R} (the EngineRig convention: aux lanes carry a copy of the channels), x
//           linFromDb(preGainDb) when internal; a key is never pre-gained;
//        d. control(): GR per sample (+ detDb, tgtDb, s2GrDb, bits for telemetry and the tap);
//        e. g = linFromDb(preGainDb - GR) per channel, wet = dry x g (preGain reaches the wet path only, K2 #3c);
//           colour() in place at the base rate with the GR as its grDbOs; x linFromDb(makeupTotalDb);
//        f. mix at the base rate against the NEVER pre-gained dry: y = mix wet + (1 - mix) dry;
//        g. poison check of this chunk (01 §5.8; see below), then the bypass ramp against the dry input;
//        h. the tap (incoming path) and, while attached, telemetry accumulation (host/TelemetryAccum.h).
//   3. While attached: the UiFrame, published once per process() call (the columns are pushed as they complete).
//
// Documented choices where 01 is silent or where S3 is a subset of it (see also the F4 handoff):
//   - ECO only, no lookahead delays: whatever HostConfig asks for, S3 runs the ECO path with no delay line, so
//     latencySamples() reports what process() does, 0; latencyFor(cfg) is the design value (lookaheadSamples +
//     kOs[quality].latency) the processor will report once F7 installs the oversampler, the lookahead delays (F5's
//     Delay.h) and the SC delay. PrepareInfo::osFactor is 1; engine scratch is sized as 01 §5.3 says, from the
//     configured budget. `look` is clamped to the configured budget (step 1) and published, but no delay moves.
//   - Poison is checked per chunk, not once per block: the chunk's processed output (every sample, not just the
//     last), the active engine's finite() and the host smoothers. On failure that chunk outputs the latency-aligned
//     sanitised dry (at S3 the sanitised input itself), the engine is reset, and the next block starts snapped (its
//     smoothers jump to the new targets, so a non-finite target cannot survive the reset); kUiPoisonReset is set for
//     the block, as it is when sanitize() replaced or clamped a sample. Recovery is therefore at most one block.
//   - The bypass ramp (host/Ramps.h) blends the poison-checked processed signal with the dry input: a settled bypass
//     is bit-exact against the input delayed by latencySamples(); io.hostBypassed and BlockParams::bypass both engage
//     it (HR B §1.6); configure() and a snap start it at its target, never mid-fade.
//   - requestSnap() (state recall) jumps the engine's smoothers, the path gain, the mix and the bypass ramp to their
//     targets at the next block start. reset() resets the engine and snaps the host smoothers; the host's sample
//     index never goes back, so control ticks and history columns stay on the absolute grid.
//   - Delta and SC listen are F7's: BlockParams::delta and ::listen are ignored and kUiDelta/kUiListen stay clear;
//     kUiMidSide stays clear (no encoder at S3) and kUiFading/kUiLookahead too.
//   - Smoothing (F4 finding). 01 §5.1 gives the host one-pole Smoother4s (20 ms): PathState::gain {preGainDb,
//     makeupTotalDb} and the mix. Each is followed here by a second, identical Smoother4 stage (a critically damped
//     two-pole: the applied gain starts moving with zero slope, as the lead's smoothstep ramps do). With the single
//     one-pole, dsp.zipper's C §5.6 step edge (25 % -> 75 % in one block at t = 1 s on a 110 Hz tone at -6 dBFS,
//     click metric <= +3 dB) read +10.3 dB for makeup, +20.7 dB for mix and +17.5 / +28.0 dB for the auto-makeup
//     detent; with the second stage -22.4, -2.8 and -10.4 / +0.6 dB. Both stages land exactly (Smoother4's epsilon
//     and stall landing), so a settled host renders its targets bit for bit (the nulls). Stage 1 stays
//     PathState::gain (01 §5.5); the stage-2 states are Impl::gainOut[slot] and Impl::mixOut.
//   - UiFrame parameter words: preGainDb, makeupEffDb and mix are the host's smoothed (stage-2) values; the
//     engine-internal ones (thrDb ... s2RelTauMs) are the block's resolved values (the engine does not expose its
//     smoothers). Engine telemetry words and internals that are not finite are published as 0.
//   - The history internal (the Mode's InternalSpec with history == true) is latched from internals() at every
//     absolute multiple of kChunk (before that chunk's control()), so HistoryColumn::internal0 is block-size
//     invariant; on an attach transition it is latched at once.
//   - UiFrame::publishCount survives a reconfigure (the editor's staleness check sees it keep moving).

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
#include "fcdsp/engine/host/Ramps.h"
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

// kChunk is a multiple of 4, so the 4-wide passes below may round a chunk up and stay inside the buffers.
static_assert(kChunk % 4 == 0);

inline int roundUp4(int n) noexcept FCDSP_NONBLOCKING { return (n + 3) & ~3; }

inline bool finiteF(float v) noexcept FCDSP_NONBLOCKING
{
    return (std::bit_cast<uint32_t>(v) & 0x7f800000u) != 0x7f800000u;
}

inline float finiteOr0(float v) noexcept FCDSP_NONBLOCKING { return finiteF(v) ? v : 0.0f; }

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

// lin[i] = linFromDb(db[i]) for i < roundUp4(n) (buffers hold kChunk floats).
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

} // namespace

// ==== Impl: everything the audio path owns; configure() allocates it ================================================

struct EngineHost::Impl {
    using Buf = std::array<float, kChunk>;
    using VBuf = std::array<simd::f32x4, kChunk>;

    HostConfig cfg{};
    float fs = 48000.0f;
    int latency = 0;                                    // what process() delays by (S3: 0; see the file comment)
    uint32_t publishCount = 0;

    alignas(64) std::array<std::byte, kArenaBytes> arena[2]{};
    std::vector<float> scratch[2];                      // per arena slot, host-owned (01 §5.3 "Scratch")
    PathState path[2]{};
    int active = 0;
    int histInternal = -1;                              // the active Mode's history internal, or -1
    Smoother4 mix{};                                    // lane 0: the incoming Mode's mix
    Smoother4 gainOut[2]{};                             // second stage of path[i].gain (file comment: "Smoothing")
    Smoother4 mixOut{};                                 // second stage of mix
    host::HostRamp bypass{};
    host::TelemetryAccum telem{};
    uint64_t sampleIndex = 0;                           // absolute, since configure(); never goes back
    bool snapPending = false;                           // after a poison reset: the next block starts snapped
    bool wasAttached = false;
    float internal0 = 0.0f;                             // latched history internal

    // chunk buffers (value-initialised: the 4-wide passes may read a few lanes past n)
    alignas(16) VBuf sc{}, gr{}, det{}, tgt{}, s2{};
    std::array<uint8_t, kChunk> bits{};
    alignas(16) Buf dry[2]{}, key[2]{}, wet[2]{}, colIn[2]{}, grCh[2]{}, y[2]{};
    alignas(16) Buf pre{}, mk{}, mixv{}, lin{}, amount{};

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

    // 01 §5.5 steps 2, 3, 5 without the fade (F7): construct into the idle arena, prepare, setParams, snapParams,
    // seed from the old engine's carry, snap the new path's gain, destroy the old engine, swap.
    void swapTo(const ModeEntry& entry, const KernelKey& k, const EngineParams& e) noexcept FCDSP_NONBLOCKING
    {
        const int idle = 1 - active;
        PathState& old = path[active];
        PathState& nu = path[idle];
        destroy(nu);
        std::vector<float>& scr = scratch[idle];
        nu.engine = entry.construct(arena[idle].data());
        nu.engine->prepare(PrepareInfo{ fs, 1, std::span<float>(scr.data(), scr.size()) });
        nu.engine->setParams(e);
        nu.engine->snapParams();
        if (old.engine != nullptr)
            nu.engine->seed(old.engine->carry());
        nu.key = k;
        nu.eng = e;
        nu.gain.setTarget(gainTarget(e, *nu.engine));
        snapGain(idle);
        destroy(old);
        active = idle;
        histInternal = historyInternal(entry);
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
    simd::f32x4 tickGain() noexcept FCDSP_NONBLOCKING
    {
        gainOut[active].setTarget(path[active].gain.tick());
        return gainOut[active].tick();
    }
    simd::f32x4 tickMix() noexcept FCDSP_NONBLOCKING
    {
        mixOut.setTarget(mix.tick());
        return mixOut.tick();
    }
    bool smoothersFinite() const noexcept FCDSP_NONBLOCKING
    {
        return finiteLanes(path[active].gain.cur) && finiteLanes(gainOut[active].cur) && finiteLanes(mix.cur)
            && finiteLanes(mixOut.cur);
    }

    float latchInternal() const noexcept FCDSP_NONBLOCKING
    {
        const PathState& p = path[active];
        if (histInternal < 0 || p.engine == nullptr)
            return 0.0f;
        float out[kInternals]{};
        p.engine->internals(out);
        return finiteOr0(out[histInternal]);
    }
};

// ==== construction and configuration =================================================================================

EngineHost::EngineHost() noexcept = default;

EngineHost::~EngineHost() = default;

void EngineHost::configure(const HostConfig& cfg, const BlockParams& initial)
{
    auto next = std::make_unique<Impl>();
    Impl& s = *next;
    s.cfg = cfg;
    s.fs = static_cast<float>(cfg.fs > 0.0 ? cfg.fs : 48000.0);
    s.latency = 0;
    const int la = lookaheadSamples(cfg.budget, cfg.fs);
    const std::size_t scratchFloats = 4u * static_cast<std::size_t>(nextPow2(la + kChunk));
    for (std::vector<float>& v : s.scratch)
        v.assign(scratchFloats, 0.0f);
    for (int i = 0; i < 2; ++i)
    {
        s.path[i].gain.prepare(s.fs, kGainSmoothMs);
        s.gainOut[i].prepare(s.fs, kGainSmoothMs);
    }
    s.mix.prepare(s.fs, kGainSmoothMs);
    s.mixOut.prepare(s.fs, kGainSmoothMs);
    s.bypass.prepare(s.fs);
    s.telem.prepare(cfg.fs > 0.0 ? cfg.fs : 48000.0);

    const EngineParams eng = clampLook(initial.eng, cfg.budget);
    const ModeSlot& ms = resolveSlot(initial.slot);
    if (ms.entry != nullptr)
    {
        const bool keyExt = initial.extKey && cfg.keyChans > 0;
        const KernelKey k{ ms.slot, eng.topo, eng.det, eng.stmode, eng.voice, static_cast<uint8_t>(keyExt ? 1 : 0) };
        s.swapTo(*ms.entry, k, eng);
    }
    s.mix.setTarget(simd::set1(eng.mix));
    s.snapMix();
    s.bypass.setTarget(initial.bypass);
    s.bypass.snap();
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

namespace {

// The unconfigured guard (HR B §1.7): input to output, a missing input channel as its neighbour or silence.
void passThrough(const ProcessIo& io) noexcept FCDSP_NONBLOCKING
{
    const int nOut = io.numOut < 2 ? io.numOut : 2;
    const int nIn = io.in == nullptr ? 0 : (io.numIn < 2 ? io.numIn : 2);
    for (int c = 0; c < nOut; ++c)
    {
        float* out = io.out[c];
        if (out == nullptr)
            continue;
        const float* in = nIn > 0 ? io.in[c < nIn ? c : nIn - 1] : nullptr;
        if (in == nullptr)
            for (int i = 0; i < io.n; ++i)
                out[i] = 0.0f;
        else if (in != out)
            for (int i = 0; i < io.n; ++i)
                out[i] = in[i];
    }
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
    const bool keyActive = s.cfg.keyChans > 0 && io.key != nullptr && io.numKey > 0 && io.key[0] != nullptr;
    const bool keyExt = bp.extKey && keyActive;
    const int nKey = io.numKey < 2 ? io.numKey : 2;

    const ModeSlot& ms = resolveSlot(bp.slot);
    if (ms.entry != nullptr)
    {
        const KernelKey k{ ms.slot, eng.topo, eng.det, eng.stmode, eng.voice, static_cast<uint8_t>(keyExt ? 1 : 0) };
        if (!(k == s.activePath().key))
            s.swapTo(*ms.entry, k, eng);                // snapped (01 §5.5 without the fade: F7)
    }
    PathState& p = s.activePath();
    p.eng = eng;
    p.engine->setParams(eng);
    p.gain.setTarget(gainTarget(eng, *p.engine));
    s.mix.setTarget(simd::set1(eng.mix));
    s.bypass.setTarget(bp.bypass || io.hostBypassed);
    if (snap)
    {
        p.engine->snapParams();
        s.snapGain(s.active);
        s.snapMix();
        s.bypass.snap();
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
    const uint32_t colBits = static_cast<uint32_t>(p.key.slot) << host::kColSlotShift;

    int replaced = 0;
    bool poisoned = false;

    // ---- 2. chunks on the absolute kChunk grid ----------------------------------------------------------------------
    for (int off = 0; off < io.n;)
    {
        const int toGrid = kChunk - static_cast<int>(s.sampleIndex % static_cast<uint64_t>(kChunk));
        const int n = io.n - off < toGrid ? io.n - off : toGrid;
        const int n4 = roundUp4(n);
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
        if (keyExt)
        {
            replaced += sanitize(io.key[0] + off, s.key[0].data(), n);
            if (nKey > 1 && io.key[1] != nullptr)
                replaced += sanitize(io.key[1] + off, s.key[1].data(), n);
            else
                for (int i = 0; i < n; ++i)
                    s.key[1][static_cast<std::size_t>(i)] = s.key[0][static_cast<std::size_t>(i)];
        }

        // b. host smoothers, per sample
        for (int i = 0; i < n; ++i)
        {
            const simd::f32x4 g = s.tickGain();
            const auto u = static_cast<std::size_t>(i);
            s.pre[u] = simd::lane<0>(g);
            s.mk[u] = simd::lane<1>(g);
            s.mixv[u] = simd::lane<0>(s.tickMix());
        }

        // c. side chain: {L, R, L, R}; x linFromDb(preGainDb) when internal, a key never pre-gained
        if (keyExt)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                s.sc[u] = vec4(s.key[0][u], s.key[1][u], s.key[0][u], s.key[1][u]);
            }
        }
        else
        {
            linFromDbArray(s.pre.data(), s.lin.data(), n4);
            for (int i = 0; i < n; ++i)
            {
                const auto u = static_cast<std::size_t>(i);
                s.sc[u] = simd::mul(vec4(s.dry[0][u], s.dry[1][u], s.dry[0][u], s.dry[1][u]), simd::set1(s.lin[u]));
            }
        }

        // d. control
        ControlIo cio;
        cio.n = n;
        cio.sampleIndex = s.sampleIndex;
        cio.sc = s.sc.data();
        cio.grDb = s.gr.data();
        if (detail)
        {
            cio.detDb = s.det.data();
            cio.tgtDb = s.tgt.data();
            cio.s2GrDb = s.s2.data();
            cio.bits = s.bits.data();
        }
        cio.keyExternal = keyExt;
        p.engine->control(cio);
        if (tap != nullptr)
            writeTap(*tap, s.sampleIndex, n, s.gr.data(), s.det.data(), s.tgt.data(), s.s2.data(), s.bits.data());

        // e. gain (preGain folded in: the wet path only), colour, makeup
        for (int i = 0; i < n; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            s.grCh[0][u] = simd::lane<0>(s.gr[u]);
            s.grCh[1][u] = simd::lane<1>(s.gr[u]);
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n4; i += 4)
            {
                const auto u = static_cast<std::size_t>(i);
                const simd::f32x4 g = linFromDb(simd::sub(simd::load(&s.pre[u]), simd::load(&s.grCh[c][u])));
                simd::store(&s.wet[c][u], simd::mul(simd::load(&s.dry[c][u]), g));
            }
        if (attached)
            for (int c = 0; c < 2; ++c)
                s.colIn[c] = s.wet[c];
        float* wetPtr[2] = { s.wet[0].data(), s.wet[1].data() };
        const float* grPtr[2] = { s.grCh[0].data(), s.grCh[1].data() };
        AudioIo aio;
        aio.nOs = n;
        aio.wet = wetPtr;
        aio.grDbOs = grPtr;
        p.engine->colour(aio);

        // f. makeup and mix at the base rate, against the never pre-gained dry
        linFromDbArray(s.mk.data(), s.lin.data(), n4);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n4; i += 4)
            {
                const auto u = static_cast<std::size_t>(i);
                const simd::f32x4 m = simd::load(&s.mixv[u]);
                const simd::f32x4 w = simd::mul(simd::load(&s.wet[c][u]), simd::load(&s.lin[u]));
                const simd::f32x4 d = simd::mul(simd::sub(simd::set1(1.0f), m), simd::load(&s.dry[c][u]));
                simd::store(&s.y[c][u], simd::fma(d, m, w));                 // (1 - mix) dry + mix wet
            }

        // g. poison (01 §5.8), then the bypass ramp against the (latency-aligned) dry input
        const bool ok = allFinite(s.y[0].data(), n) && allFinite(s.y[1].data(), n) && p.engine->finite()
                     && s.smoothersFinite();
        if (!ok)
        {
            poisoned = true;
            s.snapPending = true;
            p.engine->reset();
            for (int c = 0; c < 2; ++c)
            {
                s.y[c] = s.dry[c];
                s.colIn[c].fill(0.0f);
            }
            s.sc.fill(simd::set1(0.0f));
            s.gr.fill(simd::set1(0.0f));
            s.det.fill(simd::set1(0.0f));
            s.tgt.fill(simd::set1(0.0f));
            s.s2.fill(simd::set1(0.0f));
            s.bits.fill(0);
            s.pre.fill(0.0f);
        }
        const float* procPtr[2] = { s.y[0].data(), s.y[1].data() };
        const float* dryPtr[2] = { s.dry[0].data(), s.dry[1].data() };
        float* outPtr[2] = { s.y[0].data(), s.y[1].data() };
        s.bypass.apply(procPtr, dryPtr, outPtr, 2, n, s.amount.data());
        for (int c = 0; c < nOut; ++c)
            if (io.out[c] != nullptr)
                for (int i = 0; i < n; ++i)
                    io.out[c][off + i] = s.y[c][static_cast<std::size_t>(i)];

        // h. telemetry
        if (attached)
        {
            host::TelemetryChunk t;
            t.n = n;
            for (int c = 0; c < 2; ++c)
            {
                t.in[c] = s.dry[c].data();
                t.out[c] = s.y[c].data();
                t.colourIn[c] = s.colIn[c].data();
            }
            t.preGainDb = s.pre.data();
            t.sc = s.sc.data();
            t.grDb = s.gr.data();
            t.detDb = s.det.data();
            t.tgtDb = s.tgt.data();
            t.s2GrDb = s.s2.data();
            t.bits = s.bits.data();
            s.telem.accumulate(t, s.sampleIndex, colBits, s.internal0, history_);
        }
        else
            s.telem.advance(s.sampleIndex, n);

        s.sampleIndex += static_cast<uint64_t>(n);
        off += n;
    }

    // ---- 3. publish -------------------------------------------------------------------------------------------------
    if (!attached)
        return;
    UiFrame f{};
    f.publishCount = ++s.publishCount;
    f.modeSlot = p.key.slot;
    f.fadeFromSlot = p.key.slot;
    uint32_t flags = s.telem.publish(io.n, f);
    if (bp.bypass || io.hostBypassed)
        flags |= kUiBypassed;
    if (keyExt)
        flags |= kUiExtKeyActive;
    if (eng.topo == kTopoFB && !keyExt)
        flags |= kUiTopoFB;
    if ((eng.flags & kEngGrOff) != 0)
        flags |= kUiGrOff;
    if (poisoned || replaced > 0)
        flags |= kUiPoisonReset;
    f.flags = flags;
    f.sampleRate = s.fs;
    f.latencySamples = static_cast<uint32_t>(s.latency);
    f.fadeProgress = 1.0f;
    f.bypassAmt = s.bypass.position();
    f.historyWritten = static_cast<uint32_t>(history_.written());

    EngineTelemetry et;
    p.engine->telemetry(et);
    for (int c = 0; c < 2; ++c)
    {
        f.attackNowMs[c] = finiteOr0(et.attackNowMs[c]);
        f.releaseNowMs[c] = finiteOr0(et.releaseNowMs[c]);
        f.crestDb[c] = finiteOr0(et.crestDb[c]);
    }
    f.preGainDb = finiteOr0(simd::lane<0>(s.gainOut[s.active].cur));
    f.thrDb = eng.thrDb;
    f.slope = eng.slope;
    f.kneeDb = eng.kneeDb;
    f.rangeDb = eng.rangeDb;
    f.atkTauMs = eng.atkTauMs;
    f.relTauMs = eng.relTauMs;
    f.holdMs = eng.holdMs;
    f.lookMs = eng.lookMs;
    f.driveDb = eng.driveDb;
    f.makeupEffDb = finiteOr0(simd::lane<1>(s.gainOut[s.active].cur));
    f.mix = finiteOr0(simd::lane<0>(s.mixOut.cur));
    f.scHpfHz = eng.scHpfHz;
    f.sceDbOct = eng.sceDbOct;
    f.link = eng.link;
    f.s2ThrDb = eng.s2ThrDb;
    f.s2AtkTauMs = eng.s2AtkTauMs;
    f.s2RelTauMs = eng.s2RelTauMs;
    f.tags = eng.tags;
    f.discrete = static_cast<uint32_t>(eng.det) | static_cast<uint32_t>(eng.stmode) << 8
               | static_cast<uint32_t>(eng.voice) << 16 | static_cast<uint32_t>(eng.tmode) << 24;
    p.engine->internals(f.internals);
    for (float& v : f.internals)
        v = finiteOr0(v);
    uiFrame_.publish(f);
}

void EngineHost::reset() noexcept FCDSP_NONBLOCKING
{
    Impl* const sp = impl_.get();
    if (sp == nullptr)
        return;
    PathState& p = sp->activePath();
    if (p.engine != nullptr)
        p.engine->reset();
    sp->snapGain(sp->active);
    sp->snapMix();
    sp->bypass.snap();
    sp->telem.attach();                                 // meters and the open column restart; next column is a gap
    sp->internal0 = sp->latchInternal();
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
