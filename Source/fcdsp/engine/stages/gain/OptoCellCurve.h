#pragma once

// stage::OptoCellCurve: the T4 cell's static curve, the steady state of law::LdrShunt (01 §5.2 gain/ catalogue, §10.6
// Opto 2A; E §2.7; D §2.2). Opto 2A's Computer is FeedbackDelayed<OptoCellCurve>: this policy is the curve, the adaptor
// runs the loop (the one-sample-delay sense with its runtime guard, FeedbackDelayed.h).
//
// law::LdrShunt (E §2.7). The T4's photoresistor shunts the signal to ground behind a series resistor, so the
// attenuator's gain is 1 / (1 + g), g = R_series * G_ldr the normalised conductance, and its GR is the hyperbolic law
//
//     r = 20 log10(1 + g) = kDbPerLog2 * log2(1 + g)
//
// Under a steady light the conductance settles on g_inf = drive^S, drive = 10^((x - T) / 20) the electroluminescent
// panel's drive relative to its threshold: E's light law max(0, e - e0)^gamma with e0 = 0, and one exponent for the
// panel and the photoresistor together [H] (gamma_EL * gamma_LDR). So the curve is
//
//     r^(x) = kDbPerLog2 * log2(1 + 2^a),    a = S (x - T) kLog2PerDb        (FastMath's exp2 and log2, no libm)
//
// with T = LevelCtl::thrDb and S = LevelCtl::slope taken per sample (Stage.h's two-rate rule; design() bakes nothing):
// a softplus in dB, r^ = 0 far below T, 6.02 dB at T and S (x - T) + O(2^-a) above it, whose slope S g / (1 + g) never
// exceeds S (the guard of FeedbackDelayed.h relies on that). Feedback uses QuadKnee.h's one convention: FeedbackZdf and
// FeedbackDelayed evaluate target() with the loop gain k = S / (1 - S) in place of S, so the light law of the SENSED
// output has the exponent k and R_eff = 1 + k: COMP (S = 2/3) runs k = 2, 3:1, and LIMIT (S = 0.9) k = 9, 10:1 (D §2.2:
// "roughly 3:1 ... closer to 10:1"). That exponent is the EL-panel drive law change of the LIMIT switch (01 §10.6). The
// knee is the law's own, very soft for COMP (the static FB curve starts about 20 dB below T, D §2.2 "the knee is very
// soft") and firmer for LIMIT; LevelCtl::kneeDb is not read (Opto 2A's KNEE is n/a: "THE KNEE IS THE T4 CELL'S").
// S must be > 0: at S = 0 the law has no exponent (a constant 6 dB); the Mode's ratios are 3:1 and 10:1.
//
// Accuracy: r^ is within 1.1e-7 log2 units x kDbPerLog2 (7e-7 dB) of the law plus exp2's 1.7e-7 relative (FastMath.h),
// non-decreasing in x at every float (log2 and 1 + g are monotone; exp2's half-integer seams move by 1.7e-7 relative),
// and 0 exactly below about T - 144 / S dB (1 + g rounds to 1). NaN in x gives NaN (the poison check sees it, 01 §5.8).
//
// solveFb is FeedbackZdf<OptoCellCurve>'s Newton solve (the law has no closed-form feedback root), so the curve is a
// GainComputerPolicy on its own, and the adaptor's ZDF fallback and staticGr's static FB curve (FbAffine{0, 1}) are the
// same function. slope() gives Newton the exact derivative of the law.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/combinators/FeedbackZdf.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

namespace law {

// The T4's photoresistor as a shunt attenuator (E §2.7); g = R_series * G_ldr >= 0, the normalised conductance.
struct LdrShunt {
    static constexpr float kSeriesKohm = 10.0f;     // [H] R_series: the LDR reads R_series / g (the LDR internal)
    static constexpr float kDarkKohm = 1000.0f;     // the LDR internal's top: "above 1 MOhm" in the dark (E §2.7)

    // r = 20 log10(1 + g), dB.
    static simd::f32x4 grDb(simd::f32x4 g) noexcept FCDSP_NONBLOCKING
    {
        return simd::mul(simd::set1(kDbPerLog2), fcdsp::log2(simd::add(simd::set1(1.0f), g)));
    }

    // The inverse: g = 10^(r / 20) - 1 (0 for r <= 0).
    static simd::f32x4 conductance(simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 g = simd::sub(fcdsp::linFromDb(grDb), simd::set1(1.0f));
        return simd::max(simd::set1(0.0f), g);
    }

    // The fraction of the signal the shunt removes, 1 - 1 / (1 + g) = 1 - 10^(-r / 20): 0 (dark) ... 1.
    static simd::f32x4 attenuation(simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        return simd::sub(simd::set1(1.0f), fcdsp::linFromDb(simd::neg(simd::max(simd::set1(0.0f), grDb))));
    }

    // The photoresistor in kOhm at a GR, R_series / g, capped at kDarkKohm.
    static float ldrKohm(float grDb) noexcept FCDSP_NONBLOCKING
    {
        const float g = simd::lane<0>(conductance(simd::set1(grDb)));
        return g * kDarkKohm > kSeriesKohm ? kSeriesKohm / g : kDarkKohm;
    }
};

} // namespace law

struct OptoCellCurve {
    struct Coeffs {};

    static void design(Coeffs&, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING {}

    // The cell's GR (dB, >= 0) under the steady light of level x (dB): LdrShunt at g = 2^(S (x - T) kLog2PerDb).
    static simd::f32x4 target(const Coeffs&, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return law::LdrShunt::grDb(fcdsp::exp2(lightLog2(x, l)));
    }

    // d r^ / dx = S g / (1 + g), in [0, S]; FeedbackZdf's Newton uses it.
    static simd::f32x4 slope(const Coeffs&, simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 g = fcdsp::exp2(lightLog2(x, l));
        return simd::mul(l.slope, simd::div(g, simd::add(simd::set1(1.0f), g)));
    }

    // FB: THE root of r = A + B r^_fb(x - r) (Stage.h), by FeedbackZdf's safeguarded Newton (6 fixed steps).
    static simd::f32x4 solveFb(const Coeffs& c, simd::f32x4 x, const LevelCtl& l, FbAffine a) noexcept FCDSP_NONBLOCKING
    {
        return FeedbackZdf<OptoCellCurve>::solveFb(FeedbackZdf<OptoCellCurve>::Coeffs{ c }, x, l, a);
    }

    // log2 of the light law, S (x - T) kLog2PerDb.
    static simd::f32x4 lightLog2(simd::f32x4 x, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
    {
        return simd::mul(simd::mul(l.slope, simd::sub(x, l.thrDb)), simd::set1(kLog2PerDb));
    }
};

static_assert(GainComputerPolicy<OptoCellCurve>);

} // namespace fcdsp::stage
