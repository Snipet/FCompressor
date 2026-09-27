#pragma once

// Opto 3A (slot 10, `opto-3a`): the traits (01 §8.2; D §2.2 the UREI LA-3A, M03: "a cheap variant of M02"). The
// descriptor, physical() and specs are Opto3ADesc.cpp's; Opto3A.cpp instantiates the engine with
// FCDSP_DEFINE_MODE(Opto3A). The Mode sheet, with every [H] constant, its source and fit, and the revision history, is
// docs/modes/opto-3a.md. The T4 cell model is Opto 2A's (docs/modes/opto-2a.md); this Mode differs in its constants.
//
// Full traits, feedback only:
//
//     Detector   OptoSense                              the EL panel's drive (Opto 2A's)
//     Computer   FeedbackDelayed<OptoCellCurve>         the T4's steady state; COMP 3:1 (k 2) / LIMIT 4:1 (k 3): the
//                                                       two "virtually indistinguishable unless very heavy compression"
//     Link       LinkMax                                DUAL / LINK (the barrier-strip link)
//     Ballistics OptoCell                               attack "1.5 ms or less", 60 ms to 50 %, then the memory: charge
//                                                       2 s, release 2 s (m[2..4]): quick after light, occasional GR,
//                                                       slower after continuous heavy GR
//     Stage2     NoStage2
//     Colour     ClassATransformer                      VOICE locked CLASS A + TRANSFORMERS (+ DRIVE, the extension)
//     ScShape    R37Shelf                               HF SENS: the rear side-chain pot, m[0] = 0-10 dB below 1 kHz
//     kTopologies FB
//
// Internals (kOpto3A.internals), each on the louder of lanes 0-1, as Opto 2A's: LIGHT, G FAST, G SLOW, MEMORY (%) (the
// history internal), LDR (KOHM).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/OptoCell.h"
#include "fcdsp/engine/stages/colour/TubeTransformer.h"
#include "fcdsp/engine/stages/combinators/FeedbackDelayed.h"
#include "fcdsp/engine/stages/detector/OptoSense.h"
#include "fcdsp/engine/stages/gain/OptoCellCurve.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/R37Shelf.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kOpto3A;            // Opto3ADesc.cpp

struct Opto3A {
    static constexpr const ModeDescriptor& desc = kOpto3A;
    using Detector   = stage::OptoSense;
    using Computer   = stage::FeedbackDelayed<stage::OptoCellCurve>;
    using Link       = stage::LinkMax;
    using Ballistics = stage::OptoCell;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ClassATransformer;
    using ScShape    = stage::R37Shelf;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // EngineParams::m slots, written by opto3aPhysical (Opto3ADesc.cpp).
    static constexpr int kHfSensSlot = 0;           // the HF SENS shelf amount, dB (R37Shelf)
    static constexpr int kLimitSlot = 1;            // 1 in LIMIT (informational: the law change is the loop gain k)
    static constexpr int kShareSlot = 2, kChargeSlot = 3, kSlowSlot = 4;    // OptoCell: beta, tau_m, tau_s (ms)
    static_assert(std::is_same_v<stage::OptoCell, stage::OptoCellT<kShareSlot, kChargeSlot, kSlowSlot>>);
    static_assert(std::is_same_v<stage::R37Shelf, stage::R37ShelfT<kHfSensSlot>>);

    // UiFrame::internals[0..4] (kOpto3A.internals); ModeEngine zeroes `out` first. As Opto 2A's (Opto2A.h).
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        using Law = stage::law::LdrShunt;
        const auto& s = engine.bal_;
        const simd::f32x4 r = Ballistics::appliedDb(s), sl = Ballistics::slowDb(s), u = Ballistics::lightDb(s);
        const bool second = simd::lane<1>(r) > simd::lane<0>(r);
        const float rr = second ? simd::lane<1>(r) : simd::lane<0>(r);
        const float ss = second ? simd::lane<1>(sl) : simd::lane<0>(sl);
        const float uu = second ? simd::lane<1>(u) : simd::lane<0>(u);
        alignas(16) const float v[4] = { uu, rr, ss, 0.0f };
        alignas(16) float att[4];
        simd::store(att, Law::attenuation(simd::load(v)));
        const float full = Ballistics::shareOf(engine.bc_) * rr;
        out[0] = att[0];                                                        // LIGHT
        out[1] = att[1] > att[2] ? att[1] - att[2] : 0.0f;                      // G FAST
        out[2] = att[2];                                                        // G SLOW
        out[3] = full > kMemoryFloorDb ? 100.0f * (ss < full ? ss / full : 1.0f) : 0.0f;   // MEMORY (%)
        out[4] = Law::ldrKohm(rr);                                              // LDR (KOHM)
    }

private:
    static constexpr float kMemoryFloorDb = 1e-3f;  // below this share of GR the memory reads 0
};

} // namespace fcdsp::modes
