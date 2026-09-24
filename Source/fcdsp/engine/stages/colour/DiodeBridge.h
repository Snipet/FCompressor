#pragma once

// stage::DiodeBridge: Diode 609's colour stage, the diode-bridge gain element and its transformer-coupled amplifiers
// (01 §5.2 colour/ catalogue; 01 §10.7 Diode 609 "voice: locked DIODE"; D §2.5 [V S10, S11]; E §2.9). M5 (S10).
//
// The stage runs on the wet signal AFTER the gain element (AudioIo::wet = x * 10^((preGain - GR) / 20)), with the
// applied GR per OS sample. It adds a memoryless residual built from the two bounded basis shapes FetColour.h uses, of
// one input u = kIn * x:
//
//     y = x + (E * tanh^2(u) + O * (tanh(u) - u)) / kIn
//
//     tanh^2(u)      even: the second harmonic (and a level-following DC term); an even function has no fundamental
//                    component, so it never changes a sine's gain
//     tanh(u) - u    odd: the third harmonic and a soft compression of the fundamental
//
// with per-sample weights from two sources, each given as x-domain Taylor coefficients (a2 of x^2, a3 of -x^3; the
// shaper weights are e = a2 / kIn and o = 3 a3 / kIn^2):
//
//     E = s(GR) e_B + e_T,     O = s(GR) o_B + o_T,     s(GR) = 1 - 10^(-GR/20)
//
//   BRIDGE (B)       the diode bridge is a shunt element: the signal is attenuated by the diodes' dynamic resistance in
//                    a divider, so their nonlinearity is weighted by the bridge's share of the divider, s(GR): 0 with
//                    no gain reduction (the bridge carries no control current), -> 1 as the GR grows. The bridge is
//                    balanced, but not perfectly: mostly second harmonic from the imbalance, some third.
//   TRANSFORMERS (T) the input/output transformers and the class-A line amplifier: static (independent of GR), the
//                    unit's bypass distortion.
// Constants [H] (docs/modes/diode-609.md): D §2.5 [V S10] gives THD 0.075 % in bypass and under 0.45 % with the
// limiter in (1 kHz, 800 ms recovery); the transformers' figure is met near the unit's top operating level (+20 dBu,
// -2 dBFS: 0.066 %), the bridge's at 10 dB of GR at +14 dBu (0.30 %). The budgets they respect: dsp.static's and
// dsp.time's meter-truth rows (tap against audio <= 0.10 dB for a character Mode): the odd part's describing-function
// loss at the highest wet level dsp.static reaches (+3.3 dBFS peak at 7.7 dB of GR: 0.023 dB), and the per-sample
// gain deviation on dsp.time's 40 dB square steps (0.073 dB), set by the odd part's ADAA-1 half-sample carry at the
// first sample after the drop (0.119 dB with a bridge odd coefficient of 0.004; hence 0.002).
//
// So the stage's distortion follows the gain reduction, colourStatic = false (the COLOUR view draws transfer() at the
// operating point's GR), and GR OFF (the probes' kEngGrOff) leaves only the transformers. The level-following DC of the
// even residual is part of the shape and is not blocked (FetColour.h: a blocker's tail after a level drop reads as a GR
// error on the per-sample meter-truth rows); the transformers' LF saturation is not modelled (a memoryless stage).
//
// Anti-aliasing and arithmetic are FetColour.h's: the odd basis through ADAA-1 (its segment mean (B(u1) - B(u0)) /
// (u1 - u0), B = logCosh u - u^2/2, the midpoint value where |u1 - u0| < kEps), the even basis pointwise (ADAA-1's
// half-sample carry of an even residual reads as a GR error after a level drop; a pointwise square law aliases only the
// second harmonic of content above fs / 4 at ECO, nothing at STD or HQ). Only the residual is processed (the linear
// part x is not delayed). Four samples per vector op; the scalar tail is lane 0 of the same arithmetic, so the output
// does not depend on how a block is split. The ADAA carry is the previous INPUT sample. An input sample that is exactly
// 0 gives exactly 0 (digital silence stays silent); NaN in gives NaN out (01 §5.8). No libm (FastMath, K2 #14).
//
// transfer() is the static shape the process runs for a constant input at a given GR; harmonicsEstimate() its third-
// order (Taylor) H2 and H3 of a sine of amplitude A: a2 = E kIn, a3 = O kIn^2 / 3; H1 = A - (3/4) a3 A^3,
// H2 = a2 A^2 / 2, H3 = a3 A^3 / 4. dsp.sharedelement holds both to the running stage and a DFT.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct DiodeBridge {
    // [H] x-domain Taylor coefficients (x^2 and -x^3) of the bridge at full share and of the transformers/amplifier.
    static constexpr float kBridgeEven = 0.020f;
    static constexpr float kBridgeOdd = 0.002f;
    static constexpr float kTransformerEven = 0.0018f;
    static constexpr float kTransformerOdd = 0.0008f;
    static constexpr float kIn = 0.5f;          // the basis shapes' input scale (u = kIn x)
    static constexpr float kEps = 1.0f / 64;    // ADAA: midpoint below this |u1 - u0| (Adaa.h's Tanh value)

    struct Coeffs {
        float eB = 0, oB = 0;                   // bridge shaper weights (u domain) at full share
        float eT = 0, oT = 0;                   // transformer shaper weights
    };
    struct State {
        float xPrev = 0;                        // the previous input sample (the ADAA carry)
    };

    static void design(Coeffs& c, const EngineParams&, const StageCtx&) noexcept FCDSP_NONBLOCKING
    {
        constexpr float kE = 1.0f / kIn, kO = 3.0f / (kIn * kIn);     // x-domain coefficient -> shaper weight
        c.eB = kBridgeEven * kE;
        c.oB = kBridgeOdd * kO;
        c.eT = kTransformerEven * kE;
        c.oT = kTransformerOdd * kO;
    }

    // The bridge's share of the divider at `grDb` of gain reduction: 1 - 10^(-GR/20), 0 ... 1 (GR <= 0: 0).
    static simd::f32x4 share(simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 g = fcdsp::linFromDb(simd::neg(simd::max(zero, grDb)));    // NaN GR: max keeps 0 or NaN
        return simd::max(zero, simd::sub(simd::set1(1.0f), g));
    }
    static float share(float grDb) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(share(simd::set1(grDb))); }

    static void process(const Coeffs& c, State& s, float* x, const float* grDb, int n, int) noexcept FCDSP_NONBLOCKING
    {
        if (n <= 0)
            return;
        alignas(16) float us[5], bo[5];                  // [0] = the previous sample, [1..4] = this group
        us[0] = kIn * s.xPrev;
        bo[0] = simd::lane<0>(basisOf(simd::set1(us[0])).odd);
        int i = 0;
        for (; i + 4 <= n; i += 4)
        {
            const simd::f32x4 xv = simd::load(x + i);
            const simd::f32x4 u = simd::mul(simd::set1(kIn), xv);
            const Basis b = basisOf(u);
            simd::store(us + 1, u);
            simd::store(bo + 1, b.odd);
            simd::store(x + i, step(c, xv, u, simd::load(us), b, simd::load(bo), share(simd::load(grDb + i))));
            us[0] = us[4];
            bo[0] = bo[4];
        }
        for (; i < n; ++i)                                              // the tail: lane 0 of the same step
        {
            const simd::f32x4 xv = simd::set1(x[i]);
            const simd::f32x4 u = simd::mul(simd::set1(kIn), xv);
            const Basis b = basisOf(u);
            const simd::f32x4 y = step(c, xv, u, simd::set1(us[0]), b, simd::set1(bo[0]),
                                       share(simd::set1(grDb[i])));
            us[0] = simd::lane<0>(u);
            bo[0] = simd::lane<0>(b.odd);
            x[i] = simd::lane<0>(y);
        }
        s.xPrev = us[0] / kIn;
    }

    // The static shape at a GR (the COLOUR view; header comment).
    static float transfer(const Coeffs& c, float x, float grDb) noexcept FCDSP_NONBLOCKING
    {
        const Weights wt = weights(c, share(grDb));
        const float u = kIn * x;
        const float t = fcdsp::tanh(u);
        return x + (wt.e * (t * t) + wt.o * (t - u)) * (1.0f / kIn);
    }

    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }

    // Third-order estimate of H2 and H3 relative to H1 (dB, floored at kHarmFloorDb) of a sine of amplitude `amp`
    // through transfer() at `grDb` (header comment).
    static constexpr float kHarmFloorDb = -100.0f;
    struct Harmonics { float h2Db, h3Db; };
    static Harmonics harmonicsEstimate(const Coeffs& c, float amp, float grDb) noexcept FCDSP_NONBLOCKING
    {
        const Weights wt = weights(c, share(grDb));
        const float a2 = wt.e * kIn, a3 = wt.o * (kIn * kIn / 3.0f);
        const float A = amp > 0.0f ? amp : 0.0f;
        const float h1 = A - 0.75f * a3 * A * A * A;
        return { ratioDb(0.5f * a2 * A * A, h1), ratioDb(0.25f * a3 * A * A * A, h1) };
    }

