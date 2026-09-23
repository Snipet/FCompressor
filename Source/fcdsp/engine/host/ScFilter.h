#pragma once

// The host's side-chain filters (01 §5.4 step 2b; E §8, §9.2 row 8; K2 #14): a 2-pole Butterworth TPT state-variable
// high-pass (`schpf`, EngineParams::scHpfHz) followed by the `sce` tilt (EngineParams::sceDbOct), six first-order
// sections pivoted at 1 kHz. A pure component with one owner (K3 #12: F5); EngineHost.cpp (F7) runs it once per chunk
// on the linear side chain {c0, c1, aux0, aux1} before the lookahead delay, the path preGain and the stmode encode
// (both filters are linear and the same in every lane, so filtering L/R and then encoding equals encoding and then
// filtering). analysis::scResponse (F8) draws responseDb() times the Mode's own ScShape.
//
// HPF (E §8; Zavalishin, Simper): g = tan(pi fc / fs), k = sqrt(2), a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2;
//     v3 = v0 - ic2;  v1 = a1 ic1 + a2 v3;  v2 = ic2 + a2 ic1 + a3 v3;  ic1 = 2 v1 - ic1;  ic2 = 2 v2 - ic2
//     hp = v0 - k v1 - v2
// The bilinear transform with the cutoff prewarped (g = FastMath tanPi, no libm: K2 #14), so the digital -3.01 dB point
// is exactly fc at every rate; stable under per-sample coefficient changes. OFF (scHpfHz < kScHpfOffHz; the resolver
// writes 0) is an EXACT bypass: the output is the input bit for bit (HR's exact-neutral rule), and the state is cleared,
// so switching on starts from rest (a high-pass from rest passes its input and then removes the low end: no burst).
//
// Tilt (E §8): sigma dB/oct over 20 Hz-20 kHz, 0 dB at 1 kHz, from N = 6 first-order shelving sections, two per decade
// (rho = sqrt(10)), section i centred at f_i = 20 Hz * rho^(i + 1/2) (the half-decade cells of 20 Hz-20 kHz), zero and
// pole at f_i / s and f_i * s with s = rho^(q / 2), q = sigma / (20 log10 2): pole/zero = rho^q = 10^(Delta / 20) with
// Delta = sigma log2(rho) (E's formula; the Oustaloup placement; sigma < 0 swaps poles and zeros). Every corner is
// prewarped (g = tanPi(f / fs), clamped to f <= kScMaxCornerX fs), so each section is the bilinear transform of
// (s + gz) / (s + gp): a TPT one-pole on gp with y = x + (gz / gp - 1) lp. The cascade is scaled so |H(1 kHz)| = 1
// exactly (the digital magnitude, one more tanPi). Choices where E §8 is silent (F5):
//   - sigma is slewed on control ticks at kTiltSlewDbOctPerS (6 dB/oct in 20 ms), toward its target; a snap jumps. A
//     tilt that switches on from rest (states 0) at its full sigma answers the running signal with its HF gain, about
//     +/- sigma log2(fs / 2 kHz) dB (+26 dB at 6 dB/oct) until the low sections charge: a burst into the detector. From
//     sigma = 0 the first tick's step is tiny, and the sections follow the slew;
//   - sigma = 0 (landed) is an EXACT bypass with the state cleared, so a Mode without `sce` pays nothing;
//   - sigma is clamped to +/- kTiltMaxDbOct (the host range is +/- 6; a Mode's physical() may map its own scale).
// Accuracy (dsp.sc): the digital response matches this design within 0.001 dB. Against the ideal line sigma log2(f /
// 1 kHz), six sections at two per decade hold E §8's +/- 0.1 dB over 200 Hz-2.5 kHz (0.05 dB at 6 dB/oct, 48 kHz);
// towards the band edges the cascade runs out of sections (the tails of the missing ones) and the bilinear warp
// compresses the top: at 6 dB/oct 1.1 dB over 40 Hz-10 kHz and 3 dB at 20 Hz; at 20 kHz 6 dB (48 kHz) or 10.5 dB
// (44.1 kHz). The deviation scales with sigma. For a detector weighting this is the design's shape, not a defect;
// analysis::scResponse draws the true response (responseDb), never the ideal line.
//
// Coefficients are designed on control ticks at ABSOLUTE sample-index multiples of kTickSamples (01 §5.1 "per tick:
// the SC filter coefficients (host, via tanPi)"), and only when a target or the slewed sigma moved, so the output does
// not depend on the host block size. setTarget() per block; snap() (configure, requestSnap) designs at once.
//
// Real time: header-inline, allocation-free, FCDSP_NONBLOCKING throughout; prepare() is too (it only stores the rate).

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace fcdsp::host {

inline constexpr float kScHpfOffHz = 20.0f;              // scHpfHz below this is OFF (the resolver writes 0, 01 §4.4)
inline constexpr float kScSvfK = 1.41421356f;            // 1/Q of the 2-pole Butterworth
inline constexpr int   kTiltSections = 6;
inline constexpr float kTiltPivotHz = 1000.0f;           // 0 dB here at every sigma
inline constexpr std::array<float, kTiltSections> kTiltCentreHz{   // 20 Hz * sqrt(10)^(i + 1/2)
    35.5655882f, 112.468265f, 355.655882f, 1124.68265f, 3556.55882f, 11246.8265f };
inline constexpr float kTiltLog2SpreadPerDbOct = 0.137940078f;  // log2(s) per dB/oct: log2(sqrt 10) / (2 * 20 log10 2)
inline constexpr float kTiltMaxDbOct = 12.0f;
inline constexpr float kTiltSlewDbOctPerS = 300.0f;      // 6 dB/oct per 20 ms (the host's transition time)
inline constexpr float kScMaxCornerX = 0.49f;            // corners clamped to 0.49 fs (tanPi's domain is [0, 0.499])

// One design of both filters, as control ticks compute it (and analysis evaluates it).
struct ScDesign {
    float fs = 0.0f;
    float hpfHz = 0.0f;                                  // 0 = OFF
    float tiltDbOct = 0.0f;                              // the (slewed) sigma this design is for
    bool  hpf = false, tilt = false;                     // engaged; false = exact bypass
    float svfG = 0.0f, svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f;
    std::array<float, kTiltSections> gp{}, gz{};         // warped pole and zero of each section (tan(pi f / fs))
    std::array<float, kTiltSections> a{}, c{};           // one-pole gain gp / (1 + gp); c = gz / gp - 1
    float gain = 1.0f;                                   // pivot normalisation: |H(1 kHz)| = 1
};

namespace sc {

inline bool finiteF(float v) noexcept FCDSP_NONBLOCKING
{
    return (std::bit_cast<uint32_t>(v) & 0x7f800000u) != 0x7f800000u;
}

// tan(pi f / fs) with f / fs clamped to [0, kScMaxCornerX].
inline float warp(float hz, float fs) noexcept FCDSP_NONBLOCKING
{
    float x = hz / fs;
    x = x > 0.0f ? (x < kScMaxCornerX ? x : kScMaxCornerX) : 0.0f;       // NaN -> 0
    return tanPi(x);
}

// sigma clamped to +/- kTiltMaxDbOct; NaN and +-inf read as 0 (no tilt).
inline float clampTilt(float dbOct) noexcept FCDSP_NONBLOCKING
{
    if (!finiteF(dbOct))
        return 0.0f;
    return dbOct > -kTiltMaxDbOct ? (dbOct < kTiltMaxDbOct ? dbOct : kTiltMaxDbOct) : -kTiltMaxDbOct;
}

inline bool hpfOn(float hz) noexcept FCDSP_NONBLOCKING { return hz >= kScHpfOffHz; }     // NaN -> off

} // namespace sc

// Fills the HPF half of `d` for cutoff `hz` (OFF below kScHpfOffHz) at rate fs.
inline void designHpf(ScDesign& d, float hz, float fs) noexcept FCDSP_NONBLOCKING
{
    d.fs = fs;
    d.hpf = sc::hpfOn(hz) && fs > 0.0f;
    d.hpfHz = d.hpf ? hz : 0.0f;
    if (!d.hpf)
    {
        d.svfG = d.svfA1 = d.svfA2 = d.svfA3 = 0.0f;
        return;
    }
    const float g = sc::warp(hz, fs);
    d.svfG = g;
    d.svfA1 = 1.0f / (1.0f + g * (g + kScSvfK));
    d.svfA2 = g * d.svfA1;
    d.svfA3 = g * d.svfA2;
}

// Fills the tilt half of `d` for sigma dB/oct at rate fs. sigma == 0 designs the identity (tilt = false, the bypass).
inline void designTilt(ScDesign& d, float dbOct, float fs) noexcept FCDSP_NONBLOCKING
{
    d.fs = fs;
    const float sigma = sc::clampTilt(dbOct);
    d.tiltDbOct = sigma;
    d.tilt = sigma != 0.0f && fs > 0.0f;
    const float s = fcdsp::exp2(sigma * kTiltLog2SpreadPerDbOct);
    const float w1 = sc::warp(kTiltPivotHz, fs), w1sq = w1 * w1;
    float pivotSq = 1.0f;                                                 // |H(1 kHz)|^2 of the unscaled cascade
    for (int i = 0; i < kTiltSections; ++i)
    {
        const auto u = static_cast<std::size_t>(i);
        const float gp = sc::warp(kTiltCentreHz[u] * s, fs);
        const float gz = sc::warp(kTiltCentreHz[u] / s, fs);
        d.gp[u] = gp;
        d.gz[u] = gz;
        d.a[u] = gp / (1.0f + gp);
        d.c[u] = gz / gp - 1.0f;
        pivotSq *= (w1sq + gz * gz) / (w1sq + gp * gp);
    }
    d.gain = d.tilt ? 1.0f / simd::lane<0>(simd::sqrt(simd::set1(pivotSq))) : 1.0f;
}

// |H(hz)|^2 of a design: the HPF (bilinear Butterworth) times the scaled tilt cascade; 1 where a filter is bypassed.
inline float powerGain(const ScDesign& d, float hz) noexcept FCDSP_NONBLOCKING
{
    float x = hz / d.fs;
    x = x > 0.0f ? (x < 0.499f ? x : 0.499f) : 0.0f;
    const float w = tanPi(x), wsq = w * w;
    float p = 1.0f;
    if (d.hpf)
    {
        const float gsq = d.svfG * d.svfG, e = gsq - wsq, kgw = kScSvfK * d.svfG * w;
        p = (wsq * wsq) / (e * e + kgw * kgw);
    }
    if (d.tilt)
    {
        float t = d.gain * d.gain;
        for (int i = 0; i < kTiltSections; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            t *= (wsq + d.gz[u] * d.gz[u]) / (wsq + d.gp[u] * d.gp[u]);
        }
        p *= t;
    }
    return p;
}

// The design's magnitude in dB at hz (floored at -240 dB): analysis::scResponse's host part.
inline float magDb(const ScDesign& d, float hz) noexcept FCDSP_NONBLOCKING
{
    return simd::lane<0>(dbFromMs(simd::set1(powerGain(d, hz))));
}

class ScFilter {
public:
    // The rate, then reset() and snap(). Allocation-free (configure calls it; so may a probe).
    void prepare(float fs) noexcept FCDSP_NONBLOCKING
    {
        fs_ = fs > 0.0f ? fs : 48000.0f;
        reset();
        snap();
    }

    // Silence: every state 0 and the control ticker restarted (the next process() sample ticks). Keeps the targets
    // and the current design.
    void reset() noexcept FCDSP_NONBLOCKING
    {
        ic1_ = ic2_ = simd::set1(0.0f);
        s_.fill(simd::set1(0.0f));
        tick_ = ControlTicker{};
    }

    // Per block: EngineParams::scHpfHz (0 = OFF) and ::sceDbOct. They apply at the next control tick.
    void setTarget(float hpfHz, float tiltDbOct) noexcept FCDSP_NONBLOCKING
    {
        hpfTgt_ = hpfHz;
        tiltTgt_ = sc::clampTilt(tiltDbOct);
    }

    // The slewed sigma jumps to its target and both filters are designed now (configure, state recall).
    void snap() noexcept FCDSP_NONBLOCKING
    {
        tiltCur_ = tiltTgt_;
        redesign();
    }

    // Filters n samples in place or out of place (out may alias in); sampleIndex is the absolute index of in[0].
    void process(const simd::f32x4* in, simd::f32x4* out, int n, uint64_t sampleIndex) noexcept FCDSP_NONBLOCKING
    {
        for (int i = 0; i < n; ++i)
        {
            if (tick_.advance(sampleIndex + static_cast<uint64_t>(i)))
                onTick();
            simd::f32x4 v = in[i];
            if (d_.hpf)
                v = hpf(v);
            if (d_.tilt)
                v = tilt(v);
            out[i] = v;
        }
    }

    // Poison check (01 §5.8): every filter state is finite.
    bool finite() const noexcept FCDSP_NONBLOCKING
    {
        bool ok = finiteLanes(ic1_) && finiteLanes(ic2_);
        for (const simd::f32x4& s : s_)
            ok = ok && finiteLanes(s);
        return ok;
    }

    const ScDesign& design() const noexcept FCDSP_NONBLOCKING { return d_; }
    float hpfTarget() const noexcept FCDSP_NONBLOCKING { return hpfTgt_; }
    float tiltTarget() const noexcept FCDSP_NONBLOCKING { return tiltTgt_; }
    float tiltNow() const noexcept FCDSP_NONBLOCKING { return tiltCur_; }

    // The static response of the TARGET design at hz, in dB (analysis; the settled filter).
    static float responseDb(float hpfHz, float tiltDbOct, float fs, float hz) noexcept FCDSP_NONBLOCKING
    {
        ScDesign d;
        designHpf(d, hpfHz, fs);
        designTilt(d, tiltDbOct, fs);
        return magDb(d, hz);
    }

private:
    static bool finiteLanes(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        return sc::finiteF(simd::lane<0>(v)) && sc::finiteF(simd::lane<1>(v)) && sc::finiteF(simd::lane<2>(v))
            && sc::finiteF(simd::lane<3>(v));
    }

    // The tilt runs while its slewed sigma or its target is non-zero; the bypass needs both at 0 (landed).
    bool tiltEngaged() const noexcept FCDSP_NONBLOCKING { return tiltCur_ != 0.0f || tiltTgt_ != 0.0f; }

    // A control tick: slew sigma one step toward its target, then redesign if anything moved.
    void onTick() noexcept FCDSP_NONBLOCKING
    {
        if (tiltCur_ != tiltTgt_)
        {
            const float step = kTiltSlewDbOctPerS * static_cast<float>(kTickSamples) / fs_;
            const float gap = tiltTgt_ - tiltCur_;
            tiltCur_ = gap > step ? tiltCur_ + step : (gap < -step ? tiltCur_ - step : tiltTgt_);
        }
        const float hz = sc::hpfOn(hpfTgt_) ? hpfTgt_ : 0.0f;
        if (hz != d_.hpfHz || tiltCur_ != d_.tiltDbOct || tiltEngaged() != d_.tilt)
            redesign();
    }

    // Designs both filters from the targets (the HPF) and the slewed sigma. A filter that switches off clears its
    // state (its bypass is exact and the next switch-on starts from rest). A sigma passing through 0 while it slews
    // toward a non-zero target stays engaged (identity sections: c = 0, gain 1; the states run on).
    void redesign() noexcept FCDSP_NONBLOCKING
    {
        const bool hpfWas = d_.hpf, tiltWas = d_.tilt;
        designHpf(d_, hpfTgt_, fs_);
        designTilt(d_, tiltCur_, fs_);
        d_.tilt = d_.tilt || (tiltEngaged() && d_.fs > 0.0f);
        if (hpfWas && !d_.hpf)
            ic1_ = ic2_ = simd::set1(0.0f);
        if (tiltWas && !d_.tilt)
            s_.fill(simd::set1(0.0f));
    }

    simd::f32x4 hpf(simd::f32x4 v0) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 a1 = simd::set1(d_.svfA1), a2 = simd::set1(d_.svfA2), a3 = simd::set1(d_.svfA3);
        const simd::f32x4 v3 = simd::sub(v0, ic2_);
        const simd::f32x4 v1 = simd::fma(simd::mul(a1, ic1_), a2, v3);                     // a1 ic1 + a2 v3
        const simd::f32x4 v2 = simd::fma(simd::fma(ic2_, a2, ic1_), a3, v3);               // ic2 + a2 ic1 + a3 v3
        ic1_ = simd::sub(simd::add(v1, v1), ic1_);
        ic2_ = simd::sub(simd::add(v2, v2), ic2_);
        return simd::sub(simd::fms(v0, simd::set1(kScSvfK), v1), v2);                    // v0 - k v1 - v2
    }

    simd::f32x4 tilt(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
    {
        simd::f32x4 y = x;
        for (int i = 0; i < kTiltSections; ++i)
        {
            const auto u = static_cast<std::size_t>(i);
            const simd::f32x4 v = simd::mul(simd::sub(y, s_[u]), simd::set1(d_.a[u]));    // TPT one-pole
            const simd::f32x4 lp = simd::add(v, s_[u]);
            s_[u] = simd::add(lp, v);
            y = simd::fma(y, simd::set1(d_.c[u]), lp);                                      // hp + (gz/gp) lp
        }
        return simd::mul(y, simd::set1(d_.gain));
    }

    ScDesign d_{};
    float fs_ = 48000.0f;
    float hpfTgt_ = 0.0f, tiltTgt_ = 0.0f, tiltCur_ = 0.0f;
    simd::f32x4 ic1_{}, ic2_{};
    std::array<simd::f32x4, kTiltSections> s_{};
    ControlTicker tick_{};
};

} // namespace fcdsp::host
