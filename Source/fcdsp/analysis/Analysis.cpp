// Analysis.cpp: the analysis API (01 §7; E §6; K1 #22, #35; K2 #24; ADR-42). F8 (S5).
//
// One code path for the DSP and the display (E §6): every curve the Characteristics band and screen draw is computed
// by the Mode's own kernel code, reached through its ModeEntry (01 §8.2), never by a re-implementation:
//   staticGr     ModeEntry::staticGr = ModeEngine<T>::staticGr: G::target over four abscissae per call with the
//                unsmoothed LevelCtl (FB: G::solveFb with FbAffine{0, 1}), the very functions ModeEngine::control runs
//                per sample. dsp.analysis holds it bit-equal to the running kernel's tapped target.
//   staticGain   staticGr, then what ModeEngine::control applies after the computer: stage 2 (see below), the range
//                clamp min(r, min(rangeDb, kRangeOff)), and GR OFF (offAmt settled at 0 multiplies r by 0). Link is
//                not applied: the curve is one lane's, and L == R links to itself.
//   stepResponse a private ModeEngine, driven exactly as EngineRig / EngineHost drive the plugin's engine; dsp.analysis
//                holds it bit-identical to an EngineRig render of the same burst.
//   scResponse   host::ScFilter's target design (the body of ScFilter::responseDb, designed once for the whole call)
//                plus ModeEntry::scShapeDb = ModeEngine<T>::scShapeDb (SH::magDb).
//   colourCurve  ModeEntry::colourCurve = ModeEngine<T>::colourCurve (C::transfer).
// Every entry point opens fcdsp::ScopedFtz first (K2 #24), so the message thread and the PreviewWorker compute in the
// mode the audio thread and the probes run in; the scope restores the caller's mode on exit. analysis/ is exempt from
// the audio-path libm rule (01 §2.2 rule 4): libm appears only in plot-only arithmetic (dB conversions of the
// describing function, the harmonics and netGainDb, and the square wave's phase), never in anything claimed bit-equal
// to the kernel.
//
// Documented choices where 01 §7 is silent (F8 handoff):
//   - CurveOpts::stage2. ModeEntry exposes no static stage-2 function, and every registered kernel's Stage2 is
//     NoStage2, whose combine() passes stage 1 through (Diode 609 declares Stage2Kind::sharedElementMax but runs the
//     descriptor wave's generic traits). So the static curve with and without stage 2 is the same function today, and
//     staticGain returns it for both settings: it is exactly what the kernels do. The first kernel with a real stage 2
//     (M5, SharedElementMax) needs a static stage-2 entry point in ModeEntry (interface-change request in the handoff).
//   - The colour describing function (CurveOpts::colour, E §6.3) is evaluated at the sine amplitude AFTER the gain
//     element, A = 10^((x + peakOffset + preGainDb - GR) / 20), x the plugin-input level on the Mode's DetectorLaw axis
//     (rms: the sine peak is x + 3.0103 dB), with colourCurve at that point's GR; N(A) = (2 / (64 A)) sum y_i sin_i,
//     the 64-point trapezoid, and |N| floored at -240 dB.
//   - localRatio differentiates staticGain (default CurveOpts) at x +- 0.05 dB: 1 / (1 - dGR/dx). A flat curve (inf:1)
//     returns +inf, a negative-ratio region a negative value.
//   - netGainDb = 20 log10 |mix 10^((g + mk)/20) + 1 - mix|, floored at -240 dB: mix runs 0-200 % (Q5), and above 100 %
//     the wet term can cancel the dry one (the host mixes in phase: y = mix wet + (1 - mix) dry).
//   - StepStimulus. The burst is a 1 kHz square (|x| = A at every sample: peak == RMS for every detector, and a
//     Mode-internal SC high-pass still passes it; dsp.time's D2 stimulus): +A for the first half of each period,
//     sample n's phase fmod(1000 n, fs) exactly as EngineRig::runSquareSteps. A is a PLUGIN-INPUT amplitude,
//     A = linFromDb(thrDb + levelOverThr - preGainDb), and the side chain is A x linFromDb(preGainDb), which is how the
//     host and the Rig pre-gain the internal key: the detector sees thrDb + levelOverThr (K1 #35) up to float rounding,
//     and the render is the plugin's own for that input burst. Lanes {v, v, v, v} (a mono burst, E §6.4: L = R, aux
//     lanes carry the channels as in the host). Silence is amplitude 0 (the square's +-0).
//   - The private engine: construct -> prepare{fs, osFactor 1 (ECO: the control path runs at the base rate at every
//     Quality), scratch} -> setParams -> snapParams, then control() in kChunk chunks on the absolute grid from sample 0
//     (as a freshly configured host). colour() is not run: it never feeds the control path. The scratch is the host's
//     per-slot size 4 x nextPow2(L_la,max + kChunk) with L_la,max the 20 ms budget (01 §5.3), as EngineRig sizes it.
//     Segment lengths are round(seconds x fs); the output holds sample 0, d, 2d, ... (d = max(1, decimate)) of lane 0's
//     applied GR, as many as fit; rendering stops at the last sample written.
//   - measure(). The attack is the step at the pre -> hi edge: `from` is the last written sample before it, the trace
//     is the written samples of the hi segment, `to` its last written sample; the release is the hi -> lo edge
//     likewise, to the last written sample of the lo segment (so a stimulus must be long enough to settle, as 02
//     §9.3's runs are). A trace sample at absolute index i sits at (i - edge + 1) / fs, the step between time 0
//     (`from`) and the edge sample. The published quantity per TimeLaw is Measure.cpp's lawSeconds (D2): t63 in dB
//     (expDb) or in linear gain (expLin), t10-90, t0-90, t50, or dB/s over 10-90 % (rateDbPerS); -1 when a crossing
//     is missing. At decimate 1 it equals lawSeconds on the same trace, which dsp.analysis checks against dsp.time's
//     extraction.
//   - harmonicsDb: `amp` is the sine's linear peak amplitude at the colour stage's input (the COLOUR view's -1...+1
//     axis; 02's "-6 dBFS" is amp 0.501). y_i = colourCurve(amp sin(2 pi i / 64)) at grDb; h[k] = 20 log10(|Y_(k+1)| /
//     |Y_1|) from the 64-point DFT, floored at -240 dB (h[0] = 0; all floor when the fundamental vanishes). The sine
//     table is fcdsp::sinPi (fma-only, arch-neutral).
//   - Span lengths: every function writes min(input size, output size) values.

