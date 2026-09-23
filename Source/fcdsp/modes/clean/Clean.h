#pragma once

// Clean (slot 0, `clean`): the traits (01 §8.2, §10.3). The descriptor, physical() and specs are CleanDesc.cpp's;
// Clean.cpp instantiates the engine with FCDSP_DEFINE_MODE(Clean). The Mode sheet is docs/modes/clean.md.
//
// Full traits (F9, S3; 01 §10.3), feed-forward only:
//
//     Detector   DetSelect<PeakLog, RmsLog, DualDet>          DETECT PEAK / RMS (20 ms) / PK+RMS
//     Computer   QuadKnee                                      threshold, ratio 1:1-inf:1, knee 0-72 dB
//     Link       LinkMax                                       link 0-100 %
//     Ballistics Hold<CrestAuto<SmoothBranching>>              HOLD 0-500 ms; TIME MODE AUTO: crest-factor release
//     Stage2     NoStage2
//     Colour     ColourSelect<ColourNone, TubeSym, DiodeAsym, Bright>   VOICE OFF / TUBE / DIODE / BRIGHT (+ DRIVE)
//     ScShape    Flat
//
// Rigor clean: with VOICE OFF a below-threshold Clean nulls bit-exactly (ColourNone, D8 (b)); every detector and voice
// is held to its declared curve (dsp.static: the textbook curve, and for a voice its describing-function gain).
//
// Internals (kClean.internals, 01 §10.3), each the louder of lanes 0-1:
//   REL EFF (MS)   the release time in force: the published one, or TIME MODE AUTO's crest-scaled one (CrestAuto)
//   CREST (DB)     the side chain's crest factor, 10 log10(peak^2 / ms) over 200 ms (0 dB square, 3 dB sine)
//   PEAK DET (DB)  the peak detector's level (PEAK; the peak half of PK+RMS)
//   RMS DET (DB)   the RMS detector's level (RMS; the RMS half of PK+RMS)
// A detector that is not running (PEAK DET under RMS, RMS DET under PEAK), and any level below it, reads the declared
// floor, -60 dB.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/Bright.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/colour/DiodeAsym.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/engine/stages/combinators/ColourSelect.h"
#include "fcdsp/engine/stages/combinators/CrestAuto.h"
#include "fcdsp/engine/stages/combinators/DetSelect.h"
#include "fcdsp/engine/stages/combinators/Hold.h"
#include "fcdsp/engine/stages/detector/DualDet.h"
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

extern const ModeDescriptor kClean;             // CleanDesc.cpp

struct Clean {
    static constexpr const ModeDescriptor& desc = kClean;
    using Detector   = stage::DetSelect<stage::PeakLog, stage::RmsLog, stage::DualDet>;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::Hold<stage::CrestAuto<stage::SmoothBranching>>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::ColourNone, stage::TubeSym, stage::DiodeAsym, stage::Bright>;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // DETECT steps (kDet in CleanDesc.cpp) = DetSelect indices.
    static constexpr int kDetPeak = 0, kDetRms = 1, kDetPeakRms = 2;

    // UiFrame::internals[0..3] (kClean.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        out[0] = louder(Ballistics::releaseNowMs(engine.bc_, engine.bal_));        // REL EFF (MS)
        out[1] = louder(Ballistics::crestDb(engine.bal_));                          // CREST (DB)
        const auto& d = engine.det_;
        float peak = kDetFloorDb, rms = kDetFloorDb;
        const int which = Detector::which(d);
        if (which == kDetPeak)
            peak = louder(stage::PeakLog::levelDb(Detector::sub<kDetPeak>(d)));
        else if (which == kDetRms)
            rms = louder(stage::RmsLog::levelDb(Detector::sub<kDetRms>(d)));
        else
        {
            peak = louder(stage::DualDet::peakDb(Detector::sub<kDetPeakRms>(d)));
            rms = louder(stage::DualDet::rmsDb(Detector::sub<kDetPeakRms>(d)));
        }
        out[2] = peak > kDetFloorDb ? peak : kDetFloorDb;                           // PEAK DET (DB)
        out[3] = rms > kDetFloorDb ? rms : kDetFloorDb;                             // RMS DET (DB)
    }

private:
    static constexpr float kDetFloorDb = -60.0f;       // kClean.internals[2, 3].lo

    static float louder(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const float a = simd::lane<0>(v), b = simd::lane<1>(v);
        return a > b ? a : b;
    }
};

} // namespace fcdsp::modes
