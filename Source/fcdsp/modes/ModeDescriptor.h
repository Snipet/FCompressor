#pragma once

// ModeDescriptor: one Mode as a constexpr table (01 §4.3): a ParamSpec per Mode-filtered parameter plus analytic and
// UI metadata. It replaces C §5.1's ModeSpec and F §7.3's hooks. Each Mode defines one, with external linkage, in
// modes/<key>/<Traits>Desc.cpp (namespace fcdsp::modes; D24), and its traits reference it. Sprint-frozen; the schema
// freezes at FZ3 once proven on eight descriptors.

#include "fcdsp/core/Units.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace fcdsp {

// The session-state version (01 §9.1): bumped only when the meaning of a stored plain value changes or a parameter
// is renamed or removed, never when one is added. plugin/State.cpp writes it into <PARAMS stateVersion>; it lives in
// fcdsp so the dsp.registry lint can check introducedInStateVersion <= kStateVersion (01 §8.3).
inline constexpr uint16_t kStateVersion = 1;

enum class Group : uint8_t { vca, fet, opto, varimu, diode, modern, limit, other };      // browser columns (F §3.6)
enum class Stage2Kind : uint8_t { none, sharedElementMax, serialPre, postClip };      // E §3.3
enum class DetectorLaw : uint8_t { peak, rms, truePeak, custom };   // curve x-axis calibration (C §5.1, E §6.2)
enum class LinkLaw : uint8_t { independent, max, mean, cvSum };
enum class Rigor : uint8_t { clean, modelled, character };        // selects the tolerance row (C §5.13)
enum class CurveFamily : uint8_t { textbook, custom };            // textbook -> D1 checks it against the formula

// A declared attack or release time for the spec probes (C §5.3): `seconds` in the published `law`; for a
// program-dependent value, [lo, hi] is the published range the measurement must fall in (0 = unset).
struct TimeSpec { float seconds; TimeLaw law; float lo = 0, hi = 0; bool program = false; };

struct InternalSpec {                                              // UiFrame::internals[i] meaning
    const char* name; const char* unit; float lo, hi; int8_t decimals;
    bool history;                     // -> HistoryColumn::internal0; AT MOST ONE per Mode (lint, K1 #20)
};

struct ModeDescriptor {
    // identity (key is v1-forever once shipped)
    std::string_view key;                        // [a-z0-9-]{1,24}, e.g. "fet-76"; == its directory under modes/
    std::string_view name;                       // UI, e.g. "FET 76"
    Group group;
    uint16_t introducedInStateVersion;
    uint16_t revision = 1;                       // sound revision (K2 #10): ++ when a shipped Mode's print hash moves (01 §9.1)
    bool provisional = false;                    // generic traits from the descriptor wave (K3 #9): golden.py adopt refuses
                                                 // its modes/<key>/ rows; dsp.registry fails if FCOMPRESSOR_RELEASE=ON
    const char* topologyLine;                    // header caption: "FEEDBACK FET · PEAK"
    const char* specLine;                        // browser/footer one-liner (F §3.6)
    // parameters
    ParamTable params;
    void (*physical)(const ParamView&, EngineParams&) noexcept;   // after kit::physicalDefault; nullptr = none
    // structure (UI and probes)
    Stage2Kind stage2;
    LinkLaw linkLaw;
    DetectorLaw (*detectorLaw)(const EngineParams&) noexcept;
    bool hasColour, colourStatic, wantsLookahead;
    // analytic metadata for the spec probes (C §5.1-5.3)
    Rigor rigor; CurveFamily family;
    TimeSpec (*attackSpec)(const ParamView&, const EngineParams&) noexcept;
    TimeSpec (*releaseSpec)(const ParamView&, const EngineParams&) noexcept;
    float (*tailSeconds)(const EngineParams&) noexcept;            // longest release x 5 (+ opto memory)
    float ctBudgetNsPerSample;                                      // bench budget (E §3.7)
    // telemetry
    std::span<const InternalSpec> internals;                        // <= 8 (UiFrame::internals words 8-15 reserved)
};

} // namespace fcdsp