#include "fcdsp/analysis/Analysis.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/engine/host/ScFilter.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Setup.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace fcdsp::analysis {

namespace {

constexpr std::size_t kBatch = 64;                     // staticGain's abscissae per staticGr call (stack buffers)
constexpr std::size_t kDft = 64;                       // describing function and harmonics: 64 points (E §6.3)
constexpr float kRatioDeltaDb = 0.05f;                 // localRatio's central difference (E §6.1)
constexpr double kStepHz = 1000.0;                     // the burst's square wave
constexpr double kRmsPeakOffsetDb = 3.0102999566398120;   // a sine's peak over its RMS (01 §7 axis contract)
constexpr double kFloorLin = 1e-12;                    // -240 dB
constexpr double kFloorDb = -240.0;
constexpr double kOneMinusInvE = 0.63212055882855767;  // the 63 % point of a one-pole
constexpr std::int64_t kMaxSegment = std::int64_t{ 1 } << 29;   // per segment; the total stays below INT_MAX

// The pieces of a span pair an int-counted entry point can take at once.
constexpr std::size_t kMaxIntChunk = static_cast<std::size_t>(std::numeric_limits<int>::max());

double dbFromLinD(double a) noexcept { return 20.0 * std::log10(a > kFloorLin ? a : kFloorLin); }

// The GR the engine applies after the computer, statically (file comment): range, then GR OFF. Stage 2 adds nothing
// in any registered kernel (file comment, "CurveOpts::stage2").
float appliedGr(float r, const EngineParams& e) noexcept
{
    const float range = e.rangeDb < kRangeOff ? e.rangeDb : kRangeOff;
    const float clamped = range < r ? range : r;           // a NaN r stays NaN (poison is visible, 01 §5.8)
    return (e.flags & kEngGrOff) != 0 ? 0.0f : clamped;
}

double peakOffsetDb(const ModeEntry& en, const EngineParams& e) noexcept
{
    if (en.desc == nullptr || en.desc->detectorLaw == nullptr)
        return 0.0;
    return en.desc->detectorLaw(e) == DetectorLaw::rms ? kRmsPeakOffsetDb : 0.0;
}

// sin(2 pi i / 64), i = 0 ... 63, from fcdsp::sinPi with the argument folded into [-1, 1).
void sineTable(std::array<float, kDft>& s) noexcept
{
    for (std::size_t i = 0; i < kDft; ++i)
    {
        const int j = static_cast<int>(i) - (i < kDft / 2 ? 0 : static_cast<int>(kDft));   // [-32, 32)
        s[i] = fcdsp::sinPi(static_cast<float>(j) * (2.0f / static_cast<float>(kDft)));
    }
}

// The colour stage's static transfer, n values, through the entry (identity without one).
void colourTransfer(const ModeEntry& en, const EngineParams& e, float grDb, const float* x, float* y,
                    std::size_t n) noexcept
{
    if (en.colourCurve == nullptr)
    {
        for (std::size_t i = 0; i < n; ++i)
            y[i] = x[i];
        return;
    }
    for (std::size_t off = 0; off < n; off += kMaxIntChunk)
    {
        const std::size_t m = n - off < kMaxIntChunk ? n - off : kMaxIntChunk;
        en.colourCurve(e, grDb, x + off, y + off, static_cast<int>(m));
    }
}

// 20 log10 |N(A)|, the describing-function gain of the colour stage at sine amplitude `amp` and GR `grDb` (E §6.3).
double describingGainDb(const ModeEntry& en, const EngineParams& e, float amp, float grDb,
                        const std::array<float, kDft>& s) noexcept
{
    if (!(amp > 0.0f))
        return 0.0;
    std::array<float, kDft> x{}, y{};
    for (std::size_t i = 0; i < kDft; ++i)
        x[i] = amp * s[i];
    colourTransfer(en, e, grDb, x.data(), y.data(), kDft);
    double acc = 0.0;
    for (std::size_t i = 0; i < kDft; ++i)
        acc += static_cast<double>(y[i]) * static_cast<double>(s[i]);
    const double n = 2.0 * acc / (static_cast<double>(kDft) * static_cast<double>(amp));
    return dbFromLinD(std::fabs(n));
}

// Samples of a stimulus segment: round(seconds x fs), 0 for a negative or NaN length, capped at kMaxSegment.
std::int64_t segmentSamples(float seconds, float fs) noexcept
{
    const double v = static_cast<double>(seconds) * static_cast<double>(fs) + 0.5;
    if (!(v >= 1.0))
        return 0;
    return v < static_cast<double>(kMaxSegment) ? static_cast<std::int64_t>(v) : kMaxSegment;
}

struct Segments {
    float fs = 0.0f;
    std::int64_t pre = 0, hi = 0, lo = 0, decimate = 1;
    std::int64_t total() const noexcept { return pre + hi + lo; }
};

Segments segmentsOf(const StepStimulus& st) noexcept
{
    Segments g;
    g.fs = st.fs > 0.0f ? st.fs : 0.0f;                   // NaN -> 0: nothing to render
    if (g.fs > 0.0f)
    {
        g.pre = segmentSamples(st.preSec, g.fs);
        g.hi = segmentSamples(st.hiSec, g.fs);
        g.lo = segmentSamples(st.loSec, g.fs);
    }
    g.decimate = st.decimate > 1 ? st.decimate : 1;
    return g;
}

// Written samples of the response in [begin, end) of absolute sample indices: j with j d in [begin, end), clipped to
// the written count. Returns the first j and the count.
struct Window { std::int64_t first = 0, count = 0; };

Window windowOf(std::int64_t begin, std::int64_t end, std::int64_t d, std::int64_t written) noexcept
{
    const std::int64_t j0 = (begin + d - 1) / d;           // first j with j d >= begin
    std::int64_t j1 = (end + d - 1) / d;                   // first j with j d >= end
    j1 = j1 < written ? j1 : written;
    return Window{ j0, j1 > j0 ? j1 - j0 : 0 };
}

// The first time the trace reaches from + fraction (to - from), linearly interpolated (Measure.cpp crossingSeconds,
// with the trace sample j at absolute index (first + j) d, i.e. at ((first + j) d - edge + 1) / fs); -1 if never.
double crossingSeconds(std::span<const float> gr, const Window& w, std::int64_t edge, std::int64_t d, double from,
                       double to, double fraction, double fs) noexcept
{
    const double span = to - from;
    if (span == 0.0 || !(fs > 0.0))
        return -1.0;
    double prevT = 0.0, prevP = 0.0;
    for (std::int64_t j = 0; j < w.count; ++j)
    {
        const std::int64_t i = (w.first + j) * d;
        const double p = (static_cast<double>(gr[static_cast<std::size_t>(w.first + j)]) - from) / span;
        const double t = static_cast<double>(i - edge + 1) / fs;
        if (p >= fraction)
            return p == prevP ? t : prevT + (fraction - prevP) / (p - prevP) * (t - prevT);
        prevT = t;
        prevP = p;
    }
    return -1.0;
}

// The published quantity of `law` for the step at `edge` whose response is the window `w` (Measure.cpp lawSeconds).
double lawValue(std::span<const float> gr, const Window& w, std::int64_t edge, std::int64_t d, double fs, TimeLaw law)
    noexcept
{
    if (w.count <= 0 || w.first <= 0)
        return -1.0;                                        // no sample before the edge, or none after it
    const double from = static_cast<double>(gr[static_cast<std::size_t>(w.first - 1)]);
    const double to = static_cast<double>(gr[static_cast<std::size_t>(w.first + w.count - 1)]);
    const auto at = [&](double fraction) { return crossingSeconds(gr, w, edge, d, from, to, fraction, fs); };
    const auto between = [&](double lo, double hi) {
        const double a = at(lo), b = at(hi);
        return a < 0.0 || b < 0.0 ? -1.0 : b - a;
    };
    switch (law)
    {
        case TimeLaw::expDb:
            return at(kOneMinusInvE);
        case TimeLaw::expLin:
        {
            // 63 % of the change in LINEAR gain, found on the dB trace at the equivalent GR (monotone in each other)
            const double gFrom = std::pow(10.0, -from / 20.0), gTo = std::pow(10.0, -to / 20.0);
            const double level = -20.0 * std::log10(gFrom + kOneMinusInvE * (gTo - gFrom));
            return at((level - from) / (to - from));
        }
        case TimeLaw::t10_90:
            return between(0.1, 0.9);
        case TimeLaw::t0_90:
            return at(0.9);
        case TimeLaw::t50:
            return at(0.5);
        case TimeLaw::rateDbPerS:
        {
            const double t = between(0.1, 0.9);
            return t > 0.0 ? 0.8 * std::fabs(to - from) / t : -1.0;
        }
    }
    return -1.0;
}

int nextPow2(std::int64_t v) noexcept
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

// Destroys the private engine however stepResponse leaves (engines own no resources, 01 §5.3).
struct EngineGuard {
    IEngine* engine = nullptr;
    EngineGuard() = default;
    EngineGuard(const EngineGuard&) = delete;
    EngineGuard& operator=(const EngineGuard&) = delete;
    ~EngineGuard()
    {
        if (engine != nullptr)
            engine->~IEngine();
    }
};

} // namespace

