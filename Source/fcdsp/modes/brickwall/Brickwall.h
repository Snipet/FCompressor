#pragma once

// Brickwall (slot 7, `brickwall`): the traits (01 §8.2, §10.7; E §5.4; K2 #21). The descriptor, physical() and specs
// are BrickwallDesc.cpp's; Brickwall.cpp instantiates the engine with FCDSP_DEFINE_MODE(Brickwall). The Mode sheet is
// docs/modes/brickwall.md.
//
// A lookahead limiter (M7, S11; provisional cleared):
//
//     slot        policy                                      parameters
//     Detector    DetSelect<PeakLog, TruePeak4x>              DETECT PEAK / TP (kEngTruePeak; TP's 4x interpolator
//                                                             delays the SC by D_tp = 12, reported by scDelaySamples
//                                                             below and subtracted from the host's SC delay, K2 #21a)
//     Computer    QuadKnee                                    THRESHOLD -30...0, RATIO locked inf:1, KNEE 0-6 dB
//     Link        LinkMax                                     LINK 0-100 % (on the targets, before the box, E §8)
//     Ballistics  CrestAuto<SlidingMaxBox>                    ATTACK = LOOKAHEAD 0.5-20 ms (budget-clamped; the ramp),
//                                                             RELEASE 1-1000 ms, TIME MODE MAN / AUTO (CrestAuto's
//                                                             program-dependent release, kEngAutoRelease)
//     Stage2      NoStage2
//     Colour      ColourSelect<ColourNone, LoudClip>          VOICE CLEAN / LOUD
//     ScShape     Flat
//     kTopologies FF
//
// The lookahead: the host delays the audio by L_la and the side chain by L_la - look + D_up - D_tp, so the detector
// sees `look` samples ahead; SlidingMaxBox holds the GR over [n + look - 1, n + look + 1] for the target it computed at
// n (SlidingMaxBox.h "Alignment"). The ceiling: CEILING (makeup) = the output level of a held peak; physical() sets
// makeupDb = CEILING - THRESHOLD (+ LoudClip::kHeadroomDb in LOUD), so the threshold lands on the ceiling.
//
// Internals (UiFrame words 0-2, kBrickwallInt): HELD PEAK (dBFS: the input peak level the limiter holds its applied GR
// for, the inf:1 curve inverted at the applied GR, which the release keeps smooth between waveform peaks; -60, the
// readout's floor, while no GR is applied; the history internal),
// LOOK EFF (ms: the lookahead the window runs now, which follows `look` one sample per tick), TP OVER (dB: the held true
// peak over the held sample peak, 1 s holds; 0 at DETECT PEAK).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/SlidingMaxBox.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/colour/LoudClip.h"
#include "fcdsp/engine/stages/combinators/ColourSelect.h"
#include "fcdsp/engine/stages/combinators/CrestAuto.h"
#include "fcdsp/engine/stages/combinators/DetSelect.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/detector/TruePeak4x.h"
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
    using Detector   = stage::DetSelect<stage::PeakLog, stage::TruePeak4x>;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::CrestAuto<stage::SlidingMaxBox>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::ColourNone, stage::LoudClip>;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // Step indices (BrickwallDesc.cpp): DETECT kDet = DetSelect indices, VOICE kVoice = ColourSelect indices.
    static constexpr int kDetPeak = 0, kDetTruePeak = 1;
    static constexpr int kVoiceClean = 0, kVoiceLoud = 1;

    // The engine's own SC delay (IEngine::scDelaySamples, ModeEngine's optional Traits hook): D_tp at DETECT TP (the
    // step DetSelect runs: clamp(det, 0, 1)).
    static int scDelaySamples(const EngineParams& p) noexcept FCDSP_NONBLOCKING
    {
        return p.det >= kDetTruePeak ? stage::TruePeak4x::kDelay : 0;
    }

    static constexpr float kHeldFloorDb = -60.0f;   // HELD PEAK while no GR is applied (kBrickwallInt's lo)

    // UiFrame::internals[0..2] (header comment); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 applied = stage::SlidingMaxBox::grDb(engine.bal_.inner);
        const float r = simd::lane<1>(applied) > simd::lane<0>(applied) ? simd::lane<1>(applied) : simd::lane<0>(applied);
        const float thr = simd::lane<0>(engine.lvl_.cur);                  // the smoothed threshold (detector dB)
        const float w = simd::lane<1>(engine.lvl2_.cur);                   // the smoothed knee width
        if (r > 0.0f)
        {
            // inf:1 over QuadKnee's knee: r = (o + W/2)^2 / (2W) inside it, r = o above it (o = x - T)
            const float wk = w > stage::QuadKnee::kMinKneeDb ? w : stage::QuadKnee::kMinKneeDb;
            const float o = r >= 0.5f * wk ? r : simd::lane<0>(simd::sqrt(simd::set1(2.0f * wk * r))) - 0.5f * wk;
            out[0] = thr + o - engine.p_.preGainDb;                        // HELD PEAK (DB, plugin input)
        }
        else
            out[0] = kHeldFloorDb;
        out[1] = stage::SlidingMaxBox::lookNowMs(engine.bc_.inner);         // LOOK EFF (MS)
        if (Detector::which(engine.det_) == kDetTruePeak)
        {
            const simd::f32x4 over = stage::TruePeak4x::overDb(Detector::template sub<kDetTruePeak>(engine.det_));
            out[2] = simd::lane<0>(over) > simd::lane<1>(over) ? simd::lane<0>(over) : simd::lane<1>(over);
        }
    }
};

static_assert(BallisticsPolicy<Brickwall::Ballistics> && DetectorPolicy<Brickwall::Detector>);

} // namespace fcdsp::modes
