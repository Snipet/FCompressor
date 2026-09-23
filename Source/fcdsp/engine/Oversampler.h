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
// F6 (S2, the oversampler spike) owns this header after S0: it adds the class's members and bodies and replaces the
// four latency VALUES below, which then freeze at FZ2 and are v1-forever (targets: STD <= 4, HQ <= 64).

#include "fcdsp/core/Rt.h"
#include "fcdsp/params/Setup.h"
#include <cmath>

namespace fcdsp {

// PLACEHOLDERS until F6 measures its filters (E's measurements of JUCE's equivalents: 4 = 3.14 + Thiran, and 61).
inline constexpr int kStdLatency = 4;       // STD up->down delay, base-rate samples
inline constexpr int kStdUpDelay = 2;       // STD up-stage delay, base-rate samples
inline constexpr int kHqLatency  = 64;      // HQ up->down delay, base-rate samples
inline constexpr int kHqUpDelay  = 32;      // HQ up-stage delay, base-rate samples

struct OsDesign { int factor; int latency; int dUp; };   // latency, dUp: BASE-rate samples, integer, constexpr
inline constexpr OsDesign kOs[3] = {        // indexed by Quality
    { 1, 0, 0 },                            // ECO
    { 2, kStdLatency, kStdUpDelay },        // STD
    { 4, kHqLatency, kHqUpDelay },          // HQ
};

// Real time (FZ0 errata, R-F0 #1): reset, up and down run on the audio thread and are FCDSP_NONBLOCKING; F6's
// out-of-line definitions repeat the macro. configure allocates and is never called from the audio thread.
class Oversampler {
public:
    void configure(Quality, int maxBaseBlock, int channels);            // allocates
    void reset() noexcept FCDSP_NONBLOCKING;
    int  up(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING;   // returns n*factor;
                                                                                              //   ONCE per chunk
    void down(const float* const* osIn, int nOs, float* const* out) noexcept FCDSP_NONBLOCKING;
};

inline int lookaheadSamples(LookaheadBudget b, double fs) noexcept FCDSP_NONBLOCKING {   // ceil(ms*fs/1000); 0 = off
    return b == LookaheadBudget::off ? 0 : static_cast<int>(std::ceil(static_cast<double>(budgetMs(b)) * fs / 1000.0));
}

} // namespace fcdsp
