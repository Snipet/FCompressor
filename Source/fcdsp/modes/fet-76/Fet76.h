#pragma once

// FET 76 (slot 2, `fet-76`): the traits (01 §8.2, §10.5). The descriptor, physical() and specs are Fet76Desc.cpp's;
// Fet76.cpp instantiates the engine with FCDSP_DEFINE_MODE(Fet76).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30), from the policies that exist at S4.
// kFet76.provisional stays true until the Mode task (M2) installs the real ones: its fidelity rows are NOTE lines and
// golden.py refuses its modes/fet-76/ rows.
//
//     slot        generic (now)        final (01 §10.5, the Mode task)
//     Detector    PeakLog              PeakLog
//     Computer    QuadKnee             QuadKnee (solveFb closed form, E §2.6) + law::FetVcr [H]
//     Link        LinkMax              LinkMax
//     Ballistics  SmoothBranching      SmoothBranching (ALL: slower attack from kTagAll, E §2.7 [H])
//     Stage2      NoStage2             NoStage2
//     Colour      ColourNone           ColourSelect<FetColour> (VOICE = revision constants; drive = INPUT)
//     ScShape     Flat                 Flat
//     kTopologies FB                   FB
//
// The feedback kernel is the real one (K2 #1, #5): QuadKnee::solveFb (closed form over FbAffine) inside
// SmoothBranching::solveFb/commitFb, linked after the per-lane solve. Every FB configuration is monotone
// (dsp.registry fb.monotone): ALL is realised by the loop gain (S = 1 clamps to k = 99), never by a negative slope.
// What the generic traits leave out (fidelity only): the FET colour (no FetColour yet: the three revisions sound the
// same, and a colour stage on the DEFAULT voice would break the tap-against-audio rows), ALL's attack lag and the
// program-dependent release. A feedback release is faster than its published one-pole by the loop gain (the closed
// loop decays by (1 - c) / (1 + c k) per sample, E §2.6); fitting the published 50-1100 ms to the measured release is
// the Mode task's. No internals hook: LOOP CV, FET R, H2 and H3 read 0 until then.

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

extern const ModeDescriptor kFet76;             // Fet76Desc.cpp

struct Fet76 {
    static constexpr const ModeDescriptor& desc = kFet76;
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