private:
    struct Basis { simd::f32x4 t, odd; };                             // tanh u, B_odd(u)
    struct Weights { float e, o; };

    // tanh u (the even basis is tanh^2 u, pointwise) and the odd basis' antiderivative B_odd = logCosh u - u^2/2.
    static Basis basisOf(simd::f32x4 u) noexcept FCDSP_NONBLOCKING
    {
        return { fcdsp::tanh(u), simd::fms(fcdsp::logCosh(u), simd::set1(0.5f), simd::mul(u, u)) };
    }

    static Weights weights(const Coeffs& c, float w) noexcept FCDSP_NONBLOCKING
    {
        return { w * c.eB + c.eT, w * c.oB + c.oT };
    }

    // One sample per lane: x, u = kIn x and its basis; the previous u and odd antiderivative; the bridge share w.
    static simd::f32x4 step(const Coeffs& c, simd::f32x4 x, simd::f32x4 u, simd::f32x4 u0, const Basis& b,
                            simd::f32x4 bo0, simd::f32x4 w) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 d = simd::sub(u, u0);
        const simd::f32x4 mid = simd::fma(u0, simd::set1(0.5f), d);             // u0 + d/2
        const simd::m32x4 wide = simd::ge(simd::abs(d), simd::set1(kEps));
        const simd::f32x4 qe = simd::mul(b.t, b.t);                             // even: pointwise
        const simd::f32x4 qo = simd::sel(wide, simd::div(simd::sub(b.odd, bo0), d),
                                         simd::sub(fcdsp::tanh(mid), mid));     // odd: ADAA-1
        const simd::f32x4 e = simd::fma(simd::set1(c.eT), w, simd::set1(c.eB));   // E = e_T + s e_B
        const simd::f32x4 o = simd::fma(simd::set1(c.oT), w, simd::set1(c.oB));   // O = o_T + s o_B
        const simd::f32x4 r = simd::mul(simd::fma(simd::mul(e, qe), o, qo), simd::set1(1.0f / kIn));
        const simd::f32x4 y = simd::add(x, r);
        const simd::m32x4 silent = simd::band(simd::ge(x, zero), simd::ge(zero, x));   // x == 0 (NaN: false)
        return simd::sel(silent, zero, y);
    }

    static float ratioDb(float h, float h1) noexcept FCDSP_NONBLOCKING
    {
        constexpr float kFloorRatio = 1e-5f;                                   // -100 dB
        if (!(h1 > 0.0f))
            return kHarmFloorDb;
        const float q = h / h1;
        return q > kFloorRatio ? kDbPerLog2 * fcdsp::log2(q) : kHarmFloorDb;
    }
};

static_assert(ColourPolicy<DiodeBridge>);

} // namespace fcdsp::stage
