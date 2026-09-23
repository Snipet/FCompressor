#pragma once

// Clean (slot 0, `clean`): the traits (01 §8.2, §10.3). The descriptor, physical() and specs are CleanDesc.cpp's;
// Clean.cpp instantiates the engine with FCDSP_DEFINE_MODE(Clean).
//
// WALKING SKELETON (F3, S2): feed-forward, one kernel, the seven S2 policies. 01 §10.3's full traits arrive with F9
// (S3), which also clears kClean.provisional:
//
//     Detector   DetSelect<PeakLog, RmsLog, DualDet>      here: PeakLog (det PEAK/RMS/PK+RMS all run the peak law)
//     Computer   QuadKnee                                 as final
//     Link       LinkMax                                  as final
//     Ballistics Hold<CrestAuto<SmoothBranching>>         here: SmoothBranching (no hold, tmode AUTO not yet adaptive)
//     Stage2     NoStage2                                 as final
//     Colour     ColourSelect<ColourNone, TubeSym, ...>   here: ColourNone (every voice is clean; drive has no effect)
//     ScShape    Flat                                     as final
//
// Internals (kClean.internals, 01 §10.3): REL EFF = the release tau now (ms, the louder lane), CREST = 0 dB (no crest
// detector yet), PEAK DET = the peak detector's level (dB, the louder lane), RMS DET = the declared floor (-60 dB, no
// RMS detector yet).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
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

extern const ModeDescriptor kClean;             // CleanDesc.cpp (provisional until F9)

struct Clean {
    static constexpr const ModeDescriptor& desc = kClean;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourNone;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // UiFrame::internals[0..3] (kClean.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 rel = Ballistics::releaseNowMs(engine.bc_, engine.bal_);
        const simd::f32x4 lvl = Detector::levelDb(engine.det_);
        out[0] = louder(rel);                           // REL EFF (MS)
        out[1] = 0.0f;                                  // CREST (DB): CrestAuto arrives with F9
        out[2] = louder(lvl);                           // PEAK DET (DB)
        out[3] = kRmsDetFloorDb;                        // RMS DET (DB): RmsLog/DualDet arrive with F9
    }

private:
    static constexpr float kRmsDetFloorDb = -60.0f;    // kClean.internals[3].lo

    static float louder(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const float a = simd::lane<0>(v), b = simd::lane<1>(v);
        return a > b ? a : b;
    }
};

} // namespace fcdsp::modes
