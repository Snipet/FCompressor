#pragma once

// FCDSP_DEFINE_MODE: the last line of modes/<key>/<Traits>.cpp (01 §8.2). It instantiates ModeEngine<Traits> and its
// analysis entry points in the Mode's own TU, asserts at compile time that the engine fits an arena slot, and
// defines the Mode's ModeEntry, fcdsp::modes::kEntry_<Traits>, which Registry.cpp collects through Modes.def's
// FCMP_MODE(<slot>, "<key>", <Traits>) line. Frozen at FZ0.
//
// Use it at global scope, after the Mode's namespaces are closed, with a semicolon:
//
//     #include "fcdsp/modes/clean/Clean.h"
//     #include "fcdsp/modes/DefineMode.h"
//     FCDSP_DEFINE_MODE(Clean);
//
// `Traits` is the bare identifier Modes.def names; it is looked up from namespace fcdsp::modes (so a traits struct in
// fcdsp::modes, fcdsp or the global namespace is found). The entry has external linkage (declared extern, then
// constant-initialised), and a use inside a namespace fails to compile instead of failing to link.

#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/modes/Registry.h"
#include <cstdint>

namespace fcdsp {

namespace detail {
// ModeEntry::staticS2 (S10 interface revision, X10): ModeEngine<T>::staticS2 when T's Stage2 has a static form
// (Stage.h HasCombineStatic), else nullptr (no stage-2 curve: the analysis draws stage 1 alone, the identity).
template <class T>
constexpr auto staticS2Of() noexcept
    -> void (*)(const EngineParams&, const float*, const float*, float*, int) noexcept {
    if constexpr (HasCombineStatic<typename T::Stage2>)
        return &ModeEngine<T>::staticS2;
    else
        return nullptr;
}
} // namespace detail

// Fills a ModeEntry from ModeEngine<T>; constexpr, so the entry is constant-initialised (constinit).
template <class T>
constexpr ModeEntry makeModeEntry() noexcept {
    return ModeEntry{ &T::desc,
                      &ModeEngine<T>::construct,
                      static_cast<uint32_t>(sizeof(ModeEngine<T>)),
                      static_cast<uint32_t>(alignof(ModeEngine<T>)),
                      &ModeEngine<T>::staticGr,
                      &ModeEngine<T>::scShapeDb,
                      &ModeEngine<T>::colourCurve,
                      detail::staticS2Of<T>() };
}

} // namespace fcdsp

#define FCDSP_DEFINE_MODE(Traits)                                                                                   \
    namespace fcdsp::modes {                                                                                        \
    static_assert(sizeof(::fcdsp::ModeEngine<Traits>) <= ::fcdsp::kArenaBytes &&                                    \
                  alignof(::fcdsp::ModeEngine<Traits>) <= 64, #Traits " does not fit the engine arena");            \
    extern const ::fcdsp::ModeEntry kEntry_##Traits;                                                                \
    constinit const ::fcdsp::ModeEntry kEntry_##Traits = ::fcdsp::makeModeEntry<Traits>();                          \
    }                                                                                                               \
    static_assert(sizeof(::fcdsp::modes::kEntry_##Traits) != 0,                                                     \
                  "FCDSP_DEFINE_MODE(" #Traits ") must be used at global scope")
