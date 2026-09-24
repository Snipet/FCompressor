#pragma once

// stage::TubeTransformer: Opto 2A's TUBE + TRANSFORMERS voice, the LA-2A's tube line amplifier and its transformers
// (01 §5.2 colour/ catalogue, §10.6 Opto 2A VOICE locked TUBE; D §2.2 "Tube and transformer; a smooth, low-order
// signature"; E §2.9). A driven static voice, level-compensated like TubeSym.h's (small signals pass at unity whatever
// the drive), with an odd and an even term:
//
//     f(u) = tanh u - kEven tanh^2 u,     u = k x,   k = kIn * 10^(DRIVE / 20),   y = x + (A(u) - u - kEven tanh^2 u) / k
//
// tanh is the transformers' soft saturation (odd: the third harmonic, u^2 / 12 relative), and the even term the
// single-ended tube stages' asymmetry (the second harmonic, kEven u / 2 relative, the larger term at every level up to
// saturation); f(0) = 0, f'(0) = 1, f is monotone (kEven < 1/2) and bounded. The odd part runs through Adaa.h's ADAA-1
// residual scheme (A = tanh anti-aliased, the linear part undelayed); the even part is applied per sample, without
// ADAA: a quadratic only doubles frequencies, so at base rate it aliases only what lies above fs / 4 (a full-scale
// 20 kHz sine's second harmonic folds to 8 kHz at 48 kHz ECO: -62 dBFS at 0 dB of drive, -38 dBFS at +24 dB; STD and
// HQ run it oversampled), while ADAA-1's segment average would smear a loud sample's even residual into the next,
// quiet one. Memoryless and no DC blocker: the even term's DC (-kEven k x^2 / 2 for a sine of amplitude x; -62 dBFS for
// a 0 dBFS sine at 0 dB of drive) stays in the output, because a blocker's memory releases a loud passage's DC into
// the quiet one after it, where it is large relative to the signal (0.66 dB of y / x on dsp.time's square, 40 dB below
// a +12 dBFS step). The input scale k glides linearly across each call from where the last one ended (DRIVE moves per
// control tick; a step in k would step the even term's level: +20 dB on dsp.zipper's DRIVE edge); a static DRIVE
// gives k exactly.
//
// The level (kIn, kEven) is set by the probes' meter truth, not by the hardware's distortion figures (docs/modes/
// opto-2a.md): VOICE is locked, so every probe runs through this stage, and dsp.time's tap-vs-audio row (character:
// <= 0.1 dB at every sample) reads the colour's gain y / x on a square whose first samples, before the cell attacks,
// sit at the input threshold + 20 dB (+12 dBFS at the default PEAK RED.): u = 0.062 there, u^2 / 3 + kEven u = 0.065
// dB, plus ADAA-1's odd residual averaged across the sample where the square later drops 40 dB from the level the GR
// settles it at (u_loud^3 / (12 u_quiet), 0.011 dB). At 0 dB of drive the second harmonic is -62 dB at 0 dBFS and -80 dB
// at 0 VU (-18 dBFS), the third -94 dB at 0 dBFS; DRIVE (the standard extension, +-24 dB) scales u, so +18 dB of drive
// gives the hardware's class of distortion (about -62 dB H2 at 0 VU, -44 dB at 0 dBFS).
//
// The COLOUR view (colourCurve, f(k x) / k) and the describing-function static curve are exact (colourStatic = true:
// the shape does not depend on GR). DRIVE is smoothed per control tick (20 ms, in dB, detail::VoiceDrive).

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct TubeTransformer {
    static constexpr float kIn = 1.0f / 64.0f;      // [H] shaper input at 0 dB drive per unit of signal
    static constexpr float kEven = 0.1f;            // [H] the tube stages' even term

    struct Coeffs { detail::VoiceDrive drive{}; };
    struct State {
        adaa::Channel ch{};
        float k = 0;                                // the input scale at the end of the last call (0: none yet)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.drive.design(p, x, kIn);
    }

    static void process(const Coeffs& c, State& s, float* x, const float*, int n, int) noexcept FCDSP_NONBLOCKING
    {
        constexpr int kBlock = 64;
        alignas(16) float u[kBlock], kv[kBlock];
        if (n <= 0)
            return;
        // k moves per call (DRIVE is designed per control tick): it glides linearly across the call from where the last
        // one ended, so the even term's level never steps between samples (a static DRIVE gives k exactly).
        const float k0 = s.k > 0.0f ? s.k : c.drive.k, k1 = c.drive.k, dk = (k1 - k0) / static_cast<float>(n);
        for (int off = 0; off < n; off += kBlock)
        {
            const int m = n - off < kBlock ? n - off : kBlock;
            float* const xs = x + off;
            for (int i = 0; i < m; ++i)
            {
                kv[i] = off + i + 1 < n ? k0 + dk * static_cast<float>(off + i + 1) : k1;
                u[i] = kv[i] * xs[i];
            }
            adaa::process(adaa::Tanh{}, s.ch, u, m);                // u = A(k x): tanh, anti-aliased
            int i = 0;
            for (; i + 4 <= m; i += 4)
                simd::store(xs + i, step(simd::load(xs + i), simd::load(u + i), simd::load(kv + i)));
            for (; i < m; ++i)                                     // the tail: lane 0 of the same step
                xs[i] = simd::lane<0>(step(simd::set1(xs[i]), simd::set1(u[i]), simd::set1(kv[i])));
        }
        s.k = k1;
    }

    // The static curve f(k x) / k (the COLOUR view); grDb is not used (colourStatic).
    static float transfer(const Coeffs& c, float x, float) noexcept FCDSP_NONBLOCKING
    {
        const float t = fcdsp::tanh(c.drive.k * x);
        return (t - kEven * t * t) * c.drive.invK;
    }

    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }

private:
    // y = x + (A - k x - kEven tanh^2(k x)) / k for one group: A the anti-aliased tanh of k x.
    static simd::f32x4 step(simd::f32x4 x, simd::f32x4 a, simd::f32x4 k) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 kx = simd::mul(k, x);
        const simd::f32x4 t = fcdsp::tanh(kx);
        const simd::f32x4 r = simd::fms(simd::sub(a, kx), simd::set1(kEven), simd::mul(t, t));
        return simd::add(x, simd::div(r, k));
    }
};

static_assert(ColourPolicy<TubeTransformer>);

} // namespace fcdsp::stage
