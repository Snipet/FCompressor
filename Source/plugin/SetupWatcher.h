// Source/plugin/SetupWatcher.h: the processor's one message-thread poller (01 §2.3, §3.1, §4.6, §5.6; K2 #6).
//
// JUCE's VST3 wrapper applies host parameter changes inside process(), so a parameter listener would run on the audio
// thread, and triggerAsyncUpdate may block there. The processor therefore registers no listeners and no AsyncUpdater:
// this 20 Hz juce::Timer, owned by the processor and started by its constructor, polls the raw values instead.
//
// Each tick (poll()):
//   1. quality / labudget: if either raw value differs from the configured setup, suspendProcessing(true) (the
//      wrappers then output silence instead of calling processBlock) -> EngineHost::configure with the new setup (the
//      only allocation point) -> setLatencySamples(EngineHost::latencyFor(setup)) -> suspendProcessing(false): "one
//      block of silence plus a PDC change". Before the first prepareToPlay only the configured setup and the reported
//      latency change (there is no engine to configure yet).
//   2. mode: when the effective Mode slot changes, updateHostDisplay(ChangeDetails().withParameterInfoChanged(true)),
//      so hosts fetch the new Mode's value texts (<= 50 ms after the change; nothing on the audio thread calls it).
//   3. the UI-state handoff (Processor.h; S13 H1b): a UiState loaded on another thread reaches the editor's copy, and
//      the editor's writes reach the word a save on another thread reads, within one tick.
//
// Lock order (no deadlock with a wrapper that calls prepareToPlay under the processor's callback lock, as the AU wrapper
// does on an offline-render switch): suspendProcessing takes and releases the callback lock BEFORE the processor's
// setup mutex is taken, and setLatencySamples / updateHostDisplay run with neither held.
//
// Message thread only. The definitions live in Processor.cpp (they reach the processor's private setup state).
#pragma once

#include <juce_events/juce_events.h>

#include <cstdint>

namespace fcmp
{
    class Processor;

    class SetupWatcher final : private juce::Timer
    {
    public:
        static constexpr int kHz = 20;

        explicit SetupWatcher(Processor&) noexcept;       // does not start: the processor starts it once its
        ~SetupWatcher() override;                          // parameters exist (start())

        SetupWatcher(const SetupWatcher&) = delete;
        SetupWatcher& operator=(const SetupWatcher&) = delete;

        void start();                                      // latches the current Mode slot, then ticks at kHz
        void stop();
        void poll();                                       // one tick (the timer's callback; probes may call it)

        std::uint32_t reconfigures() const noexcept { return reconfigures_; }     // setup changes applied
        std::uint32_t announcements() const noexcept { return announcements_; }   // Mode-change host notifications

    private:
        void timerCallback() override;

        Processor& processor_;
        int lastSlot_ = -1;                                // effective Mode slot last announced (or latched)
        std::uint32_t reconfigures_ = 0;
        std::uint32_t announcements_ = 0;
    };
} // namespace fcmp