void staticGain(const ModeEntry& en, const EngineParams& e, std::span<const float> xDb, std::span<float> gainDb,
                CurveOpts opts) noexcept
{
    const ScopedFtz ftz;
    const std::size_t n = xDb.size() < gainDb.size() ? xDb.size() : gainDb.size();
    const float pre = e.preGainDb;
    std::array<float, kDft> sine{};
    double peakOffset = 0.0;
    if (opts.colour)
    {
        sineTable(sine);
        peakOffset = peakOffsetDb(en, e);
    }
    // opts.stage2: no kernel has a static stage 2 to add (file comment); both settings draw the kernels' curve.

    alignas(16) std::array<float, kBatch> xd{}, gr{};
    for (std::size_t i = 0; i < n; i += kBatch)
    {
        const std::size_t m = n - i < kBatch ? n - i : kBatch;
        for (std::size_t k = 0; k < m; ++k)
            xd[k] = xDb[i + k] + pre;                       // plugin input -> detector domain (after preGain)
        if (en.staticGr != nullptr)
            en.staticGr(e, xd.data(), gr.data(), static_cast<int>(m));
        else
            gr.fill(0.0f);
        for (std::size_t k = 0; k < m; ++k)
        {
            const float r = appliedGr(gr[k], e);
            float g = pre - r;
            if (opts.colour)
            {
                const double ampDb = static_cast<double>(xDb[i + k]) + peakOffset + static_cast<double>(pre)
                                   - static_cast<double>(r);
                const auto amp = static_cast<float>(std::pow(10.0, ampDb / 20.0));
                g = static_cast<float>(static_cast<double>(g) + describingGainDb(en, e, amp, r, sine));
            }
            gainDb[i + k] = g;
        }
    }
}

