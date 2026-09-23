// Oversampler.cpp: fcdsp's oversampler (01 §5.6; E §2.9, §5.2-5.3; K2 #11, #12; ADR-16). The design and the reasons
// for its constants are in Oversampler.h; this file holds the constexpr coefficient tables, the compile-time checks
// that tie them to kStdLatency / kStdUpDelay / kHqLatency / kHqUpDelay, and the bodies.
//
// Coefficients were designed offline in long double (F6 handoff): the IIR allpass coefficients by the elliptic
// halfband formula of de Soras' HIIR (the method juce::dsp::FilterDesign uses too), the FIR halfbands by a Remez
// exchange on cos((2i - 1)w) over the passband, then rounded to float. They are literals, so every build and every
// architecture runs the same numbers (no libm at configure time either).
//
// Structure (lanes = channels throughout):
//   STD up    x -> A0(z) -> OS sample 2n, x -> A1(z) -> OS sample 2n + 1: the polyphase form of
//             H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2 with the interpolation gain 2; each A is a cascade of
//             (a + z^-1)/(1 + a z^-1)
//   STD down  y[n] = (A0(u[2n]) + A1(u[2n - 1])) / 2, then the Thiran allpass (t + z^-1)/(1 + t z^-1) at base rate
//   HQ up     per halfband: even output 2 sum c_i (x[n - k + i] + x[n - k - i + 1]), odd output x[n - k + 1] (the
//             centre tap 1/2 times the gain 2: an exact copy); stage 1 then stage 2 per stage-1 output
//   HQ down   per halfband: y[n] = sum c_i (e[n - k + i] + e[n - k - i + 1]) + o[n - k] / 2 over the even (e) and
//             odd (o) input phases; stage 2 per pair of 4x samples, then stage 1 per pair of stage-2 outputs
// One allpass section: y = s + a*x, s' = x - a*y (two fused steps). FIR sums run from the outermost (smallest) tap
// inward into four interleaved accumulators. Everything is an fcdsp::simd op in a fixed order, so arm64 and x86-64
// agree bit for bit.
#include "fcdsp/engine/Oversampler.h"

#include <iterator>

