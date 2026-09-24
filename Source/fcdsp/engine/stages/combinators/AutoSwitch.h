#pragma once

// stage::AutoSwitch<A, B, Select>: a switch position that swaps the ballistics (01 §5.2 combinators: "AutoSwitch<A, B>
// (the tag picks B and seeds B from A)"; 01 §10.2: AUTO positions on a hardware release switch are `rel` steps tagged
// kTagAuto / kTagAuto2; 01 §10.4 Bus G: AutoSwitch<SmoothBranching, DualRelease>). A runs on the manual positions, B on
// the AUTO ones. The release switch is not part of the kernel key (01 §5.5), so the swap happens inside one engine:
//
//   design    selects B where Select::useB(p) holds (default: an AUTO tag, kTagAuto | kTagAuto2, is active; a Mode
//             whose tags are shared by two Pids, ADR-64, passes its own Select reading m[]) and designs the SELECTED
//             path only. When the selection changes, the newly selected path is designed from a value-initialised
//             Coeffs, so it LANDS on its targets (the ModeEngine convention) instead of gliding in from the times it
//             last had; the other path keeps the coefficients it ran with while it fades out (designing it with the
//             new parameters would retune it mid-fade: DualRelease's times are 0 off AUTO, and its release sped up
//             tick by tick inside the blend, a +20 dB click on the detent-edge row).
//   switch    a live change of the selection hands over and crossfades. The path that was not running is seeded from
//             the running one's GR (B::seed(A::grDb), or back), both run, and the applied GR is their blend
//             (1 - S(w)) r_A + S(w) r_B, w moving linearly over kBlendMs = 20 ms and S the host's smootherstep
//             (host/Ramps.h, 01 §5.1: every change ramps 20 ms). A hard hand-over keeps the GR continuous but not its
//             slope: leaving AUTO while its slow path holds the GR (flat) for a manual release that is attacking toward
//             a waveform peak put a corner in the gain at the edge, +4.4 dB on dsp.zipper's detent-edge click row
//             (limit +3). The blend is evaluated as a + S(w)(b - a) over its first half and b + S(1 - w)(a - b) over
//             the second (host::blend's order, exact at both ends). A reversal mid-way reuses both running paths.
//             FF: the blend starts at the first tick() after the switching control tick. FB: solveFb sees a const
//             State and cannot seed, so the hand-over happens after that sample's commitFb and the blend starts on the
//             next sample; during it both paths solve, the blend of their roots is the loop's GR, and both commit the
//             linked value (as each commits a linked value that is not its own root).
//   seed      (a Mode or kernel hand-over, Carry::grDb) seeds BOTH paths, so whichever runs continues from the carry.
//             A fresh state (reset, value-initialised: no blend position yet) starts on the selected path at once.
// grDb is the applied (blended) GR; the telemetry times and status() are those of the path with the larger weight.
// With the selection constant, AutoSwitch is bit-identical to the running path alone (dsp.dualrelease autoswitch rows).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/host/Ramps.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include <cstdint>

namespace fcdsp::stage {

// The default selector: an AUTO switch position is active (EngineParams::tags, the OR of the active step tags).
struct AutoTagSelect {
    static bool useB(const EngineParams& p) noexcept FCDSP_NONBLOCKING
    {
        return (p.tags & static_cast<uint32_t>(kTagAuto | kTagAuto2)) != 0;
    }
};

template <class A, class B, class Select = AutoTagSelect>
struct AutoSwitch {
    static_assert(BallisticsPolicy<A> && BallisticsPolicy<B>, "AutoSwitch: A and B must be BallisticsPolicy");
    static constexpr float kBlendMs = 20.0f;    // the hand-over crossfade (01 §5.1: every change ramps 20 ms)
    static constexpr float kUnset = -1.0f;      // State::wB of a fresh state: start on the selected path, no blend