void staticGr(const ModeEntry& en, const EngineParams& e, std::span<const float> xDetDb, std::span<float> grDb) noexcept
{
    const ScopedFtz ftz;
    const std::size_t n = xDetDb.size() < grDb.size() ? xDetDb.size() : grDb.size();
    if (en.staticGr == nullptr)
    {
        for (std::size_t i = 0; i < n; ++i)
            grDb[i] = 0.0f;
        return;
    }
    // Pieces of a multiple of four keep ModeEngine::staticGr's four-abscissa groups where one call would put them.
    constexpr std::size_t kPiece = kMaxIntChunk & ~std::size_t{ 3 };
    for (std::size_t off = 0; off < n; off += kPiece)
    {
        const std::size_t m = n - off < kPiece ? n - off : kPiece;
        en.staticGr(e, xDetDb.data() + off, grDb.data() + off, static_cast<int>(m));
    }
}

float localRatio(const ModeEntry& en, const EngineParams& e, float xDb) noexcept
{
    const ScopedFtz ftz;
    const std::array<float, 2> x{ xDb - kRatioDeltaDb, xDb + kRatioDeltaDb };
    std::array<float, 2> g{};
    staticGain(en, e, x, g);
    const double dx = static_cast<double>(x[1]) - static_cast<double>(x[0]);
    const double dOutDx = 1.0 + (static_cast<double>(g[1]) - static_cast<double>(g[0])) / dx;   // 1 - dGR/dx
    return static_cast<float>(1.0 / dOutDx);
}

