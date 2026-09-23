#pragma once

// Mode and kernel switching (01 §5.5; E §5.1; K2 #3, #22). A change of the KernelKey at a block start runs a 20 ms
// equal-gain crossfade between two engine paths; each path keeps its own frozen parameters and gain staging, so
// neither is driven by the other Mode's (K2 #3a-b). Crossfade starts are at least kMinFadeGapMs apart, counted in
// samples (deterministic); a newer request during a fade or within the gap is latched, latest wins.
//
// Switch sequence: construct the new engine into the idle arena slot (prepare, setParams, snapParams; its gain
// smoother snapped), seed it from the active engine's carry() (if carry.msDomain differs from the new engine's lane
// domain, every lane of grDb, detDb and s2GrDb first takes max(lane0, lane1): never under-compresses, K2 #3d), run
// both for kFadeMs, then destroy the old engine with a plain ~IEngine() and swap the path pointers.
//
// Sprint-frozen types. F7 (S6) owns this header after S0 and adds the crossfade logic as bodies/helpers.

#include "fcdsp/core/Smoother.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp {

struct KernelKey { uint8_t slot, topo, det, stmode, voice, keyExt; bool operator==(const KernelKey&) const = default; };

struct PathState {                                    // one per arena slot, owned by EngineHost
    IEngine*     engine = nullptr;
    KernelKey    key{};
    EngineParams eng{};                               // incoming path: updated every block; outgoing: FROZEN at its last value
    Smoother4    gain{};                              // {preGainDb, makeupTotalDb (= makeupDb + engine->autoMakeupDb()), -, -}
};

inline constexpr float kFadeMs = 20.f;
inline constexpr float kMinFadeGapMs = 50.f;          // between crossfade STARTS, counted in samples (deterministic)

} // namespace fcdsp
