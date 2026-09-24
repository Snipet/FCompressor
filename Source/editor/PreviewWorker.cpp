// Source/editor/PreviewWorker.cpp — see PreviewWorker.h. U1a wrote the plumbing (the queue, the rate limit, the
// synchronous path, the juce::Thread with double-buffered results, stop-before-teardown); U4 (S8) the computation.
//
// compute(), per run (02 §9.3; 01 §7; K2 #24):
// - The stimulus is 02 §9.3's (attack: +6 / +12 / +24 dB over the threshold for 1 s from 24 dB under it, then 0.2 s
//   under it; release: a +12 dB burst of 50 ms or 2 s, then 10 s at 12 dB under it). Run::stim records it with the
//   decimation that fits Run::gr (kMaxPoints).
// - analysis::stepResponse renders it ONCE at decimate 1 into a scratch the worker keeps (1.25 s / 12.05 s of samples;
//   2.3 MB at 48 kHz for a release run). The render is causal and independent of the decimation, so Run::gr, taken as
//   every d-th sample of it, is bit-identical to stepResponse(stim) itself (ui.chars checks it).
// - analysis::measure runs on the full-resolution render, once per TimeLaw (Trace::measured; Run::measured = expDb):
//   at decimate 1 it equals dsp.time's extraction, and a decimated trace would move a 200 µs FET crossing by up to d
//   samples.
// - reduce() maps the render onto the pane's log-time columns (PreviewWorker.h, Trace).
// Every analysis entry point opens fcdsp::ScopedFtz itself (01 §7, K2 #24), so the worker thread computes exactly what
// the synchronous path and the probes compute. The worker thread checks threadShouldExit() between runs, so stop()
// joins within one run (<= ~0.1 s at 192 kHz).
#include "editor/PreviewWorker.h"

#include "editor/Layout.h"

