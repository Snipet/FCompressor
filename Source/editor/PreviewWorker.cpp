// Source/editor/PreviewWorker.cpp — see PreviewWorker.h. U1a writes the plumbing (the queue, the rate limit, the
// synchronous path, the juce::Thread with double-buffered results, stop-before-teardown); the computation of the runs
// is U4's (S8): compute() below sets up every run's stimulus and completes the result with empty runs until then,
// because the analysis functions it will call (analysis::stepResponse, analysis::measure) land with F8's Analysis.cpp.
#include "editor/PreviewWorker.h"

#include "editor/Layout.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <mutex>
#include <optional>
#include <utility>

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

        constexpr float kMinIntervalS = 1.0f / layout::chars::kPreviewMaxHz;

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

        // One result for `job`. The step responses and their measured crossings are U4's (S8; see the file comment).
        void compute(const Job& job, PreviewWorker::Result& out) noexcept
        {
            out.key = job.key;
            for (int i = 0; i < PreviewWorker::kAttackRuns; ++i)
            {
                PreviewWorker::Run& r = out.attack[static_cast<std::size_t>(i)];
                r.stim = attackStimulus(job.fs, i);
                r.n = 0;
                r.measured = {};
            }
            for (int i = 0; i < PreviewWorker::kReleaseRuns; ++i)
            {
                PreviewWorker::Run& r = out.release[static_cast<std::size_t>(i)];
                r.stim = releaseStimulus(job.fs, i);
                r.n = 0;
                r.measured = {};
            }
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
                    compute(*job, *impl.back);                   // the message thread never touches `back` meanwhile
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
            front->serial = ++serial;
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
        std::unique_ptr<Result> front = std::make_unique<Result>();
        std::unique_ptr<Result> back = std::make_unique<Result>();

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
            compute(job, *d.back);
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

    const PreviewWorker::Result& PreviewWorker::result() const noexcept { return *impl_->front; }

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
    }

    bool PreviewWorker::stopped() const noexcept { return impl_->stopped; }
}
