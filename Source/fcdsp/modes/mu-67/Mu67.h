#pragma once

// Mu 67 (slot 4, `mu-67`): the traits (01 §8.2, §10.7). The descriptor, physical() and specs are Mu67Desc.cpp's;
// Mu67.cpp instantiates the engine with FCDSP_DEFINE_MODE(Mu67). The Mode sheet, with every fitted [H] constant, its
// source and the measurements, is docs/modes/mu-67.md.
//
// Full traits (M4, S10; 01 §10.7, E §2.7, D §2.3 Fairchild 660/670), feedback only:
//
//     Detector   PeakLog                        the tube side-chain rectifier (DET locked TUBE): full-wave peak, the
//                                               ballistics hold the peaks in the GR domain (E §2.4 placement 3)
//     Computer   ProgressiveKnee                the progressive (remote-cutoff) law in the loop: the FB curve's loop
//                                               gain rises from 0 at AC THRESH toward k = S / (1 - S), so the closed
//                                               loop's ratio rises with level from 1:1 toward 1 / (1 - S); DC THRESH
//                                               sets the onset o_c and the asymptote S (the knee slot, 0-40 dB)
//     Link       LinkMax                        LINK (the AGC ties the two channels' control voltages), after the
//                                               per-lane solve (K2 #5b); LAT/VERT is stmode's M/S code (the Router)
//     Ballistics TcSelector                     TIME TC1-TC4: SmoothBranching; TC5 / TC6: MultiStage3, the programme-
//                                               dependent network (2 / 10 s; 0.3 / 10 / 25 s), max of roots, AutoSwitch
//                                               hand-over between them
//     Stage2     NoStage2
//     Colour     TubePushPull                   odd-dominant push-pull residual growing with GR, INPUT its drive
//     ScShape    Flat                           (the host's SC HPF, an extension, acts before it)
//
// The feedback kernel (K2 #1, #5): ProgressiveKnee's bracketed Newton (FeedbackZdf.h's safeguarded solve, stopping
// per lane at float resolution: ProgressiveKnee.h; 01 §10.7 names FeedbackZdf<ProgressiveKnee>, which stays a valid
// wrapper of the same law and is held to it by dsp.progressiveknee, but costs a fixed six steps per solve, and Mu 67
// runs two to five solves per sample) over the ballistics' affine maps (TcSelector's branches, the max of roots),
// linked after the per-lane solve; every DC THRESH is monotone (fb.monotone, and dsp.progressiveknee over the whole
// knob). An external key evaluates the computer feed-forward on the key with the loop's open-loop times (E §2.6: the
// loop is open).
//
// EngineParams::m slots, written by muPhysical (Mu67Desc.cpp):
//   m[0], m[1]   the onset law o_c = m[0] + m[1] * kneeDb (ProgressiveKneeT<0, 1>)
//   m[2] ... m[6] MultiStage3's stage times, ms: tau_R1, tau_C2, tau_R2, tau_C3, tau_R3 (TC5: tau_C3 = tau_R3 = 0)
//   EngineParams::slope is the curve's asymptotic S (not the RATIO slot's nominal value, which is the local ratio at
//   T + 10 dB), and atkTauMs the open-loop attack (ADR-63).
//
// Internals (kMu67.internals, 01 §10.7), from the louder of lanes 0-1:
//   BIAS (V)       the grid bias the side chain applies for the applied GR: GR / kBiasDbPerVolt (the history lane)
//   EFF RATIO      the live local ratio of the static FB curve at the operating point that produces the applied GR:
//                  1 + k (1 - e^(-o_y / o_c)), o_y the output overshoot where k o_c H(o_y / o_c) = GR
//   TC WEIGHT      the share of the GR the programme network holds above the peak stage on TC5 / TC6, (GR - r1) / GR
//                  with GR = max(r1, r2, r3) (0 while stage 1 holds it after a peak, 1 once a sustained programme has
//                  charged the slow stages and stage 1 has let go), times TcSelector's blend weight; 0 on TC1-TC4 and
//                  without GR

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/stages/ballistics/MultiStage3.h"
#include "fcdsp/engine/stages/ballistics/TcSelector.h"
#include "fcdsp/engine/stages/colour/TubePushPull.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/ProgressiveKnee.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp::modes {

