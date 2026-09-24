// FCMP_PROBE layer=dsp name=analysis scope=mode timeout=120
//
// dsp.analysis.<key> (F8, S5; 03 §3.4 Rig row, E §6.5; 01 §7; K1 #22, #35; K2 #24; ADR-42): the analysis functions the
// Characteristics band and screen draw from are the audio kernel's own code. The truthfulness rows compare each
// function with the Mode's engine RUNNING (EngineRig: the same ModeEngine object code the plugin runs, K3 #10), bit for
// bit where the design claims identity:
//
// Configurations <c>: def (the Mode's defaults); c.r<R>.t<T>.k<W> for 4:1 T -30 W 6, inf:1 T -10 W 0 and 1.5:1 T -20
// W 12 (nearest detents when stepped, clamped when continuous; T is the input threshold, analysis::inputThresholdDb);
// det.<step> for every other detector step; voice.<step> for every voice step that switches the kernel's topology
// (Bus 25 OLD = FB); range<V> at 4:1 T -30 W 6 with the range nearest 6 dB, when the range is live.
//
// Rows (spec, blocking unless marked NOTE; T = input threshold, W = knee, S = slope, k = FB loop gain):
//   analysis.<c>.staticgr.grid      distinct detector levels the kernel saw: >= 1,024 (a rising staircase of 1,024
//                                   square-wave levels over [T - W - 30, T + 50] dB, 32 samples each, through the Rig)
//   analysis.<c>.staticgr.mismatch  the kernel's tapped static target (ControlIo::tgtDb) against analysis::staticGr at
//                                   the tapped detector level (ControlIo::detDb), bit for bit, every sample: lanes 2-3
//                                   always (the computer's output, never linked), lanes 0-1 as well when the kernel is
//                                   FB (tgtDb is the static FB solve) or the link is exactly 0 or 1 (L == R then links
//                                   to itself bit for bit); otherwise lanes 0-1 print a NOTE with their ulp distance
//   analysis.<c>.settled_max_err_db the engine's settled applied GR at constant levels T + {-10 ... +30} dB (rising,
//                                   each held max(0.3 s, 30 tau_A)) against preGainDb - staticGain at the tapped
//                                   detector level: <= 0.01 dB (03 §3.4: FB settled GR; FF too), release >= 100 ms;
//                                   judged for peak-law Modes (DetectorLaw::peak), a NOTE for the others (03 §3.4)
//   analysis.<c>.step.written       analysis::stepResponse writes every sample of the burst
//   analysis.<c>.step.mismatch      ... bit-identical to the Rig's tapped lane-0 GR for the same plugin-input burst (01
//                                   §7: a 1 kHz square at thrDb + 12 - preGainDb for max(0.5 s, 10 tau_A), then at
//                                   thrDb - 12 - preGainDb for max(2 s, 10 tau_R) up to 30 s, after 50 ms of silence)
//   analysis.<c>.step.nonfinite, .step.gr_min_db
//   analysis.<c>.step.<attack|release>_vs_d2_s  analysis::measure on the response against D2's extraction
//                                   (Measure.cpp lawSeconds, dsp.time) on the Rig's tap, same edges, same law (the
//                                   descriptor's attackSpec / releaseSpec law): |diff| <= 1e-9 s + 1e-6 relative
//   analysis.<c>.ratio.above_inv    1 / localRatio above the knee (FF: T + W/2 + 20; FB: T + (1 + k) W/2 + 20 dB)
//                                   against the declared 1/R: 1 - S (FF) or 1 / (1 + k) (FB, QuadKnee::loopGain):
//                                   +- 1e-3; textbook families only (NOTE otherwise), skipped where the range clamps
//   analysis.<c>.ratio.below        localRatio 10 dB below the knee == 1 +- 1e-4 (textbook families)
//   analysis.def.step.host_mismatch the def burst through the plugin's own host (fcdsp::EngineHost at ECO without
//                                   lookahead, stereo L = R, 512-sample blocks, setTap) is bit-identical too (01 §7)
//   analysis.def.step.decimate7.mismatch, .truncated.mismatch  decimate 7 writes every 7th sample of the full
//                                   response; a short output span gets the full response's prefix
//   analysis.groff.gain_mismatch    staticGain with kEngGrOff: gainDb == preGainDb bit for bit (offAmt's product)
//   analysis.stage2.mismatch        CurveOpts::stage2 true and false draw the same curve when the Mode has no static stage
//                                   2 (ModeEntry::staticS2 == nullptr: NoStage2; a NOTE otherwise); S10 interface
//                                   revision (X10): .off_mismatch: staticGr with CurveOpts stage2 = false is the
//                                   four-argument staticGr, bit for bit; .compose_mismatch: with stage2 = true it is
//                                   ModeEntry::staticS2 applied to it (the identity without one), bit for bit
//   analysis.stage2.local.*         (Mode-independent, S10) the static stage-2 hook on probe-local traits (PeakLog,
//                                   QuadKnee 2:1 T -30 W 6, LinkMax, SmoothBranching, ColourNone, Flat; FF | FB) with a
//                                   probe-local shared-element stage 2 (a 10:1 hard-knee limiter at s2thr -20 dB through
//                                   SmoothBranching on the aux lanes, max with stage 1): .nostage2_identity_mismatch
//                                   (ModeEngine<T>::staticS2 with NoStage2 is the identity, bit for bit),
//                                   .off_mismatch (s2thr at kS2Off: the computer alone, bit for bit; that makeModeEntry
//                                   fills the pointer only for a Stage2 with combineStatic is a static_assert);
//                                   .<ff|fb>.settled_max_err_db: the engine's settled GR at 10 rising square levels
//                                   T - 10 ... T + 40 (0.3 s each) against staticGain with stage 2: <= 1e-5 dB FF,
//                                   1e-4 dB FB; .<ff|fb>.stage<1|2>_decides >= 1 (each stage sets the GR somewhere)
//   analysis.colourdf.max_err_db    staticGain(colour) - staticGain against the probe's own describing function (the
//                                   64-point trapezoid on sig::sinTurns and the entry's colourCurve, dsp.static's
//                                   method) at 21 levels of every voice: <= 0.001 dB
//   analysis.sc.<cfg>.compose_mismatch  scResponse == ScFilter::responseDb + ModeEntry::scShapeDb, bit for bit, at
//                                   161 log frequencies 20 Hz-20 kHz: def, hpf (100 Hz) and sce (+3 dB/oct) where live
//   analysis.sc.<cfg>.nonfinite
//   analysis.colour.<voice>.compose_mismatch  colourCurve == ModeEntry::colourCurve at GR 0 and 6 dB, x in
//                                   [-1.5, 1.5]
//   analysis.harm.<voice>.h1_db     harmonicsDb's h[0] == 0 exactly; .nonfinite; .vs_dft_db: every harmonic above
//                                   -100 dB against the probe's own 64-point DFT (sig::sinTurns): <= 0.01 dB
//   analysis.harm.<voice>.vs_engine_db  fidelity (NOTE while provisional): H2-H5 above -70 dB of a -6 dBFS 1 kHz
//                                   sine with no GR through the Rig at 192 kHz (single-bin DFT, whole cycles) against
//                                   harmonicsDb at the colour stage's input amplitude: <= 1 dB; skipped for
//                                   colourStatic == false (the COLOUR view prints STATIC APPROXIMATION) and when the
//                                   resolved mix is not 1
//   analysis.netgain.{mix1_err_db, mix0_db, mix0p5_err_db, mix2_null_db}  netGainDb's identities: mix 1 = g + mk,
//                                   mix 0 = 0 dB, mix 0.5 = 20 log10((w + 1) / 2), mix 2 at w = 1/2 cancels (-120 dB)
//   analysis.ftz.mismatch           every entry point called with the caller's flush-to-zero OFF returns what it
//                                   returns under FTZ, bit for bit (each opens ScopedFtz itself, K2 #24);
//                                   .caller_mode_kept: each restores the caller's FP mode (1); .step.mismatch:
//                                   stepResponse into a release tail long enough to reach denormals (release at its
//                                   fastest, lo for 100 tau_R in [2, 40] s). A NOTE reports how many samples a render
//                                   WITHOUT ScopedFtz differs by (whether this Mode's tail can see a missing scope)
//   analysis.overlay.{frame_read, slot, curve_mismatch}  K2 #24: an attached EngineHost at the defaults publishes a
//                                   UiFrame; resolve(raw) + overlaySmoothed(frame) draws the resolved curve bit for
//                                   bit (and localRatio agrees). A NOTE counts how far a curve from the frame ALONE
//                                   would be off (why the UI never builds EngineParams from UiFrame alone)
// Golden rows (candidates while the Mode is provisional; adopt refuses them):
//   analysis.gain.x<d>_db           staticGain at T + {-10, 0, +10, +20} dB (def), abs:0.001
//   analysis.invratio.x20           1 / localRatio at T + 20 dB (def), abs:0.0001
//   analysis.step.<attack|release>_s  measure() of the def burst in the descriptor's laws, rel:0.01
//   analysis.sc.<cfg>.f<hz>_db      scResponse at 20, 100, 1 k, 10 kHz, abs:0.01
//   analysis.harm.<voice>.{h2,h3,thd}_db  harmonicsDb at amp 0.5 (-6 dBFS), GR 0, abs:0.5 (THD from h[1..7]);
//                                   floored at -120 dB (below it the 64-point float DFT reads its own rounding)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Fidelity.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/engine/host/ScFilter.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/colour/ColourNone.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/link/LinkMax.h"
#include "fcdsp/engine/stages/scshape/Flat.h"
#include "fcdsp/engine/stages/stage2/NoStage2.h"
#include "fcdsp/modes/DefineMode.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
#endif

