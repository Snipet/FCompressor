#pragma once

// stage::SlidingMaxBox: the lookahead limiter's ballistics (01 §5.2 ballistics/ catalogue, §5.3 "Scratch", §10.7
// Brickwall; E §5.4; K2 #21c-d). Feed-forward only. With t the (linked) target GR of each sample, L = `look` (the
// lookahead, EngineParams::lookMs) and R = the attack ramp (EngineParams::atkTauMs, at least one sample), both in
// samples, per lane:
//
//     h[n] = max(t[n - Wmax + 1 .. n])          sliding max, Wmax = max(R, L) + 2
//     b[n] = mean(h[n - L1 + 1 .. n])           box 1, L1 = floor((R + 2) / 2)
//     a[n] = mean(b[n - L2 + 1 .. n])           box 2, L2 = max(1, R + 1 - L1)        (L1 + L2 - 1 = R)
//     r[n] = a[n]                        where a[n] >= r[n - 1]   (the attack is the boxes' ramp)
//     r[n] = r[n-1] + cR (a[n] - r[n-1]) otherwise                (a one-pole release toward a, SmoothBranching's
//                                                                  advance with its sub-ulp carry)
//
// In Brickwall R = L: ATTACK is derived from LOOKAHEAD (01 §10.7), so the resolver makes atkTauMs = lookMs and the
// attack happens exactly inside the lookahead window. They are kept apart only so the engine honours atkTauMs like
// every other policy: a caller that sets a slower attack than the lookahead (dsp.zipper holds every detent render's
// attack at >= 50 ms) gets a slower ramp that completes late, as a compressor's would, instead of an instantaneous one.
//
// Two cascaded boxes of about R/2 (E §5.4: "or two cascaded boxes of L/2 for a smoother onset") instead of one of R:
// the attack is the same length, but its ramp is a piecewise quadratic S-curve with a continuous slope where one box
// gives a linear ramp with a slope step at each end; a slope step in the gain of a pure tone reads as a click on the
// C §5.0 metric (dsp.switch A -> B -> A: +8.4 dB with one 5 ms box as the carried GR ramps to the limiter's own; the
// S-curve reads below the controls). A held step still reaches its target exactly R samples after it enters.
//
// Alignment (01 §5.4 c). The host delays the side chain by L_la - look + D_up - D_tp and the audio by L_la, so the
// target of the audio sample the gain meets at engine time n + L is computed at engine time n. The boxes reach t[n]
// R - 1 = L - 1 samples later and the sliding max keeps every h >= t[n] until n + Wmax - 1 = n + L + 1, so the GR is
// >= t[n] from engine time n + L - 1 to n + L + 1: one base sample either side of the audio it is for. That covers the
// OS-rate gain interpolation between two base samples (EngineHost interpolates the GR linearly in dB) and the up-stage's
// fractional alignment at every Quality (the gain sits 0 / 0.16 / 0.25 base samples off the audio at ECO / STD / HQ),
// so every point of a true-peak report (TruePeak4x.h: [m, m + 3/4]) meets a GR at least its own target. With look = 0
// (budget OFF: zero latency) R = 1: an instantaneous attack on the sample itself, overshoot allowed. A peak is held for
// Wmax samples and then ramps down through the boxes into the one-pole release.
//
// Storage: PrepareInfo::scratch (01 §5.3: 4 x nextPow2(L_la,max + kChunk) floats per arena slot). P = the largest power
// of two <= scratch.size() / 4; per lane k in {0, 1}: G_k (P floats, the sliding max) at k P, and the two box
// histories B1_k, B2_k (P / 2 floats each) at 2P + k P and 2P + k P + P/2: exactly 4P. Only lanes 0-1 are computed;
// lanes 2-3 of every output are copies of lanes 0-1 (the aux lanes carry the channels, Router.h). L and R are clamped
// to P - 4, so Wmax <= P - 2 and L1 <= P/2 - 1 (P >= L_la,max + kChunk: never binding for L with the host's scratch).
// Without scratch (P < 8) the policy has no memory: h = t and a = h (no lookahead, no hold).
//
// The sliding max is the two-stack queue done in place on one ring per lane (amortised O(1), exact): positions
// [s, n] (the "back") hold raw targets and backMax their maximum; positions [u, s) (the "front", u = n - Wmax + 1) hold
// suffix maxima max(raw[j .. s - 1]); h = max(G[u], backMax). When u reaches s the back is turned into a front in one
// backward pass from n to u (O(Wmax) once per ~Wmax samples) and the back empties. u never decreases while `look`
// slews by one sample per tick (a longer window keeps u where it is), so every stored suffix maximum stays valid. A
// jump of `look` (snapParams after a state recall; the host's SC delay jumps too) can move u backwards: the pass is
// then redone from the new u over what the ring holds, whose front values are >= the raw targets they replaced, so h is
// at worst held high for one window (conservative) and exact again after it.
//
// Box sums (K2 #21c). A running sum per box and lane in double, sum += x[n] - x[n - len], re-summed exactly from its
// ring (oldest first, a fixed order: block-size invariant) every kResumSamples = 4096 samples, so the rounding of the
// running difference can never accumulate: once h is 0 across the windows the next re-sums make both sums exactly 0,
// and a below-threshold soak keeps GR exactly 0.0 (dsp.null's soak row). A one-sample change of a box length (a look
// slew) adjusts its sum by the element that joins or leaves; a larger jump re-sums.
//
// `look` (K2 #21d). design() runs on control ticks only; L and R follow their targets by at most one sample per tick
// (so Wmax moves by at most one, and L1 and L2 alternate: each by at most one), at the same absolute ticks where the
// host's SC delay line moves its read position (host/Delay.h: one sample per kTickSamples, at absolute multiples), from
// round(ms fs / 1000 + 1/2) exactly as EngineHost computes it. So the SC read position and the ramp move together and
// moving `look` never makes them disagree by more than the tick in flight. A value-initialised Coeffs (prepare,
// snapParams, the analysis entry points) lands on the targets, as the host's setDelay does on a snap.
//
// Release. SmoothBranching's time smoothing (20 ms in the log domain per tick) and its advance() with the sub-ulp carry
// (slow releases land exactly and track their exponential, dsp.srsweep's long-release row), landing exactly on its
// target once within kLandDb = 1e-6 dB: a one-pole toward 0 dB would otherwise stall where its step flushes to zero
// (about FLT_MIN / (1 - alpha), a GR of ~1e-35 dB that never becomes 0.0; dsp.null's soak row needs 0.0). The rate is
// Coeffs::cR, so CrestAuto<SlidingMaxBox> (TIME MODE AUTO, kEngAutoRelease) swaps in its program-dependent rate per
// sample; tauAMs (the ramp, in ms) and tauRMs are the published times CrestAuto reads.
//
// Seeding (01 §5.5). seed(v) sets the applied GR to v and marks the rings for filling with v (the next tick fills them:
// the State cannot reach the scratch, the Coeffs can), so an engine that takes over holds at least the carried GR for
// one window before it releases; it cannot see peaks already inside the outgoing engine's window (a Carry holds one GR,
// not a history), which the 20 ms crossfade's weight covers. A cold engine seeds 0: every ring 0, the sums exactly 0.
//
// Feedback: none. Brickwall compiles FF only (kTopologies); solveFb returns the static FB curve's root and commitFb
// advances the state as tick does, only so the frozen concept (Stage.h) is met.
//
// Poison: a NaN target propagates to r (the maxima keep NaN, the sums turn NaN), so the host's poison check sees it and
// resets the engine, whose seed refills the rings.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/params/EngineParams.h"
#include <cstddef>
#include <cstdint>

