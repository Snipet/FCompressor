// Source/web/engine/WebEngine.cpp: see WebEngine.h (the C ABI) and WebProtocol.h (the records).
//
// What the plugin's processor does around fcdsp::EngineHost, restated for one thread and no host (Processor.cpp is the
// model; the web facade's native probes keep the two in step):
//   - the 30 plain values of the last Params record are the raw state. Each record rebuilds the BlockParams as
//     Processor::buildBlockParams does: the 22 Mode-filtered values, resolveSlot(mode), the CONFIGURED lookahead
//     budget -> resolve() -> eng, then the globals (bypass, delta, listen, extkey, output). Every process() call runs
//     the BlockParams of the last record, so a record is the processor's per-block pull;
//   - quality and labudget are the setup (Processor::prepareToPlay, SetupWatcher::poll): a record that changes either
//     reconfigures the engine here, in the message handler, between two quanta (WebEngine.h, "Threads");
//   - a record's snap flag is the outermost endBatch(): EngineHost::requestSnap(), consumed at the next block start,
//     after the new values are in place.
// What the browser adds: denormal input samples are read as zero, and the silence gate (WebEngine.h).
#include "web/engine/WebEngine.h"

#include "web/engine/WebProtocol.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <vector>

struct FcmpWebEngine
{
    fcdsp::EngineHost  host;
    fcdsp::HostConfig  cfg;                             // fs, maxBlock, quality, budget: what the engine runs
    fcdsp::BlockParams block;                           // what the next quantum runs
    fcdsp::Resolution  resolution;                      // resolve()'s scratch
    std::array<float, fcdsp::kNumParams> plain {};      // the raw state: plain values, Pid order
    std::vector<float> scratch[3];                      // sanitised input L, R and the sink of a dropped right output
    int  cap = 0;                                       // frames per engine call (the scratch size)
    int  latency = 0;
    bool configured = false;
    bool attached = false;
    bool gateOn = true;
    bool gated = false;                                 // the gate is closed: the engine is reset and not running
    std::uint64_t silentFrames = 0;                     // exactly-zero input frames since the last sample or record
    std::uint64_t tailFrames = 0;                       // the gate's threshold at the current values, in frames
    std::uint64_t historyNext = 0;                      // the first HistoryRing column the next reply delivers
    fcmp::web::Reply reply {};
};

namespace
{
    using fcdsp::Pid;
    using fcdsp::idx;
    using Engine = FcmpWebEngine;

    constexpr int kMaxCap = 8192;                       // frames per engine call; longer calls are split
    constexpr double kMaxTailSeconds = 600.0;
    // The gate never closes sooner than this after the last sample or record: longer than a Mode crossfade the host
    // had to hold back (host/Crossfade.h: starts are 50 ms apart, a fade takes 20 ms) and than any 20 ms ramp, so the
    // shortest tails (five times a 1 ms release) cannot close it on a change that is still in flight.
    constexpr double kGateHoldSeconds = 0.1;

    // ---- raw values -> setup and BlockParams (Processor.cpp's indexOf, qualityOf, budgetOf, modeSlotOf, isOn) ------
    int indexOf(float plain, int hi) noexcept
    {
        if (!(plain > 0.0f))                            // also NaN
            return 0;
        if (plain >= static_cast<float>(hi))
            return hi;
        return static_cast<int>(plain + 0.5f);
    }

    fcdsp::Quality qualityOf(float plain) noexcept { return static_cast<fcdsp::Quality>(indexOf(plain, 2)); }
    fcdsp::LookaheadBudget budgetOf(float plain) noexcept
    {
        return static_cast<fcdsp::LookaheadBudget>(indexOf(plain, 2));
    }
    int modeSlotOf(float plain) noexcept { return indexOf(plain, fcdsp::kModeCapacity - 1); }
    bool isOn(float plain) noexcept { return plain >= 0.5f; }