namespace fcdsp::modes
{
    extern const ModeDescriptor kClean;         // CleanDesc.cpp: the probe-local stage-2 traits borrow its descriptor
}

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;

    constexpr float kFs = 48000.0f;
    constexpr float kVoiceFs = 192000.0f;          // dsp.static's voice rate: ADAA-1's residual is small there
    constexpr int kGridLevels = 1024;              // 03 §3.4: a 1,024-point grid
    constexpr int kGridHold = 32;                  // samples per grid level
    constexpr double kSettledTolDb = 0.01;         // 03 §3.4, E §6.5
    constexpr double kRatioInvTol = 1e-3;
    constexpr double kHarmVsDftDb = 0.01;
    constexpr double kHarmVsEngineDb = 1.0;
    constexpr double kColourDfTolDb = 0.001;
    constexpr double kGoldenFloorDb = -120.0;      // harmonic golden rows: the float DFT's rounding lies below
    constexpr std::size_t kRigBlock = 4096;        // the Rig renders and taps in blocks of this (bounded tap memory)
    constexpr float kStepMaxLoSec = 30.0f;
    constexpr double kStepHz = 1000.0;             // Analysis.cpp's burst: a 1 kHz square
    constexpr auto kChunkZ = static_cast<std::size_t>(kChunk);

    // ---- labels (key-safe; dsp.static's conventions) ----------------------------------------------------------------
    std::string fmtLabel(double v)
    {
        char b[32];
        std::snprintf(b, sizeof b, "%g", v);
        std::string s(b);
        std::replace(s.begin(), s.end(), '.', 'p');
        return s;
    }

    std::string ratioLabel(float slope)
    {
        if (slope >= 1.0f)
            return slope == 1.0f ? "inf" : "neg" + fmtLabel(1.0 / (static_cast<double>(slope) - 1.0));
        return fmtLabel(std::round(1000.0 / (1.0 - static_cast<double>(slope))) / 1000.0);
    }

    std::string stepKey(const char* label)
    {
        std::string s;
        for (const char* p = label; p != nullptr && *p != '\0'; ++p)
            if (std::isalnum(static_cast<unsigned char>(*p)) != 0)
                s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        return s.empty() ? "step" : s;
    }

    std::string num(double v)
    {
        char b[40];
        std::snprintf(b, sizeof b, "%.9g", v);
        return b;
    }

    std::uint32_t bitsOf(float v) noexcept { return std::bit_cast<std::uint32_t>(v); }

    // ---- the caller's FP mode ---------------------------------------------------------------------------------------
    // Flush-to-zero OFF for a scope (the probe body runs under ProbeMain's ScopedFtz), as dsp.hostile does.
#if defined(__aarch64__)
    constexpr std::uint64_t kFtzBits = std::uint64_t{ 1 } << 24;             // FPCR.FZ
    constexpr std::uint64_t kModeBits = ~std::uint64_t{ 0 };                 // FPCR holds no status flags
#else
    constexpr std::uint64_t kFtzBits = 0x8040u;                              // MXCSR FTZ | DAZ
    constexpr std::uint64_t kModeBits = ~std::uint64_t{ 0x3f };              // MXCSR without its sticky flags
