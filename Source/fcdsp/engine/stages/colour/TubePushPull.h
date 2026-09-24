#pragma once

// stage::TubePushPull: Mu 67's colour, the Fairchild's balanced push-pull variable-mu stage and its transformers (01
// §5.2 colour/ catalogue; 01 §10.7 Mu 67 "VarimuColour (odd-dominant, grows with GR)"; D §2.3 "Balanced push-pull
// triodes cancel even harmonics, so the remaining distortion is mostly odd and rises with GR [U]"; E §2.7, §2.9).
// M4 (S10). The stage runs on the wet signal AFTER the gain element (AudioIo::wet = x * 10^((preGain - GR) / 20)) with
// the applied GR per OS sample, and adds a memoryless odd residual of u = k x:
//
//     y = x + O(GR) * (tanh u - u) / k,     k = kIn * 10^(INPUT / 20)
//     O(GR) = kOddStatic + kOddGr * g / (g + kGrHalfDb),   g = max(0, GR)
//
// tanh u - u = -u^3 / 3 + ... is the third harmonic (and the higher odd ones) and a soft compression of the
// fundamental. The pair's odd term follows the gain reduction: the more the side chain biases the remote-cutoff triodes
// toward cutoff, the more curved the part of their characteristic the signal swings over; the balanced pair cancels the
// even orders, so what grows is odd (O rises from the transformers' kOddStatic with no GR to kOddStatic + kOddGr,
// half-way at kGrHalfDb of GR). The model keeps the even orders at zero (a perfectly balanced pair; the real unit's
// small imbalance is not modelled: an even residual's level-following DC and its second harmonic, even 50 dB under
// the third, put a ripple on a quadrature pair's envelope that moves dsp.time's audio-measured release by several
// samples between Qualities, M4). So colourStatic = false (01 §10.7): the COLOUR view draws transfer() at the
// operating point's GR.
//
// INPUT is the unit's 20 dB input attenuator (Mu 67's `drive`, -20 ... 0 dB, level-compensated as every drive): it
// scales u, so at -20 dB the pair's distortion is 40 dB lower (H3 ~ u^2). k is smoothed per control tick in dB
// (detail::VoiceDrive, 20 ms) and glides linearly across each process() call from where the last one ended
// (TubeTransformer.h: a step in k would step the residual's level at a detent edge of INPUT, dsp.zipper).
//
// Level [H] (docs/modes/mu-67.md), set by the probes' meter truth: the stage's gain must stay within the character
// rigor's tap_vs_audio budget (0.1 dB per sample) on the probes' squares. The binding case is dsp.time's 40 dB drop
// (T + 20 -> T - 20 dB, the GR settled at ~13 dB): ADAA-1's segment mean carries the loud sample's residual into the
// first quiet one, O u_loud^3 / (12 u_quiet) relative (TubeTransformer.h's case): 0.11 dB at kIn = 0.2, 0.06 dB at
// kIn = 0.15. The steady compression of the fundamental, O u^2 / 4, stays below 0.02 dB on dsp.static's staircase
// (to T + 26 dB). At 0 dBFS out, kIn = 0.15 gives H3 ~ -94 dB with no GR, -61 dB at 10 dB and -59 dB at 20 dB of GR
// (about 0.1 %).
//
// Anti-aliasing (01 §5.6: at ECO the colour runs ADAA-1 at base rate): the residual goes through ADAA-1 (Adaa.h's
// residual scheme): its mean over the segment between two consecutive u, (B(u1) - B(u0)) / (u1 - u0) with B = logCosh
// u - u^2 / 2 (the antiderivative of tanh u - u, 0 at 0), the midpoint value where |u1 - u0| < kEps. Only the residual
// is processed; the linear part x is not delayed, so the stage combs with nothing at any mix. The weight moves per
// sample with the GR, which breaks the ADAA assumption only to second order (E §2.9). One logCosh per sample plus the
// midpoint's tanh (FastMath: no libm, K2 #14), four samples per vector op, the scalar tail lane 0 of the same
// arithmetic. An input sample that is exactly 0 gives exactly 0 (digital silence stays silent). NaN in gives NaN out
// (01 §5.8).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct TubePushPull {
    static constexpr float kIn = 0.15f;             // [H] shaper input per unit of signal at INPUT 0 dB
    static constexpr float kOddStatic = 0.01f;      // [H] the transformers' odd term (no GR)
    static constexpr float kOddGr = 1.0f;           // [H] the pair's odd term at full bias
    static constexpr float kGrHalfDb = 12.0f;       // [H] the GR at which the pair's term is half of kOddGr
    static constexpr float kEps = 1.0f / 64;        // ADAA: the midpoint below this |u1 - u0| (Adaa.h's Tanh value)

    struct Coeffs { detail::VoiceDrive drive{}; };
    struct State {
        float uPrev = 0;                            // the previous u (the ADAA carry, in the shaper's domain)
        float k = 0;                                // the input scale at the end of the last call (0: none yet)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.drive.design(p, x, kIn);
    }

    // O(GR) per lane (header comment).
    static simd::f32x4 oddWeight(simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 g = simd::max(simd::set1(0.0f), grDb);
        const simd::f32x4 share = simd::div(g, simd::add(g, simd::set1(kGrHalfDb)));
        return simd::fma(simd::set1(kOddStatic), simd::set1(kOddGr), share);
    }
    static float oddWeight(float grDb) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(oddWeight(simd::set1(grDb))); }

    static void process(const Coeffs& c, State& s, float* x, const float* grDb, int n, int) noexcept FCDSP_NONBLOCKING
    {
        if (n <= 0)
            return;
        // k glides linearly across the call from where the last one ended (header comment); a static INPUT gives k.
        const float k0 = s.k > 0.0f ? s.k : c.drive.k, k1 = c.drive.k, dk = (k1 - k0) / static_cast<float>(n);
        alignas(16) float us[5], bo[5], kv[4];                  // [0] = the previous sample, [1..4] = this group
        us[0] = s.uPrev;
        bo[0] = simd::lane<0>(oddAntiderivative(simd::set1(us[0])));
        int i = 0;
        for (; i + 4 <= n; i += 4)
        {
            for (int j = 0; j < 4; ++j)
                kv[j] = i + j + 1 < n ? k0 + dk * static_cast<float>(i + j + 1) : k1;
            const simd::f32x4 xv = simd::load(x + i), k = simd::load(kv);
            const simd::f32x4 u = simd::mul(k, xv);
            const simd::f32x4 b = oddAntiderivative(u);
            simd::store(us + 1, u);
            simd::store(bo + 1, b);
            simd::store(x + i, step(xv, u, simd::load(us), b, simd::load(bo), k, simd::load(grDb + i)));
            us[0] = us[4];
            bo[0] = bo[4];
        }
        for (; i < n; ++i)                                      // the tail: lane 0 of the same step
        {
            const float ki = i + 1 < n ? k0 + dk * static_cast<float>(i + 1) : k1;
            const simd::f32x4 xv = simd::set1(x[i]), k = simd::set1(ki);
            const simd::f32x4 u = simd::mul(k, xv);
            const simd::f32x4 b = oddAntiderivative(u);
            x[i] = simd::lane<0>(step(xv, u, simd::set1(us[0]), b, simd::set1(bo[0]), k, simd::set1(grDb[i])));
            us[0] = simd::lane<0>(u);
            bo[0] = simd::lane<0>(b);
        }
        s.uPrev = us[0];
        s.k = k1;
    }

    // The static shape at a GR (the COLOUR view): x + O(GR) (tanh u - u) / k, u = k x.
    static float transfer(const Coeffs& c, float x, float grDb) noexcept FCDSP_NONBLOCKING
    {
        const float u = c.drive.k * x;
        return x + oddWeight(grDb) * (fcdsp::tanh(u) - u) * c.drive.invK;
    }

    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }

