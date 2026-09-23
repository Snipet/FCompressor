#pragma once

// Bus G (slot 1, `bus-g`): the traits (01 §8.2, §10.4). The descriptor, physical() and specs are BusGDesc.cpp's;
// BusG.cpp instantiates the engine with FCDSP_DEFINE_MODE(BusG).
//
// PROVISIONAL generic traits (descriptor wave DW, S4; 01 §8.4 step 1; ADR-30): built only from the policies that exist
// at S4, so the descriptor (final, schema proof for FZ3) can be registered, resolved and probed before the Mode task
// (M1) installs the real policies. kBusG.provisional stays true until then: its fidelity rows are NOTE lines and
// golden.py refuses its modes/bus-g/ rows.
//
//     slot        generic (now)        final (01 §10.4, the Mode task)
//     Detector    PeakLog              PeakLog
//     Computer    QuadKnee             QuadKnee (W from the ratio, kneeFromRatio)
//     Link        LinkMax              LinkMax
//     Ballistics  SmoothBranching      AutoSwitch<SmoothBranching, DualRelease> (RELEASE AUTO: m[0..2], E §2.5b)
//     Stage2      NoStage2             NoStage2
//     Colour      ColourNone           ColourSelect<VcaBus, ColourNone> (VOICE VCA / CLEAN)
//     ScShape     Flat                 Flat
//     kTopologies FF                   FF
//
// Differences the generic traits leave (all fidelity, none structural): RELEASE AUTO runs as a plain 2.4 s release
// (DualRelease does not exist yet); VOICE VCA has no colour stage (VcaBus does not exist yet), so VCA and CLEAN sound
// the same and DRIVE does nothing. A colour stage on the DEFAULT voice would also break the tap-against-audio rows,
// which assume the fundamental carries only the GR. No internals hook: the UiFrame words of kBusG.internals (FAST ENV,
// SLOW ENV, AUTO SLOW) read 0 until DualRelease exists.

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

extern const ModeDescriptor kBusG;              // BusGDesc.cpp

struct BusG {
    static constexpr const ModeDescriptor& desc = kBusG;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourNone;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;
};

} // namespace fcdsp::modes
