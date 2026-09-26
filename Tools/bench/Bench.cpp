// Tools/bench/Bench.cpp: fcmp_bench, the CPU bench (F4, S3; C §5.8 "CPU bench"; E §3.7; 03 §3.9; S13 H1b). Not a gate:
// CTest runs it as bench.<key> (label `bench`, outside `verify`), and the lead runs it alone, never while agents build.
//
//   fcmp_bench [--mode <key>] [--quality eco|std|hq] [--rate <hz>] [--detached | --attached] [--budget] [--quick]
//              [--seconds <s>] [--reps <n>]
//
// For each registered Mode (or --mode <key>), fcdsp::EngineHost runs the Mode's defaults on a fixed program (seeded
// noise bursts over a triangle, generated before timing starts) at {48 kHz / 128, 96 kHz / 64, 192 kHz / 32} (rate /
// block) and every Quality, with the editor detached (the plugin's usual state) and attached (telemetry on). --quality,
// --rate, --detached and --attached narrow the grid (an A/B of two builds runs one row at a time). The time of the
// process() calls alone is measured with steady_clock; the best of --reps repetitions (5) after a warm-up is reported as
//   ns per base-rate sample per channel   (E §3.7's unit, the unit of ModeDescriptor::ctBudgetNsPerSample)
//   % of one core                          (the whole stereo stream in real time)
// and its ratio to the row's budget.
//
// Budgets (S13 H1b: the HQ rule). ctBudgetNsPerSample is E §3.7's STD figure: the whole engine (host, oversampler,
// control path, colour) per base-rate sample per channel at 2x. A row's budget is
//   ECO, STD   ctBudgetNsPerSample                      (ECO runs no oversampler: expected well below it)
//   HQ         ctBudgetNsPerSample + kHqAllowanceNs     (the 4x linear-phase FIR pair, and the gain, colour and mix at
//                                                        twice STD's rate: +15...+26 ns over STD for every Mode, S12
//                                                        and S13 benches; the allowance is Mode-independent because
//                                                        the cost it pays for is)
// for the editor detached AND attached: the attached cost (telemetry) is part of the budget.
// The BUDGET ROWS are the 48 kHz / 128 rows (E §3.7's reference; C §5.8's "48k/128" row): each must be <= 1.0x its
// budget. The 96 kHz / 64 and 192 kHz / 32 rows are reported with their ratio for information only: a 32-sample block
// pays the per-block work (parameter targets, the UiFrame, the Mode's internals) over a quarter of the samples.
// Every row must stay <= 3x its budget (C §5.8's loose ceiling).
//
// Output: one "bench ..." line per row, then per Mode "budget <key>: pass|OVER (worst budget row <r>x at <row>)".
// Exit: 0; with --budget, 1 when a budget row exceeds 1.0x or any row 3x its budget; 2 for a usage error or an unknown
// Mode. --quick (0.05 s, one repetition) is a smoke run: its numbers are not measurements.
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // The HQ rule (header comment): the allowance for the 4x FIR over a Mode's STD budget, ns per sample per channel.
    constexpr double kHqAllowanceNs = 30.0;
    constexpr double kBudgetRowRate = 48000.0;          // the budget rows: 48 kHz / 128
    constexpr double kLooseCeiling = 3.0;               // C §5.8

    struct Options
    {
        std::string mode;                               // empty: every registered Mode
        int quality = -1;                               // -1: every Quality
        double rate = 0.0;                              // 0: every rate
        int attached = -1;                              // -1: both; 0 detached; 1 attached
        bool budget = false, quick = false;
        double seconds = 2.0;
        int reps = 5;
    };

    struct Config
    {
        double fs;
        int block;
    };

    constexpr Config kConfigs[] = { { 48000.0, 128 }, { 96000.0, 64 }, { 192000.0, 32 } };

    struct QualityRow
    {
        fcdsp::Quality q;
        const char* name;
    };

    constexpr QualityRow kQualities[] = { { fcdsp::Quality::eco, "eco" }, { fcdsp::Quality::std, "std" },
                                          { fcdsp::Quality::hq, "hq" } };

    void usage(std::FILE* f)
    {
        std::fprintf(f, "usage: fcmp_bench [--mode <key>] [--quality eco|std|hq] [--rate <hz>] [--detached | "
                        "--attached] [--budget] [--quick] [--seconds <s>] [--reps <n>]\n");
    }

    // A row's budget, ns per base-rate sample per channel (header comment).
    double budgetNs(const fcdsp::ModeDescriptor& d, fcdsp::Quality q) noexcept
    {
        const double b = static_cast<double>(d.ctBudgetNsPerSample);
        return q == fcdsp::Quality::hq ? b + kHqAllowanceNs : b;
    }

    // A deterministic program (PCG32, no libm): a 110 Hz-ish triangle at -12 dBFS with noise bursts at -6 dBFS.
    struct Program
    {
        std::vector<float> l, r;
    };

    Program program(std::size_t n, double fs)
    {
        Program p;
        p.l.resize(n);
        p.r.resize(n);
        std::uint64_t state = 0x853c49e6748fea9bull;
        const auto next = [&state]() {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<float>(static_cast<std::uint32_t>(state >> 40)) * 0x1p-23f - 1.0f;   // [-1, 1)
        };
        const auto period = static_cast<std::size_t>(fs / 110.0);
        const auto burst = static_cast<std::size_t>(fs / 8.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const float ph = static_cast<float>(i % period) / static_cast<float>(period);
            const float tri = 4.0f * (ph < 0.5f ? ph : 1.0f - ph) - 1.0f;
            const bool loud = (i / burst) % 2 == 0;
            p.l[i] = 0.25f * tri + (loud ? 0.5f * next() : 0.0f);
            p.r[i] = 0.25f * tri + (loud ? 0.5f * next() : 0.0f);
        }
        return p;
    }

    fcdsp::BlockParams defaultsOf(const fcdsp::ModeEntry& en)
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = fcdsp::kHostParams[i].def;
        const int slot = fcdsp::slotOf(en);
        raw.modeSlot = static_cast<std::uint8_t>(slot < 0 ? 0 : slot);
        fcdsp::modeDefaults(*en.desc, raw);
        fcdsp::Resolution res;
        fcdsp::resolve(en, raw, res);
        fcdsp::BlockParams bp;
        bp.slot = raw.modeSlot;
        bp.eng = res.eng;
        return bp;
    }

    // Seconds spent in process() for one pass over the program.
    double timePass(fcdsp::EngineHost& host, const fcdsp::BlockParams& bp, const Program& in, std::vector<float>& ol,
                    std::vector<float>& orr, int block)
    {
        const std::size_t n = in.l.size();
        double spent = 0.0;
        for (std::size_t off = 0; off < n; off += static_cast<std::size_t>(block))
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(block), n - off);
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            float* outs[2] = { ol.data() + off, orr.data() + off };
            fcdsp::ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            const auto t0 = std::chrono::steady_clock::now();
            host.process(io, bp);
            const auto t1 = std::chrono::steady_clock::now();
            spent += std::chrono::duration<double>(t1 - t0).count();
        }
        return spent;
    }

    // Benchmarks one Mode; returns false when --budget would fail it (a budget row over 1.0x or any row over 3x).
    bool benchMode(const fcdsp::ModeSlot& ms, const Options& opt)
    {
        const fcdsp::ModeEntry& en = *ms.entry;
        const fcdsp::BlockParams bp = defaultsOf(en);
        const int reps = opt.quick ? 1 : opt.reps;
        const double seconds = opt.quick ? 0.05 : opt.seconds;
        const std::string key(ms.key);
        bool within = true;
        bool anyBudgetRow = false;
        double worst = 0.0;
        std::string worstRow;
        for (const QualityRow& qu : kQualities)
        {
            if (opt.quality >= 0 && static_cast<int>(qu.q) != opt.quality)
                continue;
            const double budget = budgetNs(*en.desc, qu.q);
            for (const Config& c : kConfigs)
            {
                if (opt.rate > 0.0 && c.fs != opt.rate)
                    continue;
                const auto n = static_cast<std::size_t>(seconds * c.fs);
                const Program in = program(n, c.fs);
                std::vector<float> ol(n), orr(n);
                for (const bool attached : { false, true })
                {
                    if (opt.attached >= 0 && attached != (opt.attached == 1))
                        continue;
                    auto host = std::make_unique<fcdsp::EngineHost>();
                    fcdsp::HostConfig cfg;
                    cfg.fs = c.fs;
                    cfg.maxBlock = c.block;
                    cfg.quality = qu.q;
                    cfg.budget = fcdsp::LookaheadBudget::off;
                    host->configure(cfg, bp);
                    if (attached)
                        host->setUiAttached(true);
                    if (!opt.quick)
                        (void) timePass(*host, bp, in, ol, orr, c.block);          // warm-up
                    double best = 1e300;
                    for (int r = 0; r < reps; ++r)
                        best = std::min(best, timePass(*host, bp, in, ol, orr, c.block));
                    const double nsPerSampleCh = best * 1e9 / (static_cast<double>(n) * 2.0);
                    const double core = 100.0 * best / seconds;
                    const double ratio = budget > 0.0 ? nsPerSampleCh / budget : 0.0;
                    const bool budgetRow = c.fs == kBudgetRowRate;
                    const char* verdict = ratio > kLooseCeiling ? "  OVER 3x"
                                        : (budgetRow && ratio > 1.0 ? "  OVER" : "");
                    std::printf("bench %-12s %-3s %6.0f/%-4d %-8s %8.3f ns/sample/ch  %6.3f %% of one core  budget "
                                "%g ns: %.3fx%s\n",
                                key.c_str(), qu.name, c.fs, c.block, attached ? "attached" : "detached",
                                nsPerSampleCh, core, budget, ratio, verdict);
                    std::fflush(stdout);
                    within = within && !(ratio > kLooseCeiling);
                    if (budgetRow)
                    {
                        anyBudgetRow = true;
                        within = within && !(ratio > 1.0);
                        if (ratio > worst)
                        {
                            worst = ratio;
                            worstRow = std::string(qu.name) + " " + (attached ? "attached" : "detached");
                        }
                    }
                }
            }
        }
        if (anyBudgetRow)
            std::printf("budget %-12s %s (worst budget row %.3fx at %s)\n", key.c_str(), within ? "pass" : "OVER",
                        worst, worstRow.c_str());
        return within;
    }

    bool parseQuality(std::string_view s, int& out) noexcept
    {
        for (const QualityRow& q : kQualities)
            if (s == q.name)
            {
                out = static_cast<int>(q.q);
                return true;
            }
        return false;
    }
} // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i] != nullptr ? argv[i] : "";
        const bool hasValue = i + 1 < argc && argv[i + 1] != nullptr;
        if (a == "--mode" && hasValue)
            opt.mode = argv[++i];
        else if (a == "--quality" && hasValue)
        {
            if (!parseQuality(argv[++i], opt.quality))
            {
                usage(stderr);
                return 2;
            }
        }
        else if (a == "--rate" && hasValue)
            opt.rate = std::strtod(argv[++i], nullptr);
        else if (a == "--detached")
            opt.attached = 0;
        else if (a == "--attached")
            opt.attached = 1;
        else if (a == "--budget")
            opt.budget = true;
        else if (a == "--quick")
            opt.quick = true;
        else if (a == "--seconds" && hasValue)
            opt.seconds = std::max(0.05, std::strtod(argv[++i], nullptr));
        else if (a == "--reps" && hasValue)
            opt.reps = std::max(1, std::atoi(argv[++i]));
        else if (a == "--help" || a == "-h")
        {
            usage(stdout);
            return 0;
        }
        else
        {
            usage(stderr);
            return 2;
        }
    }
    if (opt.rate > 0.0 && std::none_of(std::begin(kConfigs), std::end(kConfigs),
                                       [&](const Config& c) { return c.fs == opt.rate; }))
    {
        std::fprintf(stderr, "fcmp_bench: --rate must be one of 48000, 96000, 192000\n");
        return 2;
    }
    if (opt.quick)
        std::printf("bench: --quick smoke run (0.05 s, one repetition): the numbers are not measurements\n");

    bool within = true, any = false;
    for (const fcdsp::ModeSlot& ms : fcdsp::modeSlots())
    {
        if (!opt.mode.empty() && ms.key != opt.mode)
            continue;
        any = true;
        within = benchMode(ms, opt) && within;
    }
    if (!any)
    {
        std::fprintf(stderr, "fcmp_bench: no registered Mode '%s'\n", opt.mode.c_str());
        return 2;
    }
    return opt.budget && !within ? 1 : 0;
}
