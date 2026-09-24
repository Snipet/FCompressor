#pragma once

// stage::SlowHp: the 33609's ATTACK SLOW side-chain high-pass (01 §5.2 scshape/ catalogue, §10.7 Diode 609; D §2.5
// [V S10]: SLOW "adds reduced sensitivity to <100Hz signals", a first-order high-pass at 100 Hz). M5 (S10). It runs per
// sample on the linear SC after the host filters and before the detector (Stage.h), so the compressor's detector hears
// less low end while the audio is untouched:
//
//     H(s) = s / (s + w_c) = 1 - w_c / (s + w_c),   f_c = EngineParams::m[kSlot] Hz (0 = ATTACK FAST: no high-pass)
//
// realised as y = v - a lp, with lp the TPT (trapezoidal, prewarped) one-pole low-pass at f_c and a the switch's
// amount, 0 ... 1: a = 1 is the high-pass exactly, a = 0 the input exactly. The switch is a detent (FAST / SLOW), so
// the amount does not jump: it is a 20 ms one-pole per control tick that lands exactly on 0 or 1 (a value-initialised
// Coeffs lands at once, the ModeEngine convention), and the corner is held while the amount fades out. So a FAST <->
// SLOW edge fades the high-pass in or out instead of stepping the side chain (K2 #4 iv, dsp.zipper's detent rows).
// a = 0 (landed) is an exact bypass with the state cleared: the default Diode 609 (ATTACK FAST) runs Flat's arithmetic,
// bit for bit.
//
// Lanes. Only lanes 0-1 (the compressor's side chain, one per channel) are shaped; lanes 2-3 pass unchanged. The host
// feeds the aux lanes a copy of the channels (Router.h: {c0, c1, c0, c1}), and in Diode 609 they carry the LIMITER's
// side chain (stage::SharedElementMax reads the detector's aux lanes): the 33609's high-pass belongs to the compressor's
// ATTACK switch, and the limiter's own side chain has none (D §2.5: the limiter's attack is FAST / SLOW, 2 / 4 ms,
// without it). The unshaped lanes run no filter state (their integrator gain is 0).
//
// magDb is the compressor side chain's (lanes 0-1) digital |H(e^jw)|: the bilinear transform maps f to the analog
// frequency tan(pi f / fs) / tan(pi f_c / fs) * f_c, so with G = 1 - a,
//     |H|^2 = (W^2 + G^2) / (W^2 + 1),   W = tan(pi f / fs) / tan(pi f_c / fs)
// (-3.01 dB at f_c for a = 1; R37Shelf.h's shelf is the same family with G = 10^(-E/20)), through FastMath's tanPi (no
// libm; f is clamped below Nyquist) and floored at -240 dB (DC of the full high-pass). analysis::scResponse draws it
// (host filters + this); dsp.sharedelement holds the running filter's response to it.

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <int kSlot = 0>
struct SlowHpT {
    static_assert(kSlot >= 0 && kSlot < 8, "SlowHpT: the corner slot indexes EngineParams::m[8]");

    static constexpr float kDefaultCornerHz = 100.0f;   // the corner a fade-out holds when none was ever set (D §2.5)
    static constexpr float kSmoothMs = 20.0f;           // the switch's per-tick fade (01 §5.1: every change ramps)
    static constexpr float kLand = 1e-4f;               // land exactly on 0 or 1 below this distance

    struct Coeffs {
        float amount = 0;                               // a, smoothed (0 ... 1)
        float cornerHz = kDefaultCornerHz;              // f_c, held while the amount fades out
        float tick = 0;                                 // 1 - alpha of the 20 ms smoother at fs / kTickSamples
        simd::f32x4 g{};                                // TPT integrator gain K / (1 + K), K = tan(pi f_c / fs); lanes
                                                        //   0-1 only (lanes 2-3: 0, no state)
        simd::f32x4 depth{};                            // -a on lanes 0-1, 0 on lanes 2-3
        bool on = false;                                // false: an exact bypass
        bool primed = false;                            // false: the next design lands on the target
    };
    struct State { simd::f32x4 z{}; };                  // the TPT integrator

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float hz = p.m[kSlot];
        const bool slow = hz > 0.0f;                    // NaN reads as FAST
        if (slow)
            c.cornerHz = hz;
        const float target = slow ? 1.0f : 0.0f;
        if (!c.primed)
        {
            c.tick = oneMinusAlpha(kSmoothMs, x.fs / static_cast<float>(kTickSamples));
            c.amount = target;
            c.primed = true;
        }
        else
        {
            const float d = target - c.amount;
            const float next = c.amount + c.tick * d;
            c.amount = (d < kLand && d > -kLand) || next == c.amount ? target : next;
        }
        c.on = c.amount > 0.0f && x.fs > 0.0f;
        if (!c.on)
        {
            c.g = simd::set1(0.0f);
            c.depth = simd::set1(0.0f);
            return;
        }
        const float k = fcdsp::tanPi(cornerTurns(c.cornerHz, x.fs));
        alignas(16) const float g[4] = { k / (1.0f + k), k / (1.0f + k), 0.0f, 0.0f };
        alignas(16) const float d[4] = { -c.amount, -c.amount, 0.0f, 0.0f };
        c.g = simd::load(g);
        c.depth = simd::load(d);
    }

    // Linear SC in, shaped SC out: y = v - a lp on lanes 0-1, v on lanes 2-3 (header comment).
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        if (!c.on)
        {
            s.z = simd::set1(0.0f);
            return v;
        }
        const simd::f32x4 w = simd::mul(simd::sub(v, s.z), c.g);    // lanes 2-3: 0 (the SC is sanitised: finite)
        const simd::f32x4 lp = simd::add(w, s.z);
        s.z = simd::add(lp, w);
        // v - a lp on lanes 0-1; lanes 2-3 are v itself (v + 0 * 0 would turn a -0 into +0)
        return simd::sel(simd::gt(c.g, simd::set1(0.0f)), simd::fma(v, c.depth, lp), v);
    }

    // |H(e^j 2 pi hz / fs)| of lanes 0-1 in dB (header comment); 0 dB when bypassed.
    static float magDb(const Coeffs& c, float hz, float fs) noexcept FCDSP_NONBLOCKING
    {
        if (!c.on || !(fs > 0.0f))
            return 0.0f;
        const float gain = 1.0f - c.amount;
        const float turns = hz / fs < kMaxTurns ? (hz > 0.0f ? hz / fs : 0.0f) : kMaxTurns;
        const float w = fcdsp::tanPi(turns) / fcdsp::tanPi(cornerTurns(c.cornerHz, fs));
        const float w2 = w * w;
        const float ratio = (w2 + gain * gain) / (w2 + 1.0f);
        return 0.5f * kDbPerLog2 * fcdsp::log2(ratio > kMinPower ? ratio : kMinPower);
    }

    // The smoothed amount (0 FAST ... 1 SLOW), for probes and telemetry.
    static float amountOf(const Coeffs& c) noexcept FCDSP_NONBLOCKING { return c.amount; }

private:
    static constexpr float kMaxTurns = 0.499f;          // tanPi's domain [0, 0.499]
    static constexpr float kMinPower = 1e-24f;          // -240 dB: the full high-pass at DC

    static float cornerTurns(float hz, float fs) noexcept FCDSP_NONBLOCKING
    {
        const float t = hz / fs;
        return t < kMaxTurns ? (t > 0.0f ? t : 0.0f) : kMaxTurns;
    }
};

using SlowHp = SlowHpT<0>;

static_assert(ScShapePolicy<SlowHp>);

} // namespace fcdsp::stage
