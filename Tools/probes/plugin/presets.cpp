// FCMP_PROBE layer=proc name=presets scope=global timeout=180
//
// proc.presets (P3, S12; 03 §3.5; 01 §9.1-9.2; 02 §9.5; C §5.7.7; K2 #10, #19, #23, #25d): the factory bank, the
// processor's PresetAccess over FunkPresets, the store and the file format, against the scratch FCMP_PRESETS_DB (CTest:
// <build>/sandbox/proc.presets/presets.db, emptied by ProbeMain; by hand without it, a fresh temporary file). The
// store is the process-wide factory::SharedPresetStore the processors use, so the probe's own imports reach their
// lists exactly as another editor's would.
//
// Rows (spec, 0/1 or counts; "bitwise" compares float bit patterns):
//   presets.store.lazy                 a processor that saved and loaded state but never called presets() has not
//                                      opened the database (only an editor opens it)
//   presets.sandboxed                  the store's file is $FCMP_PRESETS_DB, not ~/Library/Application Support/...
//   presets.bank.init                  bank[0] is "Init", uuid ...0000, modeId clean, all 22 parameters at the host
//                                      defaults bitwise (a fresh instance's values)
//   presets.bank.uuid.bad / .duplicates  00000000-0000-4000-8000-0000000000NN, NN's high digit the Mode's slot, low
//                                      digit 1..f; unique
//   presets.bank.order.bad             Mode slots non-decreasing after Init (the .inc files in Modes.def order)
//   presets.bank.names.bad             names, categories and notes printable ASCII (the editor font), names non-empty and
//                                      unique ignoring case
//   presets.bank.params.bad            exactly the 22 preset parameters in kApvtsOrder, finite, inside the host range
//   presets.bank.attrs.bad             modeId a registered key, modeRev its ModeDescriptor::revision
//   presets.bank.modes.under3          registered Modes with fewer than 3 presets besides Init
//   presets.bank.values.inert          a value off the host default on a slot its Mode shows locked, derived or n/a
//                                      (it would do nothing; resolved with the 20 MS lookahead budget)
//   presets.bank.values.offstep        a stepped slot whose value is not exactly one of its steps; a live slot
//                                      clamped by its Mode's range
//   presets.bank.indistinct            a preset equal to another of its Mode, or to its Mode's plain defaults
//   presets.fresh.*                    a fresh instance: current() 0 (Init), not modified, count() = the bank; rows
//                                      match the bank; out-of-range rows are empty
//   presets.apply.batch.bad            every factory preset applied through makePresetAccess over a recording facade
//                                      (a running instance): exactly one beginBatch first, one endBatch last, every
//                                      parameter change inside, depth 0 after (K2 #23)
//   presets.apply.mode.mismatches      the effective Mode is the preset's modeId
//   presets.apply.value.max_err        |raw - preset| / host span over the 22 (the host map round trip): <= 1e-6
//   presets.apply.view.mismatches      the resolved view (state and step of all 22) equals the preset's own
//   presets.apply.identity.bad         current() is the applied row, modified() false
//   presets.apply.globals_touched      mode aside, the 7 globals (bypass, quality, ...) keep their values
//   presets.concurrent.*               every factory preset applied 3 times through the processor's own PresetAccess
//                                      while a second thread runs processBlock: no non-finite output, no batch left
//                                      open, the last preset's values in place
//   presets.modified.*                 a one-step nudge of a preset parameter reads modified and restoring it does not;
//                                      a Mode switch reads modified, switching back does not; bypass/quality/listen
//                                      never do; revision() bumps on each flip
//   presets.step.*                     step(+1/-1) moves one row and wraps at both ends; from untitled (-1): +1 -> 0,
//                                      -1 -> the last row
//   presets.absent.*                   a preset without modeId loads clean; an unknown modeId loads clean; a missing
//                                      parameter loads its host default
//   presets.save.*                     saveAs: the new user row is current and unmodified, names the live Mode, the
//                                      store holds the live raw values bitwise and modeId/modeRev; a taken name gets
//                                      " 2"; an empty name is refused
//   presets.store.*                    a second, unshared store on the same file sees the user presets (persistent, WAL)
//                                      and the synced factory rows with their attributes
//   presets.user.apply                 another instance lists the user preset and applies it (Mode and values)
//   presets.file.*                     PresetFile with FCompressor's ProductConfig: the XML shape (root, plugin,
//                                      format, ATTR modeId, 22 PARAMs, no tags or timestamps); write/read round trip
//                                      bitwise; re-import = duplicate; a changed copy imports under a fresh uuid and a
//                                      unique name and is listed; a factory preset's changed copy imports as a user
//                                      preset; a foreign root is refused
//   presets.state.*                    <PRESET> through the processor's state: a factory preset session restores
//                                      current() and unmodified; a user preset session likewise; a modified session
//                                      stays modified; no <PRESET> -> untitled (-1), unmodified, and a Mode switch
//                                      then reads modified; a factory uuid the bank lacks -> untitled
// Golden rows (candidates until the lead blesses them): presets.bank.size, presets.bank.hash (every uuid, name,
// category, attribute and parameter bit of the bank: a moved preset shows as drift).
#include "ProbeRegistry.h"

#include "Signals.h"

