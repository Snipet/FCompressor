#pragma once

// stage::DiodeAsym: Clean's DIODE voice, an asymmetric soft saturation (even and odd harmonics) (01 §5.2 colour/
// catalogue; 01 §10.3 Clean voice DIODE; D §2.8 Pro-C 3 character Diode; E §2.9). A driven static voice (TubeSym.h,
// detail::processDriven) with the biased tanh of Adaa.h:
//
//     f(u) = (tanh(u + b) - tanh b) / (1 - tanh^2 b),   b = kBias = 1/4 [H],   kIn = 1/2 [H]
//
// f(0) = 0 and f'(0) = 1, so small signals pass at unity; the bias bends positive and negative half-waves differently
// (2nd harmonic about 0.12 u relative, plus the odd series). An asymmetric shaper also makes DC (about -tanh(b) u^2 / 2
// for a sine of amplitude u), so the residual A(k x) - k x passes a first-order DC blocker at kDcHz = 10 Hz [H] (at the
// OS rate) before it is added back: the output carries no offset, and at 1 kHz the blocker's gain is 1 - 5e-5 (the
// fundamental's describing-function gain that dsp.static checks is unaffected). The static transfer (the COLOUR view)
// is f(k x) / k itself.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct DiodeAsym {
    static constexpr float kIn = 0.5f;          // [H] shaper input at 0 dB drive per unit of signal
    static constexpr float kBias = 0.25f;       // [H] the diode's asymmetry (AsymTanh bias)
    static constexpr float kDcHz = 10.0f;       // [H] residual DC blocker corner
    static constexpr float kTwoPi = 6.28318531f;

    struct Coeffs {
        detail::VoiceDrive drive{};
        adaa::AsymTanh shape{};
        float dcA = 0;                          // DC blocker pole, e^(-2 pi fc / fsOs)
    };
    struct State {
        adaa::Channel ch{};
        detail::DcBlock dc{};
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.drive.design(p, x, kIn);
        c.shape = adaa::AsymTanh::make(kBias);                    // two FastMath calls per control tick
        const float fsOs = x.fsOs > 0.0f ? x.fsOs : x.fs;
        c.dcA = 1.0f - oneMinusAlpha(1000.0f / (kTwoPi * kDcHz), fsOs);
    }
    static void process(const Coeffs& c, State& s, float* x, const float*, int n, int) noexcept FCDSP_NONBLOCKING
    {
        detail::DcBlock& dc = s.dc;
        const float a = c.dcA;
        detail::processDriven(c.shape, c.drive, s.ch, x, n,
                              [&dc, a](float r) noexcept FCDSP_NONBLOCKING { return dc.tick(a, r); });
    }
    static float transfer(const Coeffs& c, float x, float) noexcept FCDSP_NONBLOCKING
    {
        return detail::transferDriven(c.shape, c.drive, x);
    }
    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }
};

static_assert(ColourPolicy<DiodeAsym>);

} // namespace fcdsp::stage
