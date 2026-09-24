// Source/editor/PreviewWorker.h — the step responses of the Characteristics screen (02 §7.3 STEP RESPONSE, §9.3), off
// the message thread. Owned by the Panel; declared by U1a and frozen at FZ4; U4 (S8) owns it and implements the
// computation (analysis::stepResponse + analysis::measure, F8), and may add to it.
//
// Contract (02 §7.1, §9.3, §3.7 rule 7; K2 #27):
// - It works only while the Characteristics screen is the target screen (setActive; the Panel calls it from setView).
// - request() queues a recompute for an EngineParams; the latest request wins, a request equal to the pending or the
//   last computed key is dropped, and requests are rate-limited to <= 20 Hz (layout::chars::kPreviewMaxHz) by tick().
// - Results are double-buffered: result() is the latest COMPLETE result and never changes inside a frame's draw.
// - With synchronous (PanelOptions::syncPreview) the job runs inside tick() on the calling thread, so no drawn state
//   depends on another thread's completion time; pending() stays true while a job is queued or running, so the Panel's
//   wantsFullRate() holds and HeadlessHost::settle() waits for it.
// - stop() joins the worker thread; it is the first step of Panel::shutdown(), before the gestures close. Requests
//   after stop() are dropped.
//
// The runs (02 §9.3): attack — three steps to +6 / +12 / +24 dB over the threshold, hiSec 1.0, loDbUnderThr 24,
// loSec 0.2; release — two bursts of +12 dB for 0.05 s and 2.0 s, then loSec 10. Each run is decimated to at most
// kMaxPoints points (StepStimulus::decimate), then drawn as min/max columns by StepPlot.
#pragma once

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/params/EngineParams.h"

#include <array>
#include <cstdint>
#include <memory>

namespace fcdsp
{
    struct ModeEntry;
}

namespace fcmp::ui
{
    class PreviewWorker
    {
    public:
        static constexpr int kAttackRuns  = 3;                   // +6 / +12 / +24 dB over T_in
        static constexpr int kReleaseRuns = 2;                   // after a 50 ms / 2 s burst
        static constexpr int kMaxPoints   = 4096;                // per run, after decimation

        struct Run
        {
            fcdsp::analysis::StepStimulus stim{};                // what was run (fs, levels, durations, decimate)
            int n = 0;                                           // points in gr (0: not computed)
            std::array<float, kMaxPoints> gr{};                  // applied GR (dB, >= 0), every stim.decimate-th sample
            fcdsp::analysis::TimeReadout measured{};             // analysis::measure of this run
        };

        struct Result
        {
            uint64_t key = 0;                                    // the request's key (0: nothing computed yet)
            uint32_t serial = 0;                                 // bumps with every completed result
            std::array<Run, kAttackRuns>  attack{};
            std::array<Run, kReleaseRuns> release{};
        };

        explicit PreviewWorker(bool synchronous);
        ~PreviewWorker();                                        // stop()

        PreviewWorker(const PreviewWorker&) = delete;
        PreviewWorker& operator=(const PreviewWorker&) = delete;

        void setActive(bool);                                    // the Characteristics screen is the target screen
        bool active() const noexcept;

        // Queues a recompute of every run for `entry` at `eng` (the live-overlaid EngineParams) and sample rate `fs`.
        // `key` identifies the input (FrameState::engHash); 0 is reserved. Message thread.
        void request(const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng, float fs, uint64_t key);

        void tick(float dt);                                     // message thread, once per Panel::tick
        bool pending() const noexcept;                           // a job is queued or running
        const Result& result() const noexcept;                   // the latest complete result (message thread)

        void stop();                                             // joins the worker; idempotent
        bool stopped() const noexcept;

    private:
        struct Impl;                                             // the thread, the queue and the two buffers
        std::unique_ptr<Impl> impl_;
    };
}
