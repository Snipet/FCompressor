// Tools/web/live/PrintModule.cpp: see PrintModule.h (the exports). The material and the sets are
// Tools/probes/common/PrintProgram.h's, compiled as they are; a record is laid out by Source/web/engine/WebProtocol.h;
// the hash is the one Tools/web/enginecheck.cpp restates from FunkGui's Harness. Over fcdsp alone.
#include "PrintModule.h"

#include "PrintProgram.h"

#include "web/engine/WebProtocol.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Setup.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    using namespace fcdsp;
    namespace printprog = fcmp::probe::printprog;
    namespace proto = fcmp::web;

    constexpr std::int32_t kFrames = static_cast<std::int32_t>(printprog::kSeconds * printprog::kFs);
    constexpr std::int32_t kFloorPeriod = 96;           // frames: 500 Hz at 48 kHz
    constexpr float kFloor = 0x1p-120f;                 // normal, 2^6 above the smallest normal float

    static_assert(kFrames % 256 == 0, "the program and its half are whole numbers of 128-frame quanta");

    // The i-th registered Mode in slot order (the order of web.engine.print), or nullptr.
    const ModeSlot* modeAt(std::int32_t i) noexcept
    {
        std::int32_t n = 0;
        for (const ModeSlot& ms : modeSlots())
            if (ms.entry != nullptr && ms.entry->desc != nullptr && n++ == i)
                return &ms;
        return nullptr;
    }

    char keyBuf[32];                                    // fcmp_print_key's answer
}

extern "C" {

std::int32_t fcmp_print_frames(void) noexcept
{
    return kFrames;
}

void fcmp_print_program(float* l, float* r) noexcept
{
    const printprog::Program p = printprog::program();
    std::memcpy(l, p.l.data(), p.l.size() * sizeof(float));
    std::memcpy(r, p.r.data(), p.r.size() * sizeof(float));
}

void fcmp_print_floor(float* l, float* r) noexcept
{
    fcmp_print_program(l, r);
    for (std::int32_t i = kFrames / 2; i < kFrames; ++i)
    {
        const float v = i % kFloorPeriod < kFloorPeriod / 2 ? kFloor : -kFloor;
        l[i] = v;
        r[i] = -v;
    }
}

std::int32_t fcmp_print_modes(void) noexcept
{
    std::int32_t n = 0;
    while (modeAt(n) != nullptr)
        ++n;
    return n;
}

const char* fcmp_print_key(std::int32_t mode) noexcept
{
    const ModeSlot* const ms = modeAt(mode);
    if (ms == nullptr)
        return nullptr;
    const std::size_t n = ms->key.size() < sizeof keyBuf - 1 ? ms->key.size() : sizeof keyBuf - 1;
    std::memcpy(keyBuf, ms->key.data(), n);
    keyBuf[n] = 0;
    return keyBuf;
}

const char* fcmp_print_set(std::int32_t set) noexcept
{
    const ModeSlot* const ms = modeAt(0);               // the names are the same for every Mode: string literals
    if (ms == nullptr || set < 0 || set > 3)
        return nullptr;
    return printprog::sets(*ms->entry)[static_cast<std::size_t>(set)].name;
}

std::int32_t fcmp_print_record(std::int32_t mode, std::int32_t set, float quality, float budget, float lookMs,
                               std::int32_t snap, std::uint8_t* out) noexcept
{
    const ModeSlot* const ms = modeAt(mode);
    if (ms == nullptr || set < 0 || set > 3 || out == nullptr)
        return -1;
    const RawParams raw = printprog::sets(*ms->entry)[static_cast<std::size_t>(set)].raw;
    proto::ParamsMsg m {};
    m.h = proto::header(proto::Kind::params, sizeof m);
    // Tools/web/enginecheck.cpp's plainOf: the Mode-filtered values and the Mode, the other globals at their defaults.
    for (std::size_t k = 0; k < kNumParams; ++k)
        m.plain[k] = k < kNumModeParams ? raw.v[k] : kHostParams[k].def;
    m.plain[idx(Pid::mode)] = static_cast<float>(raw.modeSlot);
    m.plain[idx(Pid::quality)] = quality;
    m.plain[idx(Pid::labudget)] = budget;
    if (lookMs >= 0.0f)
        m.plain[idx(Pid::look)] = lookMs;
    m.snap = snap != 0 ? 1u : 0u;
    std::memcpy(out, &m, sizeof m);
    return static_cast<std::int32_t>(sizeof m);
}

std::int32_t fcmp_print_attach(std::int32_t attached, std::uint8_t* out) noexcept
{
    const proto::AttachMsg m { proto::header(proto::Kind::attach, sizeof(proto::AttachMsg)), attached != 0 ? 1u : 0u };
    std::memcpy(out, &m, sizeof m);
    return static_cast<std::int32_t>(sizeof m);
}

std::int32_t fcmp_print_pull(std::uint8_t* out) noexcept
{
    const proto::PullMsg m { proto::header(proto::Kind::pull, sizeof(proto::PullMsg)) };
    std::memcpy(out, &m, sizeof m);
    return static_cast<std::int32_t>(sizeof m);
}

std::int32_t fcmp_print_running(const std::uint8_t* reply, std::int32_t n, std::int32_t blocks) noexcept
{
    proto::ReplyHead head;
    if (reply == nullptr || n < static_cast<std::int32_t>(sizeof head))
        return -1;
    std::memcpy(static_cast<void*>(&head), reply, sizeof head);
    if (head.h.magic != proto::kMagic || head.h.version != proto::kVersion
        || head.h.kind != static_cast<std::uint16_t>(proto::Kind::reply)
        || head.h.bytes != proto::replyBytes(head.columnCount))
        return -1;
    const std::uint32_t need = proto::kReplyFrame | proto::kReplyConfigured | proto::kReplyAttached;
    return (head.flags & need) == need && (head.flags & proto::kReplyGated) == 0u
        && head.frame.publishCount == static_cast<std::uint32_t>(blocks) ? 1 : 0;
}

std::int32_t fcmp_print_latency(std::int32_t quality, std::int32_t budget, double sampleRate) noexcept
{
    if (quality < 0 || quality > 2 || budget < 0 || budget > 2)
        return -1;
    HostConfig cfg;
    cfg.fs = sampleRate;
    cfg.quality = static_cast<Quality>(quality);
    cfg.budget = static_cast<LookaheadBudget>(budget);
    return EngineHost::latencyFor(cfg);
}

void fcmp_print_hash(const float* v, std::int32_t n, std::uint32_t* hash) noexcept
{
    std::uint64_t h = 1469598103934665603ull;
    for (std::int32_t i = 0; i < n; ++i)
    {
        std::uint32_t bits;
        std::memcpy(&bits, v + i, sizeof bits);
        for (int k = 0; k < 4; ++k)
        {
            h ^= (bits >> (k * 8)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    hash[0] = static_cast<std::uint32_t>(h);
    hash[1] = static_cast<std::uint32_t>(h >> 32);
}

void fcmp_print_tail(const float* v, std::int32_t n, std::int32_t* out) noexcept
{
    std::int32_t last = -1, denormal = 0;
    for (std::int32_t i = 0; i < n; ++i)
    {
        std::uint32_t bits;
        std::memcpy(&bits, v + i, sizeof bits);
        if ((bits & 0x7fffffffu) == 0u)
            continue;
        last = i;
        if ((bits & 0x7f800000u) == 0u)
            ++denormal;
    }
    out[0] = last;
    out[1] = denormal;
}

} // extern "C"
