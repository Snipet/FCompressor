// Tools/web/live/PrintModule.cpp: LEAD-PHASE BASE (docs/sprints/web-lead.md): the audio scout's working prototype of
// the test-only print module, as it stood. Card L-P owns it from here and brings it to the manifest (declarations in
// a header instead of the pragma below, the project's comment style).
//
// Scratch prototype (scout-l/audio): the test-only module of the browser print gate. dsp.print's program, the four
// parameter sets as WebProtocol Params records, and the Harness's hashFloats, over fcdsp alone. Never shipped.
#include "PrintProgram.h"

#pragma clang diagnostic ignored "-Wmissing-prototypes"      // PROTOTYPE: card L-P declares the exports

#include "web/engine/WebProtocol.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__wasm__)
  #define FP_EXPORT(name) __attribute__((export_name(#name)))
#else
  #define FP_EXPORT(name)
#endif

namespace
{
    using namespace fcdsp;
    namespace printprog = fcmp::probe::printprog;

    const ModeSlot* modeAt(std::int32_t i) noexcept
    {
        std::int32_t n = 0;
        for (const ModeSlot& ms : modeSlots())
            if (ms.entry != nullptr && ms.entry->desc != nullptr && n++ == i)
                return &ms;
        return nullptr;
    }
    char keyBuf[32];
}

extern "C" {

FP_EXPORT(fcmp_print_frames) std::int32_t fcmp_print_frames(void) noexcept
{
    return static_cast<std::int32_t>(printprog::kSeconds * printprog::kFs);
}

// The program into l and r (fcmp_print_frames() floats each).
FP_EXPORT(fcmp_print_program) void fcmp_print_program(float* l, float* r) noexcept
{
    const printprog::Program p = printprog::program();
    std::memcpy(l, p.l.data(), p.l.size() * sizeof(float));
    std::memcpy(r, p.r.data(), p.r.size() * sizeof(float));
}

FP_EXPORT(fcmp_print_modes) std::int32_t fcmp_print_modes(void) noexcept
{
    std::int32_t n = 0;
    while (modeAt(n) != nullptr)
        ++n;
    return n;
}

// The key of the i-th registered Mode, NUL-terminated (valid until the next call), or nullptr.
FP_EXPORT(fcmp_print_key) const char* fcmp_print_key(std::int32_t i) noexcept
{
    const ModeSlot* ms = modeAt(i);
    if (ms == nullptr)
        return nullptr;
    const std::size_t n = ms->key.size() < sizeof keyBuf - 1 ? ms->key.size() : sizeof keyBuf - 1;
    std::memcpy(keyBuf, ms->key.data(), n);
    keyBuf[n] = 0;
    return keyBuf;
}

// The name of set s (0..3): default, lo, hi, mid.
FP_EXPORT(fcmp_print_set) const char* fcmp_print_set(std::int32_t s) noexcept
{
    static constexpr const char* names[4] = { "default", "lo", "hi", "mid" };
    return s >= 0 && s < 4 ? names[s] : nullptr;
}

// One Params record (140 bytes at out): Mode i, set s, as web.engine.print's plainOf makes it, with QUALITY and the
// lookahead budget as given (plain values: 0 ECO, 1 STD, 2 HQ; 0 off, 1 5 ms, 2 20 ms) and LOOKAHEAD in ms when
// lookMs >= 0. Returns 140, or -1.
FP_EXPORT(fcmp_print_record)
std::int32_t fcmp_print_record(std::int32_t i, std::int32_t s, float quality, float budget, float lookMs,
                               std::int32_t snap, std::uint8_t* out) noexcept
{
    const ModeSlot* ms = modeAt(i);
    if (ms == nullptr || s < 0 || s > 3 || out == nullptr)
        return -1;
    const RawParams raw = printprog::sets(*ms->entry)[static_cast<std::size_t>(s)].raw;
    fcmp::web::ParamsMsg m {};
    m.h = fcmp::web::header(fcmp::web::Kind::params, sizeof m);
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

// Harness hashFloats (FNV-1a 64 over the little-endian bytes of each float), as two halves, hash[0] the low one.
FP_EXPORT(fcmp_print_hash) void fcmp_print_hash(const float* v, std::int32_t n, std::uint32_t* hash) noexcept
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

} // extern "C"
