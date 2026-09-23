// Bus 25 (slot 6, `bus-25`): instantiates ModeEngine<Bus25> and its analysis entry points in this TU and defines the
// Mode's registry entry, fcdsp::modes::kEntry_Bus25 (01 §8.2). Modes.def's FCMP_MODE(6, "bus-25", Bus25) line makes
// Registry.cpp collect it.

#include "fcdsp/modes/bus-25/Bus25.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Bus25);
