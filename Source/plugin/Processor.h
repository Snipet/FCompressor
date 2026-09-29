// Source/plugin/Processor.h: the FCompressor audio processor (namespace fcmp; P1, S7). It implements ProcessorFacade
// (02 §9.5, FZ1) over an APVTS and a JUCE-free fcdsp::EngineHost (01 §5.4), so the editor never sees either.
//
// Parameters (01 §3.1; K2 #7, #9, #25): an APVTS built with a nullptr UndoManager (undo belongs to the host) from
// kHostParams in the v1-forever kApvtsOrder, universal names, labels "" (the unit is part of the value text), version
// hints; one processor-owned funkgui::JuceParamPort per parameter behind port(Pid), so the ports outlive every editor
// (K2 #27). getNumPrograms() == 1 (K2 #25d).
//
// Threads (01 §2.3; ARCHITECTURE §7):
//   audio     processBlock / processBlockBypassed: a relaxed raw snapshot -> resolveSlot -> resolve -> BlockParams ->
//             EngineHost::process. While a batch is open (beginBatch/endBatch, K2 #23) the previous BlockParams are
//             reused. Never allocates, locks, logs, writes a parameter, or calls setLatencySamples/updateHostDisplay.
//   message   SetupWatcher (20 Hz): quality/labudget reconfigure under suspendProcessing + setLatencySamples; the Mode
//             change announcement. No parameter listeners and no AsyncUpdater anywhere (K2 #6).
//   prepare   prepareToPlay reads quality/labudget, calls EngineHost::configure (the only allocation point), then
//             setLatencySamples, on whatever thread the host calls it from.
//   any       the host-text lambdas (HostText.cpp: currentRaw() + formatHost/parseHost), currentRaw(), telemetry reads,
//             stateNotice(), and get/setStateInformation (a host may save or load a session off the message thread).
//
// The per-instance UI state and the state notice (S13 H1b, lead revision 5a; the S8 race): a load on any thread never
// touches what the editor holds. uiState() returns the message thread's own UiState (ui_); loads hand theirs over
// through one atomic word (uiShared_: charExpanded, scTab and a load generation), which the message thread adopts at
// its next sync (every uiState() call, every SetupWatcher tick, a save or load on the message thread) and otherwise
// refreshes from ui_, so a save on another thread reads a race-free UiState at most one sync (50 ms) old. The notice
// is published through fcdsp::Seqlock (01 §6.1) once per accepted load and read by stateNotice() on any thread;
// concurrent loads serialise only their publish (noticeWrite_), never a reader.
//
// Buses (proc.layout, B §7.4.7): main in -> out 1->1, 1->2 and 2->2 are accepted, 2->1 is rejected; the side-chain
// input is optional (disabled, mono or stereo; never disabled under an Audio Unit wrapper, which has no disabled buses:
// S13 H1b). Product constants come from FcmpProduct.h, never JucePlugin_*.
//
// P2 and P3 never touch Processor.cpp (SPRINTS §7 D20): state goes through State.h's entry points, presets through
// makePresetAccess (Presets.cpp), both with contexts this processor builds.
#pragma once

#include "plugin/ProcessorFacade.h"
#include "plugin/SetupWatcher.h"
#include "plugin/State.h"

