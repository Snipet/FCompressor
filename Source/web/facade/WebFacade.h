// Source/web/facade/WebFacade.h: the browser demo's ProcessorFacade (web Sprint C, ADR-93). The editor's Panel runs
// over it exactly as it runs over the plugin's Processor; where the Processor has an APVTS and an fcdsp::EngineHost
// beside it, this has 30 host values of its own and an EngineLink (EngineLink.h) to an engine module on another
// thread, to which it posts web/engine/WebProtocol.h's records. Tools/probes/plugin/webnull.cpp proves it against a
// real Processor: the same script through both, the output, the raw values and the telemetry equal bit for bit.
//
// Values. One HostValue per parameter (HostValue.h: JUCE's two-value model, restated so that the raw values are the
// plugin's in every case). port() writes are the editor's: setValueNotifyingHost, with the gestures told to the edit
// history, as the Processor's HistoryPort. currentRaw() is the Processor's snapshot: the 22 Mode-filtered raw values,
// resolveSlot of the `mode` value, and the lookahead budget of the `labudget` value (the engine module reconfigures
// when the record arrives, so there is no SetupWatcher for the configured budget to lag behind).
//
// What is posted, and when (the Processor's batch rule, K2 #23, as records):
//   - outside a batch, every write that changes a raw value posts one Params record, all 30 values, snap 0: the engine
//     ramps to them, as it does when the Processor's next block pulls a changed value;
//   - while a batch is open (beginBatch/endBatch, nestable) nothing is posted: the engine keeps running the values of
//     the last record, as the audio thread keeps the previous BlockParams;
//   - the outermost end posts exactly one Params record, changed or not, whose snap flag says whether any bracket of
//     the nest was ended by endBatch() (a preset, a Mode change: the values apply at once). The edit history's own
//     batches (undo, redo, an A/B switch) end without it, so what they write ramps.
// So the engine never sees half of a multi-parameter write, and an endBatch() always snaps, as in the plugin. One
// order differs in name only: a write outside a batch is the engine's when its record arrives, where the Processor's
// audio thread takes it at its next block start, so a batch opened before that block holds the write back in the
// plugin and not here. In the plugin the block may as well fall before the batch: the web is always one of the
// plugin's own orders.
//
// Telemetry. setUiAttached() is a count; an Attach record goes out when it leaves or reaches 0 (the engine's attach is
// one flag). pull() asks for a Reply and is the frame loop's call, once per drawn frame, before the Panel ticks. A
// reply is checked (magic, version, kind, sizes), then copied out of the link's bytes before anything else is posted:
// its columns go, in order, into a HistoryRing of this facade's own (a stable address: the editor keeps the
// reference), with one marker column first where the reply says the engine's ring lapped (the editor's HistoryStore
// cannot see that gap: this ring never laps for it); its UiFrame is what readUiFrame() returns from then on. Before
// any reply readUiFrame() gives a frame of zeros and true, as the Processor's Seqlock does before the first publish.
// The mirror's written() counts the columns pushed here: it is not the engine's count (UiFrame::historyWritten) once a
// gap or a second attach has happened.
//
// Not in ProcessorFacade: pull(), resync() (the link has just connected, or the engine was replaced: the values with a
// snap, and Attach when something listens), resetEngine() (a new source: the engine's state is cleared), and what
// only the page knows for diagnostics().
//
// One thread (the page's main thread; a probe's own). Portable C++ (lint web.facade): no JUCE, no Emscripten header,
// from the engine only its protocol, and the engine itself only through the link.
#pragma once

#include "web/facade/EngineLink.h"
#include "web/facade/HostValue.h"
#include "web/facade/WebPresets.h"

