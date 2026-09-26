#pragma once

// stage::BandEmphasisT<kSlot>: a band emphasis in the side chain, a peaking bell (01 §5.2 scshape/ catalogue; Octo, the
// hybrid VCA's detector "Band-Emphasis" button: an "emphasized 6kHz band", D §2.7 [V S14]). It runs per sample on the
// linear SC after the host filters and before the detector (Stage.h), so the detector hears more of the band (sibilance,
// cymbals) and the emphasis never touches the audio:
//
//     H(z) = RBJ peaking EQ at f0 = kCentreHz, Q = kQ, gain G = EngineParams::m[kSlot] dB (0 ... kMaxDb)
//
//   A = 10^(G / 40), w0 = 2 pi f0 / fs, alpha = sin w0 / (2 Q):
//   b0 = 1 + alpha A, b1 = -2 cos w0, b2 = 1 - alpha A;  a0 = 1 + alpha / A, a1 = -2 cos w0, a2 = 1 - alpha / A
//
// run as a transposed direct form II biquad per lane, the coefficients normalised by a0. G is smoothed per control tick
// in dB (a 20 ms one-pole landing exactly; a value-initialised Coeffs lands at once, the ModeEngine convention), so the
// button never steps the side chain. G = 0 (landed) is an exact bypass with the state cleared: a Mode with the button
// off runs Flat's arithmetic, bit for bit. f0 is clamped below Nyquist (kMaxTurns of fs).
//
// magDb is the digital filter's own |H(e^jw)|:
//   |H|^2 = (b0^2 + b1^2 + b2^2 + 2 (b0 b1 + b1 b2) c + 2 b0 b2 (2 c^2 - 1)) / (same over a), c = cos(2 pi hz / fs)
// through FastMath's cosPi and log2 (no libm). analysis::scResponse draws it (host filters + this).

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <int kSlot = 0>
struct BandEmphasisT {
    static_assert(kSlot >= 0 && kSlot < 8, "BandEmphasisT: the gain slot indexes EngineParams::m[8]");

    static constexpr float kCentreHz = 6000.0f;     // "emphasized 6kHz band" (D §2.7 [V S14])
    static constexpr float kQ = 1.0f;               // [H] about 1.4 octaves wide at half gain
    static constexpr float kMaxDb = 18.0f;          // the largest emphasis a Mode may ask for
    static constexpr float kSmoothMs = 20.0f;       // the button's per-tick smoothing
    static constexpr float kLandDb = 1e-4f;

    struct Coeffs {
        float gainDb = 0;                           // G, smoothed (dB)
        float tick = 0;                             // 1 - alpha of the 20 ms smoother at fs / kTickSamples
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // normalised by a0
        simd::f32x4 vb0{}, vb1{}, vb2{}, va1{}, va2{};
        bool on = false;                            // false: an exact bypass
        bool primed = false;                        // false: the next design lands on the target
    };
    struct State { simd::f32x4 s1{}, s2{}; };       // TDF-II

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float target = clampDb(p.m[kSlot]);
        if (!c.primed)
        {
            c.tick = oneMinusAlpha(kSmoothMs, x.fs / static_cast<float>(kTickSamples));
            c.gainDb = target;
            c.primed = true;
        }
        else
        {
            const float d = target - c.gainDb;
            const float next = c.gainDb + c.tick * d;
            c.gainDb = (d < kLandDb && d > -kLandDb) || next == c.gainDb ? target : next;
        }
        c.on = c.gainDb > 0.0f && x.fs > 0.0f;
        if (!c.on)
        {
            c.b0 = 1.0f;
            c.b1 = c.b2 = c.a1 = c.a2 = 0.0f;
            c.vb0 = simd::set1(1.0f);
            c.vb1 = c.vb2 = c.va1 = c.va2 = simd::set1(0.0f);
            return;
        }
        const float turns = centreTurns(x.fs);                  // w0 / pi
        const float cw = fcdsp::cosPi(turns), sw = fcdsp::sinPi(turns);
        const float a = fcdsp::exp2(c.gainDb * (0.5f * kLog2PerDb));   // 10^(G / 40)
        const float alpha = sw / (2.0f * kQ);
        const float a0 = 1.0f + alpha / a;
        const float inv = 1.0f / a0;
        c.b0 = (1.0f + alpha * a) * inv;
        c.b1 = -2.0f * cw * inv;
        c.b2 = (1.0f - alpha * a) * inv;
        c.a1 = -2.0f * cw * inv;
        c.a2 = (1.0f - alpha / a) * inv;
        c.vb0 = simd::set1(c.b0);
        c.vb1 = simd::set1(c.b1);
        c.vb2 = simd::set1(c.b2);
        c.va1 = simd::set1(c.a1);
        c.va2 = simd::set1(c.a2);
    }

    // Linear SC in, shaped SC out.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        if (!c.on)
        {
            s.s1 = simd::set1(0.0f);
            s.s2 = simd::set1(0.0f);
            return v;
        }
        const simd::f32x4 y = simd::fma(s.s1, c.vb0, v);                           // s1 + b0 v (simd::fma(a, b, c) = a + b c)
        s.s1 = simd::sub(simd::fma(s.s2, c.vb1, v), simd::mul(y, c.va1));      // s2 + b1 v - a1 y
        s.s2 = simd::sub(simd::mul(v, c.vb2), simd::mul(y, c.va2));            // b2 v - a2 y
        return y;
    }

    // |H(e^j 2 pi hz / fs)| in dB (header comment); 0 dB when bypassed.
    static float magDb(const Coeffs& c, float hz, float fs) noexcept FCDSP_NONBLOCKING
    {
        if (!c.on || !(fs > 0.0f))
            return 0.0f;
        const float t = hz > 0.0f ? (2.0f * hz / fs < 1.0f ? 2.0f * hz / fs : 1.0f) : 0.0f;   // w / pi
        const float cw = fcdsp::cosPi(t);
        const float c2 = 2.0f * cw * cw - 1.0f;
        const float num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2 + 2.0f * (c.b0 * c.b1 + c.b1 * c.b2) * cw
                        + 2.0f * c.b0 * c.b2 * c2;
        const float den = 1.0f + c.a1 * c.a1 + c.a2 * c.a2 + 2.0f * (c.a1 + c.a1 * c.a2) * cw + 2.0f * c.a2 * c2;
        return 0.5f * kDbPerLog2 * fcdsp::log2(num / den);
    }

private:
    static constexpr float kMaxTurns = 0.98f;        // w0 / pi below Nyquist

    static float clampDb(float g) noexcept FCDSP_NONBLOCKING
    {
        return g > 0.0f ? (g < kMaxDb ? g : kMaxDb) : 0.0f;      // NaN reads as 0 (no emphasis)
    }
    static float centreTurns(float fs) noexcept FCDSP_NONBLOCKING
    {
        const float t = 2.0f * kCentreHz / fs;
        return t < kMaxTurns ? t : kMaxTurns;
    }
};

using BandEmphasis = BandEmphasisT<0>;

static_assert(ScShapePolicy<BandEmphasis>);

} // namespace fcdsp::stage
