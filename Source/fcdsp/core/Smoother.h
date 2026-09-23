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

} // namespace fcdsp