namespace fcdsp::stage {

struct SlidingMaxBox {
    static constexpr int kResumSamples = 4096;      // exact re-sum period of the boxes (K2 #21c)
    static constexpr int kHoldExtra = 2;            // Wmax - max(R, L): one base sample either side (header comment)
    static constexpr uint32_t kMinRing = 8;         // below this the scratch is not used (no lookahead)
    static constexpr float kLandDb = 1e-6f;         // the release lands on its target from this close (header comment)

    struct Coeffs {
        SmoothBranching::Coeffs rel{};              // the release's time smoothing and rate (its attack is unused)
        simd::f32x4 cR{};                           // release rate 1 - alpha (CrestAuto swaps it per sample)
        float tauAMs = 0, tauRMs = 0;               // the published attack (the ramp, ms) and release (ms)
        float* buf = nullptr;                       // PrepareInfo::scratch (header comment "Storage")
        uint32_t ring = 0;                          // P, a power of two (0: no scratch)
        int look = 0;                               // the lookahead in samples, slewed one per tick toward lookTarget
        int lookTarget = 0;
        int ramp = 1;                               // the attack ramp in samples (>= 1), slewed likewise
        int rampTarget = 1;
        float fs = 0;
        bool primed = false;                        // false: the next design() lands on the target
    };

