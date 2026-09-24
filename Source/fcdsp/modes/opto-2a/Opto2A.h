#pragma once

// Opto 2A (slot 3, `opto-2a`): the traits (01 §8.2, §10.6). The descriptor, physical() and specs are Opto2ADesc.cpp's;
// Opto2A.cpp instantiates the engine with FCDSP_DEFINE_MODE(Opto2A). The Mode sheet, with the T4 cell model, every [H]
// constant, its source and fit, and the revision history, is docs/modes/opto-2a.md.
//
// Full traits (M3, S9; 01 §10.6), feedback only:
//
//     Detector   OptoSense                              the EL panel's drive: rectified, held peak (DetectorLaw custom)
//     Computer   FeedbackDelayed<OptoCellCurve>         the T4's steady state (law::LdrShunt, exponent = loop gain k),
//                                                       in the one-sample-delay loop with its runtime guard: at the
//                                                       cell's open-loop attack the bound k <= alpha / (1 - alpha) is
//                                                       661 (COMP) and 2,205 (LIMIT) at 22.05 kHz, so the delayed loop
//                                                       always runs; a violated bound falls back to FeedbackZdf per
//                                                       sample (K2 #5c)
//     Link       LinkMax                                DUAL / LINK (link 0 / 1), after the per-lane solve (K2 #5b)
//     Ballistics OptoCell                               the fast path (attack, 60 ms to 50 %) and the slow part (the
//                                                       memory: charge 5 s, release 5 s; beta = 1/2), m[2..4]
//     Stage2     NoStage2
//     Colour     TubeTransformer                        VOICE locked TUBE (+ DRIVE, the extension)
//     ScShape    R37Shelf                               EMPHASIS: the R37 low shelf, m[0] = 0-10 dB below 1 kHz; the
//                                                       host's `sce` tilt is handed 0 dB/oct (S9 lead revision 4)
//     kTopologies FB
//
// Rigor character: the curve and the times are held to the declared curve and the published program ranges (attack
// 5-20 ms, release 40-80 ms to 50 %), fidelity rows blocking.
//
// Internals (kOpto2A.internals, 01 §10.6), each on the louder of lanes 0-1:
//   LIGHT         the panel's light as the attenuation it would settle on, 1 - 10^(-u / 20) (0 dark ... 1)
//   G FAST        the fast path's share of the cell's attenuation, 10^(-s / 20) - 10^(-r / 20)
//   G SLOW        the slow part's, 1 - 10^(-s / 20)
//   MEMORY (%)    the slow part's charge against its full share of the GR held, 100 min(1, s / (beta r)): builds over
//                 seconds of program, 100 % through the memory's tail (the history internal)
//   LDR (KOHM)    the photoresistor, R_series / g, g = 10^(r / 20) - 1 (law::LdrShunt; 1000 = dark)

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

extern const ModeDescriptor kOpto2A;            // Opto2ADesc.cpp

struct Opto2A {
    static constexpr const ModeDescriptor& desc = kOpto2A;
    using Detector   = stage::OptoSense;
    using Computer   = stage::FeedbackDelayed<stage::OptoCellCurve>;
    using Link       = stage::LinkMax;
    using Ballistics = stage::OptoCell;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::TubeTransformer;
    using ScShape    = stage::R37Shelf;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // EngineParams::m slots, written by optoPhysical (Opto2ADesc.cpp).
    static constexpr int kEmphasisSlot = 0;         // R37 amount, dB (R37Shelf)
    static constexpr int kLimitSlot = 1;            // 1 in LIMIT (01 §10.6; the law change itself is the loop gain k)
    static constexpr int kShareSlot = 2, kChargeSlot = 3, kSlowSlot = 4;    // OptoCell: beta, tau_m, tau_s (ms)
    static_assert(std::is_same_v<stage::OptoCell, stage::OptoCellT<kShareSlot, kChargeSlot, kSlowSlot>>);
    static_assert(std::is_same_v<stage::R37Shelf, stage::R37ShelfT<kEmphasisSlot>>);

    // UiFrame::internals[0..4] (kOpto2A.internals); ModeEngine zeroes `out` first.
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
