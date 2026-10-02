// FCMP_PROBE layer=ui name=previewcost scope=mode timeout=120
//
// ui.previewcost.<key> (web Sprint D, ADR-93; W-N): what one step-response preview costs, and the rest that keeps the
// Panel smooth where the PreviewWorker has no thread to compute it on. Spec rows only; the milliseconds are notes,
// never limits (the probe runs beside other tests). The Mode's defaults at 48 kHz. A job is counted by
// PreviewWorker::Result::serial, which bumps once per completed result, and time is tick time, the dt handed to
// tick(): no row waits on a clock, except for a thread where the build has one.
//
// One request (02 §9.3's five runs):
//   sync.completed       a synchronous worker completes it inside its tick, five times 0.1 s apart (the worker starts
//                        at most 20 jobs a second of tick time): the key, the serial, five runs, nothing pending
//   async.equal          the asynchronous worker's result is that result, bitwise: on its thread natively, inline
//                        after the rest as wasm32 under node, where no thread can exist
//   thread.no_rest       (builds with threads) on its thread a request starts in its first tick and needs no tick
//                        time at all: the rest below belongs to the no-thread path alone
//   notes                preview_ms, preview_ms_min, preview_ms_max: the median, fastest and slowest of preview_n
//                        synchronous requests; preview_path: what an asynchronous worker does in this build ("thread"
//                        or "rest")
// The no-thread path (PreviewWorker.h's contract: computed inside tick(), once no newer request has replaced it for
// 0.15 s of tick time). Under node it is the build's own. Natively the probe chooses it with PreviewWorker.cpp's
// switch, previewWorkerRefuseThread: while it is on no worker's thread starts, as on a system that refuses one. Ticks
// of 0.04 s, so 0.12 s is inside the rest and 0.16 s past it:
//   rest.early.jobs      a request that has rested 0.12 s has cost no job ...
//   rest.early.pending   ... and is pending (the Panel keeps ticking at full rate)
//   rest.replaced.jobs   a newer request restarts the rest: 0.12 s after it (0.24 s after the first) still no job
//   rest.computed.jobs   one tick later (0.16 s) exactly one job ran ...
//   rest.computed.latest ... the newer request's, equal to the synchronous result: the replaced one was never computed
//   rest.same.jobs       a request equal to the computed one is never pending and costs no job
//   rest.repeat.early    a request repeated before every tick (StepPlot's way) rests once: no job at 0.12 s ...
//   rest.repeat.jobs     ... and one at 0.16 s
//   rest.sync.jobs       a synchronous worker (PanelOptions::syncPreview) never rests: one job in its first tick
// The Panel on that path (Panel{skipHint, asynchronous}; HeadlessHost at 1/60 s, dpi 2, theme 0; CHARACTERISTICS),
// with frames counted from the one whose StepPlots made the request:
//   panel.open.early     opening the screen: no job in the first 8 frames (0.133 s) ...
//   panel.open.jobs      ... and one by the 10th (0.167 s)
//   panel.drag.moved     a slot the Mode leaves continuous (the first of THRESHOLD, RELEASE, ATTACK, MAKEUP, MIX,
//                        RATIO, DRIVE, KNEE) written every frame for 30 frames, over half its range: the curves' input
//                        (engSerial) changed in every one of them ...
//   panel.drag.jobs      ... and no job ran: every frame of the drag is free of the computation
//   panel.rest.early     the controls at rest: no job in the next 8 frames ...
//   panel.rest.jobs      ... one by the 10th, and nothing pending
//   panel.rest.equal     the plots' result is the synchronous result of the Panel's final EngineParams
//   panel.settled        the Panel then settles (HeadlessHost::settle within 600 frames)
//   notes                drag_param: the slot written; drag_frame_ms: the median wall time of a drag frame, ticked and
//                        drawn (no job in it); rest_frame_ms: the slowest of the ten frames after it, the one that
//                        held the job
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/SubView.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/panel/HeadlessGuiScope.h>
#include <funkgui/panel/HeadlessHost.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace fcmp::ui
{
    // PreviewWorker.cpp's switch for this probe. PreviewWorker.h is frozen and does not declare it; the product never
    // calls it. While it is on no worker's thread starts, as on a system that refuses one; it returns what it was.
    bool previewWorkerRefuseThread(bool) noexcept;
}

namespace
{
    using funkgui::test::Probe;
    using fcdsp::Pid;
    namespace ui = fcmp::ui;
    namespace probe = fcmp::probe;