    // One box per lane: its running sum, the length the sum covers.
    struct Box {
        double sum[2]{};
        int len = 0;
    };

    struct State {
        SmoothBranching::State rel{};               // the applied GR r (lanes 0-1, copied to 2-3) and its carry
        simd::f32x4 held{};                         // h, the sliding max (internals: HELD PEAK)
        simd::f32x4 box{};                          // a, the ramp's output
        uint64_t t = 0;                             // the newest ring position (absolute count)
        uint64_t back = 1;                          // s: the first position of the back stack
        uint64_t win = 0;                           // u of the last sample
        Box box1{}, box2{};
        float backMax[2]{};                         // the back stack's maximum per lane (0 when empty)
        float fillValue[2]{};                       // what the rings are filled with (seed)
        int resum = 0;                              // samples to the next exact re-sum
        uint8_t phase[2]{};                         // ControlIo::bits b0-1 per lane
        bool fill = true;                           // fill the rings on the next tick
    };

    // `look` in base samples, as EngineHost's lookSamples computes it (round to nearest; NaN reads as 0).
    static int lookSamples(float lookMs, float fs) noexcept FCDSP_NONBLOCKING
    {
        const float s = lookMs * fs * 0.001f + 0.5f;
        if (!(s >= 1.0f))
            return 0;
        return s < 1.0e6f ? static_cast<int>(s) : 1000000;
    }

    // The largest look (and ramp) the rings hold (header comment "Storage").
    static int maxLook(uint32_t ring) noexcept FCDSP_NONBLOCKING
    {
        return ring >= kMinRing ? static_cast<int>(ring) - 4 : 0;
    }

    // The two box lengths for a ramp R >= 1 (header formula): L1 + L2 - 1 = R.
    static int box1Length(int ramp) noexcept FCDSP_NONBLOCKING { return (ramp + 2) / 2; }
    static int box2Length(int ramp) noexcept FCDSP_NONBLOCKING
    {
        const int l2 = ramp + 1 - box1Length(ramp);
        return l2 > 1 ? l2 : 1;
    }

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        SmoothBranching::design(c.rel, p, x);
        c.cR = c.rel.cR;
        c.tauRMs = c.rel.tauRMs;
        c.fs = x.fs;

        uint32_t ring = 1;
        const std::size_t perRing = x.scratch.size() / 4u;
        while (static_cast<std::size_t>(ring) * 2u <= perRing && ring < (1u << 30))
            ring <<= 1;
        c.ring = perRing >= kMinRing ? ring : 0u;
        c.buf = c.ring != 0 ? x.scratch.data() : nullptr;

