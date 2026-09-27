#pragma once

// Console E (slot 9, `console-e`): the traits (01 §8.2; D §2.4 the SSL E/G channel dynamics, M10). The descriptor,
// physical() and specs are ConsoleEDesc.cpp's; ConsoleE.cpp instantiates the engine with FCDSP_DEFINE_MODE(ConsoleE).
// The Mode sheet, with every [H] constant, its source and the revision history, is docs/modes/console-e.md.
//
// Full traits, feed-forward only:
//
//     Detector   DetSelect<RmsLogT<10 ms>, PeakLog>              DETECT RMS (the E module's "true RMS") / PEAK (the
//                                                                9000 J/K switch)
//     Computer   QuadKnee                                        THRESHOLD +10 … -20 (dial), RATIO 1 … inf continuous,
//                                                                KNEE OVEREASY / HARD (the E module's defeat switch)
//     Link       LinkMax                                         LINK: the adjacent channel's link, the larger GR
//     Ballistics VcaChannelT<0, 1>                               ATTACK AUTO (3–30 ms, program) / FAST 1 ms; RELEASE
//                                                                0.1–4 s, LOG or LIN (the E module's linear release)
//     Stage2     NoStage2                                        (the expander/gate section is not modelled)
//     Colour     ColourNone                                      the channel VCA is clean (VOICE, DRIVE n/a)
//     ScShape    Flat                                            (the channel HPF into the side chain is the host's
//                                                                SC HPF)
//
// AUTO makeup is always on ("calculated from the Ratio and Threshold settings", D §2.4 [V S26]): the static GR at
// 0 VU, added to makeupDb by consoleEPhysical (kEngAutoMakeup cleared), with MAKEUP an extension trim.
//
// Internals (kConsoleE.internals), on the louder of lanes 0-1:
//   ATK EFF (MS)    the attack time AUTO is running at (ATTACK's own at FAST)

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/VcaChannel.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/combinators/DetSelect.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/detector/RmsLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kConsoleE;          // ConsoleEDesc.cpp

struct ConsoleE {
    // EngineParams::m[] slots, written by consoleEPhysical.
    static constexpr int kAutoAttackSlot = 0;   // 1 = ATTACK AUTO
    static constexpr int kLinReleaseSlot = 1;   // 1 = RELEASE LIN
    static constexpr int kRmsTauUs = 10000;     // [H] the RMS window (docs/modes/console-e.md)

    static constexpr const ModeDescriptor& desc = kConsoleE;
    using Detector   = stage::DetSelect<stage::RmsLogT<kRmsTauUs>, stage::PeakLog>;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::VcaChannelT<kAutoAttackSlot, kLinReleaseSlot>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourNone;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // UiFrame::internals[0] (kConsoleE.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 gr = Ballistics::grDb(engine.bal_);
        const simd::f32x4 ms = Ballistics::attackNowMs(engine.bc_, engine.bal_);
        out[0] = simd::lane<0>(gr) >= simd::lane<1>(gr) ? simd::lane<0>(ms) : simd::lane<1>(ms);   // ATK EFF (MS)
    }
};

} // namespace fcdsp::modes
