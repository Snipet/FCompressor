// Tools/probes/common/Tolerances.h: the probe tolerance table (C §5.13 as adopted by 03 §3.7), indexed by the Mode's
// Rigor. Changing a number here is a lead-owned source change, never a bless.
//
// Rigor index: 0 = clean, 1 = modelled, 2 = character, the enumerator order of fcdsp::Rigor (01 §4.3). forRigor()
// takes that enum (or its integer) without including fcdsp, so this header compiles before any fcdsp header exists:
//
//   const auto& t = fcmp::probe::tol::forRigor(desc.rigor);
//   P.le("static.curve.max_err_db", err, t.curveOutsideKneeDb);
//
// Rows that are the same for every rigor ("same" in 03 §3.7) are the k* constants below the table.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

namespace fcmp::probe::tol
{
    // How the below-threshold null is judged (D8 (b), voice OFF).
    enum class NullRule : std::uint8_t
    {
        bitExact,       // bit-exact against the mix-0 reference of the same Quality
        belowDb,        // residual <= belowThresholdNullDb (dBFS)
        thdGolden       // no spec bound: the THD at -10 dBFS is a golden row, abs kThdGoldenAbsDb
    };

    struct RigorTolerances
    {
        const char* name;                   // "clean", "modelled", "character"
        double curveOutsideKneeDb;          // static curve error outside the knee
        double curveInsideKneeDb;           // static curve error inside the knee
        double ratioRel;                    // measured ratio against declared, relative
        bool   textbookApplies;             // the independent textbook formula check (family == textbook)
        double textbookDb;                  //   its bound, when it applies
        bool   tauBySpec;                   // true: |tau - spec| <= max(tauRel * tau, kTauMinSamples / fs_eff);
        double tauRel;                      //   false: tau within the descriptor's [lo, hi], plus golden rel:0.1
        double tauGoldenRel;                // golden tolerance of the t50/t90 rows (all rigors)
        double tauFsRatio;                  // |tau(fs) / tau(48k) - 1|
        double curveVs48kDb;                // static curve at fs against 48 kHz
        double tapGrDb;                     // tap GR against audio-derived GR
        double linkDb;                      // stereo link law
        NullRule belowThresholdNull;        // below-threshold null rule
        double belowThresholdNullDb;        //   its bound for NullRule::belowDb
    };

    inline constexpr std::array<RigorTolerances, 3> kByRigor{{
        //  name        out   in    ratio textbook      tauBySpec rel   gold  fs    48k   tap   link  null
        { "clean",     0.05, 0.10, 0.01, true,  0.001, true,  0.02, 0.1, 0.02, 0.02, 0.01, 0.01, NullRule::bitExact, 0.0 },
        { "modelled",  0.25, 0.35, 0.03, true,  0.001, true,  0.10, 0.1, 0.03, 0.05, 0.05, 0.05, NullRule::belowDb, -120.0 },
        { "character", 0.50, 0.75, 0.08, false, 0.0,   false, 0.0,  0.1, 0.05, 0.10, 0.10, 0.10, NullRule::thdGolden, 0.0 },
    }};
    inline constexpr int kRigorCount = static_cast<int>(kByRigor.size());

    // Rigor-independent rows (03 §3.7, "same").
    inline constexpr double kTauMinSamples = 1.5;               // tau floor, in samples of the effective rate
    inline constexpr double kTauAcrossQualitySamples = 1.5;     // tau at ECO/STD/HQ agree within, base samples
    inline constexpr double kClickHfRatioDb = 3.0;              // hf_ratio_db <= +3 (switch, zipper edge, bypass)
    inline constexpr double kMix0PassbandDb = 0.01;             // STD/HQ mix 0: passband flatness ...
    inline constexpr double kMix0PassbandTopHz = 20000.0;       //   ... up to 20 kHz (at 44.1 kHz)
    inline constexpr double kThdGoldenAbsDb = 0.5;              // character: THD golden abs:0.5
    inline constexpr double kCurvePx = 0.5;                     // curve on screen against the function
    inline constexpr double kChordPx = 0.25;                    //   and its chord error
    inline constexpr double kFbSolveDb = 1e-5;                  // FB solve against 200-step bisection
    inline constexpr int    kFbBisectionSteps = 200;
    inline constexpr std::int64_t kAllocationsInProcess = 0;    // allocations in process(): 0
    inline constexpr std::int64_t kModeSwitchParamWrites = 1;   // a Mode switch writes only `mode`
    inline constexpr std::int64_t kLatencyErrorSamples = 0;     // latency: exact

    // The row for a rigor index (0..2) or an fcdsp::Rigor value. Out of range throws (a harness error in ProbeMain).
    constexpr const RigorTolerances& forRigor(int rigor)
    {
        if (rigor < 0 || rigor >= kRigorCount)
            throw std::out_of_range("fcmp::probe::tol::forRigor: rigor index out of range");
        return kByRigor[static_cast<std::size_t>(rigor)];
    }

    template <class Enum>
        requires std::is_enum_v<Enum>
    constexpr const RigorTolerances& forRigor(Enum rigor)
    {
        return forRigor(static_cast<int>(rigor));
    }

    // The tau tolerance in seconds for a spec-judged rigor: max(tauRel * tau, kTauMinSamples / fsEff).
    constexpr double tauToleranceSeconds(const RigorTolerances& t, double tauSeconds, double fsEff) noexcept
    {
        const double rel = t.tauRel * (tauSeconds < 0 ? -tauSeconds : tauSeconds);
        const double minimum = kTauMinSamples / fsEff;
        return rel > minimum ? rel : minimum;
    }

    // The table tightens from clean to character, row by row.
    constexpr bool monotone() noexcept
    {
        for (int i = 1; i < kRigorCount; ++i)
        {
            const auto& a = kByRigor[static_cast<std::size_t>(i - 1)];
            const auto& b = kByRigor[static_cast<std::size_t>(i)];
            if (a.curveOutsideKneeDb > b.curveOutsideKneeDb || a.curveInsideKneeDb > b.curveInsideKneeDb
                || a.ratioRel > b.ratioRel || a.tauFsRatio > b.tauFsRatio || a.curveVs48kDb > b.curveVs48kDb
                || a.tapGrDb > b.tapGrDb || a.linkDb > b.linkDb)
                return false;
        }
        return true;
    }
    static_assert(monotone(), "Tolerances.h: a looser rigor must never have a tighter bound");
    static_assert(kByRigor[0].curveOutsideKneeDb == 0.05 && kByRigor[2].curveInsideKneeDb == 0.75, "C §5.13");

    // Wall-clock limits (a message-thread timer's latency, a CPU-cost ratio) are multiplied by FCMP_TIMING_SCALE: 1 when
    // unset, which is the lead's gate; CI sets 3 (ADR-87), since a shared cloud runner is slower and noisier than the
    // machine the limits were set on, and a timing row judges the design, not the runner. A value outside 1 … 10 is
    // ignored. Every other row is exact or has a numeric tolerance and never scales.
    inline double timingScale() noexcept
    {
        const char* s = std::getenv("FCMP_TIMING_SCALE");
        const double v = s != nullptr ? std::strtod(s, nullptr) : 1.0;
        return v >= 1.0 && v <= 10.0 ? v : 1.0;
    }
} // namespace fcmp::probe::tol