        const int cap = maxLook(c.ring);
        const int wantLook = lookSamples(p.lookMs, x.fs), wantRamp = lookSamples(p.atkTauMs, x.fs);
        c.lookTarget = wantLook < cap ? wantLook : cap;
        c.rampTarget = wantRamp < 1 ? 1 : (wantRamp < cap ? wantRamp : cap);
        if (!c.primed)
        {
            c.look = c.lookTarget;
            c.ramp = c.rampTarget;
            c.primed = true;
        }
        else
        {
            if (c.look != c.lookTarget)
                c.look += c.lookTarget > c.look ? 1 : -1;
            if (c.ramp != c.rampTarget)
                c.ramp += c.rampTarget > c.ramp ? 1 : -1;
        }
        c.look = c.look < cap ? c.look : cap;
        c.ramp = c.ramp < 1 ? 1 : (c.ramp < cap ? c.ramp : cap);
        c.tauAMs = x.fs > 0.0f ? static_cast<float>(c.ramp) * 1000.0f / x.fs : 0.0f;
    }

    // The sliding max's window: the longer of the ramp and the lookahead, plus kHoldExtra (header comment).
    static int windowMax(const Coeffs& c) noexcept FCDSP_NONBLOCKING
    {
        return (c.ramp > c.look ? c.ramp : c.look) + kHoldExtra;
    }

    // FF: target GR -> applied GR (header comment).
    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 target) noexcept FCDSP_NONBLOCKING
    {
        float h[2], a[2];
        const float g[2] = { simd::lane<0>(target), simd::lane<1>(target) };
        if (c.buf == nullptr)
        {
            h[0] = a[0] = g[0];
            h[1] = a[1] = g[1];
            s.fill = false;
        }
        else
        {
            if (s.fill)
                fillRings(c, s);
            const uint64_t mask = c.ring - 1u;
            const int ramp = c.ramp;
            const auto wmax = static_cast<uint64_t>(windowMax(c));
            const uint64_t n = ++s.t, p = n & mask;
            const uint64_t u = n - wmax + 1u;
            for (int k = 0; k < 2; ++k)
            {
                float* G = ringG(c, k);
                G[p] = g[k];
                s.backMax[k] = maxNan(s.backMax[k], g[k]);
            }
            if (u < s.win || u >= s.back)       // u moved back (a look jump) or reached the back: a new front from u
            {
                for (int k = 0; k < 2; ++k)
                {
                    float* G = ringG(c, k);
                    for (uint64_t j = n; j > u; --j)
                        G[(j - 1u) & mask] = maxNan(G[(j - 1u) & mask], G[j & mask]);
                    s.backMax[k] = 0.0f;
                }
                s.back = n + 1u;
            }
            s.win = u;
            for (int k = 0; k < 2; ++k)
                h[k] = maxNan(ringG(c, k)[u & mask], s.backMax[k]);

            // the boxes (header comment "Box sums"); both re-sum on the same countdown
            const bool resum = --s.resum <= 0;
            if (resum)
                s.resum = kResumSamples;
            float b[2];
            boxStep(c, s.box1, 0, n, box1Length(ramp), resum, h, b);
            boxStep(c, s.box2, 1, n, box2Length(ramp), resum, b, a);
        }

        alignas(16) const float hv[4] = { h[0], h[1], h[0], h[1] };
        alignas(16) const float av[4] = { a[0], a[1], a[0], a[1] };
        s.held = simd::load(hv);
        s.box = simd::load(av);
        release(c, s, s.box);
        return s.rel.r;
    }

    // FB (never compiled for Brickwall, header comment): the static FB curve's root, no state.
    template <class Solve>
    static simd::f32x4 solveFb(const Coeffs&, const State&, Solve&& solve) noexcept FCDSP_NONBLOCKING
    {
        return solve(FbAffine{ simd::set1(0.0f), simd::set1(1.0f) });
    }

    static void commitFb(const Coeffs& c, State& s, simd::f32x4 r) noexcept FCDSP_NONBLOCKING { (void) tick(c, s, r); }

    // From Carry::grDb: the applied GR continues at v and the rings fill with it on the next tick (header comment).
    static void seed(State& s, simd::f32x4 grDb) noexcept FCDSP_NONBLOCKING
    {
        const float v0 = simd::lane<0>(grDb), v1 = simd::lane<1>(grDb);
        s.fillValue[0] = v0 > 0.0f ? v0 : 0.0f;
        s.fillValue[1] = v1 > 0.0f ? v1 : 0.0f;
        alignas(16) const float v[4] = { s.fillValue[0], s.fillValue[1], s.fillValue[0], s.fillValue[1] };
        SmoothBranching::seed(s.rel, simd::load(v));
        s.held = s.rel.r;
        s.box = s.rel.r;
        s.phase[0] = s.phase[1] = 0;
        s.fill = true;
    }

    static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.rel.r; }

    static simd::f32x4 attackNowMs(const Coeffs& c, const State&) noexcept FCDSP_NONBLOCKING
    {
        return simd::set1(c.tauAMs);
    }
    static simd::f32x4 releaseNowMs(const Coeffs& c, const State&) noexcept FCDSP_NONBLOCKING
    {
        return simd::set1(c.tauRMs);
    }

    // ControlIo::bits b0-1 of the lane with the larger GR (lanes 0-1): 1 attack (rising), 2 hold (above 0 and still),
    // 3 release (falling), 0 idle.
    static uint8_t status(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return simd::lane<0>(s.rel.r) >= simd::lane<1>(s.rel.r) ? s.phase[0] : s.phase[1];
    }

    // The sliding max's current output, per lane (Brickwall's HELD PEAK).
    static simd::f32x4 heldDb(const State& s) noexcept FCDSP_NONBLOCKING { return s.held; }

    // The lookahead the window currently runs, ms (Brickwall's LOOK EFF): L, slewing toward the target.
    static float lookNowMs(const Coeffs& c) noexcept FCDSP_NONBLOCKING
    {
        return c.fs > 0.0f ? static_cast<float>(c.look) * 1000.0f / c.fs : 0.0f;
    }