extern const ModeDescriptor kMu67;              // Mu67Desc.cpp

struct Mu67 {
    static constexpr const ModeDescriptor& desc = kMu67;
    using Detector   = stage::PeakLog;
    using Computer   = stage::ProgressiveKnee;
    using Link       = stage::LinkMax;
    using Ballistics = stage::TcSelector;
    using Stage2     = stage::NoStage2;
    using Colour     = stage::TubePushPull;
    using ScShape    = stage::Flat;
    static constexpr uint8_t kTopologies = 1u << kTopoFB;

    // EngineParams::m slots (header comment).
    static constexpr int kOnsetSlot = 0, kOnsetPerDbSlot = 1;
    static constexpr int kRel1Slot = 2, kCharge2Slot = 3, kRel2Slot = 4, kCharge3Slot = 5, kRel3Slot = 6;
    static_assert(std::is_same_v<stage::ProgressiveKnee, stage::ProgressiveKneeT<kOnsetSlot, kOnsetPerDbSlot>>);
    static_assert(std::is_same_v<stage::MultiStage3,
                                 stage::MultiStage3T<kRel1Slot, kCharge2Slot, kRel2Slot, kCharge3Slot, kRel3Slot>>);

    static constexpr float kBiasDbPerVolt = 2.0f;   // [H] BIAS readout: dB of GR per volt of grid bias
    static constexpr float kBiasMaxV = 20.0f;       // the BIAS readout's full scale (kMu67.internals)
    static constexpr float kEffRatioMax = 30.0f;    // the EFF RATIO readout's full scale
    static constexpr int kSolveSteps = 16;          // the scalar solves below: fixed counts, converged to float
    static constexpr float kStepOverDb = 20.0f;     // the D2 attack step: T - 20 -> T + 20 dB (C §5.3, dsp.time)
    static constexpr int kAttackPanels = 32;        // Simpson panels of the attack integral (even)

