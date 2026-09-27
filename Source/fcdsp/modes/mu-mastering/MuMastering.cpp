// Mu Mastering (slot 12, `mu-mastering`): instantiates ModeEngine<MuMastering> and its analysis entry points in this TU
// and defines the Mode's registry entry, fcdsp::modes::kEntry_MuMastering (01 §8.2). Modes.def's FCMP_MODE(12,
// "mu-mastering", MuMastering) line makes Registry.cpp collect it.

#include "fcdsp/modes/mu-mastering/MuMastering.h"

#include "fcdsp/modes/DefineMode.h"

FCDSP_DEFINE_MODE(MuMastering);
