// Source/editor/PreviewCompute.h — the computation behind the PreviewWorker (02 §7.3 STEP RESPONSE, §9.3; U4, S8), as
// a plain function: one request in, one complete result out, on whatever thread the caller runs it. Split from
// PreviewWorker.cpp by web Sprint B (ADR-93): no JUCE and no thread type here, so the PreviewWorker runs it inline or
// on its std::thread, and a host that works off the message thread some other way can run the same function.
//
// - PreviewJob is one request: PreviewWorker::request's arguments.
// - PreviewBuffer is a result and the traces that belong to it: the unit the PreviewWorker's two buffers swap.
// - computePreview() fills `out` for `job`: the attack runs, then the release runs (PreviewCompute.cpp says how a run
//   is rendered, read and reduced). `scratch` holds one full-resolution render; the caller keeps it between calls, so
//   it grows once (2.3 MB at 48 kHz for a release run). `cancel` (nullptr: nobody can cancel) is read before every
//   run: once it is true the function returns false, and `out` is incomplete and must not be published. A run is
//   never interrupted, so a cancel takes effect within one run (<= ~0.1 s at 192 kHz). Nothing else is shared: two
//   calls on different buffers and scratches may run at once.
#pragma once

#include "editor/PreviewWorker.h"

#include "fcdsp/params/EngineParams.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace fcmp::ui
{
    struct PreviewJob
    {
        const fcdsp::ModeEntry* entry = nullptr;                 // nullptr: every run stays empty (n = 0, no Trace)
        fcdsp::EngineParams     eng{};
        float                   fs = 48000.0f;
        uint64_t                key = 0;
    };

    struct PreviewBuffer
    {
        PreviewWorker::Result result{};
        std::array<PreviewWorker::Trace, PreviewWorker::kAttackRuns>  attack{};
        std::array<PreviewWorker::Trace, PreviewWorker::kReleaseRuns> release{};
    };

    // True: `out` is the complete result for `job` (result.key = job.key; result.serial is the caller's). False:
    // `cancel` was raised.
    bool computePreview(const PreviewJob& job, PreviewBuffer& out, std::vector<float>& scratch,
                        const std::atomic<bool>* cancel);
}