    constexpr float kFs = 48000.0f;
    constexpr float kDt = 1.0f / 60.0f;                           // the Panel's frames
    constexpr float kRestDt = 0.04f;                              // the workers' ticks: three in the rest, a 4th past
    constexpr float kRepDt = 0.1f;                                // between timed requests: past the 20 Hz limiter
    constexpr int   kReps = 5;                                    // synchronous requests timed (the note: their median)
    constexpr int   kMaxSettle = 600;
    constexpr int   kDragFrames = 30;
    constexpr int   kWaitPolls = 20000;                           // a thread's job: at most this many 1 ms waits
    constexpr ui::PanelOptions kAsyncPanel { true, false, false };   // skipHint, !syncPreview, !ignoreLive

    // Whether an asynchronous worker can have a thread in this build (PreviewWorker.cpp's startThread()).
   #if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    constexpr const char* kPath = "\"rest\"";
    void waitForThread() {}                                       // no thread: a job runs inside a tick
   #else
    #define FCMP_PREVIEWCOST_THREADS 1
    constexpr const char* kPath = "\"thread\"";
    void waitForThread() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
   #endif

    // The no-thread path for the scope (natively: the refusal; as wasm32 without pthreads it changes nothing).
    struct NoThread
    {
        NoThread() noexcept : was(ui::previewWorkerRefuseThread(true)) {}
        ~NoThread() { ui::previewWorkerRefuseThread(was); }
        NoThread(const NoThread&) = delete;
        NoThread& operator=(const NoThread&) = delete;
        const bool was;
    };

    // ---- bitwise equality, member by member (the structs have padding; ui.chars' worker.async_equal) ----------------

    template <typename T, std::size_t N>
    bool sameBits(const std::array<T, N>& a, const std::array<T, N>& b)
    {
        return std::memcmp(a.data(), b.data(), sizeof(T) * N) == 0;
    }

    bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

    bool sameTrace(const ui::PreviewWorker::Trace& a, const ui::PreviewWorker::Trace& b)
    {
        return a.valid == b.valid && sameBits(a.from, b.from) && sameBits(a.to, b.to) && sameBits(a.edge, b.edge)
            && sameBits(a.lo, b.lo) && sameBits(a.hi, b.hi) && sameBits(a.measured, b.measured);
    }

    bool sameRun(const ui::PreviewWorker::Run& a, const ui::PreviewWorker::Run& b)
    {
        return a.n == b.n && a.stim.decimate == b.stim.decimate && sameBits(a.stim.fs, b.stim.fs)
            && sameBits(a.measured.attackS, b.measured.attackS) && sameBits(a.measured.releaseS, b.measured.releaseS)
            && a.measured.law == b.measured.law && sameBits(a.gr, b.gr);
    }

    // Every run and trace of the two workers' results (the keys and serials are the requests', not the results').
    bool sameResult(const ui::PreviewWorker& a, const ui::PreviewWorker& b)
    {
        const ui::PreviewWorker::Result& ra = a.result();
        const ui::PreviewWorker::Result& rb = b.result();
        bool equal = ra.key != 0 && rb.key != 0;
        for (int i = 0; equal && i < ui::PreviewWorker::kAttackRuns; ++i)
        {
            const auto k = static_cast<std::size_t>(i);
            equal = sameTrace(a.attackTrace(i), b.attackTrace(i)) && sameRun(ra.attack[k], rb.attack[k]);
        }
        for (int i = 0; equal && i < ui::PreviewWorker::kReleaseRuns; ++i)
        {
            const auto k = static_cast<std::size_t>(i);
            equal = sameTrace(a.releaseTrace(i), b.releaseTrace(i)) && sameRun(ra.release[k], rb.release[k]);
        }
        return equal;
    }

    bool allRuns(const ui::PreviewWorker::Result& r)
    {
        bool all = true;
        for (const ui::PreviewWorker::Run& run : r.attack)
            all = all && run.n > 0;
        for (const ui::PreviewWorker::Run& run : r.release)
            all = all && run.n > 0;
        return all;
    }

    int64_t jobs(const ui::PreviewWorker& w) { return static_cast<int64_t>(w.result().serial); }

    void ticks(ui::PreviewWorker& w, int n, float dt)
    {
        for (int i = 0; i < n; ++i)
            w.tick(dt);
    }

    // Ticks an asynchronous worker until `key` is its result and nothing is pending; false after maxTicks. Where the
    // job runs on a thread it takes real time, so every tick is followed by a 1 ms wait.
    bool tickUntil(ui::PreviewWorker& w, uint64_t key, float dt, int maxTicks)
    {
        for (int i = 0; i < maxTicks && (w.pending() || w.result().key != key); ++i)
        {
            w.tick(dt);
            waitForThread();
        }
        return !w.pending() && w.result().key == key;
    }

