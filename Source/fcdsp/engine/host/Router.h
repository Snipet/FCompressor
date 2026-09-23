#pragma once

// The host's stereo router (01 §3.1 `stmode`, §5.4 steps 2a and 2e, §5.5; E §8; K2 #3d, #22): the side-chain source
// (key select), the six universal stereo modes (encode of the side chain and of the audio, decode, which channel takes
// the GR), the engine's lane domain for Carry::msDomain, and the SC listen decode. A pure component with one owner (K3
// #12: F5); EngineHost.cpp (F7) orchestrates it. Everything here is a pure function of its arguments.
//
// The stereo code is EngineParams::stmode, the universal code (01 §3.1; Resolve.cpp copies the PLAIN value, so a Mode
// listing a subset, e.g. Brickwall's ST/MID/SIDE as {0, 2, 3}, still means the same modes):
//
//   code  mode         lanes 0-1   side chain (lanes c0, c1, aux0, aux1)   GR applied to
//   0     STEREO       L, R        L, R, L, R                              L and R
//   1     M/S          M, S        M, S, M, S                              M and S
//   2     MID          M, S        M, M, M, M                              M only (S passes)
//   3     SIDE         M, S        S, S, S, S                              S only (M passes)
//   4     M>S          M, S        M, M, M, M                              S only: the mid keys the side
//   5     S>M          M, S        S, S, S, S                              M only: the side keys the mid
//   6, 7  reserved     M, S        as M/S                                  M and S
//
// with M = (L + R) / 2 and S = (L - R) / 2, L = M + S, R = M - S (E §8, unity round trip). Choices where the design is
// silent (F5):
//   - a single-source mode feeds its ONE detection signal to every side-chain lane, so every link law sees two equal
//     lanes and returns them unchanged (a silent second lane would halve a mean link); the channel that must pass is
//     then held at 0 dB GR by maskGr(), which the host applies to the engine's GR before the gain and the telemetry;
//   - the reserved codes 6 and 7 behave as M/S, the lane domain ModeEngine assigns them (every code but STEREO encodes);
//   - the audio decode is RESIDUAL: out = x - D(c - w), with x the path's un-encoded input, c = E(x) its encode and w the
//     processed lanes, D the plain decode. It equals D(w) up to rounding, and it is bit-exact whenever the processing
//     left a lane untouched (w == c: a GR of 0 dB, a masked lane): the plain M/S round trip D(E(x)) cannot be, since
//     (L + R, L - R) in float loses bits. So a transparent path is transparent at every stmode (the D8 nulls).
//   - SC listen (E §8) decodes the side chain as the engine hears it: L/R as is; M/S decoded to L/R; a single-source
//     mode's detection signal on both outputs.
//
// Key select (01 §5.4 step 2a, K2 #22): the SC source is the key when the EFFECTIVE key is external, i.e. extKey is on
// AND a key bus is active (configured with channels and delivered with a first channel), else the main input. The
// effective flag is part of the kernel key (Crossfade.h KernelKey::keyExt), so a bus appearing or vanishing crossfades
// like a toggle (F7). A mono source feeds both channels. The key is never pre-gained (the host multiplies an internal
// side chain by the path preGain, 01 §5.4 step 2e); the router does not scale.
//
// Real time: every function is inline, allocation-free and FCDSP_NONBLOCKING.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include <cstdint>

