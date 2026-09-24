#pragma once

// FET 76 (slot 2, `fet-76`): the traits (01 §8.2, §10.5). The descriptor, physical() and specs are Fet76Desc.cpp's;
// Fet76.cpp instantiates the engine with FCDSP_DEFINE_MODE(Fet76). The Mode sheet, with every [H] constant, its source
// and the revision history, is docs/modes/fet-76.md.
//
// Full traits (M2, S9; 01 §10.5), feedback only:
//
//     Detector   PeakLog                        PEAK sensing after the gain element (locked)
//     Computer   QuadKnee                       the FB loop's closed-form zero-delay solve (E §2.6; K2 #1): INPUT
//                                               drives a fixed threshold kT0 (+ the ratio's offset), RATIO 4 / 8 / 12 /
//                                               20 / ALL, knee = RATIO; law::FetVcr turns its GR into the element's
//                                               resistance and gate CV (the readouts) and its share of the divider
//                                               (FetColour)
//     Link       LinkMax                        LINK (the 1176SA pair: one control voltage), after the per-lane solve
//     Ballistics SmoothBranching                ATTACK 20-800 us (reversed knob), RELEASE 50-1100 ms (reversed knob);
//                                               physical() converts the published closed-loop attack to the loop's
//                                               open-loop tau (ADR-63), with the linked 40 us minimum and ALL's lag
//     Stage2     NoStage2
//     Colour     ColourSelect<FetColour>        VOICE = the revision's constants (LN, A, F); the FET's residual follows
//                                               the gain reduction (law::FetVcr share), ALL drives it harder
//     ScShape    Flat                           (the host's SC HPF, an extension, acts before it)
//
// The feedback kernel (K2 #1, #5): QuadKnee::solveFb (closed form over FbAffine) inside SmoothBranching::solveFb /
// commitFb, linked after the per-lane solve, so the loop is stable and ring-free at every attack and rate (the naive
// one-sample loop buzzes at 4:1 and 20 us at 48 kHz, E §2.6; dsp.fetcolour holds the solved loop to that). Every FB
// configuration is monotone (dsp.registry fb.monotone): ALL is realised by the loop gain (S = 1 clamps to k = 99), a
// threshold shift, a slower attack and more colour, never by a negative slope (K2 #5a). An external key evaluates the
// computer feed-forward on the key with the loop's open-loop times (E §2.6: the loop is open).
//
// Internals (kFet76.internals, 01 §10.5), from the louder of lanes 0-1, at the APPLIED GR (the ballistics' GR times the
// GR switch's shaped ramp, so GR OFF reads the FET pinched off):
//   LOOP CV (V)     law::FetVcr::cvVolts: the gate CV that sets the element to that GR (the history internal)
//   FET R (KOHM)    law::FetVcr::resistanceKohm: the channel's resistance (100 = pinched off)
//   H2, H3 (DB)     FetColour::harmonicsEstimate at the operating point: a sine at the level the static FB curve
//                   puts the gain element's output at for that GR (T + r/k above the knee, T - W/2 + sqrt(2 W r / k)
//                   inside it, T - W/2 without GR), relative to the fundamental

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/FetColour.h"
#include "fcdsp/engine/stages/combinators/ColourSelect.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/law/FetVcr.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kFet76;             // Fet76Desc.cpp

struct Fet76 {
    static constexpr const ModeDescriptor& desc = kFet76;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::SmoothBranching;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::FetColour>;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // UiFrame::internals[0..3] (kFet76.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = Ballistics::grDb(engine.bal_);
        const float r0 = simd::lane<0>(r), r1 = simd::lane<1>(r);
        const float gr = (r0 > r1 ? r0 : r1) * detail::modeengine::rampShape(engine.offAmt_.cur);
        out[0] = law::FetVcr::cvVolts(gr);                                          // LOOP CV (V)
        out[1] = law::FetVcr::resistanceKohm(gr);                                   // FET R (KOHM)
        const stage::FetColour::Harmonics h =
            stage::FetColour::harmonicsEstimate(engine.cc_.sub.head, operatingAmplitude(engine.p_, gr), gr);
        out[2] = h.h2Db;                                                            // H2 (DB)
        out[3] = h.h3Db;                                                            // H3 (DB)
    }

    // The peak amplitude of the gain element's output where the static FB curve applies `grDb` (header comment).
    static float operatingAmplitude(const EngineParams& p, float grDb) noexcept FCDSP_NONBLOCKING
    {
        const float k = stage::QuadKnee::loopGain(p.slope);
        const float w = p.kneeDb > stage::QuadKnee::kMinKneeDb ? p.kneeDb : stage::QuadKnee::kMinKneeDb;
        const float g = grDb > 0.0f ? grDb : 0.0f;
        float overDb = -0.5f * w;                                                   // no GR: the knee's foot
        if (k > 0.0f)
        {
            if (g >= 0.5f * k * w)
                overDb = g / k;                                                     // above the knee: r = k (y - T)
            else                                                    // inside the knee: r = k (y - T + W/2)^2 / 2W
                overDb = simd::lane<0>(simd::sqrt(simd::set1(2.0f * w * g / k))) - 0.5f * w;
        }
        return simd::lane<0>(linFromDb(simd::set1(p.thrDb + overDb)));
    }
};

} // namespace fcdsp::modes
