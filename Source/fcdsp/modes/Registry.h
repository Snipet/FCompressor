#pragma once

// The Mode registry (01 §8.1-8.2). Modes.def is the single Mode list: one line per slot, append-only, slots and keys
// never reused. A Mode file never knows its slot and no TU instantiates every Mode (K3 #6): each Mode's <Traits>.cpp
// ends with FCDSP_DEFINE_MODE (DefineMode.h), which defines its ModeEntry in that TU; Registry.cpp only collects the
// addresses into constant-initialised tables (no lazy initialisation, no function-local statics: C D12).
//
// Empty-registry safe: before slot 0 ("clean") is active, modeSlots() and retired() are empty, bySlot/byKey return
// nullptr, and resolveSlot returns the null ModeSlot{0, "", nullptr}. Sprint-frozen.
//
// 01 §8.2 names the list function `modes()`, but a function fcdsp::modes cannot coexist with the namespace
// fcdsp::modes that holds the descriptors and entries (D24; "redefinition of 'modes' as different kind of symbol"),
// so the list function is `modeSlots()`.
//
// Real time (FZ0 errata, R-F0 #1): every lookup is a constant-table read and FCDSP_NONBLOCKING (Registry.cpp repeats
// the macro), and ModeEntry::construct is a pointer to a nonblocking function, so the host may construct an engine on
// the audio thread (01 §5.5). The analysis pointers are not on the audio path and are not annotated.

#include "fcdsp/core/Rt.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace fcdsp {

class IEngine;                                            // IEngine.h

inline constexpr int kModeCapacity = 128;

struct ModeEntry {                                        // one per Mode, defined by FCDSP_DEFINE_MODE in the Mode's TU
    const ModeDescriptor* desc;
    IEngine* (*construct)(void* arena) noexcept FCDSP_NONBLOCKING;   // placement-new ModeEngine<T>; RT-safe
    uint32_t engineBytes, engineAlign;
    void (*staticGr)(const EngineParams&, const float* xDetDb, float* grDb, int n) noexcept;
    void (*scShapeDb)(const EngineParams&, float fs, const float* hz, float* magDb, int n) noexcept;
    void (*colourCurve)(const EngineParams&, float grDb, const float* x, float* y, int n) noexcept;
    // S10 interface revision (X10): the settled stage 2 after the computer, grDb[i] from the stage-1 GR r1Db[i] at
    // detector level xDetDb[i] (ModeEngine<T>::staticS2; grDb may alias r1Db). nullptr when the Mode's Stage2 has no
    // static form (NoStage2): the static stage 2 is then the identity (analysis::staticGain, CurveOpts::stage2).
    void (*staticS2)(const EngineParams&, const float* xDetDb, const float* r1Db, float* grDb, int n) noexcept
        = nullptr;
};
struct ModeSlot { uint8_t slot; std::string_view key; const ModeEntry* entry; };   // registry-owned
struct Retired  { uint8_t slot; std::string_view key, successor; };

std::span<const ModeSlot> modeSlots() noexcept FCDSP_NONBLOCKING;   // assigned slots, slot order (01 §8.2's modes())
const ModeEntry* bySlot(int slot) noexcept FCDSP_NONBLOCKING;        // nullptr: unassigned or retired
const ModeEntry* byKey(std::string_view key) noexcept FCDSP_NONBLOCKING;   // registered keys only
const ModeSlot&  resolveSlot(int rawSlot) noexcept FCDSP_NONBLOCKING;      // O(1) constexpr map: retired ->
                                                                           //   successor; unassigned -> clean
const ModeSlot*  resolveKey(std::string_view key) noexcept FCDSP_NONBLOCKING;   // registered, or retired ->
                                                                                //   successor; nullptr if unknown
int              slotOf(const ModeEntry&) noexcept FCDSP_NONBLOCKING;  // ParamView::slot, telemetry; -1 = unregistered
std::span<const Retired> retired() noexcept FCDSP_NONBLOCKING;

} // namespace fcdsp
