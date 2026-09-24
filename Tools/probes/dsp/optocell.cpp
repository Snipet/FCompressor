// FCMP_PROBE layer=dsp name=optocell scope=global timeout=180
//
// dsp.optocell (M3, S9; SPRINTS S9.2; 01 §5.2-5.3, §10.6; E §2.6-2.7; D §2.2; K2 #1, #5c): the policies Opto 2A adds,
// proven on the policy functions (unit rows, 01 §8.4 step 3) and through the registered `opto-2a` engine (EngineRig,
// 48 kHz unless noted): the T4 cell's curve (OptoCellCurve, law::LdrShunt), the one-sample-delay loop and its runtime
// guard (FeedbackDelayed<OptoCellCurve>), the cell's ballistics (OptoCell: its FB maps, the per-sample rows dsp.static
// hands to this probe for a program-dependent Mode, and the program dependence the Mode sheet publishes), the EL
// panel's sensor (OptoSense), the R37 emphasis shelf (R37Shelf, and S9 lead revision 4: no double emphasis), and the
// tube/transformer voice (TubeTransformer). Spec rows only (no golden: the Mode-level goldens are dsp.static/time/...
// of `opto-2a`). References are double precision on the policies' own float rates.
//
// curve   (OptoCellCurve.h; <e> = the curve's exponent: s0p667, s0p9 (FF, the slopes), k2, k9 (FB, the loop gains))
//   optocell.curve.<e>.law_max_err_db       target() against r = 20 log10(1 + 10^(k (x - T) / 20)) in double over
//                                           x from T - 80 every 0.01 dB while r <= 60 dB: <= 1e-5
//   optocell.curve.<e>.decreases            samples where target() falls as x rises (every step of 0.01 dB): 0
//   optocell.curve.<e>.slope_max_err        slope() against the law's derivative k g / (1 + g): <= 1e-5 (relative to k)
//   optocell.curve.<e>.slope_over_k         max slope() / k: <= 1 (FeedbackDelayed's guard assumes it)
//   optocell.curve.<e>.at_threshold_db      r(T) = 20 log10 2: +-1e-5
//   optocell.curve.fb.k<k>.static_max_err_db  FeedbackDelayed<OptoCellCurve>::solveFb({0, 1}) (the static FB curve,
//                                           staticGr's) against 200-step bisection of r = r^_fb(x - r): <= 1e-5
//                                           (tol::kFbSolveDb); .zdf_mismatches: bit-identical to FeedbackZdf's
//   optocell.law.roundtrip_max_rel          LdrShunt::conductance(grDb(g)) against g, g in [1e-3, 1e3], relative to 1 + g
//                                           (the shunt's 1 + g is what the float carries): <= 2e-6
//   optocell.law.ldr_kohm_g1 / _dark        LdrShunt::ldrKohm: R_series at 6.0206 dB (g = 1), 1000 (dark) at 0 dB
// guard   (FeedbackDelayed.h, K2 #5c: "a 22.05 kHz stability row for any Mode on FeedbackDelayed")
//   optocell.guard.<fs>.<comp|limit>.limit  the bound alpha / (1 - alpha) at the Mode's open-loop attack: >= 99 (the
//                                           delayed loop runs at every loop gain the engine can make), 22.05-48 kHz
//   optocell.guard.delayed_max_err_db       within the bound, solveFb({A, B}) against A + B r^_fb(x - A / (1 - B)) in
//                                           double: <= 1e-5 (the naive loop E §2.6-2.7 prescribes for the cell)
//   optocell.guard.fallback_mismatches      a bound violated (a forced 20 us attack): solveFb is FeedbackZdf's, bit for
//                                           bit; .fallback_count >= 1
//   optocell.guard.22050.<cfg>.stable_ptp_db the engine at 22.05 kHz (COMP, LIMIT, and LIMIT with the forced attack, whose
//                                           every solve falls back): the settled GR of a T + 20 square over the last
//                                           50 ms <= 1e-4 dB peak to peak; .nonfinite 0; .gr_min_db >= 0
// cell    (OptoCell.h; through the engine, COMP and LIMIT; square steps T - 10 (0.1 s), T + 20 (3 s), T + 6 (0.35 s),
//          T - 20 (3 s), so the fast path attacks and releases and the slow part charges and then holds the tail)
//   optocell.fb.<cfg>.max_err_db            per sample, the tapped GR against the documented recurrence in double from the
//                                           tapped previous GR r_1 and detector level x: u = r^_fb(x - r_1) (the delayed
//                                           sense), r_f = alpha r_1 + (1 - alpha) u (attack where u > r_1), s <- s +
//                                           c (beta u - s) (charge where beta u > s, else the slow release), r = max(r_f,
//                                           s): <= 1e-5 dB (tol::kFbSolveDb); .<attack|release|slow>.count >= 1
//   optocell.program.<burst>.t50_s          a burst at T + 20 (COMP defaults), then T - 20: the release to 50 % in the
//                                           published program range [0.04, 0.08] s for every burst 0.1 ... 10 s
//   optocell.program.<burst>.full_s         NOTE: the release to 0.5 dB (the "then 1-15 s" of D §2.2); spec rows:
//                                           .full_nonmonotone 0 (it never shortens with a longer burst), 0.1 s ->
//                                           <= 0.5 s (a transient leaves no memory), 10 s -> [10, 16] s
//   optocell.program.partial_release.<fs>.residual_db  ADR-66's FB residual: a 0.3 s burst at T + 20, then T + 10 for
//                                           2 s: the settled GR against the static FB curve at T + 10, at 48 and 384 kHz
//                                           (the fast root is absolute, so it stops about ulp(r) / (2 c_f) short): <= 0.02
//   optocell.program.attack.<comp|limit>_s  the burst's attack (expDb, 63 %) in the published range [0.005, 0.020] s
//   optocell.program.memory.*               MEMORY after 5 s > after 0.5 s; b2 (the slow part holds) in the tail
//   optocell.carry.tail.mismatches          a carry taken while the slow part holds the GR (.held: b2), seeded into a
//                                           fresh engine: it continues within 2 ulp of the carried GR (lanes 0-1, 2 s;
//                                           the slow part's sub-ulp remainder is not in Carry, dsp.time's rule);
//                                           .aux_lanes: Carry::grDb lanes 2-3 are the slow parts (0 < s <= r)
//   optocell.carry.foreign.*                a carry from another Mode (aux lanes = its GR): the slow part starts charged
//                                           (s = r), so 0.5 s later the GR is still above half of it
// sense   (OptoSense.h)
//   optocell.sense.sine.<fs>.max_err_db     a 1 kHz sine of peak -6.02 dBFS read as its peak, settled: <= 0.01 dB
//   optocell.sense.dc_err_db                DC 0.25 reads 20 log10 0.25: <= 1e-4
//   optocell.sense.hold_samples             after a drop the level holds exactly round(5 ms fs) samples ...
//   optocell.sense.fall_db_per_ms           ... then falls at 20 log10(e) / 5 ms: +-1e-3 relative
// r37     (R37Shelf.h, D §2.2 [V S31])
//   optocell.r37.<fs>.e<E>.impl_max_err_db  the running filter's impulse response magnitude (65,536 samples, a double
//                                           DFT at 161 log frequencies 20 Hz-20 kHz) against magDb: <= 0.01 dB
//   optocell.r37.<fs>.e<E>.dc_db            magDb at 1 Hz: -E +-0.01; .hf_db at 20 kHz: > -0.1
//   optocell.r37.off.mismatches             E = 0: output bits == input bits
//   optocell.r37.emphasis.*                 S9 lead revision 4: EMPHASIS 10 resolves to m[0] = 10 dB and sceDbOct = 0
//                                           (.host_tilt), and analysis::scResponse is the R37 shelf alone, bit for bit
//                                           (.compose_mismatches)
// colour  (TubeTransformer.h)
//   optocell.tube.h2_db / .h3_db            harmonicsDb of a 0 dBFS sine at 0 dB of drive: H2 in [-65, -59], H3 <= -90
//   optocell.tube.square_max_db             the stage alone at 48 kHz on dsp.time's square as the wet path carries it
//                                           (+12 dBFS for 10 ms, the settled -2 dBFS, then 40 dB down): |20 log10(y / x)|
//                                           at every sample <= 0.08 dB (dsp.time's tap-vs-audio row: 0.1 dB)
//   optocell.tube.unity_db                  a -60 dBFS sine: the fundamental's gain within 1e-4 dB
//   optocell.tube.glide_step_ratio          a DRIVE move between two calls: on a constant input the output moves in
//                                           steps no larger than twice a uniform 64-sample glide's (a step would be 64x)
// internals
//   optocell.internals.*                    finite and inside the declared ranges after a burst and in silence (LDR 1000,
//                                           LIGHT 0)
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"
#include "Tolerances.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/OptoCell.h"
#include "fcdsp/engine/stages/colour/TubeTransformer.h"
#include "fcdsp/engine/stages/combinators/FeedbackDelayed.h"
#include "fcdsp/engine/stages/combinators/FeedbackZdf.h"
#include "fcdsp/engine/stages/detector/OptoSense.h"
#include "fcdsp/engine/stages/gain/OptoCellCurve.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/engine/stages/scshape/R37Shelf.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/opto-2a/Opto2A.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace st = fcdsp::stage;
    namespace tol = fcmp::probe::tol;
    namespace measure = fcmp::probe::measure;
    namespace sig = fcmp::probe::sig;
    using Opto = fcdsp::modes::Opto2A;

    constexpr float kFs = 48000.0f;
    constexpr float kComp = 0.6667f, kLimit = 0.90f;         // the RATIO steps' slopes (Opto2ADesc.cpp)

    std::string fsKey(float fs) { return std::to_string(static_cast<long>(fs)); }
    StageCtx ctxAt(float fs) { return StageCtx{ fs, fs, 1, {} }; }

    // The law in double: r = 20 log10(1 + 10^(k (x - T) / 20)), its slope, and the FB root by bisection.
    double lawDb(double x, double t, double k) { return 20.0 * std::log10(1.0 + std::pow(10.0, k * (x - t) / 20.0)); }
    double lawSlope(double x, double t, double k)
    {
        const double g = std::pow(10.0, k * (x - t) / 20.0);
        return k * g / (1.0 + g);
    }
    double fbRoot(double x, double t, double k, double a, double b)
    {
        double lo = a, hi = a + b * lawDb(x - a, t, k);
        for (int i = 0; i < tol::kFbBisectionSteps && hi > lo; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (mid - a - b * lawDb(x - mid, t, k) <= 0.0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    LevelCtl levelOf(float thr, float slope)
    {
        return LevelCtl{ simd::set1(thr), simd::set1(slope), simd::set1(6.0f), simd::set1(kS2Off) };
    }

    const ModeEntry& opto() { return fcmp::probe::modeEntry("opto-2a"); }

    // The Mode's resolved parameters at a ratio step (the defaults otherwise).
    EngineParams optoParams(float slope)
    {
        RawParams raw = fcmp::probe::modeRaw(opto());
        raw[Pid::ratio] = slope;
        return fcmp::probe::resolveRaw(opto(), raw).eng;
    }

    // A 1 kHz square (at 48 kHz: 24-sample half periods, |x| constant) at level segments relative to the input threshold.
    struct Seg
    {
        double overDb, seconds;
    };
    struct Run
    {
        std::vector<float> gr, gr1, det;
        std::vector<std::uint8_t> bits;
        std::vector<std::size_t> edges;
        std::int64_t nonfinite = 0;
    };
    Run runSquare(fcmp::probe::EngineRig& rig, std::span<const Seg> segs)
    {
        const EngineParams& e = rig.params();
        const double t = analysis::inputThresholdDb(e);
        const float fs = rig.fs();
        const std::size_t half = static_cast<std::size_t>(fs / 2000.0f + 0.5f);
        std::vector<float> amp;
        std::vector<std::size_t> len;
        Run run;
        std::size_t total = 0;
        for (const Seg& s : segs)
        {
            run.edges.push_back(total);
            amp.push_back(static_cast<float>(measure::amplitudeFromDb(t + s.overDb)));
            len.push_back(static_cast<std::size_t>(s.seconds * static_cast<double>(fs)));
            total += len.back();
        }
        constexpr std::size_t kBlock = 4096;
        std::vector<float> in(kBlock), yl(kBlock), yr(kBlock);
        rig.setTapping(true);
        rig.tap().clear();
        std::size_t si = 0, left = len.empty() ? 0 : len[0];
        for (std::size_t o = 0; o < total; o += kBlock)
        {
            const std::size_t m = std::min(kBlock, total - o);
            for (std::size_t k = 0; k < m; ++k)
            {
                while (left == 0 && si + 1 < len.size())
                    left = len[++si];
                --left;
                in[k] = ((o + k) / half) % 2 == 0 ? amp[si] : -amp[si];
            }
            rig.process(in.data(), in.data(), yl.data(), yr.data(), m);
            const std::vector<float> g0 = rig.tap().lane(rig.tap().grDb, 0), g1 = rig.tap().lane(rig.tap().grDb, 1);
            const std::vector<float> d0 = rig.tap().lane(rig.tap().detDb, 0);
            run.gr.insert(run.gr.end(), g0.begin(), g0.end());
            run.gr1.insert(run.gr1.end(), g1.begin(), g1.end());
            run.det.insert(run.det.end(), d0.begin(), d0.end());
            run.bits.insert(run.bits.end(), rig.tap().bits.begin(), rig.tap().bits.end());
            rig.tap().clear();
            for (std::size_t k = 0; k < m; ++k)
                run.nonfinite += (std::isfinite(yl[k]) ? 0 : 1) + (std::isfinite(yr[k]) ? 0 : 1);
        }
        rig.setTapping(false);
        return run;
    }

    // First time (s after `edge`) the trace is at or below `level`; -1 if never.
    double firstBelow(const std::vector<float>& gr, std::size_t edge, double level, float fs)
    {
        for (std::size_t i = edge; i < gr.size(); ++i)
            if (static_cast<double>(gr[i]) <= level)
                return static_cast<double>(i - edge + 1) / static_cast<double>(fs);
        return -1.0;
    }

    // |X(f)| of x (double DFT at one frequency, deterministic cosine).
    double dftMag(const std::vector<double>& x, double hz, double fs)
    {
        double re = 0.0, im = 0.0;
        const double step = hz / fs;
        for (std::size_t n = 0; n < x.size(); ++n)
        {
            const double turns = std::fmod(step * static_cast<double>(n), 1.0);
            re += x[n] * sig::cosTurns(turns);
            im -= x[n] * sig::sinTurns(turns);
        }
        return std::sqrt(re * re + im * im);
    }

    // ---- curve ------------------------------------------------------------------------------------------------------
    void curveRows(Probe& P)
    {
        const ScopedFtz ftz;
        constexpr float kThr = -12.0f;
        const st::OptoCellCurve::Coeffs c{};
        struct Law
        {
            const char* name;
            float k;
        };
        for (const Law& law : { Law{ "s0p667", kComp }, Law{ "s0p9", kLimit }, Law{ "k2", st::QuadKnee::loopGain(kComp) },
                                Law{ "k9", st::QuadKnee::loopGain(kLimit) } })
        {
            const float s = law.k;
            const std::string key = std::string("optocell.curve.") + law.name;
            const LevelCtl l = levelOf(kThr, s);
            double lawErr = 0.0, slopeErr = 0.0, slopeMax = 0.0;
            std::int64_t decreases = 0;
            float prev = -1.0f;
            for (int i = -8000; i <= 6000; ++i)                                // the curve's GR up to 60 dB
            {
                const float x = kThr + 0.01f * static_cast<float>(i);
                if (static_cast<double>(x - kThr) * static_cast<double>(s) > 60.0)
                    break;
                const float r = simd::lane<0>(st::OptoCellCurve::target(c, simd::set1(x), l));
                const float d = simd::lane<0>(st::OptoCellCurve::slope(c, simd::set1(x), l));
                const double xd = static_cast<double>(x);
                lawErr = std::max(lawErr, std::fabs(static_cast<double>(r) - lawDb(xd, kThr, static_cast<double>(s))));
                slopeErr = std::max(slopeErr, std::fabs(static_cast<double>(d) - lawSlope(xd, kThr, static_cast<double>(s)))
                                                  / static_cast<double>(s));
                slopeMax = std::max(slopeMax, static_cast<double>(d) / static_cast<double>(s));
                decreases += r < prev ? 1 : 0;
                prev = r;
            }
            const double atT = static_cast<double>(simd::lane<0>(st::OptoCellCurve::target(c, simd::set1(kThr), l)));
            P.le(key + ".law_max_err_db", lawErr, 1e-5);
            P.eq(key + ".decreases", decreases, 0);
            P.le(key + ".slope_max_err", slopeErr, 1e-5);
            P.le(key + ".slope_over_k", slopeMax, 1.0);
            P.near(key + ".at_threshold_db", atT, 20.0 * std::log10(2.0), 1e-5);
        }

        // the static FB curve (staticGr's): FeedbackDelayed<OptoCellCurve> with FbAffine{0, 1}, i.e. FeedbackZdf's root
        using FD = st::FeedbackDelayed<st::OptoCellCurve>;
        using FZ = st::FeedbackZdf<st::OptoCellCurve>;
        for (const float s : { kComp, kLimit })
        {
            const std::string key = std::string("optocell.curve.fb.k") + (s == kComp ? "2" : "9");
            FD::Coeffs fd{};
            EngineParams p = optoParams(s);
            FD::design(fd, p, ctxAt(kFs));
            const LevelCtl l = levelOf(kThr, s);
            const double k = static_cast<double>(st::QuadKnee::loopGain(s));
            double err = 0.0;
            std::int64_t zdfMismatch = 0;
            for (double x = kThr - 30.0; x <= kThr + 50.0 + 1e-9; x += 0.05)
            {
                const auto xf = static_cast<float>(x);
                const FbAffine a{ simd::set1(0.0f), simd::set1(1.0f) };
                const float r = simd::lane<0>(FD::solveFb(fd, simd::set1(xf), l, a));
                const float z = simd::lane<0>(FZ::solveFb(fd.zdf, simd::set1(xf), l, a));
                err = std::max(err, std::fabs(static_cast<double>(r) - fbRoot(static_cast<double>(xf), kThr, k, 0.0, 1.0)));
                zdfMismatch += r == z ? 0 : 1;
            }
            P.le(key + ".static_max_err_db", err, tol::kFbSolveDb);
            P.eq(key + ".zdf_mismatches", zdfMismatch, 0);
        }

        // law::LdrShunt
        {
            using Law = st::law::LdrShunt;
            double rel = 0.0;
            for (double lg = -3.0; lg <= 3.0 + 1e-9; lg += 0.01)
            {
                const auto g = static_cast<float>(std::pow(10.0, lg));
                const float back = simd::lane<0>(Law::conductance(Law::grDb(simd::set1(g))));
                rel = std::max(rel, std::fabs(static_cast<double>(back) - static_cast<double>(g))
                                        / (1.0 + static_cast<double>(g)));
            }
            P.le("optocell.law.roundtrip_max_rel", rel, 2e-6);
            P.near("optocell.law.ldr_kohm_g1", static_cast<double>(Law::ldrKohm(6.0205999f)),
                   static_cast<double>(Law::kSeriesKohm), 1e-3 * static_cast<double>(Law::kSeriesKohm));
            P.near("optocell.law.ldr_kohm_dark", static_cast<double>(Law::ldrKohm(0.0f)),
                   static_cast<double>(Law::kDarkKohm), 0.0);
        }
    }

    // ---- guard ------------------------------------------------------------------------------------------------------
    void guardRows(Probe& P)
    {
        using FD = st::FeedbackDelayed<st::OptoCellCurve>;
        using FZ = st::FeedbackZdf<st::OptoCellCurve>;
        const ScopedFtz ftz;
        for (const float fs : { 22050.0f, 44100.0f, 48000.0f })
            for (const float s : { kComp, kLimit })
            {
                FD::Coeffs fd{};
                FD::design(fd, optoParams(s), ctxAt(fs));
                const std::string key = "optocell.guard." + fsKey(fs) + (s == kComp ? ".comp" : ".limit");
                std::printf("NOTE     %s: bound k <= %.6g at the open-loop attack %.6g ms\n", key.c_str(),
                            static_cast<double>(fd.kLimit), static_cast<double>(optoParams(s).atkTauMs));
                P.ge(key + ".limit", static_cast<double>(fd.kLimit), static_cast<double>(st::QuadKnee::kMaxLoopGain));
            }

        // within the bound: the delayed formula; beyond it: FeedbackZdf's root, bit for bit
        sig::Pcg32 rng(9, 2);
        const auto uni = [&rng](double lo, double hi) {
            return lo + (hi - lo) * static_cast<double>(rng.next()) / 4294967296.0;
        };
        double delayedErr = 0.0;
        std::int64_t fallbackMismatch = 0, fallbacks = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const float fs = i % 2 == 0 ? 22050.0f : 48000.0f;
            const float s = i % 3 == 0 ? kLimit : kComp;
            EngineParams p = optoParams(s);
            const bool forced = i % 4 == 0;
            if (forced)
                p.atkTauMs = 0.02f;                                            // a 20 us attack: alpha / (1 - alpha) < 1
            FD::Coeffs fd{};
            FD::design(fd, p, ctxAt(fs));
            const auto thr = static_cast<float>(uni(-40.0, 0.0));
            const LevelCtl l = levelOf(thr, s);
            const auto x = static_cast<float>(uni(static_cast<double>(thr) - 20.0, static_cast<double>(thr) + 40.0));
            const auto r1 = static_cast<float>(uni(0.0, 30.0));
            const float c = oneMinusAlpha(static_cast<float>(uni(0.02, 5000.0)), fs);
            const FbAffine a{ simd::fms(simd::set1(r1), simd::set1(c), simd::set1(r1)), simd::set1(c) };
            const float got = simd::lane<0>(FD::solveFb(fd, simd::set1(x), l, a));
            if (!FD::delayedAt(fd, l, a))
            {
                ++fallbacks;
                fallbackMismatch += got == simd::lane<0>(FZ::solveFb(fd.zdf, simd::set1(x), l, a)) ? 0 : 1;
                continue;
            }
            const double ad = static_cast<double>(simd::lane<0>(a.A)), bd = static_cast<double>(c);
            const double held = ad / (1.0 - bd);
            const double want = ad + bd * lawDb(static_cast<double>(x) - held, static_cast<double>(thr),
                                                 static_cast<double>(st::QuadKnee::loopGain(s)));
            delayedErr = std::max(delayedErr, std::fabs(static_cast<double>(got) - want));
        }
        std::printf("NOTE     optocell.guard: %lld of 20000 solves fell back to FeedbackZdf (the forced 20 us attack)\n",
                    static_cast<long long>(fallbacks));
        P.le("optocell.guard.delayed_max_err_db", delayedErr, tol::kFbSolveDb);
        P.ge("optocell.guard.fallback_count", static_cast<double>(fallbacks), 1.0);
        P.eq("optocell.guard.fallback_mismatches", fallbackMismatch, 0);

        // the engine at 22.05 kHz: settles, finite, GR >= 0 (the delayed loop; and every solve on the ZDF fallback)
        struct Cfg
        {
            const char* name;
            float slope;
            bool forced;
        };
        for (const Cfg& cfg : { Cfg{ "comp", kComp, false }, Cfg{ "limit", kLimit, false },
                                Cfg{ "limit_fallback", kLimit, true } })
        {
            EngineParams e = optoParams(cfg.slope);
            if (cfg.forced)
                e.atkTauMs = 0.02f;
            fcmp::probe::EngineRig rig(opto(), e, 22050.0f);
            const Seg segs[] = { { -20.0, 0.2 }, { 20.0, 1.0 } };
            const Run run = runSquare(rig, segs);
            const std::size_t win = static_cast<std::size_t>(0.05 * 22050.0);
            const auto [lo, hi] = std::minmax_element(run.gr.end() - static_cast<std::ptrdiff_t>(win), run.gr.end());
            const std::string key = std::string("optocell.guard.22050.") + cfg.name;
            std::printf("NOTE     %s: settled GR %.6g dB\n", key.c_str(), static_cast<double>(run.gr.back()));
            P.le(key + ".stable_ptp_db", static_cast<double>(*hi) - static_cast<double>(*lo), 1e-4);
            P.eq(key + ".nonfinite", run.nonfinite, 0);
            P.ge(key + ".gr_min_db", static_cast<double>(*std::min_element(run.gr.begin(), run.gr.end())), 0.0);
        }
    }

    // ---- the cell's FB maps through the engine ----------------------------------------------------------------------
    void cellRows(Probe& P)
    {
        for (const float s : { kComp, kLimit })
        {
            const EngineParams e = optoParams(s);
            fcmp::probe::EngineRig rig(opto(), e, kFs);
            const Seg segs[] = { { -10.0, 0.1 }, { 20.0, 3.0 }, { 6.0, 0.35 }, { -20.0, 3.0 } };
            const Run run = runSquare(rig, segs);
            const double t = static_cast<double>(e.thrDb), k = static_cast<double>(st::QuadKnee::loopGain(e.slope));
            const double cA = static_cast<double>(oneMinusAlpha(e.atkTauMs, kFs));
            const double cR = static_cast<double>(oneMinusAlpha(e.relTauMs, kFs));
            const double cM = static_cast<double>(oneMinusAlpha(e.m[Opto::kChargeSlot], kFs));
            const double cS = static_cast<double>(oneMinusAlpha(e.m[Opto::kSlowSlot], kFs));
            const double beta = static_cast<double>(e.m[Opto::kShareSlot]);
            double err = 0.0, sl = 0.0, r1 = 0.0;
            std::int64_t atk = 0, rel = 0, slow = 0;
            for (std::size_t n = 0; n < run.gr.size(); ++n)
            {
                const double x = static_cast<double>(run.det[n]);
                const double u = lawDb(x - r1, t, k);
                const bool attack = u > r1;
                const double c = attack ? cA : cR;
                const double fast = (1.0 - c) * r1 + c * u;
                sl += (beta * u > sl ? cM : cS) * (beta * u - sl);
                const double r = std::max(fast, sl);
                sl = std::min(sl, r);
                err = std::max(err, std::fabs(static_cast<double>(run.gr[n]) - r));
                (sl >= fast && r > 0.0 ? slow : (attack ? atk : rel)) += 1;
                r1 = static_cast<double>(run.gr[n]);
            }
            const std::string key = std::string("optocell.fb.") + (s == kComp ? "comp" : "limit");
            std::printf("NOTE     %s: %lld attack, %lld fast release, %lld slow-held samples\n", key.c_str(),
                        static_cast<long long>(atk), static_cast<long long>(rel), static_cast<long long>(slow));
            P.le(key + ".max_err_db", err, tol::kFbSolveDb);
            P.ge(key + ".attack.count", static_cast<double>(atk), 1.0);
            P.ge(key + ".release.count", static_cast<double>(rel), 1.0);
            P.ge(key + ".slow.count", static_cast<double>(slow), 1.0);
        }
    }

    // ---- the program dependence (docs/modes/opto-2a.md) -------------------------------------------------------------
    void programRows(Probe& P)
    {
        const EngineParams e = optoParams(kComp);
        double prevFull = -1.0;
        std::int64_t nonmonotone = 0;
        float memShort = 0.0f, memLong = 0.0f;
        bool tailB2 = false;
        for (const double burst : { 0.1, 0.5, 1.0, 5.0, 10.0 })
        {
            fcmp::probe::EngineRig rig(opto(), e, kFs);
            const Seg segs[] = { { -20.0, 0.2 }, { 20.0, burst }, { -20.0, 20.0 } };
            const Run run = runSquare(rig, segs);
            const std::size_t edge = run.edges[2];
            const double r0 = static_cast<double>(run.gr[edge - 1]);
            const double t50 = firstBelow(run.gr, edge, 0.5 * r0, kFs), full = firstBelow(run.gr, edge, 0.5, kFs);
            char b[16];
            std::snprintf(b, sizeof b, "%g", burst);
            std::string bk(b);
            std::replace(bk.begin(), bk.end(), '.', 'p');
            const std::string key = "optocell.program.b" + bk;
            std::printf("NOTE     %s: burst %g s at %.4g dB of GR: 50 %% after %.4g s, 0.5 dB after %.4g s, 10 %% after "
                        "%.4g s\n",
                        key.c_str(), burst, r0, t50, full, firstBelow(run.gr, edge, 0.1 * r0, kFs));
            P.in(key + ".t50_s", t50, 0.04, 0.08);
            nonmonotone += full < prevFull ? 1 : 0;
            prevFull = full;
            if (burst == 0.1)
                P.le(key + ".full_s", full, 0.5);
            if (burst == 10.0)
                P.in(key + ".full_s", full, 10.0, 16.0);
            for (std::size_t i = edge + static_cast<std::size_t>(0.5 * kFs); i < edge + static_cast<std::size_t>(kFs); ++i)
                tailB2 = tailB2 || (burst == 10.0 && (run.bits[i] & 4u) != 0);

            // MEMORY at the end of the burst (a fresh run up to the edge)
            fcmp::probe::EngineRig m(opto(), e, kFs);
            const Seg on[] = { { -20.0, 0.2 }, { 20.0, burst } };
            (void) runSquare(m, on);
            float w[kInternals]{};
            m.engine().internals(w);
            if (burst == 0.5)
                memShort = w[3];
            if (burst == 5.0)
                memLong = w[3];
        }
        P.eq("optocell.program.full_nonmonotone", nonmonotone, 0);
        std::printf("NOTE     optocell.program.memory: MEMORY %.4g %% after 0.5 s, %.4g %% after 5 s\n",
                    static_cast<double>(memShort), static_cast<double>(memLong));
        P.eq("optocell.program.memory.grows", memLong > memShort ? 1 : 0, 1);
        P.eq("optocell.program.memory.tail_b2", tailB2 ? 1 : 0, 1);

        // the fast path's partial release (ADR-66: an FB root is absolute, so a release toward a non-zero light stops
        // within about ulp(r) / (2 c_f (1 + k L)) of it): a short T + 20 burst (the slow part stays under the new level),
        // then T + 10 for 2 s, the settled GR against the static FB curve at T + 10
        for (const float fs : { 48000.0f, 384000.0f })
        {
            fcmp::probe::EngineRig rig(opto(), e, fs);
            const Seg segs[] = { { -20.0, 0.2 }, { 20.0, 0.3 }, { 10.0, 2.0 } };
            const Run run = runSquare(rig, segs);
            const float x = static_cast<float>(analysis::inputThresholdDb(e) + 10.0) + e.preGainDb;
            float want = 0.0f;
            opto().staticGr(e, &x, &want, 1);
            const double dev = static_cast<double>(run.gr.back()) - static_cast<double>(want);
            const std::string key = "optocell.program.partial_release." + fsKey(fs) + ".residual_db";
            std::printf("NOTE     %s: settled %.7g dB, the static FB curve %.7g dB (%+.3g dB)\n", key.c_str(),
                        static_cast<double>(run.gr.back()), static_cast<double>(want), dev);
            P.le(key, std::fabs(dev), 0.02);
        }

        // the attack (expDb, 63 %) of a 0.5 s burst, COMP and LIMIT
        for (const float s : { kComp, kLimit })
        {
            fcmp::probe::EngineRig rig(opto(), optoParams(s), kFs);
            const Seg segs[] = { { -20.0, 0.5 }, { 20.0, 0.5 } };
            const Run run = runSquare(rig, segs);
            const std::size_t a = run.edges[1];
            const std::span<const float> tr = std::span<const float>(run.gr).subspan(a);
            const double got = measure::lawSeconds(tr, run.gr[a - 1], run.gr.back(), kFs, TimeLaw::expDb);
            const std::string key = std::string("optocell.program.attack.") + (s == kComp ? "comp" : "limit") + "_s";
            std::printf("NOTE     %s: %.6g s (open-loop tau %.6g ms)\n", key.c_str(), got,
                        static_cast<double>(optoParams(s).atkTauMs));
            P.in(key, got, 0.005, 0.020);
        }
    }

    // ---- carry ------------------------------------------------------------------------------------------------------
    void carryRows(Probe& P)
    {
        const EngineParams e = optoParams(kComp);
        const Seg first[] = { { -20.0, 0.2 }, { 20.0, 8.0 }, { -20.0, 1.0 } };          // the slow part holds by now
        fcmp::probe::EngineRig a(opto(), e, kFs);
        const Run ra = runSquare(a, first);
        const Carry c = a.engine().carry();
        const std::uint8_t lastBits = ra.bits.back();
        fcmp::probe::EngineRig b(opto(), e, kFs);
        b.engine().seed(c);
        const Seg next[] = { { -20.0, 1.0 }, { 10.0, 0.5 }, { -20.0, 0.5 } };
        const Run ca = runSquare(a, next), cb = runSquare(b, next);
        // within 2 ulp of the carried GR: the slow part's sub-ulp remainder is not in Carry (time.carry.seamless's rule)
        const float carried = std::max(simd::lane<0>(c.grDb), simd::lane<1>(c.grDb));
        const double ulp2 = 2.0 * static_cast<double>(std::nextafter(carried, HUGE_VALF) - carried);
        std::int64_t mismatches = 0;
        for (std::size_t i = 0; i < ca.gr.size(); ++i)
            mismatches += std::fabs(static_cast<double>(ca.gr[i]) - static_cast<double>(cb.gr[i])) <= ulp2
                                  && std::fabs(static_cast<double>(ca.gr1[i]) - static_cast<double>(cb.gr1[i])) <= ulp2
                              ? 0
                              : 1;
        std::printf("NOTE     optocell.carry.tail: GR %.6g dB, slow part %.6g dB handed over (b2 %d)\n",
                    static_cast<double>(simd::lane<0>(c.grDb)), static_cast<double>(simd::lane<2>(c.grDb)),
                    (lastBits & 4u) != 0 ? 1 : 0);
        P.eq("optocell.carry.tail.held", (lastBits & 4u) != 0 ? 1 : 0, 1);
        P.eq("optocell.carry.tail.mismatches", mismatches, 0);
        P.eq("optocell.carry.tail.aux_lanes",
             simd::lane<2>(c.grDb) > 0.0f && simd::lane<2>(c.grDb) <= simd::lane<0>(c.grDb) ? 1 : 0, 1);

        // a carry from another Mode: its aux lanes are its GR, so the slow part starts charged
        Carry foreign;
        foreign.grDb = simd::set1(6.0f);
        foreign.detDb = simd::set1(-40.0f);
        foreign.valid = 1;
        fcmp::probe::EngineRig f(opto(), e, kFs);
        f.engine().seed(foreign);
        const Carry back = f.engine().carry();
        const Seg quiet[] = { { -20.0, 0.5 } };
        const Run rf = runSquare(f, quiet);
        std::printf("NOTE     optocell.carry.foreign: 6 dB seeded, %.6g dB after 0.5 s\n",
                    static_cast<double>(rf.gr.back()));
        P.eq("optocell.carry.foreign.slow_charged", simd::lane<2>(back.grDb) == 6.0f ? 1 : 0, 1);
        P.ge("optocell.carry.foreign.held_db", static_cast<double>(rf.gr.back()), 3.0);
    }

    // ---- OptoSense --------------------------------------------------------------------------------------------------
    void senseRows(Probe& P)
    {
        using D = st::OptoSense;
        const ScopedFtz ftz;
        for (const float fs : { 44100.0f, 48000.0f, 96000.0f })
        {
            D::Coeffs c{};
            D::design(c, EngineParams{}, ctxAt(fs));
            D::State s{};
            D::seed(s, simd::set1(-240.0f));
            const auto n = static_cast<std::size_t>(0.04f * fs);
            double err = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const float v = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, fs, 0.5);
                const float l = simd::lane<0>(D::tick(c, s, simd::set1(v)));
                if (i >= n / 2)
                    err = std::max(err, std::fabs(static_cast<double>(l) - measure::dbFromAmplitude(0.5)));
            }
            P.le("optocell.sense.sine." + fsKey(fs) + ".max_err_db", err, 0.01);
        }
        D::Coeffs c{};
        D::design(c, EngineParams{}, ctxAt(kFs));
        D::State s{};
        D::seed(s, simd::set1(-240.0f));
        float l = 0.0f;
        for (int i = 0; i < 100; ++i)
            l = simd::lane<0>(D::tick(c, s, simd::set1(0.25f)));
        P.le("optocell.sense.dc_err_db", std::fabs(static_cast<double>(l) - measure::dbFromAmplitude(0.25)), 1e-4);
        // the drop: hold N samples, then fall at 20 log10(e) / kReleaseMs
        std::int64_t held = 0;
        float prev = l, afterHold = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            const float now = simd::lane<0>(D::tick(c, s, simd::set1(1e-6f)));
            if (now == prev && held == i)
                ++held;
            if (i == static_cast<int>(held) + 47)                          // 48 samples (1 ms) into the fall
                afterHold = now;
            prev = now;
        }
        const double wantHold = std::round(static_cast<double>(D::kHoldMs) * static_cast<double>(kFs) / 1000.0);
        const double fall = (static_cast<double>(l) - static_cast<double>(afterHold)) / 1.0;   // over 48 samples = 1 ms
        const double wantFall = 20.0 / std::log(10.0) / static_cast<double>(D::kReleaseMs);
        std::printf("NOTE     optocell.sense: hold %lld samples, then %.6g dB/ms\n", static_cast<long long>(held), fall);
        P.near("optocell.sense.hold_samples", static_cast<double>(held), wantHold, 0.0);
        P.near("optocell.sense.fall_db_per_ms", fall, wantFall, 0.0, 1e-3);
        // a 50 Hz sine's light ripple (NOTE: below 100 Hz the hold does not bridge a half-wave)
        {
            D::State r{};
            D::seed(r, simd::set1(-240.0f));
            float lo = 0.0f, hi = -300.0f;
            for (std::size_t i = 0; i < static_cast<std::size_t>(kFs); ++i)
            {
                const float v = sig::sineAt(static_cast<std::int64_t>(i), 50.0, kFs, 0.5);
                const float x = simd::lane<0>(D::tick(c, r, simd::set1(v)));
                if (i > static_cast<std::size_t>(kFs / 2))
                {
                    lo = std::min(lo == 0.0f ? x : lo, x);
                    hi = std::max(hi, x);
                }
            }
            std::printf("NOTE     optocell.sense.50hz: the light ripples %.4g dB peak to peak\n",
                        static_cast<double>(hi) - static_cast<double>(lo));
        }
    }

    // ---- R37 --------------------------------------------------------------------------------------------------------
    void r37Rows(Probe& P)
    {
        using S = st::R37Shelf;
        const ScopedFtz ftz;
        std::vector<double> hz;
        for (int i = 0; i <= 160; ++i)
            hz.push_back(20.0 * std::pow(1000.0, static_cast<double>(i) / 160.0));
        for (const float fs : { 44100.0f, 48000.0f })
            for (const float em : { 3.0f, 6.0f, 10.0f })
            {
                EngineParams p;
                p.m[Opto::kEmphasisSlot] = em;
                S::Coeffs c{};
                S::design(c, p, ctxAt(fs));
                S::State s{};
                std::vector<double> ir(65536);
                for (std::size_t n = 0; n < ir.size(); ++n)
                    ir[n] = static_cast<double>(simd::lane<0>(S::tick(c, s, simd::set1(n == 0 ? 1.0f : 0.0f))));
                double err = 0.0;
                for (const double f : hz)
                {
                    const double meas = 20.0 * std::log10(dftMag(ir, f, static_cast<double>(fs)));
                    err = std::max(err, std::fabs(meas - static_cast<double>(S::magDb(c, static_cast<float>(f), fs))));
                }
                char k[48];
                std::snprintf(k, sizeof k, "optocell.r37.%ld.e%d", static_cast<long>(fs), static_cast<int>(em));
                P.le(std::string(k) + ".impl_max_err_db", err, 0.01);
                P.near(std::string(k) + ".dc_db", static_cast<double>(S::magDb(c, 1.0f, fs)),
                       -static_cast<double>(em), 0.01);
                P.ge(std::string(k) + ".hf_db", static_cast<double>(S::magDb(c, 20000.0f, fs)), -0.1);
            }
        {
            S::Coeffs c{};
            S::design(c, EngineParams{}, ctxAt(kFs));
            S::State s{};
            sig::Pcg32 rng(3, 7);
            std::int64_t mismatches = 0;
            for (int i = 0; i < 4096; ++i)
            {
                const float v = static_cast<float>(static_cast<double>(rng.next()) / 4294967296.0 - 0.5);
                const float y = simd::lane<0>(S::tick(c, s, simd::set1(v)));
                mismatches += y == v ? 0 : 1;
            }
            P.eq("optocell.r37.off.mismatches", mismatches, 0);
        }
        // S9 lead revision 4: EMPHASIS reaches the R37 shelf only (the host's tilt stays neutral)
        {
            RawParams raw = fcmp::probe::modeRaw(opto());
            raw[Pid::sce] = 6.0f;                                              // the set-screw at 10
            const EngineParams e = fcmp::probe::resolveRaw(opto(), raw).eng;
            P.near("optocell.r37.emphasis.m0_db", static_cast<double>(e.m[Opto::kEmphasisSlot]), 10.0, 1e-5);
            P.eq("optocell.r37.emphasis.host_tilt", e.sceDbOct == 0.0f ? 1 : 0, 1);
            std::vector<float> f, a(161), b(161);
            for (const double h : hz)
                f.push_back(static_cast<float>(h));
            analysis::scResponse(opto(), e, kFs, f, a);
            opto().scShapeDb(e, kFs, f.data(), b.data(), static_cast<int>(f.size()));
            std::int64_t mismatches = 0;
            for (std::size_t i = 0; i < a.size(); ++i)
                mismatches += a[i] == b[i] ? 0 : 1;
            std::printf("NOTE     optocell.r37.emphasis: the detector path at 20 Hz %.4g dB, 317 Hz %.4g dB, 5 kHz %.4g dB\n",
                        static_cast<double>(a[0]), static_cast<double>(a[64]), static_cast<double>(a[128]));
            P.eq("optocell.r37.emphasis.compose_mismatches", mismatches, 0);
        }
    }

    // ---- TubeTransformer --------------------------------------------------------------------------------------------
    void tubeRows(Probe& P)
    {
        using T = st::TubeTransformer;
        {
            const EngineParams e = optoParams(kComp);
            float h[8]{};
            analysis::harmonicsDb(opto(), e, 0.0f, 1.0f, std::span<float, 8>(h));
            std::printf("NOTE     optocell.tube: a 0 dBFS sine at 0 dB of drive: H2 %.4g dB, H3 %.4g dB, H4 %.4g dB\n",
                        static_cast<double>(h[1]), static_cast<double>(h[2]), static_cast<double>(h[3]));
            P.in("optocell.tube.h2_db", static_cast<double>(h[1]), -65.0, -59.0);
            P.le("optocell.tube.h3_db", static_cast<double>(h[2]), -90.0);
        }
        const ScopedFtz ftz;
        EngineParams p;
        T::Coeffs c{};
        T::design(c, p, ctxAt(kFs));
        // dsp.time's square as the stage sees it: silence, the +12 dBFS onset before the cell attacks (10 ms), the level
        // the GR settles the wet path at (-2 dBFS, 40 ms), then 40 dB down; in 64-sample calls
        {
            T::State s{};
            std::vector<float> x(static_cast<std::size_t>(0.1f * kFs));
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const float a = i < 480 ? 0.0f
                                        : (i < 960 ? 3.98107171f : (i < 2880 ? 0.794328235f : 0.00794328235f));
                x[i] = ((i / 24) % 2 == 0 ? a : -a);
            }
            std::vector<float> y(x);
            for (std::size_t o = 0; o < y.size(); o += 64)
                T::process(c, s, y.data() + o, nullptr, static_cast<int>(std::min<std::size_t>(64, y.size() - o)), 0);
            double worst = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i)
                if (x[i] != 0.0f)
                    worst = std::max(worst, std::fabs(measure::dbFromAmplitude(std::fabs(static_cast<double>(y[i])
                                                                                          / static_cast<double>(x[i])))));
            P.le("optocell.tube.square_max_db", worst, 0.08);
        }
        // unity at small signals
        {
            T::State s{};
            const std::size_t n = static_cast<std::size_t>(0.1f * kFs);
            std::vector<float> x(n), y(n);
            for (std::size_t i = 0; i < n; ++i)
                x[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, 1e-3);
            y = x;
            for (std::size_t o = 0; o < n; o += 64)
                T::process(c, s, y.data() + o, nullptr, 64, 0);
            const measure::SingleBin bin(1000.0, kFs, n);
            P.le("optocell.tube.unity_db", std::fabs(bin.gainDb(x, y, 0)), 1e-4);
        }
        // DRIVE glides: design at 0 dB, then at +24 dB: the per-sample input scale never steps
        {
            T::State s{};
            std::vector<float> y(64, 0.5f);
            T::process(c, s, y.data(), nullptr, 64, 0);
            EngineParams q;
            q.driveDb = 24.0f;
            T::Coeffs c2 = c;
            for (int i = 0; i < 64; ++i)
                T::design(c2, q, ctxAt(kFs));                                   // several ticks of the drive smoother
            const float k0 = c.drive.k, k1 = c2.drive.k;
            // the glide: y = x + (A(u) - u - e) / k per sample with k linear from k0 to k1; a constant x shows k's steps
            std::fill(y.begin(), y.end(), 0.5f);
            T::process(c2, s, y.data(), nullptr, 64, 0);
            double worstStep = 0.0;
            for (std::size_t i = 1; i < y.size(); ++i)
                worstStep = std::max(worstStep, std::fabs(static_cast<double>(y[i]) - static_cast<double>(y[i - 1])));
            // a uniform glide moves the static output f(k x) / k from k0 to k1 in 64 equal steps
            const auto staticOut = [](float k) {
                const double u = 0.5 * static_cast<double>(k), t = std::tanh(u);
                return 0.5 + (t - u - static_cast<double>(T::kEven) * t * t) / static_cast<double>(k);
            };
            const double oneStep = (staticOut(k1) - staticOut(k0)) / 64.0;
            std::printf("NOTE     optocell.tube.glide: k %.6g -> %.6g, largest sample-to-sample change %.3g (a uniform "
                        "glide %.3g)\n",
                        static_cast<double>(k0), static_cast<double>(k1), worstStep, std::fabs(oneStep));
            P.le("optocell.tube.glide_step_ratio", worstStep / std::fabs(oneStep), 2.0);
        }
    }

    // ---- internals --------------------------------------------------------------------------------------------------
    void internalsRows(Probe& P)
    {
        const ModeDescriptor& d = *opto().desc;
        const EngineParams e = optoParams(kComp);
        fcmp::probe::EngineRig rig(opto(), e, kFs);
        const Seg on[] = { { -20.0, 0.2 }, { 20.0, 2.0 } };
        (void) runSquare(rig, on);
        float w[kInternals]{};
        rig.engine().internals(w);
        std::int64_t out = 0;
        for (std::size_t i = 0; i < d.internals.size(); ++i)
            out += std::isfinite(w[i]) && w[i] >= d.internals[i].lo && w[i] <= d.internals[i].hi ? 0 : 1;
        std::printf("NOTE     optocell.internals after 2 s at T + 20: LIGHT %.4g, G FAST %.4g, G SLOW %.4g, MEMORY %.4g %%, "
                    "LDR %.4g kOhm\n",
                    static_cast<double>(w[0]), static_cast<double>(w[1]), static_cast<double>(w[2]),
                    static_cast<double>(w[3]), static_cast<double>(w[4]));
        P.eq("optocell.internals.burst_out_of_range", out, 0);
        P.eq("optocell.internals.burst_lit", w[0] > 0.5f && w[1] > 0.0f && w[2] > 0.0f && w[3] > 0.0f ? 1 : 0, 1);
        fcmp::probe::EngineRig quiet(opto(), e, kFs);
        const Seg silence[] = { { -200.0, 0.5 } };
        (void) runSquare(quiet, silence);
        quiet.engine().internals(w);
        P.eq("optocell.internals.dark", w[0] == 0.0f && w[3] == 0.0f && w[4] == 1000.0f ? 1 : 0, 1);
    }
} // namespace

FCMP_PROBE(dsp, optocell)
{
    curveRows(P);
    guardRows(P);
    cellRows(P);
    programRows(P);
    carryRows(P);
    senseRows(P);
    r37Rows(P);
    tubeRows(P);
    internalsRows(P);
    return P.finish();
}