namespace fcdsp {

namespace {

// ---- STD: elliptic polyphase IIR halfbands (HIIR / juce::dsp::FilterDesign formula) ---------------------------------
// Coefficient i goes to branch i % 2 (branch 1 carries the z^-1). Up: 8 coefficients, transition 0.046 (103.7 dB);
// down: 7 coefficients, transition 0.046 (90.8 dB).
constexpr float kStdUp[8] = { 0.037588615f, 0.140128195f, 0.282692254f, 0.437978715f,
                              0.58623451f,  0.718596816f, 0.835636675f, 0.944698513f };
constexpr float kStdDown[7] = { 0.0479508936f, 0.175418988f, 0.344622284f, 0.518737912f,
                                0.676369727f,  0.813182116f, 0.937449634f };

// The Thiran section's design delay D (base samples) and its coefficient t = (1 - D) / (1 + D). D is not the DC-exact
// 4 - 3.458515 = 0.541485 but 0.005016 less: the round trip's group delay rises by 0.010 samples from DC to 1 kHz at
// 44.1 kHz, and centring the error keeps it within +-0.0050 there (K2 #11a asks for 0.01).
constexpr double kStdThiranDelay = 0.536469113;
constexpr float kStdThiran = static_cast<float>((1.0 - kStdThiranDelay) / (1.0 + kStdThiranDelay));

// Group delay at DC, in samples of the rate the allpasses run at, of one branch: sum of (1 - a) / (1 + a) per section.
template <int N>
constexpr double branchDelay(const float (&a)[N], int branch) noexcept
{
    double d = 0.0;
    for (int i = branch; i < N; i += 2)
        d += (1.0 - static_cast<double>(a[i])) / (1.0 + static_cast<double>(a[i]));
    return d;
}
// The halfband's group delay at DC in OS samples: the mean of the two branches' (each section delays 2(1-a)/(1+a) OS
// samples; branch 1 adds z^-1). In base samples it is half that.
template <int N>
constexpr double halfbandDelayOs(const float (&a)[N]) noexcept
{
    return 0.5 * (2.0 * branchDelay(a, 0) + 1.0 + 2.0 * branchDelay(a, 1));
}
constexpr double kStdUpDelayExact = 0.5 * halfbandDelayOs(kStdUp);                                     // 1.8372
constexpr double kStdRoundTripDc = 0.5 * (halfbandDelayOs(kStdUp) + halfbandDelayOs(kStdDown))         // 3.4585
                                   + (1.0 - static_cast<double>(kStdThiran)) / (1.0 + static_cast<double>(kStdThiran));
constexpr double absd(double x) noexcept { return x < 0.0 ? -x : x; }
static_assert(absd(kStdRoundTripDc - kStdLatency) < 0.0051 && kStdRoundTripDc < kStdLatency,
              "STD: the Thiran section must bring the DC group delay to kStdLatency - 0.0050");
static_assert(absd(kStdUpDelayExact - kStdUpDelay) <= 0.5, "STD: kStdUpDelay is the up stage's DC delay, rounded");
static_assert(kStdThiranDelay > 0.5 && kStdThiranDelay < 1.5, "a first-order Thiran behaves for D in (0.5, 1.5)");

// ---- HQ: equiripple FIR halfbands (Remez) ------------------------------------------------------------------------
// c[i - 1] is the tap at +-(2i - 1) from the centre; the centre tap is 1/2 and the other even offsets are 0.
// Stage 1 (2x): passband to 0.22676 (20 kHz at 44.1 kHz), stopband from 0.27324. Up k = 31 (95.5 dB), down k = 26
// (82.1 dB).
constexpr float kHq1Up[31] = {
    0.317970902f,    -0.105090179f,    0.061986234f,     -0.0431533419f,
    0.0324303359f,   -0.0254141074f,   0.020414371f,     -0.0166445374f,
    0.0136895645f,   -0.0113098677f,   0.00935728941f,   -0.00773491152f,
    0.00637619384f,  -0.00523341354f,  0.00427093823f,   -0.00346114789f,
    0.002781888f,    -0.00221484574f,  0.00174449629f,   -0.00135741406f,
    0.00104182027f,  -0.000787283934f, 0.000584525696f,  -0.000425285078f,
    0.000302229833f, -0.000208888712f, 0.000139597498f,  -8.94502809e-05f,
    5.42513408e-05f, -3.04650548e-05f, 1.88540835e-05f };
constexpr float kHq1Down[26] = {
    0.317913353f,    -0.104918882f,    0.0617050491f,    -0.0427685864f,
    0.0319505446f,   -0.0248497725f,   0.0197775885f,    -0.0159486216f,
    0.0129486294f,   -0.0105383983f,   0.00856970809f,   -0.00694517465f,
    0.00559741119f,  -0.00447752373f,  0.0035484354f,    -0.00278087612f,
    0.00215090578f,  -0.00163835927f,  0.00122586405f,   -0.000898214057f,
    0.000641975377f, -0.00044523555f,  0.00029744429f,   -0.000189307044f,
    0.000112707923f, -7.99063928e-05f };
// Stage 2 (4x): passband to 0.13662 (24.1 kHz at 44.1 kHz: stage 1's stopband edge), stopband from 0.36338. Up k = 6
// (94.3 dB), down k = 5 (79.9 dB).
constexpr float kHq2Up[6] = { 0.310168207f, -0.083803162f, 0.032515537f, -0.011524383f, 0.00312938914f,
                              -0.000495288055f };
constexpr float kHq2Down[5] = { 0.308674604f, -0.0800852701f, 0.0283051133f, -0.00834762584f, 0.00150364684f };

// DC gain 1/2 + 2 sum c_i is 1 within the passband ripple (a transcription check on every table).
template <int K>
constexpr bool unityAtDc(const float (&c)[K], double ripple) noexcept
{
    double s = 0.5;
    for (int i = 0; i < K; ++i)
        s += 2.0 * static_cast<double>(c[i]);
    return absd(s - 1.0) <= ripple;
}
static_assert(unityAtDc(kHq1Up, 1.7e-5) && unityAtDc(kHq1Down, 7.9e-5), "HQ stage 1 tables");
static_assert(unityAtDc(kHq2Up, 2.0e-5) && unityAtDc(kHq2Down, 1.1e-4), "HQ stage 2 tables");

// Base-rate delays: a halfband's centre M = 2k - 1 delays M samples of its fast rate. Up: M1u/2 + M2u/4; round trip:
// (M1u + M1d)/2 + (M2u + M2d)/4. Written in quarter samples to stay integral.
template <std::size_t K>
constexpr int centre(const float (&)[K]) noexcept { return 2 * static_cast<int>(K) - 1; }
constexpr int kHqUpQuarters = 2 * centre(kHq1Up) + centre(kHq2Up);                                   // 133 = 33.25
constexpr int kHqRoundTripQuarters = 2 * (centre(kHq1Up) + centre(kHq1Down)) + centre(kHq2Up) + centre(kHq2Down); // 61
static_assert(kHqRoundTripQuarters == 4 * kHqLatency, "HQ: kHqLatency is the exact round-trip delay");
static_assert(kHqUpQuarters - 4 * kHqUpDelay >= -2 && kHqUpQuarters - 4 * kHqUpDelay <= 2,
              "HQ: kHqUpDelay is the up stage's delay, rounded");
static_assert(kHqLatency <= 64 && kStdLatency <= 4, "01 §5.6 targets");

// ---- helpers -------------------------------------------------------------------------------------------------------
using simd::f32x4;

template <int C>
inline f32x4 gather(const float* const* ch, int i) noexcept FCDSP_NONBLOCKING
{
    alignas(16) float t[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int c = 0; c < C; ++c)
        t[c] = ch[c][i];
    return simd::load(t);
}

template <int C>
inline void scatter(float* const* ch, int i, f32x4 v) noexcept FCDSP_NONBLOCKING
{
    alignas(16) float t[4];
    simd::store(t, v);
    for (int c = 0; c < C; ++c)
        ch[c][i] = t[c];
}

// One first-order allpass section (a + z^-1)/(1 + a z^-1): y = s + a*x, s' = x - a*y.
inline f32x4 allpass(f32x4 x, f32x4 a, f32x4& s) noexcept FCDSP_NONBLOCKING
{
    const f32x4 y = simd::fma(s, a, x);
    s = simd::fms(x, a, y);
    return y;
}

// sum_{i=K..1} c[i-1] * (w[-(K - i)] + w[-(K + i - 1)]), w = a ring's newest frame: the halfband's odd-offset taps
// over the window of its even phase. Four interleaved accumulators (a single fma chain is latency-bound), summed in a
// fixed order at the end.
template <int K>
inline f32x4 halfbandSum(const f32x4* w, const float (&c)[K]) noexcept FCDSP_NONBLOCKING
{
    f32x4 acc[4] = { simd::set1(0.0f), simd::set1(0.0f), simd::set1(0.0f), simd::set1(0.0f) };
    for (int i = K; i >= 1; --i)
    {
        f32x4& a = acc[(K - i) & 3];
        a = simd::fma(a, simd::set1(c[i - 1]), simd::add(w[-(K - i)], w[-(K + i - 1)]));
    }
    return simd::add(simd::add(acc[0], acc[1]), simd::add(acc[2], acc[3]));
}

} // namespace

// ---- Oversampler -------------------------------------------------------------------------------------------------

void Oversampler::configure(Quality q, int maxBaseBlock, int channels)
{
    static_assert(std::size(kStdUp) == kStdUpCoefs && std::size(kStdDown) == kStdDownCoefs, "STD tables");
    static_assert(std::size(kHq1Up) == kHq1UpK && std::size(kHq1Down) == kHq1DownK && std::size(kHq2Up) == kHq2UpK &&
                  std::size(kHq2Down) == kHq2DownK, "HQ tables");
    static_cast<void>(maxBaseBlock);        // streaming filters: no block buffers (Oversampler.h, contract details)
    quality_ = q;
    channels_ = channels < 1 ? 1 : channels > 4 ? 4 : channels;
    reset();
}

void Oversampler::reset() noexcept FCDSP_NONBLOCKING
{
    std_ = StdState{};
    hq_ = HqState{};
}

template <int C>
void Oversampler::upStd(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING
{
    f32x4 a[kStdUpCoefs];
    for (int i = 0; i < kStdUpCoefs; ++i)
        a[i] = simd::set1(kStdUp[i]);
    for (int i = 0; i < n; ++i)
    {
        const f32x4 x = gather<C>(in, i);
        f32x4 b0 = x, b1 = x;
        for (int s = 0; s < kStdUpCoefs; s += 2)
        {
            b0 = allpass(b0, a[s], std_.up[s]);
            b1 = allpass(b1, a[s + 1], std_.up[s + 1]);
        }
        scatter<C>(osOut, 2 * i, b0);
        scatter<C>(osOut, 2 * i + 1, b1);
    }
}

template <int C>
void Oversampler::downStd(const float* const* osIn, int n, float* const* out) noexcept FCDSP_NONBLOCKING
{
    f32x4 a[kStdDownCoefs];
    for (int i = 0; i < kStdDownCoefs; ++i)
        a[i] = simd::set1(kStdDown[i]);
    const f32x4 half = simd::set1(0.5f), t = simd::set1(kStdThiran);
    for (int i = 0; i < n; ++i)
    {
        f32x4 b0 = gather<C>(osIn, 2 * i), b1 = gather<C>(osIn, 2 * i + 1);
        for (int s = 0; s < kStdDownCoefs; s += 2)
            b0 = allpass(b0, a[s], std_.down[s]);
        for (int s = 1; s < kStdDownCoefs; s += 2)
            b1 = allpass(b1, a[s], std_.down[s]);
        const f32x4 y = simd::mul(simd::add(b0, std_.oddDelay), half);
        std_.oddDelay = b1;
        scatter<C>(out, i, allpass(y, t, std_.thiran));
    }
}

template <int C>
void Oversampler::upHq(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING
{
    const f32x4 two = simd::set1(2.0f);
    for (int i = 0; i < n; ++i)
    {
        const f32x4* w1 = hq_.up1.push(gather<C>(in, i));
        const f32x4 even1 = simd::mul(halfbandSum(w1, kHq1Up), two);
        const f32x4 odd1 = w1[-(kHq1UpK - 1)];

        const f32x4* w2 = hq_.up2.push(even1);
        scatter<C>(osOut, 4 * i, simd::mul(halfbandSum(w2, kHq2Up), two));
        scatter<C>(osOut, 4 * i + 1, w2[-(kHq2UpK - 1)]);
        w2 = hq_.up2.push(odd1);
        scatter<C>(osOut, 4 * i + 2, simd::mul(halfbandSum(w2, kHq2Up), two));
        scatter<C>(osOut, 4 * i + 3, w2[-(kHq2UpK - 1)]);
    }
}

template <int C>
void Oversampler::downHq(const float* const* osIn, int n, float* const* out) noexcept FCDSP_NONBLOCKING
{
    const f32x4 half = simd::set1(0.5f);
    for (int i = 0; i < n; ++i)
    {
        f32x4 mid[2];
        for (int p = 0; p < 2; ++p)
        {
            const f32x4* e = hq_.down2Even.push(gather<C>(osIn, 4 * i + 2 * p));
            const f32x4* o = hq_.down2Odd.push(gather<C>(osIn, 4 * i + 2 * p + 1));
            mid[p] = simd::fma(halfbandSum(e, kHq2Down), half, o[-kHq2DownK]);
        }
        const f32x4* e = hq_.down1Even.push(mid[0]);
        const f32x4* o = hq_.down1Odd.push(mid[1]);
        scatter<C>(out, i, simd::fma(halfbandSum(e, kHq1Down), half, o[-kHq1DownK]));
    }
}

int Oversampler::up(const float* const* in, int n, float* const* osOut) noexcept FCDSP_NONBLOCKING
{
    if (n <= 0)
        return 0;
    switch (quality_)
    {
        case Quality::eco:
            for (int c = 0; c < channels_; ++c)
                if (osOut[c] != in[c])
                    for (int i = 0; i < n; ++i)
                        osOut[c][i] = in[c][i];
            break;
        case Quality::std:
            switch (channels_)
            {
                case 1: upStd<1>(in, n, osOut); break;
                case 2: upStd<2>(in, n, osOut); break;
                case 3: upStd<3>(in, n, osOut); break;
                case 4: upStd<4>(in, n, osOut); break;
                default: break;
            }
            break;
        case Quality::hq:
            switch (channels_)
            {
                case 1: upHq<1>(in, n, osOut); break;
                case 2: upHq<2>(in, n, osOut); break;
                case 3: upHq<3>(in, n, osOut); break;
                case 4: upHq<4>(in, n, osOut); break;
                default: break;
            }
            break;
    }
    return n * kOs[static_cast<int>(quality_)].factor;
}

void Oversampler::down(const float* const* osIn, int nOs, float* const* out) noexcept FCDSP_NONBLOCKING
{
    const int n = nOs <= 0 ? 0 : nOs / kOs[static_cast<int>(quality_)].factor;
    if (n == 0)
        return;
    switch (quality_)
    {
        case Quality::eco:
            for (int c = 0; c < channels_; ++c)
                if (out[c] != osIn[c])
                    for (int i = 0; i < n; ++i)
                        out[c][i] = osIn[c][i];
            break;
        case Quality::std:
            switch (channels_)
            {
                case 1: downStd<1>(osIn, n, out); break;
                case 2: downStd<2>(osIn, n, out); break;
                case 3: downStd<3>(osIn, n, out); break;
                case 4: downStd<4>(osIn, n, out); break;
                default: break;
            }
            break;
        case Quality::hq:
            switch (channels_)
            {
                case 1: downHq<1>(osIn, n, out); break;
                case 2: downHq<2>(osIn, n, out); break;
                case 3: downHq<3>(osIn, n, out); break;
                case 4: downHq<4>(osIn, n, out); break;
                default: break;
            }
            break;
    }
}

} // namespace fcdsp
