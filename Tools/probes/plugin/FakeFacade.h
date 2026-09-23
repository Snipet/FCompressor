// Tools/probes/plugin/FakeFacade.h — a ProcessorFacade without a processor (02 §9.5; K3 #13), so UI work and UI probes
// never wait for the real one: 29 in-memory parameter ports, scripted UiFrames, a real HistoryRing the probe pushes
// into, a counting batch (K2 #23: probes assert that multi-parameter writes are batched) and a fake PresetAccess.
// Frozen at FZ4 (U1a); a helper, not a probe (no FCMP_PROBE line). Single-threaded: the probe thread is the message
// thread, the only producer of the ring and its only reader.
//
// Semantics:
// - Ports hold the PLAIN value, as the APVTS raw atomics do (a juce::RangedAudioParameter stores
//   convertFrom0to1(value01)): they start exactly at their kHostParams defaults (Mode = slot 0), a normalised write
//   stores toPlain(pid, v), and value01() is toNorm(pid, plain). Gesture calls are counted per port, and every
//   setValue01 is logged with whether it was inside a gesture and the batch depth, so a probe can assert "every write
//   inside begin/end" and "a Mode switch writes only `mode`" (02 §8.4).
// - currentRaw() is what Processor::currentRaw() returns: the plain value of every Mode-filtered port, the effective slot
//   (resolveSlot of the `mode` port) and the configured lookahead budget — the `labudget` port's, or a scripted one
//   (the real SetupWatcher applies a new budget at 20 Hz, so the configured budget can lag the port).
// - readUiFrame() returns false until publish(), then the last published frame; publish() stamps publishCount, so
//   every publish is a new frame for the UI's staleness.
// - The script calls (script01, setPlain, setMode, …) are a host or automation writing: no gesture, not logged.
#pragma once

