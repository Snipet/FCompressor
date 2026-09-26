// Octo (slot 8, `octo`): instantiates ModeEngine<Octo> and its analysis entry points in this TU and defines the Mode's
// registry entry, fcdsp::modes::kEntry_Octo (01 §8.2). Modes.def's FCMP_MODE(8, "octo", Octo) line makes Registry.cpp
// collect it.

#include "fcdsp/modes/octo/Octo.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Octo);
