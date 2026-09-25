#pragma once

// Bus 25 (slot 6, `bus-25`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are Bus25Desc.cpp's;
// Bus25.cpp instantiates the engine with FCDSP_DEFINE_MODE(Bus25). The Mode sheet, with every [H] constant, its source
// and the revision history, is docs/modes/bus-25.md. The unit is D §2.4's API 2500: a VCA bus compressor whose TYPE
// switch runs the same side chain feed-forward (NEW) or feedback (OLD).
//
// Full traits (M6, S11; 01 §10.7):
//
//     Detector    bus25::RmsCatch         RMS (THAT 2252), locked: the mean square in a window of tau_R / 50 with a
//                                         20 dB jump catch (Bus25Rms.h); THRUST is the host's SC tilt in front of it
//                                         (`sce`, NORM / MED / LOUD = 0 / 1.5 / 3.01 dB/oct), never a second emphasis
//     Computer    QuadKnee                THRESHOLD -20 ... +20 dBu, RATIO 1.5 2 3 4 6 10 inf, KNEE HARD / MED / SOFT
//                                         (W = 0 / 6 / 12 dB); in OLD the FB closed form (the knee at the output)
//     Link        LinkCvSum               L/R LINK IND, 50 ... 100 %: each channel keeps its detector, the CVs are
//                                         summed (F5); FF on the targets, FB on the CVs the ballistics solve
//     Ballistics  bus25::CvSumBranching   ATTACK .03 .1 .3 1 3 10 30 MS, RELEASE .05 .1 .2 .5 1 2 S or VAR 50-3000
//                                         MS: SmoothBranching, with the CV-sum link solved inside the OLD loop
//                                         (Bus25Ballistics.h; ADR-67's partial-link issue)
//     Stage2      NoStage2
//     Colour      ColourNone              (the 2510/2520 op-amps and output transformer are not modelled: known limit)
//     ScShape     Flat                    (THRUST and the SC HPF are the host's)
//     kTopologies FF | FB                 VOICE NEW = FF, OLD = FB: a kernel key (01 §5.5), so a flip is the host's
//                                         20 ms kernel crossfade; ModeEngine branches per chunk on e.topo
//
// Internals (kBus25.internals), on the channel whose applied GR is larger:
//   RMS DET (DB)     the detector's level (the 2252's RMS, after THRUST; 3.01 dB under a sine's peak)
//   OWN CV (DB)      the channel's own control voltage before the link: in OLD the loop's CV (the ballistics state),
//                    in NEW its gain computer's target at RMS DET (the FF link acts on the targets)
//   LINKED CV (DB)   the applied GR, after the CV-sum node (the history internal)

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkCvSum.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/bus-25/Bus25Ballistics.h"
#include "fcdsp/modes/bus-25/Bus25Rms.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kBus25;             // Bus25Desc.cpp

struct Bus25 {
    static constexpr const ModeDescriptor& desc = kBus25;
    using Detector   = bus25::RmsCatch;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkCvSum;
    using Ballistics = bus25::CvSumBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourNone;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = (1u << kTopoFF) | (1u << kTopoFB);

    // UiFrame::internals[0..2] (kBus25.internals, header comment); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        namespace me = ::fcdsp::detail::modeengine;
        const simd::f32x4 applied = Ballistics::grDb(engine.bal_);
        const simd::f32x4 det = Detector::levelDb(engine.det_);
        simd::f32x4 own = Ballistics::cvDb(engine.bal_);
        if (engine.p_.topo != kTopoFB)
        {
            const LevelCtl l{ me::bcast<0>(engine.lvl_.cur), me::bcast<1>(engine.lvl_.cur),
                              me::bcast<1>(engine.lvl2_.cur), me::bcast<0>(engine.lvl2_.cur) };
            own = Computer::target(engine.gc_, det, l);
        }
        const bool second = simd::lane<1>(applied) > simd::lane<0>(applied);
        out[0] = second ? simd::lane<1>(det) : simd::lane<0>(det);                 // RMS DET (DB)
        out[1] = second ? simd::lane<1>(own) : simd::lane<0>(own);                 // OWN CV (DB)
        out[2] = second ? simd::lane<1>(applied) : simd::lane<0>(applied);         // LINKED CV (DB)
    }
};

static_assert(DetectorPolicy<Bus25::Detector> && BallisticsPolicy<Bus25::Ballistics>);

} // namespace fcdsp::modes