    // Processor::snapshot + globals + buildBlockParams.
    void buildBlock(Engine& e) noexcept
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = e.plain[i];
        raw.modeSlot = fcdsp::resolveSlot(modeSlotOf(e.plain[idx(Pid::mode)])).slot;
        raw.budget = e.cfg.budget;                      // the configured budget (K1 #8)

        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
        e.block.slot = ms.slot;
        if (ms.entry != nullptr)
        {
            fcdsp::resolve(*ms.entry, raw, e.resolution);
            e.block.eng = e.resolution.eng;
        }
        else
            e.block.eng = fcdsp::EngineParams {};       // empty registry: the host passes audio through
        e.block.bypass = isOn(e.plain[idx(Pid::bypass)]);
        e.block.delta = isOn(e.plain[idx(Pid::delta)]);
        e.block.listen = isOn(e.plain[idx(Pid::listen)]);
        e.block.extKey = isOn(e.plain[idx(Pid::extkey)]);   // no key input here: the host ignores it
        e.block.outputDb = e.plain[idx(Pid::output)];
    }

    // The gate's threshold: the engine's own tail at the current values (the descriptor's tail plus the latency), and
    // never less than kGateHoldSeconds. Message handler and configure only: tailSeconds is not real-time code.
    void updateTail(Engine& e) noexcept
    {
        double t = e.host.tailSeconds(e.block);
        if (!(t >= kGateHoldSeconds))                   // also NaN
            t = kGateHoldSeconds;
        if (t > kMaxTailSeconds)
            t = kMaxTailSeconds;
        e.tailFrames = static_cast<std::uint64_t>(t * e.cfg.fs) + 1u;
    }

    bool configureHost(Engine& e) noexcept
    {
        try
        {
            e.host.configure(e.cfg, e.block);           // allocates; the engine starts snapped and cleared
        }
        catch (...)
        {
            return false;                               // the previous engine, if any, is untouched
        }
        e.latency = e.host.latencySamples();
        return true;
    }

    // ---- the audio path's helpers: bit operations only ---------------------------------------------------------------
    // Copies n samples, a denormal becoming a zero of its sign (wasm has no denormals-are-zero mode, and a denormal
    // that reaches a recursive filter stays in it). Returns the OR of the magnitude bits: 0 when every sample is zero.
    std::uint32_t copyClean(const float* in, float* out, int n) noexcept FCDSP_NONBLOCKING
    {
        std::uint32_t any = 0;
        for (int i = 0; i < n; ++i)
        {
            std::uint32_t bits;
            std::memcpy(&bits, in + i, sizeof bits);
            if ((bits & 0x7f800000u) == 0u)
                bits &= 0x80000000u;
            any |= bits & 0x7fffffffu;
            std::memcpy(out + i, &bits, sizeof bits);
        }
        return any;
    }

    void zero(float* out, int n) noexcept FCDSP_NONBLOCKING
    {
        for (int i = 0; i < n; ++i)
            out[i] = 0.0f;
    }

    // ---- messages ----------------------------------------------------------------------------------------------------
    std::int32_t applyParams(Engine& e, const fcmp::web::ParamsMsg& m) noexcept
    {
        const auto before = e.plain;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const float v = m.plain[i];
            e.plain[i] = v == v ? v : fcdsp::kHostParams[i].def;   // a NaN is the parameter's default, as the host maps
        }
        const fcdsp::Quality quality = qualityOf(e.plain[idx(Pid::quality)]);
        const fcdsp::LookaheadBudget budget = budgetOf(e.plain[idx(Pid::labudget)]);
        const bool setupChanged = quality != e.cfg.quality || budget != e.cfg.budget;
        const fcdsp::HostConfig cfgBefore = e.cfg;
        e.cfg.quality = quality;
        e.cfg.budget = budget;
        buildBlock(e);                                  // with the budget just set (Processor::configureEngine)

        if (setupChanged && e.configured)
        {
            // The deviation ADR-93 records: EngineHost::configure, the allocation point, runs here, on the worklet's
            // only thread, between two quanta. The new engine starts snapped at these values.
            if (!configureHost(e))
            {
                e.plain = before;
                e.cfg = cfgBefore;
                buildBlock(e);
                return fcmp::web::kPostFailed;
            }
        }
        else if (e.gated || m.snap != 0u)
            e.host.requestSnap();                       // consumed at the next block start, with the new values; an
                                                        // idle engine wakes at them (it was reset: nothing ramps)
        if (!e.configured)
            e.latency = fcdsp::EngineHost::latencyFor(e.cfg);   // what configure will give (at its sample rate)
        updateTail(e);
        // A record that changes a value is activity for the gate, as a sample is. The plugin's engine runs through
        // silence, so a Mode change made there has crossfaded, and every ramp has ended, before signal returns; here
        // the engine runs the new values on silence for the whole threshold (the new one) before the gate may close
        // again. Without this a Mode change made while idle would crossfade into the returning signal, and a record
        // that shortens the tail could close the gate before the engine had run a block of it. A record that repeats
        // the values is not activity: a page that posts every frame must not hold the gate open for good.
        if (std::memcmp(e.plain.data(), before.data(), sizeof(float) * e.plain.size()) != 0)
        {
            e.silentFrames = 0;
            e.gated = false;
        }
        return 0;
    }

    void applyAttach(Engine& e, bool attached) noexcept
    {
        if (attached == e.attached)
            return;                                     // the host's attach is a count: only transitions reach it
        e.attached = attached;
        e.host.setUiAttached(attached);
        if (attached)
            e.historyNext = e.host.history().written(); // columns from now on; the first one carries the gap bit
    }

    // EngineHost::reset keeps a snap a record asked for (it is consumed at the next block start), so values posted
    // while the gate was closed still start settled after a Reset.
    void applyReset(Engine& e) noexcept
    {
        e.host.reset();
        e.silentFrames = 0;
        e.gated = false;
    }

    std::int32_t fillReply(Engine& e, std::uint32_t tag) noexcept
    {
        using namespace fcmp::web;
        Reply& r = e.reply;
        std::uint32_t flags = 0, count = 0;
        std::uint64_t first = e.historyNext;
        if (e.attached)
        {
            const fcdsp::HistoryRing& ring = e.host.history();
            first = ring.read(e.historyNext, std::span<fcdsp::HistoryColumn>(r.columns), count);
            if (first > e.historyNext)
                flags |= kReplyGap;
            e.historyNext = first + count;
            if (e.historyNext < ring.written())
                flags |= kReplyMore;
            flags |= kReplyAttached;
        }
        ReplyHead& head = r.head;
        if (e.host.readUiFrame(head.frame))             // else the previous frame stays in the buffer
            flags |= kReplyFrame;
        if (e.gated)
            flags |= kReplyGated;
        if (e.configured)
            flags |= kReplyConfigured;
        head.flags = flags;
        head.latencySamples = static_cast<std::uint32_t>(e.latency);
        head.columnCount = count;
        head.firstColumn = static_cast<std::uint32_t>(first);
        head.h = header(Kind::reply, replyBytes(count), tag);
        return static_cast<std::int32_t>(head.h.bytes);
    }

    // ---- the self-check's material -----------------------------------------------------------------------------------
    // Integer and single IEEE operations only (no libm, no fused operation: the build has -ffp-contract=off), so the
    // input is the same bits on every target.
    constexpr int kSelfQuantum = 128;
    constexpr int kSelfQuanta = 32;                     // per segment: 4096 frames, 85 ms at 48 kHz
    constexpr std::string_view kSelfModes[] = { "clean", "fet-76", "opto-2a", "octo", "brickwall" };

    struct SelfSignal
    {
        std::uint64_t state[2] = { 0x66636d7031ull, 0x66636d7032ull };
        std::uint32_t n = 0;

        float noise(int ch) noexcept                    // [-1, 1) on a 2^-23 grid (an LCG's high bits)
        {
            std::uint64_t& s = state[ch];
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<float>(static_cast<std::uint32_t>(s >> 40)) * 0x1p-23f - 1.0f;
        }

        static float triangle(std::uint32_t phase) noexcept   // period 96 samples (500 Hz at 48 kHz), [-1, 1]
        {
            const auto p = static_cast<int>(phase % 96u);
            return static_cast<float>(p < 48 ? p - 24 : 72 - p) / 24.0f;
        }

        void fill(float* l, float* r, int frames) noexcept
        {
            for (int i = 0; i < frames; ++i, ++n)
            {
                const float env = (n / 1536u) % 2u == 0u ? 0.5f : 0.03125f;     // loud and quiet, 32 ms each
                l[i] = env * (0.5f * triangle(n) + 0.5f * noise(0));
                r[i] = env * (0.5f * triangle(n + 24u) + 0.5f * noise(1));
            }
        }
    };

    struct Fnv                                          // FNV-1a 64 over little-endian bytes (the probes' hashFloats)
    {
        std::uint64_t h = 1469598103934665603ull;

        void word(std::uint32_t w) noexcept
        {
            for (int k = 0; k < 4; ++k)
            {
                h ^= (w >> (k * 8)) & 0xffu;
                h *= 1099511628211ull;
            }
        }

        void floats(const float* v, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                std::uint32_t bits;
                std::memcpy(&bits, v + i, sizeof bits);
                word(bits);
            }
        }
    };

    template <class Msg>
    std::int32_t post(Engine* e, const Msg& m) noexcept
    {
        std::uint8_t bytes[sizeof(Msg)];
        std::memcpy(bytes, &m, sizeof(Msg));
        return fcmp_web_post(e, bytes, static_cast<std::int32_t>(sizeof(Msg)));
    }

    // The reply's layout-dependent words (not its meters: their last bits are not part of the audio contract).
    bool selfPull(Engine* e, std::uint32_t tag, Fnv& f) noexcept
    {
        using namespace fcmp::web;
        const std::int32_t bytes = post(e, PullMsg { header(Kind::pull, sizeof(PullMsg), tag) });
        if (bytes < static_cast<std::int32_t>(kReplyFixedBytes))
            return false;
        ReplyHead head;
        std::memcpy(static_cast<void*>(&head), fcmp_web_reply(e), sizeof head);
        if (head.h.magic != kMagic || head.h.kind != static_cast<std::uint16_t>(Kind::reply) || head.h.tag != tag
            || head.h.bytes != static_cast<std::uint32_t>(bytes)
            || bytes != static_cast<std::int32_t>(replyBytes(head.columnCount)))
            return false;
        f.word(head.h.bytes);
        f.word(head.flags);
        f.word(head.latencySamples);
        f.word(head.columnCount);
        f.word(head.firstColumn);
        f.word(head.frame.publishCount);
        f.word(head.frame.modeSlot);
        f.word(head.frame.latencySamples);
        f.floats(&head.frame.sampleRate, 1);
        return true;
    }

    void selfRender(Engine* e, SelfSignal& sig, Fnv& f) noexcept
    {
        float inL[kSelfQuantum], inR[kSelfQuantum], outL[kSelfQuantum], outR[kSelfQuantum];
        for (int q = 0; q < kSelfQuanta; ++q)
        {
            sig.fill(inL, inR, kSelfQuantum);
            fcmp_web_process(e, inL, inR, outL, outR, kSelfQuantum);
            f.floats(outL, kSelfQuantum);
            f.floats(outR, kSelfQuantum);
        }
    }

    bool selfMode(Engine* e, const fcdsp::ModeEntry& entry, std::uint32_t tag, Fnv& f) noexcept
    {
        using namespace fcmp::web;
        // The Mode's own defaults over the host defaults: plain values from tables, no host map.
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = fcdsp::kHostParams[i].def;
        fcdsp::modeDefaults(*entry.desc, raw);
        ParamsMsg m {};
        m.h = header(Kind::params, sizeof(ParamsMsg), tag);
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            m.plain[i] = i < fcdsp::kNumModeParams ? raw.v[i] : fcdsp::kHostParams[i].def;
        m.plain[idx(Pid::mode)] = static_cast<float>(fcdsp::slotOf(entry));
        m.snap = 1u;

        // Segment 1: the plugin's default setup (STD, no lookahead), 48 kHz, 128-frame quanta.
        if (post(e, m) != 0)
            return false;
        const std::int32_t latency1 = fcmp_web_configure(e, 48000.0, kSelfQuantum);
        if (latency1 < 0)
            return false;
        f.word(static_cast<std::uint32_t>(latency1));
        SelfSignal sig;
        selfRender(e, sig, f);
        if (!selfPull(e, tag, f))
            return false;

        // Segment 2: an edit. Nothing in the setup changes, so the values ramp.
        m.plain[idx(Pid::thr)] = -30.0f;
        m.plain[idx(Pid::mix)] = 0.75f;
        m.plain[idx(Pid::output)] = -3.0f;
        m.snap = 0u;
        if (post(e, m) != 0)
            return false;
        selfRender(e, sig, f);
        if (!selfPull(e, tag + 1u, f))
            return false;

        // Segment 3: HQ and a 5 ms lookahead budget, with 2 ms of lookahead: the message handler reconfigures.
        m.plain[idx(Pid::look)] = 2.0f;
        m.plain[idx(Pid::quality)] = 2.0f;
        m.plain[idx(Pid::labudget)] = 1.0f;
        if (post(e, m) != 0)
            return false;
        f.word(static_cast<std::uint32_t>(fcmp_web_latency(e)));
        selfRender(e, sig, f);
        return selfPull(e, tag + 2u, f);
    }
} // namespace