    std::string json(double v)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", v);
        return buf;
    }

    // ---- one request: completed, timed, and what the asynchronous worker gives --------------------------------------

    void costRows(Probe& P, const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng, ui::PreviewWorker& sync)
    {
        std::array<double, kReps> ms{};
        bool completed = true;
        sync.setActive(true);
        for (int r = 0; r < kReps; ++r)                          // a new key each time: an equal request is dropped
        {
            const auto key = static_cast<uint64_t>(100 + r);
            sync.request(entry, eng, kFs, key);
            const bool queued = sync.pending();
            const auto t0 = std::chrono::steady_clock::now();
            sync.tick(kRepDt);
            ms[static_cast<std::size_t>(r)]
                = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            completed = completed && queued && !sync.pending() && sync.result().key == key
                     && sync.result().serial == static_cast<uint32_t>(r + 1) && allRuns(sync.result());
        }
        P.eq("sync.completed", completed ? 1 : 0, 1);
        std::sort(ms.begin(), ms.end());
        P.note("preview_ms", json(ms[kReps / 2]));
        P.note("preview_ms_min", json(ms.front()));
        P.note("preview_ms_max", json(ms.back()));
        P.note("preview_n", std::to_string(kReps));
        P.note("preview_path", kPath);

        {
            ui::PreviewWorker async(false);
            async.setActive(true);
            async.request(entry, eng, kFs, 7);
            const bool done = tickUntil(async, 7, kDt, kWaitPolls);
            P.eq("async.equal", done && sameResult(async, sync) ? 1 : 0, 1);
            async.stop();
        }
       #if defined(FCMP_PREVIEWCOST_THREADS)
        {
            ui::PreviewWorker threaded(false);
            threaded.setActive(true);
            threaded.request(entry, eng, kFs, 8);
            P.eq("thread.no_rest", tickUntil(threaded, 8, 0.0f, kWaitPolls) ? 1 : 0, 1);
            threaded.stop();
        }
       #endif
    }

    // ---- the no-thread path: the rest -------------------------------------------------------------------------------

    void restRows(Probe& P, const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng,
                  const ui::PreviewWorker& sync)
    {
        const NoThread noThread;
        constexpr uint64_t kA = 21, kB = 22, kC = 23;
        ui::PreviewWorker w(false);
        w.setActive(true);

        w.request(entry, eng, kFs, kA);
        ticks(w, 3, kRestDt);                                    // A: 0.12 s
        P.eq("rest.early.jobs", jobs(w), 0);
        P.eq("rest.early.pending", w.pending() ? 1 : 0, 1);

        w.request(entry, eng, kFs, kB);                          // newer: A is replaced inside its rest
        ticks(w, 3, kRestDt);                                    // A would be at 0.24 s; B: 0.12 s
        P.eq("rest.replaced.jobs", jobs(w), 0);

        w.tick(kRestDt);                                         // B: 0.16 s
        P.eq("rest.computed.jobs", jobs(w), 1);
        P.eq("rest.computed.latest", !w.pending() && w.result().key == kB && sameResult(w, sync) ? 1 : 0, 1);

        w.request(entry, eng, kFs, kB);                          // what is computed already
        bool idle = !w.pending();
        for (int i = 0; i < 5; ++i)
        {
            w.tick(kRestDt);
            idle = idle && !w.pending();
        }
        P.eq("rest.same.jobs", idle ? jobs(w) : -1, 1);

        for (int i = 0; i < 3; ++i)                              // C before every tick, as a StepPlot asks every frame
        {
            w.request(entry, eng, kFs, kC);
            w.tick(kRestDt);
        }
        P.eq("rest.repeat.early", jobs(w), 1);                   // C: 0.12 s
        w.request(entry, eng, kFs, kC);
        w.tick(kRestDt);                                         // C: 0.16 s
        P.eq("rest.repeat.jobs", w.result().key == kC && !w.pending() ? jobs(w) : -1, 2);
        w.stop();

        ui::PreviewWorker s(true);                               // PanelOptions::syncPreview: at once, as ever
        s.setActive(true);
        s.request(entry, eng, kFs, kA);
        s.tick(kDt);
        P.eq("rest.sync.jobs", s.result().key == kA && !s.pending() ? jobs(s) : -1, 1);
    }

    // ---- the Panel on the no-thread path ----------------------------------------------------------------------------

    // The slot the drag moves: the first of these that the Mode leaves continuous at its defaults, so that a write
    // inside its own range is a new value, new EngineParams and a new request in every frame. kNoPid: none.
    Pid dragPid(const fcdsp::ParamView& view)
    {
        for (const Pid p : { Pid::thr, Pid::rel, Pid::atk, Pid::makeup, Pid::mix, Pid::ratio, Pid::drive, Pid::knee })
        {
            const fcdsp::ParamSpec* s = view.spec[fcdsp::idx(p)];
            if (s != nullptr && s->kind == fcdsp::Kind::continuous && s->lo < s->hi
                && view[p].state == fcdsp::SlotState::live)
                return p;
        }
        return fcdsp::kNoPid;
    }

    void panelRows(Probe& P, const fcdsp::ModeDescriptor& desc)
    {
        const NoThread noThread;
        probe::FakeFacade facade(desc.key);
        ui::Panel panel(facade, kAsyncPanel);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        const ui::PreviewWorker& w = panel.context().preview;

        panel.setView({ nullptr, ui::Screen::characteristics, fcmp::ScTab::sidechain, ui::Overlay::none }, true);
        host.tick(1, kDt);                                       // the StepPlots ask
        const bool asked = w.pending() && jobs(w) == 0;
        host.tick(8, kDt);
        P.eq("panel.open.early", asked ? jobs(w) : -1, 0);
        host.tick(2, kDt);
        P.eq("panel.open.jobs", jobs(w), 1);

        const Pid pid = dragPid(panel.context().frame.res.view);
        if (pid == fcdsp::kNoPid)
        {
            P.harnessError("ui.previewcost: '" + std::string(desc.key) + "' has no continuous slot to drag");
            return;
        }
        const fcdsp::ParamSpec& spec = *panel.context().frame.res.view.spec[fcdsp::idx(pid)];
        P.note("drag_param", "\"" + std::string(facade.fakePort(pid).id()) + "\"");
        const auto frame = [&host] {                             // one frame, ticked and drawn: its wall time in ms
            const auto t0 = std::chrono::steady_clock::now();
            host.tick(1, kDt);
            host.draw();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        };
        std::array<double, kDragFrames> dragMs{};
        int moved = 0;
        for (int i = 0; i < kDragFrames; ++i)                    // from 0.23 to 0.73 of the slot's own range
        {
            const float u = 0.23f + 0.5f * static_cast<float>(i) / static_cast<float>(kDragFrames);
            const uint32_t before = panel.context().frame.engSerial;
            facade.fakePort(pid).scriptPlain(spec.lo + u * (spec.hi - spec.lo));
            dragMs[static_cast<std::size_t>(i)] = frame();
            moved += panel.context().frame.engSerial != before ? 1 : 0;
        }
        P.eq("panel.drag.moved", moved, kDragFrames);
        P.eq("panel.drag.jobs", jobs(w), 1);                     // the one of the opening
        std::sort(dragMs.begin(), dragMs.end());
        P.note("drag_frame_ms", json(dragMs[kDragFrames / 2]));

        double heldMs = 0.0;
        for (int i = 0; i < 8; ++i)
            heldMs = std::max(heldMs, frame());
        P.eq("panel.rest.early", jobs(w), 1);
        for (int i = 0; i < 2; ++i)
            heldMs = std::max(heldMs, frame());
        P.eq("panel.rest.jobs", !w.pending() ? jobs(w) : -1, 2);
        P.note("rest_frame_ms", json(heldMs));

        const ui::FrameState& f = panel.context().frame;
        ui::PreviewWorker ref(true);
        ref.setActive(true);
        if (f.entry != nullptr)
            ref.request(*f.entry, f.eng, kFs, 1);
        ref.tick(kDt);
        P.eq("panel.rest.equal", sameResult(w, ref) ? 1 : 0, 1);
        P.eq("panel.settled", host.settle(kMaxSettle, kDt) < kMaxSettle ? 1 : 0, 1);
        panel.shutdown();
    }
}

FCMP_PROBE(ui, previewcost)
{
    const funkgui::HeadlessGuiScope gui;                          // the Panel's FontService
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.previewcost: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    probe::FakeFacade facade(entry->desc->key);                   // the Mode's defaults, as ui.chars resolves them
    fcdsp::Resolution res;
    fcdsp::resolve(*entry, facade.currentRaw(), res);

    ui::PreviewWorker sync(true);
    costRows(P, *entry, res.eng, sync);
    restRows(P, *entry, res.eng, sync);
    panelRows(P, *entry->desc);
    return P.finish();
}
