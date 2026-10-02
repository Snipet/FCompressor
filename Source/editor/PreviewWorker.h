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
// - Where an asynchronous worker has no thread (Emscripten without pthreads; a system that refuses one) a request is
//   computed inside tick() too, but only once no newer request has replaced it for 0.15 s of tick time (the sum of
//   tick()'s dt), so the Panel stays smooth while a control moves and the plots follow when it rests (web Sprint D).
// - stop() joins the worker thread; it is the first step of Panel::shutdown(), before the gestures close. Requests
//   after stop() are dropped.
//
// The runs (02 §9.3): attack — three steps to +6 / +12 / +24 dB over the threshold, hiSec 1.0, loDbUnderThr 24,
// loSec 0.2; release — two bursts of +12 dB for 0.05 s and 2.0 s, then loSec 10. Each run is decimated to at most
// kMaxPoints points (StepStimulus::decimate); StepPlot draws the min/max columns of its full-resolution Trace (U4
// additions below).
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

        // ---- U4 additions (S8; additive: no FZ4 declaration above changed) -------------------------------------------
        // Run::gr keeps every d-th sample (d = 15 for an attack run at 48 kHz), too coarse for the first decades of a
        // log-time pane, where FET 76's 20–800 µs attacks live. So each run is rendered once at full resolution (the
        // render does not depend on the decimation: stepResponse writes every d-th sample of one rendering); Run::gr and
        // Run::measured are taken from that render, and it is reduced here onto its pane's log-time axis, which StepPlot
        // draws. A Trace belongs to result() (the same double buffer and serial).
        //
        // Axis: layout::kStepAttack for the attack runs, kStepRelease for the release runs. t is the time from the step
        // (the attack runs' silence -> burst edge, the release runs' burst -> quiet edge); the k-th sample after the edge
        // sits at t = (k + 1) / fs, and the response is linear in t between samples, from `from` at t = 0 (the model of
        // analysis::measure). Column k spans [t_k, t_k+1] with t_k = tMinS · (tMaxS / tMinS)^(k / kColumns): one column
        // per px of plot.w. Past the last sample the response holds `to`.
        static constexpr int kColumns  = 116;                    // plot.w of both STEP panes (asserted in the .cpp)
        static constexpr int kTimeLaws = 6;                      // fcdsp::TimeLaw enumerators (asserted in the .cpp)

        struct Trace
        {
            bool  valid = false;                                 // computed, with samples after the edge
            float from = 0.0f;                                   // GR at the last sample before the edge (dB)
            float to = 0.0f;                                     // GR at the last sample of the segment after it (dB)
            std::array<float, kColumns + 1> edge{};              // GR at t_k (dB)
            std::array<float, kColumns> lo{}, hi{};              // min / max GR over column k, its edges included (dB)
            // analysis::measure of this edge on the full-resolution render, for every TimeLaw (index = the enumerator):
            // the attack runs' attackS, the release runs' releaseS; -1 when a crossing is missing. Run::measured holds
            // the expDb reading (the worker is not told the published law: StepPlot picks the spec's law here).
            std::array<float, kTimeLaws> measured{};
        };

        const Trace& attackTrace(int run) const noexcept;        // 0 <= run < kAttackRuns (clamped); of result()
        const Trace& releaseTrace(int run) const noexcept;       // 0 <= run < kReleaseRuns (clamped); of result()

    private:
        struct Impl;                                             // the thread, the queue and the two buffers
        std::unique_ptr<Impl> impl_;
    };

    // A probe's way to the no-thread path in a build that has threads (ui.previewcost; web Sprint D, additive): while
    // it is on, a worker that has no thread yet is refused one, as a system can refuse it, so the path a web build
    // always takes runs natively too (where no thread can exist it changes nothing). It returns what it was. The
    // product never calls it. Message thread.
    bool previewWorkerRefuseThread(bool refuse) noexcept;
}
