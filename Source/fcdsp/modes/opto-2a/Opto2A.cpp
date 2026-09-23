// Opto 2A (slot 3, `opto-2a`): instantiates ModeEngine<Opto2A> and its analysis entry points in this TU and defines
// the Mode's registry entry, fcdsp::modes::kEntry_Opto2A (01 §8.2). Modes.def's FCMP_MODE(3, "opto-2a", Opto2A) line
// makes Registry.cpp collect it.

#include "fcdsp/modes/opto-2a/Opto2A.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Opto2A);
