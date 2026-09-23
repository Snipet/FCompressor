// Clean (slot 0, `clean`): instantiates ModeEngine<Clean> and its analysis entry points in this TU and defines the
// Mode's registry entry, fcdsp::modes::kEntry_Clean (01 §8.2). The arena size/alignment static_asserts live in the
// macro. Modes.def's FCMP_MODE(0, "clean", Clean) line makes Registry.cpp collect it.

#include "fcdsp/modes/clean/Clean.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(Clean);
