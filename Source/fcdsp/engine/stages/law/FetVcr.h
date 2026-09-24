#pragma once

// law::FetVcr: the FET 76 gain element as a voltage-controlled resistor (01 §5.2 law/ catalogue: "everything is
// expressed as r in dB"; 01 §10.5 traits "QuadKnee + law::FetVcr [H]"; D §2.1 "the FET works as a voltage-controlled
// resistor, the lower leg of a divider across the signal"; E §2.6-2.7). M2 (S9).
//
// The element is a divider: a series resistor R_s into the FET's channel to ground, so the signal gain and the applied
// gain reduction r (dB) are
//
//     g = R_ds / (R_s + R_ds),     r = 20 log10(1 + R_s / R_ds)
//
// and, in the triode region with the 1176's half-drain gate feedback (which cancels the channel's first-order Vds
// dependence), the channel conductance is proportional to the gate overdrive above pinch-off, so a control voltage
// v in [0, kCvFullV] maps linearly onto conductance 0 ... 1 / R_on:
//
//     1 / R_ds = (v / kCvFullV) / R_on     ->     r(v) = 20 log10(1 + (R_s / R_on) (v / kCvFullV))
//
// The loop computes r (QuadKnee's FB solve, in dB: the same loop the hardware closes through this element, E §2.6);
// this law turns r back into the element's quantities, never the other way round, so it adds no second curve:
//
//   shunt(r)            1 - g = R_s / (R_s + R_ds): the share of the signal the FET carries. 0 with no GR (the FET
//                       pinched off: the audio still passes the amplifiers, FetColour.h), -> 1 as GR grows. FetColour
//                       weights the FET's own distortion residual by it (a divider's output distortion from a channel
//                       nonlinearity scales with the channel's share of the divider, docs/modes/fet-76.md).
//   resistanceKohm(r)   R_ds = R_s / (10^(r/20) - 1), clamped to [R_on, kOpenKohm] (kOpenKohm: pinched off; the FET R
//                       readout's full scale)
//   cvVolts(r)          v = kCvFullV (R_on / R_s)(10^(r/20) - 1), clamped to [0, kCvFullV] (the LOOP CV readout)
//   grFromCv(v)         the forward law r(v), exact inverse of cvVolts below the element's depth (dsp.fetcolour)
//   maxGrDb()           r at v = kCvFullV: 20 log10(1 + R_s / R_on) = 40.09 dB, the element's depth. The engine does
//                       not clamp its GR there (FET 76's range is n/a); beyond it the readouts saturate (R_on,
//                       kCvFullV)
//
// Constants [H] (docs/modes/fet-76.md): R_s = 47 kOhm and R_on = 470 Ohm, a 100:1 divider, i.e. a 40 dB element (a
// small-signal JFET's on-resistance of a few hundred ohms against the tens of kilohms of the 1176's attenuator side of
// the divider); kCvFullV = 10 V, the loop CV that turns the channel fully on (the LOOP CV readout's full scale).
// All functions use fcdsp::exp2/log2 (FastMath, no libm, K2 #14); NaN in gives NaN out.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"

namespace fcdsp::law {

struct FetVcr {
    static constexpr float kSeriesKohm = 47.0f;     // [H] R_s, the divider's series leg
    static constexpr float kOnKohm = 0.47f;         // [H] R_on, the channel fully on
    static constexpr float kOpenKohm = 100.0f;      // the channel pinched off, as far as the FET R readout goes
    static constexpr float kCvFullV = 10.0f;        // [H] the loop CV at R_on

    // 10^(r/20) - 1 = R_s / R_ds, from the GR in dB (r <= 0 gives 0: the channel open).
    static simd::f32x4 divRatio(simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = simd::max(simd::set1(0.0f), grDb);                   // NaN in r: max keeps the 2nd
        return simd::sub(fcdsp::exp2(simd::mul(r, simd::set1(kLog2PerDb))), simd::set1(1.0f));
    }

    // 1 - g: the FET's share of the divider, per lane, in [0, 1).
    static simd::f32x4 shunt(simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 r = simd::max(simd::set1(0.0f), grDb);
        return simd::sub(simd::set1(1.0f), fcdsp::exp2(simd::mul(r, simd::set1(-kLog2PerDb))));
    }
    static float shunt(float grDb) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(shunt(simd::set1(grDb))); }

    // R_ds in kOhm for the applied GR, clamped to [R_on, kOpenKohm].
    static float resistanceKohm(float grDb) noexcept FCDSP_NONBLOCKING
    {
        const float d = simd::lane<0>(divRatio(simd::set1(grDb)));
        // R_s / d > kOpen  <=>  d < R_s / kOpen (also d = 0: the channel open)
        if (!(d >= kSeriesKohm / kOpenKohm))
            return d != d ? d : kOpenKohm;                                          // NaN stays NaN
        const float rds = kSeriesKohm / d;
        return rds > kOnKohm ? rds : kOnKohm;
    }

    // The gate CV in volts for the applied GR, clamped to [0, kCvFullV].
    static float cvVolts(float grDb) noexcept FCDSP_NONBLOCKING
    {
        const float v = kCvFullV * (kOnKohm / kSeriesKohm) * simd::lane<0>(divRatio(simd::set1(grDb)));
        return v < kCvFullV ? v : (v != v ? v : kCvFullV);
    }

    // The forward law: GR in dB for a gate CV in volts (clamped to [0, kCvFullV]).
    static float grFromCv(float volts) noexcept FCDSP_NONBLOCKING
    {
        const float v = volts > 0.0f ? (volts < kCvFullV ? volts : kCvFullV) : (volts != volts ? volts : 0.0f);
        return kDbPerLog2 * fcdsp::log2(1.0f + (kSeriesKohm / kOnKohm) * (v / kCvFullV));
    }

    // The element's depth, dB (the GR at kCvFullV).
    static float maxGrDb() noexcept FCDSP_NONBLOCKING { return grFromCv(kCvFullV); }
};

} // namespace fcdsp::law
