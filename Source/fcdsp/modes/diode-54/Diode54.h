#pragma once

// Diode 54 (slot 11, `diode-54`): the traits (01 §8.2; D §2.5 the Neve 2254, M17: a compressor and a limiter, both
// feedback, sharing one diode-bridge gain element). The descriptor, physical() and specs are Diode54Desc.cpp's;
// Diode54.cpp instantiates the engine with FCDSP_DEFINE_MODE(Diode54). The Mode sheet, with every [H] constant, its
// source and the revision history, is docs/modes/diode-54.md. The engine is Diode 609's (Diode609.h, the 33609 of the
// same lineage); the 2254 differs in its switches, its single AUTO recovery and its limiter's range.
//
// Full traits, feedback only:
//
//     Detector   PeakLog                          PEAK after the gain element (both side chains are feedback)
//     Computer   QuadKnee (FB closed form)        THRESHOLD -20 ... +10 dBu, RATIO 1.5 / 2 / 3 / 4 / 6, the knee derived
//                                                 from RATIO ("fairly soft ... over a 10dB range")
//     Link       LinkMax                          LINK DUAL / STEREO ("outputs of both side-chains are combined")
//     Ballistics AutoSwitch<SmoothBranching,      ATTACK 5 MS fixed or the /R's FAST pot 0.1–2 ms (a hybrid); RECOVERY
//                DualReleaseT<1, 2, 3>, CompAuto> .1 .2 .8 S on SmoothBranching, AUTO on DualRelease
//     Stage2     SharedElementMax<PeakLog,        the LIMITER on the aux lanes, max-combined on the shared element:
//                AutoSwitch<SmoothBranching,      LIMIT THRESHOLD +4 ... +20 dBu / OFF, LIMIT ATTACK as ATTACK, LIMIT
//                DualReleaseT<4, 5, 6>,           RECOVERY .1 .2 .8 S + AUTO
//                LimitAuto>>
//     Colour     DiodeBridge                      the bridge (GR-dependent) and the transformers; VOICE locked DIODE
//     ScShape    Flat                             (the 2254 has no SLOW side-chain high-pass; the host SC HPF is the
//                                                 extension)
//     kTopologies FB
//
// EngineParams::m slots, written by diode54Physical (ADR-64: each switch's AUTO position through its own slots):
//     m[1], m[2], m[3]  compressor AUTO: DualRelease tau_Rf, tau_C, tau_Rs (ms); 0 on the manual positions
//     m[4], m[5], m[6]  limiter AUTO: the same for the limiter
//
// Internals (kDiode54.internals), as Diode 609's: COMP GR (DB), LIMIT GR (DB) (the history internal), LIMIT WINS.

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
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/SharedElementMax.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kDiode54;           // Diode54Desc.cpp

struct Diode54 {
    static constexpr const ModeDescriptor& desc = kDiode54;

    // EngineParams::m slots (header comment), written by diode54Physical.
    static constexpr int kCompFastSlot = 1, kCompChargeSlot = 2, kCompSlowSlot = 3;
    static constexpr int kLimitFastSlot = 4, kLimitChargeSlot = 5, kLimitSlowSlot = 6;

    // The AutoSwitch selectors (ADR-64): each recovery switch's AUTO position, by its own slot.
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
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // UiFrame::internals[0..2] (kDiode54.internals); ModeEngine zeroes `out` first. As Diode 609's (Diode609.h).
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

static_assert(std::is_same_v<Diode54::Stage2::Detector, Diode54::Detector>);
static_assert(Stage2Policy<Diode54::Stage2> && HasCombineStatic<Diode54::Stage2>);
static_assert(BallisticsPolicy<Diode54::Ballistics> && HasCommitFbRhat<Diode54::Ballistics>);
static_assert(HasCommitFbRhat<Diode54::LimitBallistics> && HasRhatFb<Diode54::Computer>);

} // namespace fcdsp::modes
