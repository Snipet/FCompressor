#pragma once

// stage::TubeSym: Clean's TUBE voice, a symmetric soft saturation (odd harmonics) (01 §5.2 colour/ catalogue; 01 §10.3
// Clean voice TUBE; D §2.8 Pro-C 3 character Tube; E §2.9). It also defines the driven static voice that TubeSym,
// DiodeAsym and Bright share (detail::VoiceDrive, detail::processDriven).
//
// A driven static voice is a memoryless shaper f (f(0) = 0, f'(0) = 1) at an input scale k, level-compensated so small
// signals pass at unity gain whatever the drive:
//
//     y = x + (A(k x) - k x) / k,    k = kIn * 10^(driveDb / 20)
//
// where A is f through the ADAA-1 residual scheme of Adaa.h (only f(u) - u is anti-aliased; the linear part is not
// delayed, so the voice combs with nothing at any mix). The voices are memoryless on purpose: the COLOUR view and the
// describing-function curve (01 §7 colourCurve, E §6.3) draw exactly f(k x) / k, and dsp.static checks a voice's
// fundamental gain against that curve (colourStatic = true: the shape does not depend on GR, so grDb is ignored).
// drive is smoothed per control tick (01 §5.1: "driveDb is smoothed per tick by each engine's colour stage"): a 20 ms
// one-pole in dB at fs / kTickSamples, landing exactly on the target; a value-initialised Coeffs lands at once (the
// ModeEngine convention for snapParams and the analysis entry points). The process runs at the OS rate (StageCtx::fsOs)
// in place, in sub-blocks of 64 samples through a stack buffer.
//
// TUBE: f = tanh (adaa::Tanh), kIn = 1/2 [H]: at 0 dB drive a 0 dBFS sine drives the shaper to 0.5 (about 2 % H3), a
// -18 dBFS one to 0.06 (0.03 %). Constants and their reasons: docs/modes/clean.md.

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

namespace detail {

// The drive of a static voice, smoothed per control tick in dB.
struct VoiceDrive {
    static constexpr float kSmoothMs = 20.0f;
    static constexpr float kLandDb = 1e-4f;

    float driveDb = 0;                          // smoothed drive, dB
    float k = 0, invK = 0;                      // the shaper's input scale and its inverse
    float kTick = 0;                            // 1 - alpha of the 20 ms smoother at fs / kTickSamples
    bool primed = false;                        // false: the next design lands on the target

    void design(const EngineParams& p, const StageCtx& x, float kIn) noexcept FCDSP_NONBLOCKING
    {
        if (!primed)
        {
            kTick = oneMinusAlpha(kSmoothMs, x.fs / static_cast<float>(kTickSamples));
            driveDb = p.driveDb;
            primed = true;
        }
        else
        {
            const float d = p.driveDb - driveDb;
            const float next = driveDb + kTick * d;
            driveDb = (d < kLandDb && d > -kLandDb) || next == driveDb ? p.driveDb : next;
        }
        k = kIn * fcdsp::exp2(driveDb * kLog2PerDb);
        invK = 1.0f / k;
    }
};

// A first-order DC blocker for a voice's residual (an asymmetric shaper makes DC): y = a (y1 + r - r1).
struct DcBlock {
    float r1 = 0, y1 = 0;
    float tick(float a, float r) noexcept FCDSP_NONBLOCKING
    {
        const float y = a * (y1 + r - r1);
        r1 = r;
        y1 = y;
        return y;
    }
};

struct NoPost {
    float operator()(float r) const noexcept FCDSP_NONBLOCKING { return r; }
};

// y = x + post(A(k x) - k x) / k over one channel in place; ch carries the ADAA state (in the shaper's u domain).
template <class S, class Post>
inline void processDriven(const S& shape, const VoiceDrive& d, adaa::Channel& ch, float* x, int n,
                          Post&& post) noexcept FCDSP_NONBLOCKING
{
    constexpr int kBlock = 64;
    alignas(16) float u[kBlock];
    for (int off = 0; off < n; off += kBlock)
    {
        const int m = n - off < kBlock ? n - off : kBlock;
        float* const xs = x + off;
        for (int i = 0; i < m; ++i)
            u[i] = d.k * xs[i];
        adaa::process(shape, ch, u, m);
        for (int i = 0; i < m; ++i)
            xs[i] = xs[i] + post(u[i] - d.k * xs[i]) * d.invK;
    }
}

// The static curve f(k x) / k (the COLOUR view).
template <class S>
inline float transferDriven(const S& shape, const VoiceDrive& d, float x) noexcept FCDSP_NONBLOCKING
{
    return adaa::transfer(shape, d.k * x) * d.invK;
}

} // namespace detail

struct TubeSym {
    static constexpr float kIn = 0.5f;          // [H] shaper input at 0 dB drive per unit of signal

    struct Coeffs { detail::VoiceDrive drive{}; };
    struct State { adaa::Channel ch{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.drive.design(p, x, kIn);
    }
    static void process(const Coeffs& c, State& s, float* x, const float*, int n, int) noexcept FCDSP_NONBLOCKING
    {
        detail::processDriven(adaa::Tanh{}, c.drive, s.ch, x, n, detail::NoPost{});
    }
    static float transfer(const Coeffs& c, float x, float) noexcept FCDSP_NONBLOCKING
    {
        return detail::transferDriven(adaa::Tanh{}, c.drive, x);
    }
    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }
};

static_assert(ColourPolicy<TubeSym>);

} // namespace fcdsp::stage
