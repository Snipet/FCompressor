// Tools/web/simdbench.cpp: `fcmp_web_check simdbench` (F-W, WEB-A.1; ADR-93; no test): what fcdsp::simd's ops cost on
// this backend, in nanoseconds per 4-lane op, and log2/exp2/tanh per lane.
//
// Two numbers per op. "throughput": independent ops over arrays, what a feed-forward stage pays. "chain": each op takes
// the previous result, what a recurrence or a Horner chain pays (latency). On wasm the exact fma, the unfused pair and
// the unflushed ops are all measured whatever the build's FCOMPRESSOR_WEB_FMA is, so one run gives the comparison;
// log2/exp2/tanh use the fma the build was configured with.
//
// Every kernel is timed in short windows, round robin, and keeps its fastest window: a loaded machine (or a spell on
// an efficiency core) disturbs some rounds, not all. Under node, run with --no-liftoff to time optimised code from the
// first call (the default tiers up while the kernels warm up, which normally comes to the same).
#include "web/WebCheck.h"

#include "probes/common/Signals.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
    namespace simd = fcdsp::simd;
    using simd::f32x4;

    constexpr std::size_t kN = 1024;                        // floats per array: 256 ops per call, inside every L1
    constexpr int kRounds = 400;

    double nowNs() noexcept
    {
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // out[i] = op(a[i], b[i], c[i]): independent ops.
    template <class Op>
    [[gnu::noinline]] void across(Op op, const float* a, const float* b, const float* c, float* out) noexcept
    {
        for (std::size_t i = 0; i < kN; i += 4)
            simd::store(out + i, op(simd::load(a + i), simd::load(b + i), simd::load(c + i)));
    }
    // y = op(y, a[i], b[i]): every op waits for the one before. All four lanes of the result are stored, so the chain
    // stays a vector chain (returning one lane lets the compiler keep only that lane's scalar chain).
    template <class Op>
    [[gnu::noinline]] void along(Op op, const float* a, const float* b, const float* y0, float* out) noexcept
    {
        f32x4 y = simd::load(y0);
        for (std::size_t i = 0; i < kN; i += 4)
            y = op(y, simd::load(a + i), simd::load(b + i));
        simd::store(out, y);
    }

    struct Kernel
    {
        std::function<void()> run;
        double ops = 1;                                     // per call
        int calls = 1;                                      // per window
        double best = 1e30;                                 // ns per window
        double nsPerOp() const noexcept { return best / (static_cast<double>(calls) * ops); }
    };

    struct Bench
    {
        // a, b, c in +-[0.5, 2); small in +-0.01 and g in [0.90, 0.95) (a one-pole's input and coefficient, a sum's
        // terms); near1 in [0.975, 1.025) (a product that stays where it started)
        std::vector<float> a, b, c, g, small, near1, out;
        alignas(16) float seed[4] = { 0.25f, -0.25f, 0.125f, 0.5f };
        std::vector<Kernel> kernels;
        struct Row { std::string name; std::size_t first; bool chain; };
        std::vector<Row> rows;

        // `op3(x, y, z)` across a, b, c, and `chain(y, x, g)` along small (or near1, for a product) and g.
        template <class Op3, class Chain>
        void row(const char* name, Op3 op3, Chain chain, bool product = false)
        {
            rows.push_back({ name, kernels.size(), true });
            kernels.push_back({ [this, op3] { across(op3, a.data(), b.data(), c.data(), out.data()); },
                                static_cast<double>(kN / 4) });
            const float* x = product ? near1.data() : small.data();
            kernels.push_back({ [this, chain, x] { along(chain, x, g.data(), seed, out.data()); },
                                static_cast<double>(kN / 4) });
        }
        template <class F>
        void perLane(const char* name, const std::vector<float>& in, F f)
        {
            rows.push_back({ name, kernels.size(), false });
            const float* p = in.data();
            kernels.push_back({ [this, p, f]
                                {
                                    across([f](f32x4 x, f32x4, f32x4) noexcept { return f(x); }, p, p, p, out.data());
                                }, static_cast<double>(kN) });
        }

        void run()
        {
            for (Kernel& k : kernels)                       // warm up, and size a window to about 0.1 ms
                for (;;)
                {
                    const double t0 = nowNs();
                    for (int i = 0; i < k.calls; ++i)
                        k.run();
                    if (nowNs() - t0 > 1e5 || k.calls >= (1 << 20))
                        break;
                    k.calls *= 2;
                }
            for (int round = 0; round < kRounds; ++round)
                for (Kernel& k : kernels)
                {
                    const double t0 = nowNs();
                    for (int i = 0; i < k.calls; ++i)
                        k.run();
                    k.best = std::min(k.best, nowNs() - t0);
                }
            for (const Row& r : rows)
                if (r.chain)
                    std::printf("NOTE  simdbench %-26s %7.3f ns/op throughput   %7.3f ns/op chain\n", r.name.c_str(),
                                kernels[r.first].nsPerOp(), kernels[r.first + 1].nsPerOp());
                else
                    std::printf("NOTE  simdbench %-26s %7.3f ns/lane\n", r.name.c_str(), kernels[r.first].nsPerOp());
        }
    };
} // namespace

