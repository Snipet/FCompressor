// FET 76 (slot 2, `fet-76`): instantiates ModeEngine<Fet76> and its analysis entry points in this TU and defines the
// Mode's registry entry, fcdsp::modes::kEntry_Fet76 (01 §8.2). Modes.def's FCMP_MODE(2, "fet-76", Fet76) line makes
// Registry.cpp collect it.

#include "fcdsp/modes/fet-76/Fet76.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Fet76);
