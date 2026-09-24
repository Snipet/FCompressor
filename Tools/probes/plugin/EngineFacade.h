// Tools/probes/plugin/EngineFacade.h — a ProcessorFacade over a REAL fcdsp::EngineHost (U2, S7; SPRINTS S7.2 and D11:
// "ui.truth runs on EngineFacade, no JUCE processor"), so the UI truth probes compare what the Panel draws with what
// the engine did: 48 kHz, blocks of 128, a deterministic program, the engine's own UiFrame and HistoryRing, and a
// TestTap on every sample. A helper of fcmp_probe_plugin, not a probe (no FCMP_PROBE line).
//
// What comes from where:
// - Parameters, UiState, StateNotice, PresetAccess and the batch counter are a FakeFacade's (in-memory ports that hold
//   plain values, gesture and batch logging); the probe scripts them through params() as a host would.
// - Telemetry is the engine's: readUiFrame → EngineHost::readUiFrame, history → EngineHost::history, setUiAttached →
//   EngineHost::setUiAttached (a count; the probe attaches when it opens its Panel, as the editor does).
// - Every block is built as Processor::processBlock builds it (01 §2.3; P1): the raw snapshot (currentRaw, budget
//   included) → resolveSlot → resolve → BlockParams{slot, eng, bypass, delta, listen, extKey}; while a batch is open the
//   previous BlockParams are reused (K2 #23) and the outermost endBatch() raises an engine snap.
// - The HostConfig (quality, lookahead budget) is read from the ports at construction and by reconfigure(), which is
//   what SetupWatcher does on the message thread; a Mode change needs no reconfigure (the host crossfades).
//
// Single-threaded: the probe thread is both the audio thread (render) and the message thread (the Panel).
#pragma once

#include "FakeFacade.h"

#include "plugin/ProcessorFacade.h"

#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/engine/TestTap.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace fcdsp
{
    struct ModeEntry;
}

namespace fcmp::probe
{
    // A deterministic mono program (both input channels carry it): sine segments computed in closed form from the
    // absolute sample index (Signals.h, no libm), each starting at peakDb dBFS and falling decayDbPerS dB per second;
    // peakDb <= -200 is silence. The same program at any block size gives the same samples.
    class Program
    {
    public:
        struct Tone
        {
            double seconds = 0.0;
            float  peakDb = -200.0f;
            float  hz = 1000.0f;
            float  decayDbPerS = 0.0f;
        };

        explicit Program(std::vector<Tone>, double fs = 48000.0);

        uint64_t length() const noexcept { return length_; }               // samples
        float    sample(uint64_t n) const noexcept;                         // 0 past the end
        float    envelopeDb(uint64_t n) const noexcept;                     // the segment's level at n (dBFS peak)
        int      segmentAt(uint64_t n) const noexcept;                      // -1 past the end
        uint64_t segmentStart(int i) const noexcept;

    private:
        std::vector<Tone>     tones_;
        std::vector<uint64_t> starts_;
        double   fs_;
        uint64_t length_ = 0;
    };

    class EngineFacade final : public ProcessorFacade
    {
    public:
        static constexpr double kFs = 48000.0;
        static constexpr int    kBlock = 128;

        // Ports at their host defaults with `modeKey` selected (an unknown key keeps slot 0), the engine configured,
        // and a TestTap over the first `tapSamples` samples (grDb, detDb, tgtDb).
        EngineFacade(std::string_view modeKey, uint64_t tapSamples);
        ~EngineFacade() override;

        EngineFacade(const EngineFacade&) = delete;
        EngineFacade& operator=(const EngineFacade&) = delete;

        // ProcessorFacade
        funkgui::ParamPort&       port(fcdsp::Pid) override;
        fcdsp::RawParams          currentRaw() const override;
        bool                      readUiFrame(fcdsp::UiFrame&) const override;
        const fcdsp::HistoryRing& history() const override;
        void                      setUiAttached(bool) override;
        UiState&                  uiState() override;
        StateNotice               stateNotice() const override;
        void                      beginBatch() override;
        void                      endBatch() override;           // the outermost end requests an engine snap
        PresetAccess&             presets() override;

        // the processor side
        FakeFacade&              params() noexcept { return params_; }   // host / automation scripting
        fcdsp::EngineHost&       host() noexcept { return *host_; }
        const fcdsp::ModeEntry*  entry() const noexcept;                  // the Mode the raw values select
        fcdsp::BlockParams       blockParams();                           // what the next block runs
        void                     reconfigure();                           // SetupWatcher: quality / budget from ports
        int                      latencySamples() const noexcept;

        // Renders `blocks` blocks of kBlock samples of `program` from where the last render stopped (the program's
        // sample index is the host's), recording the input, the output and the tap.
        void     render(const Program&, int blocks);
        uint64_t processed() const noexcept { return processed_; }

        // Recorded so far (channel 0 / 1), indexed by absolute sample.
        std::span<const float> input(int ch) const noexcept;
        std::span<const float> output(int ch) const noexcept;
        // The tap's per-sample values of the incoming path, lanes 0-1 (the engine's own ControlIo outputs).
        float tapGr(uint64_t n, int lane) const noexcept;
        float tapDet(uint64_t n, int lane) const noexcept;       // detector-domain level (after preGain)
        float tapTgt(uint64_t n, int lane) const noexcept;
        uint64_t tapped() const noexcept { return tap_.written; }

    private:
        FakeFacade                         params_;
        std::unique_ptr<fcdsp::EngineHost> host_;
        fcdsp::BlockParams                 last_{};              // reused while a batch is open (K2 #23)
        bool                               haveLast_ = false;
        uint64_t                           processed_ = 0;
        std::vector<float>                 in_[2], out_[2];
        std::vector<fcdsp::simd::f32x4>    gr_, det_, tgt_;
        fcdsp::TestTap                     tap_{};
    };
}
