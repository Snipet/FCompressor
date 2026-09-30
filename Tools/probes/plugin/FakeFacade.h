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
// - diagnostics() (v1.2, ADR-85): fixed values, so a headless frame never depends on the build: versions "0.0.0", no
//   format or host, prepared at 48 kHz with 512-sample blocks, 2 in, 2 out, no key bus, the configured quality and
//   budget of currentRaw() with their latency, load 3.1 % (peak 7.8 %), no overruns, 1000 blocks. setDiagnostics()
//   scripts another (then returned as given).
// - The script calls (script01, setPlain, setMode, …) are a host or automation writing: no gesture, not logged.
#pragma once

#include "plugin/EditHistory.h"
#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/params/ParamPort.h>

#include <array>
#include <cstddef>
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

    // PresetAccess over an in-memory row list, by default the processor's at its defaults (empty until P3; since U6, S12:
    // the compiled factory bank, Init current, unmodified, as a fresh instance of the processor has it). setRows()
    // replaces it.
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

        // ---- U6 additions (S12 lead revision 8): additive, no FZ4 declaration above changed ----------------------------
        // User-preset management as the processor's PresetAccess does it over the store (P3b), in memory:
        // - rename(index, name): user rows only; a name that is empty after trimming, or taken by another row
        //   (ASCII case-insensitive, factory rows included, as PresetStore::rename) is refused.
        // - remove(index): user rows only; removing the current row leaves current() = −1 (the parameters untouched), a
        //   row before it moves it down by one.
        // - importFile(path): appends a user row named after the file's stem (the part after the last '/', without its
        //   extension), made unique the store's way ("Name", "Name 2", …); setImportRow() gives the row instead (its name
        //   made unique the same way). The path is never opened.
        // - exportFile(index, path): any row in range; the file is never written.
        // Each call is counted and its arguments logged, refused or not. script(call, result) forces the calls' result
        // from now on: false refuses (nothing changes), true takes the path above (a refusal it would make still fails,
        // so a forced true never corrupts the list); nullopt restores the behaviour above. Every success bumps
        // revision(). reads() counts the list reads (count, row, current, modified), so a probe can tell a view that
        // re-reads every frame from one that re-reads when revision() moves.
        // P3c (S12.5; S12 lead revision 11) adds Call::overwrite: overwrite(index), below.
        enum class Call : uint8_t { rename, remove, importFile, exportFile, overwrite };
        struct CallLog
        {
            int         index = -1;                      // the row (import: -1)
            std::string text;                            // rename: the new name; import, export: the path
            bool        ok = false;                      // what the call returned
        };

        bool rename(int index, std::string_view newName) override;
        bool remove(int index) override;
        bool importFile(std::string_view path) override;
        bool exportFile(int index, std::string_view path) override;

        void script(Call, std::optional<bool>) noexcept;
        void setImportRow(std::optional<Row>);
        void setCurrent(int index) noexcept;             // a host or state load chose `index` (−1: none); not counted
        const std::vector<CallLog>& calls(Call) const noexcept;
        int  count(Call) const noexcept;                 // calls(call).size()
        int  reads() const noexcept { return reads_; }
        void resetCounts() noexcept;                     // applies, steps, saves, reads and every call log

        // ---- P3c additions (S12.5; S12 lead revision 11): additive, no declaration above changed ------------------
        // overwrite(index): save over a user row as the processor's does (P3c): the row takes the live Mode (its
        // modeKey: the effective slot's key) and keeps its uuid, name and category; it becomes current() and modified()
        // is false. A factory row or an index out of range is refused (nothing changes). Counted and logged as
        // Call::overwrite (text: empty), scripted by script(Call::overwrite, …) like the others; a success bumps
        // revision(). overwrites() is count(Call::overwrite).
        bool overwrite(int index) override;
        int  overwrites() const noexcept { return count(Call::overwrite); }

        // v1.2 (ADR-91): the current row's uuid ("" for none), and making a row current by uuid without moving a
        // parameter (unknown or "": none). Not counted.
        std::string currentUuid() const override;
        void        restoreCurrent(std::string_view uuid) override;

    private:
        FakeFacade&      owner_;
        std::vector<Row> rows_;
        int              current_ = -1;
        bool             modified_ = false;
        uint32_t         revision_ = 0;
        int              applies_ = 0, steps_ = 0, saves_ = 0;

        // ---- U6 additions ---------------------------------------------------------------------------------------------
        bool refuse(Call) const noexcept;                // a scripted false
        bool nameTaken(std::string_view name, int ignoreIndex) const;
        std::string uniqueName(std::string_view wanted) const;

        static constexpr std::size_t kCalls = 5;         // Call's enumerators (P3c: overwrite is the fifth)
        std::array<std::optional<bool>, kCalls>  scripted_{};
        std::array<std::vector<CallLog>, kCalls> calls_{};
        std::optional<Row>                    importRow_;
        int                                   imported_ = 0;   // fresh uuids for imports
        mutable int                           reads_ = 0;
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
        Diagnostics               diagnostics() const override;  // v1.2 (ADR-85): see the header comment
        // v1.2 (ADR-91): the real EditHistory over this facade: the ports' gestures and the batches record, undo writes
        // the ports as a script does (no gesture, not logged), presets are FakePresets'. bumpLoadSerial() is a state load.
        EditAccess&               edits() override;
        EditHistory&              history() noexcept { return history_; }
        void                      bumpLoadSerial() noexcept { ++loadSerial_; }

        // scripting (a host, automation or the audio thread; no gestures, not logged)
        bool setMode(std::string_view key);                      // the key's slot; false: unknown key (unchanged)
        void setPlain(fcdsp::Pid, float plain);                  // the port at legal(plain)
        void setConfiguredBudget(std::optional<fcdsp::LookaheadBudget>);   // nullopt: follow the labudget port
        void publish(const fcdsp::UiFrame&);                     // readUiFrame returns it; publishCount is stamped
        void clearFrame() noexcept;                              // readUiFrame returns false again
        void pushColumn(const fcdsp::HistoryColumn&);            // into the real ring
        void setStateNotice(const StateNotice&) noexcept;
        void setDiagnostics(std::optional<Diagnostics>) noexcept;   // nullopt: the fixed values above

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
        std::optional<Diagnostics>             diagnostics_;
        std::optional<fcdsp::UiFrame>          frame_;
        std::optional<fcdsp::LookaheadBudget>  budget_;
        std::vector<FakeWrite>                 writes_;
        class HistoryHost final : public EditHistory::Host
        {
        public:
            explicit HistoryHost(FakeFacade& f) noexcept : f_(f) {}
            float       raw(fcdsp::Pid p) const override;
            void        write(fcdsp::Pid p, float plain) override;
            void        beginBatch() override;
            void        endBatch() override;
            std::string presetUuid() const override;
            void        restorePreset(const std::string& uuid) override;
            uint32_t    loadSerial() const override { return f_.loadSerial_; }
            bool        onMessageThread() const override { return true; }

        private:
            FakeFacade& f_;
        };
        uint32_t     loadSerial_ = 0;
        HistoryHost  historyHost_{ *this };
        EditHistory  history_{ historyHost_ };
        uint32_t publishes_ = 0;
        int      attach_ = 0;
        int      batchDepth_ = 0;
        int      batches_ = 0;
    };
}