extern "C" {

std::int32_t fcmp_web_abi_version(void) noexcept
{
    return static_cast<std::int32_t>(fcmp::web::kVersion);
}

FcmpWebEngine* fcmp_web_create(void) noexcept
{
    FcmpWebEngine* const e = new (std::nothrow) FcmpWebEngine;
    if (e == nullptr)
        return nullptr;
    for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        e->plain[i] = fcdsp::kHostParams[i].def;
    e->cfg.fs = 48000.0;
    e->cfg.maxBlock = 128;
    e->cfg.quality = qualityOf(e->plain[idx(Pid::quality)]);
    e->cfg.budget = budgetOf(e->plain[idx(Pid::labudget)]);
    e->cfg.mainIns = 2;
    e->cfg.mainOuts = 2;
    e->cfg.keyChans = 0;
    buildBlock(*e);
    updateTail(*e);
    e->latency = fcdsp::EngineHost::latencyFor(e->cfg);
    return e;
}

void fcmp_web_destroy(FcmpWebEngine* engine) noexcept
{
    delete engine;
}

std::int32_t fcmp_web_configure(FcmpWebEngine* engine, double sampleRate, std::int32_t maxBlock) noexcept
{
    if (engine == nullptr)
        return -1;
    Engine& e = *engine;
    e.cfg.fs = sampleRate > 0.0 && sampleRate < 1.0e6 ? sampleRate : 48000.0;   // also NaN
    e.cfg.maxBlock = maxBlock < 1 ? 1 : maxBlock > kMaxCap ? kMaxCap : maxBlock;
    e.cfg.quality = qualityOf(e.plain[idx(Pid::quality)]);
    e.cfg.budget = budgetOf(e.plain[idx(Pid::labudget)]);
    buildBlock(e);
    try
    {
        for (std::vector<float>& v : e.scratch)
            v.assign(static_cast<std::size_t>(e.cfg.maxBlock), 0.0f);
    }
    catch (...)
    {
        e.configured = false;
        e.cap = 0;
        return -1;
    }
    e.cap = e.cfg.maxBlock;
    if (!configureHost(e))
    {
        e.configured = false;
        return -1;
    }
    e.configured = true;
    e.silentFrames = 0;
    e.gated = false;
    updateTail(e);
    return e.latency;
}

void fcmp_web_process(FcmpWebEngine* engine, const float* inL, const float* inR, float* outL, float* outR,
                      std::int32_t frames) noexcept FCDSP_NONBLOCKING
{
    if (engine == nullptr || outL == nullptr || frames <= 0)
        return;
    Engine& e = *engine;
    if (!e.configured)
    {
        zero(outL, frames);
        if (outR != nullptr)
            zero(outR, frames);
        return;
    }
    if (inR == nullptr)
        inR = inL;
    float* const l = e.scratch[0].data();
    float* const r = e.scratch[1].data();
    for (int off = 0; off < frames;)
    {
        const int n = frames - off < e.cap ? frames - off : e.cap;
        std::uint32_t any = 0;
        if (inL != nullptr)
        {
            any |= copyClean(inL + off, l, n);
            any |= copyClean(inR + off, r, n);
        }
        else
        {
            zero(l, n);
            zero(r, n);
        }

        // The gate closes once the whole tail has been rendered: `tailFrames` zero frames have gone through the
        // engine before this chunk, and this chunk is zero too.
        const bool tailDone = e.silentFrames >= e.tailFrames;
        if (any == 0u)
            e.silentFrames += static_cast<std::uint64_t>(n);
        else
            e.silentFrames = 0;

        float* const oL = outL + off;
        float* const oR = outR != nullptr ? outR + off : e.scratch[2].data();
        if (e.gateOn && tailDone && any == 0u)
        {
            if (!e.gated)
            {
                e.host.reset();
                e.gated = true;
            }
            zero(oL, n);
            zero(oR, n);
        }
        else
        {
            e.gated = false;                            // signal: the reset engine starts on it, at the last values
            const float* const ins[2] = { l, r };
            float* const outs[2] = { oL, oR };
            fcdsp::ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = n;
            e.host.process(io, e.block);
        }
        off += n;
    }
}

std::int32_t fcmp_web_post(FcmpWebEngine* engine, const std::uint8_t* bytes, std::int32_t n) noexcept
{
    using namespace fcmp::web;
    if (engine == nullptr || bytes == nullptr || n < static_cast<std::int32_t>(sizeof(Header)))
        return kPostBadArgument;
    Header h;
    std::memcpy(&h, bytes, sizeof h);
    if (h.magic != kMagic)
        return kPostBadMagic;
    if (h.version != kVersion)
        return kPostBadVersion;
    const auto size = static_cast<std::uint32_t>(n);
    const auto is = [&h](Kind k) noexcept { return h.kind == static_cast<std::uint16_t>(k); };
    if (is(Kind::params))
    {
        if (h.bytes != size || size != sizeof(ParamsMsg))
            return kPostBadSize;
        ParamsMsg m;
        std::memcpy(static_cast<void*>(&m), bytes, sizeof m);
        return applyParams(*engine, m);
    }
    if (is(Kind::attach))
    {
        if (h.bytes != size || size != sizeof(AttachMsg))
            return kPostBadSize;
        AttachMsg m;
        std::memcpy(static_cast<void*>(&m), bytes, sizeof m);
        applyAttach(*engine, m.attached != 0u);
        return 0;
    }
    if (is(Kind::reset))
    {
        if (h.bytes != size || size != sizeof(ResetMsg))
            return kPostBadSize;
        applyReset(*engine);
        return 0;
    }
    if (is(Kind::pull))
    {
        if (h.bytes != size || size != sizeof(PullMsg))
            return kPostBadSize;
        return fillReply(*engine, h.tag);
    }
    return kPostBadKind;
}

const std::uint8_t* fcmp_web_reply(const FcmpWebEngine* engine) noexcept
{
    return engine != nullptr ? reinterpret_cast<const std::uint8_t*>(&engine->reply) : nullptr;
}

std::int32_t fcmp_web_latency(const FcmpWebEngine* engine) noexcept
{
    return engine != nullptr ? engine->latency : 0;
}

void fcmp_web_set_gate(FcmpWebEngine* engine, std::int32_t on) noexcept
{
    if (engine != nullptr)
        engine->gateOn = on != 0;
}

std::int32_t fcmp_web_selfcheck(std::uint32_t* hash) noexcept
{
    if (hash == nullptr)
        return -1;
    Fnv f;
    std::uint32_t tag = 1;
    for (const std::string_view key : kSelfModes)
    {
        const fcdsp::ModeEntry* const entry = fcdsp::byKey(key);
        if (entry == nullptr || entry->desc == nullptr)
            return -1;                                  // the self-check's Modes are part of every build
        FcmpWebEngine* const e = fcmp_web_create();
        if (e == nullptr)
            return -1;
        using namespace fcmp::web;
        const bool ok = post(e, AttachMsg { header(Kind::attach, sizeof(AttachMsg), tag), 1u }) == 0
                     && selfMode(e, *entry, tag, f);
        fcmp_web_destroy(e);
        if (!ok)
            return -1;
        tag += 3u;
    }
    hash[0] = static_cast<std::uint32_t>(f.h);
    hash[1] = static_cast<std::uint32_t>(f.h >> 32);
    return 0;
}

} // extern "C"