#include "plugin/Processor.h"
#include "plugin/State.h"
#include "plugin/factory/FactoryBank.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/presets/PresetFile.h>
#include <funkgui/presets/PresetStore.h>
#include <funkgui/presets/PresetTypes.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    using fcdsp::HostParam;
    using fcdsp::Pid;
    using funkgui::presets::Preset;
    using funkgui::test::Probe;
    namespace fp = funkgui::presets;
    namespace T = funkgui::test;
    namespace sig = fcmp::probe::sig;

    constexpr double kFs = 48000.0;
    constexpr int kBlock = 256;
    constexpr const char* kDbVar = "FCMP_PRESETS_DB";
    constexpr const char* kUuidPrefix = "00000000-0000-4000-8000-0000000000";

    using SharedStore = juce::SharedResourcePointer<fcmp::factory::SharedPresetStore>;

    std::uint32_t bitsOf(float v)
    {
        std::uint32_t b = 0;
        std::memcpy(&b, &v, sizeof b);
        return b;
    }

    const HostParam& hostOf(Pid pid) { return fcdsp::kHostParams[fcdsp::idx(pid)]; }

    // The Mode-filtered Pid of a parameter id, or kNoPid.
    Pid pidOf(const juce::String& id)
    {
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            if (id == fcdsp::kHostParams[i].id)
                return static_cast<Pid>(i);
        return fcdsp::kNoPid;
    }

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    juce::MemoryBlock save(fcmp::Processor& proc)
    {
        juce::MemoryBlock blob;
        proc.getStateInformation(blob);
        return blob;
    }

    void load(fcmp::Processor& proc, const juce::MemoryBlock& blob)
    {
        proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    }

    std::unique_ptr<juce::XmlElement> xmlOf(const juce::MemoryBlock& blob)
    {
        return juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
    }

    juce::MemoryBlock blobOf(const juce::XmlElement& xml)
    {
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary(xml, blob);
        return blob;
    }

    // Stereo noise through processBlock; returns the non-finite output samples.
    std::int64_t run(fcmp::Processor& proc, int blocks, std::uint64_t seed)
    {
        sig::Pcg32 rng(seed);
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        std::int64_t nonfinite = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < kBlock; ++n)
                    buf.setSample(c, n, 0.4f * rng.bipolar());
            proc.processBlock(buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < kBlock; ++n)
                    nonfinite += std::isfinite(buf.getSample(c, n)) ? 0 : 1;
        }
        return nonfinite;
    }

    std::unique_ptr<fcmp::Processor> running()
    {
        auto proc = std::make_unique<fcmp::Processor>();
        proc->prepareToPlay(kFs, kBlock);
        run(*proc, 4, 0x5eedu);
        return proc;
    }

    const fcdsp::ModeSlot* modeOf(const Preset& p)
    {
        const fp::Attribute* a = p.attr(fcmp::factory::kModeIdAttr);
        return a != nullptr ? fcdsp::resolveKey(a->value.toStdString()) : nullptr;
    }

    // A preset's 22 values as RawParams (host defaults for any it lacks) in `slot`, with `budget`.
    fcdsp::RawParams rawOf(const Preset& p, int slot, fcdsp::LookaheadBudget budget)
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = fcdsp::kHostParams[i].def;
        for (const fp::ParamValue& v : p.params)
            if (const Pid pid = pidOf(v.id); pid != fcdsp::kNoPid)
                raw.v[fcdsp::idx(pid)] = v.value;
        raw.modeSlot = static_cast<std::uint8_t>(slot);
        raw.budget = budget;
        return raw;
    }

    bool printableAscii(const juce::String& s)
    {
        for (const juce::juce_wchar ch : s)
            if (ch < 0x20 || ch > 0x7e)
                return false;
        return true;
    }

    // "00000000-0000-4000-8000-0000000000NN" -> NN, or -1.
    int uuidNumber(const juce::String& uuid)
    {
        if (!uuid.startsWith(kUuidPrefix) || uuid.length() != 36)
            return -1;
        const juce::String nn = uuid.substring(34);
        if (!nn.containsOnly("0123456789abcdef"))
            return -1;
        return nn.getHexValue32();
    }

    // Every uuid, name, category, attribute and parameter bit of the bank, in order.
    std::uint64_t bankHash(const std::vector<Preset>& bank)
    {
        std::uint64_t h = T::fnv1a(nullptr, 0);
        const auto text = [&h](const juce::String& s) {
            const std::string u = s.toStdString();
            h = T::fnv1a(u.data(), u.size() + 1, h);             // the terminator separates fields
        };
        for (const Preset& p : bank)
        {
            text(p.uuid);
            text(p.name);
            text(p.category);
            for (const fp::Attribute& a : p.attributes)
            {
                text(a.key);
                text(a.value);
            }
            for (const fp::ParamValue& v : p.params)
            {
                text(v.id);
                const std::uint32_t b = bitsOf(v.value);
                h = T::fnv1a(&b, sizeof b, h);
            }
        }
        return h;
    }

    int indexOfUuid(fcmp::PresetAccess& pa, const juce::String& uuid)
    {
        const std::string u = uuid.toStdString();
        for (int i = 0; i < pa.count(); ++i)
            if (pa.row(i).uuid == u)
                return i;
        return -1;
    }

    // ---- the batch recorder -------------------------------------------------------------------------------------------
    struct Event
    {
        enum Kind : std::uint8_t { begin, end, write } kind;
        int depth;                                               // the processor's batch depth when it happened
    };

    // ProcessorFacade over a real processor that logs beginBatch/endBatch (and still performs them).
    class RecordingFacade final : public fcmp::ProcessorFacade
    {
    public:
        RecordingFacade(fcmp::Processor& proc, std::vector<Event>& log) : proc_(proc), log_(log) {}

        funkgui::ParamPort& port(Pid p) override { return proc_.port(p); }
        fcdsp::RawParams currentRaw() const override { return proc_.currentRaw(); }
        bool readUiFrame(fcdsp::UiFrame& f) const override { return proc_.readUiFrame(f); }
        const fcdsp::HistoryRing& history() const override { return proc_.history(); }
        void setUiAttached(bool a) override { proc_.setUiAttached(a); }
        fcmp::UiState& uiState() override { return proc_.uiState(); }
        fcmp::StateNotice stateNotice() const override { return proc_.stateNotice(); }
        void beginBatch() override
        {
            proc_.beginBatch();
            log_.push_back({ Event::begin, proc_.batchDepth() });
        }
        void endBatch() override
        {
            log_.push_back({ Event::end, proc_.batchDepth() });
            proc_.endBatch();
        }
        fcmp::PresetAccess& presets() override { return proc_.presets(); }

    private:
        fcmp::Processor& proc_;
        std::vector<Event>& log_;
    };

    struct WriteRecorder final : juce::AudioProcessorParameter::Listener
    {
        WriteRecorder(fcmp::Processor& p, std::vector<Event>& l) : proc(p), log(l) {}
        void parameterValueChanged(int, float) override { log.push_back({ Event::write, proc.batchDepth() }); }
        void parameterGestureChanged(int, bool) override {}
        fcmp::Processor& proc;
        std::vector<Event>& log;
    };

    bool batchOk(const std::vector<Event>& log, int depthAfter)
    {
        int begins = 0, ends = 0, outside = 0;
        for (const Event& e : log)
        {
            begins += e.kind == Event::begin ? 1 : 0;
            ends += e.kind == Event::end ? 1 : 0;
            outside += e.kind == Event::write && e.depth < 1 ? 1 : 0;
        }
        return begins == 1 && ends == 1 && outside == 0 && !log.empty() && log.front().kind == Event::begin
            && log.back().kind == Event::end && depthAfter == 0;
    }

    // |raw - preset| / host span over the preset's parameters (absent ones: the host default).
    double valueError(const fcmp::Processor& proc, const Preset& p)
    {
        double err = 0.0;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
        {
            const HostParam& h = fcdsp::kHostParams[i];
            float want = h.def;
            if (const fp::ParamValue* v = p.find(h.id))
                want = v->value;
            const double d = std::abs(static_cast<double>(proc.rawValue(h.pid)) - static_cast<double>(want));
            err = std::max(err, d / static_cast<double>(h.hi - h.lo));
        }
        return err;
    }

    // Resolved state/step differences between the processor's live values and the preset's own, in `slot`.
    std::int64_t viewMismatches(const fcmp::Processor& proc, const Preset& p, int slot)
    {
        const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(slot);
        if (ms.entry == nullptr || ms.entry->desc == nullptr)
            return 1;
        const fcdsp::RawParams live = proc.currentRaw();
        fcdsp::ParamView a, b;
        fcdsp::resolveView(*ms.entry->desc, live, a);
        fcdsp::resolveView(*ms.entry->desc, rawOf(p, slot, live.budget), b);
        std::int64_t bad = 0;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            bad += a.p[i].state != b.p[i].state || a.p[i].step != b.p[i].step ? 1 : 0;
        return bad;
    }
} // namespace

