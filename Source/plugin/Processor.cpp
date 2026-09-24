// Source/plugin/Processor.cpp: the FCompressor processor and its SetupWatcher (P1, S7). See Processor.h and
// SetupWatcher.h for the contracts; 01 §2.3, §3.1, §5.4-5.6; 02 §9.5; K2 #6, #7, #23, #25, #27.
#include "plugin/Processor.h"

#include "FcmpProduct.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// The plugin wrappers' factory (declared by JUCE only inside juce_audio_plugin_client).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace fcmp
{
    namespace
    {
        using fcdsp::Pid;

        // Index globals from their raw (plain) values. The APVTS holds rounded values for the Int/Choice parameters (the
        // switches keep the host's position: ParamLayout.cpp); these clamp and round anyway, so a NaN or an
        // out-of-range raw can never index out of bounds.
        int indexOf(float plain, int hi) noexcept
        {
            if (!(plain > 0.0f))                                   // also NaN
                return 0;
            if (plain >= static_cast<float>(hi))
                return hi;
            return static_cast<int>(plain + 0.5f);
        }

        fcdsp::Quality qualityOf(float plain) noexcept { return static_cast<fcdsp::Quality>(indexOf(plain, 2)); }
        fcdsp::LookaheadBudget budgetOf(float plain) noexcept
        {
            return static_cast<fcdsp::LookaheadBudget>(indexOf(plain, 2));
        }
        int modeSlotOf(float plain) noexcept { return indexOf(plain, fcdsp::kModeCapacity - 1); }
        bool isOn(float plain) noexcept { return plain >= 0.5f; }
    } // namespace

    // ==== construction =============================================================================================

    Processor::Processor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                   .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
          apvts_(*this, nullptr, "PARAMS", makeParameterLayout(*this))       // no UndoManager: undo is the host's (K2 #7)
    {
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const fcdsp::HostParam& h = fcdsp::kHostParams[i];
            params_[i] = apvts_.getParameter(h.id);
            raw_[i] = apvts_.getRawParameterValue(h.id);
            jassert(params_[i] != nullptr && raw_[i] != nullptr);   // makeParameterLayout builds every kHostParams id
            if (params_[i] != nullptr)
                ports_[i] = std::make_unique<funkgui::JuceParamPort>(*params_[i]);
            // A fresh instance holds the table's defaults exactly. The APVTS seeds its raw value through the host map
            // (toPlain(toNorm(def)): makeup 3.6e-7 dB, knee 5.9999995, ...; K2 #14), while the parameter itself
            // already holds def; nothing listens yet, and a later host write goes through the map as usual.
            if (raw_[i] != nullptr)
                raw_[i]->store(h.def, std::memory_order_relaxed);
        }

        cfg_.quality = qualityOf(rawValue(Pid::quality));
        cfg_.budget = budgetOf(rawValue(Pid::labudget));
        quality_.store(static_cast<std::uint8_t>(cfg_.quality), std::memory_order_relaxed);
        budget_.store(static_cast<std::uint8_t>(cfg_.budget), std::memory_order_relaxed);
        const int latency = fcdsp::EngineHost::latencyFor(cfg_);
        latency_.store(latency, std::memory_order_relaxed);
        setLatencySamples(latency);                               // what hosts read before the first prepareToPlay

        presets_ = makePresetAccess(PresetContext{ apvts_, *this, stateHooks_ });
        setup_.start();
    }

    Processor::~Processor()
    {
        setup_.stop();                                            // no tick may reach a half-destroyed processor
    }

    // ==== identity, buses, programs ================================================================================

    const juce::String Processor::getName() const { return product::kName; }
    bool Processor::acceptsMidi() const { return false; }
    bool Processor::producesMidi() const { return false; }
    bool Processor::isMidiEffect() const { return false; }

    double Processor::getTailLengthSeconds() const
    {
        fcdsp::RawParams raw;
        snapshot(raw);
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
        double tail = 0.0;
        if (ms.entry != nullptr && ms.entry->desc != nullptr && ms.entry->desc->tailSeconds != nullptr)
        {
            fcdsp::Resolution r;
            fcdsp::resolve(*ms.entry, raw, r);
            tail = static_cast<double>(ms.entry->desc->tailSeconds(r.eng));
        }
        const double fs = sampleRate_.load(std::memory_order_relaxed);
        if (fs > 0.0)
            tail += static_cast<double>(latency_.load(std::memory_order_relaxed)) / fs;
        return std::isfinite(tail) && tail > 0.0 ? tail : 0.0;
    }

    bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        const bool mainOk = (in == mono && out == mono) || (in == mono && out == stereo) || (in == stereo && out == stereo);
        if (!mainOk || layouts.outputBuses.size() != 1 || layouts.inputBuses.size() > 2)
            return false;
        if (layouts.inputBuses.size() == 2)
        {
            const auto sidechain = layouts.getChannelSet(true, 1);
            if (!(sidechain.isDisabled() || sidechain == mono || sidechain == stereo))
                return false;
        }
        return true;
    }

    bool Processor::hasEditor() const { return true; }
    juce::AudioProcessorEditor* Processor::createEditor() { return fcmp::createEditor(*this); }

    // One program: FCompressor's presets live in its own browser, and a program list would make JUCE's VST3 wrapper add
    // a "Program" parameter whose step count moves with the bank (K2 #25d).
    int Processor::getNumPrograms() { return 1; }
    int Processor::getCurrentProgram() { return 0; }
    void Processor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
    const juce::String Processor::getProgramName(int index)
    {
        juce::ignoreUnused(index);
        return "Default";
    }
    void Processor::changeProgramName(int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

    juce::AudioProcessorParameter* Processor::getBypassParameter() const { return params_[fcdsp::idx(Pid::bypass)]; }

    // ==== state (State.h: P1's APVTS-only body, replaced by P2) ======================================================

    void Processor::getStateInformation(juce::MemoryBlock& destData)
    {
        saveState(StateContext{ apvts_, *this, ui_, notice_, stateHooks_ }, destData);
    }

    void Processor::setStateInformation(const void* data, int sizeInBytes)
    {
        loadState(StateContext{ apvts_, *this, ui_, notice_, stateHooks_ }, data, sizeInBytes);
    }

    // ==== parameters ===============================================================================================

    juce::RangedAudioParameter& Processor::parameter(Pid p) const noexcept
    {
        const std::size_t i = fcdsp::idx(p) < fcdsp::kNumParams ? fcdsp::idx(p) : 0;
        jassert(fcdsp::idx(p) < fcdsp::kNumParams && params_[i] != nullptr);
        return *params_[i];
    }

    float Processor::rawValue(Pid p) const noexcept
    {
        const std::size_t i = fcdsp::idx(p);
        if (i >= fcdsp::kNumParams)
            return 0.0f;
        const std::atomic<float>* r = raw_[i];
        return r != nullptr ? r->load(std::memory_order_relaxed) : fcdsp::kHostParams[i].def;
    }

    void Processor::snapshot(fcdsp::RawParams& raw) const noexcept
    {
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = rawValue(static_cast<Pid>(i));
        raw.modeSlot = fcdsp::resolveSlot(modeSlotOf(rawValue(Pid::mode))).slot;
        raw.budget = static_cast<fcdsp::LookaheadBudget>(budget_.load(std::memory_order_relaxed));
    }

    Processor::Globals Processor::globals() const noexcept
    {
        Globals g;
        g.bypass = isOn(rawValue(Pid::bypass));
        g.delta = isOn(rawValue(Pid::delta));
        g.listen = isOn(rawValue(Pid::listen));
        g.extKey = isOn(rawValue(Pid::extkey));
        return g;
    }

    void Processor::buildBlockParams(const fcdsp::RawParams& raw, const Globals& g, fcdsp::Resolution& scratch,
                                     fcdsp::BlockParams& out) noexcept
    {
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(raw.modeSlot);
        out.slot = ms.slot;
        if (ms.entry != nullptr)
        {
            fcdsp::resolve(*ms.entry, raw, scratch);
            out.eng = scratch.eng;
        }
        else
            out.eng = fcdsp::EngineParams{};                      // empty registry: the host passes audio through
        out.bypass = g.bypass;
        out.delta = g.delta;
        out.listen = g.listen;
        out.extKey = g.extKey;
    }

    fcdsp::RawParams Processor::currentRaw() const
    {
        fcdsp::RawParams raw;
        snapshot(raw);
        return raw;
    }

    fcdsp::HostConfig Processor::setup() const
    {
        const std::lock_guard<std::mutex> lock(setupMutex_);
        return cfg_;
    }

    // ==== batches (K2 #23) ===========================================================================================
    // beginBatch bumps the epoch and the depth, then a release fence orders both before the parameter writes that
    // follow. The audio thread loads the epoch, checks the depth, snapshots, and after an acquire fence re-reads the
    // epoch: if any raw value it loaded came from a write inside a batch, the fence pair makes the bumped epoch visible,
    // and the snapshot is discarded (the previous BlockParams stay). endBatch's release decrement publishes the batch's
    // writes to a block that sees depth 0; the outermost end raises the snap, which the audio thread passes to the
    // engine AFTER its parameter pull, so the snap always applies to the new set (HR's render-order fix).

    void Processor::beginBatch()
    {
        batchEpoch_.fetch_add(1, std::memory_order_relaxed);
        batch_.fetch_add(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
    }

    void Processor::endBatch()
    {
        int depth = batch_.load(std::memory_order_relaxed);
        while (depth > 0 && !batch_.compare_exchange_weak(depth, depth - 1, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed))
        {
        }
        jassert(depth > 0);                                       // endBatch without beginBatch: ignored
        if (depth == 1)
            snapPending_.store(true, std::memory_order_release);
    }

    // ==== facade: ports, telemetry, UI state, presets ================================================================

    funkgui::ParamPort& Processor::port(Pid p)
    {
        const std::size_t i = fcdsp::idx(p) < fcdsp::kNumParams ? fcdsp::idx(p) : 0;
        jassert(fcdsp::idx(p) < fcdsp::kNumParams);
        return *ports_[i];
    }

    bool Processor::readUiFrame(fcdsp::UiFrame& frame) const { return engine_.readUiFrame(frame); }
    const fcdsp::HistoryRing& Processor::history() const { return engine_.history(); }
    void Processor::setUiAttached(bool attached) { engine_.setUiAttached(attached); }
    UiState& Processor::uiState() { return ui_; }
    StateNotice Processor::stateNotice() const { return notice_; }
    PresetAccess& Processor::presets() { return *presets_; }

    // ==== setup: prepareToPlay and the configured engine =============================================================

    int Processor::configureEngine()
    {
        quality_.store(static_cast<std::uint8_t>(cfg_.quality), std::memory_order_relaxed);
        budget_.store(static_cast<std::uint8_t>(cfg_.budget), std::memory_order_relaxed);
        if (batch_.load(std::memory_order_acquire) == 0)
        {
            fcdsp::RawParams raw;
            snapshot(raw);                                        // with the budget just published
            buildBlockParams(raw, globals(), resolution_, block_);
        }
        engine_.configure(cfg_, block_);                          // the only allocation point; engines start snapped
        const int latency = engine_.latencySamples();
        latency_.store(latency, std::memory_order_relaxed);
        sampleRate_.store(cfg_.fs, std::memory_order_relaxed);
        configured_ = true;
        return latency;
    }

    void Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        int latency = 0;
        {
            const std::lock_guard<std::mutex> lock(setupMutex_);
            cfg_.fs = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
            cfg_.maxBlock = std::max(1, maximumExpectedSamplesPerBlock);
            cfg_.quality = qualityOf(rawValue(Pid::quality));
            cfg_.budget = budgetOf(rawValue(Pid::labudget));
            cfg_.mainIns = std::clamp(getMainBusNumInputChannels(), 0, 2);
            cfg_.mainOuts = std::clamp(getMainBusNumOutputChannels(), 0, 2);
            cfg_.keyChans = getBusCount(true) > 1 ? std::clamp(getChannelCountOfBus(true, 1), 0, 2) : 0;
            latency = configureEngine();
        }
        setLatencySamples(latency);                               // right after configure, never from the audio thread
    }

    void Processor::releaseResources() {}                        // the engine stays configured for the next prepare

    // ==== the audio thread =========================================================================================

    void Processor::reset() noexcept FCDSP_NONBLOCKING { engine_.reset(); }

    void Processor::pullBlockParams() noexcept FCDSP_NONBLOCKING
    {
        const std::uint32_t epoch = batchEpoch_.load(std::memory_order_acquire);
        if (batch_.load(std::memory_order_acquire) != 0)
            return;                                               // a batch is open: keep the previous BlockParams
        fcdsp::RawParams raw;
        snapshot(raw);
        const Globals g = globals();
        std::atomic_thread_fence(std::memory_order_acquire);
        if (batchEpoch_.load(std::memory_order_relaxed) != epoch)
            return;                                               // a batch opened during the snapshot: discard it
        buildBlockParams(raw, g, resolution_, block_);
    }

    void Processor::render(juce::AudioBuffer<float>& buffer, bool hostBypassed) noexcept FCDSP_NONBLOCKING
    {
        const bool snap = snapPending_.exchange(false, std::memory_order_acquire);
        pullBlockParams();
        if (snap)
            engine_.requestSnap();                                // consumed at this block's start: after the pull

        const int total = buffer.getNumChannels();
        const int n = buffer.getNumSamples();
        if (total <= 0 || n <= 0)
            return;
        float* const* ch = buffer.getArrayOfWritePointers();

        // JUCE's process buffer: input channels bus by bus (main, then side chain) and output channels from 0, over
        // the same channel pointers; EngineHost reads each chunk's input and key before it writes that chunk's output,
        // so the aliasing (in-place main; 1->2 with a key, whose first key channel is output channel 1) is safe.
        const int mainInsBus = getMainBusNumInputChannels();
        const int mainIns = std::clamp(std::min(mainInsBus, total), 0, 2);
        const int mainOuts = std::clamp(std::min(getMainBusNumOutputChannels(), total), 0, 2);
        int keys = getBusCount(true) > 1 ? std::min(getChannelCountOfBus(true, 1), 2) : 0;
        if (keys > 0 && mainInsBus + keys > total)
            keys = std::max(0, total - mainInsBus);
        if (mainOuts <= 0)
            return;

        const float* ins[2] = { nullptr, nullptr };
        const float* key[2] = { nullptr, nullptr };
        float* outs[2] = { nullptr, nullptr };
        for (int c = 0; c < mainIns; ++c)
            ins[c] = ch[c];
        for (int c = 0; c < keys; ++c)
            key[c] = ch[mainInsBus + c];
        for (int c = 0; c < mainOuts; ++c)
            outs[c] = ch[c];

        fcdsp::ProcessIo io;
        io.in = mainIns > 0 ? ins : nullptr;
        io.numIn = mainIns;
        io.key = keys > 0 ? key : nullptr;
        io.numKey = keys;
        io.out = outs;
        io.numOut = mainOuts;
        io.n = n;
        io.hostBypassed = hostBypassed;
        engine_.process(io, block_);
    }

    void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept FCDSP_NONBLOCKING
    {
        juce::ignoreUnused(midi);
        render(buffer, false);
    }

    // The host's own bypass when it does not drive the `bypass` parameter (HR B §1.6): the same 20 ms ramp against
    // the latency-aligned dry path, the processing running on underneath.
    void Processor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi) noexcept FCDSP_NONBLOCKING
    {
        juce::ignoreUnused(midi);
        render(buffer, true);
    }

    // ==== SetupWatcher (SetupWatcher.h) ==============================================================================

    SetupWatcher::SetupWatcher(Processor& processor) noexcept : processor_(processor) {}

    SetupWatcher::~SetupWatcher() { stopTimer(); }

    void SetupWatcher::start()
    {
        lastSlot_ = fcdsp::resolveSlot(modeSlotOf(processor_.rawValue(Pid::mode))).slot;
        startTimerHz(kHz);
    }

    void SetupWatcher::stop() { stopTimer(); }

    void SetupWatcher::timerCallback() { poll(); }

    void SetupWatcher::poll()
    {
        Processor& p = processor_;

        // 1. quality / labudget
        const fcdsp::Quality q = qualityOf(p.rawValue(Pid::quality));
        const fcdsp::LookaheadBudget b = budgetOf(p.rawValue(Pid::labudget));
        const bool differs = static_cast<std::uint8_t>(q) != p.quality_.load(std::memory_order_relaxed)
                          || static_cast<std::uint8_t>(b) != p.budget_.load(std::memory_order_relaxed);
        if (differs)
        {
            const bool wasSuspended = p.isSuspended();
            if (!wasSuspended)
                p.suspendProcessing(true);                        // takes and releases the callback lock
            int latency = -1;
            {
                const std::lock_guard<std::mutex> lock(p.setupMutex_);
                if (q != p.cfg_.quality || b != p.cfg_.budget)    // prepareToPlay may have applied it meanwhile
                {
                    p.cfg_.quality = q;
                    p.cfg_.budget = b;
                    if (p.configured_)
                        latency = p.configureEngine();
                    else
                    {
                        p.quality_.store(static_cast<std::uint8_t>(q), std::memory_order_relaxed);
                        p.budget_.store(static_cast<std::uint8_t>(b), std::memory_order_relaxed);
                        latency = fcdsp::EngineHost::latencyFor(p.cfg_);
                        p.latency_.store(latency, std::memory_order_relaxed);
                    }
                }
            }
            if (latency >= 0)
            {
                p.setLatencySamples(latency);
                ++reconfigures_;
            }
            if (!wasSuspended)
                p.suspendProcessing(false);
        }

        // 2. the Mode-change announcement
        const int slot = fcdsp::resolveSlot(modeSlotOf(p.rawValue(Pid::mode))).slot;
        if (slot != lastSlot_)
        {
            lastSlot_ = slot;
            ++announcements_;
            p.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
        }
    }
} // namespace fcmp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fcmp::Processor();
}
