#pragma once

// stage::FetColour: FET 76's colour stage, the 1176's gain element and amplifiers (01 §5.2 colour/ catalogue; 01 §10.5
// "ColourSelect<FetColour> (voice = revision constants)"; D §2.1 revisions and coloration; E §2.7, §2.9). M2 (S9).
//
// The stage runs on the wet signal AFTER the gain element (AudioIo::wet = x * 10^((preGain - GR) / 20)): the voltage
// across the FET's channel (law/FetVcr.h: the divider's output), with the applied GR per OS sample. It adds a
// memoryless residual built from two bounded basis shapes of one input u = kIn * x:
//
//     y = x + (E * tanh^2(u) + O * (tanh(u) - u)) / kIn
//
//     tanh^2(u)      even: u^2 - (2/3) u^4 + ...: the second harmonic (and a DC term that follows the level), bounded
//                    by 1, so it never changes a sine's fundamental (an even function has no fundamental component)
//     tanh(u) - u    odd: -u^3 / 3 + (2/15) u^5 - ...: the third harmonic and a soft compression of the fundamental,
//                    growing at most linearly
//
// with per-sample weights from three sources, each given as x-domain Taylor coefficients (a2 of x^2, a3 of -x^3; the
// shaper weights are e = a2 / kIn and o = 3 a3 / kIn^2):
//   FET      the channel's residual nonlinearity (the half-drain gate feedback cancels the square law's first-order
//            term; what remains is mostly second harmonic, plus a smaller odd part), weighted by s(GR) =
//            law::FetVcr::shunt, the FET's share of the divider: 0 with no gain reduction (the channel pinched off;
//            GR OFF, the attack knob's OFF detent, is this case: "no gain reduction, the audio still passes the
//            transformers", D §2.1), -> 1 as the GR grows. So the stage's distortion follows the gain reduction, as
//            the 1176's does
//            ("distortion ... with limiting", D §2.1), and colourStatic = false (01 §10.5): the COLOUR view draws
//            transfer() at the operating point's GR.
//   FET ALL  the all-buttons ratio (kTagAll in EngineParams::tags): "the bias points shift across the circuit" and
//            "distortion increases radically" (D §2.1): the FET's even coefficient x kAllEven, its odd one x kAllOdd.
//            The ALL amount a is a 0 ... 1 one-pole smoothed per control tick (20 ms, landing exactly) and interpolated
//            linearly across each process() call from the previous call's value, so the ratio's ALL detent edge never
//            steps the colour (K2 #4 iv, dsp.zipper): FET weight = (1 - a) FET + a FET ALL.
//   OUTPUT   the line amplifier and output transformer: static (independent of GR). Class-A revisions carry a second
//            harmonic; the push-pull Rev F/H output is mostly odd (D §2.1 "push-pull output stage instead of Class A").
//
//     E = s(GR) ((1 - a) e_F + a e_FA) + e_O,     O = s(GR) ((1 - a) o_F + a o_FA) + o_O
//
// VOICE picks the revision's constants (kRevisions, index = EngineParams::voice, a kernel key: a change crossfades two
// engines, 01 §5.5), [H] (docs/modes/fet-76.md, with the meter-truth and harmonic-analysis budgets each respects):
//   LN   REV D/E LN (the default; the UA reissue is patterned on D/E): low-noise circuit, lower drain-source voltage,
//        so the FET's residual is small; class-A output and transformer
//   A    REV A (blue stripe): no LN, FET preamp, class-A output with the UA-5002 transformer: more distortion
//   F    REV F/H: LN, push-pull output
// The even residual's level-following DC is part of the shape and is not blocked (a DC blocker's tail after a level
// drop reads as a GR error on the per-sample meter-truth rows; the transformers' LF coupling is a known limit of the
// model, docs/modes/fet-76.md).
//
// Anti-aliasing (01 §5.6: at ECO the FET colour runs ADAA-1 at base rate): the ODD basis goes through ADAA-1 (Adaa.h's
// residual scheme, Parker et al.): its mean over the segment between two consecutive inputs, (B(u1) - B(u0)) / (u1 -
// u0) with B = logCosh u - u^2 / 2, the midpoint value where |u1 - u0| < kEps. The EVEN basis is evaluated pointwise:
// ADAA-1 delays a residual by half a sample, and an even residual's half-sample carry after a large level drop reads,
// on the per-sample meter-truth rows (dsp.time tap_vs_audio: a 40 dB step), as a GR error of a2 x_big^2 / (3 x_small)
// (0.6 dB at the LN's coefficient); a pointwise square law only aliases the second harmonic of content above fs / 4 at
// ECO, and nothing at STD or HQ, where the stage runs at 2x / 4x. Only the residual is processed (the linear part x is
// not delayed, so the stage combs with nothing at any mix); the per-sample weights move at control rate, which breaks
// the ADAA assumption only to second order (E §2.9). One tanh and one logCosh per sample plus the midpoint's tanh
// (FastMath, no libm, K2 #14), four samples per vector op; the scalar tail is lane 0 of the same arithmetic, so the
// output does not depend on how a block is split while the parameters are static (dsp.zipper bs rows). The ADAA carry
// is the previous INPUT sample. An input sample that is exactly 0 gives exactly 0 (digital silence stays silent,
// dsp.hostile; the dropped term is the odd residual's segment mean ending at 0). NaN in gives NaN out (01 §5.8).
//
// transfer() is the static shape the process runs for a constant input: x + (E tanh^2(kIn x) + O (tanh(kIn x) - kIn
// x)) / kIn at the given GR and the current ALL amount. harmonicsEstimate() is its third-order (Taylor) H2 and H3 of a
// sine of amplitude A: a2 = E kIn, a3 = O kIn^2 / 3; H1 = A - (3/4) a3 A^3, H2 = a2 A^2 / 2, H3 = a3 A^3 / 4. Fet76's
// H2 / H3 internals read it (UiFrame; the COLOUR view computes the exact harmonics with analysis::harmonicsDb);
// dsp.fetcolour holds it to a DFT.

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/law/FetVcr.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include <cstdint>