FCMP_PROBE(proc, presets)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // processors (SetupWatcher timers), message thread
    const std::vector<Preset>& bank = fcmp::factory::factoryBank();
    const std::size_t nBank = bank.size();
    const fp::ProductConfig cfg = fcmp::factory::presetConfig();

    // ---- the scratch database (before any store opens) ------------------------------------------------------------------
    juce::File scratchDir;
    const char* dbEnv = std::getenv(kDbVar);
    if (dbEnv == nullptr || *dbEnv == '\0')
    {
        scratchDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("fcmp-proc-presets-" + juce::String(juce::Time::currentTimeMillis()));
        scratchDir.createDirectory();
        T::setEnv(kDbVar, scratchDir.getChildFile("presets.db").getFullPathName().toRawUTF8());
        dbEnv = std::getenv(kDbVar);
    }
    const juce::File dbFile = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(dbEnv));
    const juce::File workDir = dbFile.getParentDirectory();
    const juce::File realDb = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("Application Support/FCompressor/Presets.db");
    {
        const juce::File where = fp::PresetStore::defaultLocation(cfg);
        const bool sandboxed = cfg.isValid() && where == dbFile && where != realDb;
        P.eq("presets.sandboxed", sandboxed ? 1 : 0, 1);
        std::printf("NOTE     presets.db %s\n", where.getFullPathName().toRawUTF8());
        if (!sandboxed)
        {
            P.harnessError("proc.presets: the preset database is not the scratch file; refusing to touch the real one");
            return P.finish();
        }
    }

    // ---- the database opens only when an editor asks ---------------------------------------------------------------------
    {
        auto a = running();
        const juce::MemoryBlock blob = save(*a);
        auto b = running();
        load(*b, blob);
        P.eq("presets.store.lazy", SharedStore::getSharedObjectWithoutCreating().has_value() ? 0 : 1, 1);
    }
    SharedStore store;                                           // from here on, the same store the processors use

    // ---- the bank -------------------------------------------------------------------------------------------------------
    {
        bool init = nBank > 0;
        if (init)
        {
            const Preset& p = bank.front();
            const fp::Attribute* m = p.attr(fcmp::factory::kModeIdAttr);
            init = p.name == "Init" && p.uuid == juce::String(kUuidPrefix) + "00" && p.isFactory && m != nullptr
                && m->value == "clean" && p.params.size() == fcdsp::kNumModeParams;
            for (const fp::ParamValue& v : p.params)
                if (const Pid pid = pidOf(v.id); pid == fcdsp::kNoPid || bitsOf(v.value) != bitsOf(hostOf(pid).def))
                    init = false;
        }
        P.eq("presets.bank.init", init ? 1 : 0, 1);

        std::int64_t uuidBad = 0, dups = 0, orderBad = 0, namesBad = 0, paramsBad = 0, attrsBad = 0;
        std::int64_t inert = 0, offstep = 0, indistinct = 0;
        std::set<juce::String> uuids, names;
        std::map<int, int> perSlot;
        std::map<int, std::vector<const Preset*>> bySlot;
        int lastSlot = -1;
        for (std::size_t k = 0; k < nBank; ++k)
        {
            const Preset& p = bank[k];
            const fcdsp::ModeSlot* ms = modeOf(p);
            const int slot = ms != nullptr ? ms->slot : -1;
            const int nn = uuidNumber(p.uuid);
            if (k == 0)
                uuidBad += nn == 0 ? 0 : 1;
            else if (nn < 0 || (nn & 0xf) == 0 || (slot < 16 && (nn >> 4) != slot))
                ++uuidBad;
            dups += uuids.insert(p.uuid).second ? 0 : 1;
            dups += names.insert(p.name.toLowerCase()).second ? 0 : 1;
            if (k > 0)
            {
                orderBad += slot < lastSlot ? 1 : 0;
                lastSlot = std::max(lastSlot, slot);
                ++perSlot[slot];
                bySlot[slot].push_back(&p);
            }
            namesBad += p.name.trim().isEmpty() || p.category.trim().isEmpty() || !printableAscii(p.name)
                                || !printableAscii(p.category) || !printableAscii(p.notes) || !p.isFactory
                            ? 1 : 0;

            // the 22, in kApvtsOrder, finite and in the host range
            std::size_t at = 0;
            bool shape = p.params.size() == fcdsp::kNumModeParams;
            for (const Pid pid : fcdsp::kApvtsOrder)
            {
                if (fcdsp::idx(pid) >= fcdsp::kNumModeParams || !shape)
                    continue;
                const HostParam& h = hostOf(pid);
                const fp::ParamValue& v = p.params[at++];
                shape = v.id == h.id && std::isfinite(v.value) && v.value >= h.lo && v.value <= h.hi;
            }
            paramsBad += shape ? 0 : 1;

            const fp::Attribute* rev = p.attr(fcmp::factory::kModeRevAttr);
            const bool attrs = ms != nullptr && ms->entry != nullptr && ms->entry->desc != nullptr && rev != nullptr
                            && rev->value == juce::String(static_cast<int>(ms->entry->desc->revision))
                            && p.attr(fcmp::factory::kModeIdAttr)->value.toStdString() == ms->key;
            attrsBad += attrs ? 0 : 1;
            if (!attrs || !shape)
                continue;

            // against the Mode (the widest lookahead budget, so `look` resolves as the Mode shows it)
            const fcdsp::RawParams raw = rawOf(p, slot, fcdsp::LookaheadBudget::ms20);
            fcdsp::ParamView view;
            fcdsp::resolveView(*ms->entry->desc, raw, view);
            for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            {
                const fcdsp::ResolvedParam& r = view.p[i];
                const bool live = r.state == fcdsp::SlotState::live || r.state == fcdsp::SlotState::stepped;
                if (!live && bitsOf(raw.v[i]) != bitsOf(fcdsp::kHostParams[i].def))
                {
                    std::printf("NOTE     bank %s: %s = %g on a slot the Mode does not use\n", p.name.toRawUTF8(),
                                fcdsp::kHostParams[i].id, static_cast<double>(raw.v[i]));
                    ++inert;
                }
                const bool bad = (r.state == fcdsp::SlotState::stepped && bitsOf(r.plain) != bitsOf(raw.v[i]))
                              || (r.state == fcdsp::SlotState::live && (r.flags & fcdsp::kClamped) != 0);
                if (bad)
                    std::printf("NOTE     bank %s: %s = %.9g is off its step or range (%.9g)\n", p.name.toRawUTF8(),
                                fcdsp::kHostParams[i].id, static_cast<double>(raw.v[i]), static_cast<double>(r.plain));
                offstep += bad ? 1 : 0;
            }
        }
        std::int64_t under3 = 0;
        for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
            under3 += perSlot[m.slot] >= 3 ? 0 : 1;
        for (const auto& [slot, list] : bySlot)
        {
            const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(slot);
            if (ms.entry == nullptr || ms.entry->desc == nullptr)
                continue;
            fcdsp::RawParams defaults;
            for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
                defaults.v[i] = fcdsp::kHostParams[i].def;
            defaults.modeSlot = static_cast<std::uint8_t>(slot);
            fcdsp::modeDefaults(*ms.entry->desc, defaults);
            for (std::size_t x = 0; x < list.size(); ++x)
            {
                const fcdsp::RawParams rx = rawOf(*list[x], slot, fcdsp::LookaheadBudget::ms20);
                bool same = true;
                for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
                    same = same && bitsOf(rx.v[i]) == bitsOf(defaults.v[i]);
                for (std::size_t y = x + 1; y < list.size(); ++y)
                {
                    const fcdsp::RawParams ry = rawOf(*list[y], slot, fcdsp::LookaheadBudget::ms20);
                    bool eq = true;
                    for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
                        eq = eq && bitsOf(rx.v[i]) == bitsOf(ry.v[i]);
                    indistinct += eq ? 1 : 0;
                }
                indistinct += same ? 1 : 0;
            }
        }
        P.eq("presets.bank.uuid.bad", uuidBad, 0);
        P.eq("presets.bank.uuid.duplicates", dups, 0);
        P.eq("presets.bank.order.bad", orderBad, 0);
        P.eq("presets.bank.names.bad", namesBad, 0);
        P.eq("presets.bank.params.bad", paramsBad, 0);
        P.eq("presets.bank.attrs.bad", attrsBad, 0);
        P.eq("presets.bank.modes.under3", under3, 0);
        P.eq("presets.bank.values.inert", inert, 0);
        P.eq("presets.bank.values.offstep", offstep, 0);
        P.eq("presets.bank.indistinct", indistinct, 0);
        P.num("presets.bank.size", static_cast<double>(nBank), T::Tol::exact());
        P.hash("presets.bank.hash", bankHash(bank));
        std::printf("NOTE     presets.bank: %zu presets, revision 0x%08x\n", nBank,
                    static_cast<unsigned>(fcmp::factory::factoryBankRevision()));
    }

    // ---- a fresh instance ------------------------------------------------------------------------------------------------
    auto a = running();
    {
        fcmp::PresetAccess& pa = a->presets();
        P.eq("presets.fresh.current", pa.current(), 0);
        P.eq("presets.fresh.modified", pa.modified() ? 1 : 0, 0);
        P.eq("presets.fresh.count", pa.count(), static_cast<std::int64_t>(nBank));
        std::int64_t rowsBad = 0;
        for (std::size_t k = 0; k < nBank; ++k)
        {
            const fcmp::PresetAccess::Row r = pa.row(static_cast<int>(k));
            const fcdsp::ModeSlot* ms = modeOf(bank[k]);
            rowsBad += r.uuid == bank[k].uuid.toStdString() && r.name == bank[k].name.toStdString()
                               && r.category == bank[k].category.toStdString() && r.factory && ms != nullptr
                               && r.modeKey == ms->key
                           ? 0 : 1;
        }
        P.eq("presets.fresh.rows.bad", rowsBad, 0);
        P.eq("presets.fresh.rows.out_of_range", pa.row(-1).uuid.empty() && pa.row(pa.count()).uuid.empty() ? 1 : 0, 1);
        P.eq("presets.store.factory_synced", store->count(fp::Source::factory), static_cast<std::int64_t>(nBank));
    }

    // ---- every factory preset through a recording facade -------------------------------------------------------------------
    {
        auto b = running();
        const std::array<std::pair<Pid, float>, 6> globals{ { { Pid::extkey, 0.7f }, { Pid::listen, 1.0f },
                                                              { Pid::delta, 1.0f }, { Pid::bypass, 0.6f },
                                                              { Pid::quality, 2.0f }, { Pid::labudget, 1.0f } } };
        for (const auto& [pid, v] : globals)
            setPlain(*b, pid, v);
        std::array<float, 6> before{};
        for (std::size_t g = 0; g < globals.size(); ++g)
            before[g] = b->rawValue(globals[g].first);

        std::vector<Event> log;
        RecordingFacade facade(*b, log);
        fcmp::StateHooks hooks;
        const std::unique_ptr<fcmp::PresetAccess> access =
            fcmp::makePresetAccess(fcmp::PresetContext{ b->apvts(), facade, hooks });
        WriteRecorder writes(*b, log);
        for (juce::AudioProcessorParameter* p : b->getParameters())
            p->addListener(&writes);

        std::int64_t batchBad = 0, modeBad = 0, viewBad = 0, identityBad = 0, globalsBad = 0;
        double valueErr = 0.0;
        for (std::size_t n = 0; n < nBank; ++n)
        {
            const std::size_t k = nBank - 1 - n;                 // from the last: every step changes the Mode
            log.clear();
            access->apply(static_cast<int>(k));
            batchBad += batchOk(log, b->batchDepth()) ? 0 : 1;
            const fcdsp::ModeSlot* ms = modeOf(bank[k]);
            const int slot = ms != nullptr ? ms->slot : -1;
            modeBad += b->currentRaw().modeSlot == slot ? 0 : 1;
            valueErr = std::max(valueErr, valueError(*b, bank[k]));
            viewBad += viewMismatches(*b, bank[k], slot);
            identityBad += access->current() == static_cast<int>(k) && !access->modified() ? 0 : 1;
            for (std::size_t g = 0; g < globals.size(); ++g)
                globalsBad += bitsOf(b->rawValue(globals[g].first)) == bitsOf(before[g]) ? 0 : 1;
            run(*b, 1, 0x100u + k);
        }
        for (juce::AudioProcessorParameter* p : b->getParameters())
            p->removeListener(&writes);
        P.eq("presets.apply.batch.bad", batchBad, 0);
        P.eq("presets.apply.mode.mismatches", modeBad, 0);
        P.le("presets.apply.value.max_err", valueErr, 1e-6);
        P.eq("presets.apply.view.mismatches", viewBad, 0);
        P.eq("presets.apply.identity.bad", identityBad, 0);
        P.eq("presets.apply.globals_touched", globalsBad, 0);
    }

    // ---- applies while the audio thread runs ------------------------------------------------------------------------------
    {
        auto g = running();
        fcmp::PresetAccess& pa = g->presets();
        std::atomic<bool> stop{ false };
        std::atomic<std::int64_t> blocks{ 0 }, nonfinite{ 0 };
        std::thread audio([&] {
            sig::Pcg32 rng(0x99u);
            juce::AudioBuffer<float> buf(2, kBlock);
            juce::MidiBuffer midi;
            while (!stop.load(std::memory_order_acquire))
            {
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < kBlock; ++n)
                        buf.setSample(c, n, 0.4f * rng.bipolar());
                g->processBlock(buf, midi);
                std::int64_t bad = 0;
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < kBlock; ++n)
                        bad += std::isfinite(buf.getSample(c, n)) ? 0 : 1;
                nonfinite.fetch_add(bad, std::memory_order_relaxed);
                blocks.fetch_add(1, std::memory_order_release);
            }
        });
        const int applies = 3 * static_cast<int>(nBank);
        for (int i = 0; i < applies; ++i)
        {
            pa.apply((i * 7 + 3) % static_cast<int>(nBank));
            const std::int64_t b0 = blocks.load(std::memory_order_acquire);
            while (blocks.load(std::memory_order_acquire) < b0 + 1)
                std::this_thread::yield();
        }
        pa.apply(static_cast<int>(nBank) - 1);
        stop.store(true, std::memory_order_release);
        audio.join();
        P.eq("presets.concurrent.nonfinite", nonfinite.load(), 0);
        P.eq("presets.concurrent.batch_open", g->batchDepth(), 0);
        P.le("presets.concurrent.value.max_err", valueError(*g, bank.back()), 1e-6);
        std::printf("NOTE     presets.concurrent: %d applies, %lld blocks processed meanwhile\n", applies + 1,
                    static_cast<long long>(blocks.load()));
    }

    // ---- modified(), revision() -----------------------------------------------------------------------------------------
    fcmp::PresetAccess& pa = a->presets();
    int fetRow = -1;
    for (std::size_t k = 0; k < nBank && fetRow < 0; ++k)
        if (const fcdsp::ModeSlot* ms = modeOf(bank[k]); ms != nullptr && ms->key == "fet-76")
            fetRow = static_cast<int>(k);
    if (fetRow < 0)
    {
        P.harnessError("proc.presets: the bank has no fet-76 preset");
        return P.finish();
    }
    {
        pa.apply(fetRow);
        std::int64_t bad = pa.modified() ? 1 : 0;
        std::uint32_t rev = pa.revision();
        std::int64_t revBad = 0;
        const auto flip = [&](bool want) {
            bad += pa.modified() == want ? 0 : 1;
            const std::uint32_t now = pa.revision();
            revBad += now != rev ? 0 : 1;
            rev = now;
        };
        juce::RangedAudioParameter& thr = a->parameter(Pid::thr);
        const float norm = thr.getValue();
        thr.setValueNotifyingHost(std::min(1.0f, norm + 0.01f));  // a nudge
        flip(true);
        thr.setValueNotifyingHost(norm);                          // back to the baseline
        flip(false);
        P.eq("presets.modified.param", bad, 0);

        bad = 0;
        const float slot = a->rawValue(Pid::mode);
        setPlain(*a, Pid::mode, 0.0f);                            // Clean: a plain switch writes only `mode` (K2 #4)
        flip(true);
        setPlain(*a, Pid::mode, slot);
        flip(false);
        P.eq("presets.modified.mode", bad, 0);

        bad = 0;
        setPlain(*a, Pid::bypass, 1.0f);
        setPlain(*a, Pid::quality, 2.0f);
        setPlain(*a, Pid::listen, 1.0f);
        bad += pa.modified() ? 1 : 0;
        setPlain(*a, Pid::bypass, 0.0f);
        setPlain(*a, Pid::quality, 1.0f);
        setPlain(*a, Pid::listen, 0.0f);
        bad += pa.modified() ? 1 : 0;
        P.eq("presets.modified.globals", bad, 0);
        P.eq("presets.revision.follows_modified", revBad, 0);
    }

    // ---- step -------------------------------------------------------------------------------------------------------------
    {
        const int n = pa.count();
        std::int64_t bad = 0;
        pa.apply(1);
        pa.step(+1);
        bad += pa.current() == 2 ? 0 : 1;
        pa.step(-1);
        bad += pa.current() == 1 ? 0 : 1;
        pa.apply(n - 1);
        pa.step(+1);
        bad += pa.current() == 0 ? 0 : 1;
        pa.step(-1);
        bad += pa.current() == n - 1 ? 0 : 1;
        P.eq("presets.step.bad", bad, 0);
    }

    // ---- presets the bank does not have: through the store, listed and applied --------------------------------------------
    std::int64_t imported = 0;
    {
        auto importListed = [&](Preset p) -> int {
            const fp::PresetStore::ImportResult r = store->importPreset(std::move(p));
            if (!r.ok)
                return -1;
            ++imported;
            pa.revision();
            return indexOfUuid(pa, r.uuid);
        };
        // somewhere else first: FET 76 at odd values
        pa.apply(fetRow);
        setPlain(*a, Pid::thr, -31.0f);
        setPlain(*a, Pid::ratio, 0.95f);

        Preset noMode;
        noMode.uuid = juce::Uuid().toDashedString();
        noMode.name = "Probe No Mode";
        noMode.params = { { "thr", -30.0f }, { "ratio", 0.5f } };
        const int r1 = importListed(noMode);
        std::int64_t bad = r1 < 0 ? 1 : 0;
        if (r1 >= 0)
        {
            pa.apply(r1);
            const fcdsp::ModeSlot* clean = fcdsp::resolveKey("clean");
            bad += clean != nullptr && a->currentRaw().modeSlot == clean->slot ? 0 : 1;
            bad += valueError(*a, noMode) <= 1e-6 ? 0 : 1;        // absent -> the host default
            bad += pa.current() == r1 && !pa.modified() ? 0 : 1;
        }
        P.eq("presets.absent.no_modeid", bad, 0);

        Preset unknown = noMode;
        unknown.uuid = juce::Uuid().toDashedString();
        unknown.name = "Probe Unknown Mode";
        unknown.setAttr(fcmp::factory::kModeIdAttr, "no-such-mode");
        pa.apply(fetRow);
        const int r2 = importListed(unknown);
        bad = r2 < 0 ? 1 : 0;
        if (r2 >= 0)
        {
            pa.apply(r2);
            const fcdsp::ModeSlot* clean = fcdsp::resolveKey("clean");
            bad += clean != nullptr && a->currentRaw().modeSlot == clean->slot ? 0 : 1;
            bad += pa.row(r2).modeKey == "clean" ? 0 : 1;
            bad += !pa.modified() ? 0 : 1;
        }
        P.eq("presets.absent.unknown_modeid", bad, 0);

        Preset onlyMode;
        onlyMode.uuid = juce::Uuid().toDashedString();
        onlyMode.name = "Probe Only Mode";
        onlyMode.setAttr(fcmp::factory::kModeIdAttr, "opto-2a");
        const int r3 = importListed(onlyMode);
        bad = r3 < 0 ? 1 : 0;
        if (r3 >= 0)
        {
            pa.apply(r3);
            const fcdsp::ModeSlot* opto = fcdsp::resolveKey("opto-2a");
            bad += opto != nullptr && a->currentRaw().modeSlot == opto->slot ? 0 : 1;
            bad += valueError(*a, onlyMode) <= 1e-6 ? 0 : 1;
        }
        P.eq("presets.absent.params_default", bad, 0);
    }

    // ---- save as ----------------------------------------------------------------------------------------------------------
    juce::String savedUuid;
    {
        pa.apply(fetRow);
        setPlain(*a, Pid::thr, -29.5f);
        setPlain(*a, Pid::rel, 333.0f);
        const int before = pa.count();
        const bool ok = pa.saveAs("Probe Vocal", "Vocal");
        const int cur = pa.current();
        const fcmp::PresetAccess::Row r = pa.row(cur);
        std::int64_t bad = ok && pa.count() == before + 1 && cur >= static_cast<int>(nBank) ? 0 : 1;
        bad += r.name == "Probe Vocal" && r.category == "Vocal" && !r.factory && r.modeKey == "fet-76" ? 0 : 1;
        bad += pa.modified() ? 1 : 0;
        P.eq("presets.save.current", bad, 0);
        savedUuid = juce::String(r.uuid);

        bad = 0;
        const std::optional<Preset> stored = store->get(savedUuid);
        if (!stored.has_value())
            bad = 1;
        else
        {
            const fp::Attribute* id = stored->attr(fcmp::factory::kModeIdAttr);
            const fp::Attribute* rev = stored->attr(fcmp::factory::kModeRevAttr);
            const fcdsp::ModeEntry* fet = fcdsp::byKey("fet-76");
            bad += id != nullptr && id->value == "fet-76" && rev != nullptr && fet != nullptr
                           && rev->value == juce::String(static_cast<int>(fet->desc->revision))
                       ? 0 : 1;
            bad += stored->params.size() == fcdsp::kNumModeParams ? 0 : 1;
            for (const fp::ParamValue& v : stored->params)
                if (const Pid pid = pidOf(v.id); pid == fcdsp::kNoPid || bitsOf(v.value) != bitsOf(a->rawValue(pid)))
                    ++bad;
            bad += stored->isFactory ? 1 : 0;
        }
        P.eq("presets.save.stored", bad, 0);

        const bool again = pa.saveAs("Probe Vocal", "Vocal");
        P.eq("presets.save.unique_name", again && pa.row(pa.current()).name == "Probe Vocal 2" ? 1 : 0, 1);
        const int n = pa.count();
        const bool empty = pa.saveAs("   ", "Vocal");
        P.eq("presets.save.empty_refused", !empty && pa.count() == n ? 1 : 0, 1);
    }

    // ---- the file on disk, seen by another connection ---------------------------------------------------------------------
    {
        fp::PresetStore other(cfg);                              // unshared: a second connection to the same file
        std::int64_t bad = other.isPersistent() && other.file() == dbFile ? 0 : 1;
        fp::Query q;
        q.source = fp::Source::user;
        const std::vector<Preset> users = other.query(q);
        bad += static_cast<std::int64_t>(users.size()) == imported + 2 ? 0 : 1;   // the imports, Probe Vocal (2)
        const std::optional<Preset> p = other.get(savedUuid);
        bad += p.has_value() && p->attr(fcmp::factory::kModeIdAttr) != nullptr ? 0 : 1;
        P.eq("presets.store.reopen", bad, 0);

        bad = other.count(fp::Source::factory) == static_cast<int>(nBank) ? 0 : 1;
        for (const Preset& f : bank)
        {
            const std::optional<Preset> s = other.get(f.uuid);
            bad += s.has_value() && s->isFactory && s->attr(fcmp::factory::kModeIdAttr) != nullptr
                           && s->attr(fcmp::factory::kModeIdAttr)->value
                                  == f.attr(fcmp::factory::kModeIdAttr)->value
                       ? 0 : 1;
        }
        P.eq("presets.store.factory_rows", bad, 0);
    }

    // ---- another instance lists and applies the user preset ----------------------------------------------------------------
    {
        auto c = running();
        fcmp::PresetAccess& pc = c->presets();
        const int at = indexOfUuid(pc, savedUuid);
        std::int64_t bad = pc.count() == pa.count() && at >= 0 ? 0 : 1;
        const std::optional<Preset> stored = store->get(savedUuid);
        if (at >= 0 && stored.has_value())
        {
            pc.apply(at);
            const fcdsp::ModeEntry* fet = fcdsp::byKey("fet-76");
            bad += fet != nullptr && c->currentRaw().modeSlot == fcdsp::slotOf(*fet) ? 0 : 1;
            bad += valueError(*c, *stored) <= 1e-6 ? 0 : 1;
            bad += pc.current() == at && !pc.modified() ? 0 : 1;
        }
        P.eq("presets.user.apply", bad, 0);
    }

    // ---- PresetFile: export, import -------------------------------------------------------------------------------------
    {
        const std::optional<Preset> saved = store->get(savedUuid);
        std::int64_t bad = saved.has_value() ? 0 : 1;
        if (saved.has_value())
        {
            const juce::String text = fp::PresetFile::toXmlString(cfg, *saved);
            const std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(text);
            bad += xml != nullptr && xml->hasTagName("FCompressorPreset") ? 0 : 1;
            if (xml != nullptr)
            {
                bad += xml->getStringAttribute("plugin") == "FCompressor" && xml->getStringAttribute("format") == "1"
                               && xml->getStringAttribute("uuid") == saved->uuid
                           ? 0 : 1;
                int params = 0, modeIds = 0;
                for (const juce::XmlElement* e : xml->getChildIterator())
                {
                    params += e->hasTagName("PARAM") ? 1 : 0;
                    modeIds += e->hasTagName("ATTR") && e->getStringAttribute("key") == "modeId"
                                       && e->getStringAttribute("value") == "fet-76"
                                   ? 1 : 0;
                }
                bad += params == static_cast<int>(fcdsp::kNumModeParams) && modeIds == 1 ? 0 : 1;
                for (const char* personal : { "tags", "createdMs", "modifiedMs", "lastUsedMs" })
                    bad += xml->hasAttribute(personal) ? 1 : 0;
            }
        }
        P.eq("presets.file.xml", bad, 0);

        bad = saved.has_value() ? 0 : 1;
        if (saved.has_value())
        {
            const juce::File f = workDir.getChildFile(fp::PresetFile::safeFileName(saved->name) + cfg.fileExtension);
            juce::String err;
            const bool wrote = fp::PresetFile::write(cfg, *saved, f, &err);
            const std::optional<Preset> back = fp::PresetFile::read(cfg, f, &err);
            bad += wrote && f.getFileExtension() == ".fcmppreset" && back.has_value() ? 0 : 1;
            if (back.has_value())
            {
                bad += back->uuid == saved->uuid && back->name == saved->name && back->category == saved->category
                               && back->author == saved->author && back->notes == saved->notes
                               && back->params.size() == saved->params.size()
                               && back->attributes.size() == saved->attributes.size() && back->tags == 0
                               && back->createdMs == 0
                           ? 0 : 1;
                for (std::size_t i = 0; i < std::min(back->params.size(), saved->params.size()); ++i)
                    bad += back->params[i].id == saved->params[i].id
                                   && bitsOf(back->params[i].value) == bitsOf(saved->params[i].value)
                               ? 0 : 1;
                for (std::size_t i = 0; i < std::min(back->attributes.size(), saved->attributes.size()); ++i)
                    bad += back->attributes[i].key == saved->attributes[i].key
                                   && back->attributes[i].value == saved->attributes[i].value
                               ? 0 : 1;

                const fp::PresetStore::ImportResult dup = store->importPreset(*back);
                P.eq("presets.file.import.duplicate", dup.ok && dup.duplicate ? 1 : 0, 1);

                Preset changed = *back;
                changed.params.front().value = -12.0f;
                const int n = pa.count();
                const fp::PresetStore::ImportResult fresh = store->importPreset(changed);
                pa.revision();
                const int listed = indexOfUuid(pa, fresh.uuid);
                P.eq("presets.file.import.fresh", fresh.ok && !fresh.duplicate && fresh.uuid != saved->uuid
                                                          && fresh.renamed && pa.count() == n + 1 && listed >= 0
                                                          && pa.row(listed).modeKey == "fet-76"
                                                      ? 1 : 0,
                     1);
            }
        }
        P.eq("presets.file.roundtrip", bad, 0);

        // a factory preset's file: identical -> nothing; changed -> a user copy under a fresh uuid
        {
            const Preset& f = bank[static_cast<std::size_t>(fetRow)];
            const std::optional<Preset> same =
                fp::PresetFile::fromXmlString(cfg, fp::PresetFile::toXmlString(cfg, f));
            std::int64_t fbad = same.has_value() ? 0 : 1;
            if (same.has_value())
            {
                const fp::PresetStore::ImportResult r = store->importPreset(*same);
                fbad += r.ok && r.duplicate ? 0 : 1;
                Preset changed = *same;
                changed.params.front().value = -20.0f;
                const fp::PresetStore::ImportResult c = store->importPreset(changed);
                const std::optional<Preset> got = c.ok ? store->get(c.uuid) : std::nullopt;
                fbad += c.ok && c.uuid != f.uuid && got.has_value() && !got->isFactory ? 0 : 1;
            }
            P.eq("presets.file.import.factory_copy", fbad, 0);
        }

        // another product's file
        {
            const juce::String foreign = "<HardwareReverbPreset format=\"1\" plugin=\"HardwareReverb\" uuid=\"x\" "
                                         "name=\"Hall\"><PARAM id=\"thr\" value=\"-10\"/></HardwareReverbPreset>";
            P.eq("presets.file.foreign_rejected", fp::PresetFile::fromXmlString(cfg, foreign).has_value() ? 0 : 1, 1);
        }
    }

    // ---- <PRESET> through the processor's state -------------------------------------------------------------------------
    {
        int muRow = -1;
        for (std::size_t k = 0; k < nBank && muRow < 0; ++k)
            if (const fcdsp::ModeSlot* ms = modeOf(bank[k]); ms != nullptr && ms->key == "mu-67")
                muRow = static_cast<int>(k);
        auto d = running();
        fcmp::PresetAccess& pd = d->presets();
        pd.apply(std::max(muRow, 0));
        const juce::MemoryBlock factoryBlob = save(*d);
        {
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(factoryBlob);
            const juce::XmlElement* preset = xml != nullptr ? xml->getChildByName("PRESET") : nullptr;
            bool ok = preset != nullptr && muRow >= 0
                   && preset->getStringAttribute("uuid") == bank[static_cast<std::size_t>(muRow)].uuid
                   && preset->getBoolAttribute("isFactory");
            int attrs = 0;
            if (preset != nullptr)
                for (const juce::XmlElement* e : preset->getChildWithTagNameIterator("ATTR"))
                    attrs += e->getStringAttribute("key") == "modeId" && e->getStringAttribute("value") == "mu-67"
                                 ? 1 : 0;
            P.eq("presets.state.xml", ok && attrs == 1 ? 1 : 0, 1);
        }
        {
            auto e = running();
            setPlain(*e, Pid::mode, 2.0f);
            const std::uint32_t rev = e->presets().revision();
            load(*e, factoryBlob);
            P.eq("presets.state.factory", e->presets().current() == muRow && !e->presets().modified()
                                                  && e->presets().revision() != rev
                                              ? 1 : 0,
                 1);
        }
        {
            const juce::MemoryBlock userBlob = save(*a);         // a: the user preset "Probe Vocal 2" (saveAs)
            const int userRow = pa.current();
            auto f = running();
            load(*f, userBlob);
            P.eq("presets.state.user",
                 userRow >= static_cast<int>(nBank) && f->presets().current() == indexOfUuid(f->presets(),
                                                                                            juce::String(pa.row(userRow).uuid))
                         && f->presets().current() >= 0 && !f->presets().modified()
                     ? 1 : 0,
                 1);
        }
        {
            setPlain(*d, Pid::thr, -35.0f);                      // modified from the Mu 67 preset
            const juce::MemoryBlock modifiedBlob = save(*d);
            auto g = running();
            load(*g, modifiedBlob);
            P.eq("presets.state.modified_survives", g->presets().current() == muRow && g->presets().modified() ? 1 : 0,
                 1);
        }
        {
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(factoryBlob);
            std::int64_t bad = xml == nullptr ? 1 : 0;
            if (xml != nullptr)
            {
                if (juce::XmlElement* preset = xml->getChildByName("PRESET"))
                    xml->removeChildElement(preset, true);
                auto h = running();
                load(*h, blobOf(*xml));
                fcmp::PresetAccess& ph = h->presets();
                bad += ph.current() == -1 && !ph.modified() ? 0 : 1;
                const float slot = h->rawValue(Pid::mode);
                setPlain(*h, Pid::mode, 0.0f);
                bad += ph.modified() ? 0 : 1;                    // the baseline names the loaded Mode
                setPlain(*h, Pid::mode, slot);
                bad += ph.modified() ? 1 : 0;
                ph.step(+1);
                bad += ph.current() == 0 ? 0 : 1;
                load(*h, blobOf(*xml));
                ph.step(-1);
                bad += ph.current() == ph.count() - 1 ? 0 : 1;
            }
            P.eq("presets.state.no_preset", bad, 0);
        }
        {
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(factoryBlob);
            juce::XmlElement* preset = xml != nullptr ? xml->getChildByName("PRESET") : nullptr;
            std::int64_t bad = preset == nullptr ? 1 : 0;
            if (preset != nullptr)
            {
                preset->setAttribute("uuid", juce::String(kUuidPrefix) + "ff");   // a factory uuid no bank has
                auto s = running();
                load(*s, blobOf(*xml));
                bad += s->presets().current() == -1 ? 0 : 1;
            }
            P.eq("presets.state.stale_factory", bad, 0);
        }
    }

    if (scratchDir.exists())
        scratchDir.deleteRecursively();
    return P.finish();
}
