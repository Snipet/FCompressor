#pragma once

// stage::LoudClip: Brickwall's VOICE LOUD, a soft-clip stage in front of the ceiling (01 §5.2 colour/ catalogue, §10.7
// Brickwall "voice: CLEAN / LOUD (soft-clip pre-stage)"; D §5.6 M30 "Loud (soft-clip pre-stage)"; E §2.9). It runs where
// every colour stage runs, on the wet signal after the gain element and before the makeup (here the CEILING), at the OS
// rate (ECO: base rate), so it never feeds the control path and the limiter's GR is VOICE CLEAN's bit for bit.
//
// The shaper is Adaa.h's C1 cubic soft clipper f(u) = u - 4u^3/27 (|u| <= 3/2), +-1 beyond, through the driven static
// voice of TubeSym.h (level-compensated: y = x + (A(k x) - k x) / k, unity small-signal gain, ADAA-1 on the residual
// only), with its corner placed on the limiter's output level:
//
//     k = kSat / 10^(T / 20),   kSat = 3/2,   T = EngineParams::thrDb (the level the gain element holds peaks to)
//
// so a peak the limiter holds at its threshold drives the shaper exactly to its corner (u = 3/2, f = 1, f' = 0): the top
// of the program is rounded (-0.33 dB at T - 6 dB, -1.39 dB at T, before the makeup) and everything under it is left
// nearly linear. The Mode then adds kHeadroomDb to its makeup in LOUD (BrickwallDesc.cpp), which makes the whole program
// louder at the same ceiling:
//
//     kHeadroomDb = 20 log10(4/3) - 0.25 dB = 2.249 dB                                                            [H]
//
//   - 20 log10(4/3): where the ceiling must sit. The clipped waveform's own peak is 1/k (T - 3.52 dB), but its true peak
//     after band-limiting is not: a tone whose harmonics the band limit removes (the down-sampler at HQ / STD, Nyquist at
//     ECO) keeps only its fundamental, f's describing function N(A) A = A - A^3/9 = 1.125 at A = 3/2, i.e. 1.02 dB over
//     the clipped peak. With 20 log10(1.5 / 1.125) = 20 log10(4/3) of makeup that fundamental lands exactly on the ceiling,
//     so a single tone of any frequency stays under it; a low tone, whose harmonics survive, peaks 1.02 dB lower.
//   - 0.25 dB: fitted margin for broadband program, where the cubic's in-band intermodulation adds to the fundamental
//     bound: dsp.slidingmax's stress program (band-limited noise bursts, drum hits) read +0.21 dB over the single-tone
//     bound at HQ; with the margin its worst segment is under the ceiling (the HQ LOUD rows).
//
// Safety clip. ADAA-1 on the residual returns y = Q + (x[n] - x[n-1]) / 2 (Adaa.h), the mean of f plus half the step, so
// a fast signal driven into the corner overshoots the static bound 1/k by up to half a sample's step (at base rate, near
// Nyquist, several dB). A hard clip at +-1/k follows the shaper, so no output sample (at the OS rate; ECO: the output
// itself) exceeds 1/k: in LOUD, OS-rate samples never pass ceiling - 1.27 dB. It acts only on that overshoot (a static,
// slow signal never reaches it), so its own aliasing is second order.
//
// T moves (threshold automation): the clip level is smoothed per control tick like a voice's drive (a 20 ms one-pole in
// dB at fs / kTickSamples, landing within 1e-4 dB; a value-initialised Coeffs lands at once), while ModeEngine smooths
// the gain computer's T per sample and the host smooths the makeup (20 ms): the three move together within a tick.
// Memoryless and static (colourStatic): transfer() is f(k x) / k, which the COLOUR view and dsp.static's describing-
// function curve read. VOICE CLEAN is ColourNone (ColourSelect<ColourNone, LoudClip>).

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct LoudClip {
    static constexpr float kSat = 1.5f;                     // SoftClip's corner: f(3/2) = 1, f'(3/2) = 0
    static constexpr float kHeadroomDb = 2.24877473f;       // 20 log10(4/3) - 0.25: the makeup LOUD adds (header comment)
    static constexpr float kSmoothMs = 20.0f;               // the clip level's per-tick smoother (a voice's drive's)
    static constexpr float kLandDb = 1e-4f;

    struct Coeffs {
        detail::VoiceDrive drive{};                         // k and 1/k for detail::processDriven (drive unused)
        float levelDb = 0;                                  // the smoothed clip level T (dB)
        float kTick = 0;
        bool primed = false;
    };
    struct State { adaa::Channel ch{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const float target = p.thrDb;
        if (!c.primed)
        {
            c.kTick = oneMinusAlpha(kSmoothMs, x.fs / static_cast<float>(kTickSamples));
            c.levelDb = target;
            c.primed = true;
        }
        else
        {
            const float d = target - c.levelDb;
            const float next = c.levelDb + c.kTick * d;
            c.levelDb = (d < kLandDb && d > -kLandDb) || next == c.levelDb ? target : next;
        }
        c.drive.k = kSat * fcdsp::exp2(-c.levelDb * kLog2PerDb);
        c.drive.invK = 1.0f / c.drive.k;
    }

    static void process(const Coeffs& c, State& s, float* x, const float*, int n, int) noexcept FCDSP_NONBLOCKING
    {
        detail::processDriven(adaa::SoftClip{}, c.drive, s.ch, x, n, detail::NoPost{});
        const float b = c.drive.invK;                           // the safety clip (header comment): |y| <= 1/k
        for (int i = 0; i < n; ++i)
            x[i] = x[i] > b ? b : (x[i] < -b ? -b : x[i]);      // NaN compares false and passes (the poison check)
    }

    static float transfer(const Coeffs& c, float x, float) noexcept FCDSP_NONBLOCKING
    {
        return detail::transferDriven(adaa::SoftClip{}, c.drive, x);
    }

    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }
};

static_assert(ColourPolicy<LoudClip>);

} // namespace fcdsp::stage