#include "fcdsp/core/Rt.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/Seqlock.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/params/JuceParamPort.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace fcmp
{
    class Processor;

    // ---- the plugin glue, one TU each (so the owners of State/Presets never edit this processor) ----------------------
    // ParamLayout.cpp: the 29 APVTS parameters in kApvtsOrder (01 §3.1, §3.3): Float parameters on a three-lambda
    // NormalisableRange over fcdsp::toPlain/toNorm/legal, Int/Choice as JUCE's own discrete types, the five switches as
    // AudioParameterBool whose raw value keeps the host's position (ON iff >= 0.5, applied on read); labels "",
    // universal names, version hints, automatable per kHostParams, value text through HostText. The lambdas keep a
    // reference to the processor, which owns the parameters, so they never outlive it.
    juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout(const Processor&);

    // HostText.cpp (01 §4.6): the host value text of `plain` for any of the 29 parameters, cut to maximumLength
    // characters when that is > 0. Mode-filtered Pids resolve currentRaw() with v[pid] = plain (fcdsp::formatHost); the
    // 7 globals are formatted here: mode = the registered Mode's name ("—" for an unassigned slot, the successor's
    // name for a retired one), quality/labudget = their choices, extkey/listen/delta/bypass = "OFF"/"ON". Thread-safe:
    // relaxed loads and pure functions only.
    juce::String hostText(const Processor&, fcdsp::Pid, float plain, int maximumLength);
    // The inverse: true and the host plain value for a text the parameter accepts (fcdsp::parseHost for Mode-filtered
    // Pids: step labels and texts, Mode display numbers, universal units, '-' or U+2212). false: no parse.
    bool parseHostText(const Processor&, fcdsp::Pid, const juce::String& text, float& plainOut);

    // Presets.cpp: the processor's PresetAccess (02 §9.5). Until P3 it is empty; P3 replaces the body with the
    // FunkPresets-backed one, whose hooks bracket applies with the facade's batch and which installs the <PRESET> state
    // hooks through stateHooks (01 §9.2). Called once, from the processor's constructor, after the parameters exist.
    struct PresetContext
    {
        juce::AudioProcessorValueTreeState& apvts;
        ProcessorFacade& facade;
        StateHooks& stateHooks;
    };
    std::unique_ptr<PresetAccess> makePresetAccess(const PresetContext&);

    class Processor final : public juce::AudioProcessor, public ProcessorFacade
    {
    public:
        Processor();
        ~Processor() override;

        Processor(const Processor&) = delete;
        Processor& operator=(const Processor&) = delete;

        // ---- juce::AudioProcessor ---------------------------------------------------------------------------------
        const juce::String getName() const override;
        bool acceptsMidi() const override;
        bool producesMidi() const override;
        bool isMidiEffect() const override;
        double getTailLengthSeconds() const override;     // the Mode's tail at the current values + latency / fs

        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        void reset() noexcept FCDSP_NONBLOCKING override;                         // EngineHost::reset (RT-safe)
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept FCDSP_NONBLOCKING override;
        void processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                  juce::MidiBuffer& midi) noexcept FCDSP_NONBLOCKING override;
        using juce::AudioProcessor::processBlock;               // the double-precision overloads keep JUCE's defaults
        using juce::AudioProcessor::processBlockBypassed;
        juce::AudioProcessorParameter* getBypassParameter() const override;   // the `bypass` parameter (HR B §1.6)

        bool hasEditor() const override;
        juce::AudioProcessorEditor* createEditor() override;

        int getNumPrograms() override;                           // 1: no host programs (K2 #25d)
        int getCurrentProgram() override;
        void setCurrentProgram(int index) override;
        const juce::String getProgramName(int index) override;
        void changeProgramName(int index, const juce::String& newName) override;

        void getStateInformation(juce::MemoryBlock& destData) override;          // State.h saveState
        void setStateInformation(const void* data, int sizeInBytes) override;    // State.h loadState

        // ---- ProcessorFacade (02 §9.5) -----------------------------------------------------------------------------
        funkgui::ParamPort& port(fcdsp::Pid) override;           // 29 ports in Pid order, owned here (K2 #27)
        fcdsp::RawParams currentRaw() const override;            // relaxed loads + the configured lookahead budget
        bool readUiFrame(fcdsp::UiFrame&) const override;
        const fcdsp::HistoryRing& history() const override;
        void setUiAttached(bool) override;                       // EngineHost's attach count (01 §6.3)
        UiState& uiState() override;                             // message thread: syncs, then the editor's own copy
        StateNotice stateNotice() const override;                // any thread (Seqlock)
        void beginBatch() override;                              // nestable; the audio thread keeps the previous
        void endBatch() override;                                //   BlockParams; the outermost end raises the snap
        PresetAccess& presets() override;
        Diagnostics diagnostics() const override;                // v1.2 (ADR-85): message thread

        // ---- plugin internals: the glue TUs, SetupWatcher and the proc.* probes -----------------------------------
        juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }
        juce::RangedAudioParameter& parameter(fcdsp::Pid) const noexcept;      // Pid order (any of the 29)
        float rawValue(fcdsp::Pid) const noexcept;               // the APVTS raw plain value (relaxed); any thread
        fcdsp::HostConfig setup() const;                         // the configured setup (fs, block, quality, budget,
                                                                 //   channels); message thread
        SetupWatcher& setupWatcher() noexcept { return setup_; }
        int batchDepth() const noexcept { return batch_.load(std::memory_order_acquire); }

    private:
        friend class SetupWatcher;

        struct Globals                                           // the non-Mode-filtered switches BlockParams carries
        {
            bool bypass = false, delta = false, listen = false, extKey = false;
        };

        void snapshot(fcdsp::RawParams&) const noexcept;         // the 22 raw values, effective slot, configured budget
        Globals globals() const noexcept;
        // raw -> resolveSlot -> resolve -> BlockParams (scratch: a Resolution the caller owns)
        static void buildBlockParams(const fcdsp::RawParams&, const Globals&, fcdsp::Resolution& scratch,
                                     fcdsp::BlockParams& out) noexcept;
        void pullBlockParams() noexcept FCDSP_NONBLOCKING;       // audio thread: skipped while a batch is open
        void syncUi() noexcept;                                  // message thread: adopt a loaded UiState, or mirror ui_
        void publishLoadedUi(const UiState&) noexcept;           // any thread: a load's UiState, next generation
        void render(juce::AudioBuffer<float>&, bool hostBypassed) noexcept FCDSP_NONBLOCKING;
        // ADR-85: one block's DSP time against its real time: the smoothed load, its falling peak, overruns and blocks.
        void noteLoad(double elapsedSeconds, int samples) noexcept FCDSP_NONBLOCKING;
        // setupMutex_ held, the audio thread not running (prepareToPlay, or SetupWatcher under suspendProcessing):
        // publishes the configured setup, rebuilds block_ (unless a batch is open) and configures the engine.
        // Returns the new latency.
        int configureEngine();

        // Declaration order is destruction order in reverse: the watcher stops first, the ports go before the APVTS,
        // and the parameters themselves (owned by juce::AudioProcessor) go last.
        fcdsp::EngineHost engine_;
        juce::AudioProcessorValueTreeState apvts_;
        std::array<juce::RangedAudioParameter*, fcdsp::kNumParams> params_{};     // Pid order
        std::array<std::atomic<float>*, fcdsp::kNumParams> raw_{};                // Pid order
        std::array<std::unique_ptr<funkgui::JuceParamPort>, fcdsp::kNumParams> ports_{};

        // audio-thread state (also written by prepareToPlay and by SetupWatcher while processing is suspended)
        fcdsp::BlockParams block_{};                             // the previous BlockParams (reused during a batch)
        fcdsp::Resolution resolution_{};                         // resolve() scratch

        // batch (K2 #23): depth, and an epoch every beginBatch bumps, so a snapshot that raced a batch is discarded
        std::atomic<int> batch_{0};
        std::atomic<std::uint32_t> batchEpoch_{0};
        std::atomic<bool> snapPending_{false};                   // endBatch -> requestSnap at the next block start

        // setup (prepareToPlay / SetupWatcher). cfg_ and configured_ under setupMutex_; the atomics publish the
        // configured quality/budget, latency and rate to the audio thread and to readers on any thread.
        mutable std::mutex setupMutex_;
        fcdsp::HostConfig cfg_{};
        bool configured_ = false;                                // prepareToPlay has configured the engine
        std::atomic<std::uint8_t> quality_{0};
        std::atomic<std::uint8_t> budget_{0};
        std::atomic<int> latency_{0};
        std::atomic<double> sampleRate_{0.0};

        // ADR-85, the DSP load (render() writes, diagnostics() reads; relaxed: they are readings, not synchronisation).
        // prepareToPlay zeroes them, the audio thread not running.
        std::atomic<float> loadAvg_{0.0f};
        std::atomic<float> loadPeak_{0.0f};
        std::atomic<std::uint32_t> overruns_{0};
        std::atomic<std::uint32_t> blocks_{0};

        // UI state and notices (the header comment): ui_ and uiGeneration_ belong to the message thread; uiShared_ is
        // the handoff word; noticeSerial_ is guarded by noticeWrite_, which only publishers take.
        UiState ui_{};
        std::uint32_t uiGeneration_ = 0;
        std::atomic<std::uint32_t> uiShared_{ 0 };
        fcdsp::Seqlock<StateNotice> notice_{};
        std::mutex noticeWrite_;
        std::uint32_t noticeSerial_ = 0;
        StateHooks stateHooks_{};
        std::unique_ptr<PresetAccess> presets_;
        SetupWatcher setup_{ *this };

        JUCE_LEAK_DETECTOR(Processor)
    };

    // The editor factory. Exactly one of Source/plugin/CreateEditorGpu.cpp and CreateEditorGeneric.cpp is compiled into
    // each target, chosen by cmake/FcmpSources.cmake (no #if; SPRINTS §7 D22).
    juce::AudioProcessorEditor* createEditor(Processor& processor);
} // namespace fcmp
