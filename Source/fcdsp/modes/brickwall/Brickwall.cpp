// Brickwall (slot 7, `brickwall`): instantiates ModeEngine<Brickwall> and its analysis entry points in this TU and
// defines the Mode's registry entry, fcdsp::modes::kEntry_Brickwall (01 §8.2). Modes.def's
// FCMP_MODE(7, "brickwall", Brickwall) line makes Registry.cpp collect it.

#include "fcdsp/modes/brickwall/Brickwall.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Brickwall);
