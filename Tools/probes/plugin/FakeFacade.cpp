// Tools/probes/plugin/FakeFacade.cpp — see FakeFacade.h. A helper of fcmp_probe_plugin: no FCMP_PROBE line, so it
// registers no test.
#include "FakeFacade.h"

#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace fcmp::probe
{
    namespace
    {
        constexpr float kFloorDb = -200.0f;

        const fcdsp::HostParam& hostParam(fcdsp::Pid p) noexcept { return fcdsp::kHostParams[fcdsp::idx(p)]; }

        float clamp01(float v) noexcept { return std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f); }
    }

    // ---- FakePort -------------------------------------------------------------------------------------------------------

    FakePort::FakePort(FakeFacade& owner, fcdsp::Pid pid)
        : owner_(owner), pid_(pid), plain_(hostParam(pid).def)
    {
    }

    float FakePort::value01() const { return fcdsp::toNorm(pid_, plain_); }
    float FakePort::default01() const { return fcdsp::toNorm(pid_, hostParam(pid_).def); }
    int   FakePort::numSteps() const { return hostParam(pid_).numSteps; }
    const char* FakePort::id() const { return hostParam(pid_).id; }
    void* FakePort::native() const { return nullptr; }

    void FakePort::beginGesture()
    {
        ++depth_;
        ++begins_;
    }

    void FakePort::setValue01(float v)
    {
        const float v01 = clamp01(v);
        plain_ = fcdsp::toPlain(pid_, v01);
        ++sets_;
        if (depth_ == 0)
            ++outside_;
        owner_.logWrite(pid_, v01, depth_ > 0);
    }

    void FakePort::endGesture()
    {
        if (depth_ > 0)
            --depth_;
        ++ends_;
    }

    void FakePort::script01(float v01) noexcept { plain_ = fcdsp::toPlain(pid_, clamp01(v01)); }

    void FakePort::scriptPlain(float plain) noexcept { plain_ = fcdsp::legal(pid_, plain); }

    void FakePort::resetCounts() noexcept { begins_ = sets_ = ends_ = outside_ = 0; }

    // ---- FakePresets ----------------------------------------------------------------------------------------------------

    FakePresets::FakePresets(FakeFacade& owner) : owner_(owner) {}

    int FakePresets::count() const { return static_cast<int>(rows_.size()); }

    PresetAccess::Row FakePresets::row(int index) const
    {
        if (index < 0 || index >= count())
            return {};
        return rows_[static_cast<std::size_t>(index)];
    }

    int FakePresets::current() const { return current_; }
    bool FakePresets::modified() const { return modified_; }
    uint32_t FakePresets::revision() const { return revision_; }

    void FakePresets::apply(int index)
    {
        ++applies_;
        if (index < 0 || index >= count())
            return;
        owner_.beginBatch();                                     // 01 §9.2: an apply is one batch
        const Row& r = rows_[static_cast<std::size_t>(index)];
        if (!r.modeKey.empty())
            owner_.setMode(r.modeKey);
        current_ = index;
        modified_ = false;
        ++revision_;
        owner_.endBatch();
    }

    void FakePresets::step(int delta)
    {
        ++steps_;
        const int n = count();
        if (n == 0 || delta == 0)
            return;
        const int from = current_ < 0 ? (delta > 0 ? -1 : 0) : current_;
        apply(((from + delta) % n + n) % n);
    }

    bool FakePresets::saveAs(std::string_view name, std::string_view category)
    {
        ++saves_;
        if (name.empty())
            return false;
        Row r;
        r.uuid = "fake-" + std::to_string(rows_.size());
        r.name = std::string(name);
        r.category = std::string(category);
        if (const fcdsp::ModeSlot& s = fcdsp::resolveSlot(owner_.currentRaw().modeSlot); s.entry != nullptr)
            r.modeKey = std::string(s.key);
        rows_.push_back(std::move(r));
        current_ = count() - 1;
        modified_ = false;
        ++revision_;
        return true;
    }

    void FakePresets::setRows(std::vector<Row> rows)
    {
        rows_ = std::move(rows);
        current_ = -1;
        modified_ = false;
        ++revision_;
    }

    void FakePresets::setModified(bool m) noexcept
    {
        modified_ = m;
        ++revision_;
    }

    // ---- FakeFacade -----------------------------------------------------------------------------------------------------

    FakeFacade::FakeFacade() : ring_(std::make_unique<fcdsp::HistoryRing>()), presets_(*this)
    {
        ports_.reserve(fcdsp::kNumParams);
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            ports_.push_back(std::make_unique<FakePort>(*this, static_cast<fcdsp::Pid>(i)));
    }

    FakeFacade::FakeFacade(std::string_view modeKey) : FakeFacade() { setMode(modeKey); }

    FakeFacade::~FakeFacade() = default;

    funkgui::ParamPort& FakeFacade::port(fcdsp::Pid p) { return fakePort(p); }

    FakePort& FakeFacade::fakePort(fcdsp::Pid p) { return *ports_[fcdsp::idx(p)]; }

    fcdsp::RawParams FakeFacade::currentRaw() const
    {
        fcdsp::RawParams r;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            r.v[i] = ports_[i]->plain();
        const float slot = ports_[fcdsp::idx(fcdsp::Pid::mode)]->plain();
        r.modeSlot = fcdsp::resolveSlot(static_cast<int>(std::lround(slot))).slot;
        if (budget_)
            r.budget = *budget_;
        else
            r.budget = static_cast<fcdsp::LookaheadBudget>(
                static_cast<int>(std::lround(ports_[fcdsp::idx(fcdsp::Pid::labudget)]->plain())));
        return r;
    }

    bool FakeFacade::readUiFrame(fcdsp::UiFrame& out) const
    {
        if (!frame_)
            return false;
        out = *frame_;
        return true;
    }

    const fcdsp::HistoryRing& FakeFacade::history() const { return *ring_; }
    fcdsp::HistoryRing& FakeFacade::ring() noexcept { return *ring_; }

    void FakeFacade::setUiAttached(bool on)
    {
        attach_ = on ? attach_ + 1 : std::max(0, attach_ - 1);
    }

    UiState& FakeFacade::uiState() { return ui_; }
    StateNotice FakeFacade::stateNotice() const { return notice_; }

    void FakeFacade::beginBatch()
    {
        ++batchDepth_;
        ++batches_;
    }

    void FakeFacade::endBatch()
    {
        if (batchDepth_ > 0)
            --batchDepth_;
    }

    PresetAccess& FakeFacade::presets() { return presets_; }
    FakePresets& FakeFacade::fakePresets() noexcept { return presets_; }

    bool FakeFacade::setMode(std::string_view key)
    {
        const fcdsp::ModeSlot* s = fcdsp::resolveKey(key);
        if (s == nullptr || s->entry == nullptr)
            return false;
        fakePort(fcdsp::Pid::mode).scriptPlain(static_cast<float>(s->slot));
        return true;
    }

    void FakeFacade::setPlain(fcdsp::Pid p, float plain) { fakePort(p).scriptPlain(plain); }

    void FakeFacade::setConfiguredBudget(std::optional<fcdsp::LookaheadBudget> b) { budget_ = b; }

    void FakeFacade::publish(const fcdsp::UiFrame& f)
    {
        frame_ = f;
        frame_->publishCount = ++publishes_;
    }

    void FakeFacade::clearFrame() noexcept { frame_.reset(); }

    void FakeFacade::pushColumn(const fcdsp::HistoryColumn& c) { ring_->push(c); }

    void FakeFacade::setStateNotice(const StateNotice& n) noexcept { notice_ = n; }

    std::span<const FakeWrite> FakeFacade::writes() const noexcept { return writes_; }

    void FakeFacade::clearWrites() noexcept { writes_.clear(); }

    void FakeFacade::resetCounts() noexcept
    {
        for (auto& p : ports_)
            p->resetCounts();
        writes_.clear();
        batches_ = 0;
    }

    void FakeFacade::logWrite(fcdsp::Pid p, float v01, bool inGesture)
    {
        writes_.push_back({ p, v01, inGesture, batchDepth_ });
    }

    fcdsp::UiFrame FakeFacade::quietFrame(uint16_t slot, const fcdsp::EngineParams& e) noexcept
    {
        fcdsp::UiFrame f{};
        f.modeSlot = f.fadeFromSlot = slot;
        f.sampleRate = 48000.0f;
        f.fadeProgress = 1.0f;
        for (float* a : { f.inPeakDb, f.inRmsDb, f.outPeakDb, f.outRmsDb, f.scPeakDb, f.colourInPeakDb, f.curveXDb })
            a[0] = a[1] = kFloorDb;
        f.preGainDb = e.preGainDb;
        f.thrDb = e.thrDb;
        f.slope = e.slope;
        f.kneeDb = e.kneeDb;
        f.rangeDb = e.rangeDb;
        f.atkTauMs = e.atkTauMs;
        f.relTauMs = e.relTauMs;
        f.holdMs = e.holdMs;
        f.lookMs = e.lookMs;
        f.driveDb = e.driveDb;
        f.makeupEffDb = e.makeupDb;
        f.mix = e.mix;
        f.scHpfHz = e.scHpfHz;
        f.sceDbOct = e.sceDbOct;
        f.link = e.link;
        f.s2ThrDb = e.s2ThrDb;
        f.s2AtkTauMs = e.s2AtkTauMs;
        f.s2RelTauMs = e.s2RelTauMs;
        f.tags = e.tags;
        f.discrete = static_cast<uint32_t>(e.det) | static_cast<uint32_t>(e.stmode) << 8
                   | static_cast<uint32_t>(e.voice) << 16 | static_cast<uint32_t>(e.tmode) << 24;
        return f;
    }
}
