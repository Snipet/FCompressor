#pragma once

// Mu Mastering (slot 12, `mu-mastering`): the traits (01 §8.2; D §2.3 the Manley Variable Mu, Mastering version, M06).
// The descriptor, physical() and specs are MuMasteringDesc.cpp's; MuMastering.cpp instantiates the engine with
// FCDSP_DEFINE_MODE(MuMastering). The Mode sheet, with every [H] constant, its source and the revision history, is
// docs/modes/mu-mastering.md.
//
// Full traits, feedback only ("the gain control chain is technically called a feedback circuit", D §2.3 [V S12]):
//
//     Detector   PeakLog                          the tube side chain's rectifier (DETECT locked TUBE)
//     Computer   QuadKnee (FB closed form)        THRESHOLD (21 steps, calibrated to LIMIT), COMPRESS 1.5:1 / LIMIT 4:1,
//                                                 the soft knee derived from the switch
//     Link       LinkMax                          SEP / LINK (the DC control voltages combined, not the audio)
//     Ballistics SmoothBranching                  ATTACK 11 positions 20–80 ms, RECOVERY 0.2 / 0.4 / 0.6 / 4 / 8 s
//     Stage2     SharedElementMaxT<PeakLog,       LIMIT's rise toward 20:1 "at greater than 12 dB of limiting": a 20:1
//                SmoothBranching, 950, 300>       section with a 3 dB knee on the same element, the same side chain
//                                                 and the same times, above the 4:1 curve ("like a compressor followed
//                                                 by a limiter"); out in COMPRESS
//     Colour     TubePushPull                     the remote-cutoff tubes and transformers (VOICE locked 5670); INPUT
//                                                 drives them and the threshold alike
//     ScShape    Flat                             (SC HPF is the unit's own 100 Hz switch, the host's SC HPF)
//     kTopologies FB
//
// Internals (kMuMastering.internals), as Diode 609's: COMP GR (the 4:1 curve's), LIMIT GR (the 20:1 section's, the
// history internal), LIMIT WINS.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/TubePushPull.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/SharedElementMax.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kMuMastering;       // MuMasteringDesc.cpp

struct MuMastering {
    static constexpr const ModeDescriptor& desc = kMuMastering;

    static constexpr int kRiseSlopePermille = 950;  // the rise's asymptote: 20:1 (S = 0.95)
    static constexpr int kRiseKneeCentiDb = 300;    // [H, fitted] its knee at the output: 3 dB

    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::SharedElementMaxT<stage::PeakLog, stage::SmoothBranching, kRiseSlopePermille,
                                                kRiseKneeCentiDb>;
    using Colour     = stage::TubePushPull;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // UiFrame::internals[0..2] (kMuMastering.internals); ModeEngine zeroes `out` first. As Diode 609's.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const float w2 = ::fcdsp::detail::modeengine::rampShape(engine.s2On_.cur);   // ModeEngine's s2GrDb weight
        const simd::f32x4 comp = Ballistics::grDb(engine.bal_);
        const simd::f32x4 lim = simd::mul(Stage2::grDb(engine.s2_), simd::set1(w2));
        const simd::f32x4 element = simd::max(comp, lim);
        const bool second = simd::lane<1>(element) > simd::lane<0>(element);
        const float c = second ? simd::lane<1>(comp) : simd::lane<0>(comp);
        const float l = second ? simd::lane<1>(lim) : simd::lane<0>(lim);
        out[0] = c;                                                            // COMP GR (DB)
        out[1] = l;                                                            // LIMIT GR (DB)
        out[2] = l > c ? 1.0f : 0.0f;                                          // LIMIT WINS
    }
};

static_assert(std::is_same_v<MuMastering::Stage2::Detector, MuMastering::Detector>);
static_assert(Stage2Policy<MuMastering::Stage2> && HasCombineStatic<MuMastering::Stage2>);
static_assert(MuMastering::Stage2::kSlope == 0.95f && MuMastering::Stage2::kKneeDb == 3.0f);

} // namespace fcdsp::modes