FCMP_WEB_COMMAND(simdbench)
{
    const fcdsp::ScopedFtz ftz;                             // the mode fcdsp runs in (an empty scope on wasm)
    Bench d;
    fcmp::probe::sig::Pcg32 rng(0xbe9c4u, 3u);
    const auto unit = [&] { return static_cast<float>(rng.next() >> 8) * 0x1p-24f; };        // [0, 1)
    const auto normal = [&]                                                                    // +-[0.5, 2)
    {
        return std::bit_cast<float>((rng.next() & 0x80000000u) | ((126u + (rng.next() & 1u)) << 23)
                                    | (rng.next() & 0x007fffffu));
    };
    std::vector<float> xLog(kN), xExp(kN), xTanh(kN);
    for (std::size_t i = 0; i < kN; ++i)
    {
        d.a.push_back(normal());
        d.b.push_back(normal());
        d.c.push_back(normal());
        d.g.push_back(0.90f + 0.05f * unit());
        d.small.push_back(0.02f * unit() - 0.01f);
        d.near1.push_back(0.975f + 0.05f * unit());
        xLog[i] = 1e-4f + unit();                           // levels
        xExp[i] = 40.0f * unit() - 30.0f;                   // log2 gains
        xTanh[i] = 6.0f * unit() - 3.0f;
    }
    d.out.resize(kN);

#if defined(FCDSP_SIMD_NEON)
    const char* backend = "arm64 NEON";
#elif defined(FCDSP_SIMD_WASM)
    const char* backend = "wasm32 SIMD128";
#else
    const char* backend = "x86-64 SSE";
#endif
#if defined(FCDSP_WASM_FMA_UNFUSED)
    const char* built = "UNFUSED";
#else
    const char* built = "exact";
#endif
    std::printf("NOTE  simdbench: %s, fma as built: %s; the fastest of %d windows per figure\n", backend, built,
                kRounds);

    // The chains: a sum of small terms, a product of factors near 1, a one-pole (x + y*g).
    d.row("add", [](f32x4 x, f32x4 y, f32x4) noexcept { return simd::add(x, y); },
          [](f32x4 y, f32x4 x, f32x4) noexcept { return simd::add(y, x); });
    d.row("mul", [](f32x4 x, f32x4 y, f32x4) noexcept { return simd::mul(x, y); },
          [](f32x4 y, f32x4 x, f32x4) noexcept { return simd::mul(y, x); }, true);
    d.row("fma (as built)", [](f32x4 x, f32x4 y, f32x4 z) noexcept { return simd::fma(x, y, z); },
          [](f32x4 y, f32x4 x, f32x4 g) noexcept { return simd::fma(x, y, g); });
#if defined(FCDSP_SIMD_WASM)
    namespace detail = simd::detail;
    d.row("fma exact", [](f32x4 x, f32x4 y, f32x4 z) noexcept { return detail::fmaExact(x, y, z); },
          [](f32x4 y, f32x4 x, f32x4 g) noexcept { return detail::fmaExact(x, y, g); });
    d.row("fma unfused (mul, add)", [](f32x4 x, f32x4 y, f32x4 z) noexcept { return detail::fmaUnfused(x, y, z); },
          [](f32x4 y, f32x4 x, f32x4 g) noexcept { return detail::fmaUnfused(x, y, g); });
    const auto rawAdd = [](f32x4 x, f32x4 y) noexcept
    {
        return detail::asF32(wasm_f32x4_add(detail::asBits(x), detail::asBits(y)));
    };
    const auto rawMul = [](f32x4 x, f32x4 y) noexcept
    {
        return detail::asF32(wasm_f32x4_mul(detail::asBits(x), detail::asBits(y)));
    };
    d.row("add, no flush", [=](f32x4 x, f32x4 y, f32x4) noexcept { return rawAdd(x, y); },
          [=](f32x4 y, f32x4 x, f32x4) noexcept { return rawAdd(y, x); });
    d.row("mul, no flush", [=](f32x4 x, f32x4 y, f32x4) noexcept { return rawMul(x, y); },
          [=](f32x4 y, f32x4 x, f32x4) noexcept { return rawMul(y, x); }, true);
    d.row("mul, add, no flush", [=](f32x4 x, f32x4 y, f32x4 z) noexcept { return rawAdd(x, rawMul(y, z)); },
          [=](f32x4 y, f32x4 x, f32x4 g) noexcept { return rawAdd(x, rawMul(y, g)); });
#else
    d.row("mul, add (unfused)", [](f32x4 x, f32x4 y, f32x4 z) noexcept { return simd::add(x, simd::mul(y, z)); },
          [](f32x4 y, f32x4 x, f32x4 g) noexcept { return simd::add(x, simd::mul(y, g)); });
#endif
    d.perLane("log2", xLog, [](f32x4 x) noexcept { return fcdsp::log2(x); });
    d.perLane("exp2", xExp, [](f32x4 x) noexcept { return fcdsp::exp2(x); });
    d.perLane("tanh", xTanh, [](f32x4 x) noexcept { return fcdsp::tanh(x); });
    d.run();
    return 0;
}
