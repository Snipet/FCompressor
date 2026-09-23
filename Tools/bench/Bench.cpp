// Tools/bench/Bench.cpp: fcmp_bench, the CPU bench (F4, S3; C §5.8 "CPU bench"; E §3.7; 03 §3.9). Not a gate: CTest
// runs it as bench.<key> (label `bench`, outside `verify`), and the lead runs it alone, never while agents build.
//
//   fcmp_bench [--mode <key>] [--budget] [--quick] [--seconds <s>]
//
// For each registered Mode (or --mode <key>), fcdsp::EngineHost runs the Mode's defaults on a fixed program (seeded
// noise bursts over a sine, generated before timing starts) at {48 kHz / 128, 96 kHz / 64, 192 kHz / 32} (rate /
// block), with the editor detached (the plugin's usual state) and attached (telemetry on). The time of the process()
// calls alone is measured with steady_clock; the best of 5 repetitions after a warm-up is reported as
//   ns per base-rate sample per channel   (E §3.7's unit, the unit of ModeDescriptor::ctBudgetNsPerSample)
//   % of one core                          (the whole stereo stream in real time)
// and its ratio to ctBudgetNsPerSample. Quality: ECO only until F7 installs the host's oversampler (STD/HQ rows then);
// the budget is E §3.7's STD figure, so ECO is expected well below it.
//
// Exit: 0; with --budget, 1 when a row exceeds 3 x ctBudgetNsPerSample (C §5.8); 2 for a usage error or an unknown
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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct Options
    {
        std::string mode;                   // empty: every registered Mode
        bool budget = false, quick = false;
        double seconds = 2.0;
    };

    struct Config
    {
        double fs;
        int block;
    };

    constexpr Config kConfigs[] = { { 48000.0, 128 }, { 96000.0, 64 }, { 192000.0, 32 } };

    void usage(std::FILE* f)
    {
        std::fprintf(f, "usage: fcmp_bench [--mode <key>] [--budget] [--quick] [--seconds <s>]\n");
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

    // Benchmarks one Mode; returns false when a row exceeds 3 x its budget.
    bool benchMode(const fcdsp::ModeSlot& ms, const Options& opt)
    {
        const fcdsp::ModeEntry& en = *ms.entry;
        const double budget = static_cast<double>(en.desc->ctBudgetNsPerSample);
        const fcdsp::BlockParams bp = defaultsOf(en);
        const int reps = opt.quick ? 1 : 5;
        const double seconds = opt.quick ? 0.05 : opt.seconds;
        bool within = true;
        for (const Config& c : kConfigs)
        {
            const auto n = static_cast<std::size_t>(seconds * c.fs);
            const Program in = program(n, c.fs);
            std::vector<float> ol(n), orr(n);
            for (const bool attached : { false, true })
            {
                auto host = std::make_unique<fcdsp::EngineHost>();
                fcdsp::HostConfig cfg;
                cfg.fs = c.fs;
                cfg.maxBlock = c.block;
                cfg.quality = fcdsp::Quality::eco;
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
                std::printf("bench %-12s eco %6.0f/%-4d %-8s %8.3f ns/sample/ch  %6.3f %% of one core  budget %g ns: "
                            "%.3fx%s\n",
                            std::string(ms.key).c_str(), c.fs, c.block, attached ? "attached" : "detached",
                            nsPerSampleCh, core, budget, ratio, ratio > 3.0 ? "  OVER 3x" : "");
                within = within && !(ratio > 3.0);
            }
        }
        std::printf("bench %-12s std/hq: n/a until F7 installs the host's oversampler\n", std::string(ms.key).c_str());
        return within;
    }
} // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i] != nullptr ? argv[i] : "";
        if (a == "--mode" && i + 1 < argc)
            opt.mode = argv[++i];
        else if (a == "--budget")
            opt.budget = true;
        else if (a == "--quick")
            opt.quick = true;
        else if (a == "--seconds" && i + 1 < argc)
            opt.seconds = std::max(0.05, std::strtod(argv[++i], nullptr));
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
