// Opto Tube 1B (slot 13, `opto-tube-1b`): instantiates ModeEngine<OptoTube1B> and its analysis entry points in this TU
// and defines the Mode's registry entry, fcdsp::modes::kEntry_OptoTube1B (01 §8.2). Modes.def's FCMP_MODE(13,
// "opto-tube-1b", OptoTube1B) line makes Registry.cpp collect it.

#include "fcdsp/modes/opto-tube-1b/OptoTube1B.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(OptoTube1B);
