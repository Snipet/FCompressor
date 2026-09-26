#pragma once

// Octo (slot 8, `octo`): the traits (01 §8.2; D §2.7 the hybrid VCA, M18). The descriptor, physical() and specs are
// OctoDesc.cpp's; Octo.cpp instantiates the engine with FCDSP_DEFINE_MODE(Octo). The Mode sheet, with every [H]
// constant, its source and the revision history, is docs/modes/octo.md.
//
// Full traits, feed-forward only:
//
//     Detector   PeakLog                                         the VCA detector (locked)
//     Computer   QuadKnee                                        INPUT drives a fixed threshold; RATIO 1 2 3 4 6 10
//                                                                (OPTO) 20 NUKE, each with its own knee (knee = RATIO)
//     Link       LinkMean                                        LINK sums the two detectors (the GR's mean, E §8)
//     Ballistics AutoSwitch<SmoothBranching, DualRelease, Opto>  ATTACK 0.05–30 ms, RELEASE 0.05–3.5 s on
//                                                                SmoothBranching; 10:1 OPTO runs DualRelease: a fast
//                                                                release, then a slow tail (m[0..2]), up to 20 s
//     Stage2     NoStage2
//     Colour     ColourSelect<OctoDistT ×6>                      AUDIO CLEAN / HP / DIST 2 / DIST 2 + HP / DIST 3 /
//                                                                DIST 3 + HP: harmonics that grow with the GR
//     ScShape    BandEmphasisT<3>                                DETECT BAND EMPH: a 6 kHz bell in the side chain (m[3])
//
// Internals (kOcto.internals), each on the louder of lanes 0-1:
//   FAST ENV (DB)   the fast path's GR: DualRelease's r_f in OPTO, the manual release's GR otherwise
//   SLOW ENV (DB)   the slow path's GR in OPTO (the history internal), 0 otherwise
//   OPTO SLOW       1 while the slow path holds the GR (r_s > r_f), else 0 (0 off OPTO)

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/DualRelease.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/OctoDist.h"
#include "fcdsp/engine/stages/combinators/AutoSwitch.h"
#include "fcdsp/engine/stages/combinators/ColourSelect.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMean.h"
#include "fcdsp/engine/stages/scshape/BandEmphasis.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kOcto;              // OctoDesc.cpp

struct Octo {
    // EngineParams::m[] slots, written by octoPhysical.
    static constexpr int kFastReleaseSlot = 0, kChargeSlot = 1, kSlowReleaseSlot = 2;   // DualRelease (OPTO)
    static constexpr int kEmphasisSlot = 3;                                             // BandEmphasis gain (dB)
    static constexpr int kOptoSlot = 4;                                                 // 1 = 10:1 OPTO

    // 10:1 OPTO selects DualRelease (the ratio's step, not a release tag: ADR-64's Select).
    struct OptoSelect {
        static bool useB(const EngineParams& p) noexcept FCDSP_NONBLOCKING { return p.m[kOptoSlot] > 0.5f; }
    };

    static constexpr const ModeDescriptor& desc = kOcto;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMean;
    using Ballistics = stage::AutoSwitch<stage::SmoothBranching,
                                         stage::DualReleaseT<kFastReleaseSlot, kChargeSlot, kSlowReleaseSlot>,
                                         OptoSelect>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::OctoDistT<0, false>, stage::OctoDistT<0, true>,
                                           stage::OctoDistT<1, false>, stage::OctoDistT<1, true>,
                                           stage::OctoDistT<2, false>, stage::OctoDistT<2, true>>;
    using ScShape    = stage::BandEmphasisT<kEmphasisSlot>;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // UiFrame::internals[0..2] (kOcto.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        using Dual = stage::DualReleaseT<kFastReleaseSlot, kChargeSlot, kSlowReleaseSlot>;
        const auto& s = engine.bal_;
        if (!Ballistics::onB(engine.bc_, s))
        {
            out[0] = louder(stage::SmoothBranching::grDb(s.a));                   // FAST ENV (DB)
            return;
        }
        const simd::f32x4 rf = Dual::fastDb(s.b), rs = Dual::slowDb(s.b);
        const bool first = simd::lane<0>(simd::max(rf, rs)) >= simd::lane<1>(simd::max(rf, rs));
        const float f = first ? simd::lane<0>(rf) : simd::lane<1>(rf);
        const float sl = first ? simd::lane<0>(rs) : simd::lane<1>(rs);
        out[0] = f;                                                                // FAST ENV (DB)
        out[1] = sl;                                                               // SLOW ENV (DB)
        out[2] = sl > f ? 1.0f : 0.0f;                                             // OPTO SLOW
    }

private:
    static float louder(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const float a = simd::lane<0>(v), b = simd::lane<1>(v);
        return a > b ? a : b;
    }
};

} // namespace fcdsp::modes