    struct Coeffs {
        typename A::Coeffs a{};
        typename B::Coeffs b{};
        float step = 1.0f;                      // blend position per sample, 1000 / (kBlendMs fs)
        bool useB = false;                      // the selection of the last design()
    };
    struct State {
        typename A::State a{};
        typename B::State b{};
        float wB = kUnset;                      // linear blend position: 0 = A alone, 1 = B alone, between: both run
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        const bool useB = Select::useB(p);
        if (useB)
        {
            if (!c.useB)
                c.b = typename B::Coeffs{};
            B::design(c.b, p, x);
        }
        else
        {
            if (c.useB)
                c.a = typename A::Coeffs{};
            A::design(c.a, p, x);
        }
        const float perSample = x.fs > 0.0f ? 1000.0f / (kBlendMs * x.fs) : 1.0f;
        c.step = perSample > 0.0f && perSample < 1.0f ? perSample : 1.0f;     // NaN or a ramp under a sample: instant
        c.useB = useB;
    }

    // FF: the running path, or both blended while a switch fades (header comment).
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 t) noexcept FCDSP_NONBLOCKING
    {
        const float goal = c.useB ? 1.0f : 0.0f;
        if (s.wB < 0.0f)
            s.wB = goal;
        if (s.wB == goal)
            return goal == 1.0f ? B::tick(c.b, s.b, t) : A::tick(c.a, s.a, t);
        begin(s);
        s.wB = advance(s.wB, goal, c.step);
        const simd::f32x4 ra = A::tick(c.a, s.a, t);
        const simd::f32x4 rb = B::tick(c.b, s.b, t);
        return mix(ra, rb, s.wB);
    }

    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs& c, const State& s, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        const float w = s.wB < 0.0f ? (c.useB ? 1.0f : 0.0f) : s.wB;
        if (w == 0.0f)
            return A::solveFb(c.a, s.a, solve);
        if (w == 1.0f)
            return B::solveFb(c.b, s.b, solve);
        return mix(A::solveFb(c.a, s.a, solve), B::solveFb(c.b, s.b, solve), w);
    }

    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING
    {
        const float goal = c.useB ? 1.0f : 0.0f;
        if (s.wB < 0.0f)
            s.wB = goal;
        if (s.wB != 1.0f)                       // A ran this sample
            A::commitFb(c.a, s.a, r);
        if (s.wB != 0.0f)                       // B ran this sample
            B::commitFb(c.b, s.b, r);
        if (s.wB != goal)
        {
            begin(s);
            s.wB = advance(s.wB, goal, c.step);
        }
    }

    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        A::seed(s.a, grDb);
        B::seed(s.b, grDb);
    }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        if (s.wB <= 0.0f)
            return A::grDb(s.a);
        if (s.wB >= 1.0f)
            return B::grDb(s.b);
        return mix(A::grDb(s.a), B::grDb(s.b), s.wB);
    }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return onB(c, s) ? B::attackNowMs(c.b, s.b) : A::attackNowMs(c.a, s.a);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return onB(c, s) ? B::releaseNowMs(c.b, s.b) : A::releaseNowMs(c.a, s.a);
    }

    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return s.wB >= 0.5f ? B::status(s.b) : A::status(s.a);
    }

    // True where B carries the GR (weight >= 1/2; a fresh state: the selection).
    static bool onB(const Coeffs& c, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return s.wB < 0.0f ? c.useB : s.wB >= 0.5f;
    }

private:
    // A switch leaves an end of the blend: the path that was not running starts from the running one's GR.
    static void begin(State& s) noexcept FCDSP_NONBLOCKING
    {
        if (s.wB == 0.0f)
            B::seed(s.b, A::grDb(s.a));
        else if (s.wB == 1.0f)
            A::seed(s.a, B::grDb(s.b));
    }

    static float advance(float w, float goal, float step) noexcept FCDSP_NONBLOCKING
    {
        if (w < goal)
        {
            const float up = w + step;
            return up < goal ? up : goal;
        }
        const float down = w - step;
        return down > goal ? down : goal;
    }

    // (1 - S(w)) a + S(w) b, exactly a at w = 0 and b at w = 1, in host::blend's evaluation order.
    static simd::f32x4 mix(simd::f32x4 a, simd::f32x4 b, float w) noexcept FCDSP_NONBLOCKING
    {
        if (w <= 0.0f)
            return a;
        if (w >= 1.0f)
            return b;
        return w <= 0.5f ? simd::fma(a, simd::set1(host::rampShape(w)), simd::sub(b, a))
                         : simd::fma(b, simd::set1(host::rampShape(1.0f - w)), simd::sub(a, b));
    }
};

} // namespace fcdsp::stage