namespace fcdsp::stage {

struct FetColour {
    // A revision's constants [H]: x-domain Taylor coefficients (x^2 and -x^3) of the FET residual at full share and of
    // the output stage.
    struct Revision { float fetEven, fetOdd, outEven, outOdd; };
    static constexpr int kRevisionCount = 3;
    static constexpr Revision kRevisions[kRevisionCount] = {
        { 0.010f, 0.006f, 0.002f, 0.0009f },    // LN: REV D/E LN (voice 0)
        { 0.050f, 0.012f, 0.020f, 0.0040f },    // A:  REV A (voice 1)
        { 0.010f, 0.006f, 0.001f, 0.0030f },    // F:  REV F/H, push-pull output (voice 2)
    };
    static constexpr float kAllEven = 8.0f;     // [H] ALL: the FET's even coefficient x 8 ...
    static constexpr float kAllOdd = 3.0f;      // [H] ... and its odd one x 3 (the shifted bias points)
    static constexpr float kIn = 0.5f;          // the basis shapes' input scale (u = kIn x)
    static constexpr float kEps = 1.0f / 64;    // ADAA: midpoint below this |u1 - u0| (Adaa.h's Tanh value)
    static constexpr float kAllSmoothMs = 20.0f;
    static constexpr float kAllLand = 1e-4f;

    struct Coeffs {
        float eF = 0, oF = 0;                   // FET shaper weights (u domain), this revision
        float eFA = 0, oFA = 0;                 // FET ALL shaper weights
        float eO = 0, oO = 0;                   // output stage shaper weights
        float all = 0;                          // smoothed ALL amount, 0 ... 1
        float kTick = 0;                        // 1 - alpha of the 20 ms smoother at fs / kTickSamples
        int revision = 0;                       // index into kRevisions
        bool primed = false;                    // false: the next design lands on the targets
    };
    struct State {
        float xPrev = 0;                        // the previous input sample (the ADAA carry)
        float aPrev = -1;                       // the ALL amount at the end of the previous call (< 0: none yet)
    };

    static int revisionOf(uint8_t voice) noexcept FCDSP_NONBLOCKING
    {
        return voice < kRevisionCount ? static_cast<int>(voice) : kRevisionCount - 1;
    }

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float allTgt = (p.tags & kTagAll) != 0 ? 1.0f : 0.0f;
        if (!c.primed)
        {
            c.kTick = oneMinusAlpha(kAllSmoothMs, x.fs / static_cast<float>(kTickSamples));
            c.all = allTgt;
            c.primed = true;
        }
        else
        {
            const float d = allTgt - c.all;
            const float next = c.all + c.kTick * d;
            c.all = (d < kAllLand && d > -kAllLand) || next == c.all ? allTgt : next;
        }
        c.revision = revisionOf(p.voice);
        const Revision& r = kRevisions[c.revision];
        constexpr float kE = 1.0f / kIn, kO = 3.0f / (kIn * kIn);     // x-domain coefficient -> shaper weight
        c.eF = r.fetEven * kE;
        c.oF = r.fetOdd * kO;
        c.eFA = r.fetEven * kAllEven * kE;
        c.oFA = r.fetOdd * kAllOdd * kO;
        c.eO = r.outEven * kE;
        c.oO = r.outOdd * kO;
    }

