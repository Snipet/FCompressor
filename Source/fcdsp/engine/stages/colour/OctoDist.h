#pragma once

// stage::OctoDistT<kKind, kHp>: the hybrid VCA's AUDIO button (01 §5.2 colour/ catalogue; Octo; D §2.7 [V S14]: "VCA →
// Distortion Generator"; Audio Norm / HP / Dist2 / Dist2+HP / Dist3 / Dist3+HP). Octo's Colour is
// ColourSelect<OctoDistT<0, false>, <0, true>, <1, false>, <1, true>, <2, false>, <2, true>>, one per AUDIO position.
//
// The distortion generator is a driven static voice (TubeSym.h, detail::processDriven: level-compensated, only the
// residual through ADAA-1), after the gain element:
//
//     y = x + (A(k x) - k x) / k,   k = kIn[kind] * 10^(DRIVE / 20)
//
//   kind 0  CLEAN   adaa::SoftClip (odd, the "warming circuits"), kIn 0.03: THD about .02 % at +6 dBFS, and the
//                   fundamental within 0.06 dB up to +18 dBFS, so the default voice keeps meter truth at any INPUT
//   kind 1  DIST 2  adaa::AsymTanh (b = 0.35): the 2nd harmonic dominates; kIn 2.5; the residual passes a 10 Hz DC
//                   blocker (an asymmetric shaper makes DC; DiodeAsym.h)
//   kind 2  DIST 3  adaa::Tanh (odd): the 3rd dominates, "more similar to tape"; kIn 3
//
// The unit's harmonics grow "especially while compressing" (D §2.7): INPUT drives both the fixed threshold and this
// generator, so the harder it is pushed the more it compresses and the more it distorts. A -18 dBFS (0 VU) sine carries
// about 1 % H3 in DIST 3 and a comparable H2 in DIST 2; a 0 dBFS one tens of per cent (the "Redline" range); DRIVE (an
// extension) adds to it. The voices are static (colourStatic = true), so the COLOUR view and the describing-function
// curve dsp.static checks are exactly f(k x) / k.
//
// kHp adds the audio high-pass after the generator: a third-order Butterworth, -3 dB at kHpHz = 65 Hz, 18 dB/oct (D §2.7
// [V S14]: "3 dB down at 65Hz ... Its final slope is 18 dB per octave"): a TPT one-pole high-pass and a TPT SVF
// high-pass (Q = 1) at the same corner, at the OS rate. It has memory, so the COLOUR view (transfer) is the generator's
// static curve only; at 1 kHz the high-pass is -0.0002 dB. Constants and their reasons: docs/modes/octo.md.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/colour/Adaa.h"
#include "fcdsp/engine/stages/colour/TubeSym.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

template <int kKind, bool kHp>
struct OctoDistT {
    static_assert(kKind >= 0 && kKind <= 2, "OctoDistT: kind 0 CLEAN, 1 DIST 2, 2 DIST 3");

    static constexpr float kInByKind[3] = { 0.03f, 2.5f, 3.0f };    // [H] shaper input per unit of signal
    static constexpr float kBias = 0.35f;                          // [H] DIST 2's asymmetry
    static constexpr float kDcHz = 10.0f;                          // [H] DIST 2's residual DC blocker
    static constexpr float kHpHz = 65.0f;                          // AUDIO HP: -3 dB at 65 Hz (D §2.7 [V S14])
    static constexpr float kTwoPi = 6.28318531f;
    static constexpr float kMaxTurns = 0.499f;

    struct Coeffs {
        detail::VoiceDrive drive{};
        adaa::AsymTanh asym{};
        float dcA = 0;                          // DIST 2's DC blocker pole
        float hpG1 = 0;                         // one-pole: g / (1 + g), g = tan(pi fc / fsOs)
        float hpG = 0, hpD = 0;                 // SVF: g and 1 / (1 + g (k + g)), k = 1 / Q = 1
    };
    struct State {
        adaa::Channel ch{};
        detail::DcBlock dc{};
        float z1 = 0;                           // one-pole HP integrator
        float s1 = 0, s2 = 0;                   // SVF integrators
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        c.drive.design(p, x, kInByKind[kKind]);
        const float fsOs = x.fsOs > 0.0f ? x.fsOs : x.fs;
        if constexpr (kKind == 1)
        {
            c.asym = adaa::AsymTanh::make(kBias);
            c.dcA = 1.0f - oneMinusAlpha(1000.0f / (kTwoPi * kDcHz), fsOs);
        }
        if constexpr (kHp)
        {
            const float t = fsOs > 0.0f ? (kHpHz / fsOs < kMaxTurns ? kHpHz / fsOs : kMaxTurns) : 0.0f;
            const float g = fcdsp::tanPi(t);
            c.hpG1 = g / (1.0f + g);
            c.hpG = g;
            c.hpD = 1.0f / (1.0f + g * (1.0f + g));
        }
    }

    static void process(const Coeffs& c, State& s, float* x, const float*, int n, int) noexcept FCDSP_NONBLOCKING
    {
        if (n <= 0)
            return;
        if constexpr (kKind == 0)
            detail::processDriven(adaa::SoftClip{}, c.drive, s.ch, x, n, detail::NoPost{});
        else if constexpr (kKind == 1)
        {
            detail::DcBlock& dc = s.dc;
            const float a = c.dcA;
            detail::processDriven(c.asym, c.drive, s.ch, x, n,
                                  [&dc, a](float r) noexcept FCDSP_NONBLOCKING { return dc.tick(a, r); });
        }
        else
            detail::processDriven(adaa::Tanh{}, c.drive, s.ch, x, n, detail::NoPost{});
        if constexpr (kHp)
        {
            for (int i = 0; i < n; ++i)
            {
                // one-pole TPT high-pass
                const float v = (x[i] - s.z1) * c.hpG1;
                const float lp = v + s.z1;
                s.z1 = lp + v;
                const float h1 = x[i] - lp;
                // SVF TPT high-pass, Q = 1 (k = 1)
                const float hp = (h1 - (1.0f + c.hpG) * s.s1 - s.s2) * c.hpD;
                const float bp = c.hpG * hp + s.s1;
                const float lp2 = c.hpG * bp + s.s2;
                s.s1 = c.hpG * hp + bp;
                s.s2 = c.hpG * bp + lp2;
                x[i] = hp;
            }
        }
    }

    // The generator's static curve (the COLOUR view): f(k x) / k.
    static float transfer(const Coeffs& c, float x, float) noexcept FCDSP_NONBLOCKING
    {
        if constexpr (kKind == 0)
            return detail::transferDriven(adaa::SoftClip{}, c.drive, x);
        else if constexpr (kKind == 1)
            return detail::transferDriven(c.asym, c.drive, x);
        else
            return detail::transferDriven(adaa::Tanh{}, c.drive, x);
    }

    static void reset(State& s) noexcept FCDSP_NONBLOCKING { s = State{}; }
};

static_assert(ColourPolicy<OctoDistT<0, false>> && ColourPolicy<OctoDistT<1, true>> && ColourPolicy<OctoDistT<2, false>>);

} // namespace fcdsp::stage
