#pragma once

// Diode 609 (slot 5, `diode-609`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are
// Diode609Desc.cpp's; Diode609.cpp instantiates the engine with FCDSP_DEFINE_MODE(Diode609). The Mode sheet, with every
// [H] constant, its source and the revision history, is docs/modes/diode-609.md. The unit is D §2.5's Neve 33609 (with
// the 2254's steps where the 33609's are unverified): a compressor and a limiter, both feedback, sharing one
// diode-bridge gain element.
//
// Full traits (M5, S10; 01 §10.7), feedback only:
//
//     Detector   PeakLog                          PEAK after the gain element (both side chains are feedback)
//     Computer   QuadKnee (FB closed form)        THRESHOLD -20 ... +10 dBu, RATIO 1.5 / 2 / 3 / 4 / 6, the progressive
//                                                 knee derived from RATIO (the input knee spans 10 dB)
//     Link       LinkMax                          LINK DUAL / STEREO, after the per-lane solve (K2 #5b)
//     Ballistics AutoSwitch<SmoothBranching,      ATTACK FAST / SLOW (3 / 6 ms closed loop, ADR-63); RELEASE .1 .4 .8
//                DualReleaseT<1, 2, 3>, CompAuto> 1.5 S on SmoothBranching, A1 / A2 on DualRelease inside the FB solve
//                                                 (max of the fast and slow roots, 01 §5.3; exact commit with r^, X10)
//     Stage2     SharedElementMax<PeakLog,        the LIMITER on the aux lanes, max-combined with the compressor on the
//                AutoSwitch<SmoothBranching,      shared element (SharedElementMax.h): LIMIT THRESHOLD +4 ... +15 dBu /
//                DualReleaseT<4, 5, 6>,           OFF, LIMIT ATTACK FAST / SLOW (2 / 4 ms closed loop), LIMIT RELEASE 50
//                LimitAuto>>                      100 200 800 MS + A1 / A2 (DualRelease as the compressor's); on and off
//                                                 through ModeEngine's 20 ms s2On_ ramp, never a step
//     Colour     DiodeBridge                      the bridge (GR-dependent) and the transformers; VOICE locked DIODE
//     ScShape    SlowHpT<0>                       ATTACK SLOW: the compressor side chain's 100 Hz high-pass (m[0]),
//                                                 faded over 20 ms; the limiter's side chain (aux lanes) is not shaped
//     kTopologies FB
//
// EngineParams::m slots, written by diodePhysical (ADR-64: the A1/A2 tags are an OR over both release switches, so each
// switch's AUTO position is routed through its own slots, and each AutoSwitch selects on them, not on the tags):
//     m[0]              SLOW high-pass corner, Hz (100; 0 = ATTACK FAST)
//     m[1], m[2], m[3]  compressor A1 / A2: DualRelease tau_Rf, tau_C, tau_Rs (ms); 0 on the manual positions
//     m[4], m[5], m[6]  limiter A1 / A2: the same for the limiter; 0 on its manual positions
//
// Internals (kDiode609.internals, E §7: two-stage), on the channel whose element GR is larger:
//   COMP GR (DB)       the compressor's GR (stage 1: the ballistics' value, before the element's max)
//   LIMIT GR (DB)      the limiter's GR x the settled s2On_ amount (what ModeEngine's s2GrDb reports; the history
//                      internal); with the compressor holding the element it is the limiter's own control (<= COMP GR)
//   LIMIT WINS         1 while the limiter holds the element (LIMIT GR > COMP GR), else 0

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/DualRelease.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/DiodeBridge.h"
#include "fcdsp/engine/stages/combinators/AutoSwitch.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/SlowHp.h"
#include "fcdsp/engine/stages/stage2/SharedElementMax.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kDiode609;          // Diode609Desc.cpp

struct Diode609 {
    static constexpr const ModeDescriptor& desc = kDiode609;

    // EngineParams::m slots (header comment), written by diodePhysical.
    static constexpr int kSlowHpSlot = 0;
    static constexpr int kCompFastSlot = 1, kCompChargeSlot = 2, kCompSlowSlot = 3;
    static constexpr int kLimitFastSlot = 4, kLimitChargeSlot = 5, kLimitSlowSlot = 6;

    // The AutoSwitch selectors (ADR-64): each release switch's AUTO position, by its own slot.
    struct CompAuto {
        static bool useB(const EngineParams& p) noexcept FCDSP_NONBLOCKING { return p.m[kCompFastSlot] > 0.0f; }
    };
    struct LimitAuto {
        static bool useB(const EngineParams& p) noexcept FCDSP_NONBLOCKING { return p.m[kLimitFastSlot] > 0.0f; }
    };

    using CompRelease  = stage::DualReleaseT<kCompFastSlot, kCompChargeSlot, kCompSlowSlot>;
    using LimitRelease = stage::DualReleaseT<kLimitFastSlot, kLimitChargeSlot, kLimitSlowSlot>;
    using LimitBallistics = stage::AutoSwitch<stage::SmoothBranching, LimitRelease, LimitAuto>;

    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::AutoSwitch<stage::SmoothBranching, CompRelease, CompAuto>;
    using Stage2     = stage::SharedElementMax<stage::PeakLog, LimitBallistics>;
    using Colour     = stage::DiodeBridge;
    using ScShape    = stage::SlowHpT<kSlowHpSlot>;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // UiFrame::internals[0..2] (kDiode609.internals, header comment); ModeEngine zeroes `out` first.
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

// The limiter's detector lanes are the Mode detector's aux lanes (SharedElementMax.h): the same policy.
static_assert(std::is_same_v<Diode609::Stage2::Detector, Diode609::Detector>);
static_assert(Stage2Policy<Diode609::Stage2> && HasCombineStatic<Diode609::Stage2>);
static_assert(Diode609::Stage2::kSlope == 0.99f && Diode609::Stage2::kKneeDb == 0.5f);   // the diode's limiter, bit for bit
static_assert(BallisticsPolicy<Diode609::Ballistics> && HasCommitFbRhat<Diode609::Ballistics>);
static_assert(HasCommitFbRhat<Diode609::LimitBallistics> && HasRhatFb<Diode609::Computer>);

} // namespace fcdsp::modes
