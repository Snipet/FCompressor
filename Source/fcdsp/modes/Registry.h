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
    IEngine* (*construct)(void* arena) noexcept;          // placement-new ModeEngine<T>; RT-safe
    uint32_t engineBytes, engineAlign;
    void (*staticGr)(const EngineParams&, const float* xDetDb, float* grDb, int n) noexcept;
    void (*scShapeDb)(const EngineParams&, float fs, const float* hz, float* magDb, int n) noexcept;
    void (*colourCurve)(const EngineParams&, float grDb, const float* x, float* y, int n) noexcept;
};
struct ModeSlot { uint8_t slot; std::string_view key; const ModeEntry* entry; };   // registry-owned
struct Retired  { uint8_t slot; std::string_view key, successor; };

std::span<const ModeSlot> modeSlots() noexcept;           // assigned slots, slot order (01 §8.2's modes())
const ModeEntry* bySlot(int slot) noexcept;               // nullptr: unassigned or retired
const ModeEntry* byKey(std::string_view key) noexcept;    // registered keys only
const ModeSlot&  resolveSlot(int rawSlot) noexcept;       // O(1) constexpr map: retired -> successor; unassigned -> clean
const ModeSlot*  resolveKey(std::string_view key) noexcept;   // registered, or retired -> successor; nullptr if unknown
int              slotOf(const ModeEntry&) noexcept;       // for ParamView::slot and telemetry; -1 if not registered
std::span<const Retired> retired() noexcept;

} // namespace fcdsp
