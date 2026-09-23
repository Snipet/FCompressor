#pragma once

// Host ramps (01 §5.1, §5.4 step 2i; HR B §1.6; S2 lead revision "ramp shape"): the 20 ms transitions EngineHost runs
// between two whole signals: bypass (F4), SC listen and delta (F7). A pure component with one owner (K3 #12: F4);
// EngineHost.cpp only orchestrates it.
//
// Shape. A LinearRamp (core/Smoother.h: 0...1, fixed length, lands exactly on 0 and 1) whose position passes through
// smoothstep w^2 (3 - 2w) before it is applied: the shape of ModeEngine's GR OFF and stage-2 fades. A straight linear
// 20 ms fade measured +40/+48 dB on the C §5.0 click metric (limit +3), the shaped one about +1 dB (F3, S2): the shape
// has zero slope at both ends, so the crossfade has no gain-slope corner.
//
// Exactness (HR B §1.6: "at either end the output is a straight copy"). blend() returns `a` itself while the applied
// amount is exactly 0 and `b` itself once it is exactly 1, so a settled bypass is bit-exact against its dry input (and
// a settled processed path against the processed signal) whatever the other signal holds. In between it is the
// equal-gain crossfade (1 - w) a + w b: the two signals are coherent (latency-aligned, E §5.1).
//
// Real time: every member is inline and FCDSP_NONBLOCKING (core/Rt.h); LinearRamp::prepare is out of line and
// annotated (Smoother.cpp). prepare() runs in EngineHost::configure, the rest on the audio thread.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Smoother.h"

namespace fcdsp::host {

inline constexpr float kHostRampMs = 20.0f;             // bypass, listen, delta (01 §5.1)

// smoothstep: 0 -> 0 and 1 -> 1 exactly, monotone, zero slope at both ends.
inline float rampShape(float w) noexcept FCDSP_NONBLOCKING
{
    return w * w * (3.0f - 2.0f * w);
}

// (1 - w) a + w b; exactly a at w <= 0 and exactly b at w >= 1 (see "Exactness").
inline float blend(float a, float b, float w) noexcept FCDSP_NONBLOCKING
{
    if (w <= 0.0f)
        return a;
    if (w >= 1.0f)
        return b;
    return (1.0f - w) * a + w * b;
}

// One on/off host transition: a 20 ms LinearRamp applied through rampShape. The position is linear (what UiFrame's
// bypassAmt reports); the amount is the shaped value the audio is blended with.
struct HostRamp {
    LinearRamp ramp{};

    void prepare(float fs, float ms = kHostRampMs) noexcept FCDSP_NONBLOCKING { ramp.prepare(fs, ms); }
    void setTarget(bool on) noexcept FCDSP_NONBLOCKING { ramp.setTarget(on ? 1.0f : 0.0f); }
    void snap() noexcept FCDSP_NONBLOCKING { ramp.cur = ramp.tgt; }
    float position() const noexcept FCDSP_NONBLOCKING { return ramp.cur; }
    bool restingOff() const noexcept FCDSP_NONBLOCKING { return ramp.cur == 0.0f && ramp.tgt == 0.0f; }
    bool restingOn() const noexcept FCDSP_NONBLOCKING { return ramp.cur == 1.0f && ramp.tgt == 1.0f; }

    // Per sample: advance one sample, then the applied amount.
    float tick() noexcept FCDSP_NONBLOCKING { return rampShape(ramp.tick()); }

    // out[c][i] = blend(a[c][i], b[c][i], amount of sample i) for `channels` channels of n samples, advancing the
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
