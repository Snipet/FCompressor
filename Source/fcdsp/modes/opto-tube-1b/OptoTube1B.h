#pragma once

// Opto Tube 1B (slot 13, `opto-tube-1b`): the traits (01 §8.2; D §2.2 the Tube-Tech CL 1B, M04). The descriptor,
// physical() and specs are OptoTube1BDesc.cpp's; OptoTube1B.cpp instantiates the engine with
// FCDSP_DEFINE_MODE(OptoTube1B). The Mode sheet, with every [H] constant, its source and the revision history, is
// docs/modes/opto-tube-1b.md.
//
// Full traits, feed-forward (the CL 1B has a real threshold and a continuous ratio; its topology is [U], D §2.2):
//
//     Detector   OptoSense                                   the optical cell's light: the drive's held peak with the
//                                                            panel's persistence and afterglow (Opto 2A's sensor)
//     Computer   QuadKnee                                    THRESHOLD +20 … -40 dB (+ OFF), RATIO 2 … 10:1
//                                                            continuous, the opto cell's soft knee (locked)
//     Link       LinkMax                                     DUAL / LINK (the external side-chain bus)
//     Ballistics AutoSwitch<SmoothBranching,                 ATTACK/RELEASE SELECT: FIXED (1 ms / 50 ms) and MANUAL
//                DualReleaseT<0, 1, 2>, FixManSelect>        (the knobs, 0.5–300 ms / 0.05–10 s) on SmoothBranching;
//                                                            FIX/MAN on DualRelease: a fixed fast attack, a short peak
//                                                            released at the fixed 50 ms, and ATTACK (now DELAY) the
//                                                            time a peak must last before the manual RELEASE takes
//                                                            over (the slow path's charge)
//     Stage2     NoStage2
//     Colour     TubeTransformer                             VOICE locked TUBE (+ DRIVE, the extension)
//     ScShape    Flat                                        (SC LOW CUT, the Mk II's, is the host's SC HPF)
//
// EngineParams::m slots, written by optoTubePhysical: m[0] the fixed fast release, m[1] the DELAY (the slow path's
// charge), m[2] the manual release (DualRelease's tau_Rf, tau_C, tau_Rs, ms); m[3] = 1 in FIX/MAN (FixManSelect).
//
// Internals (kOptoTube1B.internals), on the louder of lanes 0-1: FAST ENV (DB) the fast path's GR (the manual one's
// off FIX/MAN), SLOW ENV (DB) the manual release's hold in FIX/MAN (the history internal), MAN HOLD (1 while it holds).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/DualRelease.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/TubeTransformer.h"
#include "fcdsp/engine/stages/combinators/AutoSwitch.h"
#include "fcdsp/engine/stages/detector/OptoSense.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kOptoTube1B;        // OptoTube1BDesc.cpp

struct OptoTube1B {
    // EngineParams::m slots (header comment), written by optoTubePhysical.
    static constexpr int kFastReleaseSlot = 0, kDelaySlot = 1, kManualReleaseSlot = 2;   // DualRelease (FIX/MAN)
    static constexpr int kFixManSlot = 3;                                                 // 1 = FIX/MAN

    // FIX/MAN selects DualRelease (the time-mode switch's own slot, not a tag: ADR-64's selector pattern).
    struct FixManSelect {
        static bool useB(const EngineParams& p) noexcept FCDSP_NONBLOCKING { return p.m[kFixManSlot] > 0.5f; }
    };
    using FixMan = stage::DualReleaseT<kFastReleaseSlot, kDelaySlot, kManualReleaseSlot>;

    static constexpr const ModeDescriptor& desc = kOptoTube1B;
    using Detector   = stage::OptoSense;
    using Computer   = stage::QuadKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::AutoSwitch<stage::SmoothBranching, FixMan, FixManSelect>;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::TubeTransformer;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFF;

    // UiFrame::internals[0..2] (kOptoTube1B.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const auto& s = engine.bal_;
        if (!Ballistics::onB(engine.bc_, s))
        {
            out[0] = louder(stage::SmoothBranching::grDb(s.a));                   // FAST ENV (DB)
            return;
        }
        const simd::f32x4 rf = FixMan::fastDb(s.b), rs = FixMan::slowDb(s.b);
        const bool first = simd::lane<0>(simd::max(rf, rs)) >= simd::lane<1>(simd::max(rf, rs));
        const float f = first ? simd::lane<0>(rf) : simd::lane<1>(rf);
        const float sl = first ? simd::lane<0>(rs) : simd::lane<1>(rs);
        out[0] = f;                                                                // FAST ENV (DB)
        out[1] = sl;                                                               // SLOW ENV (DB)
        out[2] = sl > f ? 1.0f : 0.0f;                                             // MAN HOLD
    }

private:
    static float louder(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const float a = simd::lane<0>(v), b = simd::lane<1>(v);
        return a > b ? a : b;
    }
};

} // namespace fcdsp::modes
