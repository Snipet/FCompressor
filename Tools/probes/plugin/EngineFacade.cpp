// Tools/probes/plugin/EngineFacade.cpp — see EngineFacade.h. A helper of fcmp_probe_plugin: no FCMP_PROBE line, so it
// registers no test.
#include "EngineFacade.h"

#include "Measure.h"
#include "Signals.h"

#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace fcmp::probe
{
    namespace
    {
        constexpr float kSilentDb = -200.0f;

        bool on(FakeFacade& f, fcdsp::Pid p) { return f.fakePort(p).plain() >= 0.5f; }
    }

    // ---- Program ----------------------------------------------------------------------------------------------------------

    Program::Program(std::vector<Tone> tones, double fs) : tones_(std::move(tones)), fs_(fs)
    {
        starts_.reserve(tones_.size());
        for (const Tone& t : tones_)
        {
            starts_.push_back(length_);
            length_ += static_cast<uint64_t>(std::llround(std::max(t.seconds, 0.0) * fs_));
        }
    }

    int Program::segmentAt(uint64_t n) const noexcept
    {
        if (n >= length_)
            return -1;
        const auto it = std::upper_bound(starts_.begin(), starts_.end(), n);
        return static_cast<int>(it - starts_.begin()) - 1;
    }

    uint64_t Program::segmentStart(int i) const noexcept
    {
        return i >= 0 && static_cast<std::size_t>(i) < starts_.size() ? starts_[static_cast<std::size_t>(i)] : length_;
    }

    float Program::envelopeDb(uint64_t n) const noexcept
    {
        const int i = segmentAt(n);
        if (i < 0)
            return kSilentDb;
        const Tone& t = tones_[static_cast<std::size_t>(i)];
        if (t.peakDb <= kSilentDb)
            return kSilentDb;
        const double sec = static_cast<double>(n - starts_[static_cast<std::size_t>(i)]) / fs_;
        return static_cast<float>(static_cast<double>(t.peakDb) - static_cast<double>(t.decayDbPerS) * sec);
    }

    float Program::sample(uint64_t n) const noexcept
    {
        const int i = segmentAt(n);
        if (i < 0)
            return 0.0f;
        const float db = envelopeDb(n);
        if (db <= kSilentDb)
            return 0.0f;
        const double hz = static_cast<double>(tones_[static_cast<std::size_t>(i)].hz);
        // The phase comes from the absolute index (Signals.h closed form): continuous across segments.
        const double phase = hz * static_cast<double>(n) / fs_;
        return static_cast<float>(measure::amplitudeFromDb(static_cast<double>(db)) * sig::sinTurns(phase));
    }

    // ---- EngineFacade -------------------------------------------------------------------------------------------------------

    EngineFacade::EngineFacade(std::string_view modeKey, uint64_t tapSamples)
        : params_(modeKey), host_(std::make_unique<fcdsp::EngineHost>())
    {
        const auto cap = static_cast<std::size_t>(tapSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            in_[ch].reserve(cap);
            out_[ch].reserve(cap);
        }
        const fcdsp::simd::f32x4 zero = fcdsp::simd::set1(0.0f);
        gr_.assign(cap, zero);
        det_.assign(cap, zero);
        tgt_.assign(cap, zero);
        tap_.grDb = gr_;
        tap_.detDb = det_;
        tap_.tgtDb = tgt_;
        tap_.firstSample = 0;
        reconfigure();
        host_->setTap(&tap_);
    }

    EngineFacade::~EngineFacade()
    {
        host_->setTap(nullptr);
    }

    funkgui::ParamPort& EngineFacade::port(fcdsp::Pid p) { return params_.port(p); }
    fcdsp::RawParams EngineFacade::currentRaw() const { return params_.currentRaw(); }
    bool EngineFacade::readUiFrame(fcdsp::UiFrame& f) const { return host_->readUiFrame(f); }
    const fcdsp::HistoryRing& EngineFacade::history() const { return host_->history(); }
    void EngineFacade::setUiAttached(bool a) { host_->setUiAttached(a); }
    UiState& EngineFacade::uiState() { return params_.uiState(); }
    StateNotice EngineFacade::stateNotice() const { return params_.stateNotice(); }
    void EngineFacade::beginBatch() { params_.beginBatch(); }
    PresetAccess& EngineFacade::presets() { return params_.presets(); }

    void EngineFacade::endBatch()
    {
        params_.endBatch();
        if (params_.batchDepth() == 0)
            host_->requestSnap();                                 // ProcessorFacade: endBatch() raises the engine snap
    }

    const fcdsp::ModeEntry* EngineFacade::entry() const noexcept
    {
        return fcdsp::resolveSlot(params_.currentRaw().modeSlot).entry;
    }

    // Processor::processBlock's snapshot (01 §2.3): raw → resolveSlot → resolve → BlockParams.
    fcdsp::BlockParams EngineFacade::blockParams()
    {
        const fcdsp::RawParams raw = params_.currentRaw();
        const fcdsp::ModeSlot& slot = fcdsp::resolveSlot(raw.modeSlot);
        fcdsp::BlockParams bp;
        bp.slot = slot.slot;
        if (slot.entry != nullptr)
        {
            fcdsp::Resolution res;
            fcdsp::resolve(*slot.entry, raw, res);
            bp.eng = res.eng;
        }
        bp.bypass = on(params_, fcdsp::Pid::bypass);
        bp.delta = on(params_, fcdsp::Pid::delta);
        bp.listen = on(params_, fcdsp::Pid::listen);
        bp.extKey = on(params_, fcdsp::Pid::extkey);
        return bp;
    }

    void EngineFacade::reconfigure()
    {
        fcdsp::HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = kBlock;
        const auto q = static_cast<int>(std::lround(params_.fakePort(fcdsp::Pid::quality).plain()));
        cfg.quality = static_cast<fcdsp::Quality>(std::clamp(q, 0, 2));
        cfg.budget = params_.currentRaw().budget;
        cfg.mainIns = 2;
        cfg.mainOuts = 2;
        cfg.keyChans = 0;
        last_ = blockParams();
        haveLast_ = true;
        host_->configure(cfg, last_);
    }

    int EngineFacade::latencySamples() const noexcept { return host_->latencySamples(); }

    void EngineFacade::render(const Program& program, int blocks)
    {
        std::array<float, kBlock> l{}, r{}, ol{}, orr{};
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)]
                    = program.sample(processed_ + static_cast<uint64_t>(i));
            if (!(params_.batchDepth() > 0 && haveLast_))         // K2 #23: an open batch reuses the previous block's
                last_ = blockParams();
            haveLast_ = true;
            const std::array<const float*, 2> ins { l.data(), r.data() };
            const std::array<float*, 2> outs { ol.data(), orr.data() };
            fcdsp::ProcessIo io;
            io.in = ins.data();
            io.numIn = 2;
            io.out = outs.data();
            io.numOut = 2;
            io.n = kBlock;
            host_->process(io, last_);
            in_[0].insert(in_[0].end(), l.begin(), l.end());
            in_[1].insert(in_[1].end(), r.begin(), r.end());
            out_[0].insert(out_[0].end(), ol.begin(), ol.end());
            out_[1].insert(out_[1].end(), orr.begin(), orr.end());
            processed_ += static_cast<uint64_t>(kBlock);
        }
    }

    std::span<const float> EngineFacade::input(int ch) const noexcept { return in_[ch == 0 ? 0 : 1]; }
    std::span<const float> EngineFacade::output(int ch) const noexcept { return out_[ch == 0 ? 0 : 1]; }

    namespace
    {
        float laneOf(const std::vector<fcdsp::simd::f32x4>& v, uint64_t n, int lane) noexcept
        {
            if (n >= v.size())
                return 0.0f;
            const fcdsp::simd::f32x4 x = v[static_cast<std::size_t>(n)];
            return lane == 0 ? fcdsp::simd::lane<0>(x) : fcdsp::simd::lane<1>(x);
        }
    }

    float EngineFacade::tapGr(uint64_t n, int lane) const noexcept { return laneOf(gr_, n, lane); }
    float EngineFacade::tapDet(uint64_t n, int lane) const noexcept { return laneOf(det_, n, lane); }
    float EngineFacade::tapTgt(uint64_t n, int lane) const noexcept { return laneOf(tgt_, n, lane); }
}
