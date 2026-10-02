// Source/web/facade/WebFacade.cpp: see WebFacade.h. Source/plugin/Processor.cpp is the model for everything here but
// the records; proc.webnull keeps the two in step.
#include "web/facade/WebFacade.h"

#include "web/engine/WebProtocol.h"

#include "FcmpProduct.h"

#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace fcmp::web
{
    namespace
    {
        using fcdsp::Pid;
        using fcdsp::idx;

        // Index globals from their raw values, as the processor and the engine module read them (Processor.cpp:35-49,
        // WebEngine.cpp:71-85): clamped and rounded, a NaN reading as 0.
        int indexOf(float plain, int hi) noexcept
        {
            if (!(plain > 0.0f))                                 // also NaN
                return 0;
            if (plain >= static_cast<float>(hi))
                return hi;
            return static_cast<int>(plain + 0.5f);
        }

        int qualityOf(float plain) noexcept { return indexOf(plain, 2); }
        fcdsp::LookaheadBudget budgetOf(float plain) noexcept
        {
            return static_cast<fcdsp::LookaheadBudget>(indexOf(plain, 2));
        }
        int modeSlotOf(float plain) noexcept { return indexOf(plain, fcdsp::kModeCapacity - 1); }

        std::array<HostValue, fcdsp::kNumParams> freshValues() noexcept
        {
            std::array<HostValue, fcdsp::kNumParams> v{};
            for (std::size_t i = 0; i < v.size(); ++i)
                v[i] = HostValue::fresh(static_cast<Pid>(i));
            return v;
        }

        // What the editor's HistoryStore puts where its ring lapped (editor/HistoryStore.h gapColumn): the levels at
        // the meters' floor, no gain reduction, b5.
        fcdsp::HistoryColumn gapColumn() noexcept
        {
            fcdsp::HistoryColumn c{};
            c.inPeakDb = c.outPeakDb = c.detMaxDb = -200.0f;
            c.bits = 1u << 5;
            return c;
        }

        constexpr int kQuantum = 128;                            // an AudioWorklet's render quantum

        template <class Msg>
        void post(EngineLink& link, const Msg& m)
        {
            std::uint8_t bytes[sizeof(Msg)];
            std::memcpy(bytes, &m, sizeof(Msg));
            link.post(std::span<const std::uint8_t>(bytes, sizeof(Msg)));
        }
    } // namespace

    // ==== construction ===============================================================================================

    WebFacade::WebFacade(EngineLink& link)
        : link_(link), values_(freshValues()), mirror_(std::make_unique<fcdsp::HistoryRing>())
    {
        for (std::size_t i = 0; i < ports_.size(); ++i)
            ports_[i].bind(*this, static_cast<Pid>(i));
        link_.setSink(this);
    }

    WebFacade::~WebFacade()
    {
        link_.setSink(nullptr);                                  // a late reply finds nobody
    }

    // ==== parameters =================================================================================================

    float WebFacade::Port::value01() const { return owner_->values_[idx(pid_)].value01(pid_); }
    float WebFacade::Port::default01() const { return defaultValue01(pid_); }
    int   WebFacade::Port::numSteps() const { return web::numSteps(pid_); }
    const char* WebFacade::Port::id() const { return fcdsp::kHostParams[idx(pid_)].id; }
    void* WebFacade::Port::native() const { return nullptr; }

    void WebFacade::Port::beginGesture() { owner_->history_.gestureBegan(pid_); }
    void WebFacade::Port::endGesture() { owner_->history_.gestureEnded(pid_); }

    void WebFacade::Port::setValue01(float v)
    {
        if (owner_->values_[idx(pid_)].setValueNotifyingHost(pid_, v))
            owner_->rawChanged();
    }

    funkgui::ParamPort& WebFacade::port(Pid p) { return ports_[idx(p) < fcdsp::kNumParams ? idx(p) : 0]; }

    float WebFacade::plain(Pid p) const noexcept { return idx(p) < fcdsp::kNumParams ? values_[idx(p)].raw : 0.0f; }

    // Processor::snapshot, with the budget the engine module configures from the same record.
    fcdsp::RawParams WebFacade::currentRaw() const
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = values_[i].raw;
        raw.modeSlot = fcdsp::resolveSlot(modeSlotOf(plain(Pid::mode))).slot;
        raw.budget = budgetOf(plain(Pid::labudget));
        return raw;
    }

    // ==== batches and the Params record ==============================================================================

    void WebFacade::beginBatch()
    {
        history_.batchBegan();
        ++depth_;
    }

    void WebFacade::endBatch() { finishBatch(true); }

    // Processor::finishBatch: the snap is the nest's, raised at the outermost end; the history hears the end last.
    void WebFacade::finishBatch(bool snap)
    {
        if (depth_ <= 0)
            return;                                              // endBatch without beginBatch: ignored
        if (snap)
            snapWanted_ = true;
        if (--depth_ == 0)
        {
            const bool wanted = snapWanted_;
            snapWanted_ = false;
            postParams(wanted);
        }
        history_.batchEnded();
    }

    void WebFacade::rawChanged()
    {
        if (depth_ == 0)
            postParams(false);                                   // an edit: the engine ramps to it
    }

    void WebFacade::postParams(bool snap)
    {
        ParamsMsg m{};
        m.h = header(Kind::params, sizeof(ParamsMsg));
        for (std::size_t i = 0; i < kParamCount; ++i)
            m.plain[i] = values_[i].raw;
        m.snap = snap ? 1u : 0u;
        post(link_, m);
    }

    // ==== telemetry ==================================================================================================

    void WebFacade::postAttach(bool attached)
    {
        post(link_, AttachMsg{ header(Kind::attach, sizeof(AttachMsg)), attached ? 1u : 0u });
    }

    void WebFacade::setUiAttached(bool attached)
    {
        if (attached)
        {
            if (attached_++ == 0)
                postAttach(true);
        }
        else if (attached_ > 0 && --attached_ == 0)
            postAttach(false);
    }

    void WebFacade::postPull()
    {
        pullOpen_ = true;
        pullWaited_ = 0;
        ++pullTag_;
        post(link_, PullMsg{ header(Kind::pull, sizeof(PullMsg), pullTag_) });
    }

    void WebFacade::pull()
    {
        if (pullOpen_ && ++pullWaited_ < kPullPatience)
            return;                                              // one outstanding Pull at a time
        for (int k = 0; k < kPullBurst; ++k)
        {
            postPull();
            if (pullOpen_ || (replyFlags_ & kReplyMore) == 0u)
                return;                                          // the reply comes later, or nothing more waits
        }
    }

    // A reply, parsed where it lies: the link's bytes may be at any alignment and are gone when this returns, so every
    // field is copied out (the columns one at a time: a whole Reply is 16 KiB), and nothing is posted from here. A
    // record that fails a check changes nothing. Every valid reply is taken, whichever Pull it answers: its columns are
    // the engine's next ones, and dropping a late reply would lose them with no gap to show for it.
    void WebFacade::reply(std::span<const std::uint8_t> record)
    {
        ++badReplies_;                                           // until it has passed every check
        if (record.size() < kReplyFixedBytes)
            return;
        const std::uint8_t* const bytes = record.data();
        Header h;
        std::memcpy(&h, bytes, sizeof h);
        if (h.magic != kMagic || h.version != kVersion || h.kind != static_cast<std::uint16_t>(Kind::reply))
            return;
        std::uint32_t flags = 0, latency = 0, count = 0;
        std::memcpy(&flags, bytes + offsetof(ReplyHead, flags), sizeof flags);
        std::memcpy(&latency, bytes + offsetof(ReplyHead, latencySamples), sizeof latency);
        std::memcpy(&count, bytes + offsetof(ReplyHead, columnCount), sizeof count);
        if (count > kReplyMaxColumns || h.bytes != replyBytes(count) || h.bytes > record.size())
            return;
        --badReplies_;
        ++replies_;

        if ((flags & kReplyGap) != 0u)
            mirror_->push(gapColumn());                          // columns were lost before the first one here
        for (std::uint32_t i = 0; i < count; ++i)
        {
            fcdsp::HistoryColumn c;
            std::memcpy(&c, bytes + kReplyFixedBytes + sizeof(fcdsp::HistoryColumn) * i, sizeof c);
            mirror_->push(c);
        }
        if ((flags & kReplyFrame) != 0u)
            std::memcpy(&frame_, bytes + offsetof(ReplyHead, frame), sizeof frame_);
        replyFlags_ = flags;
        replyLatency_ = latency;
        if (h.tag == pullTag_)
            pullOpen_ = false;
    }

    bool WebFacade::readUiFrame(fcdsp::UiFrame& frame) const
    {
        frame = frame_;
        return true;
    }

    const fcdsp::HistoryRing& WebFacade::history() const { return *mirror_; }

    // ==== the link's own calls =======================================================================================

    void WebFacade::resync()
    {
        pullOpen_ = false;                                       // a Pull posted to the old engine has no reply coming
        if (depth_ > 0)
            snapWanted_ = true;                                  // the open batch's end posts, with the snap
        else
            postParams(true);
        if (attached_ > 0)
            postAttach(true);
    }

    void WebFacade::resetEngine() { post(link_, ResetMsg{ header(Kind::reset, sizeof(ResetMsg)) }); }

    void WebFacade::setEnvironment(const char* format, std::string_view host)
    {
        format_ = format != nullptr ? format : "";
        const std::size_t n = std::min(host.size(), sizeof host_ - 1);
        std::memcpy(host_, host.data(), n);
        host_[n] = '\0';
    }

    void WebFacade::setEngineSetup(double sampleRate, int maxBlock)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
        maxBlock_ = maxBlock > 0 ? maxBlock : kQuantum;
    }

    // ==== UI state, presets, edits, diagnostics ======================================================================

    UiState& WebFacade::uiState() { return ui_; }
    StateNotice WebFacade::stateNotice() const { return {}; }
    PresetAccess& WebFacade::presets() { return presets_; }
    EditAccess& WebFacade::edits() { return history_; }

    float WebFacade::HistoryHost::raw(Pid p) const { return f_.plain(p); }

    void WebFacade::HistoryHost::write(Pid p, float plain)
    {
        if (idx(p) < fcdsp::kNumParams && f_.values_[idx(p)].writeExact(p, plain))
            f_.rawChanged();
    }

    void WebFacade::HistoryHost::beginBatch() { f_.beginBatch(); }
    void WebFacade::HistoryHost::endBatch() { f_.finishBatch(false); }
    std::string WebFacade::HistoryHost::presetUuid() const { return f_.presets_.currentUuid(); }
    void WebFacade::HistoryHost::restorePreset(const std::string& uuid) { f_.presets_.restoreCurrent(uuid); }

    // The engine's latency once a reply has brought it; before that, what the engine will have at the values held
    // (the latency is the setup's alone: Oversampler.h).
    int WebFacade::latencySamples() const noexcept
    {
        if (replies_ != 0u)
            return static_cast<int>(replyLatency_);
        const double fs = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
        return fcdsp::kOs[qualityOf(plain(Pid::quality))].latency
             + fcdsp::lookaheadSamples(budgetOf(plain(Pid::labudget)), fs);
    }

    // Processor::diagnostics, from what a page can know: the engine's own facts come from the replies; the DSP load
    // is not in the protocol, so the four load figures stay 0 and the settings screen shows a dash.
    Diagnostics WebFacade::diagnostics() const
    {
        Diagnostics d;
        d.version = product::kVersion;
        d.funkgui = product::kFunkGuiVersion;
        d.juce = "";                                             // none in this build
        d.format = format_;
        std::memcpy(d.host, host_, sizeof d.host);
        d.prepared = (replyFlags_ & kReplyConfigured) != 0u;
        if (d.prepared)
        {
            d.sampleRate = sampleRate_ > 0.0 ? sampleRate_ : static_cast<double>(frame_.sampleRate);
            d.maxBlock = maxBlock_;
        }
        d.mainIns = 2;                                           // the engine module's fixed layout: stereo, no key
        d.mainOuts = 2;
        d.keyChans = 0;
        d.quality = qualityOf(plain(Pid::quality));
        d.budget = static_cast<int>(budgetOf(plain(Pid::labudget)));
        d.latencySamples = latencySamples();
        return d;
    }
} // namespace fcmp::web
