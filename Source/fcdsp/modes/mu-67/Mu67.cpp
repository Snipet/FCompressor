// Mu 67 (slot 4, `mu-67`): instantiates ModeEngine<Mu67> and its analysis entry points in this TU and defines the
// Mode's registry entry, fcdsp::modes::kEntry_Mu67 (01 §8.2). Modes.def's FCMP_MODE(4, "mu-67", Mu67) line makes
// Registry.cpp collect it.

#include "fcdsp/modes/mu-67/Mu67.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Mu67);
