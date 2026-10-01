// Source/editor/PreviewCompute.cpp — see PreviewCompute.h. U4 (S8) wrote the computation inside PreviewWorker.cpp; web
// Sprint B (ADR-93) moved it here unchanged, with a cancellation flag where it asked its juce::Thread.
//
// computePreview(), per run (02 §9.3; 01 §7; K2 #24):
// - The stimulus is 02 §9.3's (attack: +6 / +12 / +24 dB over the threshold for 1 s from 24 dB under it, then 0.2 s
//   under it; release: a +12 dB burst of 50 ms or 2 s, then 10 s at 12 dB under it). Run::stim records it with the
//   decimation that fits Run::gr (kMaxPoints).
// - analysis::stepResponse renders it ONCE at decimate 1 into the caller's scratch (1.25 s / 12.05 s of samples;
//   2.3 MB at 48 kHz for a release run). The render is causal and independent of the decimation, so Run::gr, taken as
//   every d-th sample of it, is bit-identical to stepResponse(stim) itself (ui.chars checks it).
// - analysis::measure runs on the full-resolution render, once per TimeLaw (Trace::measured; Run::measured = expDb):
//   at decimate 1 it equals dsp.time's extraction, and a decimated trace would move a 200 µs FET crossing by up to d
//   samples.
// - reduce() maps the render onto the pane's log-time columns (PreviewWorker.h, Trace).
// Every analysis entry point opens fcdsp::ScopedFtz itself (01 §7, K2 #24), so a worker thread computes exactly what
// the synchronous path and the probes compute. `cancel` is read between runs, never inside one.
#include "editor/PreviewCompute.h"

