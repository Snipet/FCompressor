// Source/plugin/Processor.cpp: the FCompressor processor and its SetupWatcher (P1, S7). See Processor.h and
// SetupWatcher.h for the contracts; 01 §2.3, §3.1, §5.4-5.6; 02 §9.5; K2 #6, #7, #23, #25, #27.
#include "plugin/Processor.h"

#include "FcmpProduct.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <funkgui/core/Env.h>
#include <funkgui/prefs/UiPreferences.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <system_error>
#include <thread>

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

        // The UI handoff word (Processor.h): b0 charExpanded, b1 scTab == colour, b8-31 the load generation (it wraps;
        // only equality is used).
        constexpr std::uint32_t kUiExpanded = 1u, kUiColour = 2u;
        constexpr int kUiGenerationShift = 8;

        std::uint32_t packUi(const UiState& ui, std::uint32_t generation) noexcept
        {
            return (ui.charExpanded ? kUiExpanded : 0u) | (ui.scTab == ScTab::colour ? kUiColour : 0u)
                 | generation << kUiGenerationShift;
        }

        UiState unpackUi(std::uint32_t word) noexcept
        {
            UiState ui;
            ui.charExpanded = (word & kUiExpanded) != 0u;
            ui.scTab = (word & kUiColour) != 0u ? ScTab::colour : ScTab::sidechain;
            return ui;
        }

        std::uint32_t generationOf(std::uint32_t word) noexcept { return word >> kUiGenerationShift; }

        bool onMessageThread() noexcept { return juce::MessageManager::existsAndIsCurrentThread(); }

        // ADR-85: the DSP load's smoothing and its peak's fall (seconds of audio).
        constexpr double kLoadAvgS = 0.5;
        constexpr double kLoadPeakS = 2.0;

        // ADR-85: a machine-wide preference a new instance starts from (ProcessorFacade.h kPrefNew*). The file is
        // UiPreferences' (<PREFIX>PREFS_DIR's preferences.settings, else its default file), read here through a
        // PropertiesFile of this call's own: a host may construct the processor on any thread, and UiPreferences
        // belongs to the message thread. A missing key, a damaged file or a value that is not a decimal integer in
        // [lo, hi] reads as -1.
        int newInstancePref(const char* key, int lo, int hi)
        {
            const char* dir = funkgui::env("PREFS_DIR");
            const juce::File file = dir != nullptr && dir[0] != '\0'
                                        ? juce::File(juce::String::fromUTF8(dir)).getChildFile("preferences.settings")
                                        : funkgui::UiPreferences::defaultFile();
            if (!file.existsAsFile())
                return -1;
            juce::PropertiesFile::Options o;
            o.doNotSave = true;                                    // read only: UiPreferences writes this file
            const juce::PropertiesFile props(file, o);
            if (!props.containsKey(key))
                return -1;
            const std::string text = props.getValue(key).toStdString();
            int v = -1;
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
            return ec == std::errc{} && end == text.data() + text.size() && v >= lo && v <= hi ? v : -1;
        }

        const char* formatName(juce::AudioProcessor::WrapperType w) noexcept
        {
            if (w == juce::AudioProcessor::wrapperType_VST3)
                return "VST3";
            if (w == juce::AudioProcessor::wrapperType_AudioUnit)
                return "AU";
            if (w == juce::AudioProcessor::wrapperType_AudioUnitv3)
                return "AUV3";
            if (w == juce::AudioProcessor::wrapperType_Standalone)
                return "STANDALONE";
            return "";                                             // undefined (a probe), or a format we do not build
        }
    } // namespace

    // ==== construction =============================================================================================

    Processor::Processor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                   .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
          apvts_(*this, nullptr, kStateType, makeParameterLayout(*this))      // no UndoManager: undo is the host's (K2 #7)
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

        // ADR-85: a new instance starts from the machine's QUALITY and LOOKAHEAD for new instances (the settings
        // screen), when set; a session or a state load then sets its own, and presets never carry these two.
        for (const auto& [pid, key] : { std::pair{ Pid::quality, kPrefNewQuality },
                                        std::pair{ Pid::labudget, kPrefNewLookahead } })
            if (const int v = newInstancePref(key, 0, 2); v >= 0)
                if (juce::RangedAudioParameter* prm = params_[fcdsp::idx(pid)]; prm != nullptr && raw_[fcdsp::idx(pid)] != nullptr)
                {
                    prm->setValue(prm->convertTo0to1(static_cast<float>(v)));   // nothing listens yet
                    raw_[fcdsp::idx(pid)]->store(static_cast<float>(v), std::memory_order_relaxed);
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

    // Main 1->1, 1->2, 2->2; the side chain mono or stereo, or disabled except under an Audio Unit wrapper (S13 H1b, lead
    // revision 5b). An AU has no disabled buses: its wrapper enables every bus at construction, the AU SDK refuses a
    // 0-channel stream format on any element, and JUCE's AU wrapper lists every layout this accepts as the element's
    // kAudioUnitProperty_SupportedChannelLayoutTags, where a disabled side chain appeared as a 0-channel
    // DiscreteInOrder tag (0x930000) that no host can set. The VST3 and Standalone layouts are unchanged (a VST3 host
    // deactivates the side-chain bus; the Standalone disables it).
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
            const bool audioUnit = wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_AudioUnitv3;
            const bool disabledOk = sidechain.isDisabled() && !audioUnit;
            if (!(disabledOk || sidechain == mono || sidechain == stereo))
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

    // Any thread (Processor.h, "The per-instance UI state and the state notice"). A save on the message thread first
    // syncs, so it holds exactly what the editor last wrote; elsewhere it reads the handoff word. saveState does not
    // read the notice (StateContext still needs one).
    void Processor::getStateInformation(juce::MemoryBlock& destData)
    {
        UiState ui;
        if (onMessageThread())
        {
            syncUi();
            ui = ui_;
        }
        else
            ui = unpackUi(uiShared_.load(std::memory_order_acquire));
        StateNotice unused{};
        saveState(StateContext{ apvts_, *this, ui, unused, stateHooks_ }, destData);
    }

    // Any thread. loadState fills a local UiState and a local notice (serial 0 + 1 marks an accepted load; a blob that is
    // not <PARAMS> leaves both untouched and changes nothing); the processor then hands the UiState over, numbers the
    // notice and publishes it.
    void Processor::setStateInformation(const void* data, int sizeInBytes)
    {
        UiState ui = unpackUi(uiShared_.load(std::memory_order_acquire));
        StateNotice notice{};
        loadState(StateContext{ apvts_, *this, ui, notice, stateHooks_ }, data, sizeInBytes);
        if (notice.serial == 0u)
            return;                                               // ignored (01 §9.1 load step 1): nothing changed
        publishLoadedUi(ui);
        {
            const std::lock_guard<std::mutex> lock(noticeWrite_); // publishers only: Seqlock has one writer at a time
            notice.serial = ++noticeSerial_;
            notice_.publish(notice);
        }
        if (onMessageThread())
            syncUi();                                             // the editor's copy follows at once
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
    UiState& Processor::uiState()
    {
        syncUi();
        return ui_;
    }

    StateNotice Processor::stateNotice() const
    {
        StateNotice n{};
        while (!notice_.read(n))                                  // a publish in progress: 64 bytes, then it is done
            std::this_thread::yield();
        return n;
    }

    PresetAccess& Processor::presets() { return *presets_; }

    Diagnostics Processor::diagnostics() const
    {
        Diagnostics d;
        d.version = product::kVersion;
        d.funkgui = product::kFunkGuiVersion;
        d.juce = product::kJuceVersion;
        d.format = formatName(wrapperType);
        const juce::String host = juce::PluginHostType().getHostDescription();
        if (host != "Unknown")
            host.copyToUTF8(d.host, sizeof d.host);
        {
            const std::lock_guard<std::mutex> lock(setupMutex_);
            d.prepared = configured_;
            d.sampleRate = configured_ ? cfg_.fs : 0.0;
            d.maxBlock = configured_ ? cfg_.maxBlock : 0;
            d.mainIns = cfg_.mainIns;
            d.mainOuts = cfg_.mainOuts;
            d.keyChans = cfg_.keyChans;
            d.quality = static_cast<int>(cfg_.quality);
            d.budget = static_cast<int>(cfg_.budget);
        }
        d.latencySamples = latency_.load(std::memory_order_relaxed);
        d.loadAvg = loadAvg_.load(std::memory_order_relaxed);
        d.loadPeak = loadPeak_.load(std::memory_order_relaxed);
        d.overruns = overruns_.load(std::memory_order_relaxed);
        d.blocks = blocks_.load(std::memory_order_relaxed);
        return d;
    }

    // Message thread. A generation the editor's copy has not seen is a load: adopt it. Otherwise mirror ui_ into the word
    // for the other threads' saves; a failed exchange means a load published meanwhile, which the next sync adopts.
    void Processor::syncUi() noexcept
    {
        std::uint32_t word = uiShared_.load(std::memory_order_acquire);
        if (generationOf(word) != uiGeneration_)
        {
            ui_ = unpackUi(word);
            uiGeneration_ = generationOf(word);
            return;
        }
        const std::uint32_t mine = packUi(ui_, uiGeneration_);
        if (mine != word)
            (void) uiShared_.compare_exchange_strong(word, mine, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // Any thread: the loaded UiState with the next generation (a load always wins over an unsynced editor write).
    void Processor::publishLoadedUi(const UiState& ui) noexcept
    {
        std::uint32_t word = uiShared_.load(std::memory_order_relaxed);
        while (!uiShared_.compare_exchange_weak(word, packUi(ui, generationOf(word) + 1u), std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
        {
        }
    }

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
            loadAvg_.store(0.0f, std::memory_order_relaxed);     // ADR-85: the audio thread is not running
            loadPeak_.store(0.0f, std::memory_order_relaxed);
            overruns_.store(0, std::memory_order_relaxed);
            blocks_.store(0, std::memory_order_relaxed);
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
        // ADR-85: the block's DSP time (mach_absolute_time: no syscall, no lock) for the settings screen's DSP LOAD.
        const juce::int64 t0 = juce::Time::getHighResolutionTicks();
        engine_.process(io, block_);
        noteLoad(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0), n);
    }

    void Processor::noteLoad(double elapsedSeconds, int samples) noexcept FCDSP_NONBLOCKING
    {
        const double fs = sampleRate_.load(std::memory_order_relaxed);
        if (!(fs > 0.0) || samples <= 0 || !(elapsedSeconds >= 0.0))
            return;
        const double real = static_cast<double>(samples) / fs;
        const auto load = static_cast<float>(elapsedSeconds / real);
        const auto avgStep = static_cast<float>(std::min(1.0, real / kLoadAvgS));
        const auto peakFall = static_cast<float>(std::min(1.0, real / kLoadPeakS));
        const float avg = loadAvg_.load(std::memory_order_relaxed);
        loadAvg_.store(avg + (load - avg) * avgStep, std::memory_order_relaxed);
        const float peak = loadPeak_.load(std::memory_order_relaxed);
        loadPeak_.store(std::max(load, peak - peak * peakFall), std::memory_order_relaxed);
        if (elapsedSeconds > real)
            overruns_.fetch_add(1, std::memory_order_relaxed);
        blocks_.fetch_add(1, std::memory_order_relaxed);
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

        // 3. the UI handoff (Processor.h): a load's UiState reaches the editor's copy, the editor's reaches the word
        p.syncUi();
    }
} // namespace fcmp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fcmp::Processor();
}
