#pragma once

// Brickwall (slot 7, `brickwall`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are
// BrickwallDesc.cpp's; Brickwall.cpp instantiates the engine with FCDSP_DEFINE_MODE(Brickwall).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30), from the policies that exist at S4.
// kBrickwall.provisional stays true until the Mode task (M7) installs the real ones: its fidelity rows are NOTE lines
// and golden.py refuses its modes/brickwall/ rows.
//
//     slot        generic (now)                      final (01 §10.7, E §5.4, the Mode task)
//     Detector    PeakLog                            DetSelect<PeakLog, TruePeak4x> (DETECT PEAK / TP; D_tp through
//                                                    scDelaySamples, K2 #21a)
//     Computer    QuadKnee                           QuadKnee (∞:1, knee 0–6 dB)
//     Link        LinkMax                            LinkMax
//     Ballistics  CrestAuto<SmoothBranching>         SlidingMaxBox over PrepareInfo::scratch (attack = the lookahead
//                                                    window), CrestAuto-style AUTO release
//     Stage2      NoStage2                           NoStage2
//     Colour      ColourSelect<ColourNone, Bright>   ColourSelect<ColourNone, LoudClip> (VOICE CLEAN / LOUD)
//     ScShape     Flat                               Flat
//     kTopologies FF                                 FF
//
// What the generic traits leave out (fidelity only): the lookahead box (SlidingMaxBox does not exist yet: the attack
// runs as a one-pole of the published, look-derived time; with the budget OFF that is 0, an instantaneous attack),
// true-peak detection (TP runs the sample-peak detector) and the loud clipper (LOUD runs Bright, the cubic soft
// clipper, as a stand-in: memoryless and ADAA-1, like the final pre-stage). TIME MODE AUTO is the real
// program-dependent release (CrestAuto, kEngAutoRelease), as in Clean. No internals hook: HELD PEAK, LOOK EFF and
// TP OVER read 0 until then.

#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/Bright.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/combinators/ColourSelect.h"
#include "fcdsp/engine/stages/combinators/CrestAuto.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kBrickwall;         // BrickwallDesc.cpp

struct Brickwall {
    static constexpr const ModeDescriptor& desc = kBrickwall;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::CrestAuto<stage::SmoothBranching>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::ColourNone, stage::Bright>;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // VOICE steps (kVoice in BrickwallDesc.cpp) = ColourSelect indices.
    static constexpr int kVoiceClean = 0, kVoiceLoud = 1;
};

} // namespace fcdsp::modes
