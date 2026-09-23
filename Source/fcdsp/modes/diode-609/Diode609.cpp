// Diode 609 (slot 5, `diode-609`): instantiates ModeEngine<Diode609> and its analysis entry points in this TU and
// defines the Mode's registry entry, fcdsp::modes::kEntry_Diode609 (01 §8.2). Modes.def's
// FCMP_MODE(5, "diode-609", Diode609) line makes Registry.cpp collect it.

#include "fcdsp/modes/diode-609/Diode609.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Diode609);