float netGainDb(float gainDb, float makeupEffDb, float mix) noexcept
{
    const ScopedFtz ftz;
    const double wet = std::pow(10.0, (static_cast<double>(gainDb) + static_cast<double>(makeupEffDb)) / 20.0);
    const double m = static_cast<double>(mix);
    const double y = m * wet + (1.0 - m);
    if (std::isnan(y))
        return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(dbFromLinD(std::fabs(y)));
}

int stepResponse(const ModeEntry& en, const EngineParams& e, const StepStimulus& st, std::span<float> grDbOut)
{
    const ScopedFtz ftz;
    const Segments seg = segmentsOf(st);
    if (en.construct == nullptr || en.engineBytes > kArenaBytes || en.engineAlign > 64 || grDbOut.empty()
        || seg.total() <= 0)
        return 0;
    const std::int64_t d = seg.decimate;
    const std::int64_t want = (seg.total() + d - 1) / d;
    const std::int64_t cap = static_cast<std::int64_t>(grDbOut.size());
    const std::int64_t written = want < cap ? want : cap;
    const std::int64_t render = (written - 1) * d + 1;   // samples 0 ... (written - 1) d

    // The burst (file comment): plugin-input amplitudes, pre-gained into the side chain as the host and the Rig do.
    const float pg = simd::lane<0>(linFromDb(simd::set1(e.preGainDb)));
    const float aHi = simd::lane<0>(linFromDb(simd::set1(e.thrDb + st.hiDbOverThr - e.preGainDb)));
    const float aLo = simd::lane<0>(linFromDb(simd::set1(e.thrDb - st.loDbUnderThr - e.preGainDb)));
    const double fsd = static_cast<double>(seg.fs);

    // The private engine, in a local arena with the host's scratch (file comment).
    const int la = lookaheadSamples(LookaheadBudget::ms20, fsd);
    std::vector<float> scratch(4u * static_cast<std::size_t>(nextPow2(static_cast<std::int64_t>(la) + kChunk)), 0.0f);
    alignas(64) std::array<std::byte, kArenaBytes> arena{};
    EngineGuard guard;
    guard.engine = en.construct(arena.data());
    IEngine& engine = *guard.engine;
    engine.prepare(PrepareInfo{ seg.fs, 1, std::span<float>(scratch) });
    engine.setParams(e);
    engine.snapParams();

    alignas(16) std::array<simd::f32x4, kChunk> sc{}, gr{};
    for (std::int64_t i0 = 0; i0 < render; i0 += kChunk)
    {
        const int n = render - i0 < kChunk ? static_cast<int>(render - i0) : kChunk;
        for (int k = 0; k < n; ++k)
        {
            const std::int64_t i = i0 + k;
            const float a = i < seg.pre ? 0.0f : (i < seg.pre + seg.hi ? aHi : aLo);
            const double phase = std::fmod(kStepHz * static_cast<double>(i), fsd);
            const float x = phase < 0.5 * fsd ? a : -a;
            sc[static_cast<std::size_t>(k)] = simd::set1(x * pg);
        }
        ControlIo io;
        io.n = n;
        io.sampleIndex = static_cast<uint64_t>(i0);
        io.sc = sc.data();
        io.grDb = gr.data();
        engine.control(io);
        for (int k = 0; k < n; ++k)
        {
            const std::int64_t i = i0 + k;
            if (i % d == 0)
                grDbOut[static_cast<std::size_t>(i / d)] = simd::lane<0>(gr[static_cast<std::size_t>(k)]);
        }
    }
    return static_cast<int>(written);
}

