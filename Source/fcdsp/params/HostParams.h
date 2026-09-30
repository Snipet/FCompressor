#pragma once

// The universal host-parameter superset (01 §3.1, §3.3): 30 parameters (29 at v1, `output` since v1.2, ADR-88), their
// plain ranges, the normalised <-> plain maps and the defaults. IDs, plain ranges, maps and defaults are v1-forever.
// Implemented in HostParams.cpp.
//
// The host maps use libm (params/ is exempt from the audio-path rule, 01 §2.2): they are not the per-sample path, and
// the resolver snaps their result anyway. Golden rows that depend on host-map values use absrel, never exact (K2 #14).

#include "fcdsp/params/Pid.h"
#include <array>
#include <cstdint>

namespace fcdsp {

enum class Map : uint8_t { linear, log, power, ratio3, index, boolean };

struct HostParam {
    Pid pid; const char* id; const char* name; const char* unit;
    Map map; float lo, hi, centre;      // centre: Map::power only
    float def; int versionHint; bool automatable; bool inPresets;
    int numSteps;                       // index/boolean: 8, 128, 2, 3; else 0 (continuous)
    const char* const* choices;         // quality/labudget names, else nullptr
};
// 01 §3.1, in Pid order. `id` and `name` are universal forever (K2 #25b). `unit` is the universal unit of the VALUE
// TEXT (upper case, as FormattedValue::unit: "DB", "MS", "HZ", "%", ""); it is never the JUCE label, which is "" for
// every Mode-filtered parameter so hosts do not append a fixed unit to a Mode's own scale (K2 #25a).
extern const std::array<HostParam, kNumParams> kHostParams;

// Every map is exact (01 §3.3); v is the normalised value in [0, 1]:
//   linear  lo + (hi - lo)*v
//   log     lo*(hi/lo)^v
//   power   lo + (hi - lo)*v^k,  k = ln((centre - lo)/(hi - lo)) / ln 0.5   (knee 2.585, hold 3.322, schpf 2.644)
//   ratio3  v <= 0.8: S = 1 - 20^(-v/0.8) (1:1 -> 20:1); v <= 0.9: S = 0.95 + 0.5*(v - 0.8) (20:1 -> inf);
//           v > 0.9: S = 1 + 10*(v - 0.9) (inf -> -1:1)
//   index   lo + round(v*(numSteps - 1));  boolean  v >= 0.5
// A NaN argument maps to the parameter's default (plain, or its normalised value); out-of-range arguments clamp.
// Allocation- and lock-free: JUCE may call them from any thread, the audio thread included.
float toPlain(Pid, float norm) noexcept;    // the ONLY normalised->plain map (APVTS lambdas, UI tracks, D3 sweep)
float toNorm (Pid, float plain) noexcept;
float legal  (Pid, float plain) noexcept;   // clamp; round index/bool

} // namespace fcdsp
