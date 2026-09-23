#pragma once

// How one Mode presents one parameter (01 §4.1): the kind (continuous, stepped, hybrid, locked, derived, n/a), the
// sub-range, the steps and their tags, the Mode's own display scale, and the texts. A ModeDescriptor holds one
// ParamEntry per Mode-filtered Pid. Sprint-frozen; the schema is proven on eight descriptors and frozen at FZ3.

#include "fcdsp/params/Pid.h"
#include "fcdsp/core/Units.h"          // TimeLaw
#include <array>
#include <cstdint>
#include <span>

namespace fcdsp {

struct ParamView;                      // Resolve.h

enum class Kind : uint8_t {
    continuous,     // clamp to [lo, hi]; `steps` (optional) = soft notches, value unchanged
    stepped,        // exactly one of `steps` (strictly increasing plain)
    hybrid,         // [lo, hi] continuous U `steps` that lie OUTSIDE [lo, hi]; SlotState live inside, stepped on a step.
                    // Dangerous next to a Mode switch (K2 #4): the crossmode.no_off lint (01 §8.3) guards every use.
    locked,         // `value`; shown, never written (circuit constant)
    derived,        // `derive(view)`; shown read-only, follows `derivedFrom`
    notApplicable,  // hidden; engine gets `value` (neutral). Host text "-" (U+2013)
};

enum StepTag : uint16_t {             // OR-ed into ParamView::tags and EngineParams::tags
    kTagNone = 0,
    kTagOff = 1u << 0,     // circuit disabled (FET attack OFF, Stage 2 OFF)
    kTagAuto = 1u << 1,    // program-dependent switch position (SSL AUTO, Neve a1, digital AUTO)
    kTagAuto2 = 1u << 2,   // second auto variant (Neve a2)
    kTagAll = 1u << 3,     // 1176 all-buttons
    kTagVar = 1u << 4,     // position exposing a continuous sub-range (API VAR)
    kTagTc5 = 1u << 5, kTagTc6 = 1u << 6,   // Fairchild program time constants
    kTagProgram = 1u << 7, // the value is nominal; behaviour is program-dependent
    // bits 8..15: Mode-private, documented in that Mode's header
};

struct Step {
    float       plain;               // canonical HOST PLAIN value (what the UI writes; what snap returns)
    const char* label;               // cell label ("4", "ALL", ".3", "AUTO"); > 6 glyphs prints a registry WARNING;
                                     // the binding gate is the ui.textfit pair-fit rule (02 §8.3)
    const char* text   = nullptr;    // host/readout text; nullptr -> formatted from plain ("4:1", "0.3 MS")
    uint16_t    tag    = kTagNone;
    const char* spoken = nullptr;    // a11y; nullptr -> text, then label
};

struct DisplayMap {                  // a Mode's own scale over the same plain value ("remap")
    float (*toDisplay)(float plain) noexcept = nullptr;   // nullptr = identity
    float (*toPlain)(float display) noexcept = nullptr;   // exact inverse (lint: round trip <= 1e-4)
    const char* unit = nullptr;      // "" for dial numbers, "DBU"; nullptr = universal unit
    int8_t decimals = -1;            // -1 = universal rule
    bool invert = false;             // display rises while plain falls (INPUT and PEAK RED. over thr)
};

enum SpecFlag : uint8_t {
    kFlagExtension  = 1u << 0,       // "+" cell: not on the hardware, neutral default (D §5.1)
    kFlagHwReversed = 1u << 1,       // hardware knob runs the other way (1176): informational only
    kFlagProgram    = 1u << 2,       // locked/derived value is nominal; UI prints live EFF from UiFrame
    kFlagPlotIsPlain = 1u << 3,      // the TRANSFER plot's value for this param IS its plain value (identity DisplayMap):
                                     // knee/range handles may drag absolutely (02 §6.5). Clean sets it on thr/knee/range.
    // bits 4..7 reserved.
};

struct ParamSpec {
    Kind        kind = Kind::notApplicable;
    uint8_t     flags = 0;
    TimeLaw     law = TimeLaw::expDb;               // time params: what the PUBLISHED value means (E §2.3)
    float       lo = 0, hi = 0;                     // continuous/hybrid sub-range, host plain, lo < hi
    std::span<const Step> steps{};                  // stepped: all; hybrid: outside [lo,hi]; continuous: soft notches;
                                                    // locked list params: exactly one step (its label)
    float       value = 0;                          // locked: the value; notApplicable: neutral engine value
    float     (*derive)(const ParamView&) noexcept = nullptr;
    Pid         derivedFrom = kNoPid;               // derived: slot it follows (UI tag, view-switch source)
    float       defaultPlain = 0;                   // Mode default: double-click, default notch, "load Mode defaults"
    const char* label  = nullptr;                   // Mode's slot name ("INPUT"); nullptr = universal
    const char* tag    = nullptr;                   // label-row tag ("FIXED", "= RATIO", "AUTO")
    const char* reason = nullptr;                   // REQUIRED unless continuous/stepped (lint): footer + a11y help
    const char* brief  = nullptr;                   // <= 18 glyphs: the locked/n/a sub-line ("CIRCUIT KNEE");
                                                    // nullptr -> tag. ui.textfit lints its width (K1 #25)
    DisplayMap  display{};
};

// Dependent step lists: while `driver` resolves to `driverStep`, `spec` replaces the entry's base spec.
struct Variant { int8_t driverStep; ParamSpec spec; };

struct ParamEntry {
    ParamSpec spec{};                               // base spec
    Pid driver = kNoPid;                            // lint: must precede this Pid in kResolveOrder
    std::span<const Variant> variants{};
};

struct ParamTable {
    std::array<ParamEntry, kNumModeParams> e{};
    constexpr ParamEntry&       operator[](Pid p)       noexcept { return e[idx(p)]; }
    constexpr const ParamEntry& operator[](Pid p) const noexcept { return e[idx(p)]; }
};

} // namespace fcdsp
