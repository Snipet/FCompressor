#pragma once

// Diode 609 (slot 5, `diode-609`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are
// Diode609Desc.cpp's; Diode609.cpp instantiates the engine with FCDSP_DEFINE_MODE(Diode609).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30), from the policies that exist at S4.
// kDiode609.provisional stays true until the Mode task (M5) installs the real ones: its fidelity rows are NOTE lines
// and golden.py refuses its modes/diode-609/ rows.
//
//     slot        generic (now)        final (01 §10.7, the Mode task)
//     Detector    PeakLog              PeakLog
//     Computer    QuadKnee             QuadKnee in FB (the progressive 5 dB knee)
//     Link        LinkMax              LinkMax (the side chains are combined)
//     Ballistics  SmoothBranching      SmoothBranching + DualRelease for A1 / A2 (m[1]), max of roots in the FB solve
//     Stage2      NoStage2             SharedElementMax<PeakLog, SmoothBranching> on the aux lanes (A1 / A2: m[2])
//     Colour      ColourNone           DiodeBridge (GR-dependent)
//     ScShape     Flat                 SlowHp (ATTACK SLOW: m[0] = 100 Hz)
//     kTopologies FB                   FB
//
// The compressor runs the real feedback kernel (K2 #1, #5): QuadKnee::solveFb inside SmoothBranching::solveFb/commitFb,
// linked after the per-lane solve. What the generic traits leave out (fidelity only): the limiter (stage 2: the LIMIT
// controls resolve and reach EngineParams, s2On_ ramps, but NoStage2 adds no GR), A1/A2 (plain 2 s / 5 s releases),
// the SLOW side-chain high-pass and the diode colour. No internals hook: COMP GR, LIMIT GR and LIMIT WINS read 0 until
// the Mode task.

#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kDiode609;          // Diode609Desc.cpp

struct Diode609 {
    static constexpr const ModeDescriptor& desc = kDiode609;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourNone;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;
};

} // namespace fcdsp::modes