private:
    // B(u) = logCosh u - u^2 / 2, the antiderivative of tanh u - u (0 at 0).
    static simd::f32x4 oddAntiderivative(simd::f32x4 u) noexcept FCDSP_NONBLOCKING
    {
        return simd::fms(fcdsp::logCosh(u), simd::set1(0.5f), simd::mul(u, u));
    }

    // One sample per lane: x, u = k x and B(u); the previous u and B; the input scale k and the GR (header comment).
    static simd::f32x4 step(simd::f32x4 x, simd::f32x4 u, simd::f32x4 u0, simd::f32x4 b, simd::f32x4 b0, simd::f32x4 k,
                            simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 zero = simd::set1(0.0f);
        const simd::f32x4 d = simd::sub(u, u0);
        const simd::f32x4 mid = simd::fma(u0, simd::set1(0.5f), d);             // u0 + d/2
        const simd::m32x4 wide = simd::ge(simd::abs(d), simd::set1(kEps));
        const simd::f32x4 q = simd::sel(wide, simd::div(simd::sub(b, b0), d),
                                        simd::sub(fcdsp::tanh(mid), mid));      // ADAA-1
        const simd::f32x4 y = simd::add(x, simd::div(simd::mul(oddWeight(grDb), q), k));
        const simd::m32x4 silent = simd::band(simd::ge(x, zero), simd::ge(zero, x));   // x == 0 (NaN: false)
        return simd::sel(silent, zero, y);
    }
};

static_assert(ColourPolicy<TubePushPull>);

} // namespace fcdsp::stage