#include "plugin/ProcessorFacade.h"
#include "plugin/portable/EditHistory.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/params/ParamPort.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace fcmp::web
{
    class WebFacade final : public ProcessorFacade, private ReplySink
    {
    public:
        explicit WebFacade(EngineLink&);                         // takes the link's sink; posts nothing
        ~WebFacade() override;                                   // gives the sink back (nullptr)

        WebFacade(const WebFacade&) = delete;
        WebFacade& operator=(const WebFacade&) = delete;

        // ---- ProcessorFacade ----------------------------------------------------------------------------------------
        funkgui::ParamPort&       port(fcdsp::Pid) override;     // 30 ports in Pid order, owned here
        fcdsp::RawParams          currentRaw() const override;
        bool                      readUiFrame(fcdsp::UiFrame&) const override;
        const fcdsp::HistoryRing& history() const override;      // the mirror
        void                      setUiAttached(bool) override;  // a count (never below 0)
        UiState&                  uiState() override;
        StateNotice               stateNotice() const override;  // none: the demo loads no state
        void                      beginBatch() override;
        void                      endBatch() override;           // the outermost end posts the values, with the snap
        PresetAccess&             presets() override;
        EditAccess&               edits() override;
        Diagnostics               diagnostics() const override;  // what the web knows; the four load figures stay 0

        // ---- the web's own ------------------------------------------------------------------------------------------
        // One Pull, unless the last one is still unanswered (after kPullPatience such calls it is taken for lost and
        // a new one goes out). A link that answers inside post() is asked again at once while its reply says more
        // columns wait, at most kPullBurst times a call: 3584 columns and a marker, fewer than the mirror holds, so
        // an editor that drains the mirror after every pull() never finds it lapped; the rest comes with the next call.
        void pull();
        void resync();                                           // inside a batch: the values go at its end, snapped
        void resetEngine();
        // The page's facts for diagnostics(): the wrapper's name (a static string, "" unknown), the browser's (copied,
        // cut to Diagnostics::host), and the rate and largest block the worklet configured the engine with (0: not
        // known; the rate is then the telemetry's and the block a worklet's quantum, 128).
        void setEnvironment(const char* format, std::string_view host);
        void setEngineSetup(double sampleRate, int maxBlock);

        // ---- for WebPresets and the probes --------------------------------------------------------------------------
        float plain(fcdsp::Pid) const noexcept;                  // the raw value: Processor::rawValue
        WebPresets& webPresets() noexcept { return presets_; }
        int  batchDepth() const noexcept { return depth_; }
        int  attachCount() const noexcept { return attached_; }
        int  latencySamples() const noexcept;                    // the last reply's; before one, what the setup gives
        std::uint32_t replies() const noexcept { return replies_; }       // replies taken
        std::uint32_t repliesRefused() const noexcept { return badReplies_; }   // records the sink did not accept
        std::uint32_t replyFlags() const noexcept { return replyFlags_; }       // the last reply's ReplyFlag bits

        static constexpr int kPullPatience = 30;                 // half a second of frames
        static constexpr int kPullBurst = 7;

    private:
        // The editor's port: the parameter's values through HostValue, the gestures reported to the edit history
        // (Processor::HistoryPort over funkgui::JuceParamPort).
        class Port final : public funkgui::ParamPort
        {
        public:
            void bind(WebFacade& owner, fcdsp::Pid pid) noexcept
            {
                owner_ = &owner;
                pid_ = pid;
            }
            float value01() const override;
            float default01() const override;
            int   numSteps() const override;
            void  beginGesture() override;
            void  setValue01(float) override;
            void  endGesture() override;
            const char* id() const override;
            void* native() const override;                       // nullptr: no juce parameter behind it

        private:
            WebFacade* owner_ = nullptr;
            fcdsp::Pid pid_ = fcdsp::kNoPid;
        };

        // What the edit history reads and writes (Processor::HistoryHost).
        class HistoryHost final : public EditHistory::Host
        {
        public:
            explicit HistoryHost(WebFacade& f) noexcept : f_(f) {}
            float       raw(fcdsp::Pid) const override;
            void        write(fcdsp::Pid, float plain) override;
            void        beginBatch() override;
            void        endBatch() override;                     // no snap: the writes ramp
            std::string presetUuid() const override;
            void        restorePreset(const std::string& uuid) override;
            uint32_t    loadSerial() const override { return 0; }      // no state loads
            bool        onMessageThread() const override { return true; }

        private:
            WebFacade& f_;
        };

        void reply(std::span<const std::uint8_t> record) override;     // ReplySink
        void finishBatch(bool snap);
        void rawChanged();                                       // a write moved a raw value
        void postParams(bool snap);
        void postAttach(bool attached);
        void postPull();

        EngineLink& link_;
        std::array<HostValue, fcdsp::kNumParams> values_{};      // Pid order
        HistoryHost historyHost_{ *this };
        EditHistory history_{ historyHost_ };                    // before the ports and the presets, which use it
        std::array<Port, fcdsp::kNumParams> ports_{};
        WebPresets  presets_{ *this };

        // batch (the Processor's batch_, snapWanted_; one thread, so no epoch)
        int  depth_ = 0;
        bool snapWanted_ = false;                                // a bracket of the open nest asked for the snap

        // telemetry
        int  attached_ = 0;
        std::unique_ptr<fcdsp::HistoryRing> mirror_;             // 128 KiB at a stable address
        fcdsp::UiFrame frame_{};                                 // the last reply's frame; zeros before one
        std::uint32_t  replyFlags_ = 0;
        std::uint32_t  replyLatency_ = 0;
        std::uint32_t  replies_ = 0, badReplies_ = 0;
        std::uint32_t  pullTag_ = 0;                             // the outstanding Pull's tag (its reply echoes it)
        int            pullWaited_ = 0;                          // pull() calls since it was posted
        bool           pullOpen_ = false;

        // per-instance UI state and the page's facts
        UiState     ui_{};
        const char* format_ = "";
        char        host_[sizeof(Diagnostics::host)] {};
        double      sampleRate_ = 0.0;
        int         maxBlock_ = 128;
    };
} // namespace fcmp::web
