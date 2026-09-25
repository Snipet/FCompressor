// Tools/probes/plugin/FakeFacade.cpp — see FakeFacade.h. A helper of fcmp_probe_plugin: no FCMP_PROBE line, so it
// registers no test.
#include "FakeFacade.h"

#include "plugin/factory/FactoryBank.h"

#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <funkgui/presets/PresetTypes.h>

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

    // U6: the processor's list at its defaults since P3 (FakeFacade.h): the compiled factory bank in bank order (Init at
    // 0, then Modes.def slot order), Init current, unmodified; so a headless frame shows what the live Standalone shows
    // at its defaults (gui-live parity of the preset strip).
    FakePresets::FakePresets(FakeFacade& owner) : owner_(owner)
    {
        for (const funkgui::presets::Preset& p : fcmp::factory::factoryBank())
        {
            Row r;
            r.uuid = p.uuid.toStdString();
            r.name = p.name.toStdString();
            r.category = p.category.toStdString();
            if (const funkgui::presets::Attribute* a = p.attr(fcmp::factory::kModeIdAttr))
                r.modeKey = a->value.toStdString();
            r.factory = true;
            rows_.push_back(std::move(r));
        }
        current_ = rows_.empty() ? -1 : 0;
    }

    int FakePresets::count() const
    {
        ++reads_;
        return static_cast<int>(rows_.size());
    }

    PresetAccess::Row FakePresets::row(int index) const
    {
        ++reads_;
        if (index < 0 || index >= static_cast<int>(rows_.size()))
            return {};
        return rows_[static_cast<std::size_t>(index)];
    }

    int FakePresets::current() const
    {
        ++reads_;
        return current_;
    }

    bool FakePresets::modified() const
    {
        ++reads_;
        return modified_;
    }
    uint32_t FakePresets::revision() const { return revision_; }

    void FakePresets::apply(int index)
    {
        ++applies_;
        if (index < 0 || index >= static_cast<int>(rows_.size()))
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
        const int n = static_cast<int>(rows_.size());
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
        current_ = static_cast<int>(rows_.size()) - 1;
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

    // ---- FakePresets: U6 additions (user-preset management, S12 lead revision 8) ----------------------------------------

    namespace
    {
        std::string_view trimmed(std::string_view s) noexcept
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

        bool sameNameNoCase(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                if (lower(a[i]) != lower(b[i]))
                    return false;
            }
            return true;
        }

        constexpr std::size_t callIndex(FakePresets::Call c) noexcept { return static_cast<std::size_t>(c); }
    }

    bool FakePresets::refuse(Call c) const noexcept
    {
        const std::optional<bool>& s = scripted_[callIndex(c)];
        return s.has_value() && !*s;
    }

    bool FakePresets::nameTaken(std::string_view name, int ignoreIndex) const
    {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (static_cast<int>(i) != ignoreIndex && sameNameNoCase(trimmed(rows_[i].name), name))
                return true;
        return false;
    }

    // PresetStore::uniqueName: "Name", else "Name 2", "Name 3", …; a taken "Name 2" continues at "Name 3".
    std::string FakePresets::uniqueName(std::string_view wanted) const
    {
        const std::string base(trimmed(wanted).empty() ? std::string_view("Untitled") : trimmed(wanted));
        if (!nameTaken(base, -1))
            return base;
        std::string stem = base;
        int n = 2;
        if (const std::size_t sp = base.rfind(' '); sp != std::string::npos && sp > 0)
        {
            const std::string tail = base.substr(sp + 1);
            if (!tail.empty() && tail.size() <= 6 && tail.find_first_not_of("0123456789") == std::string::npos)
            {
                stem = std::string(trimmed(base.substr(0, sp)));
                n = std::max(2, std::stoi(tail) + 1);
            }
        }
        for (;; ++n)
            if (std::string c = stem + " " + std::to_string(n); !nameTaken(c, -1))
                return c;
    }

    bool FakePresets::rename(int index, std::string_view newName)
    {
        CallLog log{ index, std::string(newName), false };
        const std::string_view n = trimmed(newName);
        const bool userRow = index >= 0 && index < static_cast<int>(rows_.size())
                          && !rows_[static_cast<std::size_t>(index)].factory;
        if (!refuse(Call::rename) && userRow && !n.empty() && !nameTaken(n, index))
        {
            rows_[static_cast<std::size_t>(index)].name = std::string(n);
            ++revision_;
            log.ok = true;
        }
        calls_[callIndex(Call::rename)].push_back(std::move(log));
        return calls_[callIndex(Call::rename)].back().ok;
    }

    bool FakePresets::remove(int index)
    {
        CallLog log{ index, {}, false };
        const bool userRow = index >= 0 && index < static_cast<int>(rows_.size())
                          && !rows_[static_cast<std::size_t>(index)].factory;
        if (!refuse(Call::remove) && userRow)
        {
            rows_.erase(rows_.begin() + index);
            if (current_ == index)
                current_ = -1;                                   // the sound stays, unnamed
            else if (current_ > index)
                --current_;
            ++revision_;
            log.ok = true;
        }
        calls_[callIndex(Call::remove)].push_back(std::move(log));
        return calls_[callIndex(Call::remove)].back().ok;
    }

    bool FakePresets::importFile(std::string_view path)
    {
        CallLog log{ -1, std::string(path), false };
        if (!refuse(Call::importFile) && !path.empty())
        {
            Row r;
            if (importRow_.has_value())
                r = *importRow_;
            else
            {
                std::string_view stem = path.substr(path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1);
                if (const std::size_t dot = stem.rfind('.'); dot != std::string_view::npos && dot > 0)
                    stem = stem.substr(0, dot);
                r.name = std::string(stem);
            }
            r.uuid = "fake-import-" + std::to_string(++imported_);
            r.name = uniqueName(r.name);
            r.factory = false;
            rows_.push_back(std::move(r));
            ++revision_;
            log.ok = true;
        }
        calls_[callIndex(Call::importFile)].push_back(std::move(log));
        return calls_[callIndex(Call::importFile)].back().ok;
    }

    bool FakePresets::exportFile(int index, std::string_view path)
    {
        CallLog log{ index, std::string(path), false };
        log.ok = !refuse(Call::exportFile) && index >= 0 && index < static_cast<int>(rows_.size()) && !path.empty();
        calls_[callIndex(Call::exportFile)].push_back(std::move(log));
        return calls_[callIndex(Call::exportFile)].back().ok;
    }

    void FakePresets::script(Call c, std::optional<bool> result) noexcept { scripted_[callIndex(c)] = result; }

    void FakePresets::setImportRow(std::optional<Row> r) { importRow_ = std::move(r); }

    void FakePresets::setCurrent(int index) noexcept
    {
        current_ = index >= 0 && index < static_cast<int>(rows_.size()) ? index : -1;
        ++revision_;
    }

    const std::vector<FakePresets::CallLog>& FakePresets::calls(Call c) const noexcept { return calls_[callIndex(c)]; }

    int FakePresets::count(Call c) const noexcept { return static_cast<int>(calls_[callIndex(c)].size()); }

    void FakePresets::resetCounts() noexcept
    {
        applies_ = steps_ = saves_ = 0;
        reads_ = 0;
        for (std::vector<CallLog>& v : calls_)
            v.clear();
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
