#pragma once

// stage::Bright: Clean's BRIGHT voice, a clean-then-crisp symmetric saturation (01 §5.2 colour/ catalogue; 01 §10.3
// Clean voice BRIGHT; D §2.8 Pro-C 3 character Bright; E §2.9). A driven static voice (TubeSym.h,
// detail::processDriven) with the C1 cubic soft clipper of Adaa.h:
//
//     f(u) = u - 4 u^3 / 27 for |u| <= 3/2, +-1 beyond,    kIn = 3/4 [H]
//
// Below its knee it adds less 3rd harmonic than TUBE's tanh (u^2 / 27 against u^2 / 12 relative), then the corner at
// |u| = 3/2 (a jump in f'') spreads the distortion into the upper odd harmonics (their amplitudes fall as 1/n^3 instead
// of tanh's exponential decay): clean at moderate levels, bright and crisp when driven. It is memoryless like every
// Clean voice (TubeSym.h), so the COLOUR view and dsp.static's describing-function check hold exactly; a frequency-
// dependent "presence" voice would need a colourCurve contract of its own (docs/modes/clean.md).

#include "fcdsp/core/Rt.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct Bright {
    static constexpr float kIn = 0.75f;         // [H] shaper input at 0 dB drive per unit of signal

    struct Coeffs { detail::VoiceDrive drive{}; };
    struct State { adaa::Channel ch{}; };

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

static_assert(ColourPolicy<Bright>);

} // namespace fcdsp::stage
