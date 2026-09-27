// Diode 54 (slot 11, `diode-54`): instantiates ModeEngine<Diode54> and its analysis entry points in this TU and defines
// the Mode's registry entry, fcdsp::modes::kEntry_Diode54 (01 §8.2). Modes.def's FCMP_MODE(11, "diode-54", Diode54)
// line makes Registry.cpp collect it.

#include "fcdsp/modes/diode-54/Diode54.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Diode54);
