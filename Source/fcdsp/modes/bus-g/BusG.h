#pragma once

// Bus G (slot 1, `bus-g`): the traits (01 §8.2, §10.4). The descriptor, physical() and specs are BusGDesc.cpp's;
// BusG.cpp instantiates the engine with FCDSP_DEFINE_MODE(BusG). The Mode sheet, with every [H] constant, its source
// and the revision history, is docs/modes/bus-g.md.
//
// Full traits (M1, S7; 01 §10.4), feed-forward only:
//
//     Detector   PeakLog                                         PEAK, fixed in this circuit
//     Computer   QuadKnee                                        THRESHOLD (dial +-15), RATIO 2 / 4 / 10, knee = RATIO
//     Link       LinkMax                                         LINK 0-100 % (extension; the hardware is linked)
//     Ballistics AutoSwitch<SmoothBranching, DualRelease>        ATTACK .1 .3 1 3 10 30 MS; RELEASE .1 .3 .6 1.2 S on
//                                                                SmoothBranching, AUTO (the fifth position) on
//                                                                DualRelease (m[0..2] = 100 / 300 / 1000 ms)
//     Stage2     NoStage2
//     Colour     ColourSelect<VcaBus, ColourNone>                VOICE VCA (+ DRIVE, extension) / CLEAN
//     ScShape    Flat                                            (the host's SC HPF acts before it; an extension)
//
// Rigor modelled: the curve and the times are held to the published switch values and their [H] fits (dsp.static,
// dsp.time: every ratio, attack and release detent), and the default voice's colour stays inside the meter-truth
// budget of a modelled Mode (VcaBus.h).
//
// Internals (kBusG.internals, 01 §10.4), each on the louder of lanes 0-1:
//   FAST ENV (DB)   the fast path's GR: DualRelease's r_f under AUTO, the manual release's GR otherwise
//   SLOW ENV (DB)   the slow path's GR under AUTO (the history internal), 0 otherwise
//   AUTO SLOW       1 while the slow path holds the GR (r_s > r_f), else 0 (0 off AUTO)

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/DualRelease.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/colour/VcaBus.h"
#include "fcdsp/engine/stages/combinators/AutoSwitch.h"
#include "fcdsp/engine/stages/combinators/ColourSelect.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kBusG;              // BusGDesc.cpp

struct BusG {
    static constexpr const ModeDescriptor& desc = kBusG;
    using Detector   = stage::PeakLog;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::AutoSwitch<stage::SmoothBranching, stage::DualRelease>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::ColourSelect<stage::VcaBus, stage::ColourNone>;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // DualRelease's time slots (EngineParams::m, ms), written by busGPhysical under RELEASE AUTO.
    static constexpr int kFastReleaseSlot = 0, kChargeSlot = 1, kSlowReleaseSlot = 2;
    static_assert(std::is_same_v<stage::DualRelease,
                                 stage::DualReleaseT<kFastReleaseSlot, kChargeSlot, kSlowReleaseSlot>>);

    // UiFrame::internals[0..2] (kBusG.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const auto& s = engine.bal_;
        if (!Ballistics::onB(engine.bc_, s))
        {
            out[0] = louder(stage::SmoothBranching::grDb(s.a));                   // FAST ENV (DB)
            return;
        }
        const simd::f32x4 rf = stage::DualRelease::fastDb(s.b), rs = stage::DualRelease::slowDb(s.b);
        const bool first = simd::lane<0>(simd::max(rf, rs)) >= simd::lane<1>(simd::max(rf, rs));
        const float f = first ? simd::lane<0>(rf) : simd::lane<1>(rf);
        const float sl = first ? simd::lane<0>(rs) : simd::lane<1>(rs);
        out[0] = f;                                                                // FAST ENV (DB)
        out[1] = sl;                                                               // SLOW ENV (DB)
        out[2] = sl > f ? 1.0f : 0.0f;                                             // AUTO SLOW
    }

private:
    static float louder(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const float a = simd::lane<0>(v), b = simd::lane<1>(v);
        return a > b ? a : b;
    }
};

} // namespace fcdsp::modes
