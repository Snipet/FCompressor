#pragma once

// Opto 2A (slot 3, `opto-2a`): the traits (01 §8.2, §10.6). The descriptor, physical() and specs are
// Opto2ADesc.cpp's; Opto2A.cpp instantiates the engine with FCDSP_DEFINE_MODE(Opto2A).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30), from the policies that exist at S4.
// kOpto2A.provisional stays true until the Mode task (M3) installs the real ones: its fidelity rows are NOTE lines and
// golden.py refuses its modes/opto-2a/ rows.
//
//     slot        generic (now)        final (01 §10.6, the Mode task)
//     Detector    PeakLog              OptoSense (rectified, emphasis-shaped output)
//     Computer    QuadKnee             OptoCellCurve (steady state of law::LdrShunt) in FeedbackDelayed<OptoCellCurve>
//     Link        LinkMax              LinkMax
//     Ballistics  SmoothBranching      OptoCell (fast/slow conductance + memory, E §2.7)
//     Stage2      NoStage2             NoStage2
//     Colour      ColourNone           TubeTransformer
//     ScShape     Flat                 R37Shelf (EMPHASIS, m[0])
//     kTopologies FB                   FB
//
// The feedback kernel is QuadKnee's zero-delay closed form, not FeedbackDelayed: the delayed loop is only meant for
// the opto cell's own ballistics (01 §5.3), and dsp.static's per-sample FB rows compare every sample against the
// zero-delay branch root of SmoothBranching's affine maps, which the delayed loop does not solve. With the cell, the
// Mode task brings its own branch maps to those rows (dsp.static header) and FeedbackDelayed's guard (22.05 kHz row,
// K2 #5c). The generic loop runs the locked nominal times (attack 10 ms; release 60 ms to 50 %, law t50) with the
// knee at its neutral 6 dB. No internals hook: LIGHT, G FAST, G SLOW, MEMORY and LDR read 0 until the cell exists.

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

extern const ModeDescriptor kOpto2A;            // Opto2ADesc.cpp

struct Opto2A {
    static constexpr const ModeDescriptor& desc = kOpto2A;
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
