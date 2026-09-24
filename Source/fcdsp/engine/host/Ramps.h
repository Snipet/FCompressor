#pragma once

// Host ramps (01 §5.1, §5.4 steps 2f, 2g and 2i; HR B §1.6; S2/S3 lead revisions "ramp shape"): the 20 ms transitions
// EngineHost runs between two whole signals: bypass (F4), SC listen, delta and the kernel crossfade weight (F7). A pure
// component (K3 #12: F4, then F7 by lead revision 2 of S6); EngineHost.cpp only orchestrates it.
//
// Shape. A LinearRamp (core/Smoother.h: 0...1, fixed length, lands exactly on 0 and 1) whose position passes through
// smootherstep w^3 (10 - 15 w + 6 w^2) before it is applied (01 §5.1, S3 lead revision 1). A straight linear 20 ms fade
// measured +40/+48 dB on the C §5.0 click metric (limit +3); smoothstep w^2 (3 - 2 w) about +1 dB at 6 dB of GR but
// +3.3 dB on the bypass click row at 10.5 dB of GR (F4). Smootherstep has zero slope AND zero curvature at both ends,
// so neither the gain nor its slope has a corner where the fade starts or lands.
//
// Exactness (HR B §1.6: "at either end the output is a straight copy"). blend() returns `a` itself while the ramp's
// position is exactly 0 and `b` itself once it is exactly 1, so a settled bypass is bit-exact against its dry input
// (and a settled processed path against the processed signal) whatever the other signal holds. In between it is the
// equal-gain crossfade (1 - w) a + w b, w = S(t) (the two signals are coherent: latency-aligned, E §5.1), evaluated as
// a + S(t) (b - a) over the first half of the ramp and b + S(1 - t) (a - b) over the second (S(1 - t) = 1 - S(t):
// smootherstep is point-symmetric; 1 - t is exact there). Each half scales the difference by a weight that is small,
// and therefore finely resolved, near its own end. The literal (1 - w) a + w b rounds 1 - w to the 6e-8 grid below 1
// while w is still tiny: a gain staircase on `a` that dsp.null's bypass click row read at +3.6 dB (ECO) to +6.7 dB
// (HQ) against clean controls at 11 dB of GR (F7), at -130 dB absolute; this form stays at the float noise floor.
//
// Rates. Bypass and listen run at the base rate after the downsampler (01 §5.4 step 2i); delta and the kernel crossfade
// weight run per OS sample inside the OS domain (steps 2f, 2g), so EngineHost prepares those two at the OS rate: every
// ramp is 20 ms long at every Quality.
//
// Real time: every member is inline and FCDSP_NONBLOCKING (core/Rt.h); LinearRamp::prepare is out of line and
// annotated (Smoother.cpp). prepare() runs in EngineHost::configure, the rest on the audio thread.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Smoother.h"

namespace fcdsp::host {

inline constexpr float kHostRampMs = 20.0f;             // bypass, listen, delta, kernel crossfade (01 §5.1)

// smootherstep: 0 -> 0 and 1 -> 1 exactly, monotone, zero slope and zero curvature at both ends.
inline float rampShape(float w) noexcept FCDSP_NONBLOCKING
{
    return w * w * w * (w * (w * 6.0f - 15.0f) + 10.0f);
}

// The shaped equal-gain crossfade at linear ramp position t: (1 - S(t)) a + S(t) b, exactly a at t <= 0 and exactly b
// at t >= 1 (see "Exactness" for the evaluation order).
inline float blend(float a, float b, float t) noexcept FCDSP_NONBLOCKING
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    return t <= 0.5f ? a + rampShape(t) * (b - a) : b + rampShape(1.0f - t) * (a - b);
}

// One on/off host transition: a 20 ms LinearRamp applied through rampShape. The position is linear (what UiFrame's
// bypassAmt reports and what blend() takes); the shape is applied inside blend().
struct HostRamp {
    LinearRamp ramp{};

    void prepare(float fs, float ms = kHostRampMs) noexcept FCDSP_NONBLOCKING { ramp.prepare(fs, ms); }
    void setTarget(bool on) noexcept FCDSP_NONBLOCKING { ramp.setTarget(on ? 1.0f : 0.0f); }
    void snap() noexcept FCDSP_NONBLOCKING { ramp.cur = ramp.tgt; }
    float position() const noexcept FCDSP_NONBLOCKING { return ramp.cur; }
    bool restingOff() const noexcept FCDSP_NONBLOCKING { return ramp.cur == 0.0f && ramp.tgt == 0.0f; }
    bool restingOn() const noexcept FCDSP_NONBLOCKING { return ramp.cur == 1.0f && ramp.tgt == 1.0f; }

    // Per sample: advance one sample, then the linear position (blend() applies the shape).
    float tick() noexcept FCDSP_NONBLOCKING { return ramp.tick(); }

    // n positions into `pos` (the ramp advances n samples); at rest it only fills the resting position.
    void positions(float* pos, int n) noexcept FCDSP_NONBLOCKING
    {
        if (restingOff() || restingOn())
        {
            const float v = restingOn() ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
                pos[i] = v;
            return;
        }
        for (int i = 0; i < n; ++i)
            pos[i] = tick();
    }

    // Advance n samples without applying anything (a block whose output is replaced, e.g. the poison fallback).
    void skip(int n) noexcept FCDSP_NONBLOCKING
    {
        for (int i = 0; i < n && ramp.moving(); ++i)
            (void) ramp.tick();
    }

    // out[c][i] = blend(a[c][i], b[c][i], position of sample i) for `channels` channels of n samples, advancing the
    // ramp once per sample. At rest it copies a (off) or b (on) and leaves the ramp alone. out[c] may alias a[c] or
    // b[c]; `amount` is caller scratch of at least n floats.
    void apply(const float* const* a, const float* const* b, float* const* out, int channels, int n,
               float* amount) noexcept FCDSP_NONBLOCKING
    {
        if (restingOff() || restingOn())
        {
            const float* const* src = restingOff() ? a : b;
            for (int c = 0; c < channels; ++c)
                if (src[c] != out[c])
                    for (int i = 0; i < n; ++i)
                        out[c][i] = src[c][i];
            return;
        }
        for (int i = 0; i < n; ++i)
            amount[i] = tick();
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
                out[c][i] = blend(a[c][i], b[c][i], amount[i]);
    }
};

} // namespace fcdsp::host