    static void process(const Coeffs& c, State& s, float* x, const float* grDb, int n, int) noexcept FCDSP_NONBLOCKING
    {
        if (n <= 0)
            return;
        const float a0 = s.aPrev < 0.0f ? c.all : s.aPrev;
        const float da = (c.all - a0) / static_cast<float>(n);
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
            alignas(16) const float ai[4] = { a0 + da * static_cast<float>(i + 1),
                                              a0 + da * static_cast<float>(i + 2),
                                              a0 + da * static_cast<float>(i + 3),
                                              a0 + da * static_cast<float>(i + 4) };
            const simd::f32x4 y = step(c, xv, u, simd::load(us), b, simd::load(bo),
                                       law::FetVcr::shunt(simd::load(grDb + i)), simd::load(ai));
            simd::store(x + i, y);
            us[0] = us[4];
            bo[0] = bo[4];
        }
        for (; i < n; ++i)                                              // the tail: lane 0 of the same step
        {
            const simd::f32x4 xv = simd::set1(x[i]);
            const simd::f32x4 u = simd::mul(simd::set1(kIn), xv);
            const Basis b = basisOf(u);
            const float ai = a0 + da * static_cast<float>(i + 1);
            const simd::f32x4 y = step(c, xv, u, simd::set1(us[0]), b, simd::set1(bo[0]),
                                       law::FetVcr::shunt(simd::set1(grDb[i])), simd::set1(ai));
            us[0] = simd::lane<0>(u);
            bo[0] = simd::lane<0>(b.odd);
            x[i] = simd::lane<0>(y);
        }
        s.xPrev = us[0] / kIn;
        s.aPrev = c.all;
    }

    // The static shape at a GR and the current ALL amount (the COLOUR view; header comment).
    static float transfer(const Coeffs& c, float x, float grDb) noexcept FCDSP_NONBLOCKING
    {
        const float w = law::FetVcr::shunt(grDb);
        const Weights wt = weights(c, w, c.all);
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
        const Weights wt = weights(c, law::FetVcr::shunt(grDb), c.all);
        const float a2 = wt.e * kIn, a3 = wt.o * (kIn * kIn / 3.0f);
        const float A = amp > 0.0f ? amp : 0.0f;
        const float h1 = A - 0.75f * a3 * A * A * A;
        return { ratioDb(0.5f * a2 * A * A, h1), ratioDb(0.25f * a3 * A * A * A, h1) };
    }

private:
    struct Basis { simd::f32x4 t, odd; };                             // tanh u, B_odd(u)
    struct Weights { float e, o; };

    // tanh u (the even basis is tanh^2 u, evaluated pointwise) and the odd basis' antiderivative B_odd = logCosh u -
    // u^2/2 (of tanh u - u; 0 at 0).
    static Basis basisOf(simd::f32x4 u) noexcept FCDSP_NONBLOCKING
    {
        return { fcdsp::tanh(u), simd::fms(fcdsp::logCosh(u), simd::set1(0.5f), simd::mul(u, u)) };
    }

    static Weights weights(const Coeffs& c, float w, float a) noexcept FCDSP_NONBLOCKING
    {
        const float fe = c.eF + a * (c.eFA - c.eF), fo = c.oF + a * (c.oFA - c.oF);
        return { w * fe + c.eO, w * fo + c.oO };
    }

    // One sample per lane: x, u = kIn x and its basis; the previous u and odd antiderivative; the FET share w and the
    // ALL amount a. Returns y (header comment).
    static simd::f32x4 step(const Coeffs& c, simd::f32x4 x, simd::f32x4 u, simd::f32x4 u0, const Basis& b,
                            simd::f32x4 bo0, simd::f32x4 w, simd::f32x4 a) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 d = simd::sub(u, u0);
        const simd::f32x4 mid = simd::fma(u0, simd::set1(0.5f), d);             // u0 + d/2
        const simd::m32x4 wide = simd::ge(simd::abs(d), simd::set1(kEps));
        const simd::f32x4 qe = simd::mul(b.t, b.t);                             // even: pointwise
        const simd::f32x4 qo = simd::sel(wide, simd::div(simd::sub(b.odd, bo0), d),
                                         simd::sub(fcdsp::tanh(mid), mid));     // odd: ADAA-1
        // E and O per lane (weights(), vectorised)
        const simd::f32x4 fe = simd::fma(simd::set1(c.eF), a, simd::set1(c.eFA - c.eF));
        const simd::f32x4 fo = simd::fma(simd::set1(c.oF), a, simd::set1(c.oFA - c.oF));
        const simd::f32x4 e = simd::fma(simd::set1(c.eO), w, fe);
        const simd::f32x4 o = simd::fma(simd::set1(c.oO), w, fo);
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

static_assert(ColourPolicy<FetColour>);

} // namespace fcdsp::stage
