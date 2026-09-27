// Console E (slot 9, `console-e`): instantiates ModeEngine<ConsoleE> and its analysis entry points in this TU and
// defines the Mode's registry entry, fcdsp::modes::kEntry_ConsoleE (01 §8.2). Modes.def's FCMP_MODE(9, "console-e",
// ConsoleE) line makes Registry.cpp collect it.

#include "fcdsp/modes/console-e/ConsoleE.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(ConsoleE);