TimeReadout measure(std::span<const float> grDb, const StepStimulus& st, TimeLaw law) noexcept
{
    const ScopedFtz ftz;
    TimeReadout out{ -1.0f, -1.0f, law };
    const Segments seg = segmentsOf(st);
    if (!(seg.fs > 0.0f) || grDb.empty())
        return out;
    const std::int64_t d = seg.decimate;
    const auto written = static_cast<std::int64_t>(grDb.size());
    const double fs = static_cast<double>(seg.fs);
    const std::int64_t atkEdge = seg.pre, relEdge = seg.pre + seg.hi;
    const Window atk = windowOf(atkEdge, relEdge, d, written);
    const Window rel = windowOf(relEdge, seg.total(), d, written);
    out.attackS = static_cast<float>(lawValue(grDb, atk, atkEdge, d, fs, law));
    out.releaseS = static_cast<float>(lawValue(grDb, rel, relEdge, d, fs, law));
    return out;
}

void scResponse(const ModeEntry& en, const EngineParams& e, float fs, std::span<const float> hz,
                std::span<float> magDb) noexcept
{
    const ScopedFtz ftz;
    const std::size_t n = hz.size() < magDb.size() ? hz.size() : magDb.size();
    if (en.scShapeDb != nullptr)
        for (std::size_t off = 0; off < n; off += kMaxIntChunk)
        {
            const std::size_t m = n - off < kMaxIntChunk ? n - off : kMaxIntChunk;
            en.scShapeDb(e, fs, hz.data() + off, magDb.data() + off, static_cast<int>(m));
        }
    else
        for (std::size_t i = 0; i < n; ++i)
            magDb[i] = 0.0f;
    // ScFilter::responseDb(e.scHpfHz, e.sceDbOct, fs, hz), its design hoisted out of the loop.
    host::ScDesign design;
    host::designHpf(design, e.scHpfHz, fs);
    host::designTilt(design, e.sceDbOct, fs);
    for (std::size_t i = 0; i < n; ++i)
        magDb[i] = magDb[i] + host::magDb(design, hz[i]);
}

void colourCurve(const ModeEntry& en, const EngineParams& e, float grDb, std::span<const float> x,
                 std::span<float> y) noexcept
{
    const ScopedFtz ftz;
    const std::size_t n = x.size() < y.size() ? x.size() : y.size();
    colourTransfer(en, e, grDb, x.data(), y.data(), n);
}

void harmonicsDb(const ModeEntry& en, const EngineParams& e, float grDb, float amp, std::span<float, 8> h) noexcept
{
    const ScopedFtz ftz;
    std::array<float, kDft> s{}, x{}, y{};
    sineTable(s);
    for (std::size_t i = 0; i < kDft; ++i)
        x[i] = amp * s[i];
    colourTransfer(en, e, grDb, x.data(), y.data(), kDft);

    // |Y_k|, k = 1 ... 8; the table is periodic: sin(2 pi k i / 64) = s[k i mod 64], cos = s[(k i + 16) mod 64]
    std::array<double, 9> mag{};
    for (std::size_t k = 1; k <= 8; ++k)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < kDft; ++i)
        {
            const std::size_t ki = (k * i) % kDft;
            re += static_cast<double>(y[i]) * static_cast<double>(s[(ki + kDft / 4) % kDft]);
            im += static_cast<double>(y[i]) * static_cast<double>(s[ki]);
        }
        mag[k] = std::sqrt(re * re + im * im);
    }
    h[0] = 0.0f;
    for (std::size_t k = 2; k <= 8; ++k)
        h[k - 1] = mag[1] > 0.0 ? static_cast<float>(dbFromLinD(mag[k] / mag[1])) : static_cast<float>(kFloorDb);
}

} // namespace fcdsp::analysis
