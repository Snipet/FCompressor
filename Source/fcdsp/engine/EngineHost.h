#pragma once

// EngineHost: the Mode-agnostic engine host (01 §5.4-5.6; E §3.1, §9.1). JUCE-free, so every probe drives exactly
// what the plugin runs. EngineHost.cpp is orchestration only (F4 at ECO, F7 complete); the work lives in pure
// component headers under engine/host/ (ScFilter, Router, Delay, Ramps, Crossfade, TelemetryAccum) and in
// engine/Oversampler. Sprint-frozen public API.
//
// Threads: configure() from prepareToPlay, or from the message-thread SetupWatcher under suspendProcessing(true), and
// it is the ONLY allocation point; process()/reset() on the audio thread; requestSnap, setUiAttached, readUiFrame,
// history and setTap from any thread.
//
// Per-block order in process() (01 §5.4): ScopedFtz and sanitise; block start (snap flag, defensive look clamp,
// kernel key and crossfade latch, per-path gain targets); per chunk of <= 64: route, SC filters, lookahead delays,
// one up(), per path (SC preGain + encode, control(), OS-rate gain with preGain folded in, colour(), makeup, decode),
// blend, mix in the OS domain against the never-pre-gained dry, one down(), bypass/listen ramps, telemetry; then the
// poison check and the UiFrame/history publish while attached.
//
// Layout: the state other threads touch (snap flag, attach count, tap pointer, UiFrame seqlock, history ring) is
// declared here and exists from construction, so the editor may attach and read before the first configure(). All
// processing state (arena slots and PathStates, oversampler, host scratch, configuration) lives in Impl, which
// EngineHost.cpp defines and configure() allocates; its owners (F4, F7) never edit this header.

#include "fcdsp/engine/IEngine.h"            // FCDSP_NONBLOCKING
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/Seqlock.h"
#include "fcdsp/telemetry/UiFrame.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace fcdsp {

struct HostConfig {
    double fs = 48000; int maxBlock = 512;
    Quality quality = Quality::std; LookaheadBudget budget = LookaheadBudget::off;
    int mainIns = 2, mainOuts = 2, keyChans = 0;       // key 0 = bus inactive
};

struct BlockParams {                                   // built by the processor each block (reused while a batch is open)
    uint8_t slot;                                      // effective Mode slot
    EngineParams eng;                                  // resolve(slot, raw).eng
    bool bypass, delta, listen, extKey;
};

struct ProcessIo {
    const float* const* in;  int numIn;                // 1 or 2
    const float* const* key; int numKey;               // 0, 1 or 2
    float* const* out;       int numOut;               // 1 or 2; may alias in
    int n;                                             // any length >= 0; chunked internally
    bool hostBypassed;                                 // processBlockBypassed path (HR B §1.6)
};

class EngineHost {
public:
    EngineHost() noexcept;                             // allocates nothing
    ~EngineHost();
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    // prepareToPlay's thread, or SetupWatcher (message thread) under suspendProcessing(true)
    void configure(const HostConfig&, const BlockParams& initial);   // the ONLY allocation point; engines start snapped
    static int latencyFor(const HostConfig&) noexcept;              // lookaheadSamples + kOs[quality].latency
    int    latencySamples() const noexcept;
    double tailSeconds(const BlockParams&) const noexcept;          // desc.tailSeconds + latency/fs
    // audio thread; [[clang::nonblocking]] where the compiler supports it (03 §2.10 rtsan)
    void process(const ProcessIo&, const BlockParams&) noexcept FCDSP_NONBLOCKING;
    void reset() noexcept;
    // any thread
    void requestSnap() noexcept;                       // release store; consumed (acquire) at the next block START
    void setUiAttached(bool attached) noexcept;        // editor ctor(true)/dtor(false); a COUNT, not a bool
    bool readUiFrame(UiFrame&) const noexcept;         // <= 8 seqlock attempts
    const HistoryRing& history() const noexcept;
    void setTap(TestTap*) noexcept;                    // probes only; nullptr = off; loaded once per block (K1 #6, K2 #2)

private:
    struct Impl;                                       // EngineHost.cpp: everything the audio path owns
    std::unique_ptr<Impl> impl_;                       // null until configure(); process() then copies in -> out
    std::atomic<bool>     snap_{false};                // requestSnap(): release store; exchanged (acquire) per block
    std::atomic<int32_t>  attached_{0};                // editor attach count (01 §6.3)
    std::atomic<TestTap*> tap_{nullptr};               // setTap(): loaded once per block
    Seqlock<UiFrame>      uiFrame_{};                  // single writer: the audio thread
    HistoryRing           history_{};                  // single producer: the audio thread
};

} // namespace fcdsp
