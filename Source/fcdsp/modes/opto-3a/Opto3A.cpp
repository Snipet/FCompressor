// Opto 3A (slot 10, `opto-3a`): instantiates ModeEngine<Opto3A> and its analysis entry points in this TU and defines
// the Mode's registry entry, fcdsp::modes::kEntry_Opto3A (01 §8.2). Modes.def's FCMP_MODE(10, "opto-3a", Opto3A) line
// makes Registry.cpp collect it.

#include "fcdsp/modes/opto-3a/Opto3A.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Opto3A);