#include "fcdsp/core/Units.h"
#include "fcdsp/modes/Registry.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fcmp::ui
{
    namespace
    {
        struct Job
        {
            const fcdsp::ModeEntry* entry = nullptr;
            fcdsp::EngineParams     eng{};
            float                   fs = 48000.0f;
            uint64_t                key = 0;
        };

        // A result and the traces that belong to it: the unit the two buffers swap.
        struct Buffer
        {
            PreviewWorker::Result result{};
            std::array<PreviewWorker::Trace, PreviewWorker::kAttackRuns>  attack{};
            std::array<PreviewWorker::Trace, PreviewWorker::kReleaseRuns> release{};
        };

        constexpr float kMinIntervalS = 1.0f / layout::chars::kPreviewMaxHz;

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
        void computeRun(const Job& job, const fcdsp::analysis::StepStimulus& stim, bool release,
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

        // One result for `job`. `thread` (the worker; nullptr on the synchronous path) is asked between runs whether
        // to give up; returns false when it did, and the buffer is then incomplete.
        bool compute(const Job& job, Buffer& out, std::vector<float>& scratch, const juce::Thread* thread)
        {
            const auto cancelled = [thread] { return thread != nullptr && thread->threadShouldExit(); };
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

    struct PreviewWorker::Impl
    {
        class Worker final : public juce::Thread
        {
        public:
            explicit Worker(Impl& owner) : juce::Thread("FCompressor preview"), impl(owner) {}

            void run() override
            {
                while (!threadShouldExit())
                {
                    std::optional<Job> job;
                    {
                        const std::lock_guard<std::mutex> lock(impl.mutex);
                        job = std::exchange(impl.handoff, std::nullopt);
                    }
                    if (!job)
                    {
                        wait(100.0);
                        continue;
                    }
                    // The message thread never touches `back` or `scratch` while a job runs.
                    if (!compute(*job, *impl.back, impl.scratch, this))
                        return;                                  // stop(): the half-built buffer is never published
                    const std::lock_guard<std::mutex> lock(impl.mutex);
                    impl.completed = true;
                }
            }

        private:
            Impl& impl;
        };

        explicit Impl(bool sync) : synchronous(sync) {}

        void publish()                                           // message thread: the back buffer becomes the result
        {
            std::swap(front, back);
            front->result.serial = ++serial;
            running = false;
        }

        const bool synchronous;
        bool active = false;
        bool stopped = false;
        bool running = false;                                    // a job was handed over and has not been published
        std::optional<Job> queued;                               // the latest request not yet started
        uint64_t lastKey = 0;                                    // the key of the last started job
        float sinceStart = kMinIntervalS;                        // the first request starts at once
        uint32_t serial = 0;
        std::unique_ptr<Buffer> front = std::make_unique<Buffer>();
        std::unique_ptr<Buffer> back = std::make_unique<Buffer>();
        std::vector<float> scratch;                              // the full-resolution render (the computing thread's)

        std::mutex mutex;                                        // guards handoff and completed (message <-> worker)
        std::optional<Job> handoff;
        bool completed = false;
        std::unique_ptr<Worker> thread;
    };

    PreviewWorker::PreviewWorker(bool synchronous) : impl_(std::make_unique<Impl>(synchronous)) {}

    PreviewWorker::~PreviewWorker() { stop(); }

    void PreviewWorker::setActive(bool on)
    {
        impl_->active = on;
        if (!on)
            impl_->queued.reset();                               // pending() falls: the Panel may idle on PANEL
    }

    bool PreviewWorker::active() const noexcept { return impl_->active; }

    void PreviewWorker::request(const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng, float fs, uint64_t key)
    {
        Impl& d = *impl_;
        if (d.stopped || !d.active || key == 0)
            return;
        if (key == d.lastKey)                                    // the latest wish is computed or running already:
        {                                                        // an older queued request is obsolete
            d.queued.reset();
            return;
        }
        if (!d.queued || d.queued->key != key)
            d.queued = Job{ &entry, eng, fs, key };
    }

    void PreviewWorker::tick(float dt)
    {
        Impl& d = *impl_;
        if (d.stopped)
            return;
        if (dt > 0.0f)
            d.sinceStart += dt;
        if (!d.synchronous && d.running)
        {
            bool done = false;
            {
                const std::lock_guard<std::mutex> lock(d.mutex);
                done = std::exchange(d.completed, false);
            }
            if (done)
                d.publish();
        }
        if (!d.active || !d.queued || d.running || d.sinceStart < kMinIntervalS)
            return;
        const Job job = *d.queued;
        d.queued.reset();
        d.lastKey = job.key;
        d.sinceStart = 0.0f;
        d.running = true;
        if (d.synchronous)
        {
            compute(job, *d.back, d.scratch, nullptr);
            d.publish();
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(d.mutex);
            d.handoff = job;
        }
        if (!d.thread)
        {
            d.thread = std::make_unique<Impl::Worker>(d);
            d.thread->startThread();
        }
        d.thread->notify();
    }

    bool PreviewWorker::pending() const noexcept { return !impl_->stopped && (impl_->queued || impl_->running); }

    const PreviewWorker::Result& PreviewWorker::result() const noexcept { return impl_->front->result; }

    void PreviewWorker::stop()
    {
        Impl& d = *impl_;
        if (d.stopped)
            return;
        d.stopped = true;
        d.queued.reset();
        if (d.thread)
        {
            d.thread->signalThreadShouldExit();
            d.thread->notify();
            d.thread->stopThread(1000);
            d.thread.reset();
        }
        d.running = false;
        std::vector<float>().swap(d.scratch);                    // the render scratch is only needed while running
    }

    bool PreviewWorker::stopped() const noexcept { return impl_->stopped; }

    const PreviewWorker::Trace& PreviewWorker::attackTrace(int run) const noexcept
    {
        return impl_->front->attack[static_cast<std::size_t>(std::clamp(run, 0, kAttackRuns - 1))];
    }

    const PreviewWorker::Trace& PreviewWorker::releaseTrace(int run) const noexcept
    {
        return impl_->front->release[static_cast<std::size_t>(std::clamp(run, 0, kReleaseRuns - 1))];
    }
}
