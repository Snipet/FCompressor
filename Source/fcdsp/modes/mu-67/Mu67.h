#pragma once

// Mu 67 (slot 4, `mu-67`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are Mu67Desc.cpp's;
// Mu67.cpp instantiates the engine with FCDSP_DEFINE_MODE(Mu67).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30), from the policies that exist at S4.
// kMu67.provisional stays true until the Mode task (M4) installs the real ones: its fidelity rows are NOTE lines and
// golden.py refuses its modes/mu-67/ rows.
//
//     slot        generic (now)        final (01 §10.7, E §2.7, the Mode task)
//     Detector    PeakLog              the tube side-chain rectifier
//     Computer    QuadKnee             FeedbackZdf<ProgressiveKnee> (o_c = m[0], S_max = m[1])
//     Link        LinkMax              LinkMax
//     Ballistics  SmoothBranching      TcSelector: TC1–4 SmoothBranching, TC5/TC6 MultiStage3 (max of roots)
//     Stage2      NoStage2             NoStage2
//     Colour      ColourNone           VarimuColour (odd-dominant, grows with GR)
//     ScShape     Flat                 Flat
//     kTopologies FB                   FB
//
// The generic curve is QuadKnee in the real feedback kernel (K2 #1, #5: QuadKnee::solveFb inside
// SmoothBranching::solveFb/commitFb) at the derived nominal slope (RATIO, from DC THRESH) with the DC THRESH value as
// its knee width: DC THRESH 0 is a hard, steep limiter, 40 a wide, gentle knee, as on the hardware; the progressive
// curve itself (ProgressiveKnee) does not exist yet. Every DC THRESH is monotone (fb.monotone). TC5 / TC6 run as plain
// 10 s / 25 s releases. The INPUT attenuator (drive) and the colour do nothing yet. No internals hook: BIAS, EFF RATIO
// and TC WEIGHT read 0 until the Mode task.

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

extern const ModeDescriptor kMu67;              // Mu67Desc.cpp

struct Mu67 {
    static constexpr const ModeDescriptor& desc = kMu67;
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