#include "editor/Layout.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/modes/Registry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace fcmp::ui
{
    namespace
    {
        static_assert(layout::kStepAttack.plot.w == static_cast<float>(PreviewWorker::kColumns)
                          && layout::kStepRelease.plot.w == static_cast<float>(PreviewWorker::kColumns),
                      "PreviewWorker::kColumns is one column per px of both STEP panes");
        static_assert(static_cast<int>(fcdsp::TimeLaw::rateDbPerS) + 1 == PreviewWorker::kTimeLaws,
                      "PreviewWorker::kTimeLaws counts every fcdsp::TimeLaw");

        // Every n-th sample so the run fits kMaxPoints.
        int decimationFor(const fcdsp::analysis::StepStimulus& s) noexcept
        {
            const double samples = static_cast<double>(s.preSec + s.hiSec + s.loSec) * static_cast<double>(s.fs);
            const double d = std::ceil(samples / static_cast<double>(PreviewWorker::kMaxPoints));
            return d < 1.0 ? 1 : static_cast<int>(d);
        }

        // 02 §9.3: attack runs step to +6 / +12 / +24 dB over the threshold for 1 s, from 24 dB under it, then 0.2 s
        // back under it.
        fcdsp::analysis::StepStimulus attackStimulus(float fs, int i) noexcept
        {
            fcdsp::analysis::StepStimulus s;
            s.fs = fs;
            s.hiDbOverThr = layout::chars::kAttackStepsDb[static_cast<std::size_t>(i)];
            s.hiSec = 1.0f;
            s.loDbUnderThr = 24.0f;
            s.loSec = 0.2f;
            s.decimate = decimationFor(s);
            return s;
        }

        // Release runs: a +12 dB burst of 50 ms or 2 s, then 10 s at 12 dB under the threshold.
        fcdsp::analysis::StepStimulus releaseStimulus(float fs, int i) noexcept
        {
            fcdsp::analysis::StepStimulus s;
            s.fs = fs;
            s.hiDbOverThr = 12.0f;
            s.hiSec = layout::chars::kReleaseBurstsS[static_cast<std::size_t>(i)];
            s.loSec = 10.0f;
            s.decimate = decimationFor(s);
            return s;
        }

        // Samples of a stimulus segment exactly as analysis::stepResponse counts them: round(seconds × fs), 0 below
        // one sample (Analysis.cpp segmentSamples).
        int64_t segmentSamples(float seconds, float fs) noexcept
        {
            const double v = static_cast<double>(seconds) * static_cast<double>(fs) + 0.5;
            return v >= 1.0 ? static_cast<int64_t>(v) : 0;
        }

        // Room for one full-resolution render of `s` (a few samples of slack over stepResponse's own count).
        std::size_t renderCapacity(const fcdsp::analysis::StepStimulus& s) noexcept
        {
            const int64_t n = segmentSamples(s.preSec, s.fs) + segmentSamples(s.hiSec, s.fs)
                            + segmentSamples(s.loSec, s.fs);
            return static_cast<std::size_t>(std::max<int64_t>(n, 0)) + 4u;
        }

        // The response of `g` (n samples) to the step at sample `edge`, on the log-time columns of `geom`, over the
        // segment [edge, end) (PreviewWorker.h, Trace).
        void reduce(const float* g, int64_t n, int64_t edge, int64_t end, double fs, const layout::StepGeom& geom,
                    PreviewWorker::Trace& out) noexcept
        {
            out.valid = false;
            const int64_t last = std::min(end, n) - 1;               // the segment's last written sample
            if (edge < 1 || last < edge || !(fs > 0.0))
                return;
            out.valid = true;
            out.from = g[edge - 1];
            out.to = g[last];
            const double from = static_cast<double>(out.from);

            // GR at time t after the edge: linear between (0, from) and the samples at (k + 1) / fs.
            const auto at = [&](double t) noexcept {
                const double pos = t * fs - 1.0;                     // fractional sample offset from the edge
                if (pos < 0.0)
                {
                    const double u = std::max(t * fs, 0.0);
                    return static_cast<float>(from + u * (static_cast<double>(g[edge]) - from));
                }
                const auto k = static_cast<int64_t>(pos);
                if (edge + k >= last)
                    return g[last];
                const double u = pos - static_cast<double>(k);
                const double a = static_cast<double>(g[edge + k]), b = static_cast<double>(g[edge + k + 1]);
                return static_cast<float>(a + u * (b - a));
            };

            const double t0 = static_cast<double>(geom.tMinS);
            const double span = static_cast<double>(geom.tMaxS) / t0;
            std::array<double, PreviewWorker::kColumns + 1> t{};
            for (int k = 0; k <= PreviewWorker::kColumns; ++k)
            {
                const auto u = static_cast<std::size_t>(k);
                t[u] = t0 * std::pow(span, static_cast<double>(k) / static_cast<double>(PreviewWorker::kColumns));
                out.edge[u] = at(t[u]);
            }
            for (int k = 0; k < PreviewWorker::kColumns; ++k)
            {
                const auto u = static_cast<std::size_t>(k);
                float lo = std::min(out.edge[u], out.edge[u + 1]);
                float hi = std::max(out.edge[u], out.edge[u + 1]);
                // Samples strictly inside the column: offsets j with t[k] < (j + 1) / fs < t[k + 1].
                const auto j0 = static_cast<int64_t>(std::floor(t[u] * fs - 1.0)) + 1;
                const auto j1 = static_cast<int64_t>(std::ceil(t[u + 1] * fs - 1.0)) - 1;
                for (int64_t j = std::max<int64_t>(j0, 0); j <= j1 && edge + j <= last; ++j)
                {
                    lo = std::min(lo, g[edge + j]);
                    hi = std::max(hi, g[edge + j]);
                }
                out.lo[u] = lo;
                out.hi[u] = hi;
            }
        }

        // One run: the full-resolution render into `scratch`, then Run (every d-th sample, expDb reading) and Trace.
        void computeRun(const PreviewJob& job, const fcdsp::analysis::StepStimulus& stim, bool release,
                        std::vector<float>& scratch, PreviewWorker::Run& run, PreviewWorker::Trace& trace)
        {
            run.stim = stim;
            run.n = 0;
            run.measured = { -1.0f, -1.0f, fcdsp::TimeLaw::expDb };
            trace = PreviewWorker::Trace{};
            if (job.entry == nullptr)
                return;

            fcdsp::analysis::StepStimulus full = stim;
            full.decimate = 1;
            const std::size_t cap = renderCapacity(full);
            if (scratch.size() < cap)
                scratch.resize(cap);
            const int n = fcdsp::analysis::stepResponse(*job.entry, job.eng, full, std::span<float>(scratch.data(), cap));
            if (n <= 0)
                return;
            const std::span<const float> g(scratch.data(), static_cast<std::size_t>(n));

            const int d = std::max(stim.decimate, 1);
            int j = 0;
            for (; j < PreviewWorker::kMaxPoints && static_cast<int64_t>(j) * d < n; ++j)
                run.gr[static_cast<std::size_t>(j)] = g[static_cast<std::size_t>(j) * static_cast<std::size_t>(d)];
            run.n = j;

            const int64_t pre = segmentSamples(full.preSec, full.fs);
            const int64_t hi = segmentSamples(full.hiSec, full.fs);
            const int64_t lo = segmentSamples(full.loSec, full.fs);
            const int64_t edge = release ? pre + hi : pre;
            const int64_t end = release ? pre + hi + lo : pre + hi;
            reduce(scratch.data(), n, edge, end, static_cast<double>(full.fs),
                   release ? layout::kStepRelease : layout::kStepAttack, trace);

            for (int law = 0; law < PreviewWorker::kTimeLaws; ++law)
            {
                const fcdsp::analysis::TimeReadout r
                    = fcdsp::analysis::measure(g, full, static_cast<fcdsp::TimeLaw>(law));
                trace.measured[static_cast<std::size_t>(law)] = release ? r.releaseS : r.attackS;
                if (law == static_cast<int>(fcdsp::TimeLaw::expDb))
                    run.measured = r;
            }
        }
    }

    bool computePreview(const PreviewJob& job, PreviewBuffer& out, std::vector<float>& scratch,
                        const std::atomic<bool>* cancel)
    {
        const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(std::memory_order_acquire); };
        out.result.key = job.key;
        for (int i = 0; i < PreviewWorker::kAttackRuns; ++i)
        {
            if (cancelled())
                return false;
            const auto u = static_cast<std::size_t>(i);
            computeRun(job, attackStimulus(job.fs, i), false, scratch, out.result.attack[u], out.attack[u]);
        }
        for (int i = 0; i < PreviewWorker::kReleaseRuns; ++i)
        {
            if (cancelled())
                return false;
            const auto u = static_cast<std::size_t>(i);
            computeRun(job, releaseStimulus(job.fs, i), true, scratch, out.result.release[u], out.release[u]);
        }
        return true;
    }
}
