// Bus G (slot 1, `bus-g`): instantiates ModeEngine<BusG> and its analysis entry points in this TU and defines the
// Mode's registry entry, fcdsp::modes::kEntry_BusG (01 §8.2). Modes.def's FCMP_MODE(1, "bus-g", BusG) line makes
// Registry.cpp collect it.

#include "fcdsp/modes/bus-g/BusG.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(BusG);
