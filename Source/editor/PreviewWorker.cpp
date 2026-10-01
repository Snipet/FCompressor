// Source/editor/PreviewWorker.cpp — see PreviewWorker.h. U1a wrote the plumbing (the queue, the rate limit, the
// synchronous path, the worker thread with double-buffered results, stop-before-teardown); U4 (S8) the computation,
// which is PreviewCompute.cpp's since web Sprint B (ADR-93).
//
// The worker is a std::thread (a juce::Thread until web Sprint B; nothing here needs JUCE any more): it sleeps on a
// condition variable until tick() hands a job over or stop() raises `quit`, computes into the back buffer and marks it
// completed; tick() publishes it on the message thread. computePreview() reads `quit` between runs, so stop() joins
// within one run (<= ~0.1 s at 192 kHz). The synchronous path never creates the thread.
#include "editor/PreviewWorker.h"

#include "editor/Layout.h"
#include "editor/PreviewCompute.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
 #include <pthread.h>
#endif

namespace fcmp::ui
{
    namespace
    {
        constexpr float kMinIntervalS = 1.0f / layout::chars::kPreviewMaxHz;

        // std::thread has no name: the worker takes one where the platform has a call for it (debuggers, profilers).
        void nameThisThread() noexcept
        {
           #if defined(__APPLE__)
            pthread_setname_np("FCompressor preview");
           #elif defined(__linux__)
            pthread_setname_np(pthread_self(), "FCmp preview");  // Linux: at most 15 characters
           #endif
        }
    }

    struct PreviewWorker::Impl
    {
        explicit Impl(bool sync) : synchronous(sync) {}

        void publish()                                           // message thread: the back buffer becomes the result
        {
            std::swap(front, back);
            front->result.serial = ++serial;
            running = false;
        }

        void run()                                               // the worker thread
        {
            nameThisThread();
            for (;;)
            {
                std::optional<PreviewJob> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [this] { return quit.load(std::memory_order_acquire) || handoff.has_value(); });
                    if (quit.load(std::memory_order_acquire))
                        return;
                    job = std::exchange(handoff, std::nullopt);
                }
                // The message thread never touches `back` or `scratch` while a job runs.
                if (!computePreview(*job, *back, scratch, &quit))
                    return;                                      // stop(): the half-built buffer is never published
                const std::lock_guard<std::mutex> lock(mutex);
                completed = true;
            }
        }

        // Message thread: the worker thread, created by the first asynchronous job. False: the system had no thread to
        // give, and the job is computed inline, as the synchronous path does, instead of pending for ever.
        bool startThread()
        {
            if (thread.joinable())
                return true;
            try
            {
                thread = std::thread([this] { run(); });
            }
            catch (const std::system_error&)
            {
                return false;
            }
            return true;
        }

        const bool synchronous;
        bool active = false;
        bool stopped = false;
        bool running = false;                                    // a job was handed over and has not been published
        std::optional<PreviewJob> queued;                        // the latest request not yet started
        uint64_t lastKey = 0;                                    // the key of the last started job
        float sinceStart = kMinIntervalS;                        // the first request starts at once
        uint32_t serial = 0;
        std::unique_ptr<PreviewBuffer> front = std::make_unique<PreviewBuffer>();
        std::unique_ptr<PreviewBuffer> back = std::make_unique<PreviewBuffer>();
        std::vector<float> scratch;                              // the full-resolution render (the computing thread's)

        std::mutex mutex;                                        // guards handoff and completed (message <-> worker)
        std::condition_variable wake;                            // a job was handed over, or quit was raised
        std::optional<PreviewJob> handoff;
        bool completed = false;
        std::atomic<bool> quit { false };                        // stop(): raised under the mutex (no lost wake-up);
                                                                 // the computation reads it between runs without it
        std::thread thread;                                      // joined by stop(), which the destructor calls
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
            d.queued = PreviewJob{ &entry, eng, fs, key };
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
        const PreviewJob job = *d.queued;
        d.queued.reset();
        d.lastKey = job.key;
        d.sinceStart = 0.0f;
        d.running = true;
        if (d.synchronous || !d.startThread())
        {
            computePreview(job, *d.back, d.scratch, nullptr);
            d.publish();
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(d.mutex);
            d.handoff = job;
        }
        d.wake.notify_one();
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
        if (d.thread.joinable())
        {
            {
                const std::lock_guard<std::mutex> lock(d.mutex);
                d.quit.store(true, std::memory_order_release);
            }
            d.wake.notify_one();
            d.thread.join();                                     // waits for the run in progress, no longer
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
