#pragma once

// Bus 25 (slot 6, `bus-25`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are Bus25Desc.cpp's;
// Bus25.cpp instantiates the engine with FCDSP_DEFINE_MODE(Bus25).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30), from the policies that exist at S4.
// kBus25.provisional stays true until the Mode task (M6) installs the real ones: its fidelity rows are NOTE lines and
// golden.py refuses its modes/bus-25/ rows.
//
//     slot        generic (now)        final (01 §10.7, the Mode task)
//     Detector    RmsLog               RmsLog (THAT 2252 true RMS), Thrust on the host tilt
//     Computer    QuadKnee             QuadKnee (HARD / MED / SOFT widths)
//     Link        LinkMax              LinkCvSum (F5, S4: each channel keeps its detector, the CVs are summed)
//     Ballistics  SmoothBranching      SmoothBranching
//     Stage2      NoStage2             NoStage2
//     Colour      ColourNone           the 2510/2520 + output-transformer colour
//     ScShape     Flat                 Flat
//     kTopologies FF | FB              FF | FB (VOICE NEW / OLD; the kernel branches per chunk on e.topo)
//
// OLD runs the real feedback kernel (K2 #1, #5): QuadKnee::solveFb inside SmoothBranching::solveFb/commitFb, linked
// after the per-lane solve. What the generic traits leave out (fidelity only): the CV-sum link law (LinkMax until F5's
// LinkCvSum merges), the colour, and API's auto-makeup law (the engine's r^(0 dBFS)). No internals hook: RMS DET, OWN
// CV and LINKED CV read 0 until the Mode task.

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

extern const ModeDescriptor kBus25;             // Bus25Desc.cpp

struct Bus25 {
    static constexpr const ModeDescriptor& desc = kBus25;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourNone;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = (1u << kTopoFF) | (1u << kTopoFB);
};

} // namespace fcdsp::modes
