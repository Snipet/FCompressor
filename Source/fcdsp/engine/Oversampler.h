#pragma once

// Latency, oversampling and quality (01 §5.6; E §2.9, §5.2-5.3; K2 #11). fcdsp owns its oversampler (JUCE-free, so
// probes run the shipped code; constexpr coefficients, bit-exact across arches):
//   ECO  no OS; colour runs ADAA-1 on the nonlinear RESIDUAL at base rate
//   STD  2x polyphase allpass IIR halfband + a constexpr first-order Thiran allpass at base rate inside down(), so the
//        up->down group delay equals kStdLatency within 0.01 samples up to 1 kHz (K2 #11a)
//   HQ   4x = two cascaded linear-phase FIR halfbands, integer delay by construction
// Latency depends only on the global setup: latency = lookaheadSamples(budget, fs) + kOs[quality].latency. It never
// depends on the Mode. dUp is the integer base-rate delay of the up stage; the host SC delay adds it (01 §5.4 c).
//
// F6 (S2, the oversampler spike) owns this header after S0: it added the class's private members and the bodies
// (Oversampler.cpp) and set the four latency VALUES below, which freeze at FZ2 and are v1-forever.
//
// ==== The F6 design (S2); the coefficient tables and their compile-time checks are in Oversampler.cpp ===============
// Targets: the up->down round trip flat within 0.01 dB to 20 kHz at 44.1 kHz (01 §5.6 mix-0 rule), image rejection
// (up) and alias rejection (down) at least juce::dsp::Oversampling max quality's (proc.osref, ADR-60), latency
// STD <= 4 and HQ <= 64. Frequencies below are normalised to the stage's OUTPUT (up) or INPUT (down) rate.
//   STD up    elliptic halfband, 8 allpass coefficients (4 + 4 sections), transition 0.046 (passband to 0.227 =
//             20.0 kHz at 44.1 kHz): 103.7 dB stopband; group delay at DC 1.837 base samples -> kStdUpDelay = 2
//   STD down  elliptic halfband, 7 coefficients (4 + 3 sections), transition 0.046: 90.8 dB stopband
//             (JUCE max quality: up 7 coefficients / 0.05 / 93.4 dB, down 6 / 0.06 / 85.3 dB, 3.137 samples)
//   STD Thiran  first-order allpass at base rate, delay 0.5365: the round trip's 3.4585 samples at DC become 3.9950,
//             centred so the group delay stays within 4 +- 0.0050 samples from DC to 1 kHz at 44.1 kHz (the IIRs'
//             delay rises 0.010 samples by 1 kHz, so a DC-exact Thiran would sit on the 0.01 bound). tau_g at 10 kHz:
//             5.227 samples at 44.1 kHz, 5.000 at 48 kHz (reported by dsp.os). kStdLatency = 4
//   HQ stage 1  equiripple FIR halfbands at 2x, passband to 20 kHz at 44.1 kHz (0.22676), stopband from 0.27324:
//             up 123 taps (k = 31, 95.5 dB), down 103 taps (k = 26, 82.1 dB); JUCE: 119 taps / 92.6 dB from 0.275,
//             79 taps / 74.1 dB from 0.28
//   HQ stage 2  equiripple FIR halfbands at 4x, passband to 0.13662 (24.1 kHz at 44.1 kHz, stage 1's stopband edge),
//             stopband from 0.36338: up 23 taps (k = 6, 94.3 dB), down 19 taps (k = 5, 79.9 dB); JUCE: 51 taps /
//             74.7 dB from 0.30, 35 taps / 60.7 dB from 0.31
//   HQ latency  the centre delays M = 2k - 1 give (61 + 51)/2 + (11 + 9)/4 = 61 base samples: an integer by the choice
//             of lengths and exact (linear phase), = JUCE's integer-latency figure. Up stage: 61/2 + 11/4 = 33.25
//             -> kHqUpDelay = 33 (rounding misaligns the SC by 0.25 base samples, STD's by 0.16; dsp.time allows 1.5)
// Channels run in f32x4 lanes (1..4 channels, one vector op per sample for all of them); state is fixed-size, so
// configure() allocates nothing in this implementation. Every arithmetic step is an fcdsp::simd op in a fixed order,
// so the output is bit-identical on arm64 and x86-64.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/params/Setup.h"
#include <cmath>