#endif

    std::uint64_t fpMode() noexcept
    {
#if defined(__aarch64__)
        std::uint64_t v = 0;
        __asm__ volatile("mrs %0, fpcr" : "=r"(v) : : "memory");
        return v;
#else
        return _mm_getcsr();
#endif
    }

    void setFpMode(std::uint64_t v) noexcept
    {
#if defined(__aarch64__)
        __asm__ volatile("msr fpcr, %0" : : "r"(v) : "memory");
#else
        _mm_setcsr(static_cast<unsigned int>(v));
#endif
    }

    class ScopedNoFtz
    {
    public:
        ScopedNoFtz() noexcept : saved_(fpMode()) { setFpMode(saved_ & ~kFtzBits); }
        ~ScopedNoFtz() { setFpMode(saved_); }
        ScopedNoFtz(const ScopedNoFtz&) = delete;
        ScopedNoFtz& operator=(const ScopedNoFtz&) = delete;

    private:
        std::uint64_t saved_;
    };

    // ---- parameter choices (dsp.static's helpers) -------------------------------------------------------------------
    bool live(const ParamSpec* s)
    {
        return s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::stepped || s->kind == Kind::hybrid);
    }

    // The value of `pid` nearest `want`: the nearest detent (stepped), clamped (continuous, hybrid), else the resolved.
    float nearest(const ParamView& v, Pid pid, float want)
    {
        const ParamSpec* s = v.spec[idx(pid)];
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
        {
            const Step* best = &s->steps[0];
            for (const Step& st : s->steps)
                if (std::fabs(st.plain - want) < std::fabs(best->plain - want))
                    best = &st;
            return best->plain;
        }
        if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            return std::clamp(want, s->lo, s->hi);
        return v[pid].plain;
    }

    // The fastest (or slowest) value of a time parameter's active spec (its resolved value when not live).
    float timeEdge(const ParamView& v, Pid pid, bool fastest)
    {
        const ParamSpec* s = v.spec[idx(pid)];
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
            return fastest ? s->steps.front().plain : s->steps.back().plain;
        if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            return fastest ? s->lo : s->hi;
        return v[pid].plain;
    }

    // A release of at least `ms`: the first detent >= ms (else the slowest), or max(resolved, ms) clamped.
    float releaseAtLeast(const ParamView& v, float ms)
    {
        const ParamSpec* s = v.spec[idx(Pid::rel)];
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
        {
            for (const Step& st : s->steps)
                if (st.plain >= ms)
                    return st.plain;
            return s->steps.back().plain;
        }
        if (s != nullptr && (s->kind == Kind::continuous || s->kind == Kind::hybrid))
            return std::clamp(std::max(v[Pid::rel].plain, ms), s->lo, s->hi);
        return v[Pid::rel].plain;
    }

    struct Config
    {
        std::string key;
        RawParams raw;
        Resolution res;
    };

    std::string curveLabel(const EngineParams& e)
    {
        return "r" + ratioLabel(e.slope) + ".t" + fmtLabel(static_cast<double>(analysis::inputThresholdDb(e))) + ".k"
             + fmtLabel(static_cast<double>(std::max(0.0f, e.kneeDb)));
    }

    RawParams withCurve(const RawParams& base, const ParamView& view, float slope, float thr, float knee)
    {
        RawParams raw = base;
        raw[Pid::ratio] = nearest(view, Pid::ratio, slope);
        raw[Pid::thr] = nearest(view, Pid::thr, thr);
        raw[Pid::knee] = nearest(view, Pid::knee, knee);
        return raw;
    }

    std::vector<Config> configsOf(const ModeEntry& en, const RawParams& base)
    {
        std::vector<Config> out;
        // A configuration whose resolved EngineParams equal an earlier one's adds nothing (e.g. a Mode whose defaults
        // are 4:1 T -30 W 6): skipped, and so is a repeated key.
        const auto add = [&](std::string key, const RawParams& raw) {
            const Resolution r = fcmp::probe::resolveRaw(en, raw);
            for (const Config& c : out)
                if (c.key == key || std::memcmp(&c.res.eng, &r.eng, sizeof(EngineParams)) == 0)
                    return;
            out.push_back(Config{ std::move(key), raw, r });
        };
        const Resolution def = fcmp::probe::resolveRaw(en, base);
        const ParamView& view = def.view;
        add("def", base);
        const float curves[][3] = { { 0.75f, -30.0f, 6.0f },                   // slope, threshold, knee
                                    { 1.0f, -10.0f, 0.0f },
                                    { 1.0f - 1.0f / 1.5f, -20.0f, 12.0f } };
        for (const auto& c : curves)
        {
            const RawParams raw = withCurve(base, view, c[0], c[1], c[2]);
            add("c." + curveLabel(fcmp::probe::resolveRaw(en, raw).eng), raw);
        }
        if (const ParamSpec* s = view.spec[idx(Pid::det)]; s != nullptr && s->kind == Kind::stepped)
            for (const Step& st : s->steps)
                if (st.plain != view[Pid::det].plain)
                {
                    RawParams raw = base;
                    raw[Pid::det] = st.plain;
                    add("det." + stepKey(st.label), raw);
                }
        if (const ParamSpec* s = view.spec[idx(Pid::voice)]; s != nullptr && s->kind == Kind::stepped)
            for (const Step& st : s->steps)
            {
                RawParams raw = base;
                raw[Pid::voice] = st.plain;
                if (fcmp::probe::resolveRaw(en, raw).eng.topo != def.eng.topo)
                    add("voice." + stepKey(st.label), raw);
            }
        if (live(view.spec[idx(Pid::range)]))
        {
            RawParams raw = withCurve(base, view, 0.75f, -30.0f, 6.0f);
            raw[Pid::range] = nearest(view, Pid::range, 6.0f);
            const EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
            if (e.rangeDb < kRangeOff)
                add("range" + fmtLabel(static_cast<double>(e.rangeDb)), raw);
        }
        return out;
    }

    // ---- signals ----------------------------------------------------------------------------------------------------
    // Analysis.cpp's square: +a for the first half of each 1 kHz period (sample n's phase fmod(1000 n, fs)).
    float squareAt(std::int64_t n, float a, float fs)
    {
        const double fsd = static_cast<double>(fs);
        return std::fmod(kStepHz * static_cast<double>(n), fsd) < 0.5 * fsd ? a : -a;
    }

    float linDb(float db) { return simd::lane<0>(linFromDb(simd::set1(db))); }

    // Renders `in` (L = R) through the Rig in kRigBlock blocks and returns the tap's gr, det and tgt (every lane).
    struct Tapped
    {
        std::vector<simd::f32x4> gr, det, tgt;
    };

    Tapped renderTapped(fcmp::probe::EngineRig& rig, const std::vector<float>& in)
    {
        Tapped t;
        t.gr.reserve(in.size());
        t.det.reserve(in.size());
        t.tgt.reserve(in.size());
        std::vector<float> yl(kRigBlock), yr(kRigBlock);
        rig.tap().clear();
        rig.setTapping(true);
        for (std::size_t off = 0; off < in.size(); off += kRigBlock)
        {
            const std::size_t m = std::min(kRigBlock, in.size() - off);
            rig.process(in.data() + off, in.data() + off, yl.data(), yr.data(), m);
            const fcmp::probe::RigTap& tap = rig.tap();
            t.gr.insert(t.gr.end(), tap.grDb.begin(), tap.grDb.end());
            t.det.insert(t.det.end(), tap.detDb.begin(), tap.detDb.end());
            t.tgt.insert(t.tgt.end(), tap.tgtDb.begin(), tap.tgtDb.end());
            rig.tap().clear();
        }
        rig.setTapping(false);
        return t;
    }

    float laneOf(simd::f32x4 v, int ln)
    {
        alignas(16) float t[4];
        simd::store(t, v);
        return t[ln & 3];
    }

    // ---- A. staticGr against the running kernel ---------------------------------------------------------------------
    void staticGrRows(Probe& P, const ModeEntry& en, const Config& c)
    {
        const EngineParams& e = c.res.eng;
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        const double w = static_cast<double>(std::max(0.0f, e.kneeDb));
        const double lo = t - w - 30.0, hi = t + 50.0;
        std::vector<float> in(static_cast<std::size_t>(kGridLevels) * static_cast<std::size_t>(kGridHold));
        for (int l = 0; l < kGridLevels; ++l)
        {
            const double level = lo + (hi - lo) * static_cast<double>(l) / static_cast<double>(kGridLevels - 1);
            const float a = linDb(static_cast<float>(level));
            for (int k = 0; k < kGridHold; ++k)
            {
                const std::int64_t n = static_cast<std::int64_t>(l) * kGridHold + k;
                in[static_cast<std::size_t>(n)] = squareAt(n, a, kFs);
            }
        }
        fcmp::probe::EngineRig rig(en, e, kFs);
        const Tapped tap = renderTapped(rig, in);

        const bool fb = e.topo == kTopoFB;
        const bool exactLink = fb || e.link == 0.0f || e.link == 1.0f;
        std::int64_t mismatch = 0, linkedMismatch = 0;
        std::uint32_t linkedUlp = 0;
        std::vector<float> xs(tap.det.size()), got(tap.det.size());
        for (int ln = 0; ln < 4; ++ln)
        {
            for (std::size_t i = 0; i < tap.det.size(); ++i)
                xs[i] = laneOf(tap.det[i], ln);
            analysis::staticGr(en, e, xs, got);
            for (std::size_t i = 0; i < got.size(); ++i)
            {
                const float want = laneOf(tap.tgt[i], ln);
                if (bitsOf(got[i]) == bitsOf(want))
                    continue;
                if (ln >= 2)
                    ++mismatch;
                else
                {
                    ++linkedMismatch;
                    const auto a = static_cast<std::int64_t>(bitsOf(got[i]));
                    const auto b = static_cast<std::int64_t>(bitsOf(want));
                    linkedUlp = std::max(linkedUlp, static_cast<std::uint32_t>(a > b ? a - b : b - a));
                }
            }
        }
        std::vector<float> lane0(tap.det.size());
        for (std::size_t i = 0; i < tap.det.size(); ++i)
            lane0[i] = laneOf(tap.det[i], 0);
        std::sort(lane0.begin(), lane0.end());
        const auto distinct = static_cast<std::int64_t>(std::unique(lane0.begin(), lane0.end()) - lane0.begin());

        const std::string k = "analysis." + c.key + ".staticgr";
        std::printf("NOTE     %s: %s kernel, link %s, %zu samples x 4 lanes, %lld distinct detector levels "
                    "(%.4g ... %.4g dB)\n",
                    k.c_str(), fb ? "FB" : "FF", num(static_cast<double>(e.link)).c_str(), tap.det.size(),
                    static_cast<long long>(distinct), static_cast<double>(lane0.empty() ? 0.0f : lane0.front()),
                    static_cast<double>(lane0.empty() ? 0.0f : lane0.back()));
        P.ge(k + ".grid", static_cast<double>(distinct), kGridLevels);
        if (exactLink)
            P.eq(k + ".mismatch", mismatch + linkedMismatch, 0);
        else
        {
            P.eq(k + ".mismatch", mismatch, 0);
            std::printf("NOTE     %s lanes 0-1 (after link %s, which is not exactly 0 or 1): %lld sample(s) differ "
                        "from the unlinked curve by <= %u ulp (the link's rounding, not the computer's)\n",
                        k.c_str(), num(static_cast<double>(e.link)).c_str(), static_cast<long long>(linkedMismatch),
                        linkedUlp);
        }
    }

    // ---- B. the settled engine against staticGain -------------------------------------------------------------------
    void settledRows(Probe& P, const ModeEntry& en, const Config& c, bool judged)
    {
        RawParams raw = c.raw;
        raw[Pid::rel] = releaseAtLeast(c.res.view, 100.0f);
        const Resolution res = fcmp::probe::resolveRaw(en, raw);
        const EngineParams& e = res.eng;
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        const double tauA = static_cast<double>(e.atkTauMs) / 1000.0;
        const auto hold = static_cast<std::size_t>(std::min(5.0, std::max(0.3, 30.0 * tauA)) * kFs);
        fcmp::probe::EngineRig rig(en, e, kFs);
        std::vector<float> in(hold), yl(hold), yr(hold);
        double maxErr = 0.0, worstLevel = 0.0, grAt30 = 0.0;
        for (const double d : { -10.0, -3.0, 0.0, 3.0, 6.0, 10.0, 15.0, 20.0, 30.0 })
        {
            const float a = linDb(static_cast<float>(t + d));
            const auto n0 = static_cast<std::int64_t>(rig.sampleIndex());
            for (std::size_t k = 0; k < hold; ++k)
                in[k] = squareAt(n0 + static_cast<std::int64_t>(k), a, kFs);
            rig.setTapping(false);
            rig.process(in.data(), in.data(), yl.data(), yr.data(), hold - 1);
            rig.tap().clear();
            rig.setTapping(true);
            rig.process(in.data() + hold - 1, in.data() + hold - 1, yl.data(), yr.data(), 1);
            rig.setTapping(false);
            const float gr = laneOf(rig.tap().grDb.back(), 0);
            const float det = laneOf(rig.tap().detDb.back(), 0);
            const std::array<float, 1> x{ det - e.preGainDb };
            std::array<float, 1> g{};
            analysis::staticGain(en, e, x, g);
            const double want = static_cast<double>(e.preGainDb) - static_cast<double>(g[0]);
            const double err = std::fabs(static_cast<double>(gr) - want);
            if (err > maxErr)
            {
                maxErr = err;
                worstLevel = d;
            }
            grAt30 = static_cast<double>(gr);
        }
        const std::string k = "analysis." + c.key + ".settled_max_err_db";
        std::printf("NOTE     %s: %s kernel, release %s ms, hold %zu samples; worst at T%+g dB; GR at T+30 %.6g "
                    "dB\n",
                    k.c_str(), e.topo == kTopoFB ? "FB" : "FF", num(static_cast<double>(raw[Pid::rel])).c_str(), hold,
                    worstLevel, grAt30);
        if (judged)
            P.le(k, maxErr, kSettledTolDb);
        else
            std::printf("NOTE     %s = %s (<= %g: %s; not judged: the Mode's detector law is not peak, 03 §3.4)\n",
                        k.c_str(), num(maxErr).c_str(), kSettledTolDb, maxErr <= kSettledTolDb ? "PASS" : "MISS");
    }

    // ---- C. stepResponse against an engine render -------------------------------------------------------------------
    struct Burst
    {
        analysis::StepStimulus st;
        std::int64_t pre = 0, hi = 0, lo = 0;
        std::int64_t total() const { return pre + hi + lo; }
    };

    std::int64_t samplesOf(float seconds, float fs)
    {
        const double v = static_cast<double>(seconds) * static_cast<double>(fs) + 0.5;
        return v >= 1.0 ? static_cast<std::int64_t>(v) : 0;
    }

    Burst burstFor(const EngineParams& e, float loCapSec = kStepMaxLoSec)
    {
        Burst b;
        b.st.fs = kFs;
        b.st.hiSec = static_cast<float>(std::max(0.5, 10.0 * static_cast<double>(e.atkTauMs) / 1000.0));
        const double loSec = std::max(2.0, 10.0 * static_cast<double>(e.relTauMs) / 1000.0);
        b.st.loSec = std::min(loCapSec, static_cast<float>(loSec));
        b.pre = samplesOf(b.st.preSec, b.st.fs);
        b.hi = samplesOf(b.st.hiSec, b.st.fs);
        b.lo = samplesOf(b.st.loSec, b.st.fs);
        return b;
    }

    // The same burst as a plugin input for the Rig (Analysis.cpp's amplitudes and square).
    std::vector<float> burstInput(const EngineParams& e, const Burst& b)
    {
        const float aHi = linDb(e.thrDb + b.st.hiDbOverThr - e.preGainDb);
        const float aLo = linDb(e.thrDb - b.st.loDbUnderThr - e.preGainDb);
        std::vector<float> in(static_cast<std::size_t>(b.total()));
        for (std::int64_t i = 0; i < b.total(); ++i)
        {
            const float a = i < b.pre ? 0.0f : (i < b.pre + b.hi ? aHi : aLo);
            in[static_cast<std::size_t>(i)] = squareAt(i, a, b.st.fs);
        }
        return in;
    }

    std::int64_t mismatches(std::span<const float> a, std::span<const float> b)
    {
        std::int64_t n = a.size() == b.size() ? 0 : 1;
        const std::size_t m = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < m; ++i)
            n += bitsOf(a[i]) == bitsOf(b[i]) ? 0 : 1;
        return n;
    }

    struct StepResult
    {
        std::vector<float> full;
        double attackS = -1, releaseS = -1;
    };

    StepResult stepRows(Probe& P, const ModeEntry& en, const Config& c)
    {
        const ModeDescriptor& desc = *en.desc;
        const EngineParams& e = c.res.eng;
        const Burst b = burstFor(e);
        const std::string k = "analysis." + c.key + ".step";
        StepResult out;
        out.full.assign(static_cast<std::size_t>(b.total()), -1.0f);
        const int written = analysis::stepResponse(en, e, b.st, out.full);
        P.eq(k + ".written", written, b.total());

        // the engine render: the Rig, tapped (lane 0)
        fcmp::probe::EngineRig rig(en, e, kFs);
        const Tapped tap = renderTapped(rig, burstInput(e, b));
        std::vector<float> ref(tap.gr.size());
        for (std::size_t i = 0; i < ref.size(); ++i)
            ref[i] = laneOf(tap.gr[i], 0);
        P.eq(k + ".mismatch", mismatches(out.full, ref), 0);

        std::int64_t nonfinite = 0;
        double grMin = 0.0;
        for (const float v : out.full)
        {
            nonfinite += std::isfinite(v) ? 0 : 1;
            grMin = std::min(grMin, static_cast<double>(v));
        }
        P.eq(k + ".nonfinite", nonfinite, 0);
        P.ge(k + ".gr_min_db", grMin, 0.0);

        // measure() against D2's extraction on the engine render (same edges, same convention for from/to)
        const TimeLaw atkLaw = desc.attackSpec(c.res.view, e).law, relLaw = desc.releaseSpec(c.res.view, e).law;
        const analysis::TimeReadout ra = analysis::measure(out.full, b.st, atkLaw);
        const analysis::TimeReadout rr = analysis::measure(out.full, b.st, relLaw);
        const std::span<const float> r(ref);
        const auto a0 = static_cast<std::size_t>(b.pre), a1 = static_cast<std::size_t>(b.pre + b.hi);
        const auto end = static_cast<std::size_t>(b.total());
        const double wantA = measure::lawSeconds(r.subspan(a0, a1 - a0), ref[a0 - 1], ref[a1 - 1], kFs, atkLaw);
        const double wantR = measure::lawSeconds(r.subspan(a1, end - a1), ref[a1 - 1], ref[end - 1], kFs, relLaw);
        std::printf("NOTE     %s: GR %.6g dB at the end of the hi segment, %.6g dB at the end; attack %.9g s (law %d), "
                    "release %.9g s (law %d)\n",
                    k.c_str(), static_cast<double>(ref[a1 - 1]), static_cast<double>(ref[end - 1]),
                    static_cast<double>(ra.attackS), static_cast<int>(atkLaw), static_cast<double>(rr.releaseS),
                    static_cast<int>(relLaw));
        P.near(k + ".attack_vs_d2_s", static_cast<double>(ra.attackS), wantA, 1e-9, 1e-6);
        P.near(k + ".release_vs_d2_s", static_cast<double>(rr.releaseS), wantR, 1e-9, 1e-6);
        out.attackS = static_cast<double>(ra.attackS);
        out.releaseS = static_cast<double>(rr.releaseS);
        return out;
    }

    // The same burst through the plugin's own host (EngineHost at ECO without lookahead, tapped): 01 §7's
    // "bit-identical to the plugin rendering the same burst". Returns the mismatching samples (a short tap counts).
    std::int64_t hostMismatch(const ModeEntry& en, const EngineParams& e, std::span<const float> response)
    {
        const Burst b = burstFor(e);
        const std::vector<float> in = burstInput(e, b);
        HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = 512;
        cfg.quality = Quality::eco;
        cfg.budget = LookaheadBudget::off;
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = e;
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        std::vector<simd::f32x4> gr(in.size(), simd::set1(-1.0f));
        TestTap tap;
        tap.grDb = gr;
        host->setTap(&tap);
        std::vector<float> l(512), r(512);
        for (std::size_t off = 0; off < in.size(); off += l.size())
        {
            const std::size_t n = std::min(l.size(), in.size() - off);
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(off), n, l.begin());
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(off), n, r.begin());
            const float* ins[2] = { l.data(), r.data() };
            float* outs[2] = { l.data(), r.data() };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(n);
            host->process(io, bp);
        }
        host->setTap(nullptr);
        std::vector<float> lane0(gr.size());
        for (std::size_t i = 0; i < gr.size(); ++i)
            lane0[i] = laneOf(gr[i], 0);
        std::printf("NOTE     analysis.def.step.host: EngineHost (ECO, no lookahead) tapped %llu of %zu samples\n",
                    static_cast<unsigned long long>(tap.written), in.size());
        return mismatches(lane0, response) + (tap.written == in.size() ? 0 : 1);
    }

    // ---- D. localRatio ----------------------------------------------------------------------------------------------
    void ratioRows(Probe& P, const ModeEntry& en, const Config& c)
    {
        const EngineParams& e = c.res.eng;
        const std::string k = "analysis." + c.key + ".ratio";
        const double t = static_cast<double>(analysis::inputThresholdDb(e));
        const double w = static_cast<double>(std::max(0.0f, e.kneeDb));
        const bool fb = e.topo == kTopoFB;
        const double loop = static_cast<double>(stage::QuadKnee::loopGain(e.slope));
        const double over = (fb ? (1.0 + loop) * w / 2.0 : w / 2.0) + 20.0;
        const double wantInv = fb ? 1.0 / (1.0 + loop) : 1.0 - static_cast<double>(e.slope);
        const bool textbook = en.desc->family == CurveFamily::textbook;

        const auto xAbove = static_cast<float>(t + over);
        const std::array<float, 1> xa{ xAbove + 0.05f };
        std::array<float, 1> ga{};
        analysis::staticGain(en, e, xa, ga);
        const double grAbove = static_cast<double>(e.preGainDb) - static_cast<double>(ga[0]);
        const double range = static_cast<double>(std::min(e.rangeDb, kRangeOff));
        const double inv = 1.0 / static_cast<double>(analysis::localRatio(en, e, xAbove));
        const double below = static_cast<double>(analysis::localRatio(en, e, static_cast<float>(t - w / 2.0 - 10.0)));
        std::printf("NOTE     %s: 1/ratio %.9g at T+%.4g dB (declared %.9g), ratio %.9g at T-W/2-10\n", k.c_str(), inv,
                    over, wantInv, below);
        if (!textbook || (e.flags & kEngGrOff) != 0)
        {
            std::printf("NOTE     %s: not judged (%s)\n", k.c_str(),
                        textbook ? "GR OFF" : "curve family custom: no declared closed-form slope");
            return;
        }
        if (over > 90.0 || grAbove >= range - 0.5)
            std::printf("NOTE     %s.above_inv: skipped (%s)\n", k.c_str(),
                        over > 90.0 ? "the FB input-domain knee ends beyond +90 dB" : "the range clamps there");
        else
            P.near(k + ".above_inv", inv, wantInv, kRatioInvTol);
        P.near(k + ".below", below, 1.0, 1e-4);
    }

    // ---- E. side chain ----------------------------------------------------------------------------------------------
    std::vector<float> logFreqs(int n)
    {
        std::vector<float> hz(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            hz[static_cast<std::size_t>(i)] =
                static_cast<float>(20.0 * std::pow(1000.0, static_cast<double>(i) / static_cast<double>(n - 1)));
        return hz;
    }

    void scRows(Probe& P, const ModeEntry& en, const RawParams& base, const ParamView& view)
    {
        struct ScCfg
        {
            std::string key;
            RawParams raw;
        };
        std::vector<ScCfg> cfgs{ { "def", base } };
        if (live(view.spec[idx(Pid::schpf)]))
        {
            RawParams raw = base;
            raw[Pid::schpf] = nearest(view, Pid::schpf, 100.0f);
            cfgs.push_back({ "hpf", raw });
        }
        if (live(view.spec[idx(Pid::sce)]))
        {
            RawParams raw = base;
            raw[Pid::sce] = nearest(view, Pid::sce, 3.0f);
            cfgs.push_back({ "sce", raw });
        }
        const std::vector<float> hz = logFreqs(161);
        for (const ScCfg& c : cfgs)
        {
            const EngineParams e = fcmp::probe::resolveRaw(en, c.raw).eng;
            std::vector<float> mag(hz.size()), shape(hz.size());
            analysis::scResponse(en, e, kFs, hz, mag);
            en.scShapeDb(e, kFs, hz.data(), shape.data(), static_cast<int>(hz.size()));
            std::int64_t mismatch = 0, nonfinite = 0;
            for (std::size_t i = 0; i < hz.size(); ++i)
            {
                const float want = shape[i] + host::ScFilter::responseDb(e.scHpfHz, e.sceDbOct, kFs, hz[i]);
                mismatch += bitsOf(mag[i]) == bitsOf(want) ? 0 : 1;
                nonfinite += std::isfinite(mag[i]) ? 0 : 1;
            }
            const std::string k = "analysis.sc." + c.key;
            std::printf("NOTE     %s: schpf %s Hz, sce %s dB/oct\n", k.c_str(),
                        num(static_cast<double>(e.scHpfHz)).c_str(), num(static_cast<double>(e.sceDbOct)).c_str());
            P.eq(k + ".compose_mismatch", mismatch, 0);
            P.eq(k + ".nonfinite", nonfinite, 0);
            const std::array<float, 4> goldHz{ 20.0f, 100.0f, 1000.0f, 10000.0f };
            std::array<float, 4> goldMag{};
            analysis::scResponse(en, e, kFs, goldHz, goldMag);
            for (std::size_t i = 0; i < goldHz.size(); ++i)
                P.num(k + ".f" + fmtLabel(static_cast<double>(goldHz[i])) + "_db", static_cast<double>(goldMag[i]),
                      Tol::abs(0.01));
        }
    }

    // ---- F. colour: curve, describing function, harmonics -----------------------------------------------------------
    struct Voice
    {
        std::string key;
        RawParams raw;
    };

    std::vector<Voice> voicesOf(const RawParams& base, const ParamView& view)
    {
        std::vector<Voice> out;
        const ParamSpec* s = view.spec[idx(Pid::voice)];
        if (s != nullptr && s->kind == Kind::stepped && !s->steps.empty())
            for (const Step& st : s->steps)
            {
                RawParams raw = base;
                raw[Pid::voice] = st.plain;
                std::string key = stepKey(st.label);
                for (const Voice& v : out)
                    if (v.key == key)
                        key += "x";
                out.push_back({ key, raw });
            }
        else
            out.push_back({ "def", base });
        return out;
    }

    // The probe's own 64-point DFT of the entry's colourCurve (sig::sinTurns tables): h[k] as harmonicsDb defines it.
    std::array<double, 8> dftHarmonics(const ModeEntry& en, const EngineParams& e, float grDb, float amp)
    {
        constexpr int kN = 64;
        std::array<float, kN> x{}, y{};
        for (int i = 0; i < kN; ++i)
            x[static_cast<std::size_t>(i)] = static_cast<float>(static_cast<double>(amp) * sig::sinTurns(i / 64.0));
        en.colourCurve(e, grDb, x.data(), y.data(), kN);
        std::array<double, 9> mag{};
        for (int h = 1; h <= 8; ++h)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < kN; ++i)
            {
                const double turns = static_cast<double>(h * i) / 64.0;
                re += static_cast<double>(y[static_cast<std::size_t>(i)]) * sig::cosTurns(turns);
                im += static_cast<double>(y[static_cast<std::size_t>(i)]) * sig::sinTurns(turns);
            }
            mag[static_cast<std::size_t>(h)] = std::sqrt(re * re + im * im);
        }
        std::array<double, 8> out{};
        for (int h = 2; h <= 8; ++h)
            out[static_cast<std::size_t>(h - 1)] =
                mag[1] > 0.0 ? measure::dbFromAmplitude(std::max(mag[static_cast<std::size_t>(h)] / mag[1], 1e-12))
                             : -240.0;
        return out;
    }

    // The probe's own describing-function gain (dsp.static's colourDfDb, with the point's GR).
    double colourDfDb(const ModeEntry& en, const EngineParams& e, double amp, float grDb)
    {
        constexpr int kPoints = 64;
        std::vector<float> x(kPoints), y(kPoints);
        std::vector<double> s(kPoints);
        for (int i = 0; i < kPoints; ++i)
        {
            s[static_cast<std::size_t>(i)] = sig::sinTurns(static_cast<double>(i) / kPoints);
            x[static_cast<std::size_t>(i)] = static_cast<float>(amp * s[static_cast<std::size_t>(i)]);
        }
        en.colourCurve(e, grDb, x.data(), y.data(), kPoints);
        double acc = 0.0;
        for (int i = 0; i < kPoints; ++i)
            acc += static_cast<double>(y[static_cast<std::size_t>(i)]) * s[static_cast<std::size_t>(i)];
        return measure::dbFromAmplitude(std::max(std::fabs(2.0 * acc / (static_cast<double>(kPoints) * amp)), 1e-12));
    }

    double thdFrom(const std::array<float, 8>& h)
    {
        double p = 0.0;
        for (std::size_t k = 1; k < h.size(); ++k)
            p += std::pow(10.0, static_cast<double>(h[k]) / 10.0);
        return 10.0 * std::log10(std::max(p, 1e-24));
    }

    // H2...H5 of a -6 dBFS 1 kHz sine with no GR through the Rig at kVoiceFs (single-bin DFT over whole cycles), in dB
    // re the fundamental; the colour stage's input amplitude is returned in `ampOut`. Empty when there is GR.
    std::vector<double> engineHarmonics(const ModeEntry& en, const EngineParams& e, float& ampOut, double& grMax)
    {
        const auto hold = static_cast<std::size_t>(0.2f * kVoiceFs), win = static_cast<std::size_t>(0.1f * kVoiceFs);
        std::vector<float> in(hold + win), yl(hold + win), yr(hold + win);
        for (std::size_t k = 0; k < in.size(); ++k)
            in[k] = 0.5f * sig::sineAt(static_cast<std::int64_t>(k), 1000.0, kVoiceFs);
        fcmp::probe::EngineRig rig(en, e, kVoiceFs);
        rig.setTapping(true);
        rig.process(in.data(), in.data(), yl.data(), yr.data(), in.size());
        grMax = 0.0;
        for (const simd::f32x4& g : rig.tap().grDb)
            grMax = std::max(grMax, static_cast<double>(laneOf(g, 0)));
        ampOut = 0.5f * linDb(e.preGainDb);
        const std::span<const float> out = std::span<const float>(yl).subspan(hold);
        std::vector<double> h;
        double h1 = 0.0;
        for (int k = 1; k <= 5; ++k)
        {
            const measure::SingleBin bin(1000.0 * k, static_cast<double>(kVoiceFs), win);
            const double a = bin(out, static_cast<std::int64_t>(hold)).amplitude();
            if (k == 1)
                h1 = a;
            else
                h.push_back(measure::dbFromAmplitude(std::max(a / h1, 1e-12)));
        }
        return h;
    }

    void colourRows(Probe& P, fcmp::probe::Fidelity& F, const ModeEntry& en, const RawParams& base,
                    const ParamView& view)
    {
        std::vector<float> xs;
        for (int i = 0; i <= 120; ++i)
            xs.push_back(-1.5f + 0.025f * static_cast<float>(i));
        double dfErr = 0.0;
        for (const Voice& v : voicesOf(base, view))
        {
            const Resolution res = fcmp::probe::resolveRaw(en, v.raw);
            const EngineParams& e = res.eng;
            // the COLOUR view's curve is the entry's
            std::int64_t compose = 0;
            for (const float gr : { 0.0f, 6.0f })
            {
                std::vector<float> got(xs.size()), want(xs.size());
                analysis::colourCurve(en, e, gr, xs, got);
                en.colourCurve(e, gr, xs.data(), want.data(), static_cast<int>(xs.size()));
                compose += mismatches(got, want);
            }
            P.eq("analysis.colour." + v.key + ".compose_mismatch", compose, 0);

            // harmonics: contract, independent DFT, golden, and (fidelity) the engine
            const std::string k = "analysis.harm." + v.key;
            std::array<float, 8> h{};
            analysis::harmonicsDb(en, e, 0.0f, 0.5f, h);
            P.eq(k + ".h1_db", bitsOf(h[0]) == bitsOf(0.0f) ? 0 : 1, 0);
            std::int64_t nonfinite = 0;
            for (const float x : h)
                nonfinite += std::isfinite(x) ? 0 : 1;
            P.eq(k + ".nonfinite", nonfinite, 0);
            const std::array<double, 8> ref = dftHarmonics(en, e, 0.0f, 0.5f);
            double vsDft = 0.0;
            for (std::size_t i = 1; i < h.size(); ++i)
                if (ref[i] > -100.0 || static_cast<double>(h[i]) > -100.0)
                    vsDft = std::max(vsDft, std::fabs(static_cast<double>(h[i]) - ref[i]));
            P.le(k + ".vs_dft_db", vsDft, kHarmVsDftDb);
            // golden values floored at kGoldenFloorDb: below it the 64-point float DFT reads its own rounding
            const Tol gt = Tol::abs(fcmp::probe::tol::kThdGoldenAbsDb);
            P.num(k + ".h2_db", std::max(static_cast<double>(h[1]), kGoldenFloorDb), gt);
            P.num(k + ".h3_db", std::max(static_cast<double>(h[2]), kGoldenFloorDb), gt);
            P.num(k + ".thd_db", std::max(thdFrom(h), kGoldenFloorDb), gt);

            if (!en.desc->colourStatic || e.mix != 1.0f)
                std::printf("NOTE     %s.vs_engine_db: not measured (%s)\n", k.c_str(),
                            en.desc->colourStatic ? "mix is not 1" : "colourStatic == false: STATIC APPROXIMATION");
            else
            {
                RawParams quiet = v.raw;
                quiet[Pid::thr] = nearest(res.view, Pid::thr, 24.0f);     // no GR at -6 dBFS
                const EngineParams eq = fcmp::probe::resolveRaw(en, quiet).eng;
                float amp = 0.0f;
                double grMax = 0.0;
                const std::vector<double> meas = engineHarmonics(en, eq, amp, grMax);
                std::array<float, 8> pred{};
                analysis::harmonicsDb(en, eq, 0.0f, amp, pred);
                double err = 0.0;
                std::string row;
                for (std::size_t i = 0; i < meas.size(); ++i)
                {
                    const auto p = static_cast<double>(pred[i + 1]);
                    row += " H" + std::to_string(i + 2) + " " + num(meas[i]) + "/" + num(p);
                    if (p > -70.0 || meas[i] > -70.0)
                        err = std::max(err, std::fabs(meas[i] - p));
                }
                std::printf("NOTE     %s: engine/static dB re H1 at amp %.6g (192 kHz):%s\n", k.c_str(),
                            static_cast<double>(amp), row.c_str());
                if (grMax > 0.0)
                    std::printf("NOTE     %s.vs_engine_db: not judged (the quiet configuration still reduces gain: "
                                "%.4g dB)\n",
                                k.c_str(), grMax);
                else
                    F.le(k + ".vs_engine_db", err, kHarmVsEngineDb);
            }

            // the describing function staticGain adds (CurveOpts::colour) against the probe's own
            const double t = static_cast<double>(analysis::inputThresholdDb(e));
            const double peakOffset = fcmp::probe::peakOffsetDb(en, e);
            std::vector<float> x, g0(21), g1(21);
            for (int i = 0; i <= 20; ++i)
                x.push_back(static_cast<float>(t - 20.0 + 2.5 * i));
            analysis::staticGain(en, e, x, g0);
            analysis::CurveOpts col;
            col.colour = true;
            analysis::staticGain(en, e, x, g1, col);
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const float gr = e.preGainDb - g0[i];
                const double ampDb = static_cast<double>(x[i]) + peakOffset + static_cast<double>(e.preGainDb)
                                   - static_cast<double>(gr);
                const double amp = measure::amplitudeFromDb(ampDb);
                const double want = colourDfDb(en, e, amp, gr);
                dfErr = std::max(dfErr, std::fabs(static_cast<double>(g1[i]) - static_cast<double>(g0[i]) - want));
            }
        }
        P.le("analysis.colourdf.max_err_db", dfErr, kColourDfTolDb);
    }

    // ---- F2. the static stage-2 hook (S10, X10) --------------------------------------------------------------------
    // A probe-local shared-element stage 2 (SharedElementMax's shape, 01 §5.2 catalogue, E §3.3): a hard-knee limiter at
    // the stage-2 threshold with slope kS2Slope on the detector level, through SmoothBranching ballistics at the stage-2
    // times, run on the aux lanes (Stage2Policy: lanes 2-3 carry channels 0-1's stage-2 GR); the element's GR is the
    // max of the two stages. combineStatic is its settled form: max(r1, the limiter's target), every lane.
    struct ProbeSharedMax
    {
        static constexpr float kS2Slope = 0.9f;                   // 10:1
        using Bal = stage::SmoothBranching;
        struct Coeffs
        {
            Bal::Coeffs bal{};
        };
        struct State
        {
            Bal::State bal{};
        };

        static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
        {
            EngineParams q = p;
            q.atkTauMs = p.s2AtkTauMs;
            q.relTauMs = p.s2RelTauMs;
            Bal::design(c.bal, q, x);
        }
        static simd::f32x4 target(simd::f32x4 xDb, const LevelCtl& l) noexcept FCDSP_NONBLOCKING
        {
            return simd::mul(simd::set1(kS2Slope), simd::max(simd::set1(0.0f), simd::sub(xDb, l.s2ThrDb)));
        }
        static simd::f32x4 dup01(simd::f32x4 v) noexcept FCDSP_NONBLOCKING   // {v0, v1, v0, v1}
        {
            return simd::withLane<3>(simd::withLane<2>(v, simd::lane<0>(v)), simd::lane<1>(v));
        }
        static simd::f32x4 combine(const Coeffs& c, State& s, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l)
            noexcept FCDSP_NONBLOCKING
        {
            const simd::f32x4 r2 = Bal::tick(c.bal, s.bal, target(dup01(xDb), l));   // lanes 2-3: channels 0-1
            const float a = simd::lane<2>(r2), b = simd::lane<3>(r2);
            const float r0 = simd::lane<0>(r1), r1b = simd::lane<1>(r1);
            alignas(16) const float v[4] = { r0 > a ? r0 : a, r1b > b ? r1b : b, a, b };
            return simd::load(v);
        }
        static simd::f32x4 combineStatic(const Coeffs&, simd::f32x4 r1, simd::f32x4 xDb, const LevelCtl& l) noexcept
            FCDSP_NONBLOCKING
        {
            return simd::max(r1, target(xDb, l));
        }
        static void seed(State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING { Bal::seed(s.bal, dup01(v)); }
        static simd::f32x4 grDb(const State& s) noexcept FCDSP_NONBLOCKING
        {
            return simd::withLane<1>(simd::withLane<0>(simd::set1(0.0f), simd::lane<2>(s.bal.r)),
                                     simd::lane<3>(s.bal.r));
        }
    };
    static_assert(Stage2Policy<ProbeSharedMax> && HasCombineStatic<ProbeSharedMax>);

    struct SharedS2Traits
    {
        static constexpr const ModeDescriptor& desc = modes::kClean;
        using Detector   = stage::PeakLog;
        using Computer   = stage::QuadKnee;
        using Link       = stage::LinkMax;
        using Ballistics = stage::SmoothBranching;
        using Stage2     = ProbeSharedMax;
        using Colour     = stage::ColourNone;
        using ScShape    = stage::Flat;
        static constexpr uint8_t kTopologies = (1u << kTopoFF) | (1u << kTopoFB);
    };
    struct NoS2Traits : SharedS2Traits
    {
        using Stage2 = stage::NoStage2;
    };
    constexpr ModeEntry kSharedS2Entry = makeModeEntry<SharedS2Traits>();
    constexpr ModeEntry kNoS2Entry = makeModeEntry<NoS2Traits>();
    static_assert(sizeof(ModeEngine<SharedS2Traits>) <= kArenaBytes);
    // makeModeEntry fills ModeEntry::staticS2 only for a Stage2 with combineStatic (a compile-time fact)
    static_assert(kNoS2Entry.staticS2 == nullptr && kSharedS2Entry.staticS2 != nullptr);

    EngineParams sharedS2Params(std::uint8_t topo, float s2ThrDb)
    {
        const ModeEntry& clean = fcmp::probe::modeEntry("clean");
        EngineParams e = fcmp::probe::resolveRaw(clean, fcmp::probe::modeRaw(clean)).eng;
        e.preGainDb = 0.0f;
        e.thrDb = -30.0f;
        e.slope = 0.5f;                                     // 2:1
        e.kneeDb = 6.0f;
        e.rangeDb = kRangeOff;
        e.atkTauMs = 1.0f;
        e.relTauMs = 100.0f;
        e.holdMs = 0.0f;
        e.s2ThrDb = s2ThrDb;
        e.s2AtkTauMs = 1.0f;
        e.s2RelTauMs = 50.0f;
        e.link = 1.0f;
        e.mix = 1.0f;
        e.flags = 0;
        e.topo = topo;
        return e;
    }

    // Rows (per Mode, Mode-independent, as netgain's): the hook's identities and a settled engine against the curve.
    void stage2HookRows(Probe& P)
    {
        // NoStage2: no static form, so no entry pointer, and ModeEngine<T>::staticS2 is the identity, bit for bit
        {
            std::vector<float> x(37), r1(37), gr(37, -1.0f);
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                x[i] = -70.0f + 3.0f * static_cast<float>(i);
                r1[i] = 0.37f * static_cast<float>(i) + 0.001f;
            }
            const EngineParams e = sharedS2Params(kTopoFF, -20.0f);
            ModeEngine<NoS2Traits>::staticS2(e, x.data(), r1.data(), gr.data(), static_cast<int>(x.size()));
            P.eq("analysis.stage2.local.nostage2_identity_mismatch", mismatches(gr, r1), 0);

            // stage 2 off (s2ThrDb >= kS2Off, the settled s2On = 0): the computer alone, bit for bit
            const EngineParams off = sharedS2Params(kTopoFF, kS2Off);
            std::vector<float> a(x.size()), b(x.size());
            analysis::CurveOpts on;
            analysis::staticGr(kSharedS2Entry, off, x, a, on);
            analysis::staticGr(kSharedS2Entry, off, x, b);
            P.eq("analysis.stage2.local.off_mismatch", mismatches(a, b), 0);
        }
        // the settled engine against staticGain (stage 2 on): a rising staircase of square levels, FF and FB
        for (const std::uint8_t topo : { std::uint8_t{ kTopoFF }, std::uint8_t{ kTopoFB } })
        {
            const EngineParams e = sharedS2Params(topo, -20.0f);
            const auto hold = static_cast<std::size_t>(0.3f * kFs);
            fcmp::probe::EngineRig rig(kSharedS2Entry, e, kFs);
            std::vector<float> in(hold), yl(hold), yr(hold);
            double maxErr = 0.0;
            std::int64_t s2Wins = 0, s1Wins = 0, bitMismatch = 0;
            for (const double d : { -10.0, -5.0, 0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0 })
            {
                const float a = linDb(static_cast<float>(static_cast<double>(e.thrDb) + d));
                const auto n0 = static_cast<std::int64_t>(rig.sampleIndex());
                for (std::size_t k = 0; k < hold; ++k)
                    in[k] = squareAt(n0 + static_cast<std::int64_t>(k), a, kFs);
                rig.setTapping(false);
                rig.process(in.data(), in.data(), yl.data(), yr.data(), hold - 1);
                rig.tap().clear();
                rig.setTapping(true);
                rig.process(in.data() + hold - 1, in.data() + hold - 1, yl.data(), yr.data(), 1);
                rig.setTapping(false);
                const float gr = laneOf(rig.tap().grDb.back(), 0);
                const float det = laneOf(rig.tap().detDb.back(), 0);
                const std::array<float, 1> xs{ det - e.preGainDb };
                std::array<float, 1> g{}, s1{}, s12{};
                analysis::staticGain(kSharedS2Entry, e, xs, g);
                const std::array<float, 1> xd{ det };
                analysis::staticGr(kSharedS2Entry, e, xd, s1);
                analysis::staticGr(kSharedS2Entry, e, xd, s12, analysis::CurveOpts{});
                const float want = e.preGainDb - g[0];
                maxErr = std::max(maxErr, std::fabs(static_cast<double>(gr) - static_cast<double>(want)));
                bitMismatch += bitsOf(gr) == bitsOf(s12[0]) ? 0 : 1;
                (s12[0] > s1[0] ? s2Wins : s1Wins) += 1;
            }
            const std::string k = std::string("analysis.stage2.local.") + (topo == kTopoFB ? "fb" : "ff");
            std::printf("NOTE     %s: settled engine vs staticGain(stage 2) max %.3g dB; stage 2 decides %lld of 10 "
                        "levels; %lld level(s) not bit-equal to staticGr(stage 2)\n",
                        k.c_str(), maxErr, static_cast<long long>(s2Wins), static_cast<long long>(bitMismatch));
            P.le(k + ".settled_max_err_db", maxErr, topo == kTopoFB ? 1e-4 : 1e-5);
            P.ge(k + ".stage2_decides", static_cast<double>(s2Wins), 1.0);
            P.ge(k + ".stage1_decides", static_cast<double>(s1Wins), 1.0);
        }
    }

    // ---- G. netGainDb -----------------------------------------------------------------------------------------------
    void netGainRows(Probe& P)
    {
        double mix1 = 0.0, mix0 = 0.0, mixHalf = 0.0;
        for (float g = -40.0f; g <= 0.0f; g += 2.5f)
            for (const float mk : { -6.0f, 0.0f, 3.5f, 12.0f })
            {
                mix1 = std::max(mix1, std::fabs(static_cast<double>(analysis::netGainDb(g, mk, 1.0f))
                                                - (static_cast<double>(g) + static_cast<double>(mk))));
                mix0 = std::max(mix0, std::fabs(static_cast<double>(analysis::netGainDb(g, mk, 0.0f))));
                const double w = std::pow(10.0, (static_cast<double>(g) + static_cast<double>(mk)) / 20.0);
                mixHalf = std::max(mixHalf, std::fabs(static_cast<double>(analysis::netGainDb(g, mk, 0.5f))
                                                      - 20.0 * std::log10(0.5 * w + 0.5)));
            }
        P.le("analysis.netgain.mix1_err_db", mix1, 1e-5);
        P.le("analysis.netgain.mix0_db", mix0, 1e-6);
        P.le("analysis.netgain.mix0p5_err_db", mixHalf, 1e-5);
        // mix 200 % with the wet path at exactly -6.0206 dB (w = 1/2): y = 2 w - 1 = 0, floored
        const float half = static_cast<float>(20.0 * std::log10(0.5));
        const double wet = std::pow(10.0, static_cast<double>(half) / 20.0);
        std::printf("NOTE     analysis.netgain: mix 2 at w = %.17g -> residual %.3g\n", wet, 2.0 * wet - 1.0);
        P.le("analysis.netgain.mix2_null_db", static_cast<double>(analysis::netGainDb(half, 0.0f, 2.0f)), -120.0);
    }

    // ---- H. FTZ inside every entry point ----------------------------------------------------------------------------
    // A render of the burst WITHOUT ScopedFtz (the probe's own driver, FZ cleared): what a missing scope would show.
    std::vector<float> bareRender(const ModeEntry& en, const EngineParams& e, const Burst& b)
    {
        const std::vector<float> in = burstInput(e, b);
        std::vector<float> out(in.size());
        int pow2 = 1;                                           // the host's per-slot scratch (01 §5.3)
        while (pow2 < lookaheadSamples(LookaheadBudget::ms20, static_cast<double>(kFs)) + kChunk)
            pow2 <<= 1;
        std::vector<float> scratch(4u * static_cast<std::size_t>(pow2), 0.0f);
        struct alignas(64) Arena
        {
            std::array<std::byte, kArenaBytes> bytes{};
        };
        auto arena = std::make_unique<Arena>();
        IEngine* engine = en.construct(arena->bytes.data());
        const float pg = linDb(e.preGainDb);
        {
            const ScopedNoFtz noFtz;
            engine->prepare(PrepareInfo{ kFs, 1, std::span<float>(scratch) });
            engine->setParams(e);
            engine->snapParams();
            alignas(16) std::array<simd::f32x4, kChunk> sc{}, gr{};
            for (std::size_t i0 = 0; i0 < in.size(); i0 += kChunkZ)
            {
                const int n = static_cast<int>(std::min(kChunkZ, in.size() - i0));
                for (int k = 0; k < n; ++k)
                    sc[static_cast<std::size_t>(k)] = simd::set1(in[i0 + static_cast<std::size_t>(k)] * pg);
                ControlIo io;
                io.n = n;
                io.sampleIndex = i0;
                io.sc = sc.data();
                io.grDb = gr.data();
                engine->control(io);
                for (int k = 0; k < n; ++k)
                    out[i0 + static_cast<std::size_t>(k)] = laneOf(gr[static_cast<std::size_t>(k)], 0);
            }
        }
        engine->~IEngine();
        return out;
    }

    void ftzRows(Probe& P, const ModeEntry& en, const RawParams& base, const ParamView& view)
    {
        const EngineParams e = fcmp::probe::resolveRaw(en, base).eng;
        std::int64_t mismatch = 0, modeChanged = 0;
        const auto call = [&](auto&& fn) {
            fn();                                               // under ProbeMain's FTZ
            const ScopedNoFtz noFtz;
            const std::uint64_t before = fpMode() & kModeBits;
            fn();
            modeChanged += (fpMode() & kModeBits) == before ? 0 : 1;
        };
        const std::vector<float> hz = logFreqs(31);
        std::vector<float> xs, xd;
        for (int i = 0; i <= 64; ++i)
        {
            xs.push_back(-80.0f + 1.5f * static_cast<float>(i));
            xd.push_back(xs.back() + e.preGainDb);
        }
        std::vector<float> a(xs.size()), b(xs.size());
        std::array<float, 8> ha{}, hb{};
        // each lambda writes `a` on its first call (under FTZ) and `b` on its second (FTZ off), then compares
        bool second = false;
        const auto dst = [&]() -> std::vector<float>& { return second ? b : a; };
        const auto check = [&](auto&& fn) {
            second = false;
            call([&]() {
                fn(dst());
                second = true;
            });
            mismatch += mismatches(a, b);
        };
        check([&](std::vector<float>& o) { analysis::staticGr(en, e, xd, o); });
        check([&](std::vector<float>& o) { analysis::staticGain(en, e, xs, o); });
        check([&](std::vector<float>& o) {
            analysis::CurveOpts col;
            col.colour = true;
            analysis::staticGain(en, e, xs, o, col);
        });
        check([&](std::vector<float>& o) {
            for (std::size_t i = 0; i < xs.size(); ++i)
                o[i] = analysis::localRatio(en, e, xs[i]);
        });
        check([&](std::vector<float>& o) {
            std::fill(o.begin(), o.end(), 0.0f);
            analysis::scResponse(en, e, kFs, hz, std::span<float>(o).first(hz.size()));
        });
        std::vector<float> xc(xs.size());       // denormal inputs (bit patterns: no arithmetic) and [-0.64, 0.64]
        for (std::size_t i = 0; i < xc.size(); ++i)
            xc[i] = i % 2 == 0 ? std::bit_cast<float>(static_cast<std::uint32_t>(0x00400000u + i))
                               : 0.02f * static_cast<float>(i) - 0.64f;
        check([&](std::vector<float>& o) { analysis::colourCurve(en, e, 6.0f, xc, o); });
        check([&](std::vector<float>& o) {
            for (std::size_t i = 0; i < xs.size(); ++i)
                o[i] = analysis::netGainDb(xs[i], 3.0f, 0.7f);
        });
        second = false;
        call([&]() {
            analysis::harmonicsDb(en, e, 0.0f, 0.5f, second ? hb : ha);
            second = true;
        });
        mismatch += mismatches(ha, hb);

        // stepResponse and measure into a release tail long enough to reach denormals
        RawParams fast = base;
        fast[Pid::rel] = timeEdge(view, Pid::rel, true);
        const EngineParams ef = fcmp::probe::resolveRaw(en, fast).eng;
        const auto hundredTau = static_cast<float>(100.0 * static_cast<double>(ef.relTauMs) / 1000.0);
        const float loSec = std::clamp(hundredTau, 2.0f, 40.0f);
        Burst burst = burstFor(ef, loSec);
        burst.st.loSec = loSec;
        burst.lo = samplesOf(loSec, burst.st.fs);
        std::vector<float> withFtz(static_cast<std::size_t>(burst.total())), withoutFtz(withFtz.size());
        std::array<analysis::TimeReadout, 2> tr{};
        second = false;
        call([&]() {
            std::vector<float>& o = second ? withoutFtz : withFtz;
            (void) analysis::stepResponse(en, ef, burst.st, o);
            tr[second ? 1 : 0] = analysis::measure(o, burst.st, TimeLaw::expDb);
            second = true;
        });
        const std::int64_t stepMismatch = mismatches(withFtz, withoutFtz);
        const bool sameReadout =
            bitsOf(tr[0].attackS) == bitsOf(tr[1].attackS) && bitsOf(tr[0].releaseS) == bitsOf(tr[1].releaseS);
        mismatch += sameReadout ? 0 : 1;

        // sensitivity: the same burst rendered WITHOUT a scope
        const std::vector<float> bare = bareRender(en, ef, burst);
        std::int64_t differ = 0, denormals = 0;
        for (std::size_t i = 0; i < bare.size(); ++i)
        {
            differ += bitsOf(bare[i]) == bitsOf(withFtz[i]) ? 0 : 1;
            denormals += bare[i] != 0.0f && std::fabs(bare[i]) < 1.17549435e-38f ? 1 : 0;
        }
        std::printf("NOTE     analysis.ftz: release %s ms for %.4g s; a render without ScopedFtz differs in %lld "
                    "sample(s) (%lld denormal GR value(s))%s\n",
                    num(static_cast<double>(fast[Pid::rel])).c_str(), static_cast<double>(loSec),
                    static_cast<long long>(differ), static_cast<long long>(denormals),
                    differ > 0 ? ": .step.mismatch can see a missing scope"
                               : ": this Mode's tail stays normal, so .step.mismatch is blind here");

        P.eq("analysis.ftz.mismatch", mismatch, 0);
        P.eq("analysis.ftz.step.mismatch", stepMismatch, 0);
        P.eq("analysis.ftz.caller_mode_kept", modeChanged == 0 ? 1 : 0, 1);
    }

    // ---- I. the live UI path (K2 #24) -------------------------------------------------------------------------------
    void overlayRows(Probe& P, const ModeEntry& en, const RawParams& base)
    {
        const Resolution res = fcmp::probe::resolveRaw(en, base);
        const int slot = slotOf(en);
        HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = 512;
        cfg.quality = Quality::eco;
        cfg.budget = LookaheadBudget::off;
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slot);
        bp.eng = res.eng;
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        host->setUiAttached(true);
        const float a = linDb(analysis::inputThresholdDb(res.eng) + 6.0f);
        std::vector<float> l(512), r(512);
        for (std::int64_t off = 0; off < static_cast<std::int64_t>(kFs); off += 512)
        {
            for (std::size_t k = 0; k < l.size(); ++k)
                l[k] = r[k] = a * sig::sineAt(off + static_cast<std::int64_t>(k), 1000.0, kFs);
            const float* ins[2] = { l.data(), r.data() };
            float* outs[2] = { l.data(), r.data() };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = 512;
            host->process(io, bp);
        }
        UiFrame f{};
        const bool read = host->readUiFrame(f);
        host->setUiAttached(false);
        P.eq("analysis.overlay.frame_read", read ? 1 : 0, 1);
        P.eq("analysis.overlay.slot", f.modeSlot, slot);

        EngineParams live = res.eng;                // resolve(raw).eng ...
        overlaySmoothed(f, live);                   // ... + the smoothed continuous fields
        EngineParams alone{};                       // what K2 #24 forbids: EngineParams from the frame alone
        overlaySmoothed(f, alone);
        std::vector<float> xs;
        const float t = analysis::inputThresholdDb(res.eng);
        for (int i = 0; i < 120; ++i)
            xs.push_back(t - 40.0f + 0.5f * static_cast<float>(i));
        std::vector<float> want(xs.size()), got(xs.size()), bad(xs.size());
        analysis::staticGain(en, res.eng, xs, want);
        analysis::staticGain(en, live, xs, got);
        analysis::staticGain(en, alone, xs, bad);
        std::int64_t mismatch = mismatches(got, want);
        const float ratioLive = analysis::localRatio(en, live, t + 10.0f);
        mismatch += bitsOf(ratioLive) == bitsOf(analysis::localRatio(en, res.eng, t + 10.0f)) ? 0 : 1;
        P.eq("analysis.overlay.curve_mismatch", mismatch, 0);
        double off = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i)
            off = std::max(off, std::fabs(static_cast<double>(bad[i]) - static_cast<double>(want[i])));
        std::printf("NOTE     analysis.overlay: a curve from the frame alone (no m[], topo or flags) is off by up to "
                    "%.4g dB here (%lld of %zu points differ)\n",
                    off, static_cast<long long>(mismatches(bad, want)), xs.size());
    }
} // namespace