private:
    static float* ringG(const Coeffs& c, int k) noexcept FCDSP_NONBLOCKING
    {
        return c.buf + static_cast<std::size_t>(k) * c.ring;
    }
    // Box `which` (0, 1) of lane k: P / 2 floats.
    static float* ringB(const Coeffs& c, int which, int k) noexcept FCDSP_NONBLOCKING
    {
        return c.buf + 2u * static_cast<std::size_t>(c.ring) + static_cast<std::size_t>(k) * c.ring
             + static_cast<std::size_t>(which) * (c.ring / 2u);
    }

    // max that keeps a NaN from either operand (the poison check must see it, header comment).
    static float maxNan(float a, float b) noexcept FCDSP_NONBLOCKING
    {
        if (b != b)
            return b;
        return a > b || a != a ? a : b;
    }

    // One sample of one box for both lanes: x in, the mean over its last `len` inputs out (header comment).
    static void boxStep(const Coeffs& c, Box& bx, int which, uint64_t n, int len, bool resum, const float x[2],
                        float out[2]) noexcept FCDSP_NONBLOCKING
    {
        const uint64_t mask = c.ring / 2u - 1u, q = n & mask;
        const int d = len - bx.len;
        for (int k = 0; k < 2; ++k)
        {
            float* R = ringB(c, which, k);
            R[q] = x[k];
            if (resum || d > 1 || d < -1)
                bx.sum[k] = sumRing(R, mask, n, len);
            else
            {
                double sum = bx.sum[k] + static_cast<double>(x[k]);
                if (d <= 0)                                             // the element that leaves (length kept)
                    sum -= static_cast<double>(R[(n - static_cast<uint64_t>(bx.len)) & mask]);
                if (d < 0)                                              // one more leaves (the length shrank by one)
                    sum -= static_cast<double>(R[(n - static_cast<uint64_t>(bx.len) + 1u) & mask]);
                bx.sum[k] = sum;
            }
            out[k] = static_cast<float>(bx.sum[k] / static_cast<double>(len));
        }
        bx.len = len;
    }

    // The exact sum of the last `len` values up to position n, oldest first (block-size invariant).
    static double sumRing(const float* R, uint64_t mask, uint64_t n, int len) noexcept FCDSP_NONBLOCKING
    {
        double sum = 0.0;
        for (uint64_t j = n - static_cast<uint64_t>(len) + 1u; j <= n; ++j)
            sum += static_cast<double>(R[j & mask]);
        return sum;
    }

    // Every ring position holds the seed value: the front is the whole G ring (valid suffix maxima of a constant), the
    // back is empty, and both boxes are re-summed.
    static void fillRings(const Coeffs& c, State& s) noexcept FCDSP_NONBLOCKING
    {
        for (int k = 0; k < 2; ++k)
        {
            float* G = ringG(c, k);
            for (uint32_t i = 0; i < c.ring; ++i)
                G[i] = s.fillValue[k];
            for (int which = 0; which < 2; ++which)
            {
                float* R = ringB(c, which, k);
                for (uint32_t i = 0; i < c.ring / 2u; ++i)
                    R[i] = s.fillValue[k];
            }
            s.backMax[k] = 0.0f;
        }
        s.t = static_cast<uint64_t>(c.ring);
        s.back = s.t + 1u;
        s.win = s.t - static_cast<uint64_t>(windowMax(c)) + 1u;
        const uint64_t mask = c.ring / 2u - 1u;
        const int lens[2] = { box1Length(c.ramp), box2Length(c.ramp) };
        Box* boxes[2] = { &s.box1, &s.box2 };
        for (int which = 0; which < 2; ++which)
        {
            for (int k = 0; k < 2; ++k)
                boxes[which]->sum[k] = sumRing(ringB(c, which, k), mask, s.t, lens[which]);
            boxes[which]->len = lens[which];
        }
        s.resum = kResumSamples;
        s.fill = false;
    }

    // r = a where a >= r (the ramp is the attack), else SmoothBranching's release step toward a with its carry.
    static void release(const Coeffs& c, State& s, simd::f32x4 a) noexcept FCDSP_NONBLOCKING
    {
        const float before[2] = { simd::lane<0>(s.rel.r), simd::lane<1>(s.rel.r) };
        const simd::m32x4 up = simd::ge(a, s.rel.r);
        SmoothBranching::State fall = s.rel;
        SmoothBranching::advance(fall, c.cR, a);
        const simd::f32x4 one = simd::set1(1.0f), zero = simd::set1(0.0f);
        const simd::m32x4 landed = simd::gt(simd::set1(kLandDb), simd::sub(fall.r, a));   // within kLandDb: land
        fall.r = simd::sel(landed, a, fall.r);
        fall.lo = simd::sel(landed, zero, fall.lo);
        s.rel.r = simd::sel(up, a, fall.r);
        s.rel.lo = simd::sel(up, zero, fall.lo);
        s.rel.atk = simd::sel(simd::gt(s.rel.r, zero), simd::sel(up, one, zero), zero);
        s.rel.r = simd::add(s.rel.r, simd::sub(a, a));                  // + 0, or NaN for a NaN (or inf) ramp
        const float after[2] = { simd::lane<0>(s.rel.r), simd::lane<1>(s.rel.r) };
        for (int k = 0; k < 2; ++k)
            s.phase[k] = after[k] > before[k] ? uint8_t{1}
                       : after[k] < before[k] ? uint8_t{3}
                       : after[k] > 0.0f      ? uint8_t{2}
                                              : uint8_t{0};
    }
};

static_assert(BallisticsPolicy<SlidingMaxBox>);

} // namespace fcdsp::stage
