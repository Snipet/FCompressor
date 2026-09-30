#pragma once

// stage::VcaBus: Bus G's VCA + CONSOLE voice (01 §5.2 colour/ catalogue; 01 §10.4 Bus G VOICE VCA; D §2.4 "Coloration.
// Low. VCA plus console bus summing"; E §2.9). A driven static voice (TubeSym.h: detail::processDriven, level-
// compensated, y = x + (A(k x) - k x) / k, A = f through Adaa.h's ADAA-1 residual scheme) with the C1 cubic soft
// clipper of Adaa.h:
//
//     f(u) = u - 4 u^3 / 27 for |u| <= 3/2, +-1 beyond,    k = kIn * 10^(DRIVE / 20),    kIn = 1/10 [H]
//
// The shape is the solid-state bus amplifier's: clean and odd (the console's VCAs are symmetry-trimmed and its bus
// amplifiers push-pull, so the second harmonic nulls), a pure third harmonic that grows with the square of the level,
// then the rail corner at |u| = 3/2, which DRIVE reaches (+12 dB puts it at +11.5 dBFS). kIn sets the
// level at the console's published-class distortion: at 0 dB drive a 0 dBFS sine carries H3 at -68.6 dB (0.04 %), a
// -18 dBFS (0 VU) one at -104.6 dB; +12 dB of drive raises H3 by 24 dB. It also keeps the default voice inside the
// meter-truth budget the probes hold a modelled Mode to (docs/modes/bus-g.md): the fundamental of a +6 dBFS sine
// moves by 0.038 dB (y / x of a +2 dBFS square by 0.020 dB, against 0.05 dB), and 20 dB below the default threshold's
// knee (-41 dBFS) the colour residual is -140 dB (the modelled below-threshold null asks -120 dB).
//
// Memoryless, so the COLOUR view (colourCurve, f(k x) / k) and the describing-function static curve are exact
// (colourStatic = true: the shape does not depend on GR). DRIVE is smoothed per control tick (20 ms, in dB,
// detail::VoiceDrive). Constants and their reasons: docs/modes/bus-g.md.

#include "fcdsp/core/Rt.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct VcaBus {
    static constexpr float kIn = 0.1f;          // [H] shaper input at 0 dB drive per unit of signal

    struct Coeffs { detail::VoiceDrive drive{}; };
    struct State { detail::DrivenChannel ch{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.drive.design(p, x, kIn);
    }
    static void process(const Coeffs& c, State& s, float* x, const float*, int n, int) noexcept FCDSP_NONBLOCKING
    {
        detail::processDriven(adaa::SoftClip{}, c.drive, s.ch, x, n, detail::NoPost{});
    }
    static float transfer(const Coeffs& c, float x, float) noexcept FCDSP_NONBLOCKING
    {
        return detail::transferDriven(adaa::SoftClip{}, c.drive, x);
    }
    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }
};

static_assert(ColourPolicy<VcaBus>);

} // namespace fcdsp::stage