#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/params/ParamPort.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fcmp::probe
{
    class FakeFacade;

    // One logged parameter write.
    struct FakeWrite
    {
        fcdsp::Pid pid;
        float      value01;
        bool       inGesture;                        // between this port's beginGesture and endGesture
        int        batchDepth;                       // the facade's batch depth at the write (0: not batched)
    };

    // One in-memory host parameter: its value, kHostParams' id / default / steps, and a gesture log.
    class FakePort final : public funkgui::ParamPort
    {
    public:
        FakePort(FakeFacade&, fcdsp::Pid);

        FakePort(const FakePort&) = delete;
        FakePort& operator=(const FakePort&) = delete;

        // funkgui::ParamPort
        float value01() const override;
        float default01() const override;
        int   numSteps() const override;
        void  beginGesture() override;
        void  setValue01(float) override;            // logged; stores toPlain(pid, clamp01(v)) (setValueNotifyingHost)
        void  endGesture() override;
        const char* id() const override;
        void* native() const override;               // nullptr: no juce parameter behind it

        // the probe's side
        fcdsp::Pid pid() const noexcept { return pid_; }
        float plain() const noexcept { return plain_; }   // what currentRaw() reads
        void  script01(float v01) noexcept;          // a host write: toPlain(pid, v01); no gesture, not logged
        void  scriptPlain(float plain) noexcept;     // legal(pid, plain), exactly
        bool  inGesture() const noexcept { return depth_ > 0; }
        int   begins() const noexcept { return begins_; }
        int   sets() const noexcept { return sets_; }
        int   ends() const noexcept { return ends_; }
        int   setsOutsideGesture() const noexcept { return outside_; }
        void  resetCounts() noexcept;

    private:
        FakeFacade& owner_;
        fcdsp::Pid  pid_;
        float       plain_;
        int         depth_ = 0, begins_ = 0, sets_ = 0, ends_ = 0, outside_ = 0;
    };

    // PresetAccess over an in-memory row list (empty by default, like the processor's until P3).
    class FakePresets final : public PresetAccess
    {
    public:
        explicit FakePresets(FakeFacade&);

        int      count() const override;
        Row      row(int index) const override;      // a default Row for an index out of range
        int      current() const override;
        bool     modified() const override;
        uint32_t revision() const override;
        void     apply(int index) override;          // one batch: the row's Mode (if any) is scripted, current = index
        void     step(int delta) override;           // ‹ ›: apply(current ± 1), wrapping; nothing without rows
        bool     saveAs(std::string_view name, std::string_view category) override;   // appends a user row

        // the probe's side
        void setRows(std::vector<Row>);
        void setModified(bool) noexcept;
        int  applies() const noexcept { return applies_; }
        int  steps() const noexcept { return steps_; }
        int  saves() const noexcept { return saves_; }

    private:
        FakeFacade&      owner_;
        std::vector<Row> rows_;
        int              current_ = -1;
        bool             modified_ = false;
        uint32_t         revision_ = 0;
        int              applies_ = 0, steps_ = 0, saves_ = 0;
    };

    class FakeFacade final : public ProcessorFacade
    {
    public:
        FakeFacade();                                            // every port at its host default: Mode slot 0
        explicit FakeFacade(std::string_view modeKey);           // + setMode(modeKey) (an unknown key keeps slot 0)
        ~FakeFacade() override;

        FakeFacade(const FakeFacade&) = delete;
        FakeFacade& operator=(const FakeFacade&) = delete;

        // ProcessorFacade
        funkgui::ParamPort&       port(fcdsp::Pid) override;
        fcdsp::RawParams          currentRaw() const override;
        bool                      readUiFrame(fcdsp::UiFrame&) const override;
        const fcdsp::HistoryRing& history() const override;
        void                      setUiAttached(bool) override;  // counted (never below 0)
        UiState&                  uiState() override;
        StateNotice               stateNotice() const override;
        void                      beginBatch() override;         // counted and nestable
        void                      endBatch() override;
        PresetAccess&             presets() override;

        // scripting (a host, automation or the audio thread; no gestures, not logged)
        bool setMode(std::string_view key);                      // the key's slot; false: unknown key (unchanged)
        void setPlain(fcdsp::Pid, float plain);                  // the port at legal(plain)
        void setConfiguredBudget(std::optional<fcdsp::LookaheadBudget>);   // nullopt: follow the labudget port
        void publish(const fcdsp::UiFrame&);                     // readUiFrame returns it; publishCount is stamped
        void clearFrame() noexcept;                              // readUiFrame returns false again
        void pushColumn(const fcdsp::HistoryColumn&);            // into the real ring
        void setStateNotice(const StateNotice&) noexcept;

        // inspection
        FakePort&    fakePort(fcdsp::Pid);
        FakePresets& fakePresets() noexcept;
        fcdsp::HistoryRing& ring() noexcept;
        int  attachCount() const noexcept { return attach_; }
        int  batchDepth() const noexcept { return batchDepth_; }
        int  batches() const noexcept { return batches_; }       // beginBatch calls so far
        uint32_t publishes() const noexcept { return publishes_; }
        std::span<const FakeWrite> writes() const noexcept;       // every logged port write, in order
        void clearWrites() noexcept;
        void resetCounts() noexcept;                             // ports, writes, batches (not values)

        // A quiet frame for `slot`: meters at the floor (−200 dBFS), no GR, fadeProgress 1, bypassAmt 0, and the
        // smoothed fields equal to `eng` (so the overlay changes nothing). kUiLive is not set.
        static fcdsp::UiFrame quietFrame(uint16_t slot, const fcdsp::EngineParams& eng) noexcept;

    private:
        friend class FakePort;
        void logWrite(fcdsp::Pid, float v01, bool inGesture);

        std::vector<std::unique_ptr<FakePort>> ports_;           // kNumParams, Pid order
        std::unique_ptr<fcdsp::HistoryRing>    ring_;
        FakePresets                            presets_;
        UiState                                ui_{};
        StateNotice                            notice_{};
        std::optional<fcdsp::UiFrame>          frame_;
        std::optional<fcdsp::LookaheadBudget>  budget_;
        std::vector<FakeWrite>                 writes_;
        uint32_t publishes_ = 0;
        int      attach_ = 0;
        int      batchDepth_ = 0;
        int      batches_ = 0;
    };
}
