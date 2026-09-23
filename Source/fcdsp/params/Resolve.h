#pragma once

// The one resolver (01 §4.4-4.5): raw host-plain values -> what the active Mode shows (ParamView) and what the engine
// runs (EngineParams). Pure and thread-agnostic: the audio thread (per block), the UI (per frame), the host text
// lambdas and the probes all call it. Snap on read: a Mode change never writes a parameter other than `mode`.
// Sprint-frozen; F2 (S1) implements Resolve.cpp.

#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Setup.h"
#include <array>
#include <cstdint>

namespace fcdsp {

struct ModeDescriptor;                             // ModeDescriptor.h
struct ModeEntry;                                  // Registry.h

enum class SlotState : uint8_t { live, stepped, locked, derived, na };   // F §0.6 five states

struct ResolvedParam {
    float     plain = 0;         // canonical host-plain value after snap/lock/derive
    float     display = 0;       // in the Mode's display scale (DisplayMap), for readouts
    int8_t    step = -1;         // index into the active spec's steps; -1 = continuous
    SlotState state = SlotState::na;
    uint8_t   flags = 0;         // ParamSpec::flags | kClamped (1<<7) when raw lay outside [lo,hi]
    uint16_t  tag = 0;           // the step's tag
};
inline constexpr uint8_t kClamped = 1u << 7;

struct RawParams {                                   // host plain values as the APVTS raw atomics hold them
    std::array<float, kNumModeParams> v{};
    uint8_t modeSlot = 0;                            // effective slot (resolveSlot applied)
    LookaheadBudget budget = LookaheadBudget::off;   // the CONFIGURED budget (what EngineHost runs); every snapshot
                                                     // (Processor::currentRaw(), the audio thread, probes) fills it (K1 #8)
    float&       operator[](Pid p) noexcept       { return v[idx(p)]; }
    const float& operator[](Pid p) const noexcept { return v[idx(p)]; }
};

struct ParamView {
    const ModeDescriptor* desc = nullptr;
    uint8_t  slot = 0;
    uint32_t tags = 0;
    std::array<ResolvedParam, kNumModeParams> p{};
    std::array<const ParamSpec*, kNumModeParams> spec{};   // active spec after variants
    const ResolvedParam& operator[](Pid x) const noexcept { return p[idx(x)]; }
};

struct Resolution { ParamView view; EngineParams eng; };

struct Snapped { float plain; int8_t step; uint16_t tag; bool clamped; };

// snap() semantics (probe D3, C §5.4):
//   continuous    clamp(raw, lo, hi), step -1, clamped = raw outside [lo, hi]; soft notches never move the value
//   stepped       nearest step in kSnapDomain[pid] (log: ln plain; host: toNorm(pid, plain); linear: plain);
//                 ties go to the LOWER step; no hysteresis
//   hybrid        inside [lo, hi]: continuous (SlotState::live); else the nearest of {each step, lo, hi} in the snap
//                 domain: a range edge clamps, a step is that step (SlotState::stepped)
//   locked        `value` (+ step 0 when there is a one-step span)
//   n/a           `value`
//   derived       filled in pass 2: plain = derive(view), step -1
Snapped snap(Pid, const ParamSpec&, float rawPlain) noexcept;                 // THE snapping function
int   stepCount(const ParamSpec&) noexcept;
float stepPlain(const ParamSpec&, int i) noexcept;                            // UI snap-on-write
int   stepIndexOf(Pid, const ParamSpec&, float plain) noexcept;               // = snap(...).step
const ParamSpec& activeSpec(const ParamEntry&, const ParamView& partial) noexcept;

// resolveView: walk kResolveOrder (active spec = the first variant whose driverStep equals the resolved driver step),
// snap every non-derived parameter, apply the lookahead budget to `look` right after it is snapped (locked at 0 with
// the BUDGET OFF reason while raw.budget == off, else clamped to min(spec.hi, budgetMs(budget)) with kClamped), then
// evaluate the derived parameters, fill `display` through the DisplayMap, set `state` and OR the tags.
void resolveView(const ModeDescriptor&, const RawParams&, ParamView& out) noexcept;   // UI, text, probes
void resolve(const ModeEntry&, const RawParams&, Resolution& out) noexcept;           // + physicalDefault + physical
void modeDefaults(const ModeDescriptor&, RawParams& inOut) noexcept;   // writes defaultPlain for live/stepped only

namespace kit {
// Plain values into the engine fields (range >= 60 -> kRangeOff, schpf < 20 -> 0, s2thr >= 24 -> kS2Off), published
// times to tau through the spec's law (divide by lawFactor), the step indices of det/stmode/voice/tmode, and
// automu -> kEngAutoMakeup. The Mode's physical() runs after it.
void physicalDefault(const ParamView&, EngineParams&) noexcept;
}

} // namespace fcdsp