FCMP_PROBE(dsp, analysis)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    fcmp::probe::Fidelity F(P, desc.provisional);
    const RawParams base = fcmp::probe::modeRaw(en);
    const Resolution def = fcmp::probe::resolveRaw(en, base);
    const EngineParams& e0 = def.eng;
    const bool peakLaw = desc.detectorLaw(e0) == DetectorLaw::peak;
    std::printf("NOTE     %.*s: detector law %d (%s), topology %s, stage2 kind %d, provisional %d\n",
                static_cast<int>(C.key.size()), C.key.data(),
                static_cast<int>(desc.detectorLaw(e0)), peakLaw ? "settled rows judged" : "settled rows are NOTEs",
                e0.topo == kTopoFB ? "FB" : "FF", static_cast<int>(desc.stage2), desc.provisional ? 1 : 0);

    StepResult defStep;
    for (const Config& c : configsOf(en, base))
    {
        staticGrRows(P, en, c);
        settledRows(P, en, c, peakLaw);
        StepResult s = stepRows(P, en, c);
        ratioRows(P, en, c);
        if (c.key == "def")
            defStep = std::move(s);
    }

    // stepResponse against the plugin's host, and its output contract: decimation and a short span
    {
        P.eq("analysis.def.step.host_mismatch", hostMismatch(en, e0, defStep.full), 0);
        const Burst b = burstFor(e0);
        analysis::StepStimulus st = b.st;
        st.decimate = 7;
        std::vector<float> dec(static_cast<std::size_t>((b.total() + 6) / 7), -1.0f);
        const int n7 = analysis::stepResponse(en, e0, st, dec);
        std::vector<float> every7;
        for (std::size_t i = 0; i < defStep.full.size(); i += 7)
            every7.push_back(defStep.full[i]);
        P.eq("analysis.def.step.decimate7.mismatch",
             mismatches(dec, every7) + (n7 == static_cast<int>(dec.size()) ? 0 : 1), 0);
        std::vector<float> part(defStep.full.size() / 3, -1.0f);
        const int np = analysis::stepResponse(en, e0, b.st, part);
        P.eq("analysis.def.step.truncated.mismatch",
             mismatches(part, std::span<const float>(defStep.full).first(part.size()))
                 + (np == static_cast<int>(part.size()) ? 0 : 1),
             0);
    }

    // GR OFF and stage 2 in staticGain
    {
        std::vector<float> xs, a(80), b(80);
        for (int i = 0; i < 80; ++i)
            xs.push_back(-70.0f + static_cast<float>(i));
        EngineParams off = e0;
        off.flags = static_cast<std::uint8_t>(off.flags | kEngGrOff);
        analysis::staticGain(en, off, xs, a);
        std::int64_t groff = 0;
        for (const float g : a)
            groff += bitsOf(g) == bitsOf(off.preGainDb) ? 0 : 1;
        P.eq("analysis.groff.gain_mismatch", groff, 0);
        // stage 2 (S10: ModeEntry::staticS2). Without a static stage 2 both settings draw the same curve; the CurveOpts
        // form of staticGr is the computer alone with stage2 = false, and the computer through staticS2 with it.
        analysis::CurveOpts s1;
        s1.stage2 = false;
        analysis::staticGain(en, e0, xs, a);
        analysis::staticGain(en, e0, xs, b, s1);
        if (en.staticS2 == nullptr)
            P.eq("analysis.stage2.mismatch", mismatches(a, b), 0);
        else
            std::printf("NOTE     analysis.stage2: the Mode draws a static stage 2 (ModeEntry::staticS2)\n");
        std::vector<float> xd(xs.size()), g4(xs.size()), gOff(xs.size()), gOn(xs.size()), gRef(xs.size());
        for (std::size_t i = 0; i < xs.size(); ++i)
            xd[i] = xs[i] + e0.preGainDb;
        analysis::staticGr(en, e0, xd, g4);
        analysis::staticGr(en, e0, xd, gOff, s1);
        analysis::staticGr(en, e0, xd, gOn, analysis::CurveOpts{});
        gRef = g4;
        if (en.staticS2 != nullptr)
            en.staticS2(e0, xd.data(), g4.data(), gRef.data(), static_cast<int>(xd.size()));
        P.eq("analysis.stage2.off_mismatch", mismatches(gOff, g4), 0);
        P.eq("analysis.stage2.compose_mismatch", mismatches(gOn, gRef), 0);
    }
    stage2HookRows(P);

    scRows(P, en, base, def.view);
    colourRows(P, F, en, base, def.view);
    netGainRows(P);
    ftzRows(P, en, base, def.view);
    overlayRows(P, en, base);

    // golden rows at the defaults
    {
        const float t = analysis::inputThresholdDb(e0);
        const std::array<float, 4> xs{ t - 10.0f, t, t + 10.0f, t + 20.0f };
        std::array<float, 4> g{};
        analysis::staticGain(en, e0, xs, g);
        const char* labels[] = { "x-10", "x0", "x10", "x20" };
        for (std::size_t i = 0; i < xs.size(); ++i)
            P.num(std::string("analysis.gain.") + labels[i] + "_db", static_cast<double>(g[i]), Tol::abs(0.001));
        P.num("analysis.invratio.x20", 1.0 / static_cast<double>(analysis::localRatio(en, e0, t + 20.0f)),
              Tol::abs(0.0001));
        P.num("analysis.step.attack_s", defStep.attackS, Tol::rel(0.01));
        P.num("analysis.step.release_s", defStep.releaseS, Tol::rel(0.01));
    }

    F.summary();
    return P.finish();
}
