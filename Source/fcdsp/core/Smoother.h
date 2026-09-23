#pragma once

// Per-sample parameter smoothing (01 §5.1; E §4.6; K2 #20). Block-size invariant: state advances per sample only.
//   - Smoother4: four independent one-pole smoothers (tau 20 ms by default) with an epsilon landing: once
//     |cur - tgt| < eps in a lane, that lane lands exactly on its target (HardwareReverb's smoothSnap).
//   - LinearRamp: a 0...1 amount moving linearly over a fixed length; it lands exactly on 0 and 1. Used for every
//     gain change that must never step (GR OFF, stage 2 on/off, bypass, listen, delta, the crossfade weight).
// Sentinels are finite range ends (kRangeOff = 60, kS2Off = 24), so nothing smooths from 1000 (K2 #20).
//
// Frozen at FZ0. F0 declares; F1 (S1) implements. Every member is FCDSP_NONBLOCKING (core/Rt.h; FZ0 errata). tick()
// runs per sample, so F1 should define the members inline in this header (an out-of-line definition repeats the
// macro).
//
// How ModeEngine turns its two level smoothers into the per-sample LevelCtl (Stage.h; FZ0 errata, R-F0 #2) is
// documented in ModeEngine.h.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"

namespace fcdsp {

struct Smoother4 {                          // four independent per-sample one-poles, tau 20 ms, epsilon landing
    simd::f32x4 cur, tgt, a, eps;
    void prepare(float fs, float tauMs = 20.f, simd::f32x4 epsilon = simd::set1(1e-5f)) noexcept FCDSP_NONBLOCKING;
    void setTarget(simd::f32x4 t) noexcept FCDSP_NONBLOCKING { tgt = t; }
    void snap() noexcept FCDSP_NONBLOCKING { cur = tgt; }
    simd::f32x4 tick() noexcept FCDSP_NONBLOCKING;   // cur = tgt + a*(cur - tgt); lands exactly when |cur - tgt| < eps
};

struct LinearRamp {                         // 0...1 amount, linear, fixed length; lands exactly on 0 and 1
    float cur = 0, tgt = 0, step = 0;
    void prepare(float fs, float ms = 20.f) noexcept FCDSP_NONBLOCKING;
    void setTarget(float t) noexcept FCDSP_NONBLOCKING;
    float tick() noexcept FCDSP_NONBLOCKING;
    bool moving() const noexcept FCDSP_NONBLOCKING;
};

// ---- Bodies (F1, S1) -------------------------------------------------------------------------------------------------
// prepare() is out of line (Smoother.cpp); the per-sample members are inline here.
//
// Smoother4::prepare(fs, tauMs, epsilon) sets a = alphaFromTau(tauMs, fs) in every lane and eps; it does not move cur
// or tgt. tick() lands a lane exactly on its target when |cur - tgt| < eps OR when the one-pole has stalled: once
// (1 - a) * |cur - tgt| is below half an ulp of the value, tgt + a*(cur - tgt) rounds back to cur and a float one-pole
// never gets closer (HR's reason for smoothSnap's eps, there at block rate). Per sample, with a 20 ms tau, that stall
// sits far above any practical eps (at 48 kHz 1 - a = 1.04e-3: a -40 dB target stalls 1.8e-3 dB short, a slope near 1
// about 3e-5 short; 4x that at 192 kHz), so without the stall test the smoothers would never land and the settled
// engine would not match analysis::staticGr bit for bit (01 §5.1). The landing step is below
// max(eps, ulp(value) / (2 (1 - a))): 0.015 dB for a 60 dB value at 384 kHz (dsp.units).
//
// LinearRamp: step = 1000 / (ms * fs), or 1 (instant) when the ramp is shorter than a sample or ms/fs is <= 0 or NaN;
// prepare() keeps cur and tgt. tick() moves cur toward tgt by step and clamps onto it, so it lands exactly (on 0 and 1,
// or any target in between) after ms * fs / 1000 samples, give or take one for the float accumulation.

inline simd::f32x4 Smoother4::tick() noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 d = simd::sub(cur, tgt);
    const simd::f32x4 next = simd::fma(tgt, a, d);                     // tgt + a*(cur - tgt), one rounding
    const simd::m32x4 stalled = simd::band(simd::ge(next, cur), simd::ge(cur, next));
    cur = simd::sel(simd::bor(simd::gt(eps, simd::abs(d)), stalled), tgt, next);
    return cur;
}

inline void LinearRamp::setTarget(float t) noexcept FCDSP_NONBLOCKING { tgt = t; }

inline float LinearRamp::tick() noexcept FCDSP_NONBLOCKING
{
    if (cur < tgt)
    {
        const float up = cur + step;
        cur = up < tgt ? up : tgt;
    }
    else if (cur > tgt)
    {
        const float down = cur - step;
        cur = down > tgt ? down : tgt;
    }
    return cur;
}

inline bool LinearRamp::moving() const noexcept FCDSP_NONBLOCKING { return cur != tgt; }

} // namespace fcdsp