namespace fcdsp::host {

enum class StereoMode : uint8_t { stereo = 0, midSide = 1, mid = 2, side = 3, midKeysSide = 4, sideKeysMid = 5 };
inline constexpr int kStereoModes = 6;                   // universal codes 0..5; 6 and 7 are reserved (01 §3.1)
inline constexpr int kStereoCodes = 8;                   // the host range of stmode (capacity 8, 01 §3.1)

// What a stereo code does, as a table the host reads.
struct Route {
    StereoMode mode = StereoMode::stereo;
    LaneDomain domain = LaneDomain::lr;                  // lanes 0-1 of the engine: L/R or M/S (Carry::msDomain)
    int8_t scFrom = -1;                                  // -1: each lane detects its own channel; 0: every lane
                                                         //   detects M (lane 0); 1: every lane detects S (lane 1)
    bool gain0 = true, gain1 = true;                     // whether lane 0 / lane 1 of the audio takes the GR
};

constexpr StereoMode stereoMode(uint8_t code) noexcept FCDSP_NONBLOCKING
{
    return code < kStereoModes ? static_cast<StereoMode>(code) : StereoMode::midSide;   // reserved: as M/S
}

constexpr Route routeOf(uint8_t code) noexcept FCDSP_NONBLOCKING
{
    Route r;
    r.mode = stereoMode(code);
    switch (r.mode)
    {
        case StereoMode::stereo:      return r;
        case StereoMode::midSide:     break;
        case StereoMode::mid:         r.scFrom = 0; r.gain1 = false; break;
        case StereoMode::side:        r.scFrom = 1; r.gain0 = false; break;
        case StereoMode::midKeysSide: r.scFrom = 0; r.gain0 = false; break;
        case StereoMode::sideKeysMid: r.scFrom = 1; r.gain1 = false; break;
    }
    r.domain = LaneDomain::ms;
    return r;
}

// The engine's lane domain for a code: L/R for STEREO, M/S for every other code (ModeEngine's rule, K2 #3d).
constexpr LaneDomain laneDomain(uint8_t code) noexcept FCDSP_NONBLOCKING { return routeOf(code).domain; }

// ---- key select ------------------------------------------------------------------------------------------------------

// The effective external key (K2 #22): extKey AND an active key bus (configured channels and a delivered first
// channel). The kernel key's keyExt.
inline bool keyExternal(bool extKey, int keyChansConfigured, const float* const* key, int numKey) noexcept
    FCDSP_NONBLOCKING
{
    return extKey && keyChansConfigured > 0 && key != nullptr && numKey > 0 && key[0] != nullptr;
}

struct ScSource {
    const float* ch[2] = { nullptr, nullptr };           // left and right source channel (a mono source twice)
    bool external = false;
};

// The side-chain source channels: the key when `external` (keyExternal()), else the main input; a mono source (one
// channel, or a missing second one) feeds both. `main` must hold at least one channel.
inline ScSource selectSc(const float* const* main, int numMain, const float* const* key, int numKey,
                         bool external) noexcept FCDSP_NONBLOCKING
{
    ScSource s;
    s.external = external;
    const float* const* src = external ? key : main;
    const int num = external ? numKey : numMain;
    s.ch[0] = src[0];
    s.ch[1] = num > 1 && src[1] != nullptr ? src[1] : src[0];
    return s;
}

// out[i] = {L, R, L, R} of a source (the aux lanes carry a copy of the channels, the EngineRig convention).
inline void scLanes(const ScSource& s, simd::f32x4* out, int n) noexcept FCDSP_NONBLOCKING
{
    for (int i = 0; i < n; ++i)
    {
        alignas(16) const float v[4] = { s.ch[0][i], s.ch[1][i], s.ch[0][i], s.ch[1][i] };
        out[i] = simd::load(v);
    }
}

// ---- side chain ------------------------------------------------------------------------------------------------------

// One side-chain sample {L, R, -, -} encoded per route: lanes {c0, c1, c0, c1}.
inline simd::f32x4 encodeSc(simd::f32x4 lr, const Route& r) noexcept FCDSP_NONBLOCKING
{
    const float l = simd::lane<0>(lr), rr = simd::lane<1>(lr);
    if (r.domain == LaneDomain::lr)
    {
        alignas(16) const float v[4] = { l, rr, l, rr };
        return simd::load(v);
    }
    const float m = 0.5f * (l + rr), s = 0.5f * (l - rr);
    const float c0 = r.scFrom == 1 ? s : m;
    const float c1 = r.scFrom == 0 ? m : s;
    alignas(16) const float v[4] = { c0, c1, c0, c1 };
    return simd::load(v);
}

// n side-chain samples; out may alias in.
inline void encodeSc(const simd::f32x4* in, simd::f32x4* out, int n, const Route& r) noexcept FCDSP_NONBLOCKING
{
    for (int i = 0; i < n; ++i)
        out[i] = encodeSc(in[i], r);
}

// The engine's GR with the passing channel held at 0 dB (lanes 0-1; lanes 2-3 unchanged).
inline simd::f32x4 maskGr(simd::f32x4 gr, const Route& r) noexcept FCDSP_NONBLOCKING
{
    if (!r.gain0)
        gr = simd::withLane<0>(gr, 0.0f);
    if (!r.gain1)
        gr = simd::withLane<1>(gr, 0.0f);
    return gr;
}

// SC listen: the side chain as the engine hears it, decoded to L/R (lanes 0-1 of each sample).
inline void listen(const simd::f32x4* sc, float* outL, float* outR, int n, const Route& r) noexcept FCDSP_NONBLOCKING
{
    for (int i = 0; i < n; ++i)
    {
        const float c0 = simd::lane<0>(sc[i]), c1 = simd::lane<1>(sc[i]);
        if (r.domain == LaneDomain::lr)
        {
            outL[i] = c0;
            outR[i] = c1;
        }
        else if (r.scFrom >= 0)
        {
            outL[i] = c0;
            outR[i] = c0;
        }
        else
        {
            outL[i] = c0 + c1;
            outR[i] = c0 - c1;
        }
    }
}

// ---- audio (the OS rate) ---------------------------------------------------------------------------------------------

// c0/c1 = the route's lanes of L/R: a copy for STEREO, M = (L + R) / 2 and S = (L - R) / 2 otherwise. c0 may alias l
// and c1 r (in place).
inline void encode(const float* l, const float* r, float* c0, float* c1, int n, const Route& rt) noexcept
    FCDSP_NONBLOCKING
{
    if (rt.domain == LaneDomain::lr)
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = l[i], b = r[i];
            c0[i] = a;
            c1[i] = b;
        }
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        const float a = l[i], b = r[i];
        c0[i] = 0.5f * (a + b);
        c1[i] = 0.5f * (a - b);
    }
}

// The residual decode (header comment): outL/outR = x - D(E(x) - w), with x = (l, r) the path's un-encoded input and
// w = (w0, w1) its processed lanes; STEREO copies w. Bit-exact x wherever w == E(x); within rounding of D(w) otherwise.
// Every output may alias w0/w1 or l/r (each index is read before it is written).
inline void decode(const float* w0, const float* w1, const float* l, const float* r, float* outL, float* outR, int n,
                   const Route& rt) noexcept FCDSP_NONBLOCKING
{
    if (rt.domain == LaneDomain::lr)
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = w0[i], b = w1[i];
            outL[i] = a;
            outR[i] = b;
        }
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        const float a = l[i], b = r[i];
        const float d0 = 0.5f * (a + b) - w0[i];          // c - w; +0 exactly when the lane is untouched
        const float d1 = 0.5f * (a - b) - w1[i];
        outL[i] = a - (d0 + d1);                          // x - 0 keeps x bit for bit, -0 included
        outR[i] = b - (d0 - d1);
    }
}

} // namespace fcdsp::host
