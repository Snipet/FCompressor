#pragma once

// stage::R37Shelf: the LA-2A's R37 "EMPHASIS" set-screw, a first-order shelf in the side chain (01 §5.2 scshape/
// catalogue, §10.6 Opto 2A; D §2.2 [V S31]: "high-emphasis for the frequencies above 1 kHz", lows attenuated "for up
// to 10dB"). It runs per sample on the linear SC after the host filters and before the detector (Stage.h), so the
// cell hears less low end and the emphasis never touches the audio:
//
//     H(s) = (s + G w_c) / (s + w_c),   G = 10^(-E / 20),   E = EngineParams::m[kSlot] in dB (0 ... 10)
//
// unity above the corner w_c = 2 pi kCornerHz [H], -E dB below G f_c (E = 10: -10 dB under 316 Hz). It is the TPT
// (trapezoidal, prewarped) one-pole low-pass lp of the corner, recombined as y = v + (G - 1) lp, so G = 1 is the input
// exactly. E is smoothed per control tick in dB (a 20 ms one-pole landing exactly; a value-initialised Coeffs lands at
// once, the ModeEngine convention), so a moving set-screw never steps the side chain. E = 0 (landed) is an exact bypass
// with the state cleared: the default Opto 2A (EMPHASIS 0) runs Flat's arithmetic, bit for bit.
//
// magDb is the digital filter's own |H(e^jw)|: the bilinear transform maps f to the analog frequency tan(pi f / fs) /
// tan(pi f_c / fs) * f_c, so |H|^2 = (W^2 + G^2) / (W^2 + 1) with W = tan(pi f / fs) / tan(pi f_c / fs), through
// FastMath's tanPi (no libm; f is clamped below Nyquist). analysis::scResponse draws it (host filters + this), and
// dsp.optocell holds the running filter's impulse response to it.
//
// Opto 2A's physical() hands the host's `sce` tilt a neutral 0 dB/oct and this shelf the R37 amount (S9 lead revision
// 4: EMPHASIS once drove both, a double emphasis).

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <int kSlot = 0>
struct R37ShelfT {
    static_assert(kSlot >= 0 && kSlot < 8, "R37ShelfT: the emphasis slot indexes EngineParams::m[8]");

    static constexpr float kCornerHz = 1000.0f;     // [H] the shelf's upper corner: emphasis "above 1 kHz"
    static constexpr float kMaxDb = 10.0f;          // "for up to 10dB" (the set-screw's end)
    static constexpr float kSmoothMs = 20.0f;       // the set-screw's per-tick smoothing
    static constexpr float kLandDb = 1e-4f;

    struct Coeffs {
        float emphasisDb = 0;                       // E, smoothed (dB)
        float tick = 0;                             // 1 - alpha of the 20 ms smoother at fs / kTickSamples
        float fs = 0;                               // the rate the filter runs at
        simd::f32x4 g{};                            // the TPT integrator gain K / (1 + K), K = tan(pi f_c / fs)
        simd::f32x4 depth{};                        // G - 1 (0 ... -0.684)
        bool on = false;                            // false: an exact bypass
        bool primed = false;                        // false: the next design lands on the target
    };
    struct State { simd::f32x4 z{}; };              // the TPT integrator

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float target = clampDb(p.m[kSlot]);
        if (!c.primed)
        {
            c.tick = oneMinusAlpha(kSmoothMs, x.fs / static_cast<float>(kTickSamples));
            c.emphasisDb = target;
            c.primed = true;
        }
        else
        {
            const float d = target - c.emphasisDb;
            const float next = c.emphasisDb + c.tick * d;
            c.emphasisDb = (d < kLandDb && d > -kLandDb) || next == c.emphasisDb ? target : next;
        }
        c.fs = x.fs;
        c.on = c.emphasisDb > 0.0f && x.fs > 0.0f;
        if (!c.on)
        {
            c.g = simd::set1(0.0f);
            c.depth = simd::set1(0.0f);
            return;
        }
        const float k = fcdsp::tanPi(cornerTurns(x.fs));
        c.g = simd::set1(k / (1.0f + k));
        c.depth = simd::set1(fcdsp::exp2(-c.emphasisDb * kLog2PerDb) - 1.0f);
    }

    // Linear SC in, shaped SC out.
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        if (!c.on)
        {
            s.z = simd::set1(0.0f);
            return v;
        }
        const simd::f32x4 w = simd::mul(simd::sub(v, s.z), c.g);
        const simd::f32x4 lp = simd::add(w, s.z);
        s.z = simd::add(lp, w);
        return simd::fma(v, c.depth, lp);                        // v + (G - 1) lp
    }

    // |H(e^j 2 pi hz / fs)| in dB (header comment); 0 dB when bypassed.
    static float magDb(const Coeffs& c, float hz, float fs) noexcept FCDSP_NONBLOCKING
    {
        if (!c.on || !(fs > 0.0f))
            return 0.0f;
        const float gain = 1.0f + simd::lane<0>(c.depth);
        const float turns = hz / fs < kMaxTurns ? (hz > 0.0f ? hz / fs : 0.0f) : kMaxTurns;
        const float w = fcdsp::tanPi(turns) / fcdsp::tanPi(cornerTurns(fs));
        const float w2 = w * w;
        return 0.5f * kDbPerLog2 * fcdsp::log2((w2 + gain * gain) / (w2 + 1.0f));
    }

private:
    static constexpr float kMaxTurns = 0.499f;       // tanPi's domain [0, 0.499]

    static float clampDb(float e) noexcept FCDSP_NONBLOCKING
    {
        return e > 0.0f ? (e < kMaxDb ? e : kMaxDb) : 0.0f;       // NaN reads as 0 (no emphasis)
    }
    static float cornerTurns(float fs) noexcept FCDSP_NONBLOCKING
    {
        const float t = kCornerHz / fs;
        return t < kMaxTurns ? t : kMaxTurns;
    }
};

using R37Shelf = R37ShelfT<0>;

static_assert(ScShapePolicy<R37Shelf>);

} // namespace fcdsp::stage