    // H(u) = u - 1 + e^-u (ProgressiveKnee::shape), scalar.
    static float curveShape(float u) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 uv = simd::set1(u > 0.0f ? u : 0.0f);
        const simd::f32x4 e = fcdsp::exp2(simd::mul(uv, simd::set1(-stage::ProgressiveKnee::kLog2E)));
        return simd::lane<0>(stage::ProgressiveKnee::shape(uv, e));
    }
    static float expNeg(float u) noexcept FCDSP_NONBLOCKING
    {
        return fcdsp::exp2(-(u > 0.0f ? u : 0.0f) * stage::ProgressiveKnee::kLog2E);
    }

    // The static FB curve at an input overshoot o_x = x - T >= 0 (dB), loop gain k, onset o_c: the output overshoot
    // o_y, the root of o_y + k o_c H(o_y / o_c) = o_x (increasing and convex, so Newton from o_x descends onto it).
    static float outputOverDb(float overInDb, float k, float oc) noexcept FCDSP_NONBLOCKING
    {
        if (!(overInDb > 0.0f))
            return 0.0f;
        float y = overInDb;
        for (int i = 0; i < kSolveSteps; ++i)
        {
            const float u = y / oc;
            const float g = y + k * oc * curveShape(u) - overInDb;
            const float next = y - g / (1.0f + k * (1.0f - expNeg(u)));
            y = next > 0.0f ? next : 0.5f * y;
        }
        return y;
    }

    // The static FB curve's local ratio dx / dy at an output overshoot o_y: 1 + k (1 - e^(-o_y / o_c)).
    static float localRatioAtOutput(float overOutDb, float k, float oc) noexcept FCDSP_NONBLOCKING
    {
        return 1.0f + k * (1.0f - expNeg(overOutDb / oc));
    }

    // The output overshoot where the static FB curve applies `grDb` (k o_c H(o_y / o_c) = GR; bisection, H monotone).
    static float outputOverAtGr(float grDb, float k, float oc) noexcept FCDSP_NONBLOCKING
    {
        if (!(grDb > 0.0f) || !(k > 0.0f))
            return 0.0f;
        float lo = 0.0f, hi = grDb / k + oc;                    // H(u) >= u - 1: o_y <= GR / k + o_c
        for (int i = 0; i < 2 * kSolveSteps; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (k * oc * curveShape(mid / oc) < grDb ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    }

    // ADR-63 for a progressive loop: tau_open / tau_published, so that the continuous-time loop's attack on the D2
    // step (the level from T - 20 to T + 20 dB; the 63 % point of the GR's dB change, TimeLaw::expDb) takes the
    // published time. On the attack branch dr/dt = (r^_fb(x - r) - r) / tau_open, so t_63 = tau_open * I with
    //     I = integral over [0, r_63] of dr / (k o_c H((o_x - r) / o_c) - r),   o_x = 20 dB, r_63 = (1 - 1/e) r_inf,
    // r_inf the static FB GR at o_x; the factor is 1 / I (a linear loop gives 1 + k, FET 76's conversion). Simpson's
    // rule over kAttackPanels panels (the integrand is smooth and bounded on [0, r_63]).
    static float attackOpenLoopFactor(float k, float oc) noexcept FCDSP_NONBLOCKING
    {
        const float rInf = kStepOverDb - outputOverDb(kStepOverDb, k, oc);
        if (!(rInf > 0.0f))
            return 1.0f;
        const float r63 = 0.632120559f * rInf;
        const float h = r63 / static_cast<float>(kAttackPanels);
        const auto inv = [&](float r) noexcept {
            return 1.0f / (k * oc * curveShape((kStepOverDb - r) / oc) - r);
        };
        float sum = inv(0.0f) + inv(r63);
        for (int i = 1; i < kAttackPanels; ++i)
            sum += (i % 2 == 1 ? 4.0f : 2.0f) * inv(h * static_cast<float>(i));
        const float integral = sum * h / 3.0f;
        return integral > 0.0f ? 1.0f / integral : 1.0f;
    }

    // o_c and k from the engine's parameters (the targets ModeEngine's smoothers land on).
    static float onsetOf(const EngineParams& p) noexcept FCDSP_NONBLOCKING
    {
        const float oc = p.m[kOnsetSlot] + p.m[kOnsetPerDbSlot] * p.kneeDb;
        return oc > stage::ProgressiveKnee::kMinOnsetDb ? oc : stage::ProgressiveKnee::kMinOnsetDb;
    }

    // UiFrame::internals[0..2] (kMu67.internals); ModeEngine zeroes `out` first.
    static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = Ballistics::grDb(engine.bal_);
        const bool second = simd::lane<1>(r) > simd::lane<0>(r);
        const float gr = second ? simd::lane<1>(r) : simd::lane<0>(r);
        const float k = stage::QuadKnee::loopGain(engine.p_.slope);
        const float oc = onsetOf(engine.p_);
        const float bias = gr > 0.0f ? gr / kBiasDbPerVolt : 0.0f;
        out[0] = bias < kBiasMaxV ? bias : kBiasMaxV;                                  // BIAS (V)
        const float eff = localRatioAtOutput(outputOverAtGr(gr, k, oc), k, oc);
        out[1] = eff < kEffRatioMax ? eff : kEffRatioMax;                              // EFF RATIO
        // TC WEIGHT: the programme network's share while it runs (TcSelector = AutoSwitch<SmoothBranching,
        // MultiStage3>: its B path and blend weight)
        const auto& sw = engine.bal_;
        const float wB = sw.wB < 0.0f ? (engine.bc_.useB ? 1.0f : 0.0f) : sw.wB;
        const simd::f32x4 fast = stage::MultiStage3::stage1Db(sw.b), all = stage::MultiStage3::grDb(sw.b);
        const float f = second ? simd::lane<1>(fast) : simd::lane<0>(fast);
        const float a = second ? simd::lane<1>(all) : simd::lane<0>(all);
        const float share = a > kWeightFloorDb ? (f < a ? (a - f) / a : 0.0f) : 0.0f;
        out[2] = wB * share;                                                           // TC WEIGHT
    }

private:
    static constexpr float kWeightFloorDb = 1e-3f;  // below this GR the TC WEIGHT reads 0
};

} // namespace fcdsp::modes