namespace fcdsp {

// F6 values (S2), frozen at FZ2 and v1-forever from then (targets: STD <= 4, HQ <= 64; E's measurements of JUCE's
// equivalents: 4 = 3.14 + Thiran, and 61).
inline constexpr int kStdLatency = 4;       // STD up->down delay, base-rate samples
inline constexpr int kStdUpDelay = 2;       // STD up-stage delay, base-rate samples
inline constexpr int kHqLatency  = 61;      // HQ up->down delay, base-rate samples
inline constexpr int kHqUpDelay  = 33;      // HQ up-stage delay, base-rate samples

struct OsDesign { int factor; int latency; int dUp; };   // latency, dUp: BASE-rate samples, integer, constexpr
inline constexpr OsDesign kOs[3] = {        // indexed by Quality
    { 1, 0, 0 },                            // ECO
    { 2, kStdLatency, kStdUpDelay },        // STD
    { 4, kHqLatency, kHqUpDelay },          // HQ
};

// Real time (FZ0 errata, R-F0 #1): reset, up and down run on the audio thread and are FCDSP_NONBLOCKING; F6's
// out-of-line definitions repeat the macro. configure allocates and is never called from the audio thread.
//
// F6 contract details (01 §5.6 leaves them open):
//   - configure(q, maxBaseBlock, channels) selects the Quality, takes channels in [1, 4] (clamped) and resets the
//     state. The filters stream sample by sample, so no block buffer depends on maxBaseBlock and nothing is allocated.
//   - up(in, n, osOut) reads n base samples per channel and writes n * factor OS samples per channel; it returns
//     n * factor (n <= 0 writes nothing and returns 0). osOut must not alias in unless the Quality is ECO.
//   - down(osIn, nOs, out) consumes nOs / factor whole base frames (a remainder is ignored) and writes that many
//     samples per channel. out may alias osIn.
//   - A default-constructed Oversampler is ECO with 0 channels: up returns n and writes nothing.
class Oversampler {
public:
    void configure(Quality, int maxBaseBlock, int channels);            // allocates
    void reset() noexcept FCDSP_NONBLOCKING;
    int  up(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING;   // returns n*factor;
                                                                                              //   ONCE per chunk
    void down(const float* const* osIn, int nOs, float* const* out) noexcept FCDSP_NONBLOCKING;

private:
    // Design sizes (tables in Oversampler.cpp). IIR: allpass coefficients per filter. FIR: a halfband with k distinct
    // taps per side has N = 4k - 1 taps and its centre at M = 2k - 1 (odd).
    static constexpr int kStdUpCoefs = 8, kStdDownCoefs = 7;
    static constexpr int kHq1UpK = 31, kHq1DownK = 26, kHq2UpK = 6, kHq2DownK = 5;

    // A doubled ring of f32x4 frames: every push writes slot pos and pos + W, so the last W frames are contiguous;
    // push returns a pointer p to the new frame, and p[-j] is the frame pushed j pushes earlier (0 <= j < W).
    // W is a power of two.
    template <int W>
    struct Ring {
        static_assert(W > 0 && (W & (W - 1)) == 0, "Ring: W must be a power of two");
        simd::f32x4 v[2 * W];
        int pos;
        const simd::f32x4* push(simd::f32x4 x) noexcept FCDSP_NONBLOCKING
        {
            v[pos] = x;
            v[pos + W] = x;
            const simd::f32x4* newest = &v[pos + W];
            pos = (pos + 1) & (W - 1);
            return newest;
        }
    };

    // STD: allpass section states (s = x - a*y) of the up and down filters, the down filter's odd-branch delay and the
    // Thiran state.
    struct StdState {
        simd::f32x4 up[kStdUpCoefs];
        simd::f32x4 down[kStdDownCoefs];
        simd::f32x4 oddDelay;
        simd::f32x4 thiran;
    };
    // HQ: input windows of the four halfbands (sizes: the next power of two above 2k frames; the decimators keep their
    // odd phase, a pure delay of k frames, in a ring of its own).
    struct HqState {
        Ring<64> up1;                       // base rate, 2k = 62 frames
        Ring<16> up2;                       // 2x, 12
        Ring<16> down2Even;                 // 2x frames of even 4x samples, 10
        Ring<8>  down2Odd;                  // odd 4x samples, k + 1 = 6
        Ring<64> down1Even;                 // base frames of even 2x samples, 52
        Ring<32> down1Odd;                  // odd 2x samples, k + 1 = 27
    };
    static_assert(2 * kHq1UpK <= 64 && 2 * kHq2UpK <= 16 && 2 * kHq2DownK <= 16 && kHq2DownK + 1 <= 8 &&
                  2 * kHq1DownK <= 64 && kHq1DownK + 1 <= 32, "every HQ ring holds its window");

    template <int C> void upStd(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING;
    template <int C> void upHq(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING;
    template <int C> void downStd(const float* const* osIn, int n, float* const* out) noexcept FCDSP_NONBLOCKING;
    template <int C> void downHq(const float* const* osIn, int n, float* const* out) noexcept FCDSP_NONBLOCKING;

    Quality quality_ = Quality::eco;
    int channels_ = 0;
    StdState std_{};
    HqState hq_{};
};

inline int lookaheadSamples(LookaheadBudget b, double fs) noexcept FCDSP_NONBLOCKING {   // ceil(ms*fs/1000); 0 = off
    return b == LookaheadBudget::off ? 0 : static_cast<int>(std::ceil(static_cast<double>(budgetMs(b)) * fs / 1000.0));
}

} // namespace fcdsp
